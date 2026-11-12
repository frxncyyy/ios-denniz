# V10 — A32 data processing, moves, multiplies: status

Package V10 of `gpu/hwcodec` track V. Owner files: `gpu/hwcodec/emu/n2a32_arm_dp.c`,
`gpu/hwcodec/emu/test_v10/**`. Interface: `gpu/hwcodec/include/n2a32.h` (frozen), only
`n2a32_arm_dp()`.

Last update: 11 November 2026. **Host-tested only. Nothing has run on the phone; nothing has
executed on a real ARM core.** "Expected" values come from the ARM ARM pseudocode and
llvm-mc encodings, not from hardware.

## What is implemented

`n2a32_arm_dp(c, insn)` decodes and executes, for A32 with any condition except `0xF`:

| Group | Mnemonics / forms |
|---|---|
| Logical | AND EOR ORR BIC, MOV MVN, with immediate (ARMExpandImm_C), register with immediate shift, register with register-specified shift; S variants |
| Arithmetic | SUB RSB ADD ADC SBC RSC, same three operand forms, S variants |
| Compare/test | TST TEQ CMP CMN, same three operand forms (always set N,Z,C,V) |
| Move immediate | MOVW, MOVT |
| Multiply | MUL MLA MLS (32-bit); UMULL UMLAL SMULL SMLAL UMAAL (64-bit, S where it exists) |
| PSW | MRS Rd, CPSR; MSR APSR_nzcvq / APSR_g / APSR_nzcvqg, Rm (register form) |
| Other | CLZ Rd, Rm; ADR (ADD/SUB immediate with Rn = r15); ALU write of r15 (S=0) branches through `n2a32_bx()` |

Shift semantics implemented exactly as the ARM ARM: LSL/LSR/ASR/ROR by 0..31 and the special
encodings LSR #32, ASR #32, RRX, LSL #0; register-specified shifts with amounts 0, 1..31, 32,
33..255 (LSL/LSR produce 0 with the documented carry; ASR saturates to the sign; ROR uses the
amount mod 32 and carry = bit 31 for a multiple-of-32 amount). Immediate operand uses
ARMExpandImm_C: rotate = 0 leaves C unchanged, otherwise C = bit 31 of the expanded value.
Flags: N,Z from the result; C = shifter carry for logical ops and the adder carry for
arithmetic ops; V is written only by arithmetic ops (and left unchanged by logical ops); ADC,
SBC and RSC use the *current C flag* as carry-in, not the shifter carry-out; Q and GE are never
touched; multiply S variants set N,Z only (C,V unchanged); MLS and UMAAL have no S variant.

## Test command and result

    bash gpu/hwcodec/emu/test_v10/run_tests.sh

Last lines (ASan+UBSan build/run, then -O2 build/run):

    V10: 542 cases, 47 undef encodings, sweep ok=150783 undef=69217
    N2TEST V10 PASS 220589/220589
    V10: 542 cases, 47 undef encodings, sweep ok=150783 undef=69217
    N2TEST V10 PASS 220589/220589

The test drives `n2a32_arm_dp()` directly on a `struct n2a32_cpu` whose `mem->base` is a 16 MiB
`calloc`, with `r[15]` = 0x1008 and `next_pc` = 0x1004 as the core would set them, and checks
all 15 registers, N/Z/C/V/Q/GE, `thumb`, `next_pc`, and that `r[15]` is never written directly.

* `cases_v10.inc`: 542 cases. Every ALU mnemonic has 7 immediate + 10 immediate-shift + 9
  register-shift cases, plus two cases with r15 as an ALU data operand; every shift variant
  (LSL/LSR/ASR/ROR/RRX, #0, #32, register amounts 0/32/33/255) is covered; carry-out, carry-in, overflow, zero and negative flag edges are
  present; r13 (SP) and r15 (ADR, and ALU write of PC with and without bit 0 set) are covered;
  MOVW/MOVT, all eight multiplies (including 64-bit zero/negative/carry cases, SP/LR
  destinations and UMAAL carry chains), MRS/MSR, CLZ and ADR each have 6+ cases.
* `undef_v10.inc`: 47 encodings that must return `N2A32_UNDEF` **and leave the CPU state
  unchanged** (BX, BLX, QADD/QSUB/QDADD, LDRH/STRH/LDRSB/LDR/LDRD/STRD/LDM/STM/PUSH/POP,
  NOP/YIELD/WFE/WFI/SEV/DBG, MSR immediate, MRS/MSR SPSR, MSR with fsxc mask, movs pc/adds pc,
  PLD, B/BL, SVC, BKPT, UDF, SDIV/UDIV, REV/SXTB/PKH/SEL/SSAT/USAT, SWP, MRC, CLREX, LDREX,
  STREX, MRS/MSR SPSR, ...).
* Sweep: 200000 pseudo-random words of this class's encoding space (bits 27-26 = 00, random
  condition including 0xF, bit 25, opcode, S, registers and shift fields) plus 20000 fully
  random words, under ASan+UBSan and again at -O2. Every word either executes or returns
  `N2A32_UNDEF`; no crash, no direct r15 write, flag fields stay in range, and UNDEF leaves the
  state unchanged.

