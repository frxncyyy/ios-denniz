# fakevenc HEVC part -- STATUS (owner: opus-hevc)

11 November 2026. Host only. Nothing was run on the phone, nothing was published, no vendor code or vendor output is in
these files (the runs read the vendor libraries at run time from `gpu/build/cam-vendor-20261109/vfs`).

## Files (all new; opus-venc's files are changed only through the documented hook in a /tmp copy)

| File | What |
|---|---|
| `n2venc_fake_hevc.[ch]` | HEVC frames of the fake VENC: decides per frame trigger from register 0x000 bit 2 whether the frame is HEVC, learns the session's VPS/SPS/PPS from the bitstream buffer (or assumes the vendor's configuration with the size of register 0x114c when the headers are joined), and writes an IDR slice that decodes with them: CUs of 16x16, DC intra prediction, largest allowed TBs, integer DCT residual at QP 22 (coarser on buffer overflow), CABAC, sign data hiding, cu_qp_delta, PCM / bypass / transform-skip flags when the PPS/SPS enable them, deblocking disabled through the slice-header override. |
| `t_fakevenc_hevc.c`, `run_tests_hevc.sh` | 5 header configurations (one is the vendor's), register/DMA entry, joined-header fallback, overflow, H.264 rejection; every stream decoded by ffmpeg and compared byte for byte with the encoder's reconstruction + `tests/check/hevc_check.py` |
| `apply_hevc_patches.py` | the edits outside these files (V41 param set -- now in the tree --, opus-venc hook + test link line, IV Makefile / n2vhost.c / run_vhost.sh: `N2VHOST_CODEC=hevc`, `N2VHOST_JOINED=1`, HEVC stats line, `.h265` check); refuses to run outside /tmp |
| `run_session_hevc.sh` | copy tree -> /tmp, apply opus-venc's and these patches, run vpud + vendor libs for an HEVC session |
| `../facts/HEVC_ENC_PROTOCOL.md`, `../facts/HEVC_DEC_NOTES.md` | the protocol facts (verified vs inferred, addresses) |

The hook in `n2venc_fake.c` (3 edits, block `opus-venc` of apply_hevc_patches.py): `#include "n2venc_fake_hevc.h"`;
two static adapters `hevc_dma()` / `hevc_avail()` over `dma_xfer()` / `dma_locate()`; in `encode_frame()` right after
`f->st.frames++` a call `n2venc_hevc_frame(&io, &bs_cap)`; a result >= 0 skips the H.264 encoder (0 = DRAM overflow
irq, else BYTE_CNT = result, FRM irq). H.264 frames (0x000 bit 2 clear) are untouched: opus-venc's suite passes on the
hooked copy (`N2TEST fakevenc PASS 3/3`, run on /tmp/n2hw-opus-hevc/vt).

## Commands and last lines

    bash gpu/hwcodec/fakevenc/run_tests_hevc.sh
    N2TEST fakevenc-hevc PASS 17/17     (unit 47/47 x ASan+UBSan / TSan / -O2; 4 streams decoded == reconstruction;
                                         5 hevc_check PASS incl. 1920x1080, case 3 with deblocking on: PSNR only)

    bash gpu/hwcodec/fakevenc/run_session_hevc.sh                    # also --size 640x480, --size 1920x1080, --san
    run_session_hevc: N2VHOST encode seq-hdr bytes=78 key=0
    run_session_hevc: N2VHOST encode frame fds=1 bytes=43869 key=1
    run_session_hevc: N2VHOST fake-venc-hevc frames=1 headers=1 defaults=0 320x240 qp=22 last_bytes=43869 ...
    run_vhost.sh: .../bitstream-320x240.h265 validated (HEVC SPS/geometry/decode/PSNR, 43947 bytes)   [39.0 dB]
    640x480: header 79 B, frame 174789 B, validated. 1920x1080: header 80 B, frame 795230 B (QP 28 after the 1 MiB
    buffer overflowed at QP 22), validated, 33.5 dB. --san: validated, no sanitizer report.
    N2VHOST_JOINED=1 (daemon mode): validated only with the V40 mirror refresh (board "opus-hevc HIGH"); without it
    the vendor returns VPS/SPS/PPS + zeros (H.264 the same).

## Not done / limits

- Lossy only (the vendor headers forbid PCM/bypass); only IDR frames: P frames get an IDR slice too (not exercised).
- Not supported (counted in `unsupported`, a stream is still written or the frame falls back): scaling lists, tiles,
  WPP, bit depth > 8, chroma formats other than 4:2:0, a 4x4 luma TB.
- Headers are process-wide (one session at a time, as the host runner does).
- `run_session_hevc.sh` and the HEVC runner switch depend on IV accepting the n2vhost.c/run_vhost.sh/Makefile blocks;
  V40 must land the mirror refresh for joined headers; IP: HEVC default level when the client sends 0; P03: `--codec`.
- HEVC decode: analysis only (facts/HEVC_DEC_NOTES.md); no host run.
