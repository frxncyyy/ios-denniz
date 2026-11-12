# VDEC decode path: the AP side (`ap/n2ap_vdec.c`), what it speaks and what it owns

Owner: DEC01. Host-verified only; **nothing here has run on the phone**. Every statement is [V] verified
on the PC today (test line, kernel file:line or vendor symbol given) or [I] inferred. Paths: `K` =
`nord2-current-kernel`, `KV` = `K/drivers/media/platform/mtk-vcodec`, `VFS` = the extracted
`gpu/build/cam-vendor-20261109/vfs`, `L` = `VFS/lib`. No vendor code is reproduced here: only symbol
names, addresses and constants.

Companion files: `facts/VDEC_PROTOCOL.md` (opus-vdec, the protocol study), `ap/n2ap_vdec.h` (the API),
`ap/test_dec01/` (tests, fake daemon, real-vendor probe, vector runner), `kern/VCODEC_HW.md` (the
kernel/register side). The encoder twin is `ap/n2ap_venc.c` + `ap/MESSAGE_FLOW.md` (V41); this client
copies its send ordering (ACK first, then `ipi_done`) and its no-print rule.

## 1. Where this sits

```
 VideoToolbox 'avc1'/'hvc1' decoder (DEC06)         <- outside this package
        |  daemon protocol (include/n2vcd_proto.h, DEC05)
 n2vcodecd (one process, interpreted vendor library)
        |  AP side: THIS FILE (n2ap_vdec.c)  <- replaces KV/vdec/vdec_common_if.c + vdec_vcu_if.c
        |  /dev/vcu model (V40): n2vcu_ap_send / n2vcu_ap_register / n2vcu_map_dm_addr
        |  hardware back end (frozen include/n2vc_hw.h): power, regs, wait_irq, dma
 MT6893 VDEC 0x16000000 (LAT + CORE)               <- real block on the phone, T02 fake on the PC
```

The AP client owns exactly what the Linux V4L2 decoder driver owns for one instance:

| Kernel file | What it does | Here |
|---|---|---|
| `KV/vdec/vdec_common_if.c` | INIT/START/RESET/DEINIT, vsi field hand-over | `n2ap_vdec_open/decode/flush/close` |
| `KV/vdec_vcu_if.c` | IPI send/ack, VCU->AP requests, vsi mapping, cap query | `dec_send`, `dec_dispatch`, `n2ap_vdec_query_cap` |
| `KV/mt6885/mtk_vcodec_dec_pm.c` | clocks, racing words, HW break | `dec_lock`, `dec_unlock`, `dec_racing`, `dec_hw_break_*` |
| `KV/mt6885/mtk_vcodec_intr.c` | the done-interrupt wait | `VCU_IPIMSG_DEC_WAITISR` handler |
| `KV/mtk_vcodec_dec.c` (fb queues) | ready list, display/free harvest | `free_q`, `in_use`, `out_q`, `freed_q`, `dec_harvest` |

The vendor library (`libvpud_vcodec.so` + `libvcodecdrv.so`) stays in the daemon and does the codec work.
It is never linked here; `ap/test_dec01/probe_dec01.c` runs it under the A32 interpreter on T02's fake.

## 2. Message sequence [V]

One H.264/H.265 instance, ids from `mtk_vdec_ipi.h` (= `KV/vdec_ipi_msg.h`):

| Step | AP message (id) | on IPI | Kernel reference | Host proof |
|---|---|---|---|---|
| cap | `AP_IPIMSG_DEC_QUERY_CAP` 0xA006 | `IPI_VDEC_COMMON` (1) | `vdec_vcu_if.c:594-625` | probe: H264/H265/HEIF input, NV12/MM21/MT21 output (facts 2.4) |
| open | `AP_IPIMSG_DEC_INIT` 0xA000 | `IPI_VDEC_H264`=2 / `H265`=3 / `HEIF`=4 | `:511-544` | INIT_ACK carries `vcu_inst_addr` = the vsi |
| params | `AP_IPIMSG_DEC_SET_PARAM` 0xA005 FRAME_SIZE, DECODE_MODE, ... | same | `:627-651` | FRAME_SIZE is always sent, 0x0 included |
| header | `AP_IPIMSG_DEC_START` 0xA001, `fb_handle == 0`, index 0xFF | same | `vdec_common_if.c:217-260` | status 0, `changed & RES_CHANGE`, pic/crop/dpb filled |
| frame | `AP_IPIMSG_DEC_START` with one output buffer | same | same | LOCK_LAT + LOCK_CORE + 2 WAITISR + UNLOCKs |
| flush | `AP_IPIMSG_DEC_RESET` 0xA004 | same | `:243` (`bs == NULL`) | RESET_ACK then ring harvest |
| close | `AP_IPIMSG_DEC_DEINIT` 0xA003 | same | `:582` | DEINIT_ACK, power released |

