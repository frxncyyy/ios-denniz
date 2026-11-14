# K02 STATUS — /dev/redwood-vcodec hardware part

Worker: hw-k02. Package K02 of 10. Date: 11 November 2026, wave 3 (rev 7).
State: **host-tested only; nothing here has run on the phone.** Not published. Not committed (the lead owns commits).

## Wave 2 changes (all in my owned files)

K02-L1 (lead's HIGH finding — busy-wait under the global mutex):

- `n2vc_now_us()` = `absolutetime_to_nanoseconds(mach_absolute_time())/1000` in the kernel build.
- `n2vc_sleep()` = `msleep(&n2vc, &n2vc_mtx, PZERO, "n2vc-wait", &ts)` with `ts = 500 us`. It is used by
  `n2vc_wait_irq_locked()` and the `RWVC_OP_POLL` loop instead of `delay()`; msleep drops `n2vc_mtx` while sleeping,
  so a 5 s WAIT_IRQ no longer spins a core or blocks the JPEG driver's `n2vc_ven_get` or the relay.
- Timeouts are measured with `mach_absolute_time()`, never by counting sleeps. After **every** wake both the file
  (`f->used` **and** a new per-open id, because a destroyed slot can be reused by a new open) and the line's/window's
  block (`n2vc.on[blk]`) are re-validated before any register access: `ENXIO` if the file went away, `EACCES` if the
  block was powered off.
- `delay()` remains only for waits <= 100 us: the SPM/gate/mux sequences (10/50/100 us) and `RWVC_OP_DELAY_US`, which
  is executed in <= 100 us chunks.
- Host shim: deterministic fake clock (`n2vc_host_now_us`), `n2vc_host_sleep()` advances it and runs at most one armed
  "during the sleep" callback; test_k02 uses it to power a block off / close the file / close-and-reopen the slot under
  a sleeping waiter.
- The model now also counts a **read** of a block's window while that block is not powered (before, only writes).

K02-L2: `RWVC_DMA_INFO` implemented per header rev 2: own handle or a `RWVC_DMA_SHARED` handle of the other minor
(kernel: IK's `n2vr_dma_allowed()`); `EINVAL` handle 0, `ENOENT` unknown/stale generation, `EPERM` not usable by the
caller; `bytes`/`out_pa` filled, `reserved` untouched. Allowed on both minors (lead's decision; IK's relay already
forwards it). No local `RWVC_DMA_SHARED` define in my files (the only one was in IK's relay, now replaced by the
header's macro).

K02-L3: `RWVC_DMA_RW` copies in chunks of at most 1 MiB, with `dc civac`+`dsb sy` per chunk (after write chunks,
before read chunks). The mutex stays held, so no mid-copy handle revalidation is needed.

Wave 2b: opus-kern's K02 audit findings, implemented in the same file (they had announced 0311 hunks; I asked them to
hold those and rebase):

- HIGH-1: `RWVC_WIN_VENC`/`RWVC_WIN_VENC_C1` are now **0x3000** bytes (vpud mmaps 0x3000; 0x17020000+0x3000 stays below
  JPGENC 0x17030000). The frozen header still documents 0x2000; the kernel accepts the larger range as a superset (lead
  may want to update the header comment; I cannot edit it).
- HIGH-2: before any write, VENC_C1 (larb8) and VDEC_CORE (larb4) additionally require **SPM status bit 19 (MDP)** set
  in 0x16c and 0x170, else `EACCES`; their SMI common is the MDP one (`smi_drv.c`:198-232), not the display's. VENC
  (larb7) and VDEC_SOC (larb5) stay on the DIS check. Also from opus-kern's tested hunks: **CLK_CFG_1** (topckgen 0x020)
  `disp_sel` gate bit 7 and `mdp_sel` gate bit 15 (MDP blocks) must be clear, else `EACCES` (`clk-mt6885.c`:1693-1702).
- Diagnostic (read only): the first VENC/VENC_C1 completion per boot logs the GICD `ISPENDR` bit of INTID 340/345 before
  the ack, so a phone run can confirm the dts SPI without installing a handler.
- HIGH-3: `d_close` fires once for the last close but `d_open` per open, so the slot table now destroys **every** slot of
  that minor and calls `n2vr_server_close()` once per slot; a slot with an ioctl in flight is marked dead and destroyed
  when the ioctl returns (as before). The slot table/`n2vc_slot_add`/`n2vc_slot_close_all` are compiled for the host test
  (`N2VC_HOST_SLOTS`) and tested.
- MED-4: the VENC larbs clear `NON_SEC_CON` bits 0, 8-15 **and 16-19** (mask 0xfff01; no MMSRAM routing because MMSRAM is
  not powered); larb4/5 keep mask 0xff01. Read-back uses the same mask.
- MED-5 / INFO-6: no kernel change (VENC_C1 is the full VEN_CORE1 domain + gates + larb8; real IRQ needs a chained
  dispatcher, documented in VCODEC_HW.md §8).

Wave 2c: root's 273 MHz decision (board 1794413297) and opus-lead's build break (1794413826), both fixed in my file:

- `N2VC_VENC_SLOW` (default 1) selects `venc_sel` parent **4 = `mainpll_d4_d2` = 273 MHz** (`mt6893.dts`:5037-5040
  `venc_step3`, the lowest vcore OPP) on every VENC/VENC_C1 power-on done through `RWVC_POWER` (vcodecd). Setting the
  knob to 0 restores parent 12 = `univpll_d4` = 624 MHz. Only while VEN and VENC_C1 are both off: a running domain is
  never re-muxed. `n2vc_venc_mux()` writes `CLR 0x8f<<24`, `SET parent<<24`, `UPDATE1 1<<24`, waits 100 us, reads back
  and requires gate clear + the wanted parent; `CLK_CFG_13` bits 0-23 (the aud_2/adsp/dpmaif muxes) are not touched.
  The trap opus-lead found (the old `n2vc_mux()` skipped a running mux) cannot happen here: the new function rewrites
  when the parent differs, and logs the switch as `NORD2 VCODEC: venc_sel 0xc030601 -> 0x4030601 (273 MHz)` (opus-lead
  asked for this line; FIRST_PHONE_TEST.md checks it).
- The JPEG driver's shared `n2vc_ven_get()` keeps its own clock: parent 12 (624 MHz), the phone's boot value and what M1
  measured (1080p in 4.6 ms). It re-selects 624 when it powers VEN from off after a vcodec job left 273. If the JPEG path
  holds VEN while a vcodec job runs, that job runs at 624 because a running mux is never rewritten (both directions are
  host-tested). This is my reading of "JPEG is unaffected" in the decision; if the lead wants JPEG to follow 273 too, it
  is one constant.
- hw-kern's autotest (`kern/p031x/bsd/dev/nord2_vcodec_at.inc`:194,:198) calls the old 3-argument
  `n2vc_acquire_locked()`. That name is now a macro for `n2vc_acquire_ex(..., 0)` = "never re-mux", so **at.inc compiles
  unchanged** and opus-lead's build break is fixed in my file; no edit to at.inc is needed. `0` also keeps the
  `--slow`/`--fast` knob's own parent, which step 1 writes before acquiring.
- `run_tests.sh` now builds and runs a third binary with `-DN2VC_VENC_SLOW=0`, so both knob settings are exercised.

Wave 3: `RWVC_WIN_VDEC_ALL` (ABI rev 3, DEC07's patch) landed in my file.

- `nord2_vcodec.inc` now carries DEC07's per-page window: one 0x16000000+0x30000 entry (`RWVC_WIN_VDEC_ALL`), the
  48-page block map, `n2vc_op_block()` used by both the op path and every POLL wake-up, and the larb allow-list. The
  old ten windows and their block checks are untouched. `include/redwood_vcodec.h` is still rev 2, so a six-line
  scaffold defines `RWVC_WIN_VDEC_ALL 12`/`N2VC_WIN_N 13`; it disappears by itself when hw-lead lands rev 3.
- Larb policy: `SMI_LARB_SLP_CON` is 0xc (`smi_reg.h`:70). larb5 0x1600d00c and larb4 0x1602e00c accept a write whose
  changed bits are all inside bit 0 and refuse everything else with `EACCES`; all other words on both pages are
  read-only. larb4's entry is DEC04's finding that the core path performs the same sleep-protect handshake
  (board 1794414983), larb5's is opus-dec1's trace. This replaces DEC07's shipped empty/one-entry list by design.
- Patch note: DEC07's README says to apply from `gpu/hwcodec`; the patch header is `a/bsd/dev/...`, so it must be
  applied from `gpu/hwcodec/kern` (`patch -p1 -i test_dec07/patches/nord2_vcodec-vdec-all.patch`). It applied with
  offsets 1-11 lines against e601a947.

New tests in `kern/test_k02/t_k02.c` (model has a 0x30000 `W_VALL` window with its own per-page power check):
- no power: a core page and the larb5 page are `EACCES`;
- SOC alone: the 0x0f page writes/reads back, the 0x20 page is `EACCES`, larb5 is readable;
- CORE (implies SOC): 0x20/0x0e/0x2f writes, both SLP_CON bit-0 writes, read-back; larb6 and VDEC_SYS are writable;
- the allow-list is exact-offset and bit-masked: bit 1, a masked bit 0+1 write, offset 0x10 on either larb and a
  masked write outside bit 0 are `EACCES`, `out_done` stops; bit 0 alone passes, and a POLL of bit 16 is allowed;
- range: 0x2fffc is the last word, 0x30000 is `EINVAL`;
- core powered off from a sleep hook during a POLL on a core page: `EACCES`, one 500 us sleep, zero model violations,
  `out_done` = 1.

Negative controls for the new tests (in /tmp, not in the tree): empty allow-list -> 5 failures; page map all CORE -> 3
failures (the SOC-only page); both are the intended checks.

## What is implemented (all in my owned files)

`gpu/hwcodec/kern/bsd/dev/nord2_vcodec.inc` (MIT, ~1060 lines):

- `nord2_vcodec_start()`: `cdevsw_add` + `/dev/redwood-vcodec` (minor 0, 0600), then IK's `n2vr_start(&hw)` for
  `/dev/redwood-vcodec-client`, then K01's `n2jd_share_ven(n2vc_ven_get, n2vc_ven_put)`.
- `RWVC_INFO`: ABI 1, real powered mask, pool PA/size/free, `irq_mode 0`, `server_present` from the relay.
- `RWVC_POWER`: per-open-file and per-block reference counts; VEN_C1 implies VEN and VDEC_CORE implies VDEC_SOC (one
  hidden reference on the parent each); display-domain check before any bus-protection release; full bounded SPM
  sequences for the four blocks (PWR_ON/ON2ND + status timeout with withdrawal, CLK_DIS/ISO/RST_B/SRAM_PDN with ack,
  protection release step 2 then step 1, gate enable + status check, LARB conf + OSTDL + `NON_SEC_CON` clear and
  read-back; reverse order on power-off, with acknowledged protection and a real error on ack timeout).
- `RWVC_REGS`: copy-in in 64-op chunks (32 KB max), 2048-op limit, per-op window/alignment/range validation and "the
  block of the window is powered" (EACCES), READ/WRITE/WRITE_MASK/POLL/DELAY, results copied out even when a later op
  fails, `out_done` = fully executed ops, POLL = first read + 500 us `msleep` between reads up to the measured timeout,
  DELAY <= 1000 us (in <= 100 us `delay()` chunks), 2 s measured budget per batch.
- `RWVC_DMA_ALLOC/FREE/RW/FILL`: first-fit page allocator over `RW_CLAIM_VCODEC` (86 MiB, 22016 pages, 1024 slots,
  128 live handles per open), zero-filled page-aligned buffers, generation-tagged handles, owner-or-shared access
  (shared only across minors), 64-byte cache maintenance, everything freed on close.
- `RWVC_DMA_INFO`: see wave 2 above.
- `RWVC_WAIT_IRQ` / `RWVC_IRQ_CLEAR`: polled mode only (`irq_mode 0`), first status read then 500 us sleeps up to the
  measured `timeout_ms` (<= 5000), VENC = `WIN+0x5c` matched on `0xbf`, cleared by writing the bits to `WIN+0x60`;
  VDEC = `VDEC_MISC`/`VDEC_LAT_MISC+0xa4` matched on bit 16, cleared with the two Linux ISR writes. Per-open count
  since the last wait.
- Minor policy: minor 0 gets everything; minor 1 gets INFO + DMA alloc/free/rw/info only (EACCES otherwise). MSG and
  the minor-1 node are hw-ik's relay; this file registers it and performs its server open/close and MSG dispatch.
- `n2vc_ven_get()` / `n2vc_ven_put()`: the shared VEN reference K01 calls; first get runs the full VEN sequence and
  enables gates 0/4/28 (never bit 8, the JPEG encoder's), last put powers down.
- `n2vc_relay_hw_ioctl()` / `n2vc_relay_closed()` / `n2vc_open_create/destroy()` / `n2vc_ioctl()` / `n2vc_info_fill()`:
  the seam documented in `kern/VCODEC_HW.md` §9. IK's relay now calls `n2vc_relay_closed(p)` at a client's last close
  (that old open item is resolved).

`gpu/hwcodec/kern/VCODEC_HW.md`: updated §5/§6/§7 for the sleeping waits, the revalidation, `RWVC_DMA_INFO`, the 1 MiB
RW chunks and the read-side model check. The power/mux/gate/LARB/IRQ facts and their `file:line` citations are unchanged.

`gpu/hwcodec/kern/test_k02/{t_k02.c,run_tests.sh}`: register/SPM model host test, extended as below.

## Tested (host only)

    $ bash gpu/hwcodec/kern/test_k02/run_tests.sh
    == clang -O1 ASan+UBSan
    == clang -O2
    == clang -O2 N2VC_VENC_SLOW=0 (the 624 MHz knob)
    == run ASan+UBSan
    NORD2 VCODEC: VENC larb port 5 was routed to MMSRAM (0x15501): back to DRAM
    NORD2 VCODEC: VENC not powered: the display domain is off
    NORD2 VCODEC: VENC power status timeout (con 0x1112): request withdrawn
    NORD2 VCODEC: VENC SRAM ack timeout (con 0x100d): bus protection left on
    NORD2 VCODEC: VENC still isolated/protected after power-on (spm 0x1a0000/0x1a0000 con 0xd mm 0xff000000 mm2 0xc3)
    NORD2 VCODEC: VENC bus protection 0x4000000 not acknowledged (0xf3000000)
    NORD2 VCODEC: VENC power-off failed (110): its parent reference is kept
    NORD2 VCODEC: venc_sel mux 0xe0 did not take parent 4 (reads 0x80000000)
    NORD2 VCODEC: VENC larb port 3 still translates (0x5501)
    NORD2 VCODEC: VENC_C1 not powered: the MDP domain (its SMI common) is off
    NORD2 VCODEC: VDEC_CORE not powered: the MDP domain (its SMI common) is off
    NORD2 VCODEC: VENC not powered: the disp_sel clock is gated (CLK_CFG_1 0x80)
    NORD2 VCODEC: VDEC_SOC not powered: the disp_sel clock is gated (CLK_CFG_1 0x80)
    NORD2 VCODEC: VENC_C1 not powered: the mdp_sel clock is gated (CLK_CFG_1 0x8000)
    NORD2 VCODEC: first VENC done status 0x4; GIC INTID 340 pending -1 (before the ack)
    NORD2 VCODEC: first VENC_C1 done status 0x4; GIC INTID 345 pending -1 (before the ack)
    N2TEST K02 PASS 588919/588919 (violations 0)
    == run -O2
    N2TEST K02 PASS 588919/588919 (violations 0)
    == run -O2 N2VC_VENC_SLOW=0
    N2TEST K02 PASS 588919/588919 (violations 0)
    N2TEST K02 PASS 588919/588919 (violations 0)

(~21 s, three builds `-std=c11 -g -Wall -Wextra -Werror`; the `NORD2 VCODEC:` lines are the injected-fault cases, the
GIC-pending diagnostic (`-1` on the host, which has no GICD) and the `venc_sel 0xc030601 -> 0x4030601 (273 MHz)` switch
lines wave 2c added for the phone log; all are expected.)

The model fails the run (violations > 0) on any read **or** write of a block's register window / gate controller / LARB
while that domain is off/isolated/protected, on any OSTDL/NON_SEC_CON write to another block's LARB port, on any
protection/PWR_CON/gate/mux write outside the block being powered, and on every wait that does not end.

New in wave 2, covered by tests:

- power-off during `RWVC_WAIT_IRQ` -> `EACCES`, no window access after the block went off (0 violations);
- file close during `RWVC_WAIT_IRQ` -> `ENXIO`;
- close-and-reopen of the same slot during `RWVC_WAIT_IRQ` -> `ENXIO` (open-id check);
- power-off during `RWVC_OP_POLL` -> `EACCES`, `out_done` stops before the poll, exactly one 500 us sleep;
- file close during `RWVC_OP_POLL` -> `ENXIO`;
- measured time: a 3 ms WAIT_IRQ leaves after >= 3000 fake us; a POLL that matches after ~1.5 ms reports a duration in
  [1000, 2000] us;
- MDP off: VENC_C1 and VDEC_CORE refuse with EACCES (and unwind the hidden parent reference), VENC/VDEC_SOC still work;
- CLK_CFG_1 gate bits: disp_sel gated refuses VENC and VDEC_SOC; mdp_sel gated refuses VENC_C1 (and VDEC_CORE);
- window range: offset 0x2ffc of the VENC window is accepted, 0x2ffe is EINVAL (0x3000 window);
- the VENC larb clears NON_SEC_CON bit 16 (MMSRAM routing) and the VDEC larb keeps it;
- 16 minor-0 opens produce 16 slots; one `d_close` returns/destroys all of them (busy one marked dead), no EBUSY leak;
- `RWVC_DMA_INFO`: own handle (0), shared handle from the other minor (0), unshared handle from another file (EPERM),
  another minor-1 client's shared handle (EPERM), handle 0 (EINVAL), unknown and wrong-generation handles (ENOENT),
  freed-and-reused slot (old handle ENOENT, new handle 0), `reserved` untouched;
- 1 MiB-chunked DMA_RW: 3 MiB - 200 bytes at offset 100, literal pattern checked in the model's memory at both 1 MiB
  chunk boundaries and the ends, byte-identical read-back, bytes outside the range untouched;
- all earlier coverage stays green (four blocks, parent dependencies, fault injection, register validation branches,
  2048-op budget, all 12 windows, DMA ownership/sharing, 10000-op allocator property test, close with outstanding
  buffers + power refs, polled IRQs on all four lines, `n2vc_ven_get/put`).

Negative controls (test-of-the-test, run in /tmp on copies, not in the tree): deleting the post-sleep `on[blk]` check
gives 1000 "read of a block that is not powered" violations and `N2TEST K02 FAIL 588803/588805`; deleting the open-id
check makes the close-and-reopen case continue with the new file and fail that check. Both new checks are load-bearing.

Kernel compile (local, /tmp copy of the q10at3 stage + 0298 + relay + my .inc + jpegdev + **at.inc**, exact CI flags
via `gpu/tools/xnu_staged_compile.py ... --extra=-Wno-unsafe-buffer-usage`, `-Wall -Wextra -Werror`): **compiles clean**
(1,328,368-byte object, 17:46); `llvm-nm`: 6 new undefined symbols vs the q10at3 object (`_copyin`, `_copyout`,
`_memset`, `_proc_exiting`, `_proc_pid`, `_proc_uniqueid`, all defined in `kernel.development.nord2-metal-q10at3`), **0
unknown**; the same object without at.inc is 1,289,200 bytes, also 0 unknown. Including at.inc is the proof that
opus-lead's 0311 build break is fixed. The official check is IK's `kern/compile_check.sh` (regenerates 0299 with the
current relay/header); I did not run it because it writes IK's patch file; IK must regenerate 0299 with the FINAL sha **800fd77c** (q10jx was built from 7ded4040; e601a947 added the phone log line and 800fd77c adds the DEC07 VDEC_ALL window).

## Not done / open

1. **Real interrupts (`irq_mode 1`) are not implemented.** Polled sleeping mode only, as the task orders. The exact
   GICv3 registration proposal (per-SPI `nord2_gic_spi_configure` + `ml_install_interrupt_handler`, confirm-pending,
   spurious guard, 500 us sleep-poll fallback) is in `kern/VCODEC_HW.md` §8 for hw-ik; opus-kern's finding that
   `ml_install_interrupt_handler` keeps one handler per CPU (so a chained dispatcher is needed) is recorded there.
2. **Never run on the phone.** No flash, no boot, no ioctl from user space. The VEN sequence is the M1-proven one; the
   VDE/VDE2/VEN_CORE1 sequences are transcribed from `clk-mt6885-pg.c` and only exercised against the model.
3. **`vdec_lat_sel` uncertainty**: on MT6893 the VDE1 (SOC/LAT) gates declare `vdec_ck` as parent although
   `vdec_lat_sel` exists; this driver programs both muxes when gated. `[I]`, documented.
4. **VENC core 1 IRQ registers** (`0x1782005c/0x60`) are inferred from the identical IP block; Linux's handler only
   ever uses core 0's window. `[I]`.
5. **`VDEC_RACING` window → VDEC_CORE** is an inference (the racing words are restored by the core clock-on path).
6. **Mux parents**: `vdec_sel`/`vdec_lat_sel` are left alone when already running; when gated the driver selects
   parent 14 (`univpll_d4`). `venc_sel` is owner-dependent (wave 2c): vcodecd gets parent 4 = 273 MHz
   (`N2VC_VENC_SLOW=1`), the JPEG path parent 12 = 624 MHz; a running mux is never rewritten. The resulting frequencies
   depend on the bootloader's PLL rates, which this kernel does not read. `[I]`.
7. **The 2 s per-batch budget and the 2 s/1 s clamp** are kernel policy I added (the frozen header bounds one POLL at
   1 s but not a whole batch); documented in VCODEC_HW.md §7. One constant to change.
8. **DMA handle generation is 16 bits**: after 65536 reuses of one slot a stale handle could alias a new allocation.
   The pool has 1024 slots; the host test does 10001 allocations without wrapping.
9. **Close/open ABA is now detected**, but the id is captured inside `n2vc_ioctl`'s locked section; a close that races
   *before* the id capture is the normal `ENXIO` at entry. No remaining known hole. Residual: a minor-1 client's close
   can destroy its `n2vc_open` while another thread of the same process is inside an ioctl (IK's relay does not defer
   it the way `n2vc_dev_close` does for minor 0); the id check makes the waiter return `ENXIO` instead of touching the
   reused slot, which is the safe outcome.
10. The slot table and its close-all semantics are host-tested (`N2VC_HOST_SLOTS`), but **the cdevsw entry points and
    the relay registration are kernel-only**: they are covered by IK's compile check and the local staged compile above,
    not by a model test. `d_open`/`d_close` themselves need a phone or a kernel simulator to exercise.
11. `n2vc_pool_free_pages()` is a linear scan of 22016 bits on every `RWVC_INFO`; fine at this frequency, not a hot path.
12. **Header comment vs kernel window**: `include/redwood_vcodec.h` still says VENC/VENC_C1 are 0x2000 bytes; the kernel
    now accepts 0x3000 (superset, vpud's mmap). The header is frozen and owned by hw-lead; asked for a comment update on
    the board.
13. R2's rule sweep flagged the missing SPDX first line in `kern/test_k02/t_k02.c` (and scripts): added
    `/* SPDX-License-Identifier: MIT */` first line and `# SPDX-License-Identifier: MIT` in `run_tests.sh`; no code change.
14. opus-kern's audit accepted the VEN/VEN_C1/VDE/VDE2 PWR_CON, status, protection bits/order, muxes, gates, larb conf,
    VENC and VDEC IRQ registers as [V]; the four items they found (HIGH-1/2/3, MED-4) are implemented and tested here.
    MED-5/INFO-6 need no kernel change.

15. `RWVC_WIN_VDEC_ALL` is tested host-only and the page map is `[I]` for the vendor pages the open sources do not
    name (they are conservatively mapped to the core, which implies SOC); DEC02's measured page list can refine single
    pages in `n2vc_vdec_page_blk()`. The header rev 3 text is hw-lead's; until it lands, my scaffold keeps the tree
    building and the old ten windows unchanged.

## Files

- `gpu/hwcodec/kern/bsd/dev/nord2_vcodec.inc` (mine)
- `gpu/hwcodec/kern/VCODEC_HW.md` (mine)
- `gpu/hwcodec/kern/test_k02/{run_tests.sh,t_k02.c,STATUS.md}` (mine)
- `gpu/hwcodec/dsh-state/workers/K02/STATUS.md` (this file)
