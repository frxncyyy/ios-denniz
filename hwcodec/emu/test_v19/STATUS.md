# V19 — NEON shifts by immediate, widening/narrowing, scalar forms

Status: **implemented and host-tested** (ASan+UBSan -O1 and plain -O2). Not run on the phone
(no phone access for workers). No vendor file copied; nothing published.

## Test command and last lines

    $ bash gpu/hwcodec/emu/test_v19/run_tests.sh
    gen_vectors: 495 encodings, 3960 vectors
    sweep: 52640 OK, 147360 UNDEF, 200000 total
    N2TEST V19 PASS 44435/44435

The runner builds twice under `/tmp/n2hw-V19/` (clang `-O1 -fsanitize=address,undefined
-fno-sanitize-recover=all` and `-O2` without sanitizers), runs both, requires identical summary
lines. 44435 checks = 3960 generated vectors x (10 D registers + FPSCR) plus hand-worked
edge cases, 22 must-be-UNDEF encodings (each also checked to leave all 32 D registers and FPSCR
unchanged) and the 200000-word sweep. The sweep reports 52640 OK / 147360 UNDEF; both counts are
non-zero by design (the sweep covers the whole `F2/F3` bit23=1 space, including the V20 spaces).

## Files

    emu/n2a32_neon_shift.c          the class function (MIT)
    emu/test_v19/run_tests.sh       test runner (builds only under /tmp/n2hw-V19)
    emu/test_v19/gen_vectors.py     llvm-mc encoding generator + ARM ARM pseudocode model
    emu/test_v19/t_v19.c            direct class tests, UNDEF tests, 200000-word sweep
    emu/test_v19/k19_neon_shift.c   differential kernels for T01
    emu/test_v19/STATUS.md          this file
    dsh-state/workers/V19/STATUS.md handoff copy

## Implemented mnemonics (all forms listed)

Two registers and a shift amount (`bit4=1`, excluding the modified-immediate space):

| Mnemonic | Types implemented | Notes |
|---|---|---|
| VSHR | .s8/.u8/.s16/.u16/.s32/.u32/.s64/.u64, D and Q | logical/arithmetic, shift 1..esize |
| VSRA | same | accumulate, wrap |
| VRSHR | same | add 2^(shift-1) then shift |
| VRSRA | same | accumulate rounded shift |
| VSRI | .8/.16/.32/.64, D/Q | keep the most significant `shift` destination bits; insert `src >> shift` into the low bits |
| VSHL (immediate) | .i8/.i16/.i32/.i64, D/Q | shift 0..esize-1 |
| VSLI | .8/.16/.32/.64, D/Q | keep the least significant `shift` destination bits; insert `src << shift` into the high bits |
| VQSHL | .s8/.u8/.s16/.u16/.s32/.u32/.s64/.u64, D/Q | saturating, QC |
| VQSHLU | .s8/.s16/.s32/.s64, D/Q | signed input, unsigned result, QC |
| VSHRN | .i16/.i32/.i64 | logical, narrow to half |
| VRSHRN | .i16/.i32/.i64 | rounding narrow |
| VQSHRN | .s16/.u16/.s32/.u32/.s64/.u64 | saturating narrow, QC |
| VQRSHRN | same | rounding saturating narrow, QC |
| VQSHRUN | .s16/.s32/.s64 | signed in, unsigned out, QC |
| VQRSHRUN | .s16/.s32/.s64 | rounding variant, QC |
| VSHLL | .s8/.u8/.s16/.u16/.s32/.u32 | widening shift, shift 1..src-1 |
| VMOVL | .s8/.u8/.s16/.u16/.s32/.u32 | VSHLL #0 |
| VCVT.S32.F32 / VCVT.U32.F32 | D/Q, fbits 1..32 | float -> fixed, truncate toward zero, saturate, QC |
| VCVT.F32.S32 / VCVT.F32.U32 | D/Q, fbits 1..32 | fixed -> float, round to nearest even |

Three registers of different lengths (`bit4=0`, bit6=0):

| Mnemonic | Types implemented |
|---|---|
| VADDL / VSUBL | .s8/.u8/.s16/.u16/.s32/.u32 |
| VADDW / VSUBW | .s8/.u8/.s16/.u16/.s32/.u32 |
| VADDHN / VRADDHN / VSUBHN / VRSUBHN | .i16/.i32/.i64 |
| VABAL / VABDL | .s8/.u8/.s16/.u16/.s32/.u32 |
| VMLAL / VMLSL | .s8/.u8/.s16/.u16/.s32/.u32 |
| VQDMLAL / VQDMLSL / VQDMULL | .s16/.s32, QC |
| VMULL | .s8/.u8/.s16/.u16/.s32/.u32 |
| VMULL.P8 | 8-bit polynomial |