Replies: `VCU_IPIMSG_DEC_*_ACK` 0xB000-0xB006. A reply is accepted only when its `ap_inst_addr`
(+8) equals the session cookie (`vdec_vcu_if.c:221-225`); a mismatch is counted as a protocol error
and produces no ACK. Sending follows `ap/n2ap_venc.c` rev 3: wait for the ACK, then for
`n2vcu_ap_wait_dispatched()` (the daemon re-entered `VCU_GET_OBJECT`), both bounded by the session
timeout (6000 ms, `IPI_TIMEOUT_MS` of `KV/mtk_vcu.c:72`). A timeout marks the session aborted; later
calls return `-EIO` (`vdec_vcu_if.c:416-417`).

### 2.1 vsi hand-over and the 64-entry trap [V]

The daemon allocates `struct vdec_vsi`; the client maps it with `n2vcu_map_dm_addr(inst_addr)` and
addresses everything after the rings through `struct vdec_vsi_tail` at
`8*n + 16 + 2*(48*n + 16)`. For this kernel's `VIDEO_MAX_FRAME = 64` that is offset **0x1a30**
(`dec`), `dec.bs_fd` **0x1a40**, `dec.vdec_bs_va` **0x1ac8**, `pic` **0x1af8**, `crop` **0x1b78**;
`mtk_vdec_ipi.h` pins them with `_Static_assert`. A client built against a 32-entry header writes every
`dec` field 0xd00 bytes too low and START fails exactly like DEC02's first probe.

The open checks that the whole tail (`vsi_dec_offset(n) + sizeof(tail)`) is inside the DMEM window and
stores the window; `n2ap_vdec_set_ring_entries()` re-checks it before rebinding.

## 3. The first blocker: `get_ringbuf input handle NULL` [V]

Before DEC01 the vendor answered `AP_IPIMSG_DEC_START` with status -1 and
`[ERROR] get_ringbuf input handle NULL !!` (`libvpud_vcodec.so` `get_ringbuf` @0x8c70, NULL test
@0x8c76). It is not an ion-shim defect and needs **no V34 change**:

- The START branch (`vdec_codec_msg_handler` @0xb6dc, START @0xb7a8) calls
  `get_ringbuf(inst, _, [inst+0x1ac8], [inst+0x1acc])`, i.e. the two halves of
  **`vsi.dec.vdec_bs_va`**, and rejects a zero first word. The field is the client's bitstream
  **cookie**, not an fd: it must be non-zero on every START (a DMA handle works and comes back in
  `list_free_bs`).
- The ion import in the same branch uses `vsi.dec.bs_fd` (0x1a40), the V34 share id registered with
  `n2shim_ion_register_share(id, handle, pa, bytes)`. Our client passes `job.bs_fd` verbatim.
- `vsi.general_buf_fd` must be **-1** on every START (including the header, no-fb one): 0 imports fd 0
  ("ion_import failed ... general buffer"). `general_buf_dma/size` are 0.
- The second half of `vdec_bs_va` (`[0x1acc]`) is the high 32 bits; keeping the cookie 32-bit avoids
  any ambiguity in the 32-bit daemon.

With those, the probe's header and frame STARTs are accepted: `changed=0x23`, no ringbuf log line, no
register access for the header, 144 reads/448 writes for the fake frame path. The signed V34 share-id
table already does the right thing; no patch was needed and none was posted.

## 4. START field layout [V]

`n2ap_vdec_decode()` writes the same fields in the same order as `vdec_common_if.c:243-290`:

