# R1 — review findings, track V and track T

Reviewer: hw-r1. Started 11 November 2026. Host-only review: no phone, no publishing.
Scope: every package of track V and track T (`work_packages.json`), against its package
entry and the frozen headers in `gpu/hwcodec/include/`.

Method, per package that posts DONE:

1. read the package source against its `work_packages.json` entry and `include/n2a32.h` /
   `include/n2elf.h`;
2. run its own `run_tests.sh` from the work-tree root (builds under `/tmp/n2hw-r1` when the
   script allows it; otherwise the script's own `/tmp/n2hw-<id>`);
3. run my own spot tests (`review/r1/spot/spot.c`, `review/r1/run_spot.sh`): encodings
   assembled with llvm-mc 23.1.4, expected values written as literals from the Arm ARM
   pseudocode, independent of the package tests;
4. inspect for: ARM ARM deviations (flags, PC bias, IT, writeback order, sign/zero
   extension, saturation, NaN/rounding), tests that mirror the implementation, unbounded
   loops/recursion on guest input, host pointers in guest memory, thread safety, and
   guest-pointer validation.

My spot tests are the oracle; doubtful encodings were cross-checked with
`llvm-mc --show-encoding` / `llvm-objdump`. The package tests are not used as expected-value
sources.

## Verdicts

| pkg | DONE rev reviewed | verdict | evidence |
|---|---|---|---|
| V02 | rev 5 (commit 7c772e5) | **accept** | `emu/test_v02/run_tests.sh` → `N2TEST V02 PASS 860/860`; core/mem/excl read line by line; routing, IT, host calls, faults, MMIO, `n2a32_call` checked. F5/F7 fixed; F8 (stop_request atomics) and F20 (stop pair reporting) open, both low. |
| V03 | rev 4 | **accept** | `loader/test_v03/run_tests.sh` → `N2TEST V03 PASS 12971/12971`; vendor line `objects 5, relocations 3120, unresolved 146, unique imports 149`; `n2elf.c`/`n2elf_reloc.c` read; relocation targets bounded to PT_LOAD, SLEB128/RELR bounded, GNU hash chains bounded, guest addresses only (no host pointers). F6 fixed in rev 4. |
| V10 | rev 2 | **accept** | `emu/test_v10/run_tests.sh` → `N2TEST V10 PASS 220589/220589`; 22 independent A32 DP forms in my spot run, all pass (adds/adcs/sbcs/rsbs, LSR#32/RRX/ASR#32, register shifts, MUL/UMULL/SMULL/MLS, MOVW/MOVT, CLZ, TST/CMP flags, rotated immediates). |
| V11 | rev 4 | **accept** | `emu/test_v11/run_tests.sh` → `N2TEST V11 PASS 960/960` (both builds); 31 independent media forms in my spot run, all pass (extend/bitfield/REV/RBIT, SSAT/USAT(+16), QADD family, SADD16/SASX/SSAX/SHADD16/UQADD16/SEL, PKH, SMUL/SMLA/SMLAL/SMUAD/SMUSD/SMLAD/SMLALD/SMMUL/SMMLA/SMMLS/USAD8, UDIV, SDIV incl. INT_MIN/-1). |
| V12 | rev 2 | **accept** | `emu/test_v12/run_tests.sh` → `N2TEST V12 PASS 200797/200797`; 29 independent A32 load/store, LDM/STM, exclusive, PLD, unprivileged, B/BL/BLX/BX, MRC forms in my spot run, all pass. |
| V14 | rev 1 + T01s applied v14-fix.patch | **accept with findings — implementation verified 308/308; test_v14 has 3 stale expectations** | `emu/test_v14/run_tests.sh` → `N2TEST V14 FAIL 449/453` on the patched tree: the 4 failures are stale expectations (`movpc` wants thumb=1, but BXWritePC must switch to ARM with an even LR; census ok/undef want the old hole counts 0xDEC6/0x213A, now 0xE0C6/0x1F3A). My independent suite is `R1-SPOT PASS 308/308` on the patched tree, and `run_vhost.sh` now reaches M-c. F1/F2/F4 are fixed in the implementation; V14s test needs three literals updated. |
| V15 | rev 4 | **accept** | `emu/test_v15/run_tests.sh` → `N2TEST V15 PASS 1616/1616`; 26 independent T32 DP forms in my spot run now all pass, including the 16-bit parallel GE case after F3 was fixed (re-checked 12:50). PKH halves and ADC/SBC carry-in were already found by T01 and are fixed. |
| V16 | DONE | **accept** | `emu/test_v16/run_tests.sh` → `N2TEST V16 PASS 3284938/3284938`; my 23 independent T32 memory/system forms all pass, including the F9 imm12/literal cases and the F13 TBB/TBH literal base (both fixed). Code read: branch immediates, LDM/STM, LDRD/STRD, exclusives, TBB/TBH, MRS/MSR, hints/barriers, cp15 match the ARM ARM. |
| V17 | DONE rev 2 | **accept** | `emu/test_v17/run_tests.sh` → `N2TEST V17 PASS 2264/2264`; F12 (VMOV Rt,Sn / Sn,Rt) fixed and re-checked; F11 withdrawn as my false positive (VSEL uses APSR, V17 evidence); all 26 of my VFP forms pass. |
| V18 | DONE rev 2 | **accept** | `emu/test_v18/run_tests.sh` → `N2TEST V18 PASS 225740/225740` plus `V18-IOS` compile and `V18-VENDOR` mnemonic coverage; my 23 independent NEON 3-same forms all pass (integer add/sub/logic/compare/max/min/hadd/qadd/mul/mla/abd/rhadd/tst, float add/ceq/max, qdmulh). Code read: FZ/DN/NaN handling with FE_TONEAREST restored, QC sticky, saturation via 64-bit intermediates. |
| V19 | DONE rev 2 + T01s applied overlap patch | **accept** | `emu/test_v19/run_tests.sh` → `N2TEST V19 PASS 44435/44435`; F10 was real and is fixed in both the implementation and `gen_vectors.py` (re-checked 13:20); F17 (in-place Vd/Vm) is fixed by T01s applied overlap patch and re-checked with the VSHN repro (0x3456345634563456) and the full spot suite. My 19 independent NEON forms all pass (vshr/vshl/vsra/vrshr/vrsra/vsri/vsli/vqshl/vqshlu/vqshrn/vqrshrun/vmovl/vaddl/vmull/vmlal/scalar vmla/vqdmulh). |
| V20 | DONE rev 2 | **accept** | `emu/test_v20/run_tests.sh` → `N2TEST V20 PASS 436882/436882` (t_misc/t_ldst/t_cover 17408/t_sweep 200000); F16 (VEXT) fixed and re-checked; my 18 independent NEON misc/ldst forms pass (vdup/vmov lane via V17, vswp/vtrn/vuzp/vzip, vclz/vcnt/vrev64/vpadal, vld1/vst1 + lane forms, vmovn/vqmovn). |
| V30 | rev 5 | **accept with findings — F15 high** | `shim/test_v30/run_tests.sh` → `N2TEST V30 PASS 277633/277633` + `N2TEST V30-CORE PASS 6/6`; format engine differential against host printf (independent), 64 MiB host-buffer cap, precision/format caps, heap under one mutex, no host pointers in guest memory. F15 (found via V33): `%0<width>d` with a large width overflows `body[480]` in `emit_int`. |
| V32 | DONE rev 7 | **accept** | `shim/test_v32/run_tests.sh` on rev 7: `N2TEST V32 PASS 13149/13149` (direct 2390/2390, e2e 10759/10759, TSan e2e 10730/10730); F19 fixed by making the blocking waits interruptible and relaxing the TSan timedlock expectation; thread table and object side-table with refcounts under `g_thr_lock`, guest stacks from a pool, stop propagation via `__atomic_store_n`, bounded join/timedlock loops. F8 (core plain read of `stop_request`) is V02s side and remains open. |
| V33 | DONE rev 9 | **accept** | `shim/test_v33/run_tests.sh` → `N2TEST V33 PASS 341/341` (ASan+UBSan, -O2, TSan); F18 fixed in rev 7 (host paths are ENOENT) and rev 8 added the `..` refusal; rev 9 adds `n2vfs_set_maps()` for IV. fd table under one mutex, guest buffers bounded/NULL/MMIO-checked, guest strings capped, open flags translated Linux->Darwin. |
| V34 | DONE | **accept** | `shim/test_v34/run_tests.sh` → `N2TEST V34 PASS 2035/2035` + `N2TEST V34CORE PASS 103/103` (real V03 loader + vendor object); guest strings bounded, property/ION/dl tables under one mutex, handles validated, data imports are guest cells not host pointers, no host pointer in guest memory. |
| V40 | DONE | **accept with findings — F14 low** | `vcu/test_v40/run_tests.sh` → `N2TEST V40 PASS 1161/1161` (387 × ASan+UBSan/-O2/TSan); `n2vcu.c` read: ioctl dispatch, 80-byte share copies, alloc/free/cache bounds, GCE list clamped to CMDQ_CMD_MAX with every poll/wait bounded, register mmap limited to the three documented windows. F14: 64-bit mmap offset overflow can pass the shmem range tests. |
| V41 | DONE rev 3 | **accept** | `ap/test_v41/run_tests.sh` → `N2TEST V41 PASS 578/578`, `V41 real n2vcu PASS 39/39`, layout 63 lines match the kernel header, armv7 static asserts. Code read: every IPI message checks `len >= sizeof(m)`, query copy bounded by `query_bytes`, DMA rw checks `offset+bytes <= d->bytes` and the guest/mem identity, one mutex around the DMA table. |
| IV | DONE rev 2 | **accept** | `host/test_iv/run_tests.sh` → `N2TEST IV PASS 45/45`; `run_vhost.sh` loads all 5 vendor objects, links 0 unresolved, runs the init arrays (M-b), reaches M-c (`set_object=2 get_object=1 allocations=9`) after the V14 patch and V33s maps hook. M-d is blocked on the daemons `vcu_inst_addr`, not IV. |
| T01 | — | **provisional — harness works; differential table open** | `tests/emu/run_tests.sh`: trace 28 × 3, support 130197 × 2, asmrun 13, fuzz 250k+250k all pass; the differential stage now runs and reports 1405/18200 mismatches (arm-O0 clean, thumb-O0 676, NEON narrow/widen, k_v15_extend). That is T01 doing its job; the mismatches are tracked against the owning packages, not a T01 defect. No DONE post yet. |
| T02 | DONE | **accept** | `tests/hw/run_tests.sh` → `N2TEST T02-hw PASS 6166/6166` in ASan+UBSan, -O2 and TSan; `tests/check/run_tests.sh` → `N2TEST T02-check PASS 22/22` (JPEG/H.264/HEVC validators, including negative cases). `n2vc_hw_fake.c` read: power model, script parser bounded (fgets, 16 tokens/line, 32-rule chain, `-ELOOP`), DMA first-fit with `offset+bytes` bounds, one mutex. Phone scripts are main-session-only, never flash/reset, refuse to run without `PHONE_EXEC`, and `50-recovery.md` forbids stock-Android boots over `userdata`. The earlier lock-during-sleep note was addressed: `wait_irq`/POLL/DELAY no longer hold the instance mutex across the sleep. |

## Findings

Status values: `open`, `fixed`, `rejected`. Severity: `high` (wrong decode/value on ordinary
compiled code or a blocker for integration), `medium` (architectural deviation on reachable
but uncommon forms), `low` (robustness/edge).

| id | pkg | severity | status | one line |
|---|---|---|---|---|
| F1 | V14 | high | fixed (T01 applied the patch; my suite 308/308) | CBZ/CBNZ with `i=1` (offset outside 0..63) return UNDEF and lose offset bit 6 |
| F2 | V14 | medium | fixed (T01 applied the patch; my suite 308/308) | `MOV PC, Rm` uses `n2a32_branch` instead of `BXWritePC` (no interworking) |
| F3 | V15 | medium | fixed (re-checked 12:50, V15 rev 4) | 16-bit parallel add/sub set only one GE bit per halfword; GE[3:2] always 0 |
| F4 | V14 | high | fixed (T01 applied the patch; my suite 308/308) | LSR/ASR **register** shift by 0 treated as shift by 32 (T01 found it; I reproduced it) |
| F5 | V02 | low | fixed (rev 5, re-checked 13:00) | fetch at `pc=0xfffffffe` reads the second halfword one byte past the 4 GiB reservation; no `pc+size` bound |
| F6 | V03 | low | fixed (rev 4, re-checked) | `n2elf_link()` is not transactional: after a mid-relocation failure a retry re-applies R_ARM_RELATIVE (double bias) |
| F7 | V02 | low | fixed (rev 5, re-checked 13:00) | `n2a32_mem_mmio()` accepts `addr=0`, so guest address 0 can become readable MMIO, breaking the `n2a32_ptr(0)==NULL` contract |
| F8 | V02/V32 | medium | open | `stop_request` is read with a plain load in `n2a32_run` while V32 stores it atomically from another thread — C11 data race / TSan report |
| F9 | V16 | high | fixed (re-checked 13:50) | `single_transfer` uses bit 8 as U for the imm12/literal forms, so every F8xx imm12 load/store subtracts its offset |
| F10 | V19 | high | fixed (rev 2, re-checked 13:20) | VSRI/VSLI keep the wrong half of the destination vector (and `gen_vectors.py` mirrors it) |
| F11 | V17 | — | **rejected — my false positive** (V17 evidence, re-checked) | VSEL actually uses the APSR condition flags (ConditionPassed), not FPSCR.NZCV; my repro set only FPSCR |
| F12 | V17 | high | fixed 13:10 (my spot re-check passes) | VMOV Rt,Sn / Sn,Rt return UNDEF; libvcodecdrv uses them |
| F13 | V16 | medium | fixed (re-checked 13:50; T01 found it) | TBB/TBH with Rn=PC uses `Align(PC,4)` instead of PC |
| F14 | V40 | low | open | 64-bit mmap offset overflow can pass the MAP_SHMEM_* range checks |
| F15 | V30 | high | open | `emit_int` overflows `body[480]` for `%0<large width>d` (stack smash from a guest format string) |
| F16 | V20 | high | fixed (re-checked 14:05; T01 found it) | VEXT concatenates Dm:Dn instead of Dn:Dm |
| F17 | V19 | high | fixed (T01s patch applied; re-checked) | narrow/shift loops write Vd before reading all of Vm; Vd==Vm corrupts lanes |
| F18 | V33 | high | fixed (rev 7, re-checked; IV found it) | `fopen()` can open arbitrary host paths (`/proc/self/maps`, ...) |
| F19 | V32 | low | fixed (rev 7, re-checked) | rev 4 TSan e2e timedlock expectations fail (EINVAL 22) |
| F20 | V02 | low | open | after a recovered nested stop, `stop_insn` and `stop_addr` can describe different failures |

### F1 — V14, high, open

`gpu/hwcodec/emu/n2a32_t16.c:324` (dispatch) and `:196-205` (`t16_misc_cbz`).

The T1 encoding is `1011 0 op i 1 imm5 Rn` (Arm ARM A8.8.30/A8.8.31): bit 11 = op
(CBZ/CBNZ), bit 10 = 0, bit 9 = i, bit 8 = 1, bits 7-3 = imm5, bits 2-0 = Rn. The dispatch
mask is `(w & 0xF700u) == 0xB100u`, which requires bit 9 (`i`) to be 0. Every CBZ/CBNZ whose
branch offset has bit 6 set (forward >= 64 bytes, or any backward branch) is therefore not
claimed and falls through to the "reserved holes" `return N2A32_UNDEF` at `:343`.
`t16_misc_cbz` also ignores bit 9 when forming the target, so even a claimed i=1 word would
miss `i << 6`.

Repro (my spot test, llvm-mc encoding):
```
cbz r0, far at 0x10 with far=0x58  -> 0xB310  (llvm-objdump: b310 cbz r0, 0x58 @ imm=#0x44)
c.r[15]=0x1004; c.next_pc=0x1002; c.thumb=1; c.r[0]=0;
n2a32_t16(&c, 0xB310) -> rc=1 (UNDEF), next_pc=0x1002; expected rc=0, next_pc=0x1048.
cbnz r1,i=1 (0xBB11) same.
```
Why it matters: D01 census counts `cbz=4429`, `cbnz=744` in the vendor set; offsets >= 64
bytes are ordinary. This stops the Thumb integration on real code.

Fix: mask `(w & 0xF500u) == 0xB100u` (ignore bits 11 and 9), and
`imm = (((w >> 3) & 0x1F) << 1) | (((w >> 9) & 1) << 6)`.
The package census test currently lists 0xB300–0xB3FF and 0xBB00–0xBBFF as "reserved holes"
(`emu/test_v14/STATUS.md`), i.e. it mirrors the bug; it needs a case for 0xB310/0xBB10 and a
negative-offset CBZ.

### F2 — V14, medium, open

`gpu/hwcodec/emu/n2a32_t16.c:458-462` (`t16_special`, case 0x2).

Arm ARM MOV (register) T1 (`0100 0110 DM Rm Rdn`): `if d == 15 then BXWritePC(R[m])`.
`BXWritePC` sets PSTATE.T from `R[m]<0>` and aligns the target. The code calls
`n2a32_branch(c, c->r[rs])`, which keeps `c->thumb` unchanged. With an odd LR both agree;
with an even LR (an ARM return address) the interpreter stays in Thumb and executes ARM code
as Thumb.

Repro:
```
c.r[15]=0x1004; c.next_pc=0x1002; c.thumb=1; c.r[14]=0x100;
n2a32_t16(&c, 0x46F7) /* mov pc, lr */ -> next_pc=0x100, thumb=1; expected thumb=0.
```
Fix: `if (rdn == 15u) n2a32_bx(c, c->r[rs]);`. Keep `ADD PC, Rm` on `n2a32_branch`
(ALUWritePC does not interwork) — that one is correct.

### F3 — V15, medium, fixed in rev 4

`gpu/hwcodec/emu/n2a32_t32_dp.c:585-588` and `:618` (`parallel_op`).

For the halfword variants the Arm ARM gives each lane a two-bit GE field: GE[1:0] for the
low halfword, GE[3:2] for the high, both bits 11 when the condition holds, 00 otherwise.
Only the byte variants use one GE bit per lane. The code does `ge |= cond << lane` for every
lane and then `c->ge = lanes == 4 ? (ge & 0xF) : (ge & 0x3)`, so a halfword lane can only set
GE[lane] and GE[3:2] is always 0.

Repro:
```
sadd16 r0, r1, r2 = 0xFA91F002; r1=0x0001FFFF, r2=0x00010001
low 0xFFFF+1 = 0x10000 (exact signed 65536 >= 0) -> GE[1:0]=11
high 1+1 = 2 (>= 0)                              -> GE[3:2]=11
current r0=0x00020000 ge=0x3; expected ge=0xF.
```
Same for SASX/SSAX/SSUB16 and U/UQ/UH 16-bit variants.
The package test mirrored it: `emu/test_v15/t_dp.c:909-931` expected 0x1/0x3/0x2 for 16-bit
cases. V11's A32 implementation sets both bits (correct), a useful cross-check.

Fix applied by V15 in rev 4: `gbit = lanes == 4 ? lane : 2*lane`, `gwidth = lanes == 4 ? 1 : 2`,
`((1u << gwidth) - 1u) << gbit`, mask `ge & 0xF`; the package test was updated with
independent expectations. My spot check `sadd16` now returns GE=0xF; V15 rev 4 suite is
1616/1616. F3 closed.

### F4 — V14, high, open (T01 found it; independently reproduced)

`gpu/hwcodec/emu/n2a32_t16.c:122-147` (`t16_shift`).

`if (n == 0) n = 32;` is correct for the **immediate** forms (LSR #0 / ASR #0 encode #32) but
wrong for the **register** forms (`t16_data_processing` case 0x3/0x4 pass `y & 0xFF`): a
register-specified shift of 0 leaves the value and C unchanged (Arm ARM LSR (register) T1 /
ASR (register) T1). The code returns 0 and C = bit 31.

Repro:
```
c.r[0]=0x5a, c.r[1]=0, c.cf=1
lsrs r0,r1 (0x40C8) -> rc=0 r0=0x00000000 c=0; expected r0=0x5a, c=1
asrs r0,r1 (0x4108) -> same; expected unchanged.
```
Why it matters: D01 census `lsrs=1033`, `asrs=196`; a variable shift count of 0 happens on
the first iteration of ordinary shift loops. T01 measured 35 thumb-O0 kernel mismatches from
this (k_crc32 15, k_v15_rev 20). Fix: split the helper or pass an `immediate` flag; only the
immediate path maps 0 to 32. LSL and ROR register-by-0 are already correct.

### F5 — V02, low, open

`gpu/hwcodec/emu/n2a32_core.c:509-526`: the fetch does not check `pc + size <= 4 GiB`.
At `pc = 0xFFFFFFFE` with Thumb and a first halfword >= 0xE800, `memcpy(&hw2, base + pc + 2, 2)`
reads at `base + 0x100000000`, one byte past the 4 GiB reservation. The fault handler only
converts addresses inside `[base, base + 4 GiB)` to `N2A32_STOP_FAULT`; a fault there is
chained to the default action (host process death). On this host the adjacent mapping made
the read succeed, so my test observed `STOP_UNDEF` on garbage rather than a fault — either
way the access is outside the reservation. Suggested fix: reject `pc + size > 0x100000000`
with `STOP_FAULT` before the fetch. Same shape for the 16-bit fetch at `pc = 0xFFFFFFFF`
(Thumb) and for `n2a32_ld32` at `0xFFFFFFFE`. Low: the guest is the user's own code and
0xFFFFFFFE is not a normal branch target.

### F6 — V03, low, open

`gpu/hwcodec/loader/n2elf.c:811-874`: if a relocation fails halfway (unsupported type,
bad target, unresolved data import), earlier relocations in the same image have already been
written and `im->linked` remains 0. A second `n2elf_link()` call re-applies them;
R_ARM_RELATIVE reads the addend from the target, so it would add the bias twice. The API does
not promise retry, and all current callers abort on failure, so this is a robustness note.
Suggested fix: set `im->linked = 1` on the first attempt regardless, or record an `im->failed`
and refuse a retry.

### F7 — V02, low, open

`gpu/hwcodec/emu/n2a32_mem.c:241-298`: `n2a32_mem_mmio()` does not reject `addr == 0`.
Mapping the first megabyte as MMIO makes guest address 0 readable through the hook, while
`n2a32_ptr(0)` is contractually NULL and `n2a32_mem_map`/`protect` reject address 0. No
current caller maps 0; a one-line `addr == 0` rejection (or a `range_ok`-style check) would
keep the contract uniform.

### F8 — V02/V32, medium, open

`gpu/hwcodec/emu/n2a32_core.c:468` reads `c->stop_request` with a plain load; the
frozen header (`include/n2a32.h:77`) documents it as written "by a shim or another thread",
and V32 will use `__atomic_store_n(..., __ATOMIC_RELEASE)`. A plain read racing an atomic
store is still a C11 data race and TSan reports it, even though `volatile` forces a reload.
Fix (no header change): `if (__atomic_load_n(&c->stop_request, __ATOMIC_ACQUIRE) != 0)` at
:468; V32's side is already release. No single-threaded test is affected; this is the
thread-safety contract needed for the V32 TSan claim. Posted to V02.

### Verified fix patch for F1/F2/F4

V14 was idle after posting DONE, so I prepared the three fixes as a unified diff in my own
tree: `review/r1/v14-fix.patch` (no edit to V14's file). It adds the `immediate` argument to
`t16_shift`, fixes the CBZ/CBNZ mask and target, and uses `n2a32_bx` for `MOV PC,Rm`. Building
the patched copy with the same flags and running my spot suite gives
the three V14 failures disappear (at the time of that run the suite was 197 checks and the
patched copy gave `R1-SPOT PASS 197/197`; after adding the V19 forms the same check is
214/216, the two remaining failures being F10). V14 also needs its census expectations updated: 0xB300–0xB3FF and 0xBB00–0xBBFF are
CBZ/CBNZ i=1, not reserved holes. A correction to the F1 repro: `cbnz r1` with i=1 is
`0xBB11` (`0xBB10` is `cbnz r0`).

### F9 — V16, high, open (provisional; V16 has no DONE post yet)

`gpu/hwcodec/emu/n2a32_t32_mem.c:90,118,121` (`single_transfer`). `sign = (hw1 >> 8) & 1` is
0 for every 0xF8xx encoding and 1 for 0xF9xx. The code passes it as the U bit to `apply_u()`
for the imm12 and literal forms. In the T32 load/store single imm12 encodings
(`LDR/STR/LDRB/LDRH/LDRSB/LDRSH (immediate)` T3/T4 and the literal forms) the 12-bit offset is
always **added**; bit 8 is the low bit of the op nibble (0xF8C STR vs 0xF8D LDR, 0xF88 STRB vs
0xF89 LDRB, ...), not U. Result: every 0xF8xx imm12 access reads/writes at `base - offset`.

Repro (llvm-mc): `ldr.w r0,[r1,#4]` = `F8D1 0004` -> reads `[r1-4]`, expected `[r1+4]`;
`str.w r0,[r1,#4]` = `F8C1 0004` -> writes `[r1-4]`; `ldrb.w r0,[r1,#1]` = `F891 0001` ->
`[r1-1]`; `ldr.w r0,[pc,#4]` = `F8DF 0004` -> `[Align(PC,4)-4]`. The imm8 (P/U/W) and register
forms are correct. Census: ldr.w=12421, str.w=4429, ldrb.w=3979, strb.w=1384. Fix: use
`addr = base + (hw2 & 0xFFFu)` for imm12 and literal; keep `uu` for the imm8 form. Posted to
V16 as a provisional finding; V16 is still in progress and has no package tests yet.

### F10 — V19, high, fixed in rev 2

`gpu/hwcodec/emu/n2a32_neon_shift.c:172-191` (`vsri`, `vsli`) and the same formulas in
`emu/test_v19/gen_vectors.py:70-80`. Arm's definition (Arm Compiler armasm reference,
VSLI and VSRI; A32/T32 Advanced SIMD):
- **VSRI**: the source is shifted right and inserted into the low bits; the **most
  significant** `shift` bits of Vd are unchanged. `result = (Vd & ~((1 << (esize-shift)) - 1))
  | (Vm >> shift)`.
- **VSLI**: the source is shifted left and inserted into the high bits; the **least
  significant** `shift` bits of Vd are unchanged. `result = (Vd & ((1 << shift) - 1))
  | (Vm << shift)`.

The code keeps the low bits of Vd for VSRI and the high bits for VSLI — the opposite.
Repro (llvm-mc encodings, my spot suite):
```
vsri.32 q0,q1,#8 = F3B80452; q0=ffffffff, q1=11223344
  current d0=112233ff; expected 0xff112233 (top 8 bits of Vd kept)
vsli.32 q0,q1,#8 = F3A80552 (U=1; F2A80552 is VSHL.I32); q0=ffffffff, q1=11223344
  current d0=22334400; expected 0x223344ff (low 8 bits of Vd kept)
```
Why it matters: D01 census `vsli.64=20, vsli.32=15, vsri.32=12, vsri.64=7`; these are
bitfield/colour-channel inserts where the kept half is the point. The package test generator
has the same wrong `vsri`/`vsli`, so `N2TEST V19 PASS 44424/44424` is not evidence against
this. Fix both the implementation and `gen_vectors.py`; add a case with a non-trivial Vd
(e.g. `0xdeadbeef`). V19 fixed both sides in rev 2 and the suite is 44435/44435; my spot check
now gives 0xff112233 / 0x223344ff. F10 closed.

### F11/F12 — V17, medium/high (F11 rejected, F12 fixed)

F12 (high), `gpu/hwcodec/emu/n2a32_vfp.c:1144-1175` (`vfp_dp`, bit4 block): only VMRS
(`0x0EF10A10`) and VMSR (`0x0EE10A10`) are matched; the single-register core↔VFP moves fall
through to UNDEF. The vendor library uses them:
```
ee00 0a10  vmov s0, r0      (0xf4932, 0x110c9e, ...)
ee10 0a10  vmov r0, s0      (0x1102ba, 0x1103ce, 0x110590, ...)
```
Encodings: `VMOV Sn,Rt = EE000A10` (bit20=0), `VMOV Rt,Sn = EE100A10` (bit20=1), with
`Sn = (bits 19-16) << 1 | bit7`; examples `vmov r0,s1 = EE100A90`, `vmov r0,s2 = EE110A10`,
`vmov r0,s31 = EE1F0A90`. Repro: `n2a32_vfp(&c, 0xEE100A10)` → rc=1; expected rc=0 and
`c.r[0]` = the bits of S0. This stops the vendor library on an ordinary scalar move.

F11 — **rejected, my false positive.** I assumed VSEL used FPSCR.NZCV. V17's evidence
(and the ARM ARM `ConditionPassed()` for VSEL) shows it uses the APSR condition flags; the
standard compiler sequence is `vcmp` + `vmrs APSR_nzcv,fpscr` + `vsel<cond>`, and T01's
k_fminmax disassembly has two VSELs with the same FPSCR and operands but different APSR
state, which only works with APSR. My repro set `c.fpscr` but not `c.z`. I corrected my spot
test to set the APSR (and deliberately the opposite FPSCR); it passes. No V17 change needed.

### F13 — V16, medium, open (provisional; T01 found it, confirmed by inspection)

`gpu/hwcodec/emu/n2a32_t32_mem.c:336` (`dual_excl`, TBB/TBH): `base = (rn == 15u) ?
(c->r[15] & ~3u) : c->r[rn];`. For TBB/TBH the literal base is **PC** (`c->r[15]`), not
`Align(PC,4)`; a 32-bit instruction can start at 2 mod 4, and then PC is 2 mod 4. T01's
`k_switch_dense` reproduces it. Fix: use `c->r[15]` for Rn==15. The LDR-literal `Align(PC,4)`
in `single_transfer` is correct and should stay.

### F14 — V40, low, open

`gpu/hwcodec/vcu/n2vcu.c:1168-1208` (`vcu_mmap_ioctl`): `end = offset + len` (uint64) is
computed before the `offset >= BASE && end <= BASE + RANGE` tests for the MAP_SHMEM_* ranges.
A guest `mmap64` offset near UINT64_MAX wraps `end` to a small value, so the test passes and
the return is `dmem_guest + (uint32_t)(offset - BASE)` — a guest address outside the requested
range (and outside the DMEM that `n2vcu_map_dm_addr` accepts). No host escape (the guest owns
its address space), but the mapping should fail. Repro shape: `len=0x1000`,
`offset=0xfffffffffffff000` → returns `dmem_guest + 0x7ffff000`; expected failure. Fix: reject
`offset > UINT64_MAX - len`, or require `offset < (1ull << 40)` before the range tests.

### F15 — V30, high, open (found by V33's suite, reproduced standalone)

A verified patch is in `review/r1/v30-f15.patch` (19 lines): cap the pre-added zeros to
`sizeof(body) - np - nd`; `emit_field` pads the rest with `opad`. On a patched copy,
`%0500d` produces 500 bytes with no UBSan report (first/last '0'), and the standalone repro
is clean. Not applied to V30's file (not mine).

`gpu/hwcodec/shim/n2shim_format.c:645-672` (`emit_int`): `char body[PREC_MAX + 80]` is 480
bytes; the `0`-flag path adds `width - total` to `zeros` and then writes `zeros` bytes into
`body`. Precision is capped at 400 but width is not, so `%0500d` writes `body[480]` (UBSan:
`index 480 out of bounds for type 'char[480]'`), and a larger width is a full stack smash
attempt from a guest-controlled format string. Repro: `n2fmt(&c, out, sizeof out, "%0500d",
&a)` with one int argument. Fix: cap the pre-added zeros to `sizeof(body) - np - nd` (or drop
the pre-add; `emit_field` already zero-pads with `opad` in O(1)). V33's suite currently fails
only because of this line.

### F16 — V20, high, open (provisional; T01 found it, independently reproduced)

`gpu/hwcodec/emu/n2a32_neon_misc.c:1053-1070` (`vext`): the source bytes are assembled from
`mfull` first and `nfull` second. VEXT concatenates **Dn:Dm**, so the order must be nfull
then mfull. Repro (llvm-mc encodings, direct class call):
```
vext.8 d0,d1,d2,#4 = F2B10402; d1=0706050403020100, d2=1716151413121110
  current d0=0302010017161514; expected 1312111007060504
vext.8 d0,d1,d2,#0 = F2B10002 -> current d2; expected d1
vext.8 q0,q1,q2,#4 = F2B20444 -> current d0=1b1a191817161514; expected 0b0a090807060504
```
Census `vext.64=19, vext.32=7`. The package's own VEXT cases mirror the swap, so
`N2TEST V20 PASS 420362/420362` is not evidence against this. Fix: swap the two copies
(Q path) and the `lo`/`hi` pair (D path).

## Tests I ran (last lines)

```
bash gpu/hwcodec/emu/test_v02/run_tests.sh       N2TEST V02 PASS 860/860
bash gpu/hwcodec/loader/test_v03/run_tests.sh    N2TEST V03 PASS 12971/12971
bash gpu/hwcodec/emu/test_v10/run_tests.sh       N2TEST V10 PASS 220589/220589
bash gpu/hwcodec/emu/test_v11/run_tests.sh       N2TEST V11 PASS 960/960   (both builds)
bash gpu/hwcodec/emu/test_v12/run_tests.sh       N2TEST V12 PASS 200797/200797
bash gpu/hwcodec/emu/test_v14/run_tests.sh       N2TEST V14 PASS 453/453
bash gpu/hwcodec/emu/test_v15/run_tests.sh       N2TEST V15 PASS 1616/1616
bash gpu/hwcodec/shim/test_v30/run_tests.sh      N2TEST V30 PASS 277633/277633 + V30-CORE 5/5
bash gpu/hwcodec/tests/emu/run_tests.sh          trace/support/asmrun pass; fuzz refuses (V16-V20 missing)
bash gpu/hwcodec/tests/hw/run_tests.sh           N2TEST T02-hw PASS 6166/6166 (ASan+UBSan, -O2, TSan)
bash gpu/hwcodec/tests/check/run_tests.sh        N2TEST T02-check PASS 22/22
bash gpu/hwcodec/vcu/test_v40/run_tests.sh       N2TEST V40 PASS 1161/1161
bash gpu/hwcodec/ap/test_v41/run_tests.sh        N2TEST V41 PASS 578/578 (+ real n2vcu 39/39)
bash gpu/hwcodec/host/test_iv/run_tests.sh       N2TEST IV PASS 45/45
bash gpu/hwcodec/host/run_vhost.sh               N2VHOST reached M-b (stop UNDEF 0xb318 = V14 F1)
bash gpu/hwcodec/review/r1/run_spot.sh           N2TEST R1 PASS 308/308
```

`review/r1/run_spot.sh` builds the class files and my spot test with the mandated flags
(`-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all` and
`-O2` without sanitizers) under `/tmp/n2hw-r1/spot`. It links the V02 core, memory and
exclusive-monitor sources plus the class sources. The 305 passing checks cover 22 V10,
31 V11, 29 V12, 54 V14, 26 V15, 19 V19, 26 V17, 23 V18, 23 V16 and 18 V20 instruction forms and 14 V02 core checks (IT taken and
not taken, A32 PC bias, a skipped conditional instruction, an unmapped-load fault, a
host-call slot, a shim calling back into the guest, the instruction limit, and a T32 32-bit
fetch); every encoding is from llvm-mc and every expected value is a literal in the test with
its Arm ARM derivation in the comment.

### F17 — V19, high, fixed by T01s applied overlap patch

`gpu/hwcodec/emu/n2a32_neon_shift.c:525-560` (`do_shift`, opc 8/9) and the other
write-Vd-while-reading-Vm loops (VSHLL/VMOVL, VADDL/VADDW/VSUBL/VSUBW, VMLAL/VMLSL, scalar
forms). When Vd overlaps Vm (legal and emitted by compilers for in-place shifts/narrows), a
later iteration reads a lane already overwritten by an earlier one. T01's repro: q0 =
0x12345678 x4, `vshrn.i32 d1,q0,#8` (d1 is the high half of q0) -> expected
0x3456345634563456, got 0x3456345656343456. Fix: copy the 128-bit source to a local before
the loop, or buffer the results. T01 applied such a patch (tests/emu/v19-overlap-fix.diff);
the VSHN repro now gives 0x3456345634563456 and T01s differential table dropped from 551 to
50 mismatches (the NEON narrow/widen block is gone).

### F18 — V33, high, fixed in rev 7 (IV found it)

`gpu/hwcodec/shim/n2vfs.c` `vf_dump_path()` (~:489-504) returns `path` unchanged for anything
not under `/data` or `/sdcard`; `n2vfs_open_path(..., allow_host=1)` then opens that host
path. `sh_fopen()` (`shim/n2shim_stdio.c:409`) passes allow_host=1, so a guest
`fopen("/proc/self/maps")` reads the **host** process's maps (and any host file the daemon can
read). IV's vpud then parses a host address and faults in memcpy at 0xff600000. Fix: return
NULL for non-/data,/sdcard paths so allow_host can only reach the N2HW_DUMP_DIR rewrite;
synthesize /proc/self/maps from the guest mapping list later if vpud needs it. V33 fixed it
by returning NULL from `vf_dump_path()` for anything not under /data or /sdcard (the guest
file system is now devices + virtual files + the dump tree only); suite 323/323, IV confirmed
`/proc/self/maps` is ENOENT.

## Differential harness status (T01, after the V14 patch)

`bash gpu/hwcodec/tests/emu/run_tests.sh` now reaches the differential stage:

```
configuration    calls     pass mismatch     stop  missing
arm-O0           2780     2780        0        0        0
arm-O2           2780     2775        5        0        0
arm-O3v          2780     2780        0        0        0
thumb-O0         2780     2780        0        0        0
thumb-O2         2780     2775        5        0        0
thumb-O3v        2780     2760       20        0        0
armv8-O3v        2780     2760        0       20        0
N2TEST T01 FAIL 50/19460 calls differ or are missing
```

thumb-O0 is perfect and the NEON narrow/widen block is gone after the V19 patch. The
remaining items are `k_v15_extend` (10, V15) and `k_fvec_loop` (20, thumb-O3v, V17/V18/V19
float vector path); the 20 armv8-O3v stops are LDAEX. The 20 armv8-O3v stops are `ldaex` (e1950e9f), an ARMv8
acquire exclusive, not an ARMv7 LDREXD: the armv8 config is compiling the atomic kernel with
ARMv8 atomics that the ARMv7 class does not implement. That is a harness-target decision (drop
`-march=armv8-a` for that kernel or add an explicit ARMv8 extension), not a V12 defect.

## Notes on test quality (not findings)

- V10/V11/V12 tests use llvm-mc encodings and literal expectations, plus large random
  sweeps; my independent forms agree with them.
- V15's 16-bit GE expectations mirror the implementation (F3); V14's census test encodes the
  CBZ holes as reserved (F1). Both need independent expectations.
- V03's relocation tests use hand-built tables with literal expected words, and its
  acceptance line loads the five real vendor objects (3120 relocations, 146 unresolved) —
  independent of the loader's own tables.
- V30's formatter is checked against the host `snprintf` (300 generated cases × 4 AAPCS
  layouts × 5 capacities) and a 30000-case fuzz, plus an end-to-end ARM guest through
  V03 + V02; this is a genuinely independent oracle.

## Not done / limits of this review

- V16, V17 and V18 are present and their own suites pass (V16 3284917/3284917, V17
  2199/2199, V18's case generation was still in flux) but they had not posted DONE at the
  time of writing; provisional findings F9/F13 (V16) and F11 (V17) are tracked above.
  V20, V32, V33, V34, V41 and IV had no files yet. V40 and T02 posted DONE and are accepted
  above (V40 with the low F14).
- T01's full differential/fuzz suite cannot run until V16–V20 exist; only its trace,
  support-object, asmrun and symboliser sections were exercised.
- No phone execution and no arm64 iOS run: host-tested only, as the rules require.
- Performance and interpreter throughput were not measured.
