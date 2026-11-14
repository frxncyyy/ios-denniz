# V20 — NEON two-register miscellaneous, VEXT/VTBL/VDUP/immediates, structure load/store

Host-tested only. Nothing here has run on the phone; no phone, adb, fastboot or publish was used.
Files: `emu/n2a32_neon_misc.c`, `emu/n2a32_neon_ldst.c`, `emu/test_v20/**`.

## Commands and results

    $ bash gpu/hwcodec/emu/test_v20/run_tests.sh
    mkenc: 288 encodings
    gen_space: 17408 words, 9240 must execute, 20 must UNDEF, 8148 unallocated must UNDEF
    t_misc: 445 checks, 0 failures
    t_ldst: 588 checks, 0 failures
    t_cover: 17408 checks, 0 failures
    t_sweep: 200000 checks, 0 failures
    t_misc: 445 checks, 0 failures
    t_ldst: 588 checks, 0 failures
    t_cover: 17408 checks, 0 failures
    t_sweep: 200000 checks, 0 failures
    N2TEST V20 PASS 436882/436882

The runner builds each suite twice: `-O1 -fsanitize=address,undefined -fno-sanitize-recover=all`
and plain `-O2`. The two halves of the output are those two builds; all products are under
`/tmp/n2hw-V20` (~11 s total).

arm64 iOS compile check (not part of run_tests.sh; compile only, nothing runs there):

    SDK=gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk
    clang -target arm64-apple-ios16.0 -isysroot $SDK -std=c11 -O2 -Wall -Wextra -Werror \
          -I gpu/hwcodec/include -c gpu/hwcodec/emu/n2a32_neon_misc.c -o /tmp/n2hw-V20/misc_ios.o
    clang ... n2a32_neon_ldst.c -o /tmp/n2hw-V20/ldst_ios.o
    -> zero warnings, Mach-O 64-bit arm64 objects

What the suites are:

* `t_misc.c` — 445 unit checks. Encodings come from `misc.s` through `llvm-mc --show-encoding`
  (`mkenc.py`); expected register values are literals derived from the ARM ARM pseudocode, written
  out in the comments next to each check. Cover: every implemented mnemonic in D and Q form where it
  exists, every element size, saturation edges (INT_MIN for VQABS/VQNEG/VQMOVN), QC flag checks,
  VCVT round-towards-zero for FP→int and round-to-nearest-even for int→FP with FPSCR.RMode set to
  the opposite value, all six VRINT modes, all four VCVT{A,N,P,M} modes, standard-FPSCR NaN
  handling (VABS/VNEG copy NaN sign-cleared/flipped; VRINT/VRECPE/VRSQRTE return the default NaN),
  VRSQRTE/VRECPE special values (+-0, +-inf, |x| >= 2^126, negative finite), VTBX's unchanged-lane
  rule, VEXT at every immediate value including the R1 F16 reproductions (#0/#4/#7 D, #0/#4/#15 Q,
  .16/.32/.64 forms), modified immediates at every cmode group (including the I64 byte mask and the
  F32 form), and encodings that must return `N2A32_UNDEF` and leave the CPU state unchanged.
