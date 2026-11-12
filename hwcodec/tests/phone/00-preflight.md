# 00 — boot, nodes, read-only preflight

**Goal:** prove the 0299 kernel made the three nodes and that opening them and
asking `RWVC_INFO` / `RWJ_INFO` touches no codec register.

## Steps

1. Flash the exact 0299 boot image prepared by the main session (boot
   partition only), boot iOS, wait for the autotest channel:

   ```sh
   cd gpu/build/autotest
   python3 phone.py wait
   python3 phone.py logs /tmp/n2hw-T02/phone/boot.log
   ```

2. On the phone, as root (the guest runner does this for you):

   ```sh
   ls -l /dev/redwood-*
   /usr/local/bin/rwvc_probe info
   /usr/local/bin/rwvc_probe client
   /usr/local/bin/rwjpeg_tool info
   ```

## Expected

Kernel log, once, from `nord2_input_init`:

```text
NORD2 VCODEC: /dev/redwood-vcodec made, /dev/redwood-vcodec-client made, JPEG VEN sharing on (VENC 0x17020000, VDEC 0x16000000; pool 0xda800000+0x5600000; polled, irq_mode 0)
NORD2 JPEG: /dev/redwood-jpeg made (major N, 0666), 24 MiB source + 16 MiB output at 0xd8000000
```

No `NORD2 JPG:` or `NORD2 VCODEC:` register line at boot.

Node list:

```text
crw-rw-rw- ... /dev/redwood-jpeg
crw------- ... /dev/redwood-vcodec
crw-rw-rw- ... /dev/redwood-vcodec-client
```

`rwvc_probe info` (minor 0):

```text
N2VC info abi=1 powered=0x0 pool_pa=0xda800000 pool_bytes=90177536 pool_free=90177536 irq_mode=0 server=0
N2VC verdict PASS
```

`rwvc_probe client` (minor 1, no `vcodecd` running):

```text
N2VC client info abi=1 server=0 irq_mode=0
N2VC client RWVC_MSG_SEND no server -> errno 6 (No such file or directory)
N2VC client RWVC_MSG_RECV timeout 0 -> errno 35 (Resource temporarily unavailable)
N2VC client RWVC_POWER on minor 1 -> errno 13 (Permission denied)
N2VC client RWVC_REGS on minor 1 -> errno 13 (Permission denied)
N2VC client verdict PASS
N2VC verdict PASS
```

`rwjpeg_tool info`:

```text
N2JPG info abi=1 max_pixels=12582912 jobs=0 caps=0x1
```

## Pass criteria

- Three nodes exist with the modes above.
- `N2VC info abi=1`, `powered=0x0` (nothing has been powered yet).
- `rwvc_probe client` ends `N2VC client verdict PASS`; this proves the relay
  answers ENXIO/EAGAIN and that minor 1 cannot power or touch registers.
- `N2JPG info abi=1`, `caps=0x1`, `jobs=0`.
- No `NORD2 VCODEC:` error line in the kernel log.

## If it fails

| Symptom | Meaning |
|---|---|
| a node missing | K01/K02/IK start path failed; the boot log line says which `devfs_make_node` failed |
| `open: Permission denied` on minor 0 | not root; run the probe as root |
| `N2VC info` fails ENOTTY | wrong image or an old `/dev/redwood-vcodec` |
| `rwvc_probe client` EACCES on INFO | minor 1 policy differs from `redwood_vcodec.h`; report to hw-ik |
| `server_present=1` with no daemon | a stale `vcodecd` is running; stop it before the relay checks |
