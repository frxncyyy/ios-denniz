# 50 — recovery (main session only)

These tests power VEN/VDE and touch larb4/5/7/8. If the phone stops answering,
recover with the last known-good boot image; never boot or flash stock
Android over the iOS `userdata`.

## If the phone still answers USB

```sh
cd gpu/build/autotest
python3 phone.py status
python3 phone.py logs /tmp/n2hw-T02/phone/last.log
python3 phone.py reset fastboot
```

The log slice names the last `NORD2 VCODEC:` line (mux, gate, protection,
power timeout, larb port) and therefore which block was being powered.

## If the phone does not answer

1. Hold power until the phone resets; it should return to LK fastboot (the
   daily kernels arm the fast-boot bit, but a hard reset is the fallback).
2. Flash the last good boot image **to `boot` only**:

   ```sh
   cd gpu/build/autotest
   ./flash_q10at.sh <last-good-image> recovery
   ```

   `q10ik` and `q10hw1` are the known-good images named in `STATUS.md`.
3. If fastboot is not reachable, use the RAM ADB rescue described in
   `AGENTS.md`/`NOTES.md`.

## Never

- `fastboot flash boot` of a stock Android image: iOS is in `userdata`.
- `fastboot erase`/`format` of `userdata`, `persist`, `modem`, `bootloader`,
  `recovery` or the GPT.
- A second codec test in the same boot after a `NORD2 VCODEC:` timeout: power
  state may be half-applied; reboot first.
