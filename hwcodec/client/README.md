# gpu/hwcodec/client — vcodecd client library, transports, phone back end, CLI

P03's package.  Everything here is user space; nothing in it runs on the phone by itself.  Interfaces are the frozen
`include/n2vcd_proto.h` (datagrams), `include/n2vc_hw.h` (hardware back end) and `include/redwood_vcodec.h`
(`/dev/redwood-vcodec` ioctls).

| File | What |
|---|---|
| `n2vc_hw_dev.c` | `n2vc_hw_open_dev()`: the phone hardware back end.  One `RWVC_*` ioctl per `struct n2vc_hw` method on `/dev/redwood-vcodec` (minor 0); EINTR retried; open validates `RWVC_INFO.abi_version`. |
| `n2vcd_transport.c` | `n2vcd_transport_relay()` (kernel relay on `/dev/redwood-vcodec-client`: `RWVC_MSG_SEND/RECV`, `RWVC_DMA_ALLOC` with `RWVC_DMA_SHARED`, `RWVC_DMA_RW/FREE`) and `n2vcd_transport_socket()` (host `SOCK_SEQPACKET`, buffers as files). |
| `n2vcd_server.h/.c` | server side of both transports for vcodecd, API-compatible with `daemon/n2vcd_srv.h` (accept / recv-from-any / send-to-client / close notice / `RWVC_DMA_INFO`). |
| `n2vcd_client.h/.c` | `n2vcd_connect`, `n2vcd_connect_path`/`n2vcd_status_errno` (path connect + 0/-errno for P04), `n2vcd_enc_open`, `n2vcd_enc_frame(_ex)`, `n2vcd_enc_set`, `n2vcd_flush`, `n2vcd_close`, `n2vcd_disconnect`; sequence numbers checked, every receive bounded (5 s default), errors mapped to `N2VCD_E_*`, one mutex per client (a session is safe to drive from several threads). |
| `n2vcd_cli.c` | `n2vcd_cli enc in.yuv WxH fps bitrate out.h264 [--socket PATH | --dev PATH]` and `n2vcd_cli info`.  The phone test tool. |
| `n2vc_ioctl.h` | the one `ioctl` declaration iOS needs, and the test-only injection point (`-DN2VC_TEST_HOOKS`). |
| `test/` | `t_client.c` (recording fake ioctl + scripted fake servers), `cli_fake_server.py`, `run_tests.sh`. |

## Using the client library

```c
struct n2vcd_transport t;
if (n2vcd_transport_relay(&t, NULL) != 0)            /* phone: /dev/redwood-vcodec-client */
        ...;
struct n2vcd_client *c;
char err[256];
if (n2vcd_connect(&c, &t, 0, err, sizeof(err)) != N2VCD_E_OK) { t.close(t.ctx); ... }

struct n2vcd_enc_params p = {
        .codec = N2VCD_CODEC_H264, .encoder = 1,
        .width = 1920, .height = 1080, .fps_num = 30, .fps_den = 1, .bitrate = 8000000,
};
struct n2vcd_session *s;
if (n2vcd_enc_open(c, &p, &s, err, sizeof(err)) != N2VCD_E_OK) ...;

uint8_t *out = malloc(n2vcd_session_out_bytes(s));
uint32_t bytes; int key;
int r = n2vcd_enc_frame(s, y, uv, 1920, 1920, pts, 0, out, n2vcd_session_out_bytes(s),
                        &bytes, &key);
(void)r;
n2vcd_close(s);
n2vcd_disconnect(c);
```

- `n2vcd_connect()` copies the transport; **on success the client owns it** and `n2vcd_disconnect()` closes it.  On
  failure the caller still owns it and must close it.  `n2vcd_connect_path(&c, socket_path, dev_path, ...)` creates
  the transport (both NULL = phone relay) and owns it in both directions; `n2vcd_status_errno()` maps an `N2VCD_E_*`
  value to 0 or the closest negative errno for callers that prefer that convention.
- Buffers: `n2vcd_enc_open()` allocates the input and output DMA buffers the server asked for in `OPEN_OK` (shared
  pool buffers on the phone, files under the host transport's directory).  The caller never sees the handles.
- Input is NV12: luma `width x height` at `y_stride`, chroma interleaved UV `width/2 x height/2` pairs (width bytes
  per row) at `chroma_stride`.  Both are re-packed to `align16(width)` inside the buffer.  With
  `y_stride == n2vcd_session_stride(s)` the plane is copied in one call, so the source must then be contiguous for
  `(rows-1)*stride + width` bytes.
- `out` must have room for `n2vcd_session_out_bytes(s)`; a smaller `cap` is refused with `N2VCD_E_ARGS` *before* the
  frame is sent.  `out == NULL` encodes without copying.  `n2vcd_enc_frame_ex()` also reports `hw_us`, `key_frame`,
  `header_bytes`.
- Every receive waits at most 5 s (`n2vcd_connect()` argument, or `n2vcd_set_timeout()`); silence is `N2VCD_E_HW` with
  `n2vcd_last_errno() == ETIMEDOUT`.
- Error replies are returned as their `N2VCD_E_*` status; `n2vcd_last_detail()` carries the daemon's detail (the n2a32
  stop code for `E_GUEST`).
