# V11 STATUS — A32 media, saturating arithmetic and divide (`n2a32_arm_media`)

Package V11. Owner files: `gpu/hwcodec/emu/n2a32_arm_media.c`, `gpu/hwcodec/emu/test_v11/**`.
Host-only: **nothing here has run on the phone** (there is nothing for this package to run
there). "Host-tested" is the only claim made below.

Last update: 11 November 2026.

## 1. What is implemented

`int n2a32_arm_media(struct n2a32_cpu *c, uint32_t insn)` in `gpu/hwcodec/emu/n2a32_arm_media.c`
(806 lines, MIT, C11, no assembly, no memory access, no printing, no `abort`). It returns
`N2A32_OK` or `N2A32_UNDEF` exactly under the `include/n2a32.h` contract: the core has already
tested the condition and set `r[15]` / `next_pc` / `in_it`; the class never branches (nothing in
this class writes the PC) and never touches guest memory.

Implemented mnemonics (all A32, every shift/rotation variant; `ROR #0/8/16/24` where the encoding
has a rotation field, `LSL`/`ASR` where it has a shift field):

| group | mnemonics |
|---|---|
| extend | `SXTB SXTH UXTB UXTH SXTAB SXTAH UXTAB UXTAH SXTB16 SXTAB16 UXTB16 UXTAB16` |
| bit field | `UBFX SBFX BFI BFC` |
| reverse | `REV REV16 REVSH RBIT` |
| saturate | `SSAT USAT` (LSL #0-31 / ASR #1-32), `SSAT16 USAT16` |
| pack / select | `PKHBT PKHTB` (LSL #0-31 / ASR #1-32), `SEL` |
| parallel signed | `SADD16 SASX SSAX SSUB16 SADD8 SSUB8` |
| parallel signed saturating | `QADD16 QASX QSAX QSUB16 QADD8 QSUB8` |
| parallel signed halving | `SHADD16 SHASX SHSAX SHSUB16 SHADD8 SHSUB8` |
| parallel unsigned | `UADD16 UASX USAX USUB16 UADD8 USUB8` |
| parallel unsigned saturating | `UQADD16 UQASX UQSAX UQSUB16 UQADD8 UQSUB8` |
| parallel unsigned halving | `UHADD16 UHASX UHSAX UHSUB16 UHADD8 UHSUB8` |
| 32-bit saturating | `QADD QSUB QDADD QDSUB` |
| 16x16 multiply | `SMULBB SMULBT SMULTB SMULTT`, `SMLABB SMLABT SMLATB SMLATT`, `SMULWB SMULWT`, `SMLAW B`/`SMLAW T`, `SMLALBB SMLALBT SMLALTB SMLALTT` |
| dual multiply | `SMUAD SMUADX SMUSD SMUSDX`, `SMLAD SMLADX SMLSD SMLSDX`, `SMLALD SMLALDX SMLSLD SMLSLDX` |
| most-significant-word multiply | `SMMUL SMMULR SMMLA SMMLAR SMMLS SMMLSR` |
| sum of absolute differences | `USAD8 USADA8` |
| divide | `SDIV UDIV` (division by zero yields 0; `INT_MIN / -1` yields `INT_MIN`) |

The `X` forms exchange the second operand's halfwords before multiplying:
`product1 = Rn<15:0> * Rm<31:16>`, `product2 = Rn<31:16> * Rm<15:0>`, and the subtract
forms compute `product1 - product2` (Arm A32/T32 instruction reference for SMUSDX/SMLSDX/
SMLSLDX: "subtracts the second product from the first"). The first version of this file had
the two products the other way round for the subtract forms; the unit tests encoded the same
mistake, so they passed. Corrected on 11 Nov 2026: implementation and all 12 affected expected
values in `t_v11.c`, plus a new `smlsdx` overflow/Q case.

Flag rules used (Arm ARMv7-A A7.4/A8.8 plus the Arm A32/T32 instruction-set reference):

* the `S` and `U` parallel variants write `GE[3:0]` (halfword forms move `GE[1:0]` and
  `GE[3:2]` together). `S`: GE = 1 iff the exact signed lane sum is >= 0. `U`: GE = 1 on carry
  out of an addition, or when no borrow occurred in a subtraction.
* the `Q`, `SH`, `UQ`, `UH` parallel variants change **no** flags at all (not GE, not Q):
  "The Q flag is not affected even if this operation saturates", "does not affect the N, Z, C, V,
  Q, or GE flags" (Arm A32/T32 instruction reference, parallel add/subtract).
* `SSAT`/`USAT`/`SSAT16`/`USAT16` and `QADD`/`QSUB`/`QDADD`/`QDSUB` set Q (sticky) on saturation.
* `SMUAD(X)`, `SMLAD(X)`, `SMLSD(X)`, `SMLAxy`, `SMLAWy` set Q when the exact result does not fit
  in 32 bits. `SMLAD/SMLSD` use the *exact* `p1 ± p2 + Ra` (a two-step overflow test would set Q
  for e.g. `Ra = -1` with two `0x40000000` products, which the architecture does not).
* `SMUSD/SMUSDX` leave Q unchanged: the difference of the two products is always in
  `(-2^31, 2^31)` (each product is in `[-2^30, 2^30]` and the extreme difference is
  `2^31 - 32768`), so these instructions cannot overflow; the Arm A32/T32 reference lists the Q
  rule for `SMUAD/SMUADX` only. This is no longer an open question.
* `SMULxy`, `SMULWy`, `SMLALxy`, `SMLALD/SMLSLD`, `SMMUL/SMMLA/SMMLS`, `USAD8/USADA8`,
  `SDIV/UDIV`, the extends, `REV*`/`RBIT`, `UBFX/SBFX/BFI/BFC`, `PKH*`, `SEL` set no flags.
* nothing in the class touches N, Z, C or V.

## 2. Tests

    bash gpu/hwcodec/emu/test_v11/run_tests.sh

last lines (11 Nov 2026, clang 23.1.4, x86-64):

    gen_enc.py: 172 encodings
    N2TEST V11 PASS 960/960
    N2TEST V11 PASS 960/960

The script builds twice under `/tmp/n2hw-V11/` (the only place it writes): `-O1 -g` with
`-fsanitize=address,undefined -fno-sanitize-recover=all`, and `-O2` with no sanitizers, both with
`-std=c11 -Wall -Wextra -Werror -I gpu/hwcodec/include`; the two runs must print the same last
line. 960 checks run in each build (the pass line is the check count, not a subset).

What the tests do:

* **Encodings come from llvm-mc**, never typed in:
  - `v11_enc.s`: 387 lines of assembly, one `@ <name>` line before each instruction;
  - `gen_enc.py` runs
    `llvm-mc -triple=armv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8 --show-encoding`
    and emits `#define ENC_<name> <word>` (172 encodings). A renamed/missing encoding is a
    compile error in `t_v11.c`.
* **Expected values are literals** worked out from the ARM ARM pseudocode (see the comments in
  `t_v11.c`), 6+ cases per instruction form, with the flag/saturation edge cases: signed lane
  sums crossing zero and `0x7FFF/0x8000`, carry out and borrow for the U variants, saturating
  lanes (Q/UQ), halving of negative sums, `SSAT`/`USAT` at both rails and with LSL/ASR/ASR #32,
  Q sticky behaviour (a saturating instruction with Q already 1 leaves it 1, a non-saturating one
  does not clear it), `SMUAD` overflow, the exact `SMLAD` Q case, `SMULWy` negative products,
  `SMMLS` negative 64-bit results, divide by zero, `INT_MIN / -1`, unsigned division wrap.
* every case checks that registers not named as outputs keep their input value, that `r15` and
  `next_pc` do not move, that N/Z/C/V (`1/1/0/1` preset), `thumb` and `in_it` are unchanged, and
  the expected GE/Q.
* **rejected encodings**: 57 `N2A32_UNDEF` cases built by substituting a single field of an
  llvm-mc encoding (each substitution is commented): r15 in every operand position, fixed-bit
  violations, unallocated sub-spaces of the 0110/0111/0001 groups, `[6:5]` violations for
  UBFX/SBFX/BFI/BFC, `lsb + width > 32`, `msb < lsb`, `RdLo == RdHi`, `SMULxy`/`SMULWy` fixed
  fields, and ten instructions of other classes (MUL, AND, LDR, BX, MLA, SMULL, CLZ, MOVW, ADD,
  MRS) plus two T32 words. Each must return `N2A32_UNDEF` with no register or flag change.
* **sweep**: 200000 random words biased into the class's encoding space (0110/0111/0001 groups,
  plus one quarter completely random) — every word must return `N2A32_OK` or `N2A32_UNDEF`, never
  crash, and an OK return must not move the PC. It caught one real defect during development
  (`SMLALxy` with `RdLo = r15` wrote the PC).
* **every llvm-mc-producible encoding of the class executes**: 160 of the 172 encodings are
  this class's instructions (all 104 implemented mnemonics appear at least once; the other 12
  are the foreign-class/T16 words used for the UNDEF tests). The sweep covers the rest of the
  encoding space with UNDEF.