Encodings are never hand-typed: `mkenc.py` runs
`llvm-mc -triple=armv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8
--show-encoding` on `encodings.txt` (assembly text in this directory) and emits `enc_v10.h`.
`gen_cases.py` is the throw-away ARM-ARM calculator that produced the literals in
`cases_v10.inc`; it is kept for provenance and is **not** run by `run_tests.sh`.

### Optional core-routing check

    bash gpu/hwcodec/emu/test_v10/check_routing_v10.sh

Not part of `run_tests.sh` (it needs T01's asmrun and V02's core, which are other packages).
It assembles 12 A32 snippets covering this class and requires `stop=ok` from the real core.
Result against the tree: **`V10-ROUTE PASS 12/12`** (core of 12:11). Before that core
revision it failed 11/12 on `0xe0800351` = `add r0, r1, r2, asr r3`, which the core routed to
arm_media because its case 0 tested `op2 == 5` instead of V11's exact QADD mask; reported and
fixed by hw-v02. The 12 snippets cover ALU immediate/register, immediate shifts (incl. RRX and
LSR/ASR #32), MOVW/MOVT, all multiplies incl. UMAAL, MRS/MSR APSR, CLZ, ADR, ALU write of PC
and TST/TEQ/CMP/CMN.

## Deliberately N2A32_UNDEF

Outside the class (owned by other packages): BX, BLX (register and immediate); all hints
(NOP/YIELD/WFE/WFI/SEV/DBG/SEVL); MSR (immediate); every load/store (LDR/STR/LDRB/LDRH/LDRSB/
LDRSH/LDRD/STRD/LDM/STM/LDREX/STREX/PLD/PLI/PUSH/POP); coprocessor (MRC/MCR/CDP/LDC/STC,
including cp10/cp11, which are V17); SVC, BKPT, UDF, B, BL; media and DSP (QADD/QSUB/QDADD/
QDSUB, SMULxy/SMLAxy/SMLALxy/SMULWy/SMLAWy, SDIV/UDIV, PKH, SEL, SXT/UXT, SSAT/USAT, REV/RBIT,
parallel add/sub); every word with cond = 0xF (unconditional space).

Inside the class, deliberately UNDEF:

| Encoding | Why |
|---|---|
| MRS Rd, SPSR (op 10, S=0, Rn=1111, imm12=0) | no SPSR in User mode |
| MSR SPSR_<mask>, Rm (op 11, S=0) | no SPSR in User mode |
| MSR CPSR_<mask>, Rm with mask bit 0 (control) or bit 1 (x) set, e.g. `msr cpsr_fsxc, r0` | those fields are not writable in User mode |
| MSR (immediate) (e.g. `msr apsr_nzcvq, #0`, 0xE328F000) | architecturally UNDEFINED in User mode |
| data processing with S=1 and Rd=15 (`movs pc, lr`, `adds pc, r0, #4`) | ALUExceptionReturn, privileged |
| MOVW/MOVT with Rd=15 | UNPREDICTABLE |
| MRS with Rd=15; MSR with Rm=15; CLZ with Rd=15 or Rm=15 | UNPREDICTABLE |
| MUL/MLA/MLS/UMULL/UMLAL/SMULL/SMLAL/UMAAL with any register field = 15 | UNPREDICTABLE |
| MLS or UMAAL with bit 20 (S) set | no S variant exists (unallocated) |
| register-space TST/TEQ/CMP/CMN with S=0 that are not the exact MRS/MSR patterns | unallocated |
| immediate-space opcodes 9/11 with S=0 other than MOVW/MOVT (hint/MSR-immediate space) | V12 |
| bits 27-25 = 000, bit 4 = 1, bit 7 = 1 that are not multiply (extra load/store and unallocated) | V12 / unallocated |
| every word with cond = 0xF | unconditional space |

Executed although the ARM ARM calls the *operand combination* UNPREDICTABLE (documented choice,
because real hardware does this and vendor code may rely on it): r15 read as an ALU operand via
`c->r[15]` (this is what makes ADR work; also `add r0, pc, r1`, `and r0, r1, pc`); TST/TEQ/CMP/
CMN ignore their Rd field; MOV/MVN ignore their Rn field; overlapping registers in MUL/MLA/MLS
use the pre-instruction values.

## Integration and open items

1. **T01 differential harness — done and green for V10.** The ten `k_v10_*` kernels in
   `emu/test_v10/k10_dp.c` are installed by T01 as `tests/emu/kernels/k10_v10_dp.c`.
   Final in-tree run at 12:15 with the core of 12:11 (`bash gpu/hwcodec/tests/emu/run_diff.sh`,
   97 kernels, 20 seeds, seven configurations):

       configuration    calls     pass mismatch     stop  missing
       arm-O0           1940     1700        0      240        0
       arm-O2           1940     1212        0      728        0
       arm-O3v          1940     1392        0      548        0
       thumb-O0         1940       25       15     1900        0
       thumb-O2         1940      858       33     1049        0
       thumb-O3v        1940      898       33     1009        0
       armv8-O3v        1940     1426        0      514        0

   **No `k_v10_*` call stops in any of the four ARM configurations** (verified in the four
   per-configuration logs) and the ARM mismatch count is 0, so every V10 instruction executed
   identically to the native x86-64 reference. The `k_v10_*` "failing calls" listed by
   diffcheck are all `thumb-O0/O2/O3v` stops on T32 32-bit instructions (V15/V16/V17-V20 not
   complete yet); the overall `N2TEST T01 FAIL` is other packages' stops/mismatches (VFP, NEON,
   T32), not V10. Earlier history: the harness could not build until 11:53 (V02's
   `n2a32_mem.c` tautological comparisons) and misrouted ORR/MOV/BIC/MVN and shift-by-register
   ASR until 12:11 (a32_route case 0); both were reported with exact patches and fixed.
   The ARM -O2 kernel object contains (llvm-objdump) and 34, ands 6, orr 51, orrs 1, eor 337,
   bic 8, mvn 12, add 962, adds 4, sub 99, subs 12, rsb 11, rsbs 4, adc 4, sbc 4, sbcs 3, cmp
   71, cmn 12, tst 11, mov 223, movw 194, movt 173, mul 7, mla 7, mls 4, umull 14, umlal 4,
   smull 8, smlal 14, clz 3, plus `add rX, pc, #imm` ADR forms and `add pc, rX, rY`
   ALU-write-PC forms, so the differential result covers the class broadly.
