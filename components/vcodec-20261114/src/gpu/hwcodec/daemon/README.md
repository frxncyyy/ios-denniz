# gpu/hwcodec/daemon -- vcodecd

The daemon that hosts the phone's VENC through IV's interpreter (`n2vhost`) and V41's AP-side encoder (`n2ap_venc`) and
serves it over the frozen `include/n2vcd_proto.h`. One encoder session at a time in v1; decoder requests answer
`N2VCD_E_UNSUPPORTED` until a VDEC AP side exists.

```
vcodecd [--fake] [--dev PATH] [--socket PATH] [--vendor DIR] [--record PATH]
        [--dma-dir DIR] [--hw-timeout-ms N] [--session-idle-ms N] [--selftest] [--no-console] [--help]
```

- `--fake`: T02's `n2vc_hw_open_fake` instead of `/dev/redwood-vcodec`; compiled only in the test build
  (`-DN2IP_TEST_HOOKS`), so the product binary never references the fake back end.
- `--socket PATH`: a SOCK_SEQPACKET UNIX socket instead of the kernel relay (host only; iOS has the relay only).
- `--vendor DIR`: default `$N2HW_VENDOR_DIR`, else `/private/var/db/nord2-vcodec/vendor`.
- `--dma-dir DIR`: test builds only (`-DN2IP_TEST_HOOKS`); DMA handle N is the file `DIR/N`, which is how the host test's
  client and daemon share the buffers the kernel would share on the phone.
- `--hw-timeout-ms`: encode wall-clock limit (default 2000); also the AP-side IPI wait bound.
- `--session-idle-ms`: drop a session with no request for this long (default 10000, 0 disables).
- `--selftest`: no hardware, no vendor files, no daemon state. Reserves the 4 GiB guest space, maps pages, runs
  1,000,000 interpreted A32+T32 instructions on two guest threads, provokes one guest fault and recovers, and checks
  `_Thread_local` per thread. Prints `N2VCD selftest PASS/FAIL <step>` to stdout and `/dev/console`; exit 0/1.
  This is the first thing the main session runs on the phone.
- `--gate PATH`: if PATH is absent, log one line and exit 0 (launchd runs the daemon at boot only after the
  transaction created the marker). A guest stop is fatal: answer `E_GUEST`, log one `fatal guest stop` line
  (address + `n2elf_describe` result), drop the power references and exit 70 so launchd restarts the daemon;
  hardware timeouts stay non-fatal.

## Protocol conventions beyond the header

- Every reply echoes the request's `seq` and `session`. `OPEN_OK` carries the new session id; `HELLO_OK` has session 0.
- **Any error is a `struct n2vcd_simple` with `h.type = N2VCD_ERROR`**, whatever the request was (OPEN, ENC_FRAME, ...),
  with `status` = `N2VCD_E_*` and `detail` = the errno, the n2a32 stop code, or a byte count, depending on the error.
- A guest stop detected while no request is in flight is sent **unsolicited** to the owning client with `seq = 0`,
  `type = N2VCD_ERROR`, `status = N2VCD_E_GUEST`, `detail` = the `enum n2a32_stop` value; every other client that
  has sent traffic gets the same event. seq 0 is reserved: a client request with seq 0 is refused with `E_ARGS`.
  The daemon then exits 70 (launchd restarts it); client libraries must treat `seq 0`/E_GUEST as session death and
  fall back to software.