## 3. Encodings deliberately rejected (`N2A32_UNDEF`)

Everything outside the three spaces below is another package's class and is rejected. Inside the
class's spaces these are rejected:

* **0110 space** (`[27:25] = 011`, bit 4 = 1):
  - `[23:20] = 0, 4, 9, 13` — unallocated in the ARM ARM;
  - parallel add/sub with `[11:8] != 1111`, or `[7:4]` not in `{1,3,5,7,9,F}`;
  - extend forms with `[23:20]` not in `{8,A,B,C,E,F}` or bits `[9:8] != 00`;
  - `SSAT`/`USAT` with `[7:4]` not in `{1,5,9,D}` (i.e. bit 5 set) or `[23:20]` not in
    `{A,B,E,F}`;
  - `SSAT16`/`USAT16` with `[11:8] != 1111`;
  - `REV`/`REV16`/`REVSH`/`RBIT` with `[19:16] != 1111`, `SEL` with `[11:8] != 1111`;
  - `PKH` with `[23:20] != 8`;
* **0111 space**:
  - `[23:20]` in `{2,6,7,9,A}` and the non-bitfield forms of `C..F`;
  - dual multiply with `[7:4]` not in `{1,3,5,7}`;
  - `SMMUL/SMMLA/SMMLS` with `[7:4]` not in `{1,3,D,F}` (validated as bit0 = 1 and
    bit2 == bit3);
  - `USAD8/USADA8`, `SDIV`, `UDIV` with `[7:4] != 1`; `SDIV`/`UDIV` with `[15:12] != 1111`;
  - bit field with `[6:5]` not `00` (insert) / `10` (extract), extract with
    `lsb + widthm1 > 31`, insert with `msb < lsb`;
