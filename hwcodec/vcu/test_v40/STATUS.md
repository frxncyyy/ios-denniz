# V40 -- the /dev/vcu device model: status

Package: `gpu/hwcodec/vcu/` (owner V40). Last update: 11 November 2026, 18:30.
Host-tested only. **Not run on the phone, nothing published, nothing committed by me** (the lead commits).

## Post-restart verification (18:25, after the PC power loss)

The files were byte-identical after the restart (`n2vcu.c c70a4bca…`, `n2vcu.h 4bb0c483…`,
`mtk_vcu_abi.h be9fa5f4…`, `t_v40.c 832c8b00…`); no recovery was needed. Re-ran on the restarted
machine:

    bash gpu/hwcodec/vcu/test_v40/run_tests.sh          -> N2TEST V40 PASS 1536/1536
    N2HW_OUT=/tmp/n2hw-V40/verify/h264 bash gpu/hwcodec/fakevenc/run_session.sh
      -> bitstream-320x240.h264 validated (115841 bytes), M-f reached, fake violations 0
    N2HW_OUT=/tmp/n2hw-V40/verify/h265 bash gpu/hwcodec/fakevenc/run_session_hevc.sh
      -> bitstream-320x240.h265 validated (43947 bytes), M-f reached, fake violations 0
    joined-header settings (daemon/vcodecd.c:599-600 seq_hdr_mode=1 prependheader=1) patched into a
    /tmp copy of host/n2vhost.c only:
      -> N2VHOST encode frame bytes=115841 key=1 sync=1
      -> run_vhost.sh: .../bitstream-320x240.h264 validated (H.264 SPS/geometry/decode/PSNR, 115870 bytes)
         (before rev 6 this was headers + zeros: the memmove read the stale guest mirror)

DEC04's merge was re-validated on the current tree (their earlier run used n2vcu.c 3a767585; the
final c70a4bca differs only in the stricter 0x064 match):

    N2HW_TMP=/tmp/n2hw-V40/d4 bash gpu/hwcodec/vcu/test_dec04/run_tests.sh
      -> N2TEST DEC04 PASS 2016/2016; V40 suite on the patched /tmp mirror: N2TEST V40 PASS 1539/1539

That run stages the proposed rev-3 header enum in the /tmp mirror only; the tree header is still
rev 2. Landing order once hw-lead/root lands `include/redwood_vcodec.h` rev 3 (DEC07 patch): copy
`test_dec04/dec04.patch` then `test_dec04/dec04-v40-adapt.patch` over `vcu/` and `test_v40/`, re-run
both suites. Not needed for the first VENC phone encode; needed by DEC02/DEC04 for VDEC.

## Rev 6 (this session): VENC bitstream mirror refresh for joined headers

