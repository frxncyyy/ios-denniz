# V12 STATUS — A32 load/store, load/store multiple, exclusives; branches, system, hints

Worker: hw-v12. Date: 11 November 2026. Host-tested only; **nothing was run on the phone**.
Owned files: `gpu/hwcodec/emu/n2a32_arm_mem.c`, `gpu/hwcodec/emu/n2a32_arm_misc.c`,
`gpu/hwcodec/emu/test_v12/**`. No other file was edited. No vendor file was copied; the vendor
libraries were only read for the census remark at the end.

## 1. What is implemented

Both functions obey the frozen contract in `include/n2a32.h`: the core has already checked the
condition and set `r[15]`/`next_pc`/`in_it`; the class returns `N2A32_OK`/`N2A32_UNDEF`, never
calls `abort()`/`printf`, branches only through `n2a32_branch()`/`n2a32_bx()` and touches guest
memory only through `n2a32_ld8/16/32/64()` and `n2a32_st8/16/32/64()`. No instruction in either
file writes the condition flags.

### `n2a32_arm_mem` — A32 memory instructions

| Mnemonic | Forms implemented |
|---|---|
| LDR, STR, LDRB, STRB | immediate `imm12` (`±`), literal (`Rn=15`), register with `LSL/LSR/ASR/ROR/RRX` (`imm5:type:0:Rm`), pre-index (`!`), post-index, writeback, unaligned accesses |
| LDRT, STRT, LDRBT, STRBT | P=0,W=1 unprivileged immediate forms: offset applied to the address, **no** writeback (ARM ARM A8.8.95/A8.8.148) |
| LDR with Rt=15 | value loaded, then `n2a32_bx()` (interworking), writeback applied first |
| LDRH, STRH | immediate `imm8=imm4H:imm4L` (`±`), register `Rm`, pre/post-index, writeback |
| LDRSB, LDRSH | immediate and register, sign-extended to 32 bits |
| LDRD, STRD | immediate and register, Rt even and `<= 12`; low word at `addr`, high word at `addr+4` |
| LDM, STM | IA, IB, DA, DB (P/U), writeback, `POP`/`PUSH` aliases, empty register list = no-op; a `pc` in an LDM list branches through `n2a32_bx()`; base-in-list: LDM suppresses writeback, STM stores the old base and writes back |
| LDREX, LDREXB, LDREXH, LDREXD | load + `n2a32_excl_mark(c, addr, size, value)`; 64-bit for LDREXD (Rt even, `<= 12`) |
| STREX, STREXB, STREXH, STREXD | `n2a32_excl_store()` result (0 stored / 1 failed) into Rd; STREXD takes Rt/Rt+1, Rt even |
| CLREX | opens the local monitor (`c->excl_valid = 0`); accepted by **both** entry points because the core routes the word to `arm_misc` |
| PLD, PLI, PLDW | immediate and register forms, address generated but **not accessed**, no state change |

### `n2a32_arm_misc` — branches, system, hints, cp15

| Mnemonic | Behaviour |
|---|---|
| B, BL | `n2a32_branch(r[15] + SignExtend(imm24:'00'))`; BL sets `r[14] = next_pc` |
| BLX (immediate) | cond=1111 only, `r[14] = next_pc`, `c->thumb = 1`, branch to `r[15] + SignExtend(imm24:H:'0')` |
| BX, BLX (register) | `n2a32_bx(Rm)`; BLX sets `r[14] = next_pc` first |
| SVC | `c->stop = N2A32_STOP_SVC`, returns `N2A32_UNDEF` (the core keeps a non-NONE stop) |
| BKPT | `c->stop = N2A32_STOP_BKPT`, returns `N2A32_UNDEF` |
| UDF | `c->stop = N2A32_STOP_UNDEF`, returns `N2A32_UNDEF` |
| NOP, YIELD, WFE, WFI, SEV, other HINT #imm4 | no effect |
| DBG #imm4 | no effect (debug not enabled) |
| DMB, DSB, ISB (any option) | no effect (single-threaded guest execution model) |
| CLREX | `c->excl_valid = 0` |
| MRC p15,0,Rt,c13,c0,3 (cond != 1111, Rt != 15) | `Rt = c->tpidruro` |