* **0001 space**:
  - `QADD/QSUB/QDADD/QDSUB` with `[11:4] != 0x05` or `[23:20]` not in `{0,2,4,6}`;
  - `SMULxy/SMLAxy/SMLALxy/SMULWy/SMLAWy` with `[7:4]` not in `{8,A,C,E}` or `[23:20]` not in
    `{0,2,4,6}`;
  - `SMULxy` and `SMULWy` with `[15:12] != 0000` (fixed field), `SMLAxy`/`SMLAWy` with `Ra = 15`;
* **r15 in any register-operand field** (PC is UNPREDICTABLE for every instruction of the class;
  SP/r13 is legal in A32 and executes);
* `SMLALxy`/`SMLALD`/`SMLSLD` with `RdLo == RdHi` (UNPREDICTABLE).

## 4. Integration notes for the core (V02)

Routing that must reach this file (also posted on the board):

    (insn & 0x0E000010) == 0x06000010   /* 0110 and 0111 media space, bit4 = 1 */
    (insn & 0x0F900FF0) == 0x01000050   /* QADD/QSUB/QDADD/QDSUB */
    (insn & 0x0F900090) == 0x01000080   /* SMULxy/SMLAxy/SMLALxy/SMULWy/SMLAWy */

The last two must be tested before any generic "0001 = data processing" rule (V10 also proposes
exactly that order). `emu/n2a32_core.c` (V02, DONE) routes all of the above to this file and
sends `[27:25]=011` words with bit4 = 0 (LDR/STR register) to `n2a32_arm_mem`; this file would
reject them anyway, which is covered by the "ldr" UNDEF test.

