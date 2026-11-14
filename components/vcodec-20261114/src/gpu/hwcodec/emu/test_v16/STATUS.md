# V16 -- T32 32-bit load/store, branches and system: package status

Owner: hw-v16.  Files: `gpu/hwcodec/emu/n2a32_t32_mem.c`, `gpu/hwcodec/emu/n2a32_t32_misc.c`,
`gpu/hwcodec/emu/test_v16/**` (this directory).  Interface: `include/n2a32.h` only (frozen).

## Test command and last line

    $ bash gpu/hwcodec/emu/test_v16/run_tests.sh
    V16 checks 3284977
    N2TEST V16 PASS 3284977/3284977      <- ASan+UBSan, clang -O1 -fsanitize=address,undefined
    V16 checks 3284977
    N2TEST V16 PASS 3284977/3284977      <- clang -O2, no sanitizers

(The runner prints the verdict twice: once per build.)  About 4 s total, builds under
`/tmp/n2hw-v16/build`.  All 3284977 checks are ASan/UBSan clean.

Encodings are produced by
`llvm-mc -triple=thumbv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8
--show-encoding` from the assembly text in `gen_enc.py` (never hand-typed hex); expected
values are literals derived from the ARM ARM pseudocode (for the branches, the immediate
offsets are the literals the assembler was asked to encode).

## What is implemented (host-tested; nothing has run on the phone)

`n2a32_t32_mem` (region `0xE800-0xE9FF` and `0xF800-0xF9FF`):

