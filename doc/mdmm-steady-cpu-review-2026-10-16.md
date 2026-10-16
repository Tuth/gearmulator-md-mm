# Steady MD/MM CPU review, 2026-10-16

Two prototypes were checked against snapshot `30ddd9e1` and DSP `32d7febb`.
Neither produced a consistent speedup. All production source and the DSP pin
are restored to that snapshot. This review does not provide an optimized release.
Private firmware, PCM and raw captures are not committed.

| Prototype | MM control median CPU seconds | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| Serial clock deadline cache | 8.01726 | 8.01227 | 0.06% |
| Inline MIDI wire ownership query | 7.92781 | 8.16885 | -3.04% |

Each MM comparison has three alternating pairs, CPU affinity 2, GCC 13.3.0 Linux
x64 Release, three 3-second phases. Full PCM SHA-256, boot/frame counts and both
DSP cycle/PC boundaries match in every run. The tiny/noisy clock result and the
mixed inline result do not justify retaining either change. Neither establishes
Windows performance. The MD clock smoke pair likewise matches full PCM and DSP
boundaries; its timing overlaps compilation and is not a performance result.

The serial cache was opt-in within the generic DSP library and enabled by the
prototype host. It reused the known earliest serial edge between due events,
with generation invalidation for register/clock/phase updates, including changes
from inside TX callbacks. Its differential test passed 5000 varied time steps,
checking TX/RX timestamps/order, base phase and both returned deadline domains.
The full prototype and test are retained only as rejected patches.

The native gprof build produced useful function call counts, with boot and
settle excluded using moncontrol. The MM fixture made over 106 million calls to
TurboMidiTransfer::ownsMidiWire over the three render phases. Moving its unchanged
acquire-load/state query into the class header removed the out-of-line symbol in
the candidate executable, but the normal uninstrumented A/B measurements still
showed no consistent benefit. The existing mdTurboMidiUnitTest passed with the
candidate. This is why call counts alone are insufficient to select a speedup.

Generated JIT code and shared-library execution are not fully covered by gprof's
native text histogram. Its percentages are not whole-emulator CPU shares, and
instrumentation perturbs cost. Next collect Windows sampled CPU stacks with
matching PDBs on the clean Release; do not add more per-slice clocks.

Data: [clock](performance/mm-serial-clock-cache-2026-10-16.json),
[inline](performance/mm-midi-wire-inline-2026-10-16.json),
[native call counts](performance/mm-native-call-counts-2026-10-16.json).
The MD JSON is a correctness smoke record only.

Rejected patch reproduction (test checkout only): apply the serial DSP patch
inside source/dsp56300 at 32d7febb, and its parent patch at 30ddd9e1; alternatively
apply the independent MIDI inline patch to the parent at 30ddd9e1. Do not apply
the different experiments together. No ROM or user state is needed to read them.
