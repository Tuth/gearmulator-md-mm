#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdautomation.h"
#include "baseLib/filesystem.h"
#include "synthLib/performanceReport.h"
#include <cstdlib>
#include <thread>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
double processCpuSeconds() {
#ifdef _WIN32
  FILETIME created{}, exited{}, kernel{}, user{};
  if(!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    throw std::runtime_error("GetProcessTimes failed");
  const auto ticks = [](FILETIME t) {
    return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  };
  return double(ticks(kernel) + ticks(user)) * 1e-7;
#else
  const auto ticks = std::clock();
  if(ticks == std::clock_t(-1)) throw std::runtime_error("process CPU clock unavailable");
  return double(ticks) / CLOCKS_PER_SEC;
#endif
}
}

// Unpaced core capacity, not host/driver latency. Filesystem I/O is outside
// callback and aggregate render timings. Private firmware is never copied.
int main(int argc, char** argv) try {
  if(argc < 3 || argc > 5 || (std::string(argv[1]) != "md" && std::string(argv[1]) != "mm")) {
    std::cerr << "usage: mdmmCoreBenchmark md|mm ROM.bin [seconds-per-phase=4] [output.f32]\n";
    return 2;
  }
  const auto model = std::string(argv[1]) == "mm" ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum;
  const unsigned seconds = argc >= 4 ? std::stoul(argv[3]) : 4;
  if(seconds < 1 || seconds > 120) throw std::runtime_error("phase duration must be 1..120 seconds");
  std::vector<uint8_t> rom;
  if(!baseLib::filesystem::readFile(rom, argv[2]) || !md::RomLoader::isRomForModel(rom, model))
    throw std::runtime_error("unsupported firmware identity");
  auto hw = std::make_unique<md::Hardware>(rom, argv[2], model);
  constexpr uint32_t block = 128;
  uint32_t bootFrames = 0;
  while(!hw->isFirmwareMidiReady() && bootFrames < md::g_samplerate * 30) {
    hw->advance(block); bootFrames += block;
  }
  if(!hw->isFirmwareMidiReady()) throw std::runtime_error("boot timeout");
  if(model == md::MachineModel::Machinedrum) {
    while(!hw->isFactoryFlashReadyForReboot() && bootFrames < md::g_samplerate * 30) {
      hw->advance(block); bootFrames += block;
    }
    if(!hw->isFactoryFlashReadyForReboot()) throw std::runtime_error("factory initialization timeout");
    auto flash=hw->copyFlashData(); auto ram=hw->copyPatchRam();
    hw.reset();
    hw=std::make_unique<md::Hardware>(rom, argv[2], model, ram, nullptr, flash);
    uint32_t rebootFrames=0;
    while(!hw->isFirmwareMidiReady() && rebootFrames < md::g_samplerate * 30) {
      hw->advance(block); rebootFrames += block;
    }
    bootFrames += rebootFrames;
    if(!hw->isFirmwareMidiReady()) throw std::runtime_error("initialized boot timeout");
  }
  for(unsigned i=0; i < md::g_samplerate * 2; i += block) hw->advance(block);
  if(model == md::MachineModel::Machinedrum) {
    // OS 1.63 manual, Appendix C: SPS-1 TRX machine IDs 16..28, init all pages.
    for(uint8_t t=0;t<16;++t) {
      synthLib::SMidiEvent assign(synthLib::MidiEventSource::Host);
      assign.sysex={0xf0,0,0x20,0x3c,2,0,0x5b,t,static_cast<uint8_t>(16+t%13),0,2,0xf7};
      if(!hw->sendMidi(assign)) throw std::runtime_error("TRX machine assignment rejected");
      for(unsigned i=0;i<8192;i+=block) hw->advance(block);
      for(auto page : {md::automation::machinedrum::Level, md::automation::machinedrum::Mute}) {
        const auto cc=md::automation::encodeParameterChange(model,{static_cast<uint8_t>(page),t,0,static_cast<uint8_t>(page==md::automation::machinedrum::Level?100:0)},0);
        if(!cc || !hw->sendMidi({synthLib::MidiEventSource::Host,(*cc)[0],(*cc)[1],(*cc)[2]}))
          throw std::runtime_error("track level/mute rejected");
      }
      for(unsigned i=0;i<8192;i+=block) hw->advance(block);
    }
  }
  std::array<std::array<float, block>, 6> audio{};
  synthLib::TAudioOutputs outputs{};
  for(unsigned c=0;c<audio.size();++c) outputs[c]=audio[c].data();
  std::ofstream pcm;
  if(argc == 5) { pcm.open(argv[4], std::ios::binary); if(!pcm) throw std::runtime_error("cannot open PCM output"); }
  synthLib::RealtimeInstrumentation instrumentation;
  instrumentation.setEnabled(false);
  synthLib::PerformanceReport report(instrumentation);
  if(const auto* capture = std::getenv("GEARMULATOR_CORE_CAPTURE"); capture && *capture) {
    synthLib::PerformanceReport::Limits limits;
    limits.flushInterval = std::chrono::milliseconds(50);
    report.start(capture, {{"product", "MD/MM headless core"}, {"model", argv[1]},
      {"component_timing_available", GEARMULATOR_DIAGNOSTIC_LOGGING ? "true" : "false"},
      {"component_timing_period", std::to_string(synthLib::RealtimeInstrumentation::ComponentSamplingPeriod)}}, limits);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(report.status()==synthLib::PerformanceReport::Status::Starting
      && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(report.status()!=synthLib::PerformanceReport::Status::Recording) throw std::runtime_error("capture start failed");
  }
  for(unsigned phase=0;phase<3;++phase) {
    uint64_t hash=14695981039346656037ull;
    double power=0, renderSeconds=0, renderCpuSeconds=0;
    std::vector<double> durations;
    const unsigned blocks=(md::g_samplerate * seconds + block - 1)/block;
    durations.reserve(blocks);
    // Note bursts exercise all six MM tracks or all sixteen MD instruments.
    md::PanelRowState rows;
    for(unsigned i=0;i<blocks;++i) {
      if(phase == 1 && model == md::MachineModel::Machinedrum && (i % 172 == 0 || i % 172 == 8)) {
        for(unsigned t=0;t<16;++t) {
          const auto key=md::panelPacket(model, static_cast<md::PanelControl>(t));
          if(!key) throw std::runtime_error("missing trigger mapping");
          const auto packet=i%172==0?rows.press(*key):rows.release(*key);
          if(!hw->trySendPanelEvent(packet.row,packet.mask)) throw std::runtime_error("panel admission failed");
        }
      }
      if(phase && i % 172 == 0) {
        const unsigned tracks=model==md::MachineModel::Monomachine?6:16;
        for(unsigned t=0;t<tracks;++t) {
          const auto status=static_cast<uint8_t>((phase==1?0x90:0x80) | (model==md::MachineModel::Monomachine?t:0));
          const auto note=static_cast<uint8_t>(model==md::MachineModel::Monomachine?60:36+t);
          if(!hw->sendMidi({synthLib::MidiEventSource::Host,status,note,static_cast<uint8_t>(phase==1?100:0)}))
            throw std::runtime_error("MIDI admission failed");
        }
      }
      const auto cpu=processCpuSeconds();
      const auto start=std::chrono::steady_clock::now();
      {
        synthLib::RealtimeInstrumentation::CallbackScope callback(instrumentation,block,md::g_samplerate);
        callback.setActiveOutputLayout(3,6);
        synthLib::RealtimeInstrumentation::setCurrentDeviceContext(md::g_samplerate,0,100);
        hw->processAudio(outputs,block,0);
      }
      const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
      renderSeconds+=elapsed;
      renderCpuSeconds+=processCpuSeconds()-cpu;
      durations.push_back(elapsed);
      for(const auto& channel:audio) for(float sample:channel) {
        if(!std::isfinite(sample)) throw std::runtime_error("nonfinite output");
        power+=double(sample)*sample;
        uint32_t bits; std::memcpy(&bits,&sample,4);
        for(unsigned b=0;b<4;++b) { hash^=(bits>>(8*b))&255; hash*=1099511628211ull; }
      }
      if(pcm.is_open()) for(const auto& channel:audio)
        pcm.write(reinterpret_cast<const char*>(channel.data()),sizeof(channel));
    }
    if(pcm.is_open() && !pcm) throw std::runtime_error("PCM write failed");
    if(phase == 1 && power < 1e-10) throw std::runtime_error("active benchmark produced silence");
    std::sort(durations.begin(),durations.end());
    auto& dsp1=hw->getDspMixer().dsp(); auto& dsp2=hw->getDspProducer().dsp();
    const double audioSeconds=double(blocks)*block/md::g_samplerate;
    std::cout << "{\"model\":\""<<argv[1]<<"\",\"phase\":"<<phase<<",\"bootFrames\":"<<bootFrames
      <<",\"frames\":"<<blocks*block<<",\"audioSeconds\":"<<audioSeconds<<",\"renderWallSeconds\":"<<renderSeconds
      <<",\"renderCpuSeconds\":"<<renderCpuSeconds<<",\"p50us\":"<<durations[blocks/2]*1e6
      <<",\"p99us\":"<<durations[std::min<size_t>(blocks-1,blocks*99/100)]*1e6
      <<",\"rms\":"<<std::sqrt(power/(blocks*block*audio.size()))<<",\"pcmFnv64\":\""<<std::hex<<hash<<std::dec
      <<"\",\"dsp1Cycles\":"<<dsp1.getCycles()<<",\"dsp2Cycles\":"<<dsp2.getCycles()
      <<",\"dsp1Pc\":"<<dsp1.getPC().var<<",\"dsp2Pc\":"<<dsp2.getPC().var<<"}"<<std::endl;
  }
  report.stop();
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
