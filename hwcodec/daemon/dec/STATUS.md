# DEC05 — vcodecd decoder service + client library

Last update: 11 November 2026, ~19:05.  Host only: no phone, no publish.  Nothing was committed
(the lead owns commits; `git status` shows only new files under the owned paths).

## Implemented

- **Protocol rev 2 (proposed)**: `daemon/dec/n2vcd_proto_dec_rev2.h.txt`, paste-ready for
  `include/n2vcd_proto.h`, based on opus-dec1's proposal with two amendments:
  1. `DEC_ADD_BUFFER` gains `c_handle`/`c_bytes` (32 bytes).  `c_handle == 0` is the normal
     one-buffer form (C plane at `stride*buf_h`, matching `ap/n2ap_vdec.c`'s
     `fb_c_handle == fb_handle` branch at 18:33); a separate second RWVC buffer is allowed too.
  2. `OPEN.flags` bits 8..15 select the requested output format (0/1 MM21, 2 NV12 refused with
     `E_UNSUPPORTED` today); `DEC_PICTURE_OK`'s last word is `poc` (same offset).
  `test/stage_proto.py` inserts the text into a `/tmp` copy of the frozen header until hw-lead
  lands it; the tree header was still rev 1 at the time of writing.
- **Service** `daemon/dec/n2vcd_dec_service.c`: one decoder session at a time; OPEN (codec, optional
  max size, format), DEC_HEADER (geometry + buffer sizes), DEC_ADD_BUFFER xN (up to 16), DEC_INPUT
  (Annex B AU + pts), DEC_PICTURE (display order, pts/poc/crop size), DEC_RELEASE, DEC_FREE_BUFFERS
  (resolution change), FLUSH, CLOSE, HELLO for the standalone daemon.  Idle timeout, one-session
  limit, client-gone clean-up, seq/type/session validation, `N2VCD_ERROR` replies, fatal guest-stop
  path (`E_GUEST` to the owner and seq 0 to the other clients, then the `fatal` hook; the test daemon
  exits 70).  Geometry invariants: display ≤ OPEN max, stride 16-aligned, buf_h 32-aligned,
  `out_buf_bytes == stride*buf_h*3/2`, `dpb_frames` 1..16, MM21 only.
- **AP adapter** `daemon/dec/n2vcd_dec_ap_n2ap.c`: DEC01's `n2ap_vdec` as the backend (also run
  against DEC01's real `n2ap_vdec.c` + scripted daemon stand-ins: `test/run_adapter.sh`, 54/54).  Header START
  with no frame buffer; synchronous one-buffer-per-START decode; `fb_c_handle == fb_handle` single
  allocation; ion share ids 0x580.. (V41 owns 0x520..0x55f); client handles registered in V41's
  `n2vc_dma` table from the service's pa; list_disp/list_free harvest; release/reuse accounting;
  resolution-change flush; guest-stop callback passed in by the daemon.  It also refuses a stream
  whose `vsi.crop.left/top` is nonzero (not on the wire; all measured streams are 0,0).
- **Client** `client/dec/n2vcd_dec.c`: `n2vcd_dec_connect(_path)`, `info`, `set_timeout`,
  `open`/`header`/`add_buffers`/`buffer_handle`/`buffer_rw`/`input`/`picture`/`release`/
  `free_buffers`/`flush`/`close`/`disconnect`.  One mutex per client, seq-checked replies, 5 s
  default timeout, `N2VCD_E_*` mapping, session/client death semantics, one buffer per pool entry
  with `plane 1` translated to `y_bytes + offset`, all buffers allocated through P03's transport.
  `n2vcd_dec_path.c` is separate so a test binary can link the client without P03's transports.

## Tested (host, exact commands and last lines)

```
bash gpu/hwcodec/daemon/dec/run_tests.sh
DEC05 unit asan DEC05 unit PASS 277/277
DEC05 unit o2   DEC05 unit PASS 277/277
DEC05 unit tsan DEC05 unit PASS 277/277
DEC05 oracle 320x240x6 PASS (320,240 frames 6)
DEC05 oracle 1920x1080 PASS (1920,1080)
DEC05 e2e asan DEC05 e2e PASS 104/104
DEC05 e2e o2   DEC05 e2e PASS 104/104
DEC05 e2e tsan DEC05 e2e PASS 104/104
DEC05 ios compile PASS (4 objects)
N2TEST DEC05 PASS 1150/1150
```

15 s, `/tmp/n2hw-DEC05` 26 MB, `daemon/dec` 168 KB + `client/dec` 48 KB, no file over 300 KB.

What the numbers mean:
- Unit (277 checks per config): every protocol message and its failure modes (wrong size, wrong
  session, seq 0, bad magic, unknown type, truncated/oversized, wrong/missing reply, double
  release, add beyond `dpb_frames`, too-small planes, alias handles, input size/flags, idle drop,
  client gone, guest stop, daemon gone, timeout), the 320x240 and 1920x1080 geometry literals from
  DEC03's vendor table, 2:1 plane split, buffer I/O, reorder/B-frame display order, flush drain,
  buffer exhaustion, and the full resolution-change flow (changed input → free → new header → new
  pool → resend).