| vsi field | value |
|---|---|
| `dec.vdec_bs_va` | bitstream cookie = `job.bs_handle` (non-zero) |
| `dec.bs_dma` | PA of the bitstream (`n2vc_dma_lookup_handle`) |
| `dec.bs_fd` | V34 share id (`job.bs_fd`) |
| `dec.vdec_fb_va` / `index` | 0 / 0xFF for a header; else the fb cookie / `job.fb_index` |
| `dec.fb_num_planes` | job planes (0 = session default: 2 for H.264/H.265, 1 for HEIF) |
| `dec.fb_dma[0..]`, `fb_fd[0..]` | plane 0, then plane 1 when the format is planar; all others 0 |
| `dec.timestamp` | `job.timestamp` |
| `general_buf_fd/dma/size` | -1 / 0 / 0 |

Message `data[]` = `{length, size, flags}` — the kernel's field names are the opposite of their
meaning (`vdec_common_if.c:267-269` sends `bs->size`/`bs->length`; `mtk_vcodec_dec.c:670-671` sets
`bs->size = planes[0].bytesused` and `bs->length = planes[0].length`). So **data[0] is the access-unit
length and data[1] is the mapped bitstream-buffer size**; with the old order HEVC logs
`flush size ... out of range` and answers NEED_MORE_OUTPUT_BUF every other START (opus-hevcdec's
finding). Both words are bounds-checked against the registered allocation and a violation is
`-EINVAL` before any message.

### 4.1 Two output planes (MM21) [V]

The vendor reports `pic.fb_sz[0]` (Y) and `pic.fb_sz[1]` (C) and programs `fb_dma[1]`/`fb_fd[1]` as the
chroma target. Three legal forms in this API:

1. **two allocations (what V4L2 does)**: `fb_c_handle` + `fb_c_fd`; the chroma PA is looked up and the
   sizes are checked against `fb_sz[0]`/`fb_sz[1]`;
2. one allocation: `fb_c_handle == fb_handle`; chroma PA = `fb_pa + fb_sz[0]`, same share id, and the
   whole allocation must be at least `fb_sz[0] + fb_sz[1]`. The same-alloc display harvest copies the
   whole buffer once. Both forms decode and display every frame in the vector run (MM21, 320x240 and
   1920x1080); DEC05's single-allocation choice is fine.
3. single plane: `fb_planes = 1` (NV12/HEIF); the chroma words are written as 0.

`fb_planes = 2` with neither form is refused (-EINVAL) when `fb_sz[1] != 0`: programming a zero DMA
target is a write to physical address 0. `GET_FRAME_BUFFER` applies the same rule to the queued
buffers, and answers "no buffer" (`index 0xFF`, `vdec_fb_va 0`) if a handle is not in the DMA registry.

Cookies: `vdec_bs_va` and `vdec_fb_va` are opaque 64-bit tokens that must be **non-zero and distinct
per queued buffer**; the kernel passes V4L2/kernel pointers, so a client should pass a large value
(`dec1/vdec_ref.c` uses `0xB0000000+i` and `0xF0000000+i`; the probe does the same through
`job.bs_cookie`/`job.fb_cookie`). DMA handles are small integers and are only the fallback.

**Share-id namespace**: the probe's first multi-AU run published the bitstream share ids at
`0x501..0x504` and lost frames with `ion_import failed ... Y framebuf 0x102`; V34's shim allocates the
daemon's *own* shares starting at `0x400` and rising (`n2shim_android.c` `N2AND_ION_SHARE0`), so an AP
id in that path can collide with a daemon-internal share. Moving the AP ids to `0x700+` (bitstream) and
`0x800+` (frame planes), the convention of the reference client, gives 12/12 displayed frames.
DEC05/V34 must keep AP-published share ids out of the daemon's allocation path.

### 4.2 Output format [V]

The fourcc the daemon writes into `pic.fourcc` is the contract the detile stage needs:

| value | name | when (measured on the fake) |
|---|---|---|
| `0x31324d4d` | MM21 | `vendor.mtk.vdec.frame.layout.mode = "0"` (now landed; V34): both buffer forms tested (320x240, 1920x1080); what `ap/mm21` detiles |
| `0x3132544d` | MT21 | the same streams with the property unset (>= 0x1fe000 pixels): UFO-compressed |
| `0x5331324d` | M21S | **1-plane** session (`fb_num_planes = 1`), small pictures |

