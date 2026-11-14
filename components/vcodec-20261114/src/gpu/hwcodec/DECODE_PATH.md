# Hardware decode path (H.264, then HEVC) — status

Owner: opus-dec1. Updated 2026-11-11 ~18:00. Status words: **PC** = runs on the host runner against the fake hardware
(no pixels can come out of a fake), **BLOCKED** = cannot proceed without the named item, **PHONE** = needs a phone cycle.
Nothing in this file has run on the phone.

**2026-11-11 ~24:00 (opus-decint): phone payloads READY** -- vc3-decode (H.264 640x480 + 1080p) and vc4-hevcdec (HEVC 480p +
1080p), rootfs only on q10je: procedure, expected lines and failure table in `DECODE_PHONE_TEST.md`; PC rehearsal
`decint/run_sim.sh` (vcodecd --decoder + n2vcd_dprobe over the socket transport, kernel-parity fake, 16 KiB granule). Sections
below are the earlier host history.

## 1. What runs on the PC today

`bash gpu/hwcodec/dec1/run_ref.sh` → `N2TEST DEC1-REF PASS 3/3` (≈25 s, builds in `/tmp/n2hw-opus-dec1/ref`).
`gpu/hwcodec/dec1/vdec_ref.c` is the reference AP-side driver (MIT, ~330 lines, every rule commented with its
kernel line / vendor address). It drives the phone's unmodified `vpud` + vendor libs under the interpreter:

| Stream | Result |
|---|---|
| H.264 Main 320x240, 12 AUs, `-bf 2` | header START → 320x240 / buf 320x256 / dpb 10 / MM21; 12 frame STARTs acked; **12 pictures in list_disp in correct display order**; 12 buffers freed; RESET, DEINIT ok |
| H.264 High 1920x1080, 6 AUs | header → 1920x1088, crop 1920x1080, dpb 8, MM21 (Y 2088960 + C 1044480); 6/6 displayed |
| HEVC Main 320x240 | header START → 320x240 / MM21 / dpb 10 (software parse). Frames: BLOCKED on the fake (section 4) |

Per H.264 AU the vendor sends LOCK_LAT, LOCK_CORE, WAITISR(1 = LAT), WAITISR(0 = CORE), UNLOCK_LAT, UNLOCK_CORE,
then START_ACK. ~145 register reads + 450–515 writes per AU (direct MMIO, no GCE), plus ~420 accesses outside the
frozen 12 windows (0x16011800–0x16013fff, 0x16016xxx, 0x16002004/0x1600201c, larb5 0x1600d00c).
Header parsing (SPS/PPS/slice header) is **software** (no register access, no LOCK; agrees with opus-dec2).

Pending fixes the script applies to its /tmp copy (remove when landed in the tree):
- V34 `vendor.mtk.vdec.frame.layout.mode = "0"` in `shim/n2shim_props.c` (else ≥1080p comes out UFO-compressed MT21).
- (V30 va_list fix: landed by IV, 277633/277633.)

## 2. Solved blockers (facts, all [V] on the host)

1. **`get_ringbuf input handle NULL`** (libvpud_vcodec 0x8c70): this kernel has `VIDEO_MAX_FRAME = 64`
   (`include/uapi/linux/videodev2.h:73`) so the vsi rings are 64 entries: list_free_bs @0x0, list_free @0x210,
   list_disp @0xe20, **dec @0x1a30**, pic @0x1af8. The caller (0xc562) passes `vsi.dec.vdec_bs_va` (vsi+0x1ac8):
   must be a non-zero cookie. VDEC_PROTOCOL.md 2.2 (dec @3376) was computed with 32.
2. **vpud died after the first LAT done** (FAULT at 0x63656476 = "vdec"): `n2fmt_args_va` dereferenced the AAPCS
   va_list (passed by value) once too often; first hit by the vendor log helper (libvcodec_utility 0x1e59c). Fixed.
