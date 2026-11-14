# The AP <-> VCU message flow of one VENC session, and what `ap/n2ap_venc.c` does with it

Owner: V41.  Every field value below is for the example session the host test drives: **H.264, 1920x1080
visible, 1920x1088 coded, 30 fps, 20 Mbit/s, NV12, V4L2 defaults for the remaining controls**.  The
line citations are into
`$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel`
(short: `K/`).  No part of this was run on the phone.

Roles:

- **AP** = `ap/n2ap_venc.c`, the user-space re-done `venc_vcu_if.c` + `venc_common_if.c` + the clock part
  of `mt6885/mtk_vcodec_enc_pm.c` + the encoder ISR of `mt6885/mtk_vcodec_intr.c`, one session.
- **VCU** = the daemon (`vpud` / `libvcodecdrv.so` in our model), which owns the codec core, the GCE
  command lists and the bitstream.
- Transport = V40's `vcu/n2vcu.h`: `n2vcu_ap_send()` is the queue half of `vcu_ipi_send()` (`K/drivers/
  media/platform/mtk-vcu/mtk_vcu.c:383-446`), `n2vcu_ap_wait_dispatched()` its `ipi_done` half, and
  `n2vcu_ap_register()` + the AP handler are the peer of `vcu_ipi_handler()` (`mtk_vcu.c:1396-1410`).

Layout of every message is in `ap/mtk_venc_ipi.h`.  Sizes were checked against the kernel's own
`venc_ipi_msg.h` compiled twice (32-bit and 64-bit branch; `ap/test_v41/probe_kernel.c` vs
`probe_mine.c`) and against an armv7 AAPCS compile of the same header.

**Send ordering (one AP message).** `vcu_ipi_send()` has two waits, in this order: first the ACK,
`wait_event_timeout(vcu->ack_wq[i], vcu->ipi_id_ack[id], timeout)` (`mtk_vcu.c:435`), where
`ipi_id_ack` is set by `vcu_ipi_handler()` once the AP handler has run (`:1411-1412`); then
`while (ipi_done[i] == 0)` (`:470`), which `vcu_ipi_get()` sets at the *entry* of the daemon's
next `GET_OBJECT` (`:500`).  `n2ap_venc.c` mirrors that order (rev 3): a condition variable the
handler signals for the ACK, then `n2vcu_ap_wait_dispatched()`.  The order matters for the real
`vpud`, whose receiver thread fetches the message, hands it to a service thread and re-enters
`GET_OBJECT` before the reply is sent: waiting for "dispatched" first returns before the reply
exists (IV measured exactly that on the real daemon).

## 0. The session in one picture

```
AP (V4L2 driver role)                          VCU (vpud)                 hardware
  AP_IPIMSG_ENC_INIT            ----------->   venc_vcodec_msg_handler
                                                  allocates struct venc_vsi
  VCU_IPIMSG_ENC_INIT_DONE      <-----------   {vcu_inst_addr}
  AP_IPIMSG_ENC_QUERY_CAP       ----------->   (on IPI_VENC_COMMON)
  VCU_IPIMSG_ENC_QUERY_CAP_ACK  <-----------   {vcu_data_addr -> 64 entries}
  AP_IPIMSG_ENC_SET_PARAM(ENC)  ----------->   reads venc_vsi.config
  VCU_IPIMSG_ENC_SET_PARAM_DONE <-----------
  [JOINED header mode only: AP_IPIMSG_ENC_SET_PARAM(PREPEND_HEADER) + DONE]
  AP_IPIMSG_ENC_ENCODE(seq hdr) ---------->   first frame only, SEPARATE mode
  AP_IPIMSG_ENC_ENCODE(frame,bs)---------->   builds its GCE list, then:
    VCU_IPIMSG_ENC_POWER_ON     <-----------   "power the block"   -> hw->power(VENC)  (VENC_C1 only if core 1 runs)
    VCU_IPIMSG_ENC_WAIT_ISR     <-----------   "wait for done"     -> hw->wait_irq(VENC, 500 ms)
    VCU_IPIMSG_ENC_PUT_BUFFER   <-----------   "drain the free-buffer ring"
  VCU_IPIMSG_ENC_ENCODE_DONE    <-----------   {state, is_key_frm, bs_size}
  ... one ENCODE per frame ...
  AP_IPIMSG_ENC_DEINIT          ----------->   
  VCU_IPIMSG_ENC_DEINIT_DONE    <-----------