* `t_ldst.c` — 588 unit checks. All VLD/VST forms with literal expected register contents and literal
  expected memory bytes: VLD1/VST1 with 1-4 registers at 8/16/32/64 bits, VLD2/3/4 and VST2/3/4
  (including VLD2 with four registers = two blocks), the ARMv8 register-stride-2 multiple forms
  (types 1/5/9), single structure to/from one lane (every size and several lane indices, VLD2/3/4
  lane forms), the ARMv8 register-stride-2 lane forms (.16 bit 5 / .32 bit 6: `vld2.16 {d0[1],d2[1]}`,
  `vld3.32 {d0[0],d2[0],d4[0]}`, `vld4.16 {d16[0],d18[0],d20[0],d22[0]}`, `vld4.32`, `vst2.32`),
  VLD1-4 to all lanes (including the VLD1 two-register form and the 128-bit-alignment `.32`
  encoding), offset / post-index-register / post-index-by-transfer-size writeback, Rn = SP, high
  register lists (d16-d31), lane-load upper-lane preservation (a `vld1.32 {d18[0]}` leaves d18 words
  1-3 and d19 untouched, which answers V18/T01's k_u8_sad question), and unallocated size/alignment/
  lane nibbles returning UNDEF with no state change.
* `t_cover.c` + `gen_space.py` — 17408 checks in both directions. `gen_space.py` sweeps the three
  encoding spaces with `llvm-mc --disassemble`: every word of the two-register-misc/VEXT space
  (A x B x C x Q x D/M x two Vm values, both U values), every modified-immediate word
  (i/imm3/cmode/op/Q/imm4), and the element/structure space (bits 23-20 x type x size x align x Rm).
  Every word llvm decodes to a mnemonic V20 claims must execute (9240 words); the two half-precision
  conversions and the 18 AES/SHA2 words must return UNDEF (20 words); and every word of those spaces
  that llvm's disassembler rejects (8148 words, all with non-wrapping registers) must also return
  UNDEF rather than execute as some instruction. The last is the reverse check that found the
  unallocated A=0xC/VREV.64 space, the odd-Q-register forms and the missing register-stride-2 lane
  forms during this pass.
* `t_sweep.c` — 200000 random words (100000 in the 0xF2/0xF3 space, 100000 in the 0xF4 space) under
  ASan/UBSan and `-O2`: every call returns `N2A32_OK` or `N2A32_UNDEF`, nothing crashes, and an UNDEF
  leaves the whole `struct n2a32_cpu` unchanged.

One-off audits not part of `run_tests.sh` (kept here as evidence; the C tools were in `/tmp/n2hw-V20`,
the generator is the same llvm-mc oracle):

* Two-register misc/VEXT space, 294912 words (U, D, A, B, C, Q, M and Vd,Vm in {0,1,2,3,14,15}):
  **0** words we execute that llvm rejects. 1836 llvm-decodable words we UNDEF, all intentional:
  VTBL/VTBX register lists running past d31 (UNPREDICTABLE), VCVT.F16.F32/VCVT.F32.F16, and the
  two-register AES/SHA2 forms.
* Structure load/store space, 32768 words (single, L, D, Vd, type, size/align field, Rn=1, Rm=15):
  **0** words we execute that llvm rejects. 569 llvm-decodable words we UNDEF, every one of them a
  register list that wraps past d31 (UNPREDICTABLE; deliberate choice, documented below).

Also run once by hand (not part of `run_tests.sh`, because it needs V02's core):

    $ clang -I gpu/hwcodec/include integ.c emu/n2a32_core.c emu/n2a32_mem.c emu/n2a32_excl.c \
            emu/n2a32_stub_classes.c emu/n2a32_neon_misc.c emu/n2a32_neon_ldst.c -pthread
    integ: OK

That runs `vmov.i32 q0,#0xff; vld1.8 {d0},[r1]; vtbl.8 d5,{d0,d1},d2; vst1.32 {d0,d1},[r0]`
through the real dispatcher and checks the results; the same sequence (and a second one with
`vld1.64/vtbl.8/vst1.64 [r1:128], r3/vld1.8`) produces identical register and memory results when
executed in A32 and in T32 form through the core (both stop at the instruction limit with the same
state). The core converts T32 NEON load/store (0xF9..) to the A32 0xF4.. form before calling this
class, so the same decode is used in both instruction sets.

Differential kernels for T01: `test_v20/k20_neon.c` (8 kernels). Run in a private copy of
`tests/emu` with only that kernel file (on the tree after T01 applied R1's V14 F1/F2/F4 patch):

    N2HW_TMP=/tmp/n2hw-V20/t01run2 bash <copy>/tests/emu/run_diff.sh
    arm-O0 160/160, arm-O2 160/160, arm-O3v 160/160, thumb-O0 160/160,
    thumb-O2 160/160, thumb-O3v 160/160, armv8-O3v 160/160
    N2TEST T01 PASS 1120/1120

Before the V14 patch the only differences were 30/160 in thumb-O0 (k_v20_fill, which contains no
NEON at all in that build, and k_v20_dup_shuffle, which also uses V17's vmov.f64); the V20
instruction sequences were verified identical in A32 and T32 through the core, and the V14 patch
removed all of them.

## Implemented

`n2a32_neon_misc` (A32 Advanced SIMD data-processing, the encoding groups the core routes to V20):

| Group | Instructions |
|---|---|
| two registers, misc (0xF3B/0xF3F) | VREV64.8/16/32, VREV32.8/16, VREV16.8, VPADDL.S/U8/16/32, VPADAL.S/U8/16/32, VCLS.S8/16/32, VCLZ.I8/16/32, VCNT.8, VMVN, VQABS.S8/16/32, VQNEG.S8/16/32 (QC), VCGT/VCGE/VCEQ/VCLE/VCLT #0 for .S8/16/32 and .F32, VABS/VNEG .S8/16/32 and .F32, VSWP, VTRN/VUZP/VZIP .8/16/32, VMOVN.I16/32/64, VQMOVN.S/U16/32/64, VQMOVUN.S16/32/64 (QC), VSHLL.i8/16/32 (maximum shift), VCVT.F32.S32/U32 and VCVT.S32/U32.F32, VRECPE.U32/F32, VRSQRTE.U32/F32, VRINTN/X/A/Z/M/P.F32, VCVT{A,N,P,M}.{S32,U32}.F32, VTBL/VTBX.8 with 1-4 table registers, VDUP.8/16/32 (scalar lane) |
| VEXT (0xF2B/0xF2F) | VEXT.8 D and Q, imm 0-7 / 0-15, source Dn:Dm (Dn in the low bytes) |
| one register, modified immediate | VMOV.I8/I16/I32/I64, VMOV.F32, VMVN.I16/I32, VORR.I16/I32, VBIC.I16/I32 — every cmode including the 32-bit MSL #8/#16 forms and the I64 byte-mask; D and Q destination where the encoding allows |

`n2a32_neon_ldst` (0xF4xxxxxx):

| Group | Instructions |
|---|---|
| multiple structures | VLD1/VST1 with 1-4 registers, VLD2/VST2 with 2 and 4 registers, VLD3/VST3, VLD4/VST4, all of .8/.16/.32/.64; ARMv8 register-stride-2 forms (VLD4/VST4 stride 2, VLD3/VST3 stride 2, VLD2/VST2 stride 2) |
| single structure, one lane | VLD1/VST1, VLD2/VST2, VLD3/VST3, VLD4/VST4 to/from one lane, all sizes, lane indices and alignment fields, including the ARMv8 register-stride-2 lane forms (.16 bit 5, .32 bit 6) |
| single structure, all lanes | VLD1 (one or two registers), VLD2, VLD3, VLD4, including the 128-bit-alignment encoding of the .32 form |
| addressing | `[Rn]` (Rm = 15), `[Rn], Rm`, `[Rn]!` / post-index by the number of bytes transferred (Rm = 13); Rn = 13 (SP) allowed, Rn = 15 rejected |

## Semantics fixed in this revision (rev 2, after the first DONE)

* **VEXT operand order (R1 F16).** The source is Dn:Dm with Dn in the low bytes; the first
  implementation concatenated Dm first. Fixed in `vext()`; tests now include R1's exact repros
  (`vext.8 d0,d1,d2,#0/#4/#7`, `vext.8 q0,q1,q2,#4/#8/#15`) and the .16/.32/.64 forms.
* **VCVT rounding.** ARM ARM F6.1.60: Advanced SIMD VCVT converts FP→integer with Round towards
  Zero and integer→FP with Round to Nearest (ties to even), using StandardFPSCRValue(); FPSCR.RMode
  is not consulted (the plain VCVT has no rounding-mode variants; VCVT{A,N,P,M} do). The previous
  code used the live FPSCR.RMode for both directions, which would round `(int)1.5` to 2 on a guest
  with the default FPSCR. Tests set FPSCR.RMode to RZ/RM/RP and require truncation / RNE.
* **Standard FPSCR for NEON float.** DN = 1 (every arithmetic NaN result is the default NaN),
  FZ = 1 (denormal inputs flushed to a signed zero) and RNE, regardless of the live FPSCR bits.
  VABS/VNEG are pure sign operations (FPAbs/FPNeg: NaNs are copied with the sign cleared/flipped
  and are never replaced by the default NaN). VRECPE/VRSQRTE special values now follow the armasm
  table: ±0/denormal → ±inf, ±inf → ±0 (VRECPE) / +0 or default NaN (VRSQRTE), |x| ≥ 2^126 → ±0,
  negative finite → default NaN, NaN → default NaN.
* **Q-register parity.** Q forms name even D registers; odd numbers are UNPREDICTABLE and llvm
  rejects the words. Rejected now for every Q form, including the special cases VMOVN (Qm even),
  VSHLL (Qd even), VDUP (Qd even) and VEXT (Qd/Qn/Qm even). VDUP's D source and the VTBL table
  registers may be odd.
* **Unallocated size/alignment/lane fields** are rejected (a `[single][load][type]` 16-bit mask
  derived from llvm's disassembler, see `n2a32_neon_ldst.c`), so no unallocated load/store word
  executes as a load/store. This includes the missing VLD2/3/4 `.64` forms, VLD1 `.64` alignment
  restrictions, and the lane-index/alignment holes.
* **ARMv8 register-stride-2 single-lane forms** were missing entirely (they were executed as
  consecutive registers). They are now decoded and tested.
* **VREV/PADDL/... with size = 11 (A = 0xC)** were executed as 64-bit element operations; that
  encoding group has no .64 form. They now return UNDEF.
* **VSWP with a size field other than 00** was executed as VSWP; only A = 0x2 is allocated. Now UNDEF.

## Deliberately N2A32_UNDEF

* `VCVT.F16.F32` / `VCVT.F32.F16` (half precision).
* The ARMv8 crypto forms that share the two-register-misc encoding space: AESE.8, AESD.8, AESMC.8,
  AESIMC.8 (A = 0, B = 3, bits 7-6 = op), SHA1H.32 (A = 9, B = 2, C = 1), SHA1SU1.32 and
  SHA256SU0.32 (A = 0xA, B = 3, bits 7-6 = 10/11). A byte-level scan of the five vendor objects
  (`bin/vpud` and the four `lib*.so`) finds **zero** words matching those encodings, so the
  `aese.8`/`sha1su1.32`/`sha256su0.32` entries in the D01 "undecodable" list are disassembly
  artefacts, not real instructions (reported to hw-d01).
* Encodings the ARM ARM leaves UNDEFINED and llvm refuses to assemble or disassemble:
  VREV32.32; VCNT/VMVN with a size other than .8; the .F32 forms with a size other than 32;
  `VUZP.32`/`VZIP.32` with D registers (UNDEFINED in ARMv8); VTBL with a register list that runs
  past d31; the modified immediate with cmode = 1111 and op = 1; single-lane encodings with size
  bits 11 (type 12-15 are the all-lanes forms, handled); register lists that wrap past d31;
  stores to all lanes; the `.32`-with-128-bit-alignment encoding outside VLD2/VLD4.
* Register lists that wrap past d31 are UNPREDICTABLE in the ARM ARM; V20 chooses to return UNDEF
  rather than wrap the register number. llvm still disassembles such words, so the reverse check
  above reports them (569 load/store and 1440 VTBL/VTBX words) as expected UNDEFs.
* Everything outside the two encoding spaces: the core routes it elsewhere, and if it ever arrives
  here it UNDEFs without touching the CPU state.

## Known risks / open questions

1. **VRECPE/VRSQRTE.** The integer forms return the correctly rounded fixed-point value
   (2^32/a, 2^32/sqrt(a)); the ARM ARM's estimate table is implementation-defined, so a particular
   core may return a slightly different value. The float forms return the correctly rounded
   reciprocal / inverse square root (relative error <= 2^-24), which is inside the architectural
   2^-8 bound but is not the table value the phone's hardware produces. Nothing in the vendor
   census uses these.
2. **FPSCR cumulative exception bits.** NEON float operations set only QC (for the saturating
   integer forms); IOC/DZC/OFC/UFC/IXC are not updated, consistent with the other NEON packages.
   The control bits used are the StandardFPSCRValue() constants, not the live FPSCR.
3. **MSL immediates** (cmode 12/13) are implemented from the ARM ARM; llvm cannot assemble them, so
   the unit suite covers every other cmode by assembly and the whole modified-immediate space by
   the disassembler/reverse sweeps. Their expanded values are checked by the sweep only for
   acceptance, not against a second model.
4. **VORR/VBIC with a D destination** are accepted (llvm assembles them; the ARM ARM lists Qd only)
   and execute as the natural 64-bit operation.
5. Compiled for arm64 iOS (compile check only): both files build with
   `clang -target arm64-apple-ios16.0 -isysroot gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk
   -std=c11 -O2 -Wall -Wextra -Werror -I gpu/hwcodec/include -c ...` and produce Mach-O arm64
   objects with zero warnings. They are plain C11 with no host-specific code, no `arm_neon.h` and
   no libm. Not run on the phone.