So the reviewer's M21S observation was caused by the probe's old `fb_num_planes = 1`, and the old 1080p
MT21 was the property, exactly as opus-dec1 said; the allocation form does not change the fourcc (both
forms give MM21 with the property at "0"). A client that will detile MM21 can pin the contract:
`cfg.output_fourcc = N2AP_VDEC_FOURCC_MM21` makes a header START with RES_CHANGE and a different
fourcc return `-ENOTSUP` (trace event `fourcc`). The probe's default run uses this (`--require-mm21`);
the vector runner leaves it optional so a deviation is visible in its output.

## 5. The decoder power role: LOCK/UNLOCK, racing words, HW break [V on the model]

The daemon sends these synchronously during START; the handler answers on the daemon's thread, like
`vcu_dec_ipi_handler` (`vdec_vcu_if.c:185-407`).

| request | kernel action | here |
|---|---|---|
| `LOCK_LAT` 0xC005 | `vdec_decode_prepare(LAT)`: power VDE (SOC), clocks | `dec_lock(core=0)`: SOC block on |
| `LOCK_CORE` 0xC003 | same for CORE: VDE2 + parent VDE | `dec_lock(core=1)`: SOC on (held for the active period) + CORE on, racing restore for the words this session saved |
| `UNLOCK_LAT` 0xC006 | `vdec_decode_unprepare(LAT)`: HW break LAT, clocks off | `dec_unlock(core=0)`: HW break LAT, SOC dropped when it was the last user |
| `UNLOCK_CORE` 0xC004 | same for CORE | `dec_unlock(core=1)`: last-CORE racing save, HW break CORE, CORE off |

Details, all from `mt6885/mtk_vcodec_dec_pm.c`:

- The racing words are 68 u32 at `VDEC_RACING_CTRL + 0x100` (`MTK_VDEC_RACING_INFO_SIZE`,
  `mt6885/mtk_vcodec_pm.h:16`): the kernel restores them when the shared active count goes 0 -> 1
  (`:199-213`) and saves them on 1 -> 0 (`:269-280`), in both the CORE and the LAT branch.