Root's RESTARTED request and opus-hevc's HIGH: with `seq_hdr_mode=1` + `prependheader=1` (the
daemon's settings, `daemon/vcodecd.c:599-600`) the vendor encodes at the bitstream buffer start and
`VENC_PrependHeader` (oal 0x228d0) memmoves the frame up on the CPU (0x2295c) without invalidating
first. Our guest view is a copy, so the memmove read stale bytes and the daemon got header + zeros.
`vcu_venc_refresh_bitstream()` (n2vcu.c, called from `vcu_gce_flush_ioctl()` after
`gce_run_list()` for `codec_type == VCU_VENC`) now copies the hardware bytes back into the guest
mirror: it finds the `CMD_WRITE` to VENC window offset 0x064 (bitstream base, value pa >> 4,
`facts/VENC_PROTOCOL.md`), resolves the buffer in the model's allocation table or through V41's
`n2vc_dma_lookup_pa()`, and calls `n2vc_dma_sync(..., to_device = 0)` for the bytes from that
address to the buffer end. Only the exact VENC/VENC_C1 offset 0x064 is refreshed (0x1064 is not),
only for VENC lists; a failed list still refreshes (the hardware may have written part of it) and
an unknown pa is not an error. New stats `gce_bitstream_syncs` / `gce_bitstream_bytes`.

Measured with the real vendor stack on the host: `bitsyncs=1 bitbytes=1048576` per encode (H.264 and
HEVC, plain tree). With the daemon's joined-header params patched into a /tmp copy of
`host/n2vhost.c` only, the H.264 frame is 115841 bytes (was header+zeros) and the 115870-byte
stream passes the SPS/geometry/decode/PSNR check.

## Rev 5: VENC power around a GCE job, callback survives a failed list

Root's first-phone-encode request (board 17:00) and opus-venc's patch: in GCE mode the library never
sends `VCU_IPIMSG_ENC_POWER_ON`; Linux powers the encoder inside `vcu_gce_set_cmd_flush()` when the
core's job count was 0 (`venc_encode_prepare`, mtk_vcu.c:994-997) and unprepares it in the flush
callback (:806-830). `vcu_gce_flush_ioctl()` now calls `hw->power(RWVC_BLK_VENC | RWVC_BLK_VENC_C1
by core_id, 1)` before `gce_run_list()` and 0 after, for `codec_type == VCU_VENC` only (the kernel's
`if (i == VCU_VENC)`); VDEC lists are untouched. A NULL `power` hook is a back end without power
control and is skipped.

Also from the same patch: `flush_done` (the kernel's `flush_done` atomic, :806-812) is now
incremented **even when the list failed**. The kernel's CMDQ runs the timeout callback and then the
flush callback for a failed packet, so the daemon's `VCU_GCE_WAIT_CALLBACK` must not hang when the
hardware errored. The ioctl still returns the list's first error, as before. Errors stay counted
where they are detected (no double count).

New test `t_gce_power()` (20 checks x 3 builds): core 0 powers `RWVC_BLK_VENC` on then off, core 1
powers `RWVC_BLK_VENC_C1`, the register write lands while powered, a VDEC list makes no power call,
and a list that fails with `-ETIMEDOUT` (no `VENC_EOF`) still gets power on/off and exactly one
consumable callback (second wait `-ETIMEDOUT`).

Integration on the host runner with this tree (no /tmp patches, T02 fake + opus-venc's fake VENC,
`bash gpu/hwcodec/host/run_vhost.sh --seconds 5`):

    N2VHOST hw power_on_venc=3 venc_ops=64 irq_waits=1 venc_waits=1 regs_calls=14
    N2VHOST vcu ... gce_flushes=1 ... log_gets=0
    N2VHOST fake violations=0
    N2VHOST bitstream .../bitstream-320x240.h264 115841 bytes
    run_vhost.sh: ... validated (H.264 SPS/geometry/decode/PSNR, 115841 bytes)

The runner also powers VENC around the job itself, so both power paths run; they are balanced
(on/off pairs) and the fake reports no violation. The model's calls are the ones the phone needs.

## What is implemented

| File | What |
|---|---|
| `mtk_vcu_abi.h` | GPL-2.0. The `/dev/vcu` ABI as the 32-bit (AAPCS) daemon sees it: `mem_obj` 0x20, `map_obj` 0x10, `gce_cmds` 0xe808 (u64 arrays 8-aligned: addr at 0x800, data 0x4800, mask 0x8800, dma_offset 0xa800, dma_size 0xc800, cmd_cnt 0xe800), `gce_cmdq_obj` 0x20, `gce_obj` 0x10, `share_obj` 0x58, `log_test_nofuse` 0x400; `enum gce_cmd_id`, `enum gce_event_id`, `enum ipi_id` (mtk_vcu.h:113) and `mtk_vcu_ipi_instance()` (mtk_vcu.c:118-125); every request number; `_Static_assert` on every size, offset and request. |
| `n2vcu.c` | The `struct n2vfs_dev` for `/dev/vcu` and `/dev/vpu` over `n2vc_hw`, plus the AP-side API. One context shared by both nodes (V33: one ctx per device). VENC GCE lists refresh the bitstream buffer's guest mirror after the run (rev 6). |
| `n2vcu.h` | `n2vcu_create/destroy`, `n2vcu_dev/dev_vpu`, `n2vcu_ap_register/ap_send/ap_pending/ap_wait_dispatched/ap_set_log`, `n2vcu_map_dm_addr/dmem`, timeout setters, `n2vcu_set_gce_threads`, `n2vcu_abort`, `n2vcu_stats`. |
| `test_v40/` | `layout_probe.c` (the kernel header compiled for armv7a), `probe_extract.py`, `t_v40.c` (512 checks), `run_tests.sh`, `compile_ios.sh`. |

Handlers, each reproducing `mtk_vcu.c` / `mtk_vcodec_mem.c`:

* `VCU_SET_OBJECT` (:1805-1823): range check on `id` (`-EINVAL`), payload handed to the registered
  handler, the (possibly modified) `share_obj` copied back, unknown-but-in-range id returns 0
  (the kernel's `vcu_ipi_handler` return value is overwritten by `copy_to_user`). `len > 80` is
  clamped before the handler (memory safety; the kernel passes the bogus length through).
  `id > IPI_VCU_INIT` with handler return 0 counts the ACK the kernel raises (:1403-1410).
* `VCU_GET_OBJECT` (:478-518): slot by `ipi_id_to_inst_id`; `dispatched` (ipi_done) set at entry,
  `got` (ipi_got) cleared on fetch; blocks in 200 us steps, **bounded** (default 10 s), returns
  `-ETIMEDOUT`, `-EINTR` when `stop_request` or `n2vcu_abort()`.
  `n2vcu_ap_wait_dispatched()` reproduces the AP's `ipi_done` spin (:441-446), i.e. it completes
  only after the daemon comes back for its next message.
* `VCU_MVA_ALLOCATION/FREE`, `VCU_PA_ALLOCATION/FREE` (:1830-1921, `mtk_vcodec_mem.c:160-315`):
  `hw->dma_alloc`, guest mirror via `n2vc_dma_guest_map`, `iova = pa = hardware address`,
  `va = guest address`; failure sets all three fields to `-1` and returns `-EINVAL`/`-ENOMEM`;
  free compares `va`+`iova`+`len` (MVA) or `pa`+`va` (PA) exactly like `mtk_vcu_free_buffer/page`,
  zeroes the fields on success; 0 length, > 128 MiB and the 512-buffer cap are enforced;
  the last `close()` frees everything (as `mtk_vcu_mem_release` does).
* `VCU_CACHE_FLUSH_ALL/BUFF/INVALIDATE` (:1922-1958, `mtk_vcodec_mem.c:358-410`): `n2vc_dma_sync`
  for **vb2 (MVA)** buffers only, as the kernel (a PA page is not in `vcu_queue->bufs` and the
  kernel's `vcu_buffer_cache_sync` then returns `-EINVAL`); range checked without wrapping.
* `VCU_GCE_SET_CMD_FLUSH` (:880-1096, `vcu_set_gce_cmd` :695-800): per command:
  `CMD_READ` -> `RWVC_OP_READ` (value kept as the GCE scratch value), `CMD_WRITE` -> `RWVC_OP_WRITE`
  or `RWVC_OP_WRITE_MASK` when `mask != 0xffffffff`, `CMD_POLL_REG` -> `RWVC_OP_POLL`
  (value=data, mask=mask, the frozen `RWVC_REGS` rule), `CMD_WAIT_EVENT` -> `hw->wait_irq` with the
  event class mapped to `RWVC_IRQ_VDEC` (0..15), `RWVC_IRQ_VDEC_LAT` (16..31),
  `RWVC_IRQ_VENC_C1` (VENC_EOF_C1=36) or `RWVC_IRQ_VENC` (other VENC/unknown), events >= 64 dropped
  like :746-751, `CMD_MEM_MV` -> register or PA source read + PA destination write (:752-772, the
  destination must be a PA page there too), `CMD_POLL_ADDR` -> register poll op or a bounded poll of
  the guest/PA mirror, `CMD_SEC_WRITE` and unknown ids dropped (:798-800). Adjacent register
  commands go to `hw->regs` as one batch (up to `RWVC_REG_MAX_OPS`); a wait or an observable read
  flushes the batch first, so the back end sees the exact order. Bad addresses are **skipped and
  counted**, not errors (the kernel logs and skips). `codec_type >= 2`, `core_id >= th_num` and
  `cmds_user_ptr == 0` are `-EINVAL`; `cmd_cnt >= 2048` is clamped to 2048, executed and the ioctl
  returns `-EINVAL` (:1036-1041); `flush_done` is incremented whenever the list was run, even when
  it failed (the kernel's timeout path ends in the same callback); a VENC list powers its core
  (`RWVC_BLK_VENC`/`RWVC_BLK_VENC_C1`) around the run (mtk_vcu.c:994-997, :822) and refreshes the
  bitstream buffer's guest mirror from hardware memory after it (rev 6: the vendor's
  `VENC_PrependHeader` memmove reads it on the CPU inside the same job; statistics
  `gce_bitstream_syncs`/`gce_bitstream_bytes`).
* `VCU_GCE_WAIT_CALLBACK` (:1098-1140): unknown handle `-EINVAL`, otherwise waits for
  `flush_done` (bounded, default 5 s), consumes one.
* `VCU_SET_MMAP_TYPE`: accepted (`map_obj` read), returns 0. Deviation: the kernel has no case and
  returns -1/EPERM; the task says "accept".
* `VCU_GET_LOG_OBJECT`/`SET_LOG_OBJECT` (:521-545, :2113-2145): **GET blocks** like the kernel's
  `wait_event_freezable(vdec_log_get_wq, vdec_log_got)` - on a condition variable until
  `n2vcu_ap_set_log(vcu, text)` stores a log (the model's `vcu_set_log()`), a device **close**,
  `n2vcu_destroy()` or `n2vcu_abort()`, or the calling cpu's `stop_request`; the last four return
  `-EINTR` (the kernel's `-ERESTARTSYS`) within one 100 ms poll tick. `vdec_log_got` is cleared
  after the copy (:542). The text is truncated to 1023 bytes plus NUL (:2123). `size` is the `_IOC`
  size field and is clamped, so a smaller guest object is never overrun. SET stores the bytes, sets
  the same flag and wakes a waiter. `n2vcu_destroy()` waits (500 ms bound) for a blocked GET to
  leave before freeing the context; `stats.log_waits` counts the calls that had to block, `SET`'s
  request number is nr 12 with an `_IOC` size the kernel tree cannot supply (`struct log_test` is
  not defined anywhere), so any size is accepted.
* Unknown request / wrong `_IOC` size: `-EPERM` (the kernel's `default:` returns `ret = -1`).
* `mmap` (:1653-1720): offset inside the three VCU windows 0x16000000+0x40000, 0x17020000+0x10000,
  0x17820000+0x10000 -> a guest MMIO megabyte in a private range 0x30000000..0x34000000 (below the
  loader's allocator start 0x40000000), hook translating to `RWVC_WIN_*` and `hw->regs`; sub-word and
  (undefined on real Device memory) unaligned 32-bit accesses are defined as little-endian byte
  accesses split across the two words. `MAP_SHMEM_ALLOC_BASE/COMMIT_BASE` -> the 0x80000-byte DMEM
  (offset honoured; the kernel always maps from its start). `MAP_SHMEM_MM*`/`PA_BASE` and a bare
  `pa`/`iova` -> the registered DMA buffer's guest mapping. Anything else `-EINVAL`.
* `n2vcu_map_dm_addr` (:1199-1230): guest address must be in `[dmem, dmem+len]` (`off > len` is
  rejected exactly like the kernel), returns `n2a32_ptr`.

## Test oracle

`t_v40.c` builds every guest struct byte by byte at a literal offset, and compares three independent
sources for every size/offset/request number: the **armv7a probe of the kernel's own uapi header**
(`layout_probe.c` -> `probe_values.h`, generated on every run), the values in `mtk_vcu_abi.h`, and
literals in the test. Two back ends are driven: a recording fake in the test (exact `rwvc_reg_op`
batches, DMA and IRQ scripting) and T02's `n2vc_hw_open_fake()` with a script file (power-gated
windows, violation counter, record file). The four `n2vc_dma_*` functions are a local stand-in for
V41's `ap/n2vc_dma.c`. The test links V02's real `emu/n2a32_mem.c`; the two core entry points it
needs (`n2a32_cpu_init`, `n2a32_trace_mmio`) are reproduced in the test file.

Sensitivity was checked by mutation: moving the VENC window base by 4 bytes fails 16 checks;
changing `MTK_VCU_SET_OBJECT` by one is caught by `_Static_assert` at compile time.

## Review findings

* **R1 F14 (low, fixed in rev 3, rewritten in rev 4).** `vcu_mmap_ioctl()` computed
  `end = offset + len` before the MAP_SHMEM_* range tests, so a 64-bit offset near `UINT64_MAX`
  wrapped and could pass them. Rev 3 capped the offset; rev 4 does what the lead asked - every
  range test is now `vcu_span_contains(base, window, offset, len)`, which compares `offset - base`
  and `len` against `window - (offset - base)` and never forms `offset + len`, so no offset can
  wrap. The 32-bit iova guard stays (the hardware address registers are 32 bits, and truncating
  would alias another buffer). Tests: offset `0xfffffffffffff000` and `UINT64_MAX` -> 0/EINVAL,
  iova `0x100000000` -> 0, the last page of ALLOC/COMMIT/PA/MM accepted, one byte less headroom
  and one byte past each end rejected.
* **Lead review (HIGH, fixed in rev 4): `VCU_GET_LOG_OBJECT` must block.** It returned
  immediately, so vpud's log thread spun: IV measured 805506 calls in 5 s on the host runner.
  It now blocks on a condition variable exactly as `vcu_log_get()` does (see the handler bullet
  above); `n2vcu_ap_set_log()` is the new AP-side setter. Covered by `t_log_block()` (300 ms
  blocked then released by a set_log with the text; `-EINTR` within 200 ms from `stop_request`,
  close and abort; flag cleared after the copy) and `t_log_destroy()` (destroy wakes the waiter
  before freeing the context).
  **Measured on the real vpud host path** (IV's runner, tree as of rev 4): `run_vhost.sh --seconds 5`
  prints `log_gets=0` (IV's pre-fix run: `log_gets=805506`), and under gdb a breakpoint on
  `vcu_get_log_object` is hit exactly **once** in the same 5 s run and never again: the daemon's
  log thread enters the ioctl once and stays blocked, instead of spinning ~160k times per second.
  The run still reaches M-c and stops at the known M-d blocker (`n2ap_venc_open` -EIO,
  `vcu_inst_addr` outside the DMEM - V34/V41/IV's item, unchanged by this revision).

## Test results (11 November 2026, this tree)

    $ bash gpu/hwcodec/vcu/test_v40/run_tests.sh
    == V40 layout probe: .../nord2-current-kernel/include/uapi/linux/mtk_vcu_controls.h (armv7a AAPCS)
    V40: probe words 59, sentinel 0x56433430
    == V40 build/run: asan
    SUMMARY 479 479
    == V40 build/run: o2
    SUMMARY 479 479
    == V40 build/run: tsan
    SUMMARY 479 479
    N2TEST V40 PASS 1437/1437

(three builds of the same 479 checks: `-O1 -g -fsanitize=address,undefined
-fno-sanitize-recover=all`, plain `-O2`, and `-fsanitize=thread -O1 -g`; all `-std=c11 -Wall
-Wextra -Werror`; artifacts only under `/tmp/n2hw-V40`, ~20 s total; the threaded
GET_OBJECT and GET_LOG suites are what the TSan build covers.  Rev 4 added 67 checks: the blocking
log wait, its four wake sources, the log_blocked gauge, and the F14 range boundaries.  Rev 5 adds
20: VENC/VENC_C1 power around a GCE list, no power for VDEC, and the callback after a failed
list.)

    $ bash gpu/hwcodec/vcu/test_v40/compile_ios.sh
    N2TEST V40-IOS PASS 1/1 (arm64-apple-ios16.5, compile only)

## What the tests show and do not show

Shown: the ioctl decoder accepts exactly the kernel's request numbers and rejects anything else like
the kernel; every handler's return value and every field of every returned struct matches the kernel
source for the normal, error and edge cases listed above; GCE commands produce the expected
`rwvc_reg_op` sequences in the expected order (one test per `gce_cmd_id`, plus the skip rules, the
2048-op batch, the event-to-IRQ mapping and the timeout paths); the mmap paths resolve and the MMIO
hooks reach `hw->regs` at the right window and offset; the model coexists with T02's fake through
the frozen `n2vc_hw` interface and records no power violations; `VCU_GET_LOG_OBJECT` really blocks
(a second thread stays inside the ioctl for 300 ms) and is released with the text by
`n2vcu_ap_set_log()`, or with `-EINTR` by `stop_request` (within 200 ms), a close, abort and
destroy.

Not shown: anything on the phone; that the real GCE semantics equal the software replay (in
particular `CMD_POLL_REG`'s masked comparison and `CMD_READ` into the GCE scratch register are not
observable through the vendor API, so only the emitted ops are checked); that the vendor code's
computed mmap offsets are among the ones the model accepts (the two computed `eVideoMemMAP`/
`eVideoMMAP` offsets of D01's open question 8.6 are still unknown - the model accepts the bare
pa/iova and the documented bases); performance.

## Not done / deviations (all deliberate, none silent)

1. **Waits that the kernel bounds stay bounded here** (GET_OBJECT 10 s, wait-for-event 1 s,
   poll-address 1 s, wait-for-callback 5 s; settable, `-ETIMEDOUT`). The two waits the kernel makes
   indefinite (GET_OBJECT, GET_LOG_OBJECT) block until their event/abort/stop_request here as well.
2. **GCE lists execute synchronously inside the ioctl**; the first `hw->regs`/`wait_irq` error is
   returned by the ioctl. The kernel queues the list and reports failures asynchronously through the
   timeout callback. `WAIT_CALLBACK` afterwards finds `flush_done` already set, which is also what
   the kernel does when the callback has already run; the callback is signalled for a failed list
   too (mtk_vcu.c:806-812, the `vcu_gce_timeout_callback` path). A VENC list powers its core for
   the duration of the synchronous run; the kernel prepares on the first queued job and unprepares
   after the last callback, which is the same thing one job at a time.
3. **Addresses inside a VCU map window but outside every `RWVC_WIN_*` window are skipped and
   counted** (`stats.gce_skipped`, `last_skipped_cmd_addr`); the kernel's CMDQ would write them. The
   model has no back end for the gaps (e.g. 0x16001000, 0x17022000..0x1702ffff).
4. **`mem_obj` fields**: `iova = pa = hardware address` and `va = guest mapping` for both allocation
   calls. The kernel returns `iova` for MVA, `pa` for PA, 0 for the unused one, and a 16-bit
   `CODEC_MSK(vaddr)` in `va`. The vendor code only has to pass the values back, which works.
5. **`VCU_SET_OBJECT` clamps `len` to 80** before calling the handler; the kernel passes a bogus
   length through to the V4L2 handler.
6. **`VCU_SET_MMAP_TYPE` returns 0** (kernel: -1/EPERM). **GET_LOG blocks like the kernel**
   (`wait_event_freezable`); unlike the kernel it can return `-EINTR` from close/destroy/abort,
   which a real file descriptor cannot reach. Both log ioctls clamp to the `_IOC` size (the kernel
   copies a fixed 1024 bytes); SET has no kernel case at all (`struct log_test` does not exist),
   so its "store and raise the flag" behaviour is the model's own, chosen to keep the ABI usable
   from the guest side.
7. **DMEM mmap honours the offset inside the MAP_SHMEM_ALLOC/COMMIT range**; the kernel ignores it.
8. **mmap also accepts a bare `pa`/`iova`** of an allocated buffer as the offset, because that is
   what the vendor's `eVideoMemMAP` passes; the kernel only accepts that right after a
   `VCU_PA_ALLOCATION` (its `map_buf_pa` flag). The model reproduces the flag for the first mmap,
   and falls through to the window/DMEM rules when the flagged PA lookup fails (the kernel returns
   `-EINVAL` there).
9. **No unregister**: `n2a32_mem` has no unmap and `n2vfs` has no unregister, so the MMIO hooks stay
   registered and `n2vcu_destroy()` must be the last thing (documented in the code).
10. `secure != 0` on a GCE flush is executed normally (the kernel only selects a secure CMDQ thread).
    `CMD_SEC_WRITE` is skipped, like the kernel does without `CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT`.
11. A bad guest pointer is not turned into `-EFAULT`: it faults like any guest access and the core
    stops with `N2A32_STOP_FAULT`. The model cannot probe guest pages.
12. `CMD_MEM_MV`/`CMD_POLL_ADDR` accept any address inside an allocated DMA buffer, while the
    kernel's `vcu_check_gce_pa_base()` only accepts the first page of a PA allocation
    (mtk_vcu.c:632-650). The vendor's GCE pages are one page each, so the two agree in practice.
13. Event ids 41..63 have no name in the device tree's event list (`gce_codec_eid` stays -1 for
    them); the kernel would pass -1 to `cmdq_pkt_wfe()`, the model maps them to the VENC line by
    class. The vendor census shows only the named events in use, so this is a path nobody takes.
14. **The bitstream refresh assumes the 0x064 value is the start of its buffer.** For a model
    allocation it copies from that address to the allocation end; for an AP-registered buffer it
    uses V41's `n2vc_dma_lookup_pa()` + `n2vc_dma_sync()`. `n2vc_dma_sync()` (ap/n2vc_dma.c:363)
    only accepts the **base** guest address of a mapping, so a value inside a buffer (offset != 0)
    would fail with `-EINVAL` and silently skip the refresh. Every observed VENC job writes the
    buffer base there (`facts/VENC_PROTOCOL.md`; the prepend path reads from the base), so this is
    not reachable with the current vendor library. If a future build programs an interior address,
    pass the entry's base guest address and the delta as `offset`.
15. **Write combining (DEC04, not yet in the tree) stays OFF by default.** It is a posted-write
    optimisation: a queued store is not visible until the next read/ioctl/flush and a failed batch
    loses its stores (counted in `stats.wc_errors`). The model's default stays immediate like an
    MMIO store; the decode client enables it explicitly (`n2vcu_set_write_combining(v, 1)`), where
    the register volume is. The encode path (~64 ops/frame) does not need it. If hw-lead wants the
    spec's ON default, it is a one-line change plus more of `t_v40.c` adapted; answer posted to
    hw-dec04/hw-lead (18:43).

## Open questions

* `RWVC_OP_POLL` compares `(read & mask) == (value & mask)` (frozen `redwood_vcodec.h`), while the
  MediaTek CMDQ POLL instruction may compare `(read & mask) == value`. Only differs if the vendor
  library passes a value with bits outside `mask` set; the emitted op is what K02's ABI defines.
* Whether `n2vc_dma_guest_map` (no `pa` parameter) is enough for V41's registry; see the board post
  to hw-v41 - the model keeps its own `(pa, guest, bytes)` table so nothing breaks.
* The two computed mmap offsets of `eVideoMemMAP`/`eVideoMMAP` (D01 question 8.6).

`slbc_ready` (root's second first-phone item) is **not a /dev/vcu value**: the vendor library reads
it from `struct venc_vcu_config` field offset 160 inside the guest `venc_vsi` (ap/MESSAGE_FLOW.md
3.1, `venc_common_if.c:468-526`), written by the AP side in `n2ap_venc.c:917`. Every
`struct n2ap_enc_param` on the daemon path is `memset` to 0 first (`vcodecd.c:905-906`,
`n2ap_enc_param_1080p30_h264()`), so `fgSLBCReady` is already 0. No change in this package; posted
to hw-v41/root.

## Coordination (rev 5)

* Posted to `hw-iv`: GET_LOG blocks now (re-measure post of the 805506/5 s loop; my run of IV's
  runner prints `log_gets=0` and a gdb breakpoint on `vcu_get_log_object` is hit once in 5 s).
* Posted to `hw-v41`/`hw-ip`/root: power is now inside `vcu_gce_flush_ioctl` (runner-side
  `hw.power` is redundant but harmless: balanced on/off), `slbc_ready` is the AP side's zeroed
  vsi field above, and `--venc-slow` belongs to the daemon (not a V40 file).

## Files

`gpu/hwcodec/vcu/{n2vcu.c,n2vcu.h,mtk_vcu_abi.h}`,
`gpu/hwcodec/vcu/test_v40/{t_v40.c,layout_probe.c,probe_extract.py,run_tests.sh,compile_ios.sh,STATUS.md}`,
`gpu/hwcodec/dsh-state/workers/V40/STATUS.md`.
