# Track V host runner — progress (package IV)

Owner: hw-iv.  Files: `gpu/hwcodec/host/{n2vhost.c,n2vhost.h,Makefile,run_vhost.sh,PROGRESS.md}`,
`gpu/hwcodec/host/test_iv/**`.  Host only: the hardware back end is T02's fake `n2vc_hw`, the vendor set is
read at run time from `gpu/build/cam-vendor-20261109/vfs` (`$N2HW_VENDOR_DIR` overrides).  No phone, no
publishing, nothing written outside `/tmp/n2hw-iv`.

Everything below was measured on this tree.  "[V]" = the line quoted is in the log named next to it.

## What the runner does

`bash gpu/hwcodec/host/run_vhost.sh` builds (`nice -n 10`, `-j2`, two configurations) and runs `n2vhost`:

1. creates the guest address space (V02) and the `/dev/vcu` + `/dev/vpu` model (V40), registered in V33's
   n2vfs *before* any guest code;
2. registers the shim tables in the order string (49, V30), heap (10, V30), thread (66, V32), stdio (27,
   V33), android (35, V34), data (2, V34) + data-local (2, IV); sets V34's `n2shim_set`/`n2shim_hw`;
3. loads `bin/vpud` and the four libraries, links them (0 unresolved on this tree), prints the unresolved
   list;
4. runs the init arrays;
5. starts vpud's `_start` → `__libc_init` → `main` on its own guest thread;
6. waits `N2VHOST_AP_DELAY_MS` (default 1000 ms), then drives the AP side (V41): `n2ap_venc_open` →
   `n2ap_venc_start` → one SEQ_HDR job → one 320x240 NV12 frame → `n2ap_venc_close`, powering the
   VENC/VENC_C1 blocks around each job (the model of `venc_encode_prepare`/`mtk_vcodec_enc_clock_on`)
   and registering the two DMA buffers as V34 ion shares so the daemon's ring buffer can `ion_import`
   them (`n2ap_venc_encode_fd`, fd 0x500/0x501);
7. joins the guest with a bound, then prints the report whose last line is
   `N2VHOST reached M-x: ...`.  If the AP drive wrote a bitstream, `run_vhost.sh` also runs T02's
   `tests/check/h264_check.py` over it.

