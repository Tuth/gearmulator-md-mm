# Windows MD/MM component and JIT timing handoff

Diagnostic branch: `diagnostics/mm-jit-component-timing-20261016` on
[Tuth/gearmulator-md-mm](https://github.com/Tuth/gearmulator-md-mm).
Based on collaboration snapshot `30ddd9e1e212549b623e77aeac89e86bc03f306c`.
DSP timing hooks: `bbc66c90270d429ceb21a04c2ce5ee4a5103068c` on
[Tuth/dsp56300-md-mm](https://github.com/Tuth/dsp56300-md-mm/tree/diagnostics/mm-jit-timing-20261016).
This branch points `.gitmodules` to that DSP fork, so a fresh recursive checkout
can retrieve the pinned commit directly.

This is measurement code, not a CPU optimization or a replacement release.
Linux headless checks are described below. Windows VST3/standalone compilation
and execution still need to be performed on the Windows machine with VS 2022.
The Linux checks do not establish Windows GUI compilation or audio-driver timing.

## Build on Windows

Use a separate checkout to keep the build configuration and output bundles
separate. Prerequisites: VS 2022 with Desktop development with C++, CMake and Git.
From a VS 2022 developer PowerShell:

```powershell
git clone --branch diagnostics/mm-jit-component-timing-20261016 https://github.com/Tuth/gearmulator-md-mm.git gearmulator-mdmm-timing
cd gearmulator-mdmm-timing
git submodule update --init --recursive
cmake -S . -B temp/cmake_win64_timing -G "Visual Studio 17 2022" -A x64 -Dgearmulator_DIAGNOSTICS=ON -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF -Dgearmulator_SYNTH_OSIRUS=OFF -Dgearmulator_SYNTH_OSTIRUS=OFF -Dgearmulator_SYNTH_VAVRA=OFF -Dgearmulator_SYNTH_XENIA=OFF -Dgearmulator_SYNTH_NODALRED2X=OFF -Dgearmulator_SYNTH_JE8086=OFF -Dgearmulator_SYNTH_ELEKTRON=ON
cmake --build temp/cmake_win64_timing --config Release --target mmJucePlugin_VST3 mdJucePlugin_VST3 mmJucePlugin_Standalone mdJucePlugin_Standalone --parallel 4
```

Check every command's exit code. Keep the CMakeCache, DLL/PDB files and source
revision with the diagnostic package. CMake 4 may need
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for legacy dependency configuration.
The VST3 bundles are under `bin/plugins/Release/VST3/`.
Use the complete diagnostic bundle in the host, avoiding a duplicate installed
copy of the same plugin. No firmware is included in this branch/package.

## Capture

1. Keep host sample rate, block size, connected outputs and DSP clock unchanged
   from the clean reference capture (48 kHz, 1000 frames, six outputs, 100%).
2. Use Performance diagnostics -> Start performance capture. No extra profiling
   environment variables or `mdmm_perf.enable` file are required for this report.
3. Record about 60 seconds using the same kit/pattern: a settled section followed
   by the track/engine changes that previously produced compilation spikes.
4. Stop the capture and retain its `performance-*.jsonl`. The header must say
   schema 3 and `component_timing_available: "true"`, with the diagnostic revision.
5. Analyze with `python tools/mdmm-core-benchmark/analyze_capture.py CAPTURE.jsonl`.

The existing report worker writes files off the emulation/audio thread.
Callback paths use bounded queues, timestamps and counters; no new file I/O,
mutex or allocation is introduced in those paths. The JIT itself still allocates
as before. Component scopes are compiled only with diagnostics ON and clocked
only on every 16th captured callback. JIT emission batches are timed on every
captured callback; recursive child generation is counted once in its outer batch.

MCU/DSP buckets are mutually exclusive within sampled callbacks, including inline
DSP catch-up nested in MCU processing. They include supporting work inside the
scopes, not pure instruction-engine CPU samples. Other scheduler/output work
remains unassigned. Summary component times apply only to the sampled population;
use `componentSampledCallbackCount/Nanoseconds` and `componentTimingSampled`.
Periodic sampling can alias periodic workloads. JIT times overlap component times
and must not be added to them. The maximum is an outer compilation-batch maximum,
not necessarily one DSP block. All these are wall times, including preemption.

Clock reads and reporting perturb timing. An early single paired native trial of
sampled capture used about 4.7% more render CPU time than capture off; this is an
orientation, not a stable overhead estimate or a Windows number. A dense,
every-callback component prototype was more expensive and was replaced with
sampling. Compare diagnostic timing to capture-off behaviour before interpreting
new deadline misses as production failures. No extra audio buffering or machine
clock changes are part of this work.

## Validation

- Native Release headless build with diagnostics ON succeeded.
- Realtime instrumentation and performance report tests pass, including nested
  MCU/DSP accounting, recursive JIT timing, deferred timing, sampling and reset.
- DSP assembler, JIT, interpreter and optimizer test suites pass. The hook test
  checks that cached execution is outside compilation timing and a zero start
  token suppresses the end hook.
- MM: capture-on/off complete PCM and both DSP cycle/PC boundaries match. The
  3-second-per-phase PCM SHA-256 also equals the earlier unmodified `30ddd9e1`
  capture at both bounded-catch-up settings.
- MD: the short capture-on/off firmware render also matches complete PCM and
  DSP cycle/PC boundaries.
- The final short MM capture retains all callbacks, all component samples and all
  JIT timing batches; callback/summary fields reconcile. Raw firmware and captures
  remain private and are not committed.

The first Linux sample identifies costs in MCU and both DSP scopes; it does not
justify attributing the Windows 53 ms callback until a Windows diagnostic capture
is available. The next decision is whether JIT emission dominates those specific
spikes, and which component dominates settled rendering on the user's host.
