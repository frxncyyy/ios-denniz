# 20 — `/dev/redwood-vcodec` read-only identification and relay policy

**Goal:** prove the VENC/VDEC character device answers `RWVC_INFO`, that minor
1 enforces the client policy (no power, no registers), and that the relay
reports "no server" correctly. No codec register is written.

## Steps

On the phone, as root, with **no `vcodecd` running**:

```sh
/usr/local/bin/rwvc_probe info
/usr/local/bin/rwvc_probe client
```

Kernel log: fetch with `python3 gpu/build/autotest/phone.py logs` after the
commands.

## Expected

```text
N2VC info abi=1 powered=0x0 pool_pa=0xda800000 pool_bytes=90177536 pool_free=90177536 irq_mode=0 server=0
N2VC verdict PASS
N2VC client info abi=1 server=0 irq_mode=0
N2VC client RWVC_MSG_SEND no server -> errno 6 (No such file or directory)
N2VC client RWVC_MSG_RECV timeout 0 -> errno 35 (Resource temporarily unavailable)
N2VC client RWVC_POWER on minor 1 -> errno 13 (Permission denied)
N2VC client RWVC_REGS on minor 1 -> errno 13 (Permission denied)
N2VC client verdict PASS
N2VC verdict PASS
```

Errno numbers are the XNU ones; the pass criterion is the `verdict PASS` and
the error class, not the exact number.

Kernel log: no `NORD2 VCODEC:` line at all. The startup line was printed once
at boot (`00-preflight.md`); no power/register line is expected here.

## Pass criteria

- `rwvc_probe info` exits 0, `abi=1`, `powered=0x0`, `irq_mode=0`,
  `server=0`.
- `rwvc_probe client` exits 0 and every line above is present (or
  `SKIP relay checks` if a server is genuinely running).
- No `NORD2 VCODEC:` error line appears after the test.

## If it fails

| Symptom | Meaning |
|---|---|
| `RWVC_INFO` ENOTTY | the node is not the 0299 device |
| `abi != 1` | kernel/user header drift; report to hw-ik |
| `powered != 0` | something powered a block before the read-only test; stop it and reboot |
| `client` returns ENOTTY | minor 1 is not the relay node |
| `client` EACCES on INFO | minor-1 policy is stricter than `redwood_vcodec.h` |
| `client` SEND succeeds with no daemon | `server_present` is stale; check `RWVC_INFO` |

After this test, run `30-venc-power.md` (it powers each block once and reads
one word) and then `10-jpeg.md` (the JPEG device). Do not run both at once.