Accessors for the daemon (package IP): `n2vhost_start`, `n2vhost_vcu`, `n2vhost_hw`, `n2vhost_mem`,
`n2vhost_elf`, `n2vhost_milestone`, `n2vhost_milestone_evidence`, `n2vhost_stop_text`, `n2vhost_stop_reason`,
`n2vhost_error`, `n2vhost_report`, `n2vhost_join`, `n2vhost_stop`.
**One guest set per process** (V33's n2vfs has no unregister); `n2vhost_stop()` parks the address space,
the `/dev/vcu` model and the linked set in a global root until exit.  A second start is refused with an
error that names the limitation.

## Milestones — all six reached on this tree [V]

    N2VHOST M-a loaded 5 objects, linked, 0 unresolved imports, 0 called so far
    N2VHOST M-b init arrays ran over 5 objects (0 init functions: ...)
    N2VHOST M-c first ioctl on /dev/vcu: set_object=8 get_object=6 allocations=9 gce_flushes=1 (n2vcu_stats)
    N2VHOST M-d AP_IPIMSG_ENC_INIT (0xc000) answered with VCU_IPIMSG_ENC_INIT_DONE (0xd000): n2ap_venc_open returned 0, last_status=0
    N2VHOST M-e first VENC window access: write VENC 0x00a8 0x00000000
    N2VHOST M-f wait_irq(RWVC_IRQ_VENC, timeout_ms=1000) reached
    N2VHOST hw power_on_venc=3 venc_ops=64 irq_waits=1 venc_waits=1 regs_calls=14
    N2VHOST reached M-f: wait_irq(RWVC_IRQ_VENC, timeout_ms=1000) reached
    run_vhost.sh: .../bitstream-320x240.h264 validated (H.264 SPS/geometry/decode/PSNR, 115841 bytes)

The AP drive now produces a **valid H.264 IDR** on the fake VENC (opus-venc's model on top of T02's
fake): seq-hdr 29 bytes, frame 115812 bytes, EOS flush, fake-venc `frames=1 headers=1 irqs=1/1`;
T02's checker decodes it at 320x240 Baseline and the PSNR passes.  M-c is evidence from V40's counters
(`n2vcu_stats`), not a parsed log.  M-e/M-f are recorded by the `n2vc_hw` wrapper: the first non-delay
op on `RWVC_WIN_VENC` and the first `wait_irq(RWVC_IRQ_VENC)`.  `venc_ops=64` is the daemon's register
sequence through the GCE; `regs_calls=14`.

### What it took since the previous handoff (M-b → M-f)

* **Guest `/proc/self/maps`**: V33's `n2vfs_set_maps`, filled from the loader's real PT_LOAD bases.  The
  text now has bionic's shape: page-aligned ranges, and the GNU_RELRO page of a writable segment is
  `r--p` (not `rw-p`).  Without the RELRO split vpud's `remap_shmem()` picked the 0x3f0-byte
  `.data.rel.ro` line, copied that into the DMEM and reported a codec-instance address outside every
  mapping.  With it, vpud logs `shmem_end 40114000 shmem_start 4009e000 size 483328` (the real data+bss
  segment) and maps the DMEM over exactly that range, which is the vendor's design.
* **V40 COMMIT VMA** (carry patch, see below): `vcu_mapping_dm_addr()` translates against
  `VCU_DMEM0_VMA`, the COMMIT mmap's `vma->vm_start` (`mtk_vcu.c:1199-1225`, `:1707-1711`), not against
  the DMEM's guest allocation.  The model now records the requested COMMIT address and translates
  through it; `n2vc_dma_guest_map`'s VMA is where vpud put its static codec tables
  (`venc_drv_inst` 0x4010ceb0), so V41's `n2ap_venc_open` accepts INIT_DONE.
* **V41 encode path** (carry patch): `n2ap_venc_encode_fd()` puts the two ion share ids in
  `venc_ap_ipi_msg_enc.input_fd[0]`/`bs_fd`; `n2ap_venc_wait_done()` waits for the asynchronous
  `PUT_BUFFER`/`ENCODE_DONE` result; `n2vc_dma_guest_unmap()` no longer clears an AP-owned entry when
  the daemon closes its ion view.
* **V34 ion lifetime** (carry patch): a share registered by the AP through
  `n2shim_ion_register_share()` survives the daemon's `ion_free`/`ion_share_close`, so every job can
  import it again.
* **AP power**: the runner powers VENC/VENC_C1 before each job (`venc_power()`), the model of the
  kernel's `venc_encode_prepare()` → `mtk_vcodec_enc_clock_on()` (`venc_drv_if.c:109-124`).  This daemon
  never sends `VCU_IPIMSG_ENC_POWER_ON`; without the power the fake hardware rejects the first VENC
  write with EACCES and the whole sequence stops.
* **R2 F15**: the weak empty shim-table stand-ins are gone.  `n2vhost.o` references
  `n2shim_thread/stdio/android/data_all` strongly, so a product link that forgets an archive member
  fails at link time instead of carrying empty tables.  `test_iv` checks the symbols and the counts.
* **Trace off in product mode** (opus-perf): `guest_main()` attaches T01's trace only when
  `N2HW_TRACE` is set.  Without it `cpu->trace == NULL`, so `emu/n2a32_core.c:626` takes the fast
  interpreter loop; the daemon (vcodecd) no longer formats a record per instruction.  The report says
  which path ran, and `test_iv` runs both.
* **Fake VENC** (opus-venc): `fakevenc/n2venc_fake.c` is built into the runner and wraps T02's fake
  (`n2venc_fake_wrap`); the wrapper sits between V40/V41 and T02's register/recorder model, so a
  trigger writes a synthetic IDR slice, raises the done interrupt and reports `BYTE_CNT`.  The runner
  prints the model's counters (`fake-venc ... frames=1 headers=1 ...`).  `run_vhost.sh` still is the
  one command; `fakevenc/run_session.sh` is opus-venc's copy-tree harness and passes on this tree
  (`vcodecd`-style copy + apply_patches reports ALREADY for every block).
* **V34 cache sync** (owner + carry): `ION_SYS_CACHE_SYNC` is a copy between the guest mirror and the
  device copy (clean/flush to device, invalidate from device).  Without it the daemon's CPU-written
  SPS/PPS were wiped by the AP's from_device sync and the checker found no SPS.
* **V41 encode completion** (owner + carry): drain `venc_vsi.list_free` after every encode (the
  kernel's `mtk_enc_put_buf`), allow `FRAME_FINAL` without a frame (the EOS flush), and sync the
  bitstream from the device before returning a size.

## Carry patches outside gpu/hwcodec/host (owners inactive or integrating the same text)

These are minimal, commented at the edit, backward compatible, and each has a board post as the record
(IV -> V40, IV -> V41, IV -> V34, IV -> root, 11 November).  Revert them if the owners return with a
different design; nothing else in their files changed.

| File | Change | Why |
|---|---|---|
| `gpu/hwcodec/vcu/n2vcu.c` | `commit_vma`/`commit_seen`; `vcu_dev_mmap` records `n2shim_arg(c,0)` for the COMMIT range; `n2vcu_dmem()`/`n2vcu_map_dm_addr()` translate against it | `VCU_DMEM0_VMA` is the COMMIT VMA (`mtk_vcu.c:1707-1711`); vpud's `vcu_inst_addr` is in its own RW data segment |
| `gpu/hwcodec/ap/n2ap_venc.c`, `.h` | `n2ap_venc_encode_fd()` (fd fields), `n2ap_venc_wait_done()` (async result), `n2ap_venc_power()`; V41 rev 5 now owns these | vendor ring buffer `ion_import`s the fds; done arrives after the ACK (`venc_vcu_if.c:135-173`) |
| `gpu/hwcodec/ap/n2vc_dma.c` | `ap_owned` flag: `guest_unmap` keeps an AP-registered entry; `guest_forget` releases it | the daemon's `ion_munmap` after each job is a view change, not the AP releasing the buffer |
| `gpu/hwcodec/ap/test_v41/t_v41.c` | three checks updated to the new unmap/forget contract | the suite is V41's oracle; the old checks asserted the defect |
| `gpu/hwcodec/ap/test_v41/{tsan.supp,run_tests.sh}` | suppress the guest-shared `list_free` ring pair in the fake-daemon TSan run | the model has two host threads on guest shared memory; the protocol, not a host lock, orders the real ring |
| `gpu/hwcodec/shim/n2shim_android.c` | `foreign` shares: `ion_free`/`ion_share_close` are no-ops for them | importer lifetime vs exporter lifetime, second encode failed otherwise |
| `gpu/hwcodec/shim/n2shim_android.c` | `ION_SYS_CACHE_SYNC` copies guest<->device (owner V34 applied the same text) | the daemon's CPU-written SPS/PPS must reach the device copy; the checker found no SPS otherwise |
| `gpu/hwcodec/ap/n2ap_venc.c` | post-encode `ven_put_buffer`, EOS `FRAME_FINAL` without a frame, from_device sync in `wait_done`, `n2ap_venc_power` (owner V41 has the same text) | header size only in `list_free`; async frame size arrives later; the AP powers the block around the job |
| `gpu/hwcodec/shim/n2shim_format.c`, `shim/test_v30/t_format.c`, `shim/test_v34/t_unit.c` | `n2fmt_args_va` takes the guest va_list argument area directly (no extra load); six tests updated to the same model | ARM EABI passes `va_list` as a by-value struct; the decode path faulted at 0x63656476 (opus-dec1 HIGH) |

## Stops reported to owners (all fixed or carried)

| # | Owner | Where | Status |
|---|---|---|---|
| 1 | hw-v14 | T16 CBZ/CBNZ with `i`=1 UNDEF | fixed in tree (R1 patch), M-c unblocked |
| 2 | hw-v33 | `fopen("/proc/self/maps")` opened the host file | fixed (fopen host paths removed) |
| 3 | hw-v33 | `/proc/self/maps` content | fixed (`n2vfs_set_maps` + IV's bionic-shaped text) |
| 4 | hw-v40 | COMMIT VMA/DMEM0_VMA | carry patch applied, board post |
| 5 | hw-v41 | fds, async done, AP power, `guest_unmap` | carry patch applied (power is runner-side), board post |
| 6 | hw-v34 | foreign share lifetime | carry patch applied, board post |
| 7 | hw-ip | `build_ios.sh` extracts the archive before building it on the first run; then the known three missing `sdk_symbols.txt` lines | reported to hw-ip; the second run reaches the three SDK symbols only |
| 8 | hw-v02 | nested stop report mixes pc/insn | still open (cosmetic; no longer hit) |

## Commands and their last lines

    bash gpu/hwcodec/host/run_vhost.sh
    N2VHOST reached M-f: wait_irq(RWVC_IRQ_VENC, timeout_ms=1000) reached
    (stderr) .../bitstream-320x240.h264 validated (H.264 SPS/geometry/decode/PSNR, 115841 bytes)

    bash gpu/hwcodec/host/test_iv/run_tests.sh
    N2TEST IV PASS 80/80          (includes a TSan build + short run of the threaded runner)

    bash gpu/hwcodec/fakevenc/run_session.sh
    run_vhost.sh: .../bitstream-320x240.h264 validated (H.264 SPS/geometry/decode/PSNR, 115841 bytes)

    make -C gpu/hwcodec/host ios OUT=/tmp/n2hw-iv
    N2VHOST ios: 33 objects compiled for arm64-apple-ios16.0, 0 failed (compile check only)
    make -C gpu/hwcodec/host ios-lib OUT=/tmp/n2hw-iv
    N2VHOST ios-lib: /tmp/n2hw-iv/ios/libn2vhost.a (33 objects, arm64-apple-ios16.0)

Affected packages' own suites after the carry patches:

    bash gpu/hwcodec/vcu/test_v40/run_tests.sh          -> N2TEST V40 PASS 1437/1437
    bash gpu/hwcodec/ap/test_v41/run_tests.sh           -> N2TEST V41 PASS 630/630
    bash gpu/hwcodec/shim/test_v34/run_tests.sh         -> N2TEST V34 PASS 2044/2044 (+ V34CORE 103/103)
    bash gpu/hwcodec/fakevenc/run_tests.sh              -> N2TEST fakevenc PASS 3/3
    bash gpu/hwcodec/vcu/test_v40/compile_ios.sh        -> N2TEST V40-IOS PASS 1/1

`bash gpu/hwcodec/build_ios.sh` (package IP's product link) now succeeds with the real shim tables:
`vcodecd: linked` (391536 bytes, V32/V33 table symbols present, no fake hardware).  The first run after
a cold `/tmp` once failed the stdio check while IP's script was still rebuilding the archive; IP fixed
the order, a rerun passes.  V34 adopted the foreign-share fix as its committed rev 2 (2044 checks);
V40/V41 carry patches are still ours to hand back if those owners return.

## Known limitations / open questions

* The AP drive sends one H.264 320x240 session (seq-hdr + one IDR + EOS); no HEVC, no P frames, no
  decode, no rate measurement.  opus-venc's model is I_PCM/CAVLC/4:2:0 only; a real stream still needs
  the phone.
* `n2vhost` refuses a second set per process (n2vfs has no unregister).  The daemon starts once.
* `n2vhost_stop()` parks the guest set (reachable globals) instead of freeing it.
* If the guest is stuck inside a host shim at stop time, the thread is detached and the process exits.
* `n2ap_venc_wait_done()` accepts the newest nonzero free-buffer entry if the daemon's cookie differs
  from `vsi->venc.venc_bs_va`; the kernel's `venc_get_free_buffers()` also consumes the ring without a
  cookie check.  Do not use it to assert sizes.
* The carry patches are now mostly the owners' own code: V34 rev 2 adopted the foreign-share and
  cache-sync text, V41 rev 5 owns encode_fd/wait_done/power/list_free/EOS, V40 rev 5 owns the GCE power
  and GET_LOG blocking.  Only the V40 COMMIT-VMA translation and the shim cache-sync may still be
  IV-carried; re-check on their next rev.
* The TSan run suppresses exactly one pair: the guest-shared `venc_vsi.list_free` ring (the daemon's
  guest stores vs the AP's read).  `host/test_iv/tsan.supp` and `ap/test_v41/tsan.supp` explain why it
  cannot be locked in the model.  Everything else must be race-free.
* The iOS target is a compile check only; `build_ios.sh`'s product link now succeeds with the IV
  archive (`vcodecd: linked`, 391536 bytes).