- A guest stop (or a session error, or a seq-0 watchdog datagram) is session-fatal: later calls on that session return
  `N2VCD_E_SESSION`, and `n2vcd_close()` only releases local resources.  The caller should fall back to software for
  that session and reconnect with a fresh client at the next StartSession; the old client must not be reused.
- A malformed, short or out-of-sequence reply marks the whole client dead (`E_SESSION` afterwards): a
  one-reply-per-request stream cannot be resynchronised.  The caller reconnects.

## Transports

**Relay (phone).**  `/dev/redwood-vcodec-client` (minor 1), one ioctl per call; opening it probes `RWVC_INFO` and
refuses an ABI mismatch with `-EPROTO`.  `RWVC_DMA_ALLOC` always sets
`RWVC_DMA_SHARED`, so vcodecd (minor 0) can read the buffer and look its physical address up with `RWVC_DMA_INFO`.
`EAGAIN` from SEND is backpressure (the client library retries it inside its timeout); `EINTR` is retried by the
transport.  The kernel may deliver `ENXIO`/`EPIPE` when the daemon goes away; that becomes `E_SESSION`.

**Socket (host).**  `SOCK_SEQPACKET` at the given path, one datagram per message.  Buffers are the files
`<socket path>.buf/<decimal handle>` (directory created 0700, files 0600 and `ftruncate`d to the requested size).  That
is the host equivalent of the kernel pool: vcodecd's existing `--dma-dir <socket path>.buf` makes its file-backed DMA
read the same files, so no daemon change is needed.  Darwin has no `SOCK_SEQPACKET` for `AF_UNIX`; on iOS
`n2vcd_transport_socket()` returns `-ENOSYS` (the phone always uses the relay).

**Server helpers** (`n2vcd_server.h`): same names and semantics as `daemon/n2vcd_srv.h`, so hw-ip can replace its
file with these two without touching vcodecd's protocol code.  `n2vcd_srv_recv()` returns a positive byte count, `0`
for a close notice, `-EAGAIN` on timeout (including "only a new accept happened"), `-EMSGSIZE` for an oversized
datagram that was consumed.  `n2vcd_srv_send()` returns `0`, `-EAGAIN` (dropped, client kept) or `-EPIPE` (client
dropped).  `n2vcd_srv_dma_info()` is relay-only (`RWVC_DMA_INFO`) and `-ENOSYS` on the socket transport.

## CLI

```
n2vcd_cli enc in.yuv WxH fps bitrate out.h264 [--socket PATH | --dev PATH]
n2vcd_cli info [--socket PATH | --dev PATH]
```

`in.yuv` is raw NV12, frames back to back (1.5·W·H bytes each, no header).  `fps` is `30` or `30000/1001`; `bitrate`
is bits per second with optional `k`/`K` (1000) or `M` (1000000).  The default transport is the relay; `--socket` is
host-only and mutually exclusive with `--dev`.

stdout, one line per frame:

```
N2VCD CLI frame 0 bytes=1577 key=1 hw_us=59
N2VCD CLI done frames=2 bytes=1587 key=1 avg_hw_us=53
```

Exit codes: `0` the whole input was encoded and the output is a complete Annex B stream (every access unit starts
with `00 00 01`/`00 00 00 01`, last byte non-zero); `1` client/daemon/hardware failure; `2` usage or bad input file.
`hw_us` comes from the daemon (`ENC_FRAME_OK.hw_us`: its wall-clock encode time; the AP IPI result has no hardware
timestamp).  Only H.264 is offered by the CLI; HEVC is available through the library.

## Testing

```
bash gpu/hwcodec/client/test/run_tests.sh
```

Builds everything under `/tmp/n2hw-P03`, three variants (ASan+UBSan -O1, -O2, TSan -O1), then the CLI end to end
against `cli_fake_server.py`, then compiles all five sources for arm64-apple-ios16.0 with `-Werror`.  See
`STATUS.md` for the current result and known gaps.

## Known limits

- No decoder API: protocol v1's `DEC_*` requests are not wrapped (the daemon answers `E_UNSUPPORTED`).
- The socket transport is host-only by design; `SOCK_SEQPACKET` does not exist on Darwin.
- The relay path has been verified in host tests against a recording ioctl, not on a phone.
- `n2vc_hw.regs()` has no poll-timeout parameter in the frozen header; the back end uses `RWVC_REGS.poll_timeout_us =
  1000000` (the kernel maximum).  A batch of several polls is still bounded by the kernel's 2 s batch budget.
