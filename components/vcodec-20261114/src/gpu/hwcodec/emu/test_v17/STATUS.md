# V17 STATUS -- VFP scalar floating point (A32 and T32)

Host-tested only.  **Not run on the phone. Not compiled for arm64 iOS** (no iOS SDK sysroot in this
session; the file uses only C11 + `<fenv.h>` + `<math.h>`).  No private material copied, nothing
published.

## Files (all owned by V17)

| File | Purpose |
|---|---|
| `emu/n2a32_vfp.c` | the class function `n2a32_vfp` |
| `emu/n2a32_fp_util.h` | shared static-inline helpers: FPSCR rounding-mode bridge, integer-code saturating float->int (32/64 signed/unsigned), NaN bit helpers, host-exception -> FPSCR mapping.  V18-V20 may include it read-only. |
| `emu/test_v17/run_tests.sh` | host test driver (ASan+UBSan -O1 and plain -O2) |
| `emu/test_v17/gen_enc.py` | assembles `vectors.s` + `vectors_t32.s` with llvm-mc and writes the encoding header |
| `emu/test_v17/vectors.s`, `vectors_t32.s` | 309 labelled A32/T32 VFP instructions (no hand-typed hex) |
| `emu/test_v17/t_vfp.c` | 2199 host checks (unit + T32 twins + 200000-word sweep) |
| `emu/test_v17/k_v17_fp.c` | self-contained C differential kernels for T01 (`k_v17_*`) |
| `emu/test_v17/vfp_demo.s`, `vfp_demo_t32.s` | core-integration snippets (A32 and Thumb-2) |
| `dsh-state/workers/V17/STATUS.md` | this handoff |

## Implemented

Everything in the package deliverable.  By group:

- **Load/store**: `VLDR`/`VSTR` immediate (positive/negative, PC literal load, SP base),
  `VLDM`/`VSTM` IA and DB with optional writeback, `VPUSH`/`VPOP`, and the odd-imm8 FLDMX
  forms `FLDMIAX`/`FLDMDBX`/`FSTMIAX`/`FSTMDBX`.  Single and double lists, register lists up to
  S0-S31 / D0-D15 (FLDMX extra word handled).
- **Core <-> FP transfers**: `VMOV Rt,Sn`, `VMOV Sn,Rt`, `VMOV Dm,Rt,Rt2`, `VMOV Rt,Rt2,Dm`,
  the obsolete two-singles `VMOV Sm,Sm1,Rt,Rt2` and reverse, all lane forms
  `VMOV.{8,s8,u8,16,s16,u16,32} Rt,Dn[x]` and `Dn[x],Rt`, `VMOV.F32/F64 #imm`, `VMOV.F32/F64
  reg`, and `VDUP.{8,16,32} Dd/Qd,Rt` (the cp10 form; V20 owns the Advanced SIMD scalar-lane form).
- **System registers**: `VMRS APSR_nzcv,FPSCR`, `VMRS Rt,FPSCR`, `VMSR FPSCR,Rt`.
- **Arithmetic, F32 and F64**: `VADD VSUB VMUL VNMUL VDIV VMLA VMLS VNMLA VNMLS VFMA VFMS VFNMA
  VFNMS` (VFMA/VFMS/VFNMA/VFNMS use a real fused `fma`; VMLA/VMLS/VNMLA/VNMLS round the product
  separately, as the architecture specifies).
- **Unary**: `VABS VNEG VSQRT`.
- **Compare**: `VCMP` and `VCMPE`, register form and `#0`.
- **Convert**: `VCVT.F64.F32`, `VCVT.F32.F64`; `VCVT`/`VCVTR` `.{S,U}32.{F32,F64}` (round toward
  zero / FPSCR mode); `VCVT.{F32,F64}.{S,U}32`; fixed point 16- and 32-bit signed/unsigned in both
  directions; half precision `VCVTB`/`VCVTT` `.F32.F16` and `.F16.F32`.
- **ARMv8**: `VSEL` (EQ/VS/GE/GT; the condition comes from the APSR condition flags, as the ARM
  ARM ConditionPassed; compilers precede VSEL with `VMRS APSR_nzcv,FPSCR` after a VFP compare),
  `VMAXNM`/`VMINNM`, `VRINTA/N/P/M/X/Z/R`, `VCVTA/N/P/M` signed and unsigned.