## 5. Not done / open questions / risks

* **Differential harness (T01)**: the kernels for this class are in the corpus as
  `tests/emu/kernels/k07_v11_media.c` (8 kernels, T01 copied them from
  `emu/test_v11/k_v11_media.c`). `llvm-objdump` on the ARM object shows the compiler emitting
  `ubfx uxth uxtb ssat pkhbt uxtah sxtb smmul rev rbit qadd qsub bfi` from these kernels
  (13 forms). `SDIV`/`UDIV` do not appear because the harness's ARM flags have no `+hwdiv`; the
  unit tests cover them from llvm-mc encodings. `SADD16`/`SEL`/`REV16`/`REVSH`/`SMUAD`-style
  forms are likewise covered by the unit tests only (clang lowers the C idioms to SMULxy/
  SMLABB, not to the dual multiply instructions; verified with `clang -march=armv7a -O2 -S`).
* `SMUSD/SMUSDX` and Q: resolved, not open (section 1). They cannot overflow.
* `USAT #0` saturates every value to 0, per "saturates to the unsigned range 0 <= x <= 2^sat - 1"
  with sat = 0 [I from the pseudocode wording and the `UnsignedSat` definition].
* `USAT`/`SSAT` with `ASR #32`: the shift count 0 in the imm5 field means 32 (A32 only), as in
  the ARM ARM; `asr32()` is written without implementation-defined shifts.
* `SMLALxy`/`SMLALD`/`SMLSLD` with `RdLo == RdHi` is rejected instead of picking an arbitrary
  write order (UNPREDICTABLE).
* A few architecturally UNPREDICTABLE operand choices are executed with a defined effect
  instead of UNDEF where the encoding is otherwise allocated: SP as a source/destination is
  accepted in A32 (the Arm ARM marks it deprecated, not undefined). Unallocated opcodes and
  fixed-field violations are rejected (section 3).
* Endianness: little-endian only, as the header requires.
* Performance: not measured (the class is a handful of instructions per call; no attempt was made
  to optimise the dispatch).
* Nothing here is a claim about the phone; V11 has no device-dependent code.

## 6. End-to-end check through the real core (V02's `n2a32_run`)

Not a test in `run_tests.sh` (it links V02's files, which may change under me), but run by hand
on 11 Nov 2026 to confirm routing and PC handling:

* after the SMUSDX correction, a 6-instruction guest program (MOVW/MOVT r1 = 0x00030002,
  MOVW/MOVT r2 = 0x00050004, `smusdx r0, r1, r2` = 0xE700F271 from llvm-mc, `bx lr`) run through
  the current core with `n2a32_call` returned `N2A32_STOP_RETURN` and **r0 = 0xFFFFFFFE**, the
  corrected `Rn.lo*Rm.hi - Rn.hi*Rm.lo` = `2*5 - 3*4 = -2` (the old orientation would have given
  +2). This also confirms that V02's final A32 route sends 0111 media words to this file.
* an earlier 26-instruction program with 22 of this class's instructions (SXTB SXTB16 UXTB UBFX
  BFI REV QADD SADD16 SSAT SMULBB SMMUL USAD8 SMLABB QDSUB SHSUB16 UQADD8 USADA8 SMLALD PKHBT)
  returned `N2A32_STOP_RETURN` and matched independent arithmetic: r0 = 0xFFADFFEF (SXTB16),
  r1 = 0xFFFFBEEF (PKHBT), r10 = 0x1D34E48C (SMMUL), r11 = 0x00000224 (USAD8),
  r12 = 0x00003400 (BFI), r7 = 0x000000EE (UBFX), q = 1 (sticky from QADD's saturation of
  `0x56781234 * 2`), ge = 0xC.

