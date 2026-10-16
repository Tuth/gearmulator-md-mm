// Local diagnostic (not part of the suite): does the MD / MM DSP code use Arithmetic Saturation Mode (SR bit 20, SM)?
// The dsp56300 core here never applies SM (limit_arithmeticSaturation has no caller since dsp56300 a949e03c, 2021, and
// the JIT never tests SRB_SM). Two measurements per DSP: SR sampled every 256 frames of a 20 s run, and every P word
// disassembled with the emulator's own disassembler, listing what writes SR / EOM (data words can match too).
//   smScanProbe <firmware.bin> md|mm
#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/dsp.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
	if(argc < 3) { std::printf("usage: smScanProbe <firmware.bin> md|mm\n"); return 2; }
	const auto model = std::string(argv[2]) == "mm" ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum;
	std::vector<uint8_t> rom;
	if(!baseLib::filesystem::readFile(rom, argv[1]) || !md::RomLoader::isRomForModel(rom, model)) { std::printf("FAIL rom\n"); return 1; }
	auto hw = std::make_unique<md::Hardware>(rom, argv[1], model);

	if(argc == 4 && std::string(argv[3]) == "bench")
	{
		// A/B for the SM patch: boot, then 30 s rendered with notes on 6 channels; wall time of the render
		// window and an FNV hash of every output sample (identical code -> identical audio)
		for(int s = 0; s < 40 && !(hw->isAudioReady() && hw->isFirmwareMidiReady()); ++s) hw->advance(44100 / 2);
		for(int s = 0; s < 10; ++s) hw->advance(44100 / 2);
		std::vector<float> l(256), r(256);
		synthLib::TAudioOutputs outs{};
		outs[0] = l.data(); outs[1] = r.data();
		uint64_t h = 14695981039346656037ull;
		double peak = 0.0;
		const auto t0 = std::chrono::steady_clock::now();
		for(int b = 0; b < 44100 * 30 / 256; ++b)
		{
			if(b % 43 == 0)   // ~4 notes a second, cycling channels 0-5 and pitches
			{
				const uint8_t ch = static_cast<uint8_t>((b / 43) % 6), note = static_cast<uint8_t>(36 + (b / 43) % 24);
				hw->sendMidi({synthLib::MidiEventSource::Host, static_cast<uint8_t>(0x90 | ch), note, 120});
			}
			if(b % 43 == 30)
			{
				const uint8_t ch = static_cast<uint8_t>((b / 43) % 6), note = static_cast<uint8_t>(36 + (b / 43) % 24);
				hw->sendMidi({synthLib::MidiEventSource::Host, static_cast<uint8_t>(0x80 | ch), note, 0});
			}
			hw->processAudio(outs, 256, 0);
			for(int i = 0; i < 256; ++i)
				for(const float v : { l[size_t(i)], r[size_t(i)] })
				{
					uint32_t u; std::memcpy(&u, &v, 4);
					h = (h ^ u) * 1099511628211ull;
					peak = std::max(peak, double(std::abs(v)));
				}
		}
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::printf("bench: 30 s audio in %.0f ms (%.2fx realtime), output hash %016llX, peak %.4f\n", ms, 30000.0 / ms,
			static_cast<unsigned long long>(h), peak);
		return 0;
	}
	md::Dsp* dsps[2] = { &hw->getDspMixer(), &hw->getDspProducer() };
	const char* names[2] = { "dsp1 mixer", "dsp2 producer" };
	uint64_t samples[2] = {}, smOn[2] = {};
	uint32_t srOr[2] = {};
	// where code runs: PC pages (256 words) seen while sampling, so the static scan walks code, not data
	std::vector<uint32_t> pages[2] = { std::vector<uint32_t>(0x8000), std::vector<uint32_t>(0x8000) };
	const uint32_t frames = 44100 * 20;
	for(uint32_t f = 0; f < frames; f += 8)
	{
		hw->advance(8);
		for(int i = 0; i < 2; ++i)
		{
			const uint32_t sr = dsps[i]->dsp().getSR().var;
			++samples[i]; srOr[i] |= sr;
			if(sr & dsp56k::SR_SM) ++smOn[i];
			const uint32_t pc = dsps[i]->dsp().getPC().var & 0x7fffff;
			++pages[i][pc >> 8];
		}
	}
	std::printf("audio ready %d, midi ready %d\n", int(hw->isAudioReady()), int(hw->isFirmwareMidiReady()));
	for(int i = 0; i < 2; ++i)
	{
		auto& d = dsps[i]->dsp();
		std::printf("%s: SR sampled %llu times, SM set in %llu, OR of all SR = %06X\n", names[i],
			static_cast<unsigned long long>(samples[i]), static_cast<unsigned long long>(smOn[i]), srOr[i]);
		dsp56k::Disassembler dis(d.opcodes());
		const auto& mem = d.memory();
		const dsp56k::TWord size = mem.sizeP();
		int hits = 0;
		std::printf("%s code pages (PC seen):", names[i]);
		for(uint32_t pg = 0; pg < 0x8000; ++pg) if(pages[i][pg]) std::printf(" %04X", pg << 8);
		std::printf("\n");
		for(dsp56k::TWord pc = 0; pc < size && hits < 200;)
		{
			// only the 256-word pages code ran in, and their neighbours (cold code next to hot code)
			const uint32_t pg = pc >> 8;
			if(!pages[i][pg] && !(pg > 0 && pages[i][pg - 1]) && !(pg + 1 < 0x8000 && pages[i][pg + 1])) { pc = (pg + 1) << 8; continue; }
			const auto op = mem.get(dsp56k::MemArea_P, pc);
			const auto opB = pc + 1 < size ? mem.get(dsp56k::MemArea_P, pc + 1) : 0;
			std::string s;
			const auto len = dis.disassemble(s, op, opB, 0, 0, pc);
			const dsp56k::TWord at = pc;
			pc += len ? len : 1;
			std::string l = s;
			std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
			const bool eom = l.find("eom") != std::string::npos || l.find("emr") != std::string::npos;
			const auto p = l.find(",sr");
			const bool toSr = p != std::string::npos && (p + 3 == l.size() || !std::isalnum(static_cast<unsigned char>(l[p + 3])));
			const bool bitSr = (l.find("bset") == 0 || l.find("bclr") == 0 || l.find("bchg") == 0) && l.find(",sr") != std::string::npos;
			if(eom || toSr || bitSr) { std::printf("  P:%06X %06X  %s\n", at, op, s.c_str()); ++hits; }
		}
		std::printf("%s: P size %06X, %d SR/EOM-writing candidates\n", names[i], size, hits);
		if(argc == 4 && std::string(argv[3]) == "full")
		{
			// all of P: only what SETS SM (ori #xx,eom with bit 4 / an immediate into SR with bit 20), and only where
			// the 6 words around decode as instructions (a data table decodes as "dc" mostly)
			auto isCode = [&](dsp56k::TWord a) {
				std::string t; dis.disassemble(t, mem.get(dsp56k::MemArea_P, a), mem.get(dsp56k::MemArea_P, a + 1), 0, 0, a);
				return t.rfind("dc", 0) != 0;
			};
			int cand = 0, codeLike = 0;
			for(dsp56k::TWord a = 3; a + 4 < size; ++a)
			{
				const auto op = mem.get(dsp56k::MemArea_P, a);
				const bool oriSM = (op & 0xFF00FF) == 0x0000FB && (op & 0x001000);
				std::string t;
				bool immSR = false;
				if(!oriSM && (op & 0xFFFF00) == 0x05F400)   // move #>imm,<reg>: the second word is the value
				{
					dis.disassemble(t, op, mem.get(dsp56k::MemArea_P, a + 1), 0, 0, a);
					immSR = t.find(",sr") != std::string::npos && (mem.get(dsp56k::MemArea_P, a + 1) & 0x100000);
				}
				if(!oriSM && !immSR) continue;
				++cand;
				bool code = true;
				for(int k = -3; k <= 3 && code; ++k) if(k) code = isCode(a + dsp56k::TWord(k));
				if(!code) continue;
				++codeLike;
				dis.disassemble(t, op, mem.get(dsp56k::MemArea_P, a + 1), 0, 0, a);
				if(codeLike <= 40) std::printf("  SM-set candidate in code-like surroundings P:%06X %06X  %s\n", a, op, t.c_str());
			}
			std::printf("%s full P: %d SM-setting encodings, %d in code-like surroundings\n", names[i], cand, codeLike);
		}
		if(argc >= 5 && i == 0)   // smScanProbe <fw> mm <from> <to>: a plain listing (is it code?), PC-seen counts per page
			for(dsp56k::TWord pc = dsp56k::TWord(std::strtoul(argv[3], nullptr, 16)); pc < dsp56k::TWord(std::strtoul(argv[4], nullptr, 16));)
			{
				std::string s;
				const auto len = dis.disassemble(s, mem.get(dsp56k::MemArea_P, pc), mem.get(dsp56k::MemArea_P, pc + 1), 0, 0, pc);
				std::printf("    P:%06X %06X  %s\n", pc, mem.get(dsp56k::MemArea_P, pc), s.c_str());
				pc += len ? len : 1;
			}
	}
	return 0;
}
