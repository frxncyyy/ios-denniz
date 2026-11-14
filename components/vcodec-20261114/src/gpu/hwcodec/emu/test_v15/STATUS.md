# V15 — T32 32-bit data processing: status

Package V15 of `gpu/hwcodec`. Owner: `hw-v15`. Host-tested only: **nothing here
has run on the phone or under the real core**. Last update 11 November 2026.

## 1. What is implemented

`gpu/hwcodec/emu/n2a32_t32_dp.c` implements `n2a32_t32_dp()` of
`include/n2a32.h` exactly under the stated contract: it never tests the
condition, never touches `r[15]`, `next_pc`, `thumb` or `in_it`, never accesses
memory, never branches, never prints and never aborts. It decodes four
encoding regions and returns `N2A32_UNDEF` for everything else:

| Region | Mask / match (32-bit word = first halfword << 16 \| second) | Content |
|---|---|---|
| A | `(i & 0xFE000000) == 0xEA000000` | data processing, shifted/plain register, 0xEA00-0xEBFF |
| B | `(i & 0xFA008000) == 0xF0000000` | data processing, modified immediate (hw2 bit 15 = 0) |
| C | `(i & 0xFA008000) == 0xF2000000` | data processing, plain binary immediate (hw2 bit 15 = 0) |
| D | `(i & 0xFE000000) == 0xFA000000` | data processing, register, 0xFA00-0xFBFF |

Mnemonics (all executed):

* Region A (0xEA00-0xEBFF): `AND BIC ORR ORN EOR MOV MVN TST TEQ` with
  `LSL/LSR/ASR/ROR/RRX` immediate shifts and with the plain register form;
  `PKHBT PKHTB`; `ADD ADC SBC SUB RSB CMN CMP` with the same shift variants.
  The `TST/TEQ/CMN/CMP` aliases (Rd = 1111 with S) and the `MOV/MVN` aliases
  (Rn = 1111) are the only forms with r15 in those fields, and they never write
  r15.
* Region B (modified immediate): the same ops except `PKH` (no immediate form)
  and including `MOV/MVN` and the compare aliases. `ThumbExpandImm_C`
  implements all four `imm12<11:10> == 00` replicated-byte cases and the
  rotated case (carry out = bit 31 of the expanded value).
* Region C (plain binary immediate): `ADDW SUBW ADR MOVW MOVT` (ADR is
  ADDW/SUBW with Rn = 1111), `SSAT SSAT16 USAT USAT16` (LSL and ASR shifts,
  halfword forms selected by bit 5 with a zero shift field), `SBFX UBFX`
  (width 1..32), `BFI BFC` (width 1..32).
* Region D (0xFA00-0xFAFF): `LSL LSR ASR ROR` by register (with S),
  `SXTB SXTH UXTB UXTH SXTB16 UXTB16` and the accumulating `SXTAB SXTAH UXTAB
  UXTAH SXTAB16 UXTAB16` with all four byte rotations,
  `REV REV16 REVSH RBIT CLZ SEL`,
  the 36 parallel add/subtract instructions `SADD8 SADD16 SASX SSUB8 SSUB16
  SSAX QADD8 QADD16 QASX QSUB8 QSUB16 QSAX SHADD8 SHADD16 SHASX SHSUB8 SHSUB16
  SHSAX UADD8 UADD16 UASX USUB8 USUB16 USAX UQADD8 UQADD16 UQASX UQSUB8 UQSUB16
  UQSAX UHADD8 UHADD16 UHASX UHSUB8 UHSUB16 UHSAX`, and
  `QADD QSUB QDADD QDSUB`.
* Region D (0xFB00-0xFBFF): `MUL MLA MLS`, `SMULL UMULL SMLAL UMLAL UMAAL`,
  `SDIV UDIV`, `SMULBB SMULBT SMULTB SMULTT`, `SMLABB SMLABT SMLATB SMLATT`,
  `SMULWB SMULWT SMLAWb SMLAWt`, `SMLALBB SMLALBT SMLALTB SMLALTT`,
  `SMUAD SMUADX SMUSD SMUSDX SMLAD SMLADX SMLSD SMLSDX`,
  `SMLALD SMLALDX SMLSLD SMLSLDX`, `SMMUL SMMULR SMMLA SMMLAR SMMLS SMMLSR`,
  `USAD8 USADA8`.