## 2. Deliberately `N2A32_UNDEF`

Every word in the spaces above that is not listed in §1, in particular:

* `LDRB`/`STRB`/`LDRH`/`STRH`/`LDRSB`/`LDRSH`/`LDRD`/`STRD` with `Rt=15` (ARMv7 UNPREDICTABLE);
  `LDRD`/`STRD` with odd `Rt` or `Rt=14` (Rt+1 would be 15).
* `LDM`/`STM` with the S bit (`^` user-register form and exception return).
* `LDREX*`/`STREX*` with any register 15 (`Rt`, `Rd`, or `Rt`/`Rt+1` for the D forms).
* `LDM`/`STM` with cond=1111; any cond=1111 load/store that is not PLD/PLI/PLDW
  (including `Rt != 15`, `W=1`, `L=0`, and (P,B)=(0,0)); extra-load/store register form with
  bits 11-8 != 0000; extra-load/store shape S=0,H=0 that is not an exclusive.
* `BXJ` (0x012FFF2x); `MCR` of the same c13/c0/3 word; every other `MRC`/`MCR`
  (any coprocessor, incl. cp15 registers other than TPIDRURO, and `Rt=15`); `MCRR`/`MRRC`;
  `LDC`/`STC`/`CDP` if they reach this function.
* `CPS`, `SETEND` (the core routes F0/F1 here), `HVC`, `SMC`, `ERET`, `SRS`, `RFE`.
* Hints/DBG with cond=1111 (that space is Advanced SIMD and is routed to V18-V20, not here).

Architecturally UNPREDICTABLE cases that are implemented deterministically instead of UNDEF:
LDR/STR writeback with `Rn == Rt` (load wins, then base is written); LDM/STM with `Rn == 15`
(base = biased PC); extra-load/store register form with `Rm == 15` (reads biased PC); empty
LDM/STM list (no-op). Unaligned accesses are performed (the frozen interface has no way to
signal an alignment fault; the core only turns SIGSEGV into `N2A32_STOP_FAULT`).

## 3. Tests — commands and last lines

```
$ bash gpu/hwcodec/emu/test_v12/run_tests.sh
N2TEST V12 PASS 200797/200797      # clang -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
N2TEST V12 PASS 200797/200797      # clang -O2, no sanitizers
```

Run time on this machine: 4.8 s. 200797 checks = 487 unit assertions + 2 x 155 generated
encodings (each fed to both entry points) + 200000 random sweep words.

What the test does (`test_v12/t_v12.c`, 43 test functions):

* Unit tests call `n2a32_arm_mem`/`n2a32_arm_misc` directly on a `struct n2a32_cpu` whose
  `mem->base` is a 16 MiB `calloc` (guest addresses below 16 MiB), exactly as the task
  requires, with a test-local `n2a32_mmio_load/store` (records MMIO accesses) and a test-local
  value-based `n2a32_excl_mark/store` stand-in for V02's file.
* Every instruction word comes from `test_v12/gen_enc.py` assembly text through
  `llvm-mc -triple=armv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8
  --show-encoding` (PLDW uses the same flags plus `+mp`, which llvm requires for it). No
  hand-typed encoding appears anywhere; `enc.h` is generated at test time.
* Expected register/memory/flag values are literals derived from the ARM ARM pseudocode:
  positive and negative offsets, all shift types incl. LSR #32/ASR #32/RRX with C=0 and C=1
  (and a 32-bit address wrap), unaligned loads/stores, byte/imm12 edge values, `pc` and `sp`
  operands, LDM/STM IA/IB/DA/DB address arithmetic, base-in-list, PC-in-list, POP/PUSH,
  exclusive success/failure/memory-changed/no-monitor, CLREX, MMIO load/store, and all
  branch target arithmetic (forward/backward, H bit, Thumb switch, LR value).
