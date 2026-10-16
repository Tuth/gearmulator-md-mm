# Firmware-backed MD/MM core benchmark

This headless target builds the existing emulation libraries without JUCE or the
GUI dependencies. It measures unpaced core capacity at 44.1 kHz with 128-frame
blocks and six connected output channels. It does not measure a VST3 host,
resampling, a Windows audio driver, or realtime wake-up/deadline behaviour.

Initialize the pinned DSP, mc68k, and nested asmjit submodules first:

```sh
git submodule update --init source/dsp56300 source/mc68k
git -C source/dsp56300 submodule update --init source/asmjit
cmake -S tools/mdmm-core-benchmark -B build/core-benchmark -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-benchmark --target mdmmCoreBenchmark --parallel 4
build/core-benchmark/mdmmCoreBenchmark md /private/md-1.63.bin 3 build/md.f32
build/core-benchmark/mdmmCoreBenchmark mm /private/mm-1.32b.bin 3 build/mm.f32
```

With CMake 4, add `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` when configuring the
legacy dependencies. The default compiler flags come from the parent project's
`base.cmake`; no native CPU tuning or extra LTO is enabled by this target.

Both images must match the identities already enforced by `RomLoader`. Firmware
is read in place. On a fresh MD, first-run flash initialization and a cold reboot
finish before measuring. Initialization is private in memory: no ROM, flash, kit,
NVRAM or project file is saved or changed on disk. The temporary MD kit assigns
TRX IDs 16..28 with all pages initialized, then sets levels and unmutes tracks.
The assignment follows Appendix C of the manufacturer's OS 1.63 manual:
https://www.elektron.se/wp-content/uploads/2024/09/machinedrum_manual_OS1.63.pdf

Phases are idle (0), repeated note/trigger bursts (1), and a note-off interval (2).
MM uses the fresh firmware-initialized kit and triggers all six MIDI channels.
MD triggers all sixteen tracks. A silent active phase fails. This is one
reproducible synthetic workload, not coverage of every synthesis engine or kit.

JSON records (lines beginning with `{`; dependencies may also log startup text)
include render-only wall/process CPU time, p50/p99 callback durations, frame
counts, RMS, a canonical little-endian FNV-1a hash of all six channels, and both
DSP cycle counters and PCs. CPU time is process time, not an IP-sample estimate.
Timing wraps only `Hardware::processAudio`; MIDI/panel submission, hashing and
optional file writes happen outside the timed interval. Initialization, warm-up,
JIT compilation during boot and output allocation are excluded, but first use of
an active voice may still compile code during the measured active phase.

The optional PCM output contains native float32 samples in block-major,
channel-major order (128 samples/channel/block, six channels). For each phase,
the frame count rounds up to a complete 128-frame block. Compare complete files
byte-for-byte, plus boot/frame counts, cycle counters and PCs, between preserved
baseline and candidate executables. Use SHA-256 separately when recording file
identities. FNV is a convenient regression checksum, not a cryptographic digest.

Run controls and candidates sequentially on the same CPU with identical firmware,
flags and duration. Repeat in alternating order before claiming a gain. CPU
percent normalized to realtime is `renderCpuSeconds / audioSeconds * 100`, in
units of one logical CPU. These Linux core measurements do not establish a
Windows standalone/VST3 speedup or audio-driver stability.

Automated alternating A/B comparison (temporary PCM files are removed after full
SHA-256 and clock-boundary comparison; logs and results remain private):

```sh
python3 tools/mdmm-core-benchmark/compare.py /path/to/control /path/to/candidate \
  md /private/md-1.63.bin build/md-comparison --repeats 3 --seconds 3 --cpu 2
```

Omit `--cpu` outside Linux. Select an available CPU rather than assuming CPU 2.
The output directory must not already exist. Controls and candidates must be
built with the same benchmark source and compiler settings. This script fails on
any full PCM SHA-256, boot/frame count, DSP-cycle or DSP-PC mismatch. Its JSON
summary identifies both executable hashes and the firmware hash.

For runtime-switch comparisons, pass the same executable twice and explicit
per-process overrides, for example `--control-env
GEARMULATOR_MDMM_BOUNDED_CATCHUP=0 --candidate-env
GEARMULATOR_MDMM_BOUNDED_CATCHUP=1`. Overrides are recorded in `results.json`;
other environment variables are inherited. These switches must exist in the
tested source revision. The older `ab69ad0` baseline does not implement this
catch-up switch. Do not compare that older executable to the current snapshot
and attribute all differences to one switch.

The focused firmware test targets are `mdAudioFirmwareTest`,
`mmAudioFirmwareTest`, and `idleSchedulerFirmwareTest`. Configure with
`-DMD_TRANSPORT_DIAGNOSTICS=ON` to additionally build
`transportScorecardFirmwareTest`; both control and candidate must use that same
setting if comparing performance. Test binaries take their private fixtures from
`GEARMULATOR_MD_FIRMWARE_BIN` and `GEARMULATOR_MM_FIRMWARE_BIN`. The DSP's existing
`dsp56kTestRunner` target exercises bounded-dispatch correctness independently.

The MD active phase deliberately combines mapped host notes and panel trigger
bursts. The final panel burst can extend across the phase boundary; phase 2 stops
new triggers and sends note-offs, but does not assert that every MD panel key has
been released. Do not interpret its time as a universal all-voices-idle baseline.
The six-run historical review and rejected candidates are documented in
[the review notes](../../doc/mdmm-performance-review-2026-10-16.md).

Analyze an existing product JSONL capture (no emulator or firmware execution):

```sh
python3 tools/mdmm-core-benchmark/analyze_capture.py /private/performance.jsonl
```

The analyzer checks full-trace totals against the cumulative summary before
reporting inclusive wall timings. `traceComplete` distinguishes complete traces
from selected callback samples; percentiles on incomplete traces describe only
those selected records. Callback budget use is not per-thread CPU utilization.

For exploratory Linux native call counts, configure a separate `-pg` build with
`-DCMAKE_CXX_FLAGS=-pg -DCMAKE_C_FLAGS=-pg -DCMAKE_EXE_LINKER_FLAGS=-pg` and run
with `GEARMULATOR_CORE_GPROF=1`. On glibc, the harness disables collection during
boot/settle and enables it for each entire phase (including harness hashing and
I/O outside render timings). gprof's percentages omit generated JIT text and do
not describe the complete emulator. Use an ordinary, non-profiled build for A/B
performance checks. The MIDI unit test is available as `mdTurboMidiUnitTest`.
