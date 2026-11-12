# P03 status — client library, transports, phone hardware back end, CLI

Last update: 11 November 2026, 16:25.  Host-tested only: **no phone access, nothing published**.
Commits: `1908a5a` (package), `19c7b5e` (P04 helpers + docs).

## Implemented

| Deliverable | State | Notes |
|---|---|---|
| `client/n2vc_hw_dev.c` | done | `n2vc_hw_open_dev()`; each method is one `RWVC_*` ioctl on `/dev/redwood-vcodec`; EINTR retried; one `RWVC_INFO` at open validates the ABI; `RWVC_REGS.poll_timeout_us = 1000000`; every failure is `-errno`. |
| `client/n2vcd_transport.c` | done | Relay: `RWVC_MSG_SEND/RECV`, `RWVC_DMA_ALLOC` with `RWVC_DMA_SHARED`, `RWVC_DMA_RW/FREE` on `/dev/redwood-vcodec-client`. Socket: `SOCK_SEQPACKET`, buffers in `<socket>.buf/<decimal handle>`, 0600 + `ftruncate`; host-only (`-ENOSYS` on iOS). |
| `client/n2vcd_server.h`, `.c` | done | `n2vcd_srv_open_socket/open_relay/recv/send/dma_info/close`, API-compatible with `daemon/n2vcd_srv.h` (accept, recv-from-any with round-robin fairness, close notices, `-EMSGSIZE` for a consumed oversized datagram, `-EAGAIN`/`-EPIPE` send results, `RWVC_DMA_INFO` relay-only). |
| `client/n2vcd_client.h`, `.c` | done | `n2vcd_connect` (HELLO/version), `n2vcd_connect_path` + `n2vcd_status_errno` (path-based connect and 0/-errno results for P04's adapter), `n2vcd_enc_open` (allocates the `OPEN_OK` buffers), `n2vcd_enc_frame` / `_ex`, `n2vcd_enc_set`, `n2vcd_flush`, `n2vcd_close`, `n2vcd_disconnect`; sequence and session checked on every reply; 5 s default receive timeout; `N2VCD_E_*` mapping; session-fatal `E_GUEST`/`E_SESSION`; one mutex per client (thread-safe per session). |
| `client/n2vcd_cli.c` | done | `enc in.yuv WxH fps bitrate out.h264 [--socket|--dev]`, `info`; NV12 reader, per-frame line `N2VCD CLI frame N bytes=B key=K hw_us=U`, summary line, exit 0 only on a complete Annex B stream, exit 2 on usage/input errors. |
| `client/test/t_client.c` | done | 179 checks: recording fake ioctl (every request/struct compared with literals from `redwood_vcodec.h`), EINTR/error paths, socket transport and buffer files, server helper both transports, and a scripted fake vcodecd for happy path, errors, short replies, wrong seq, silence, seq-0 watchdog, oversized out_bytes, local cap reject, and two threads on one session. |
| `client/test/cli_fake_server.py` | done | Scripted daemon for the real CLI: literal replies, verifies the NV12 input buffer byte for byte, writes canned AUs, JSON result. |
| `client/test/run_tests.sh` | done | Three C builds (ASan+UBSan -O1, -O2, TSan -O1), CLI end to end, arm64-apple-ios16.0 compile gate with `-Werror`. |

## Tested (host only)

```
$ bash gpu/hwcodec/client/test/run_tests.sh
== build asan ==
== run asan ==
checks: 179, failures: 0
N2TEST P03-TEST PASS 179/179
== build o2 ==
== run o2 ==
checks: 179, failures: 0
N2TEST P03-TEST PASS 179/179
== build tsan ==
== run tsan ==
checks: 179, failures: 0
N2TEST P03-TEST PASS 179/179
== build n2vcd_cli ==
== n2vcd_cli info ==
== n2vcd_cli enc ==
== n2vcd_cli usage ==
== arm64-apple-ios16.0 compile check ==
N2TEST P03 PASS 550/550
```

What that means:

- 179 checks in each of the three builds (the same sources; ASan+UBSan aborts on the first UB, TSan on a data race).
- The fake ioctl compares the *input bytes* of every request (`RWVC_INFO`, `POWER`, `REGS`, `WAIT_IRQ`, `IRQ_CLEAR`,
  `DMA_ALLOC/FREE/RW`, `DMA_INFO`, `MSG_SEND/RECV`) with struct literals built from `include/redwood_vcodec.h`,
  checks the encoded request numbers, EINTR retry and error returns.
- The scripted vcodecd checks HELLO/OPEN/ENC_FRAME/ENC_SET/FLUSH/CLOSE bytes (including all reserved fields), the NV12
  packing for an unpadded (650) and a padded (656) source stride, the geometry `650x482 -> stride 656, in 488064`, the
  access-unit copy, the wrong-seq/short/silent/seq-0/oversized/guest-stop and local-cap paths, and 40 frames from two
  threads with each thread's output matching its own pts.
- The CLI test runs the real binary against `cli_fake_server.py`: exact stdout (3 lines), exact `.h264` bytes,
  `info` line, usage exit code 2.
- The iOS gate compiles all five sources for `arm64-apple-ios16.0` with the gpu/video SDK stub and `-Werror`; it is
  **compile only**, not a link of a product and not a run.

Mutation checks (not part of the suite): changing the client stride from `align16(width)` to `width` makes 24 checks
fail; dropping `RWVC_DMA_SHARED` from the relay allocation makes 1 check fail.  The tests are not vacuous.

## Cross-package integration (host)

- **vcodecd end to end, real daemon process:** I ran a copy of `daemon/test/run_tests.sh` with only its output
  directory redirected to `/tmp/n2hw-P03/ip/test` (hw-ip's files untouched).  It builds hw-ip's `vcodecd` plus
  `fake_vhost.c` and runs its `scenario_p03_cli` with this package's `n2vcd_cli`.  Result:
  `N2TEST IP PASS 717/717` (235/235 ASan+UBSan, 235/235 -O2, 235/235 TSan, 12/12 real n2vhost) at the time of
  DONE; a re-run after the final P03 changes against hw-ip's 16:25 tree gave `N2TEST IP PASS 722/722`
  (237/237 x 3 configs, 11/11 real n2vhost) with the same CLI result.  The CLI log in the -O2 run of the first:
  ```
  N2VCD CLI frame 0 bytes=1577 key=1 hw_us=59
  N2VCD CLI frame 1 bytes=10 key=0 hw_us=47
  N2VCD CLI done frames=2 bytes=1587 key=1 avg_hw_us=53
  ```
  This proves the socket transport, the `<socket>.buf/<handle>` convention and vcodecd's `--dma-dir` interoperate,
  and that the CLI drives a real protocol implementation, not only my fake.
- **P04's VideoToolbox adapter:** P04's DONE rev 1 includes a step that compiles these client sources and links them
  with its four `vt/` objects through the default seam; it reports `P03 real-client link+run 2` with the nine weak
  names resolved (registration 3/3, `n2hwenc_vt_available() 0` on the host without a relay).  So the final header is
  integrated by the consumer, not only by my tests.
- **iOS build:** `n2vcd_cli` links for arm64-apple-ios16.0 against gpu/video's TBDs with no undefined symbols (69888
  bytes, `NOUNDEFS`).  `bash gpu/hwcodec/build_ios.sh /tmp/n2hw-P03/ios` currently fails at the **vcodecd** link on
  three symbols referenced by the current IV archive (`_pthread_key_create`, `_pthread_setspecific`, `_strnlen`);
  they are exports of the SDK's `libSystem.B.tbd` but absent from `daemon/sdk_symbols.txt` (hw-ip's file).  With them
  appended to a throwaway supplemental TBD, the same link produces a 271504-byte arm64 `vcodecd` with `NOUNDEFS`,
  including this package.  Reported to hw-ip; no client source change is involved.