2. **D01 census (posted 12:02, FACTS.md section pending): zero reachable A32 instructions in
   all five vendor objects.** Every defined FUNC symbol (2375 of them) and every .ARM.exidx
   entry has st_value bit 0 set (Thumb); the census walked 323577 instructions, all Thumb, and
   no .text block scores as plausible A32. So the acceptance condition "no mnemonic of this
   class used by the vendor libraries is returned UNDEF" holds: none are used. Consequence
   stated honestly: this class is **not on the vendor hot path**; it is required by
   include/n2a32.h and is exercised by the unit tests and the (currently blocked) A32
   differential corpus. D01 offers no synthetic A32 corpus because no reachable A32 code
   exists; the differential kernels in emu/test_v10/k10_dp.c are the A32 coverage.
3. **MRS CPSR value is a choice**: `APSR | 0x1D0` (A, I, F read as 1 in User mode, E = 0,
   mode = User) plus T and the IT bits. Real ARMv8 AArch32 EL0 hardware is expected to read
   A=I=F=1; if V16's T32 MRS uses something else the two modes would disagree. Posted to
   hw-v16 for a shared formula.
4. **Decode boundary with V02's routing table**: their draft row "b24==1 && b23-20>=8 ->
   arm_mem" would misroute ORR/MOV/BIC/MVN (bit24=1 for opcodes 8-15; bits 23-20 = 8..15 for
   ORR/MOV/BIC/MVN), and V12's extra-load/store mask also matches MUL/MLA/MLS/UMAAL. Exact
   masks and ordering posted to hw-v02; if the core's table keeps the draft rule, those four
   mnemonics stop with UNDEF. Their hint mask also misses DBG (0xE320F0F0); pointed out.
5. **MSR control/x masks stop instead of being ignored.** Real hardware may silently ignore
   writes to CPSR[7:0]/[15:8] in User mode. I chose UNDEF (architecturally UNPREDICTABLE /
   not permitted) and listed it; if the census finds `msr cpsr_c` in user-mode vendor code,
   this becomes a real incompatibility and should change to "ignore those bits".
6. No phone test and no real-hardware evidence of any kind; the JPEG/VENC work is unrelated to
   this package.
