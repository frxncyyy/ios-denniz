# V18 — NEON: three registers of the same length (status)

Package: `n2a32_neon_3same` (Advanced SIMD three-registers-same-length, integer + float32 + logic).
Owner: worker `V18`. Everything here is **host-tested only**; nothing was run on the phone and nothing
was published. No vendor bytes were copied into the tree.

## What is implemented

`gpu/hwcodec/emu/n2a32_neon_3same.c` (one file, C11, MIT), the whole encoding class

    1111 001U 0D size Vn Vd opc N Q M bit4 Vm        (ARM ARM A7.4.3, bit23 == 0)

for both the D and Q forms (Q8–Q15 included: the D/N/M bits carry the high bit of the D-register
number, so `q8`..`q15` are legal; an odd D-register number with Q=1 is architecturally
UNPREDICTABLE and llvm-mc rejects it, so it returns `N2A32_UNDEF`).

| Group | Mnemonics |
|---|---|
| integer add/sub | `VADD VSUB` (I8/I16/I32/I64) |
| halving | `VHADD VHSUB VRHADD` (S/U 8/16/32) |
| saturating | `VQADD VQSUB` (S/U 8/16/32/64), `VQDMULH VQRDMULH` (S16/S32), QC in FPSCR bit 27 |
| compares | `VCGT VCGE` (S/U 8/16/32), `VCEQ VTST` (8/16/32) |
| variable shifts | `VSHL VQSHL VRSHL VQRSHL` (S/U 8/16/32/64; count = signed byte of Vn, negative = right) |
| min/max/abs | `VMAX VMIN VABD VABA` (S/U 8/16/32) |
| multiply | `VMUL` (I8/I16/I32 and P8), `VMLA VMLS` (I8/I16/I32) |
| pairwise (D form, as encoded) | `VPADD` (I8/I16/I32), `VPMAX VPMIN` (S/U 8/16/32) |
| logic | `VAND VBIC VORR VORN VEOR VBSL VBIT VBIF` (D and Q, all 64-bit halves) |
| float32 | `VADD VSUB VMUL VMLA VMLS VFMA VFMS VCEQ VCGE VCGT VACGE VACGT VMAX VMIN VMAXNM VMINNM VABD VRECPS VRSQRTS`, and D-form `VPADD.F32 VPMAX.F32 VPMIN.F32` |

Float uses the **standard FPSCR value**: round-to-nearest-even, DN=1 (any NaN operand of an
arithmetic op → default NaN `0x7fc00000`), FZ=1 (denormal inputs flushed, denormal results flushed to
a signed zero). `VMLA/VMLS.F32` round the product before the add; `VFMA/VFMS.F32` are fused (single
rounding, double intermediates — exact for binary32 because 53 >= 2*24+2). `VMAX/VMIN.F32` use
`FPMax/FPMin` (any NaN → default NaN; `+0 > -0`); `VMAXNM/VMINNM` replace a single quiet NaN by
∓infinity first (a signalling NaN still gives the default NaN). `VRECPS` = fused 2 + (−Vn)·Vm,
`VRSQRTS` = fused (3 + (−Vn)·Vm)/2, with the ARM ARM's 0×∞ → 2.0 / 1.5 special cases.

The semantics were taken from the ARM ARM pseudocode (the fetched instruction pages plus the shared
`FPRecipStepFused`/`FPRSqrtStepFused`/`FPMax`/`FPMin`/`FPMulAdd` pseudocode), and the encoding table
was verified against `llvm-mc --disassemble` for every (U, size, opc, bit4, Q) combination.

### Deliberately `N2A32_UNDEF`

* the SHA1/SHA256 **three-register** crypto space (`opc=1100`, bit4=0, Q form): SHA1C, SHA1P, SHA1M,
  SHA1SU0, SHA256H, SHA256H2, SHA256SU1. They are allocated encodings of this class but outside the
  V18 deliverable. The vendor objects use only the two-register forms SHA1H/SHA1SU1/SHA256SU0 and
  AESE/AESD/AESMC/AESIMC, which are in V20's space (`bit23=1`), not here.