- E2E (104 checks per config): ffmpeg streams (ffprobe-checked in the shell), a real `dec_testd`
  process over `SOCK_SEQPACKET` using hw-ip's `n2vcd_srv.c`, SPS parsed by DEC03's `n2vld`, 5 access
  units with display-order pts 1001..1005, every picture read back from its shared buffer file
  (the daemon copies no pixels), flush drain 2, 1080p geometry 1920x1080/1920/1088/3133440, daemon
  killed mid-session → `E_SESSION`, idle-ms daemon → `E_SESSION`, guest-stop daemon → connect
  `E_GUEST` and `waitpid` status 70.
- iOS: `n2vcd_dec_service.c`, `n2vcd_dec.c`, `n2vcd_dec_path.c`, `n2vcd_dec_ap_n2ap.c` compile for
  `arm64-apple-ios16.0` against the iPhoneOS 16.5 SDK with `-Wall -Wextra -Werror`.

## Not done / unresolved

1. **Nothing on the phone.**  The adapter has been executed against DEC01's real `n2ap_vdec.c` and
   its scripted daemon stand-ins (`test/run_adapter.sh`, 54/54) but not against the interpreted
   vendor library; DEC01's probe/runner owns that path.  The service/client/fake path is fully
   host-tested.
2. **The rev-2 header is proposed, not landed.**  The tree `include/n2vcd_proto.h` is still rev 1;
   every build stages the text in `/tmp`.  When hw-lead lands a different text, the two amendments
   (two plane handles optional, OPEN format flags, `poc`) must be reconciled — posted with the exact
   text.
3. **Integration into `daemon/vcodecd.c` is hw-ip's edit.**  The dispatcher rules and the exact
   constructor snippet are in `daemon/dec/README.md`; the HELLO reply must change `dec_codecs` from
   0 to 3.  Until then `vcodecd` still answers decoder requests `E_UNSUPPORTED`.
4. **Session limit across encoder and decoder**: this service allows one decoder session; the daemon
   currently allows one encoder session.  Running both at once is hw-ip's policy; the vpud
   interpreter is single-threaded, so serialising them is recommended.  Not tested.
5. **Output format**: MM21 only.  The vendor produces NV12 for no measured path (`DECODE_PATH.md`
   section 2.3); requesting NV12 is refused at OPEN.  MM21→NV12 detile is DEC06's (opus-mm21's
   `n2_mm21`).
6. **Crop origin** (nonzero `vsi.crop.left/top`) is refused by the adapter because the wire text
   carries only the display size.  All measured streams are 0,0; a real stream with an origin needs
   a protocol field.
7. **VDEC_NEED_MORE_OUTPUT_BUF** (changed bit 4) is retried up to 3 times with another free
   reference buffer before the input is reported as not consumed; on the real vendor this also
   depends on DEC01 fixing START data[0]/data[1] (opus-hevcdec: bytesused first, buffer size
   second).  My adapter already passes `bs_bytes` = allocation and `bs_length` = AU per the
   `n2ap_vdec_job` header semantics, so only `n2ap_vdec.c`'s message fill needs the swap.
8. **Resolution change** follows the proposed flow; on the real vendor it depends on
   `n2ap_vdec_flush` after `changed` (the adapter does that).  The vendor behaviour is `[I]` and not
   exercised on the phone.
8. **`dpb_frames` is capped at 16** (DEC01's `N2AP_VDEC_FB_QUEUE`).  A stream whose DPB needs more
   is refused with `E_HW`/`E_UNSUPPORTED` rather than dropping pictures; H.264 level 4.1 with 16
   references (19 buffers) would hit that.
9. **Write-combining** is not enabled here because the adapter does not own the vcu; the daemon must
   call `n2vcu_set_write_combining(vcu, 1)` at open (DEC04's measurement: 2116 → 1033 RWVC_REGS calls
   per AU).  Posted to hw-ip.
10. **`n2vcd_dec_client` is separate from P03's client**: it speaks the same transport but has its
    own HELLO/session state; DEC06 should use `n2vcd_dec_connect_path()` (or P03's transport +
    `n2vcd_dec_connect`) for decoder sessions only.
11. **Timeout after a lost reply** leaves the client usable (no seq resync is attempted); the next
    reply would be out of sequence and mark it dead.  A caller should reconnect after `E_HW`; DEC06's
    policy is its own.
12. **No phone image change is needed for this package**; the phone test is hw-ip/lead2's once the
    header and dispatcher land, with the RWVC_WIN_VDEC_ALL window (DEC07 rev 3) in the image.

## Files

`daemon/dec/`: `n2vcd_proto_dec_rev2.h.txt`, `n2vcd_dec_service.h/.c`, `n2vcd_dec_ap_n2ap.h/.c`,
`README.md`, `STATUS.md`, `run_tests.sh`, `test/{fake_ap.h,fake_ap.c,t_dec_unit.c,dec_testd.c,
t_dec_e2e.c,t_adapter.c,run_adapter.sh,stage_proto.py}`.
`client/dec/`: `n2vcd_dec.h`, `n2vcd_dec.c`, `n2vcd_dec_path.c`, `README.md`.