## Not done / unresolved

1. **No phone run.**  Nothing here has touched `/dev/redwood-vcodec*`; the relay and the hardware back end are
   verified against a recording ioctl only.  The main session owns the first device test.
2. **`n2vc_hw.regs()` has no timeout parameter** in the frozen header.  The back end uses the kernel maximum
   (1 s per poll) and the kernel batch budget is 2 s; if the VENC command lists need a different value, the interface
   needs a new field or a private setter (posted to hw-lead/hw-ip).
3. **`n2vcd_server.c` duplicates hw-ip's `n2vcd_srv_*` names.**  It is API-compatible and tested, but vcodecd keeps
   its own implementation until hw-ip decides to switch; both files must not be linked into one binary.
4. **CLI is H.264 only** (as specified by `out.h264`); HEVC is available through the library.  The CLI does not expose
   `ENC_SET`/`FLUSH` (the library does; the C test covers them).
5. **No decoder API.**  `N2VCD_DEC_*` is not wrapped; vcodecd answers `E_UNSUPPORTED` in v1.
6. **Second OPEN after E_BUSY** is not retried automatically; the caller retries.  The fake-server test only checks
   that a retry after the server closed fails cleanly (`E_SESSION`/`E_HW`).
7. **`n2vcd_enc_frame` requires `cap >= session->out_bytes`.**  A caller with a smaller destination buffer gets
   `E_ARGS` before the frame is sent; allocate the `OPEN_OK.out_buf_bytes` size (what the CLI and the intended plug-in
   do).

## Review findings

- R2 **F26** (low): the client header repeated a daemon claim ("vcodecd exits ... launchd restarts it").  Fixed in
  `a5415cd`: `n2vcd_client.h` and `README.md` now state only the client rule (close the session, fall back to software
  for this session, retry with a fresh `n2vcd_connect()` at the next StartSession; the old client must not be reused).
  No code change.
- R2 **F27** (low): a `calloc(session)` failure after OPEN_OK returned without CLOSE.  Fixed: all three post-OPEN_OK
  failure paths use `close_opened_session()`; a test for the injection is not practical without a product allocation
  hook, so this one is fixed by inspection.
- R2 **F28** (low): reply sessions are now checked centrally in `xact()` for every typed (non-ERROR) reply; OPEN_OK
  carries the new id and is the one documented exception (`N2VCD_ANY_SESSION`), validated by `n2vcd_enc_open()`.
  New fake-server scenario: a reply for session 43 to a session-42 request returns `E_SESSION` and kills the client.
  ERROR replies are still accepted regardless of session because the daemon's OPEN `E_BUSY` reply carries the
  *existing* session id, not 0.
- R2 **F29** (low): `n2vcd_transport_relay()` now probes `RWVC_INFO` and returns `-EPROTO` for an ABI mismatch
  (tested, including the fd close) instead of failing later at HELLO.
- R2 re-ran rev 2 (`N2TEST P03 PASS 550/550`) and **accepted P03 with no further finding**; F26-F29 are closed.


## Files

```
client/n2vc_hw_dev.c
client/n2vcd_transport.c
client/n2vcd_server.h
client/n2vcd_server.c
client/n2vcd_client.h
client/n2vcd_client.c
client/n2vcd_cli.c
client/n2vc_ioctl.h
client/README.md
client/STATUS.md
client/test/t_client.c
client/test/cli_fake_server.py
client/test/run_tests.sh
```

Total 176 KB, no file over 300 KB; build products under `/tmp/n2hw-P03` only.