| Mnemonic | Forms |
|---|---|
| LDR, STR (word) | imm12, imm8 (negative offset, post-index +/-, pre-index writeback +/-), register (Rm << 0-3), literal (+/-), `LDR pc` (interworking branch), `STR pc` |
| LDRB, STRB | same forms except no writeback-to-pc and no byte store of pc |
| LDRH, STRH | same forms |
| LDRSB, LDRSH | imm12, imm8, register, literal |
| LDRT, STRT, LDRBT, STRBT, LDRHT, STRHT, LDRSBT, LDRSHT | the unprivileged imm8 form (P=1,U=1,W=0): offset applied, no writeback |
| LDRD, STRD | imm8 offset, pre-index writeback, post-index, literal; any Rt/Rt2 pair (see below) |
| LDM/STM | IA and DB, with and without writeback, PC in the list (interworking branch), PUSH wide (STMDB sp!), POP wide (LDMIA sp!) |
| LDREX, STREX | word, imm8 offset scaled by 4 |
| LDREXB/H/D, STREXB/H/D | same-address exclusives, monitor through `n2a32_excl_*` |
| CLREX | clears the local monitor (also accepted by the misc class) |
| TBB, TBH | Rn or PC base, byte/halfword table entry, `n2a32_branch()` |
| PLD, PLDW, PLI | every addressing form the assembler emits (PLDW comes from a second llvm-mc run with `+mp`, the one extra feature it needs; the phone's libraries contain `pldw` at libvcodecdrv.so 0xf9c10); address generation only, no access |

`n2a32_t32_misc`:

| Mnemonic | Forms |
|---|---|
| B.W (T4) | 25-bit immediate, whole range |
| B<c>.W (T3) | all 14 conditions, evaluated with `n2a32_cond()`; 21-bit immediate |
| BL (T1) | 25-bit immediate, LR = PC \| 1, stays in Thumb |
| BLX (immediate, T2) | 25-bit immediate including the H bit, target = Align(PC,4) + imm, LR = PC \| 1, switches to ARM state via `n2a32_bx()` |
| MRS | `MRS Rd, APSR` (SYSm = 0) |
| MSR | `MSR APSR_nzcvq`, `APSR_g`, `APSR_nzcvqg, Rn` |
| NOP, YIELD, WFE, WFI, SEV, SEVL, DBG | every 32-bit hint encoding, all no-ops (unallocated hint numbers too) |
| DMB, DSB, ISB | no-ops in this execution model (any option byte) |
| CLREX | clears the local monitor |
| UDF.W #imm16 | sets `c->stop = N2A32_STOP_UNDEF` and returns `N2A32_UNDEF` |
| MRC p15, 0, Rt, c13, c0, 3 | `Rt = c->tpidruro` |

## Every encoding deliberately returning N2A32_UNDEF

Memory class:

1. LDM/STM with `Rn == 1111`, an empty register list, SP (bit 13) in the list, or a first
   halfword whose bits 7 and 8 are equal (neither IA nor DB).
2. LDRD/STRD with Rt or Rt2 == 1111, a literal form with writeback, or the unallocated
   (P,U,W) = (0,*,0) combination.
3. LDREX/STREX (word) with Rt or Rd == 1111; LDREXB/H/D and STREXB/H/D with any register-15
   data register, a fixed 1111/Rt2 field that is not 1111 where required, or `Rn == 1111`.
4. LDREXD/STREXD with odd Rt, Rt > 12, encoded Rt2 != Rt+1, or Rd == 1111.
5. TBB/TBH with Rm == 1111, second-halfword bits 11-8 != 0, bits 7-5 != 0, or Rt != 1111.
6. Byte/halfword store with Rt == 1111 (no store-PC form exists).
7. Load/store single with op 6, 7, 14 or 15, and the `0xF9C`/`0xF9D` word slots (there is
   no sign-extending word load).
8. Register-offset forms with Rm == 1111 or second-halfword bits 10-6 != 0.
9. Immediate-8 forms with (P,U,W) = (0,0,0) or (0,1,0) (not allocated).
10. Sign-extending store slots (`0xF98`, `0xF9A`) and everything outside the two regions.

Misc class:

11. B<c>.W with cond 111x: the T3 space allocates conditions 0000-1101 only.  cond 1110
    is not a second spelling of B.W (B.W is the 25-bit T4 form, whose second halfword has
    bit 12 set; llvm-objdump and gdb both decode the cond-1110 T3 word as undefined), and
    cond 1111 is the UDF/unallocated space.  Any other word in the branch region whose
    second halfword does not select one of the four forms is UNDEF too.  The earlier code
    decoded cond 1110 as a 25-bit B.W; that bug was found during the restart review and
    fixed (the test derives the two words from a generated `bne.w` by replacing the
    condition nibble, and checks that neither branches).
12. MRS with SYSm != 0 or Rd == 1111; MSR with the reserved bits set or Rn == 1111; hints
    with second-halfword bits 11-8 != 0; barriers with bits 15-8 != 0x8F or opc not in
    {2 CLREX, 4 DSB, 5 DMB, 6 ISB}.
13. Every coprocessor word except `MRC p15, 0, Rt, c13, c0, 3`: MCR of the same register,
    any other cp15 opc1/CRn/CRm/opc2, `MRC p14`, MRRC/MCRR, and LDC/STC/CDP for cp 12-14.
14. 32-bit SVC does not exist (SVC is 16-bit only, V14); a 16-bit word handed to either
    class returns UNDEF.

## Deliberate behavioural choices (documented, not hidden)

- **LDRD/STRD register pairs.**  The ARMv7 wording calls an odd Rt or an encoded Rt2 != Rt+1
  UNPREDICTABLE, but real ARMv8-A hardware and every assembler accept them, and the phone's
  own libraries use them heavily (4309 of the 5223 `ldrd`/`strd` words in the three dumps,
  e.g. `strd r3, r1, [sp, #8]` at libvpud_vcodec.so 0x88f2 = `e9cd 3102`).  They execute as
  encoded (`R[Rt]` at the address, `R[Rt2]` four bytes higher).  Only Rt/Rt2 == 1111 is
  UNDEF.
- **TBB/TBH with Rn == 1111** use PC exactly (`c->r[15]` = address + 4), without
  Align(PC,4): review finding F13 / T01's k_switch_dense.  LDR-literal keeps Align(PC,4).
- **LDM/STM writeback** follows the ARM ARM pseudocode: the transfers happen first, then
  `R[n] += 4*count` (with PC in the list the count includes PC).  A base register that is
  itself in the list therefore takes the loaded value plus the writeback; that case is
  architecturally UNPREDICTABLE and does not occur in the vendor libraries.
- **Rn == Rt with writeback** is executed as access-then-writeback (one of the
  UNPREDICTABLE outcomes).
- **Preloads** accept every addressing form (including the immediates-8 offset variant) and
  never access memory, so a hint can never stop the guest.
- **Barriers, WFI/WFE/SEV/YIELD/DBG and the unallocated hints** are no-ops; there is no event
  or debug hardware in the emulator.  `CLREX` drops the local monitor.
- **Not modelled:** address alignment checks (the guest memory layer is byte-addressed and
  the real SCTLR.A value is unknown) and the "translate" bit of the unprivileged forms
  (there is no MMU).  The exclusive monitor is value-based (`n2a32_excl_*`, V02).

## Coverage of the vendor libraries (acceptance check 2)

`gpu/hwcodec/facts/census.py` reports 58,373 reachable V16-class instructions in
`bin/vpud` + `libvpd_vcodec.so` + `libvcodecdrv.so` + `libvcodec_utility.so` +
`libvcodec_oal.so` (FACTS.md 4.8: `blx` 23082, `ldr.w` 12421, `str.w` 4429, `ldrb.w` 3979,
`strd` 3084, `beq.w` 2259, `strb.w` 1384, `ldrd` 1174, `pop.w` 1095, `bne.w` 929, `push.w`
884, `b.w` 730, `bl` 406, `stm.w` 406, ... `tbh` 65, `tbb` 62, `ldm.w` 39, `dsb` 36,
`ldrsh.w` 21, `ldrsb.w` 8, `ldrh` 4, `blxne` 2, `mrc` 2, `pldw` 1, `pushlt.w` 1).

Independent check run on 2026-11-11 (host only, not part of `run_tests.sh`): every
32-bit instruction of the linear llvm-objdump of the three largest libraries whose mnemonic
belongs to V16 was fed to both class functions.  Results:

- every `ldr.w/str.w/ldrb.w/strb.w/ldrh.w/strh.w/ldrsb.w/ldrsh.w/ldrd/strd/ldm.w/stm.w/
  ldmdb/stmdb/push.w/pop.w/ldrex/strex/ldrexb/strexb/ldrexh/strexh/tbb/tbh/pld/pldw/pli/
  b.w/b<c>.w/bl/blx/mrs/msr/nop.w/dmb/dsb/isb/clrex` word decodes with N2A32_OK;
- the only UNDEF words are (a) data that decodes with `pc`/`sp` in impossible fields
  (`ldrd pc, pc, ...`, `ldmdb ..., {..., sp, pc}`, `strh.w pc, ...`), (b) the nine
  `mrc`/four `mcr`/one `mrrc` cp15 words, which are all `p15, opc1 1..6, ..., 7` forms with
  `0xFFxx` second halfwords (no `MRC p15, c13, c0, 3` exists in the dumps; they look like
  data), and (c) `strex pc, pc, [lr, #0x3f8]` (UNPREDICTABLE data).
- no reachable-code mnemonic of this class returns UNDEF.  The two `mrc` instructions the
  census counts are in the `p15, opc1 1..6` group above and are documented UNDEF per the
  task contract ("other coprocessor forms UNDEF").

## External evidence (manual, not in run_tests.sh)

End-to-end through the real core (`tests/emu/asmrun.sh --thumb`, snippets in /tmp, all
assembled by llvm-mc):

- loads/stores/LDRD/STRD/LDM/STM/PUSH/POP snippet: 22 instructions, `stop=ok RETURN`,
  r0=0x1234, r4=0x5a, r5=0x1234, r8/r9=0x12345678/0xdef09abc, memory as expected
  (`strd` pairs at 0x33000/0x33008 correct, `stm.w r1!` writeback r1=0x33018).
- B.W / BL / B<c>.W / TBB / PUSH.W / POP.W snippet: `stop=ok RETURN`, r0 = 0x0f
  (7 from the BL'd helper + 8 from TBB case 3).
- LDR literal / TBH / LDREX+STREX / PUSH.W / POP.W snippet: `stop=ok RETURN`, r0 = 0x1544
  (literals 0x1230+0x300, table entry 2, strex status 0 and the stored value 0x12).

T01 differential harness (`gpu/hwcodec/tests/emu/run_diff.sh`): my kernels are in
`emu/test_v16/k_v16_mem.c` (nine kernels: `k_v16_ldst_sizes`, `k_v16_ldst_imm`,
`k_v16_ldrd`, `k_v16_ldm`, `k_v16_switch_dense`, `k_v16_switch_wide`, `k_v16_branch`,
`k_v16_stack`, `k_v16_atomic`).  Posted to hw-t01 for installation as
`tests/emu/kernels/k14_v16_mem.c`; it is not in T01's kernels/ directory yet (that
directory belongs to hw-t01), so the check below ran in a /tmp copy of the tree.

Result on the restart tree (2026-11-11, host only): with hw-r1's V14 patch applied in the
/tmp copy (`review/r1/v14-fix.patch`; the tree copy is unchanged), all **180/180** V16
kernel calls pass in **thumb-O0, thumb-O2, thumb-O3v, arm-O0, arm-O2 and arm-O3v**.  The
same run's `thumb-O0` row is `2780/2780` for the whole corpus.  In `armv8-O3v`, 20 calls
fail only in `k_v16_atomic`, which for that configuration is A32 `LDREXD/STREXD` (V12's
class, not V16; the exact stop `e1950e9f` was reported to hw-v12 on the board).  Without
the V14 patch the thumb rows still fail for many kernels of several packages (e.g.
`k_v16_switch_wide` stops at a 16-bit `0xb320` CBZ, V14's class), so V16's differential
result was previously masked; it is not a V16 failure.

All V16-class instructions in the built `libkernels.so` decode OK with the class functions
(checked instruction by instruction).

## Independent review

hw-r1 reviewed the package and recorded **accept** in `review/r1/FINDINGS.md` (row V16:
"N2TEST V16 PASS 3284938/3284938; my 23 independent T32 memory/system forms all pass,
including the F9 imm12/literal cases and the F13 TBB/TBH literal base (both fixed).  Code
read: branch immediates, LDM/STM, LDRD/STRD, exclusives, TBB/TBH, MRS/MSR, hints/barriers,
cp15 match the ARM ARM").  The two findings against this package were F9 (imm12/literal U
bit, fixed before the suite existed) and F13 (TBB/TBH PC base, fixed with a regression test
that fails on the old code).  The count below is from the latest tree (test-only additions
after that review run).

## Known risks / open questions

- `MRC p15, opc1 1..6, ..., 7` words in the dumps return UNDEF.  If any of them is real
  code rather than data, a guest stop would happen there; the census walk should confirm.
- Unaligned normal loads/stores are executed without an alignment fault (memory layer is
  byte-addressed); LDRD/STRD/LDREX/STREX alignment is likewise not enforced.
- The `P=1,U=1,W=0` immediate-8 encoding is treated as the unprivileged family
  (LDRT/STRT/...), which is the architectural meaning; a hand-written plain load with that
  encoding would behave as unprivileged (no writeback) rather than as a normal offset load
  (which the assembler never emits).
- Not compiled for arm64 iOS (the class files are portable C11; nothing OS-specific) and not
  run on the phone.