Flag/state rules implemented from the ARM ARM: AddWithCarry for the arithmetic
ops, `Shift_C` with the amount clamped to 32 (LSL/LSR/ASR) and reduced modulo 32
(ROR) after the "amount 0" case, RRX for the immediate ROR #0 encoding, the C
flag as the carry-in of ADC/SBC (the shifter/expander carry output only updates
C for the logical ops - confirmed by T01's differential corpus and by clang's
`adcs; adc.w rHi, rHi, Ry, asr #31` 64-bit sign-extend idiom), sticky `Q`
on saturation, `GE<3:0>` from the parallel ops (signed: sign of the unhalved,
unsaturated lane sum; unsigned: lane carry for additions and no-borrow for
subtractions; the byte forms set one bit per lane, the halfword forms set the
two-bit fields GE<1:0> and GE<3:2>, both bits equal - hw-r1 finding F3), `SEL`
from `GE<3:0>`.

## 2. Encodings deliberately left N2A32_UNDEF

These are UNPREDICTABLE or unassigned per the ARM ARM; the class never guesses:

Region A:
* op values 0101, 0111, 1001, 1100, 1111 (not assigned).
* Rn = 1111 for any op other than ORR/ORN (MOV/MVN are the only legal aliases).
* Rd = 1111 unless S = 1 and op is AND/EOR/ADD/SUB (TST/TEQ/CMN/CMP).
* Rm = r15.
* PKH with S = 1, with Rn = 1111, with Rd = 1111, or with shift type 01/11.

Region B:
* op 0101, 0110 (PKH has no immediate form), 0111, 1001, 1100, 1111.
* the same Rn/Rd rules as region A.
* second halfword bit 15 = 1 (that is the `B<c>.W`/`B.W`/`BL`/`BLX` space).

Region C:
* op5 values other than 0, 4, 10, 12, 16, 18, 20, 22, 24, 26, 28.
* Rd = 1111 (all forms).
* Rn = 1111 in SSAT/SSAT16/USAT/USAT16/SBFX/UBFX.
* SSAT/USAT with shift type 01/11; SSAT16/USAT16 with the sat field above 15.
* SBFX/UBFX with lsb + width > 32; BFI/BFC with msb < lsb.
* second halfword bit 15 = 1 (branch space).

Region D (0xFA00):
* second halfword bits 15:12 != 1111.
* shift-by-register with op1 0..7 and op2 != 0; extend with op1 0..5 and op2
  outside 8..11 (bits 7:6 = 11 is the undefined pattern; llvm's disassembler
  accepts it, the ARM ARM does not); op1 6/7 with op2 8..15.
* parallel shapes op1 not in {8,9,10,12,13,14} or variants op2 not in
  {0,1,2,4,5,6}; QADD family op1 = 8 with op2 outside 8..11; REV family
  op1 = 9 with op2 outside 8..11; SEL/CLZ with op2 != 8.
* r15 as Rd, Rn or Rm.

Region D (0xFB00):
* unassigned op1/op2 combinations (op1 9 or 11 with op2 != 15, op1 15,
  op2 2..5 for SMULxy, op2 != 0 for USAD8, SMULL/UMULL/SMLAL/UMLAL with
  op2 != 0 except the listed SMLALxy/SMLALD/SMLSLD/UMAAL combinations, ...).
* r15 as Rd, RdLo, RdHi or Rm; RdLo == RdHi.
* MLS/SMMLS with Ra = 1111 (architecturally UNPREDICTABLE; llvm decodes them
  with pc as the addend).

Region C note: bit 10 (`i`) is ignored by the ops that do not use imm12
(SSAT/SSAT16/SBFX/UBFX/BFI/BFC), matching llvm's disassembler, which accepts
both values for SBFX/UBFX. MOVW/MOVT treat bits 3:0 of the first halfword as
`imm4`, not as Rn.

## 3. Tests