- **NaN/flags**: default NaN when FPSCR.DN, otherwise ARM-order quieted-NaN propagation (sNaN
  priority); FPSCR IOC/DZC/OFC/UFC/IXC (and IDC for FZ input flushing) accumulate; FPSCR.FZ flushes
  subnormal inputs and results; VCMP writes FPSCR NZCV only.

## Encodings deliberately returned as N2A32_UNDEF

- Any `MRC`/`MCR` cp10/cp11 other than the FPSCR `VMRS`/`VMSR` above: `FPSID`, `FPEXC`,
  `FPINST`, `FPINST2`, `MVFR0/1/2`, and unallocated transfer forms.
- Every encoding in the VFP space that the ARM ARM leaves UNDEFINED or that this decoder does not
  claim: reserved data-processing opcode groups, bit 4 set in the ARMv8 unconditional space,
  `VCVT` half with `sz=1`, `VCVT` fixed with `fbits` outside 1..32 (32-bit) / 0..16 (16-bit).
- Load/store oddities: `VSTR` with `Rn=15` (literal store), `VLDM`/`VSTM` with `Rn=15`, an empty
  register list (`imm8=0`), a list that would run past S31/D31, and P/U/W combinations other than
  IA (`P=0,U=1`) / DB (`P=1,U=0,W=1`, or `P=0,U=0,W=1` for store).
- Transfers with `Rt=15` (`VMOV` core<->S/D, lane and `VDUP`), `VDUP` to an odd-numbered Q
  register, the two-singles transfer with an odd first register, and `VMOV` lane lane/size fields
  that are not one of the defined 8/16/32-bit patterns.
