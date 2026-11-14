# fakevenc -- STATUS (owner: opus-venc)

11 November 2026. Host only. Nothing was run on the phone, nothing was published, no vendor code is in this
directory (the tools read the vendor files at run time from `gpu/build/cam-vendor-20261109/vfs`).

## What is here

| File | What |
|---|---|
| `n2venc_fake.[ch]` | VENC behaviour on top of any `n2vc_hw` (normally T02's fake): soft reset 0x0a8, trigger 0x058 (bit 2 = frame) -> IDR slice of I_PCM macroblocks from the NV12 source at 0x06c/0x070<<4 into the bitstream buffer at 0x064<<4, BYTE_CNT 0x098, IRQ status 0x05c, one interrupt on RWVC_IRQ_VENC(_C1), ack 0x060, POLL ops evaluated on the model, DRAM (0x8) status when the buffer is too small. SPS/PPS learned from `n2venc_fake_set_headers()` or from the start of the bitstream buffer. |
| `t_fakevenc.c`, `run_tests.sh` | unit test (49 checks x ASan/UBSan, -O2, TSan) + ffmpeg decode of the model's frame, bit-exact against the source |
| `apply_patches.py` | the changes outside this directory that a full session needs, as anchored idempotent edits on a COPY (refuses to run outside /tmp) |
| `run_session.sh` | copy tree -> /tmp, add fakevenc, apply the patches, build, run vpud + vendor libs: SEQ_HDR + IDR frame |
| `tools/wsites.py` | register call-site census of libvcodecdrv.so (facts/VENC_PROTOCOL.md section 3) |
| `../facts/VENC_PROTOCOL.md` | the register-level protocol (verified vs inferred) |

## Commands and last lines

    bash gpu/hwcodec/fakevenc/run_tests.sh
    N2TEST fakevenc PASS 3/3            (each: "N2TEST fakevenc-unit PASS 49/49" + "decoded frame equals the source")

    bash gpu/hwcodec/fakevenc/run_session.sh            # also --san, --size 640x480
    run_session: N2VHOST encode seq-hdr bytes=29 key=0
    run_session: N2VHOST encode frame fds=1 bytes=115812 key=1
    run_session: N2VHOST fake-venc resets=1 triggers=1 frames=1 irqs=1/1 acks=1 ... dma_errors=0 unsupported=0 headers=1
    run_session: N2VHOST M-f wait_irq(RWVC_IRQ_VENC, timeout_ms=1000) reached
    run_vhost.sh: .../bitstream-320x240.h264 validated (H.264 SPS/geometry/decode/PSNR, 115841 bytes)

640x480: seq-hdr 30 bytes, frame 463212 bytes, validated. `--san` (ASan+UBSan build of the whole runner): same result,
no sanitizer report. These ran against the work tree of 16:50 (IV's encode_fd/wait_done flow, V41's fds, V34's
`foreign` flag already in the tree) plus `apply_patches.py`.

## Changes outside fakevenc/ (owners told on the board; applied only to /tmp copies)

Needed for the session (each was the stop of the run without it):

1. V34 `shim/n2shim_android.c`: `ION_SYS_CACHE_SYNC` copies guest<->device (clean/flush = to device, invalidate =
   from device). Without it the SPS/PPS the daemon writes with the CPU are wiped by the AP's from_device sync and the
   stream fails the H.264 check.
2. V41 `ap/n2ap_venc.c`: drain `list_free` after every encode (kernel `mtk_enc_put_buf()` in synchronous mode). Without
   it the SEQ_HDR size reads 0 and the frame returns the header's 29 bytes.
3. IV `host/Makefile`, `host/n2vhost.c`: build `fakevenc/n2venc_fake.c`, wrap T02's fake with `n2venc_fake_wrap()`
   (`hw` stays T02's for its controls; `hw_used` goes to n2vhost_start_entry and is closed at the end), print the
   model's counters.

Recommended (the session also passes without them in the current tree):

4. V40 `vcu/n2vcu.c`: power the VENC core around every GCE flush (the kernel's `venc_encode_prepare()` at
   `mtk_vcu.c:994`), and always signal the flush callback even when the list failed (the kernel's cmdq callback runs
   for a timed-out packet too). IV's runner powers VENC around each job instead, which is fine for the host.
5. V41: `FRAME_FINAL` without a frame (the EOS flush, `mtk_vcodec_enc.c:1928`) and `n2ap_venc_wait_output()`;
   superseded by IV/V41's `wait_done` if that lands.
6. V33/V40 DMEM MAP_FIXED commit (`n2vfs_dev_mmap_fixed`): superseded by IV's `commit_vma` translation, skipped
   automatically when that is in the file.

Already in the tree from other owners (my earlier findings, fixed independently): RELRO/page-rounded maps (IV),
V34 `foreign` ion_free/munmap, V41 share fds, IV wait_done.

## Not done / limits

- The model writes I_PCM, CAVLC, 4:2:0 only; a CABAC or 4:2:2 PPS is counted in `unsupported` and still gets CAVLC
  (would not decode). The vendor uses Baseline/CAVLC for these sizes.
- No rate control, no references, no motion: every frame is an IDR-style I_PCM slice. P frames, HEVC, the hardware
  SPS/PPS/VPS triggers (bits 1/2/0x80; counted in `unsupported`, status bit raised, 0 bytes) and weighted prediction
  (events 37/38) are not modelled. Multi-frame sessions were not run.
- The inner T02 recorder shows the inner array value for model registers (e.g. `R VENC 0x0098 0x00000000` while the
  library got the model's byte count); `venc-fake.txt` in the run directory has the model's own lines.
- The model keeps its registers across power-off (real hardware would lose them; every job reprograms them anyway).
- Meanings marked [I] in VENC_PROTOCOL.md (0x068 units, 0x028 layout beyond two sizes, readback registers) are
  guesses that the phone must confirm.