```
$ bash gpu/hwcodec/emu/test_v15/run_tests.sh
gen_enc: 263 vectors -> /tmp/n2hw-V15/enc_gen.h
N2TEST V15 PASS 1764/1764
N2TEST V15 PASS 1764/1764
```

The runner prints the line twice: once for `clang -O1 -g -Wall -Wextra -Werror
-fsanitize=address,undefined -fno-sanitize-recover=all` and once for `-O2`
without sanitizers. It builds only under `/tmp/n2hw-V15` (override with
`N2HW_TMP`), takes about 4 s, and writes nothing into the work tree.

* 263 instruction encodings come from `vectors.s` through `llvm-mc
  -triple=thumbv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8
  --show-encoding`, read back from the object file (so the two ADR fixups are
  resolved by llvm-mc). No hand-typed hexadecimal instruction appears in the
  test. `gen_enc.py` fails the build if a vector is not exactly one 32-bit word.
* 1764 checks: every expected register, flag, Q and GE value is a literal
  worked out from the ARM ARM pseudocode, covering carry-out, overflow, zero
  and negative flag edges, shift amounts 0/1/31/32/33, RRX with C = 0/1, all
  four rotations of every extend, SP as an ADD/SUB operand, r15 as the ADR
  base and in the compare/MOV aliases, saturation just inside/outside the
  range, sticky Q, divide by zero, INT_MIN / -1, UMAAL carries, and the two
  ADC/SBC carry-in (C flag; regression case from T01's differential run).
* 19 must-be-UNDEF encodings, each derived from an assembled vector by changing
  a field (never typed in), and each also checked to leave the PC state alone.
* A 200000-word sweep: half the words are drawn from the class's four regions,
  half are uniformly random; every call must return OK or UNDEF, and must leave
  `r[15]`/`next_pc` unchanged. Runs under ASan and UBSan.
* Decode coverage was additionally cross-checked once (not in run_tests.sh)
  against `llvm-objdump` over ~4000 structured words covering every op field,
  shift type, register and immediate form of the four regions: the only words
  llvm decodes as a V15 mnemonic that this class rejects are the three
  UNPREDICTABLE encodings SBFX/UBFX with lsb+width > 32 and BFI with msb < lsb.
* Compiler-oracle spot checks (clang -mthumb -O2, disassembly inspected): the
  encodings clang emits for `(x>>7)&0x1ff` (ubfx 7,9), `(x<<5)>>25` (sbfx
  20,7), `(x&~0xff00)|((y&0xff)<<8)` (bfi 8,8), `__builtin_bswap32` (rev),
  `(int16_t)x` (sxth), `(x&0xffff0000)|(y&0xffff)` (pkhbt) and
  `(x&0xffff0000)|((y>>8)&0xffff)` (pkhtb asr 8) decode with the same lsb,
  width and half selection that this class implements; `umull r1,r0,r1,r0` and
  `smull r0,r1,r1,r0` confirm RdLo = second<15:12> and RdHi = second<11:8>;
  `smulwb`, `smmul`, `smmlar`, `qadd` and `qsub` confirm the operand orders of
  the 0xFB00 class and the QADD family; `ssat`/`usat` confirm the sat field
  position. This is what caught the ADC/SBC and PKH bugs (reported by T01); the
  corresponding literals are in the unit tests.
* Portability compile (not an iOS SDK build, no SDK on this host): the file
  builds clean with `clang -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding`
  for `arm64-apple-ios16.0` (Mach-O arm64 object), `aarch64-linux-gnu` and
  `armv7a-linux-gnueabi -mfpu=neon-vfpv4`.
* Census (hw-d01, 11 November): 32166 reachable 32-bit T32 data processing
  instructions in the five vendor objects, 91 distinct mnemonics. All 91 are
  implemented (`movw movt add.w cmp.w mov.w and orr.w mvn sub.w mla addw bic
  lsl.w and.w lsr.w clz ubfx udiv orr smlabb adr.w mls mul eor.w rsb.w bfi
  tst.w eor uxtb.w adds.w rsb ands mov*.w subs.w bfc adc sdiv umull uxtab
  lsls.w asr.w subw bic.w orrs.w usat mvns.w adc.w pkhbt smlal sbc.w ssat
  sbcs cmn.w mvn.w rev.w rrx uxtah asrs.w lsr*.w sbc eors sbfx smull smulwb
  teq.w umlal uxth.w`), so no mnemonic used by the vendor libraries returns
  UNDEF.

## 3a. Optional integration check (not part of the PASS line)

```
$ bash gpu/hwcodec/emu/test_v15/check_routing.sh
V15-ROUTE PASS 10/10
```

Ten T32 snippets (one per encoding region plus bit fields, saturation,
multiplies, register shifts, extends/reverse, parallel/QADD) are entered from ARM
mode with BX, executed through V02's real core and V14's 16-bit `bx lr`, and
their results checked. This is what proves that `t32_route()` sends every V15
region to this class and that the core's 32-bit fetch (`first << 16 | second`)
matches. It needs `tests/emu/asmrun.sh` (T01) and the core in the tree; it builds
under `/tmp/n2hw-V15/route`. UDIV/SDIV cannot be assembled there because asmrun
calls llvm-mc without `+hwdiv`; they are covered by the unit tests.

## 3b. Differential kernels for T01

`emu/test_v15/k15_t32_dp.c` (8 kernels, `k_v15_*`, install as
`tests/emu/kernels/k15_v15_t32_dp.c`). Compiles clean for
armv7a/thumb and armv7a/arm at -O0/-O2 with `-ffreestanding` and on the host
with `-Wall -Wextra -Werror`; all kernels are seed-pure on 20 seeds. The
instruction mix they generate was verified with llvm-objdump.

## 4. Not done / open

* No execution on the phone and no execution under the real core yet: this is
  a host-tested class only. V02's `t32_route` already routes the four regions
  to this function (checked line by line against the masks above), but the
  integrated T32 path has not been run by me.
* ADC/SBC with a modified immediate are implemented but **cannot be tested**
  with this oracle: llvm-mc refuses to assemble them ("operand must be a
  register"), though llvm-objdump decodes them (encoding 11110 i 0 1010/1011).
  The vendor set cannot contain them for the same reason.
* PKHTB with a zero shift field: the assembler never emits it (llvm
  canonicalises `pkhtb r0,r1,r2` to `pkhbt r0,r2,r1`, which is only equivalent
  if the zero shift means "no shift"). This class implements no shift for that
  encoding; llvm-objdump prints it as `asr #32`, which is a disassembly-text
  choice that no toolchain output relies on. The unit test derives the encoding
  from an assembled PKHBT by setting the type field.
* The parallel add/sub GE rules (GE from the unhalved, unsaturated lane sum;
  two GE bits per halfword lane) were corrected after hw-r1 finding F3. No
  vendor library uses any parallel instruction, so this cannot be validated
  against real code; the interpretation is stated in the source and the repro
  from the review is a unit case.
* `USAT16` saturates the zero-extended halfword (unsigned input); `SSAT16` the
  sign-extended one. Verified against the ARM ARM's UnsignedSat(SInt?) wording
  by construction of the test (0xFFFF with `#15` gives 0x7FFF, not 0).
* REV/REV16/REVSH/RBIT/CLZ are single-source encodings that carry the register
  in both `first<3:0>` and `second<3:0>`; NDK clang writes the same register in
  both. This class prefers `second<3:0>` and falls back to `first<3:0>` when it
  is 1111.
* Differential kernels for T01's harness: not written. A T32 kernel would need
  the core and V16 (T32 branches/load-store) to be complete before it can run,
  so it is deferred until the core's T32 path executes; T01 has been told.

## 5. Files

* `gpu/hwcodec/emu/n2a32_t32_dp.c` — the class (MIT).
* `gpu/hwcodec/emu/test_v15/{run_tests.sh,gen_enc.py,vectors.s,t_dp.c}` — the
  host tests (MIT).
* `gpu/hwcodec/emu/test_v15/check_routing.sh` — optional real-core routing
  check (MIT).
* `gpu/hwcodec/emu/test_v15/k15_t32_dp.c` — differential kernels for T01 (MIT).
* `gpu/hwcodec/dsh-state/workers/V15/STATUS.md` — handoff copy.
