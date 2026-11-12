# gpu/hwcodec/daemon/dec — the decoder half of vcodecd

Package DEC05.  The decoder service, its AP adapter, the client library (`client/dec/`) and the
proposed `include/n2vcd_proto.h` rev-2 text.  Host-tested only: nothing here has run on the phone.

| File | What |
|---|---|
| `n2vcd_proto_dec_rev2.h.txt` | paste-ready rev-2 text for `include/n2vcd_proto.h` (decoder messages, base opus-dec1's proposal, two amendments marked).  `test/stage_proto.py` builds a `/tmp` header from it until hw-lead lands it. |
| `n2vcd_dec_service.h/.c` | the protocol state machine: one decoder session, a fixed pool of 16 output entries, `DEC_HEADER`/`DEC_ADD_BUFFER`/`DEC_INPUT`/`DEC_PICTURE`/`DEC_RELEASE`/`DEC_FREE_BUFFERS`/`FLUSH`/`CLOSE`, idle timeout, exit-70 guest-stop policy.  No pixels, no bitstream copies, no printing (hooks only). |
| `n2vcd_dec_ap_n2ap.h/.c` | the AP ops over DEC01's `ap/n2ap_vdec.c`: header START with no frame buffer, synchronous one-buffer-per-START decode, list_disp/list_free harvest, ion share publishing, client handles in the `n2vc_dma` registry. |
| `test/fake_ap.c` | test-only AP model: SPS geometry parsed through DEC03's `n2vld`, reorder/delay, display order, changed/resolution flow, guest stop, buffer-file I/O for the E2E. |
| `test/t_dec_unit.c` | in-process suite: client + service + fake AP, all error paths. |
| `test/dec_testd.c`, `test/t_dec_e2e.c` | a real `dec_testd` process over `SOCK_SEQPACKET` (hw-ip's `n2vcd_srv.c`), real stream files, fatal exit 70, idle drop, daemon gone. |
| `run_tests.sh` | the acceptance command. |

## Integrating into vcodecd (hw-ip)

The daemon owns the vhost/vcu, the receive loop and the watchdog; this module owns one decoder
session.  Nothing here creates a vhost or touches `g.s`.

```c
/* once, at startup, after the vcu and hardware back end exist: */
struct n2vcd_dec_ap_n2ap_cfg acfg = {
        .vcu = vcu, .hw = vhw, .timeout_ms = hw_timeout_ms,
        .guest_stopped = vcodecd_guest_stopped, .guest_ctx = &g,
};
struct n2vcd_dec_ap_n2ap *adapter = n2vcd_dec_ap_n2ap_create(&acfg);
struct n2vcd_dec_service_cfg dcfg = {
        .send = daemon_send_to_client, .dma_info = daemon_dma_info, .ctx = &g,
        .ap = n2vcd_dec_ap_n2ap_ops(), .ap_ctx = adapter,
        .log = daemon_log, .fatal = daemon_fatal_guest_stop,       /* the existing exit-70 path */
        .session_idle_ms = g.session_idle_ms,
};
struct n2vcd_dec_service *dec = n2vcd_dec_service_create(&dcfg);

/* in the receive loop, before the encoder switch: */
if (n2vcd_dec_service_owns(buf, n)) {
        (void)n2vcd_dec_service_handle(dec, from, buf, (uint32_t)n);
        return;
}
/* FLUSH and CLOSE are shared v1 ids: route them to us when the session id is ours. */
if ((type == N2VCD_FLUSH || type == N2VCD_CLOSE) && h.session == n2vcd_dec_service_session(dec)) {
        (void)n2vcd_dec_service_handle(dec, from, buf, (uint32_t)n);
        return;
}
/* a close notice: */ n2vcd_dec_service_client_gone(dec, from);
/* every loop iteration (<= 1 s): */ (void)n2vcd_dec_service_tick(dec, now_ms());
/* HELLO_OK must advertise dec_codecs = 3 once the decoder is built in. */
/* enable the decode path's write-combining once per model (DEC04): n2vcu_set_write_combining(vcu, 1); */
```

Semantics that match the encoder path exactly: one session at a time (`E_BUSY`, detail = the active
id), every reply echoes `seq`/`session`, errors are `n2vcd_simple` with `h.type = N2VCD_ERROR`, seq 0
is reserved for unsolicited events, a client that vanishes releases its session, and a guest stop
answers `E_GUEST` + detail, notifies the other clients with seq 0 and calls `fatal` (which must
reply/log and `_exit(70)`).

Output buffers are client `RWVC_DMA_SHARED` buffers; `dma_info` is the daemon's wrapper around
`n2vcd_srv_dma_info` (RWVC_DMA_INFO).  On the host socket transport there is no pa; pass a resolver
that returns `-ENOSYS` and the service accepts `pa = 0` (the AP side needs a pa only on the phone).

## Protocol notes

`DEC_ADD_BUFFER` carries two plane handles; `c_handle == 0` is the normal one-buffer form (Y at 0,
C at `stride * buf_h`) and matches `n2ap_vdec.c`'s `fb_c_handle == fb_handle` branch.  A separate
second buffer is accepted too.  `DEC_PICTURE_OK` carries the display size (`width`/`height`), the
coded pitch (`stride`), the aligned height (`buf_h`), the format (`MM21`) and the vendor POC.
Crop origin (vsi.crop left/top) is not on the wire; the AP adapter refuses a stream with a nonzero
origin rather than shifting the client's detile silently (all measured streams are 0,0).

## Tests

```
bash gpu/hwcodec/daemon/dec/run_tests.sh
```

stages the rev-2 header in `/tmp` until it lands, then:

- unit suite, ASan+UBSan -O1, -O2 and TSan: client/service/fake AP, every message, buffer limits,
  double release, bad sizes, seq 0, truncated datagrams, wrong/missing replies, idle drop, client
  gone, guest stop, resolution change, buffer exhaustion, 320x240/1920x1080 geometry literals;
- end-to-end: two ffmpeg streams (checked with ffprobe), a real `dec_testd` process, the client over
  `SOCK_SEQPACKET`, picture data read back from the shared buffer files (the daemon copied nothing),
  display order and pts, flush drain, 1080p geometry, daemon killed mid-session, idle drop, and a
  guest-stop daemon exiting 70;
- arm64-apple-ios16.0 compile of the four product sources.

Optional, not part of the acceptance run (the stand-ins belong to DEC01):

```
bash gpu/hwcodec/daemon/dec/test/run_adapter.sh
```

links the AP adapter against DEC01's real `ap/n2ap_vdec.c` and its scripted daemon stand-ins and
drives INIT/header/add_buffer/frame/picture/release/resolution-change/flush/DEINIT (54 checks).

Products in `/tmp/n2hw-DEC05` only.