* Every encoding llvm-mc produced for the package (155) is fed to both entry points and must
  return OK or UNDEF.
* A 200000-word deterministic sweep in V12's encoding space (families plus arbitrary words)
  runs under ASan/UBSan and -O2; it uses a separate 4 GiB `MAP_NORESERVE` reservation so every
  32-bit address a random encoding can form is backed, and asserts only "OK or UNDEF, never a
  crash". All general registers are zero, so the touched pages stay near 0 and near 4 GiB.
* Spot checks that the class functions do not re-test the condition (a `BEQ` with Z=0 and an
  `MRCEQ` with Z=0 still execute), and that flags are never modified.
* `test_v12/gen_enc.py` is also the list of every mnemonic/form the tests cover.
* Differential kernels for T01's harness: `test_v12/k_v12_mem.c` (k12_ldst, k12_ldrd,
  k12_signed, k12_ldm_stm, k12_branch, k12_stack) is self-contained and compiles clean with
  T01's exact ARM flags; it produces LDR/STR/LDRB/STRB/LDRH/STRH/LDRSB/LDRSH/LDRD/STRD,
  LDM/STM (struct copies, PUSH/POP), B/BL/BL through a register and stack traffic across the
  -O0/-O1/-O2 configurations. Posted to hw-t01 for installation in tests/emu/kernels/.
* Core-level smoke test with T01's `asmrun` against the real `n2a32_run` (not part of
  run_tests.sh): a snippet using LDR/STR/LDRB/STRB/LDRH/STRH/LDRSB/LDRSH immediate forms,
  LDMIA/STMDB, PLD, LDREX/STREX, CLREX, DMB/DSB/ISB, LDRT, LDRD immediate and
  `MRC p15,0,r4,c13,c0,3` stops with `stop=ok reason=1 (RETURN)` and the expected
  register/memory values. Five valid instructions currently stop with UNDEF because of
  `a32_route` (see §4 item 1): 0xE6901002 `ldr r1,[r0],r2`, 0xE15010B2 `ldrh r1,[r0,#-2]`,
  0xE0C020D8 `ldrd r2,r3,[r0],#8`, 0xE16020F8 `strd r2,r3,[r0,#-8]!`, 0xE320F002 `wfe`.

Cross-compile check (not executed): both implementation files compile cleanly for
`--target=arm64-apple-ios14.0 -O2 -std=c11 -ffreestanding -Wall -Wextra -Werror` (Mach-O arm64
objects produced). Host execution evidence is x86-64 Linux only.

## 4. Not done / open items

1. **Routing (V02)**: `emu/n2a32_core.c` (11:35 version) misroutes several groups of my space.
   The worst is group 0: it treats bit 24 as the extra-load/store marker, but in that group
   bit 24 is data-processing opcode bit 3, so every MOV/MVN/ORR/BIC/TST/TEQ/CMP/CMN goes to
   `arm_mem` and returns UNDEF - this is why every ARM function stops at its first
   `mov r11, sp` (0xE1A0B00D) in T01's corpus. Also misrouted: P=0 LDR/STR register to
   `arm_media`; extra loads/stores with P=0 or U=0 to `arm_dp`; YIELD/WFE/WFI/SEV/DBG to
   `arm_dp`; U=0 PLD/PLI and all PLDW to `arm_misc`/`neon_ldst`. I posted the exact tested
   unified diff to hw-v02 (case 0/1/3 and F4-F7 masks), then a correction: the naive
   `(insn & 0x0E000090) == 0x90` extra-load/store test also catches MUL/MLA/SWP (their
   bits 7-4 are 1001, the unallocated (S,H)=00 shape); the exact test adds
   `(((insn & 0x60) != 0) || (((insn >> 20) & 0xF) >= 8))` (T01 found this, I verified with
   llvm-mc). With the corrected diff applied to a private copy of the core, T01's `asmrun`
   runs all the previously failing snippets to `N2A32_STOP_RETURN` (prologue/mov,
   `ldr r1,[r0],r2`, `ldrh r1,[r0,#-2]`, `ldrd ...post`, `strd ...pre-neg`, `pldw`,
   `wfe/sev/yield/dbg`) and MUL/MLA still execute (mul 6*7 = 42, mla 6*7+42 = 84). Not yet in
   the shared tree, so not verified through `n2a32_run` there.
