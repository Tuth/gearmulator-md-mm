# DSP56300 manual validation of the dsp56300 submodule (STATE v43, 2026-10-02)

Why this file exists: the DSP56300 Family Manual (DSP56300FM, rev. 5) is a scanned-layout PDF whose text
extraction is not reliable for tables and bit diagrams. Every claim below was checked on the **rendered page
image**, not on extracted text. Page numbers are given as printed page (PDF page).

Method: every opcode word that the Machinedrum and Monomachine DSP firmware actually execute (JIT block coverage
after boot plus notes on all 16 channels: 2,255 unique words, 73 mnemonics) was run as a single instruction from
24 seeded register/SR states, in the JIT and in the interpreter, on this submodule and on the MS2000R emulator's
dsp56300 tree (which carries the upstream dsp56300/dsp56300 fixes). Every difference was checked against the
rendered manual before anything was changed. Tooling is local to the MS2000R project (`_valid/xdiff`).

| Instruction | What the manual says (rendered page) | What this fork did | Fix |
|---|---|---|---|
| Bcc/Jcc/Tcc/... GT, LE | Table 12-18, 12-24 (PDF 238): GT = Z + (N xor V) = 0, LE = Z + (N xor V) = 1 | x64 JIT: GT taken with Z set; LE wrong in 64 CCR states (interpreter, AArch64) | upstream 4bbbed98, a31c1dfb, 5898ebbd |
| NR, NN | Table 12-18, 12-24: NR = Z + (not U and not E) = 1 | a zero accumulator was "not normalized" (128 of 256 CCR states) | upstream f0398f14 |
| SUBR | 13-175 (PDF 415): D is shifted right arithmetically, its MS bit held, then S subtracted | x64 JIT lost the sign (b = $FF7F...: result $003F... instead of $FFBF...) | upstream fa5e0af7 |
| NORMF | 13-147 (PDF 387): C unchanged | C written from the internal ASR/ASL | upstream e32c7059 (adapted to this fork's If() based NORMF) |
| BSET/BCLR/BCHG #n,SR | 13-35 (PDF 275): for SR, C "set if bit 0 is specified, unaffected otherwise" | C taken from the tested bit | upstream 1c0694c8 |
| ADD, SUB, CMP | 13-7 (PDF 247), 13-45 (PDF 285): V and L by the standard definition; Table 5-1 5-16 (PDF 96): V = overflow of the 56-bit result; 5-14 (PDF 94): L set if V is set | V never set (interpreter and both JITs) | ported from upstream 65406f8f (V/L out of line in the JIT) |
| LSL with a register count (x64) | LSL 13-93 (PDF 333): C = last bit shifted out of bit 47, 0 for a zero count | C always 0 | ported from upstream 65406f8f |
| CCR deferred-flag lifetime (interpreter) | — | an explicit CCR write could be overwritten by a pending lazy update | upstream 42611210 (the part this fork lacked) |

Upstream fixes **not** taken, because this fork already had an equivalent: ASL L (004bea1e), x64 ASR carry (0e070bee),
ROL/ROR N from bit 23 (6235d60e), ROR Z order (f6ae8743), NEG/ABS/INC/DEC/ADDL/DIV overflow, Scale-Up U on AArch64
(04d30c02 would have added the base twice here), NORMF register pinning (ff6a2c85).

Upstream fix **rejected**: 14864be3 (AArch64 ASL carry). It assumes a left-aligned accumulator during the shift; this
fork's AArch64 ASL shifts a right-aligned, zero-extended copy, where C at bit 56 is already correct. The upstream
formula reads bit 64-n of that copy (a copy of the sign bit for small n) instead of bit 56-n.

Verification (all on 2026-10-02):
- dsp56kTestRunner: assembler 280/280, JIT, interpreter and optimizer suites pass on x64 (MSVC Release) **and on
  AArch64** (cross-built with GCC, run under qemu-aarch64). New tests: 256-state NR/NN/LE/GT sweep, SUBR, NORMF
  ground truth, BSET on SR (plain and after a deferred CMP), the 55-case `ccrGroundTruth` table.
- joelanders manual-oracle accumulator suite: 13,664 manual cases, 0 failures; differential 7,680/0.
- Single-instruction differential vs the MS2000R tree: interpreter 0 differences; JIT 5 cases in 3 `mac ... ,l:`
  opcodes, where the MS2000R JIT is the outlier (S taken after the ALU op) and this fork agrees with both interpreters.
- MD/MM firmware ctest with the real ROMs (MD UW OS 1.63, MM SFX-60 OS 1.32b): 11/11 passed
  (mmBoot, mdUw, mdRamAudioOracle, mdAudio, mmAudio, mmSine, mmSineMidi, mmInput, mmDigipro, mmDigiproEnsemble, mmSineOracle).

Effect on sound: MD/MM firmware use bge/blt/bgt/ble, tgt/tle, SUBR and ADD/SUB/CMP heavily, so audio can differ from
earlier builds wherever those paths were taken wrongly. The emulation is closer to the hardware rule; listen-test in the host.