- Buffers are client DMA handles allocated with the share flag; `OPEN_OK.in_buf_bytes` / `out_buf_bytes` are the sizes.
  The input is NV12 with luma at 0 and chroma at `stride * align16(height)`; `ENC_FRAME.stride` must equal
  `align16(width)` (the VENC's `buf_width`). The output buffer must have `out_capacity` >= `out_buf_bytes`; the daemon leaves
  64 KiB of headroom for parameter sets. `OPEN.profile`/`level` are H.264 `profile_idc`/`level_idc` (66/77/100, 10..62) and
  HEVC `profile_idc` (1/2/3) / `level_idc` (30*level, plus 3 for the .1 levels); 0 selects the default.
- `ENC_FRAME_OK.header_bytes > 0` means the access unit starts with SPS/PPS(/VPS) of that many bytes. In v1 the daemon
  opens the session in the stock **separate-header** mode (`seq_hdr_mode = 0`): immediately after
  `n2ap_venc_start()` it runs one `N2AP_ENC_SEQ_HDR` job into its own DMA buffer, caches the parameter sets, and
  prepends them (device-side move + write, `shift_up()`) before every IDR that does not already start with them.
  Nothing depends on the vendor's CPU-side joined-header memmove reading a fresh DMA mirror.
- `ENC_FRAME.force_key != 0` asks the AP side for an IDR through `VENC_SET_PARAM_FORCE_INTRA` before the picture.
- The timeout passed to `n2ap_venc_open` is the daemon's `--hw-timeout-ms` (default 2000), so a hung IPI wait
  is answered `N2VCD_E_HW` within the same limit the daemon checks.
- After `n2ap_venc_encode()` returns 0 bytes, the daemon calls `n2ap_venc_wait_done()` for the same bound: on the
  real GCE path the ENCODE is ACKed before the job runs and size/key arrive later in `list_free`.
- `FLUSH` is a no-op success: `n2ap_venc_encode` is synchronous, so no frame is ever queued.
- `ENC_SET` maps to `VENC_SET_PARAM_ADJUST_BITRATE` / `ADJUST_FRAMERATE` / `FORCE_INTRA`; 0 means "leave unchanged".
- `hw_us` is the wall time of the encode call: V41's IPI result does not carry a hardware timestamp.

The daemon object itself compiles for arm64-apple-ios16.0. With IV's `ios-lib` archive in `N2VHOST_IOS_LIB`,
`build_ios.sh` builds IV's archive and P03's client and reports `vcodecd: linked` plus `n2vcd_cli` on this tree.
`daemon/sdk_symbols.txt` carries every supplemental libSystem symbol.

`N2VCD ...` lines go to stderr and `/dev/console` (unless `--no-console`).

## Host test

`bash gpu/hwcodec/daemon/test/run_tests.sh` builds this source plus T02's fake `n2vc_hw`, the model in
`daemon/test/fake_vhost.c` (the real IV/V41 headers, a fake implementation), `daemon/test/t_daemon.c`, the interpreter
core and P03's `n2vcd_cli`, and runs the whole protocol against a real `vcodecd` process on a UNIX socket: HELLO/version,
OPEN/ENC_FRAME with a canned ffmpeg H.264 stream, parameter sets, the separate-header job at OPEN (the model strips
every in-band SPS/PPS, so only the cached header can make a key frame valid), asynchronous completion through
`n2ap_venc_wait_done`, ENC_FRAME force-key and ENC_SET force-key, ENC_SET bitrate/framerate and its bounds, FLUSH,
CLOSE, two clients (BUSY), a client killed mid-session, decoder and unknown requests, truncated/malformed/oversized
datagrams, seq 0, guest stop in a request and from the watchdog, a hardware timeout, V41 fault paths, 10000 random
datagrams, and a clean SIGTERM. It also re-assembles `daemon/test/selftest_{a32,t32}.s` with llvm-mc and diffs the
embedded bytes, then runs `vcodecd --selftest` in the ASan+UBSan and -O2 builds (not TSan: its fault bridge
`siglongjmp`s out of a SIGSEGV handler, which TSan rejects).
P03's real client library/`n2vcd_cli` then encodes a two-frame NV12 file through a second daemon
(HELLO/OPEN/ENC_FRAME/CLOSE, Annex B output checked).

The last section builds IV's real `libn2vhost.a` with IV's own Makefile and links the same `vcodecd.c` against it plus
the phone's extracted vendor set (read at run time), then `daemon/test/t_real.c` drives it. A missing IV build or vendor
set fails the suite unless `N2IP_ALLOW_SKIP_REAL=1` is set. `daemon/test/p03_absent.c` is the test-only
`n2vc_hw_open_dev` (-ENODEV) used by that link; it cannot reach a device. Everything is built under `/tmp/n2hw-ip`.
