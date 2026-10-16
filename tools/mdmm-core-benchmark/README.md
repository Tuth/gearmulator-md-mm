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

## Diagnostic timings

Configure with `-Dgearmulator_DIAGNOSTICS=ON` and run with
`GEARMULATOR_CORE_CAPTURE=/absolute/path/capture.jsonl`. The benchmark starts the
existing off-thread report writer after boot/settle and scopes each render block.
An unset or empty variable disables capture. This diagnostic run is not a normal
performance baseline: clock reads, callback counters and file writing add cost.
The capture uses schema 3. `analyze_capture.py` accepts old schema-2 recordings as
well; a selected callback trace need not contain every ordinary callback.

MCU/DSP component scopes are exclusive of nested components and sampled every
16th callback, starting with the first. Each sampled callback is retained in the
trace. Summary component time applies only to those callbacks, whose count and
outer duration are reported separately. It is not total CPU time or a per-thread
profile. Inline DSP catch-up is charged to the DSP bucket, even inside an MCU
slice. Scheduler selection and output mapping outside the scopes remain outside
these buckets. Periodic sampling can miss brief workloads or correlate with
periodic activity; do not extrapolate unobserved callbacks as measured totals.

JIT emission/installation is timed for every compilation during an enabled host
callback, regardless of component sampling. Recursive child generation is counted
once in the outer compilation batch. The batch maximum is not a single-block
compiler maximum. JIT times overlap the component scopes and must not be added
to their sum. Already generated machine-code execution is excluded. Release
plugins (`gearmulator_DIAGNOSTICS=OFF`) do not install these timing hooks or
component scopes; generic report fields remain available with zero timing values.