- **Phone correction (opus-decint, vc19; DEC02 trace):** the kernel's `VDEC_RACING` window
  (`0x16004000`) and `RWVC_WIN_VDEC_ALL` page 0x04 belong to **VDEC_CORE**, so a restore at a
  LAT-only clock-on is refused with EACCES (DEC02 measured one per AU, because the vendor locks LAT
  first). This client therefore does exactly what the hardware allows: it powers SOC at the first
  lock whatever the core, defers the restore until the CORE lock has powered the core (before the
  vendor's first core register access, which the LOCK_CORE ack gates), restores only words this
  session actually saved (`racing_valid`; before the first save writing the zeroed copy could only
  harm the core's reset values), and saves on the last CORE unlock, before the core goes off. A
  LAT-only period after the core is already off has nothing left to save. The host test's power
  model expects the same (t_dec01 `pwr`, `soc`, `soc_lat` cases).
- **Every UNLOCK** runs the HW-break sequence for the core it releases, while that block is still on
  (`:282`, `:296-460`):
  - CORE: if `VDEC_SYS` != 0, set `MISC+0x100` bit 0, poll `MISC+0x104` for 0x11 (plus UFO
    `BASE+0x800+0x8c` bits 0x11000 when UFO is on, `+0x1c` bit 1 toggled), then `VLD+0x108` = 1, 0;
  - LAT: if `SOC_GCON+0x200` != 0, set `LAT_MISC+0x100` bit 0, poll `LAT_MISC+0x104` bits 0 and 4,
    then `LAT_VLD+0x108` = 1, 0.
  Every poll is bounded (the kernel's 20 ms/1 s budget); a timeout is counted, not fatal.
- `WAITISR` 0xC000: entry `status` = core id (0 CORE, 1 LAT); waits 500 ms
  (`WAIT_INTR_TIMEOUT_MS`, `mtk_vcodec_drv.h:35`) on `RWVC_IRQ_VDEC` / `RWVC_IRQ_VDEC_LAT`; the
  result is written back exactly as `mtk_vcodec_wait_for_done_ctx` returns it: **0, -1 (timeout,
  `mt6885/mtk_vcodec_intr.c:30`) or -2 (interrupted, :36)**. A successful wait also acks the interrupt
  (`hw->irq_clear`). T02 raises no VDEC IRQ, so the host probe sees -1 twice per frame; that is a fake
  limitation, not a client timeout (the reference client's `VR_FAKEIRQ` switch does the same).
- `PUT_FRAME_BUFFER` 0xC002 harvests the three rings (section 6).
- `GET_FRAME_BUFFER` 0xC001: pops one queued buffer and fills `vsi.dec`; blocks up to 1 s
  (`vdec_vcu_if.c:292`) unless the client is flushing, then answers `index 0xFF`.

## 6. Frame-buffer lifecycle and cache maintenance [V]

```
client: push_fb(handle, c_handle/fd, bytes, index)         -> free_q
daemon: GET_FRAME_BUFFER                                   -> in_use (cookie = job/fb handle)
daemon inside START: list_disp (display order, poc, ts)    -> out_q  + from_device sync
daemon inside START: list_free (reusable)                  -> freed_q
AP after every ACK/RESET: harvest (no PUT needed in sync mode)
```

- The vendor **never sends PUT_FRAME_BUFFER in synchronous mode** (one buffer in `vsi.dec` per START):
  measured 0 requests over 12 AUs by opus-dec1. The kernel harvests in its worker after
  `vdec_if_decode()`; this client does the same after every acknowledged START and RESET.
- `list_free_bs` returns one bitstream cookie per START. The ring has 64 entries; not draining it
  wedges the session after 64 AUs. It is drained in the same harvest and exposed through
  `n2ap_vdec_pop_free_bs()`.
- **B-frames**: `list_free` can arrive before `list_disp` for the same cookie. The in-use record has
  `in_disp`/`in_free` flags and is dropped only when both are set; a buffer is not re-pushable before
  that (`push_fb` returns `-EBUSY`). The two-phase case is covered by `t_dec01` case `free2`.
- `dec_harvest` records the plane copies under the mutex and performs them after unlocking: a 1080p
  MM21 frame is ~3 MB and no client-queue access should wait for it.
- Sync rules: bitstream **to_device** before START; every output plane **from_device** when it appears
  in `list_disp`. Same-allocation two-plane buffers are copied once (whole allocation);
  separate planes are copied one each. The bitstream is *not* copied back after START and the output
  buffer is not copied to the device before it: only the direction the hardware needs.
- No `general_buf` and no secure path.

Limits: `N2AP_VDEC_FB_QUEUE`/`OUT_QUEUE`/`FREE_QUEUE` are 64, one per vendor ring slot, so no cookie is
dropped.

## 7. Threading and lifetime [V]

- The daemon's requests arrive on its own thread (n2vcu hands them to the registered handler). The
  client's reply/queues/power state is protected by one mutex; `proto_errors` is atomic; `stats` is
  read after the session is quiescent.
- `close()` must not free the session while a handler invocation is still inside it: the sender is
  woken from *within* the handler, so it can run ahead of the handler's return. Every session is in a
  small `dec_live` table; the handler dereferences its `ctx` only if it finds it there, and close
  removes the entry and waits for the in-flight calls before freeing. A late dispatch with a stale
  pointer is a no-op. Tested by `t_dec01` case `race` (20 ms of LOCK/UNLOCK hammering while the session
  closes, plus one direct call with the freed pointer) under ASan/UBSan and TSan.
- No printing from library code: the trace hook carries `"lock"`, `"unlock"`, `"power"`,
  `"racing_restore"`, `"racing_save"`, `"hw_break"`, `"wait_isr"`, `"get_fb"`, `"put_fb"`,
  `"decode"`, `"decode_done"`, `"fourcc"`, … .

## 8. Running it on the PC (the runner hook)

Everything builds into `/tmp/n2hw-DEC01`; nothing is written into the tree.

```
bash gpu/hwcodec/ap/test_dec01/run_tests.sh        # unit + layout + iOS compile + real vendor + vectors
bash gpu/hwcodec/ap/test_dec01/decode_vectors.sh   # only the ffmpeg vectors
```

The real-vendor probe (`test_dec01/probe_dec01.c`) starts `vpud` under the A32 interpreter on T02's
fake hardware, plays the AP role through this library, and prints one milestone line per step:

| milestone | meaning |
|---|---|
| VD-a | QUERY_CAP answered: H264/H265 + NV12 in the lists |
| VD-b | INIT answered, vsi inside the DMEM, FRAME_SIZE/DECODE_MODE accepted |
| VD-c | DEC_START accepted (no `get_ringbuf` error) |
| VD-d | first VDEC register access: LOCK_LAT/LOCK_CORE reached the power model |
| VD-e | first WAITISR: the daemon waited for a decoder interrupt |

Last DEC01 run (this is the evidence, not a plan):

```
N2TEST DEC01 PASS 999/999
N2VDEC reached VD-b ... vsi 0x4009ed88 inside dmem 0x4009e000+524288
N2VDEC reached VD-c: DEC_START accepted, changed=0x23 (ringbuf log lines seen: 0)
N2VDEC reached VD-d: first VDEC access - LOCK_LAT ...
N2VDEC reached VD-e: first WAITISR ...
N2VDEC frames: in=1 displayed=1          (the default one-AU stream)
N2VDEC vectors: 4/6 reached VD-d or VD-e, 2 stopped at VD-c (named need), 0 failed
  t320bf0  12 AUs -> 12 displayed
  t320bf2  12 AUs -> 12 displayed
  t1080bf0  4 AUs -> 4 displayed
  t1080bf2  4 AUs -> 4 displayed
```

The probe cycles four bitstream buffers and a pool of two-plane output buffers the way the kernel's
V4L2 worker does (one buffer in `vsi.dec` per START, harvest after each ACK, flush at EOS). With the
fake IRQ switch the vendor produces every display: 12/12 for both 320x240 streams and 4/4 for both
1080p streams. The vectors are generated at test time with ffmpeg (testsrc, yuv420p, libx264 Main
`-bf 0` and `-bf 2` at 320x240 and 1920x1080, libx265 at both sizes). The HEVC vectors stop at VD-c and
name the first unmet need: T02's fake has no HEVC fetch-ok model (`LAT_VLD +0xe8`), so the vendor's
frame path polls forever there (DEC03 has the model, not wired into the fake). That is a fake
limitation: the client code is the same for both codecs and the HEVC header START is accepted, with
MM21 and the same fb sizes as H.264.

Header facts from the vectors: 320x240 -> pic 320x240, buf 320x256, dpb_sz 9/10 (bf0/bf2), fb_sz
81920/40960, fourcc MM21; 1920x1080 -> pic 1920x1088, buf 1920x1088, dpb_sz 8, fb_sz
2088960/1044480, fourcc MM21 with two plane buffers (MT21 if both planes share one allocation).

## 9. Not done / open

- **Never run on the phone.** The whole file is host-tested; K02's `/dev/redwood-vcodec` and P03's
  back end are the phone path.
- **HEVC frames** need DEC03's VLD model wired into T02's fake (or the phone). Header START is proven.
- **Output format**: MM21 at every size tested for both buffer forms with V34's landed
  `layout.mode = "0"`. If DEC05 wants the contract enforced, set `cfg.output_fourcc = MM21`; until the
  property reached the tree, 1080p was UFO `MT21`.
- **Share-id namespace**: AP-published ion share ids must stay out of V34's own allocation path
  (which starts at 0x400 and rises); the probe uses 0x700+ for bitstreams and 0x800+ for frame planes.
  Publishing 0x501..0x504 lost frames with `ion_import failed ... 0x102` even though registration
  succeeded. DEC05/V34 to confirm the production id policy (board post).
- **Racing-window block**: the frozen model maps `VDEC_RACING` to CORE while the kernel implies SOC
  (section 5) [I]. A LAT-only first lock therefore logs one fake register violation. Needs DEC02's
  page trace + a T02/DEC07 decision, not a client change.
- **WAITISR on the fake always times out**: no VDEC IRQ in T02. The real interrupt path is P03 + SPI
  424/425; `wait_irq`/`irq_clear` are wired, but no phone run yet.
- **DMA pool size** for 1080p (DPB x 3 MB + MV/UBE) is hw-lead's 86 MiB RWVC pool decision
  (VDEC_PROTOCOL.md 5.8); this client only consumes handles.
- **No NV12 linear path**: `SET_DECODE_LAYOUT` was not needed here; the vendor reports planar MM21/MT21
  and the plug-in detiles (ap/mm21, DEC06).
- **`general_buf`** (secure/normal side buffers) is not supported: always -1.