```

The AP never blocks inside a request: `VCU_IPIMSG_ENC_POWER_ON`, `POWER_OFF`, `WAIT_ISR` and
`PUT_BUFFER` arrive through the registered handler on the daemon's thread while the AP sits in
`n2vcu_ap_wait_dispatched()`.  That is the same execution context in which the Linux kernel runs
`vcu_enc_ipi_handler()` (`venc_vcu_if.c:68-191`).

## 1. Open: `AP_IPIMSG_ENC_INIT`

`n2ap_venc_open(vcu, hw, N2AP_FOURCC_H264, timeout)`.

1. IPI id from the codec fourcc, exactly the switch of `venc_init()` (`venc_common_if.c:232-271`):
   H264 → `IPI_VENC_H264` 15 (`V4L2_PIX_FMT_H264` = `0x34363248`).  H265 → 16, HEIF → 17, MPEG4 → 19,
   H263 → 21.  VP8 is **rejected with `-EINVAL`** before any message, because `venc_if_init()` only
   accepts the five VCU codecs (`venc_drv_if.c:34-52`) and the VP8 path is the VPU driver, which this
   kernel does not build.  No hybrid id (20) is chosen: that needs `ctx->oal_vcodec == 1`, which the
   V4L2 layer only sets for the OAL codec path.
2. `n2vcu_ap_register(vcu, id, handler, session)` = `vcu_ipi_register()` (`mtk_vcu.c:346-381`).
3. Send `struct venc_ap_ipi_msg_init` (16 bytes):

   | offset | field | value |
   |---|---|---|
   | 0 | `msg_id` | `0xC000` (`AP_IPIMSG_VENC_BASE`, `venc_ipi_msg.h:14,34`) |
   | 4 | `reserved` | 0 (exists so the 64-bit kernel and 32-bit VCU agree, `venc_ipi_msg.h:124-130`) |
   | 8 | `venc_inst` | the AP instance token; the kernel sends `(unsigned long)vcu` (`venc_vcu_if.c:315`). Ours is a 32-bit token `0x4e32xxxx` — the daemon only echoes it. |

   and wait for `VCU_IPIMSG_ENC_INIT_DONE` (`0xD000`), bounded by `timeout` (default 6000 ms =
   `IPI_TIMEOUT_MS`, `mtk_vcu.c:72`).
4. The reply is `struct venc_vcu_ipi_msg_init` (24 bytes), `venc_ipi_msg.h:262-282`:

   | offset | field | value |
   |---|---|---|
   | 0 | `msg_id` | `0xD000` |
   | 4 | `status` | 0 = `VENC_IPI_MSG_STATUS_OK`; anything else fails the open with `-EINVAL` |
   | 8 | `venc_inst` | the same token; a mismatch is `-EPROTO` (the kernel's `vcu != priv` check, `venc_vcu_if.c:97-108`) |
   | 16 | `vcu_inst_addr` | the VCU address of its `struct venc_vsi` |
   | 20 | `reserved` | 0 |

5. `vsi = vcu_mapping_dm_addr(vcu_inst_addr)` (`venc_vcu_if.c:23-24`, `mtk_vcu.c:1199-1230`).  Our
   model checks that the address is 8-aligned and that all **2016 bytes** of `struct venc_vsi` fit in
   the VCU DMEM window (`n2vcu_dmem()`), then maps it with `n2vcu_map_dm_addr()`.
6. Nothing else is read at open.  `vsi->sizeimage[]` and `vsi->sync_mode` are the VCU's to fill; the AP
   consumes them after `VENC_SET_PARAM_ENC` (step 3).

## 2. Capability query: `AP_IPIMSG_ENC_QUERY_CAP` on IPI_VENC_COMMON

The V4L2 layer queries capabilities while initialising a context, *before* `venc_if_init()`
(`mtk_vcodec_enc.c:49-76` via `get_supported_format`, called from `mtk_vcodec_enc_set_default_params`
at `:2660`).  The temporary instance has `id = 0`, and
`vcu_enc_query_cap()` maps that to `IPI_VENC_COMMON` (`venc_vcu_if.c:347`).  We keep that: the query
goes on **IPI 14**, not on the session's 15/16/…, with a separate global query handler (one at a time,
like the kernel's single `ipi_desc[IPI_VENC_COMMON].handler`).

Request `struct venc_ap_ipi_query_cap` (24 bytes, `venc_ipi_msg.h:139-160`; the 32-bit compat variant is
the same bytes, `:143-157`):

| offset | field | formats query | frame sizes query |
|---|---|---|---|
| 0 | `msg_id` | `0xC004` | `0xC004` |
| 4 | `id` | 0 (`VENC_GET_PARAM_CAPABILITY_SUPPORTED_FORMATS`) | 1 (`…_FRAME_SIZES`) |
| 8 | `ap_inst_addr` | AP token | AP token |
| 16 | `ap_data_addr` | AP result buffer | AP result buffer |

Reply `struct venc_vcu_ipi_query_cap_ack` (40 bytes, `venc_ipi_msg.h:165-190`): `msg_id 0xD004`,
`status`, the echoed `ap_inst_addr` at 8, `id` at 16, the echoed `ap_data_addr` at 24,
`vcu_data_addr` at 32.  The AP copies `sizeof(element) * MTK_MAX_ENC_CODECS_SUPPORT` bytes out of the
DMEM at `vcu_data_addr` (`venc_vcu_if.c:38-51`): 64 × 12 bytes for `struct mtk_video_fmt`, 64 × 36 for
`struct mtk_codec_framesizes`.  Our API requires the caller's buffer to be that large and never copies
more; a bad `vcu_data_addr` is a protocol error (`-EIO`).

## 3. Stream start: `VENC_SET_PARAM_ENC`, then `VENC_SET_PARAM_PREPEND_HEADER`

`vb2ops_venc_start_streaming()` sends `VENC_SET_PARAM_ENC` always (`mtk_vcodec_enc.c:1857`) and then
`VENC_SET_PARAM_PREPEND_HEADER` only when the codec is H264/H265/HEIF/MPEG4 **and** the header mode is
not `V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE` (`:1874-1892`).  The control is registered with
`max = JOINED_WITH_1ST_FRAME` and `def = SEPARATE` (`:2753-2755`), so a session with stock controls
sends **only ENC** here.  With `JOINED_WITH_1ST_FRAME` it sends ENC then PREPEND_HEADER and the first
frame's bitstream carries SPS/PPS; with SEPARATE the driver encodes the sequence header as its own
`AP_IPIMSG_ENC_ENCODE` (`bs_mode = VENC_BS_MODE_SEQ_HDR`) right before the first frame
(`m2mops_venc_device_run`, `:2598-2615` → `mtk_venc_encode_header`, `:1975-2050`).
`n2ap_venc_start()` is exactly that pair and `seq_hdr_mode` selects which.

### 3.1 The configuration lives in the vsi, not in the message

`venc_common_if.c:468-526` writes `struct venc_vcu_config` inside the shared `struct venc_vsi` before
any IPI is sent; the IPI only says "ENC".  For the example session (V4L2 defaults where the
application does not choose a value; `mtk_vcodec_enc.c:2727-2742` are the control defaults):

| vsi->config field | offset | value | why |
|---|---|---|---|
| `input_fourcc` | 0 | 6 (`VENC_YUV_FORMAT_NV12`, `mtk_vcodec_drv.h:110`) | source `V4L2_PIX_FMT_NV12` |
| `bitrate` | 4 | 20000000 (`0x01312D00`) | `V4L2_CID_MPEG_VIDEO_BITRATE` default |
| `pic_w` / `pic_h` | 8 / 12 | 1920 / 1080 | visible size |
| `buf_w` / `buf_h` | 16 / 20 | 1920 / 1088 | coded size (H.264 macroblock alignment) |
| `gop_size` | 24 | 0 | control default; apps set e.g. 30 |
| `intra_period` | 28 | 0 | control default |
| `framerate` | 32 | 30 | `framerate_num / framerate_denom` |
| `profile` / `level` | 36 / 40 | 0 / 0 | control defaults (BASELINE / LEVEL_1_0); apps pick HIGH=4 / LEVEL_4_1=12 |
| `wfd` | 44 | 0 | forced by the driver, `venc_common_if.c:526` |
| `operationrate` | 48 | 0 | |
| `scenario` | 52 | 0 | |
| `prependheader` | 56 | 0 | `V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR` default |
| `bitratemode` | 60 | 0 | VBR default |
| `roi_rc_qp`, `roion` | 64, 68 | 0 | |
| `heif_grid_size` | 72 | 0 | |
| `color_desc` | 76..144 | zeros | 17 u32 |
| `resolutionChange` .. `max_h` | 144..156 | 0 | |
| `num_b_frame` | 156 | 0 | |
| `slbc_ready` | 160 | 0 | no SLBC in our build |
| `i_qp` / `p_qp` / `b_qp` | 164/168/172 | 51 / 51 / 51 | RC QP control defaults |
| `svp_mode` | 176 | 0 | no secure session |
| `svp_is_hal_secure_handle` | 180 | 0 | never written by the kernel either |
| `tsvc` | 184 | 0 | |
| `max_qp` / `min_qp` | 188 / 192 | 51 / 0 | |
| `i_p_qp_delta` | 196 | 0 | |
| `qp_control_mode`, `frame_level_qp` | 200, 204 | 0, 0 | |
| `maxrefpnum` | 208 | 0 | |
| `maxrefbufFrameNum`, `dummynal` | 212, 216 | 0 | |

For HEVC/HEIF and MPEG4 the profile/level are converted before the write, exactly as
`venc_common_if.c:509-525`: HEVC profile MAIN(0)→2, MAIN_STILL_PICTURE(1)→8, MAIN_10(2)→4, other→0;
HEVC level `2*level + (tier == MAIN ? 0 : 1)` for levels 0..12, other→26; MPEG4 profile identity 0..4,
other→100; MPEG4 level identity 0..7, other→4.  H264 copies its V4L2 values unchanged; H263 leaves
both at 0.

### 3.2 `AP_IPIMSG_ENC_SET_PARAM` (48 bytes, `venc_ipi_msg.h:172-182`)

| offset | field | value for ENC | value for PREPEND_HEADER |
|---|---|---|---|
| 0 | `msg_id` | `0xC001` | `0xC001` |
| 4 | `vcu_inst_addr` | the vsi address from INIT_DONE | same |
| 8 | `param_id` | 0 (`VENC_SET_PARAM_ENC`) | 7 (`VENC_SET_PARAM_PREPEND_HEADER`) |
| 12 | `data_item` | 0 (the values are in the vsi) | 0 |
| 16 | `data[8]` | zero | zero |

The reply is `struct venc_vcu_ipi_msg_set_param` (48 bytes, `venc_ipi_msg.h:293-309`): `msg_id 0xD001`,
`status`, `venc_inst`, the echoed `param_id`/`data_item`, `data[6]`.

After an ENC reply the AP reads back:

- `vsi->sizeimage[0..2]` → `struct n2ap_enc_param.sizeimage[]` (`venc_common_if.c:528-537`); the VCU
  fills these with the working-buffer sizes the AP allocates.
- `vsi->sync_mode` → `p->sync_mode`, and `ctx->async_mode = !sync_mode` (`:539`).  A daemon that
  runs in async mode (sync_mode 0) reports completed frames through `vsi->list_free` and
  `VCU_IPIMSG_ENC_PUT_BUFFER`; sync mode reports them in `VCU_IPIMSG_ENC_ENCODE_DONE`.

### 3.3 Every other `venc_set_param_type`

`n2ap_venc_set_param()` reproduces the `data_item`/`data[0]` mapping of `vcu_enc_set_param()`
(`venc_vcu_if.c:387-502`) field for field: `ADJUST_BITRATE`→`bitrate`, `ADJUST_FRAMERATE`→`frm_rate`,
`GOP_SIZE`→`gop_size`, `INTRA_PERIOD`→`intra_period`, `SCENARIO`→`scenario`, `NONREFP`→`nonrefp`,
`NONREFPFREQ`→`nonrefpfreq`, `DETECTED_FRAMERATE`→`detectframerate`, `RFS_ON`→`rfs`,
`PREPEND_SPSPPS_TO_IDR`→`prependheader`, `OPERATION_RATE`→`operationrate`,
`BITRATE_MODE`→`bitratemode`, `ROI_ON`→`roion`, `HEIF_GRID_SIZE`→`heif_grid_size`, `SEC_MODE`→`svp_mode`,
`TSVC`→`tsvc`, `ADJUST_MAX_QP`→`max_qp`, `ADJUST_MIN_QP`→`min_qp`,
`ADJUST_I_P_QP_DELTA`→`i_p_qp_delta`, `ADJUST_FRAME_LEVEL_QP`→`frame_level_qp`,
`MAX_REFP_NUM`→`maxrefpnum`, `REFP_DISTANCE`→`refpdistance`, `REFP_FRMNUM`→`refpfrmnum`,
`ENABLE_DUMMY_NAL`→`dummynal`; the zero-item ones are `ENC`, `FORCE_INTRA`, `SKIP_FRAME`,
`PREPEND_HEADER`, `COLOR_DESC` (the color struct itself goes into the vsi, `venc_vcu_if.c:456-458`).
`TS_MODE` (8) and `LOG` (29) are **not** in the VCU switch: `vcu_enc_set_param()` returns `-EINVAL`
before sending (`:499-501`), and so do we.  The host test verifies the whole table against literals.

## 4. One frame: `AP_IPIMSG_ENC_ENCODE`

`n2ap_venc_encode(frame_handle, bs_handle, N2AP_ENC_FRAME, &result)`.

### 4.1 The AP writes into the vsi first (`venc_common_if.c:146-158`)

- `vsi->venc.venc_bs_va` = the bitstream buffer's guest address (or a tagged 32-bit handle cookie when
  the buffer has no guest mapping).  The kernel stores a pointer to its own buffer struct; the daemon
  treats the value as opaque and echoes it in the free-buffer ring.
- `vsi->venc.venc_fb_va` = the frame buffer's cookie; `timestamp`, `roimap`, `qpmap` 0 in this API.
- `vsi->meta_fd = 0`, `meta_size = 0`, `meta_addr = 0` (no HDR dynamic metadata; `venc_vcu_if.c:549-558`).
- `vsi->venc.index`, `fb_num_planes` are not written by the kernel here either.

Then `n2vc_dma_sync(..., to_device=1)` for the frame and the bitstream buffer, as the frozen
`include/n2vc_hw.h` requires ("to_device before the trigger").

The first job after stream start in the default (SEPARATE) header mode is a sequence-header
encode of the bitstream buffer alone, then the frame encode (`m2mops_venc_device_run` calls
`mtk_venc_encode_header` and queues the frame work, `mtk_vcodec_enc.c:2598-2615`); in JOINED mode the
first message is the frame.

### 4.2 `struct venc_ap_ipi_msg_enc` (76 bytes, `venc_ipi_msg.h:196-221`)

| offset | field | value (1080p example) |
|---|---|---|
| 0 | `msg_id` | `0xC002` |
| 4 | `vcu_inst_addr` | vsi address |
| 8 | `input_addr[0]` | frame physical address (32-bit; the larb ports bypass the IOMMU) |
| 12/16 | `input_addr[1/2]` | 0 (single-plane NV12) |
| 20 | `input_size[0]` | 1920·1088·3/2 = 3133440 = `0x2FD000` |
| 24/28 | `input_size[1/2]` | 0 |
| 32 | `bs_addr` | bitstream buffer physical address |
| 36 | `bs_size` | e.g. `0x100000` (1 MiB) |
| 40 | `data_offset[0..2]` | 0 |
| 52 | `input_fd[0..2]` | 0 with `n2ap_venc_encode()`; `n2ap_venc_encode_fd()` passes the V34 share id (Android sends a dma-buf fd, `get_mapped_fd`, `venc_vcu_if.c:545`; the vendor ring buffer `ion_import()`s it) |
| 58 | `bs_fd` | 0 with `n2ap_venc_encode()`; the V34 share id with `n2ap_venc_encode_fd()` |
| 60 | `fb_num_planes` | 1 |
| 61 | `bs_mode` | 3 (`VENC_BS_MODE_FRAME`, `venc_ipi_msg.h:462`) |
| 64 | `meta_size` | 0 |
| 68 | `meta_fd` | 0 |
| 72 | `qpmap` | 0 |

Sequence header (`N2AP_ENC_SEQ_HDR`): `bs_mode = 2` (`VENC_BS_MODE_SEQ_HDR`), `fb_num_planes = 0`, all
`input_*` 0 — the kernel passes `frm_buf = NULL` for that call (`venc_common_if.c:136`).  Final frame:
`bs_mode = 4` (`VENC_BS_MODE_FRAME_FINAL`, `venc_common_if.c:205`); the frame may also be NULL (the EOS
flush, `mtk_vcodec_enc.c:1928`), only `VENC_BS_MODE_FRAME` requires a frame.

### 4.3 While the AP waits: the daemon's requests

| message | size | fields | AP action (kernel equivalent) |
|---|---|---|---|
| `VCU_IPIMSG_ENC_POWER_ON` `0xE000` | 16 | `status`, `venc_inst` | `hw->power(VENC,1)`, refcounted.  Linux's `venc_encode_prepare()` → `mtk_vcodec_enc_clock_on(ctx, 0)` gates larb7/VENC0 (`mtk_vcodec_enc_pm.c:260-331`), but `RWVC_POWER(VENC_C1)` on this model is the **whole** VEN_CORE1 domain + gates + larb8, not VENC0's bare clock gate; power-cycling it resets core 1 (hw-k02).  A core-0 session therefore takes only `RWVC_BLK_VENC`; the V4L2 driver's `venc_lock()`/`enc_sem` has no equivalent (one client). |
| `VCU_IPIMSG_ENC_POWER_OFF` `0xE001` | 16 | same | release one reference; at zero `hw->power(VENC,0)` |

A daemon that programs the core through GCE command lists and never sends POWER_ON
(`libvcodecdrv.so` does; the Linux kernel powers the block in `vcu_gce_set_cmd_flush`,
`mtk_vcu.c:994`) is covered by the explicit `n2ap_venc_power(e, on)` API: one reference before the
first register write, one release after the job's done interrupt, sharing the same counter as the
daemon's POWER_ON/POWER_OFF.
| `VCU_IPIMSG_ENC_WAIT_ISR` `0xE002` | 24 | `irq_status` at 16, `timeout` at 20 | `hw->wait_irq(VENC, 500 ms)` (`WAIT_INTR_TIMEOUT_MS`, `mtk_vcodec_drv.h:35`); on success write the status and `timeout=0`; on timeout write the previous `ctx->irq_status` and `timeout=1` (`venc_vcu_if.c:58-66,159-169`) |
| `VCU_IPIMSG_ENC_PUT_BUFFER` `0xE003` | 16 | — | drain `vsi->list_free` one entry per call, advancing `read_idx` and decrementing `count` exactly like `venc_get_free_buffers()` (`venc_common_if.c:354-391`) |

The WAIT_ISR reply is written into the same buffer the daemon sent; the kernel copies the modified
`share_obj` back to user space (`mtk_vcu.c:1814-1821`), so the daemon sees `irq_status`/`timeout`.

### 4.4 The completion

`struct venc_vcu_ipi_msg_enc` (32 bytes, `venc_ipi_msg.h:322-343`): `msg_id 0xD002`, `status`,
`venc_inst`, `state` (0 frame, 1 partial, 2 skip, 3 error), `is_key_frm`, `bs_size`, `reserved`.

**Kernel gap, reproduced deliberately:** `vcu_enc_ipi_handler()` has no case for
`VCU_IPIMSG_ENC_ENCODE_DONE` (`venc_vcu_if.c:123-179`); the message falls into the default branch, so
only `signaled`/`failure` are set and `state`/`is_key_frm`/`bs_size` are ignored.  The V4L2 layer gets
the size and the key flag from the **free-buffer ring** instead (`mtk_enc_put_buf()` →
`venc_get_free_buffers()`, `mtk_vcodec_enc.c:113-178`, `venc_common_if.c:354-391`).  In synchronous
mode the kernel drains that ring right after `venc_if_encode()` (`mtk_vcodec_enc.c:2590`, `:1931`);
our AP does the same after every successful send, which is the only way a sequence-header size is
visible: `libvpud_vcodec` answers that job with ENCODE_ACK and leaves the size in `list_free` without a
PUT_BUFFER.  Our result rule is:

1. if a ring entry drained during this encode matches the bitstream cookie → use its `bs_size` and
   `is_key_frm` (`result.from_free_list = 1`, the kernel's normal path);
2. else if an ENCODE_DONE arrived → use its fields (what a plain daemon answers; the kernel's
   `FRAME_FINAL` path reads `vcu->bs_size`, which this tree never sets — another gap);
3. else the answer was `VCU_IPIMSG_ENC_ENCODE_ACK` (0xD005, common 16 bytes): size 0, key 0.

`VCU_IPIMSG_ENC_ENCODE_ACK` is treated as a completion for the send even though the kernel's case is
empty (`venc_vcu_if.c:174-175`): otherwise the AP could never tell "acknowledged, no data" from "the
daemon is still working".  An asynchronous (GCE) frame job is therefore acknowledged with size 0;
`n2ap_venc_wait_done()` polls the PUT_BUFFER captures (bounded), and then syncs the bitstream buffer
`from_device` (cache invalidation equivalent).  The synced entries are the ones the daemon's
`venc_vsi.list_free` ring carried, exactly `venc_get_free_buffers()`.

That AP sync is at the encode boundary.  A daemon that post-processes the bitstream on the CPU
*inside* the job — `VENC_PrependHeader` does `memmove(bs + hdr_len, bs, frame_bytes)` after the GCE
flush — needs the mirror refreshed right after that flush, not when the AP call returns; otherwise the
CPU move reads stale zeros and flushes them over the slice (opus-hevc, host-verified for H.264 and
HEVC).  That refresh belongs to the daemon/model's GCE executor (`vcu_gce_flush_ioctl` in V40), where
`n2vc_dma_sync(..., to_device=0)` is the primitive.

## 5. Close: `AP_IPIMSG_ENC_DEINIT`

`struct venc_ap_ipi_msg_deinit` (8 bytes, `venc_ipi_msg.h:228-233`): `msg_id 0xC003`, `vcu_inst_addr`.
Reply `struct venc_vcu_ipi_msg_deinit` (16 bytes, `venc_ipi_msg.h:350-357`): `msg_id 0xD003`, `status`,
`venc_inst`.  After that the AP replaces its IPI handler with a dropper (there is no
`vcu_ipi_unregister()`), drops any power reference the daemon left behind, and frees the session.

## 6. Error and timeout semantics

| situation | AP result | kernel equivalent |
|---|---|---|
| daemon reports `status != 0` in a DONE | `-EINVAL`, session usable | `vcu_enc_send_msg()`: `vcu->failure` → `-EINVAL` (`venc_vcu_if.c:230-231`) |
| daemon reports a negative status the handler itself set (bad DMEM address) | that code (`-EIO`) | n/a (kernel would have faulted) |
| message shorter than its struct, or wrong `venc_inst`/`ap_inst_addr` | `-EPROTO`, no abort for a short reply, no ack for a wrong cookie | kernel checks `venc_inst` (`:97-108`) but not lengths |
| the daemon fetches the message but never answers / never re-enters GET | `-ETIMEDOUT` after `timeout_ms`; session **aborted**, later sends `-EIO` | `IPI_TIMEOUT_MS` (`6000`) then kills vpud; `vcu->abort` makes later sends `-EIO` (`:207-208`) |
| `hw->power`/`wait_irq` fails | `-EIO` on the affected send; session usable | the clock/IRQ APIs log and continue |

The timeout is the **whole** condition: `n2vcu_ap_wait_dispatched()` reports success only after the
daemon has come back for its next message (`ipi_done` in `mtk_vcu.c:441-446`), which it can only do
after every reply it owed has been dispatched.  A daemon that fetches and then stalls times out.

## 7. What is deliberately not modelled

- `venc_lock()`/`venc_unlock()` and `enc_sem[]` (`mtk_vcodec_enc.c:3158-3210`): the hardware semaphore
  between V4L2 contexts.  Our model has one encoder client; the daemon serialises internally.
- `vcu_get_gce_lock()` and `venc_encode_pmqos_*()` (DVFS/EMI bandwidth): the phone's kernel driver does
  those; our `/dev/redwood-vcodec` power sequence is the clock/power part only.
- dma-buf fds: `n2ap_venc_encode_fd()` passes the V34 share ids in `input_fd[0]`/`bs_fd` because the
  vendor ring buffer calls `ion_import()` on them.  `n2ap_venc_encode()` publishes the two handles
  itself through V34's weak `n2shim_ion_register_share()` (stable ids 0x520..0x55f, one per handle)
  when the shim is linked, so the product daemon's plain call works; without the shim it sends 0.
  The AP still syncs every guest-mapped buffer through `n2vc_dma_sync()` around the job.
- HDR dynamic metadata (`vsi->meta_*`, `struct mtk_hdr_dynamic_info`): always zero.
- Multi-plane source frames: `n2ap_venc_encode()` takes one frame DMA handle (NV12/NV21).  The
  message struct supports three planes; adding an API for it is future work.
- `VENC_SET_PARAM_TS_MODE` and `VENC_SET_PARAM_LOG`: rejected before sending, as the VCU does.