* every unallocated encoding of the space (e.g. `VHADD size 11`, `VQDMULH size 00`, `VCEQ/VTST/VMLA/
  VMUL size 11`, `VPMAX/VPMIN/VPADD with Q=1`, all float ops with size 01/11, the ARMv8.2
  dot-product and fp16 encodings, which the build's `-mattr` does not enable);
* Q form with an odd D-register number (UNPREDICTABLE; see above);
* any word outside `bits31-25 == 1111001, bit23 == 0`.

## Tests

Command and last lines (run twice: `clang -O1 -fsanitize=address,undefined
-fno-sanitize-recover=all` and plain `-O2`; both print the same):

    $ bash gpu/hwcodec/emu/test_v18/run_tests.sh
    V18-GEN forms 332 cases 2656 allocated-combos 339 covered 332
    V18-ORACLE words 1536 ok 996 undef 540 derived-undef 1413
    V18: 2790 cases, 2949 oracle words, sweep ok=56088 undef=164908
    N2TEST V18 PASS 225740/225740
    V18: 2790 cases, 2949 oracle words, sweep ok=56088 undef=164908
    N2TEST V18 PASS 225740/225740
    V18-IOS n2a32_neon_3same.c compiles for arm64-apple-ios16.0 with zero warnings
    V18-VENDOR class instructions 243, mnemonics: <unallocated>=20, vadd=46, vand=11, vbic=3,
      vbsl=5, vcgt=4, vhadd=14, vmax=3, vmul=1, vorr=47, vqrshl=53, vrhadd=30, vshl=19, vsub=5, vtst=2
    V18-VENDOR PASS every class mnemonic used by the vendor objects is implemented

What the groups are:

1. **Cases** (2790): 2656 generated cases — 8 for every allocated (mnemonic, datatype, Q) form,
   including all four element sizes, both signednesses, Q8–Q15 and high D registers — plus 134
   hand-derived golden cases. Encodings come from `gen_cases.py`, which assembles generated and
   hand-written assembly (`golden_encodings.txt`) with
   `llvm-mc -triple=armv8a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8
   --show-encoding`; **no encoding is typed by hand**. Expected values are literals: the generated
   ones from `ref_model.py`, a lane-by-lane transcription of the ARM ARM pseudocode written
   independently of the C (Python integers, no bit tricks); the golden ones computed by hand
   (arithmetic in each comment), e.g. the fused/non-fused VFMA/VMLA difference
   (0x337ffffe vs 0x00000000), VQADD.S64/QSUB saturation, VRHADD rounding, VQRSHL rounding with
   shift −128, VBSL/VBIT/VBIF masks, fmaxnm with qNaN/sNaN. The generator also refuses to finish
   unless every allocated non-SHA encoding form has >= 6 cases, every saturating form has cases
   with QC=1 and QC=0, and (for the shift forms) the eight boundary shift counts
   {0, 1, es-1, -(es-1), es, -es, es+1, -(es+1)} are exercised for every element size 8/16/32/64
   (verified: the generated registers contain exactly those counts).
2. **Decode oracle** (2949 words): every (U, size, opc, bit4, Q) combination, three register patterns,
   with the expected OK/UNDEF taken from `llvm-mc --disassemble` itself (996 must execute, 540 must
   be UNDEF, 1413 derived Q words with an odd register must be UNDEF).
3. **Sweep**: 200000 random words of the encoding space (half biased to well-formed Q forms) plus
   20000 words outside the class; each must execute or return UNDEF — never crash, never write a core
   register, `next_pc`, `thumb`, `in_it`, `itstate` or NZCV/GE, only the destination D register(s) and
   QC; a UNDEF must leave the whole `struct n2a32_cpu` byte-identical. ASan+UBSan clean.
4. **Rounding mode**: one float case runs with the host in `FE_UPWARD`; NEON must still compute the
   RNE result (`1.0 + 2^-24 = 1.0`) and restore the host mode.
5. **iOS compile**: `clang -c --target=arm64-apple-ios16.0 -ffreestanding -nostdinc` with
   `gpu/video/sdk/sysinc`, `-Wall -Wextra -Werror`, zero warnings. (`<fenv.h>` is optional via
   `__has_include`; that SDK has none, so the iOS object relies on the process default RNE mode; the
   host build does save/force/restore RNE.)
6. **Vendor acceptance** (`check_vendor_words.py`): linear llvm-objdump walk of the five objects in
   `gpu/build/cam-vendor-20261109/vfs`, keeping only 32-bit Thumb Advanced SIMD words whose converted
   A32 form is this class. 243 class instructions; every mnemonic is implemented (details in the
   D01 message posted on the board).

Differential harness (T01's `tests/emu/run_diff.sh`, run locally with `N2HW_TMP=/tmp/n2hw-V18/diffrun`,
117 kernels x 20 seeds x 7 configs, tree state before k20_neon.c was added; a later re-run is blocked by
T01's `diffrun: too many kernels (max 128)` with the 130-kernel corpus, reported to hw-t01):

    configuration    calls     pass mismatch     stop
    arm-O0           2340     2340        0        0
    arm-O2           2340     2215      125        0
    arm-O3v          2340     2253       87        0
    thumb-O0         2340     1738      551       51
    thumb-O2         2340     2086      140      114
    thumb-O3v        2340     2088      127      125
    armv8-O3v        2340     2253       87        0

Per-kernel failures of the kernels that contain only V18-class instructions (attribution by encoding,
from the -O2/-O3v objects): `k_neon_vminmax` (vmax/vmin), `k_neon_vmla`, `k_neon_vmul`, `k_neon_vshl`,
`k_neon_vcgtbit`, `k_neon_vneg`, `k_neon_vshrn` -- **0 mismatches in every ARM configuration**
(arm-O0/O2/O3v/armv8-O3v). They do differ in thumb-O0, but at -O0 the C is scalar (no NEON), so those
belong to T16/T32 and not to this class. The remaining ARM-configuration failures are in kernels that
mix V18 with V19/V20 instructions (k_i8_loop, k_i16_loop, k_i16_mul, k_neon_vmla16, k_u8_sad); the
V19-only kernel k_neon_vsubl also fails in arm-O2 (20/20), so the harness is red for reasons outside
this package. No V18-class instruction could be isolated as a mismatch source.

Ad-hoc integration evidence: the 2M-case fuzz harness with **all** class files linked and the exact
command from hw-t01's report is clean under ASan+UBSan (`fuzz: a32=1000000 t32=1000000 ok=96961
undef=674621 fault=1228418 budget=24`, exit 0), which is also the regression test for the two
shift-by-64 sites T01 found (both fixed: `sh == 0` fast path; the signed 64-bit VQSHL overflow test is
now `asr64(v << s, s) != v`).

## Not done / open

* **No phone run** (worker rules) and no silicon oracle: expected values are ARM ARM + llvm-mc, the
  differential harness is the only execution evidence beyond that.
* **FPSCR cumulative exception flags** (IOC/DZC/OFC/UFC/IXC) are not set by the float ops; only QC is
  (as the task requires). Documented, not accidentally omitted.
* **Differential kernels**: written and compile-checked
  (`emu/test_v18/k18_neon_3same.c`, 13 kernels whose ARM -O3v objects contain vadd/sub/mul/vmla/and/
  orr/eor/bsl/bit/ceq/vcgt/vshl), but installing them in `tests/emu/kernels/` is T01's directory;
  posted to hw-t01. I ran the harness locally with the tree's own kernels (k12_neon, k05_vec) to
  exercise the class; those rows are reported in the DONE message.
* **SHA1C/SHA1P/SHA1M/SHA1SU0/SHA256H/SHA256H2/SHA256SU1** are UNDEF (see above). If the vendor's
  SHA-1 path ever needs them (it does not today: only SHA1H/SHA1SU1/SHA256SU0 are present, in V20's
  space), they are a separate package.
* **Accuracy of the float model**: FZ/DN/RNE are implemented explicitly; the host FPU must be IEEE
  binary32/binary64 (both targets are). `fesetround` is used when `<fenv.h>` exists; on the reduced
  iOS SDK the process default RNE is assumed. The double-intermediate trick for FMA/VRECPS/VRSQRTS is
  exact (53 >= 2*24+2) except for an intermediate that would be a *double* subnormal, which cannot
  occur for binary32 inputs.
* `VQADD/VQSUB/VABD/...` with esize 64: only `VADD/VSUB/VQADD/VQSUB/VSHL/VQSHL/VRSHL/VQRSHL` exist
  in that size; everything else at size 11 is unallocated and returns UNDEF (verified by the oracle).
* Not committed: workers do not touch git history (WORKER_RULES §2).

## Files

* `gpu/hwcodec/emu/n2a32_neon_3same.c` — the implementation.
* `gpu/hwcodec/emu/test_v18/` — `run_tests.sh`, `gen_cases.py`, `ref_model.py`,
  `gen_decode_oracle.py`, `golden_encodings.txt`, `golden_cases.inc`, `t_3same.c`,
  `check_vendor_words.py`, `k18_neon_3same.c`, this file.