- NEON encodings (V18-V20's space); the core does not route them here, but anything unrecognised
  returns UNDEF.

The D01 census (FACTS.md 4.9) lists 46 mnemonics for this class.  The scalar VFP ones are:
`vldr vstr vmov.32 vmov vcvt.f64.s32 vpop vpush vdiv.f64 vmov.f32 vmla.f64 vcvt.f32.u32
vcvt.f64.f32 vdiv.f32 vsub.f64 vmov.f64 vcvt.f32.s32 vcvt.f64.u32 vcvt.s32.f64 vmrs vadd.f64
vmul.f32 vmul.f64 vcmp.f32 vsub.f32 vabs.f32 vcmp.f64 vcvt.s32.f32 vcvt.u32.f32 vcvt.u32.f64
vmls.f64 vmov.16 vmov.u16 vsqrt.f64 vadd.f32 vmla.f32 vsqrt.f32 vstmia`
-- **all are implemented**; none of them can return UNDEF.  (`vrshr`, `vsra`, `vdup`, `vrsra` in
the same census line are NEON words owned by V19/V20, not cp10/cp11 VFP.)

## Tested

Command (from the work tree root):

```
bash gpu/hwcodec/emu/test_v17/run_tests.sh
```

Last lines of the last run (identical for the ASan+UBSan -O1 and the plain -O2 builds, ~7 s total,
all build output under `/tmp/n2hw-V17`):

```
gen_enc: 309 encodings -> /tmp/n2hw-V17/enc_v17.h
V17 sweep: 200000 random VFP words: 58209 executed, 141791 UNDEF
N2TEST V17 PASS 2229/2229
V17 sweep: 200000 random VFP words: 58209 executed, 141791 UNDEF
N2TEST V17 PASS 2229/2229
```

What the 2229 checks cover: every form above with literal expected values worked out from the ARM
ARM/IEEE-754 (not from the implementation), both operand precisions, zero/negative/infinity/
overflow/underflow/divide-by-zero/invalid edge cases, FPSCR.RMode RN/RP/RM/RZ, FPSCR.DN, FPSCR.FZ,
NaN quieting/payload/order (including sNaN + FPSCR.DN=1 still setting IOC), r15 literals, SP bases, and Thumb-2 twins.  309 instruction words come
from llvm-mc through `gen_enc.py`; the sweep feeds 200000 random words drawn from the VFP encoding
groups (load/store, data-processing, unconditional, T32-shaped) under ASan+UBSan and requires
OK-or-UNDEF with the PC state untouched.

Core integration (not just the class function):
- `bash gpu/hwcodec/tests/emu/asmrun.sh --armv8 --mem 0x30000:36 emu/test_v17/vfp_demo.s` and the
  `--thumb --armv8` twin produce
  `mem[0x00030000]=000040400000803f00004040000000400000c84100000000640000006400000064000000`
  (VSQRT+VADD, VSELEQ, VRINTA, VMAXNM, VCVT fixed, VDUP, VMOV lane), exactly the expected patterns.  VSEL is also
  tested with the FPSCR flags deliberately opposite to APSR (R1 F11 regression case, see below).
- T01 differential harness with this file in `emu/` (run `bash gpu/hwcodec/tests/emu/run_diff.sh`):
  the **arm-O0 row is 2340/2340 calls, 0 mismatch, 0 stop**; the scalar FP kernels `k_faddsub`,
  `k_fmuldiv`, `k_fconv`, `k_daddsub`, `k_dmuldiv`, `k_dconv` are absent from the failure list in
  every ARM configuration.  (The harness still fails overall because of other classes' NEON /
  Thumb kernels, not V17.)
- `emu/test_v17/k_v17_fp.c` (8 self-contained `k_v17_*` kernels) compiles with T01's exact ARM
  flags; posted to hw-t01 for installation under `tests/emu/kernels/`.

## Not done, assumptions, risks

- **FLDMX/FSSTMX**: the odd-imm8 double list transfers the n doubles followed by one 32-bit extra
  word, which is written to / read from FPSCR.  The removed VFPv2 mixed-endian exponent packing is
  not modelled.  The vendor set does not use these forms (D01 census).
- **VCVT fixed-point**: the field decode (`fbits = 16/32 - (imm4*2 + i)`) was derived from
  llvm-mc 23.1.4's encodings plus the ARM ARM field diagram; round-tripping every fbits value
  0..16 / 1..32 matches.  For the F64 forms only the low 32 bits of the D register hold the
  integer.  16-bit results are sign-/zero-extended into the 32-bit S register.  Out-of-range
  fbits returns UNDEF (architecturally UNPREDICTABLE).
- **FP -> integer**: ARM-style saturation (INT_MIN/INT_MAX, 0/UINT_MAX, 0 for NaN) with IOC;
  IXC is also set when a fractional part was discarded, including some out-of-range negative ->
  unsigned cases.  Tests only pin IOC in those ambiguous cases.
- **VABS/VNEG of NaN** preserve the NaN sign and payload (FPProcessNaN); VABS does not clear a
  NaN's sign bit.  This follows the ARM ARM pseudocode.
- **VMAXNM/VMINNM** return the non-NaN operand when one operand is NaN (even with DN=1); both-NaN
  gives the default NaN with DN=1 or the quieted first sNaN / first NaN otherwise.
- **FZ** treats exact subnormal results as flushed with UFC|IXC; hardware ARM may not set UFC for an
  exactly representable subnormal.  **Underflow tininess** uses the host (x86 after-rounding)
  definition; tests use values away from the boundary.
- **VRINT** maps only IOC/IXC (VRINTX sets IXC when the value changes); the no-inexact variants
  deliberately drop the host's IXC/UFC.  `VCVT{A,N,P,M}` maps IOC/IXC.
- **R1 F11 was investigated and refuted**: VSEL reads APSR, not FPSCR.NZCV.  Evidence: T01's
  k_fminmax armv8-O3v code does `vcmp.f32 s0,s2; vmrs APSR_nzcv,fpscr; vcmp.f32 s2,s0;
  vselgt.f32 s4,s2,s0; vmrs APSR_nzcv,fpscr; vselgt.f32 s0,s2,s0`; the two VSELs see the same
  FPSCR value but must produce different operands, and the kernel matches the host on 20/20 seeds
  only with APSR-based VSEL (FPSCR-based: 19/20 fail).  The VMRS APSR_nzcv instructions are the
  architectural way to feed a VFP compare into VSEL.
- **VMOV immediate has no zero encoding**: imm8 = 0x00 is +2.0 and 0x80 is -2.0; the earlier note
  claiming a zero special case was wrong and is fixed (it broke T01 k18_float, now 20/20).
- The sweep's 141791 UNDEF words are unallocated encodings inside the forced VFP groups; they are
  required to be UNDEF and are counted, not crashes.
- Not compiled for arm64 iOS; not run on the phone; no performance measurements.