A first version of that program also contained `mov r7, lr` (V10's class); the draft
`a32_route` in `emu/n2a32_core.c` at that time sent it to `n2a32_arm_mem` and the run stopped
UNDEF there — the routing defect V10 and T01 already reported to V02, not a V11 defect.

## 7. Acceptance item "census shows no UNDEF mnemonic in use"

**Satisfied (vacuously), on the evidence of hw-d01's census.** D01's recursive walk over all five
vendor objects (`facts/census.py`, posted on the board and in `FACTS.md` "instruction census")
found 309511 reachable instructions and **zero A32 instructions**: every defined FUNC symbol and
every `.ARM.exidx` entry is Thumb (st_value bit 0 set) except one size-0 Thumb alias. Therefore no
A32 mnemonic of this class is used by the vendor libraries at all, and no V11 UNDEF can intersect
their instruction stream. The list to check against would be section 3 above. A shortcut was tried
and rejected as unsound: scanning the vendor `.text` for 4-byte-aligned words that match this
class's masks finds mostly Thumb-2 halfword pairs, so it says nothing about A32 code. All 104
implemented mnemonics are covered by an llvm-mc encoding in `v11_enc.s`; the residual risk is an
A32 encoding the ARM ARM assigns elsewhere that some future (non-vendor) A32 caller uses.

## 8. Differential harness status (T01)

The kernels are in the corpus as `tests/emu/kernels/k07_v11_media.c` (T01 copied the 8 kernels
from `emu/test_v11/k_v11_media.c`). I ran the official harness on 11 Nov 2026 with the current
core:

    N2HW_TMP=/tmp/n2hw-V11/diff bash gpu/hwcodec/tests/emu/run_diff.sh
    N2TEST T01 FAIL 6283/14700 calls differ

**All V11 kernels pass in every ARM configuration** (the four arm/armv8 columns):

    configuration    calls     pass mismatch     stop  missing
    arm-O0           2100     1860        0      240        0
    arm-O2           2100     1352        0      748        0
    arm-O3v          2100     1552        0      548        0
    armv8-O3v        2100     1586        0      514        0

The stops in those columns are other packages' kernels: `k_f*` at `ee000a10` (V17 VFP, not in
the tree) and the same for the NEON kernels. The harness's overall FAIL comes from the
`thumb-*` columns, where `k_v11_*` stops are all on T32 instructions outside this class
(`f7ffffd4` = T32 BL, `f001ec22` = T32 BL, ...; V16 T32 branches/load-store are not complete
yet) — the stop is reported against the kernel's name but the instruction belongs to V16. No
V11 kernel has a value mismatch in any configuration.

This is real differential evidence for the forms clang emits from these kernels: `ubfx uxtb
uxtb16 sbfx bfc rev rbit qadd ssat sxtb smmul pkhbt` and the SMULxy/SMLABB family. `pkhbt`
appears in the ARM -O2/-O3v/armv8 objects of `k_v11_pack` (at -O0 clang uses `and`/`orr`
instead), so both PKH forms are also differentially checked. It does not cover the dual-multiply
family, the 8/16-bit parallel families or `SEL`: clang does not emit `smusd`/`smusdx` from the C
idiom (checked with `clang -march=armv7a -O2 -S`; it emitted `smulbt`/`smulbb` instead), and the
parallel/SEL idioms become `and`/`orr`/byte tests, so those remain covered by the unit tests
only.
