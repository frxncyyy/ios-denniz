# gpu/hwcodec/client/dec — decoder client library (DEC05)

`n2vcd_dec.h/.c` is the `n2vcd_dec_*` client used by DEC06 (and the decoder CLI when it exists).  It
speaks `include/n2vcd_proto.h` rev 2 over a frozen `struct n2vcd_transport` (P03's relay on the
phone, any host transport in tests) and owns one decoder session.

```c
struct n2vcd_dec_client *c;
if (n2vcd_dec_connect_path(&c, socket_path /* host */, NULL /* phone relay */, 2000, err, sizeof err))
        ...;
struct n2vcd_dec_session *s;
uint32_t in_bytes;
n2vcd_dec_open(c, N2VCD_CODEC_H264, 0, 0, 0, &s, &in_bytes, err, sizeof err);

struct n2vcd_dec_geometry g;
n2vcd_dec_header(s, au0, au0_len, &g);          /* SPS/PPS(+first slice); not consumed */
n2vcd_dec_add_buffers(s, &g);                   /* g.dpb_frames Y+C buffers of g.out_buf_bytes */

struct n2vcd_dec_input_info in;
n2vcd_dec_input(s, au, au_len, pts, 0, &in);    /* the input buffer is free again on return */
struct n2vcd_dec_pic pic;
while (n2vcd_dec_picture(s, &pic) == 1) {
        /* copy/detile from pic.y_handle (Y at 0, C at g.y_bytes) into the CVPixelBuffer */
        n2vcd_dec_release(s, pic.index);
}
```

See `daemon/dec/STATUS.md` for what is and is not tested.  The library is host-tested (ASan/UBSan,
-O2, TSan) and compiles for arm64-apple-ios16.0; it has not run on the phone.

Notes:
- Errors are `N2VCD_E_*`; `n2vcd_dec_status_errno()` maps them to 0/-errno for VideoToolbox.
- `E_GUEST`/`E_SESSION` are session-fatal: later calls return `E_SESSION` without touching the
  transport.  A malformed/out-of-sequence reply kills the whole client.  Reconnect and fall back to
  software for that session.
- `n2vcd_dec_buffer_rw(s, index, plane, offset, data, bytes, write)` is the only pixel path; the
  daemon never copies picture data.
- `n2vcd_dec_path.c` (P03 transports by path) is a separate object so tests that link only
  `n2vcd_dec.c` do not need P03's transport sources.