Two registers and a scalar (`bit4=0`, bit6=1):

| Mnemonic | Types implemented |
|---|---|
| VMLA / VMLS / VMUL (integer) | .i16/.i32, D/Q, lanes 0..3 (16-bit) / 0..1 (32-bit) |
| VMLAL / VMLSL / VMULL | .s16/.u16/.s32/.u32, all lanes |
| VQDMLAL / VQDMLSL / VQDMULL | .s16/.s32, all lanes, QC |
| VQDMULH / VQRDMULH | .s16/.s32, D/Q, all lanes, QC |
| VMLA.F32 / VMLS.F32 / VMUL.F32 | D/Q, lanes 0/1, standard FPSCR |

QC is written to `c->fpscr` bit 27 (same convention as V18's work package). NEON float uses the
standard FPSCR value: round to nearest even, default NaN (`0x7FC00000`), flush denormals to zero;
VMLA/VMLS are two separately rounded operations (not fused).

## Decode and routing

The core converts T32 `111U 1111 ...` to `1111 001U ...` and calls this function with the A32
form. The exact split for `bit23=1` (posted to V02 and applied verbatim in `emu/n2a32_core.c`):

    bit23==0                                        -> neon_3same (V18)
    bit4==1, bits21-19==0, bit7==0                  -> neon_misc (V20, one-reg modified immediate)
    bit4==1 otherwise                               -> neon_shift (V19)
    bit4==0, bits21-20==3                           -> neon_misc (V20, two-reg misc)
    bit4==0 otherwise                               -> neon_shift (V19: bit6=0 3-diff, bit6=1 scalar)

`imm7 = (bit7 << 6) | bits21-16`; the element size is identified by the ranges [8,16)/[16,32)/
[32,64)/[64,128), which are disjoint because of the architecturally allowed shift ranges.

## Deliberately N2A32_UNDEF (all leave the CPU state unchanged)

* Everything outside `(insn & 0xFE000000) == 0xF2000000` or with bit23==0 (3-same, VFP, load/store,
  branches, ...).
* `bit4=1, bits21-19==0, bit7==0`: one register + modified immediate (VMOV/VMVN/VORR/VBIC
  immediate) — V20.
* `bit4=0, bits21-20==3`: two registers, miscellaneous (VMOVN/VQMOVN/VQMOVUN/VREV*/VPADDL/
  VPADAL/VCLS/VCLZ/VCNT/VMVN/VQABS/VQNEG/VCGT/VCGE/VCEQ/VCLE/VCLT #0/VABS/VNEG/VSWP/VTRN/
  VUZP/VZIP/VSHLL maximum shift) — V20.
* VEXT (V20).
* Shift opc 0xb/0xc/0xd (unallocated); VSRI with U=0; VQSHLU with U=0.
* VSHLL with bit6=1 or an odd destination D number.
* VCVT fixed with `imm7` outside [32,63] (fbits outside 1..32).
* Narrowing with `imm7` outside [8,63] or an odd source Q D number.
* 3-diff/scalar with size==3; scalar with size 0 or 3; VQDMLAL/VQDMLSL/VQDMULL with size 0 or U=1;
  VMULL.P8 with U=1 or size!=0; 3-diff opc 0xf; odd Q destination/source numbers; VADDW/VSUBW
  with odd Qn; VADDHN/VRADDHN/VSUBHN/VRSUBHN with odd Qn/Qm.
* Scalar VMLA/VMLS/VMUL integer with size 0/3; float scalar with size!=2; VQDMULH/VQRDMULH with
  size 0/3; scalar VQDMLAL/VQDMLSL/VQDMULL with U=1; scalar long forms with odd Qd.
* Every other unallocated encoding in the class space.

## Tests: what they actually cover

* `gen_vectors.py` assembles 495 encodings with llvm-mc (no hand-typed encoding hex) covering every
  mnemonic/type/width/lane/rounding variant above, and computes 8 expected vectors per encoding
  from a separate Python model written from the ARM ARM pseudocode (edge values 0, -1, INT_MIN,
  INT_MAX, 0x80.., 0x0101.., plus deterministic pseudo-random values). `t_v19.c` sets D0..D9,
  calls the class function directly on a `struct n2a32_cpu` whose `mem->base` is a 16 MiB calloc,
  and compares all ten D registers and FPSCR exactly.
* Hand-worked literal cases for the tricky semantics: VSRI/VSLI kept-half placement (R1 F10: VSRI keeps the
  destination's most significant `shift' bits and inserts `src >> shift' into the low bits; VSLI keeps
  the destination's least significant `shift' bits and inserts `src << shift' into the high bits;
  VSLI #0 is the source), rounding carries (VRSHR/VRSHRN), saturation
  and QC (VQSHL/VQSHLU/VQSHRN/VQSHRUN/VQDMULL/VQDMLAL/VQDMLSL/VQDMULH/VQRDMULH), VSRI/VSLI
  insert with shift 0/esize, VSHLL/VMOVL sign extension, VCVT truncation and saturation, float
  default NaN and flush-to-zero, scalar lane 0/max, Q-form register selection, and a check that
  r[], r15, next_pc, thumb and in_it are not modified.
* Stress run (not part of run_tests.sh): the same generator with 32 vectors per encoding gives
  15840 vectors and `N2TEST V19 PASS 175115/175115` under ASan+UBSan, no mismatch.
* 22 must-be-UNDEF encodings, each checked to leave all 32 D registers and FPSCR unchanged.
* A 200000-word sweep over the `F2/F3` bit23=1 space: every word either executes or returns
  `N2A32_UNDEF`; on UNDEF the D registers and FPSCR are unchanged. Run under ASan+UBSan.
* No r15/sp operands are legal in this class (NEON has no core-register operands); the
  register-preservation case checks that the function does not touch them.
* The census's `vshrlt.u32` case: the T32 word `fffa301a` (inside `it lt`) converts to the A32
  form `f3fa301a`; the hand test feeds that form directly and checks the shifted result. The
  condition is the core's ITSTATE, not an A32 cond field.

## Not done / open

* Not run on the phone; no hardware execution. Expected values are the ARM ARM pseudocode model
  and hand-worked literals, not silicon.
* Not compiled for arm64 iOS yet (the file is portable C11; it needs libm for `fegetround`/
  `fesetround`, which libSystem provides).
* The T01 differential kernels `emu/test_v19/k19_neon_shift.c` are written and compile clean for
  armv7a NEON (they generate `vshr.s16/s32`, `vshrn.i16/i32`, `vaddw.u16`, `vshll.s16`,
  `vmovl.s16`, plus V18/V20 instructions). With V18/V20 now in the tree, a local harness run with
  only the k19 kernels still reports differing calls in the arm rows, but the V19 instructions are
  not the cause: for seed 0 I fed the kernels' exact data through `n2a32_neon_shift` and reproduced the
  host hashes for the V19 ops (`VADDW.U16`+`VSHRN.I32`: 2c610a32; `VMLAL.S16`: d16dae9d), while
  the ARM rows differ in the V18/V20 setup (`vdup`/`vld1`/`vshl` register/`vmovn`/`vmla`/
  `vmax`/`vcgt`). `k19_narrow`, `k19_shift_imm`, `k19_narrow_sat`, `k19_scalar_mul` pass in the
  arm/armv8 rows; the thumb-O0 rows stop at T32 32-bit instructions (V15/V16 not in the tree).
  Reported to T01 with the isolation evidence. This is a dependency, not a V19 result.
* VCVT fixed->float rounding is implemented as round-to-nearest-even and float->fixed as truncate
  toward zero, from the ARM ARM `FPToFixed(..., round_to_zero=TRUE)` / `FixedToFP` reading. No
  hardware oracle was available to confirm the tie cases; the test cases avoid ties except where
  the ARM ARM is explicit.
* FPSCR cumulative exception bits (IOC/DZC/OFC/UFC/IXC) are not set by the NEON float ops; only
  QC is. V17 owns the VFP cumulative-bit convention; if the lead wants NEON to set them too, that
  is a small addition.
* No support for the secure-world or vendor-specific extensions; none are in this class.

## Integration notes

* V02 applied the exact `neon_route` split posted by V19 to `emu/n2a32_core.c`; its five new
  routing cases and the existing 14 NEON rows pass (`N2TEST V02 PASS 822/822` at the time).
* V02's core, V03's loader and the class function link together; the direct unit test does not
  depend on the loader.
* The T01 kernels are in `emu/test_v19/k19_neon_shift.c`; T01 can copy them into
  `tests/emu/kernels/`. They are self-contained (only `kcommon.h`).