3. **Output format**: default layout mode 1 keeps UFO compression for ≥ 0x1fe000 pixels (libvpud_vcodec 0x107f4) →
   `MT21` = V4L2_PIX_FMT_MT21C, unreadable by the CPU. Property `vendor.mtk.vdec.frame.layout.mode=0` → `MM21`
   (16x32 Y tiles, 16x16 CbCr tiles), vendor log `ufo=0`. SET_PARAM_UFO_MODE (3) has no effect. Linear NV12 direct
   output: `SET_DECODE_LAYOUT` fails for H.264 → MM21→NV12 detile on the iOS side (NEON or Metal).
4. `general_buf_fd` must be -1 when no general buffer is passed.

## 3. Facts the phone path depends on

- **larb sleep handshake**: every LOCK the vendor writes SMI larb SLP_CON (larb5 `0x1600d00c`) bit0 and polls bit16
  up to 50000 times (`vdec_hal_wait_larb_sleep_protect` @0x292e8). The kernel window must allow R/W of that word
  (DEC07's allow-list does for larb5 +0x00c bit0; larb4 +0x00c not seen in the host trace yet — DEC02 to confirm).
  Without it: ~100k failed accesses per frame.
- Frame buffers: the vendor imports each plane by share id and never maps it (`va:(nil)`); bitstream: it copies the
  AU into its own 5 MiB DMA ring (VCU_PA_ALLOCATION) and returns the bs cookie in list_free_bs in the same START.
- Sync mode (one free fb in vsi.dec at every START) never triggers GET/PUT_FRAME_BUFFER.
- WAITISR: core ids 1 (LAT) then 0 (CORE). The fake raises no VDEC IRQ: the reference answers "done"
  (`VR_FAKEIRQ=1`). On the phone: `RWVC_WAIT_IRQ(VDEC_LAT / VDEC)`; K02's ISR status/ack for SPI 424/425 is host-tested only.
- Memory: a 320x240 session already makes ~34 vendor DMA allocations (two 5 MiB rings, one 14 MiB buffer, 24 KiB
  .. 320 KiB working buffers ≈ 27 MiB); 1080p not measured yet (DEC02), plus (dpb+2) x 3 MiB output buffers. The RWVC
  pool is 86 MiB shared with VENC: 1080p is expected to fit, 4K not.

## 4. Open / blocked

| Item | Owner | State |
|---|---|---|
| `ap/n2ap_vdec.c` (library form of vdec_ref.c) | hw-dec01 | header in tree, review posted (2 planes, cookies, general_buf, reuse rule) |
| Decode messages in `include/n2vcd_proto.h` rev 2 | hw-lead (text: `dec1/n2vcd_proto_dec_rev2.h.txt`) | proposed |
| Daemon decode service + client | hw-dec05 | not started in the tree |
| V34 property layout.mode=0 | hw-v34 | posted |
| RWVC_WIN_VDEC_ALL (header rev 3, kernel, fake, V40) | hw-lead / DEC07 / DEC04 / V40 | patches DONE, not landed |
| Host CPU per AU on the fake: 0.08–0.15 s even with the larb ack (vendor timed polls on status bits the fake never sets, e.g. "Wait PicNum rst fail") | DEC02/DEC03 (fake model) | open; not a phone number |
| HEVC frames: vendor polls LAT_VLD 0xe8 (VLD fetch-ok) forever on the fake; with bit0 forced, STARTs ack but no WAITISR/display (HEVC blocks not modelled) | opus-hevc / DEC03 | open |
| MM21 → NV12 detile + VT decoder object | DEC06 | not started |
| Pixels: only the real VDEC produces them | main session | PHONE |

## 5. Feasibility of decode on the phone this week

The software side (vendor flow, buffers, formats, protocol) is solved on the PC; what remains is integration
(n2ap_vdec library, daemon service, VT plug-in with detile) plus the kernel window rev 3 in an image. Realistic order:
(1) DEC01 library + DEC05 daemon messages (host), (2) image with RWVC_WIN_VDEC_ALL + larb SLP_CON allow-list,
(3) phone: `n2vcd_cli dec` of a 320x240 stream → compare MM21 output with ffmpeg on the PC. Steps 1–2 are days of
worker time, not weeks; the unknown is the real hardware's behaviour (IRQ, racing/HW-break, clocks), visible only
in the first phone cycle. A first hardware-decoded picture this week is possible if the VENC phone cycles go
smoothly and the window rev 3 lands with them; VideoToolbox integration later.