2. **`n2a32_excl.c` (V02) landed at 11:35.** My CLREX writes `c->excl_valid = 0`, which is
   compatible with it (it keeps the read value/width in its own process-wide table and fails
   any STREX while `excl_valid == 0`; note it stores the *width* in `excl_valid`, not 1, while
   my unit-test stand-in stores 1 - the class functions never inspect the field). My unit tests
   still use the local value-based CAS stand-in so they do not depend on V02's file.
3. **Census: acceptance criterion satisfied [V].** I ran hw-d01's `facts/census.py` (read-only)
   over all five vendor objects: every decoded instruction is Thumb (`insns=Thumb=...` for
   vpud 1696, libvpud_vcodec 12185, libvcodecdrv 260331, libvcodec_utility 32997,
   libvcodec_oal 2687), and the per-class reports contain **no V10/V11/V12 (A32) instruction
   at all** - only V14-V20. This matches the ELF symbol evidence (all defined FUNC symbols
   have odd `st_value`). So no mnemonic of this package is used by the vendor libraries and
   the UNDEF list cannot intersect them. Coverage 68-92% of code bytes (the walk leaves some
   bytes undecoded), but the mode evidence is structural, not count-based. If ARM code in a
   future library uses `MRC p15,0,Rt,c13,c0,2` (TPIDRURW) or another user-readable cp15
   register, it is UNDEF today and I will implement whatever word the census reports.
4. **T01 differential harness**: kernels written and posted (`test_v12/k_v12_mem.c`, T01 to
   install in tests/emu/kernels/); the harness itself has not run V12 yet because the core
   routing above is still wrong and V16/V17 were stubs when this was written. The core-level
   smoke test in §3 covers the correctly routed forms end to end.
5. No architectural-model-of-the-EMM: the "exclusive monitor" is V02's value-based CAS, not a
   real monitor; LDREXD alignment and exclusives to Device memory are not modelled.

## 5. Risks / interface notes

* LDRT/STRT offset semantics (offset applied, no writeback) are from ARM ARM A8.8.95/A8.8.148;
  llvm-mc's syntax is `ldrt Rt, [Rn], #imm` which looks post-indexed but is not.
* P=0,W=1 in the LDR/STR **register** form is treated as post-index-with-writeback (there is no
  unprivileged register form). llvm-mc never emits it; the sweep covers it for safety.
* `STREX` byte/halfword/doubleword use `n2a32_excl_store` with 1/2/8 bytes; the sizes are
  passed as documented in `include/n2a32.h`.
* `DBG` is treated as a no-op (task lists only five hints; DBG is the sixth hint-space form and
  is what a compiler emits for `__builtin_debugtrap`).
* PLD/PLI/PLDW are accepted only with `Rt=15`, `L=1`, `W=0` and the ARM-ARM (P,B) combinations;
  I verified against llvm-mc that no NEON structure load/store encoding collides with the masks.
* The test's 4 GiB sweep mapping is a test-only device; the implementation itself is portable
  C11 and uses only the frozen memory helpers.

## 6. Files

```
gpu/hwcodec/emu/n2a32_arm_mem.c            (new)
gpu/hwcodec/emu/n2a32_arm_misc.c           (new)
gpu/hwcodec/emu/test_v12/run_tests.sh      (new)
gpu/hwcodec/emu/test_v12/t_v12.c           (new)
gpu/hwcodec/emu/test_v12/gen_enc.py        (new)
gpu/hwcodec/emu/test_v12/k_v12_mem.c       (new, differential kernels for T01)
gpu/hwcodec/emu/test_v12/check_routing.sh  (new, optional core-routing check)
gpu/hwcodec/emu/test_v12/STATUS.md         (this file, copy)
gpu/hwcodec/dsh-state/workers/V12/STATUS.md (this file)
```
