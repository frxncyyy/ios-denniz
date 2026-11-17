# Integrating the Nord2 audio-ID candidate (milestone 1)

Scope: one default-off, read-only diagnostic (`nord2audioid=1`). There are no Wi-Fi or battery
code edits and no new ADT nodes; the IOKit, PMIF and boot-image layout are unchanged.
Everything below is for the parent to run. **This worker ran none of it.**

Paths: `W` = `research/nord2/audio/claude-opus`, `BT` = `research/ci-39158169212/buildtree`
(the tree that `tools/nord2-mkpatch.sh` and `tools/nord2-xnu-cc.py` use).

## 1. Copy the canonical sources

| From (`W/candidate/…`) | To (repo) |
|---|---|
| `shim/nord2-xnu/audio_id.h` | `shim/nord2-xnu/audio_id.h` |
| `shim/nord2-xnu/audio_id.c` | `shim/nord2-xnu/audio_id.c` |
| `shim/nord2-xnu/audio_id_bind.inc` | `shim/nord2-xnu/audio_id_bind.inc` |
| `tools/test-nord2-audio-id.py` | `tools/test-nord2-audio-id.py` |
| `tools/fixtures/nord2-audio-i2c-sim.h` | `tools/fixtures/nord2-audio-i2c-sim.h` |
| `tools/fixtures/nord2-audio-id-core.c` | `tools/fixtures/nord2-audio-id-core.c` |
| `tools/fixtures/nord2-audio-id-bind.c` | `tools/fixtures/nord2-audio-id-bind.c` |

The names do not collide (`nau_`/`nab_` prefixes are unused in the snapshot).

## 2. Host tests (before touching XNU)

```sh
python3 tools/test-nord2-audio-id.py          # expect: PASS nord2 audio-id core
                                              #         PASS nord2 audio-id native binding
                                              #         PASS nord2 audio-id core and native binding
CROSS=1 CLANG=<apple-or-upstream clang> python3 tools/test-nord2-audio-id.py   # + arm64 freestanding compile
NORD2_AUDIO_VERBOSE=1 python3 tools/test-nord2-audio-id.py                     # shows the simulated log lines
```
The fixtures were written and hand-traced but never executed. Any failure is real:
report it back to the audio worker rather than relaxing a check.

## 3. Apply the call sites and regenerate 0082

`W/candidate/patch/audio-id-integration.diff` has three parts:

1. `bsd/dev/sweet_touch.c` (3 added lines) and `bsd/conf/files` (1 line): apply in `BT/xnu`:
   ```sh
   cd research/ci-39158169212/buildtree/xnu
   patch -p1 --dry-run < ../../../nord2/audio/claude-opus/candidate/patch/audio-id-integration.diff
   ```
   `patch` will also see the `tools/` hunk; apply the bsd hunks here and the tools hunk in
   step 2. Splitting the file first is fine. The line numbers were computed from 0082 hunk
   offsets; offsets are expected if sweet_touch.c moved. The semantic placement is:
   - `#include "audio_id_bind.inc"` immediately after `#include "wifi_wfsys_bind.inc"`
     (needs `ni_rd/ni_wr/ni_mask/ni_gpio_bit/ni_level`, `ni_gpio`, `ni_lt`);
   - `if (!nab_prepare_return()) { return -1; }` in `nord2_input_prepare_return()` after
     `nw_restore()` and before the `ni_saved` early return;
   - `nab_audio_id();` in `ni_thread()` after `ni_wifi_power_probe();` and before the
     touch-pin configuration loop.
2. `tools/nord2-mkpatch.sh` (repo root): add `audio_id` to the helper loop and NEW_FILES, and copy
   `audio_id_bind.inc`.
3. Run `tools/nord2-mkpatch.sh`. It copies the helpers into `BT/xnu/bsd/dev`, wrapping
   `audio_id.c` in `#if defined(NORD2)`. It then regenerates 0082 and verifies that the patch
   reproduces the buildtree.

## 4. Local compile

```sh
tools/nord2-xnu-cc.py research/ci-39158169212/buildtree bsd \
  research/ci-39158169212/buildtree/xnu/bsd/dev/audio_id.c /tmp/audio_id.o
tools/nord2-xnu-cc.py research/ci-39158169212/buildtree bsd \
  research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_touch.c /tmp/sweet_touch.o
```
New external symbols: none. The binding uses only `ml_io_map`, `delay`, `PE_parse_boot_argn`,
`printf` and `panic`, which sweet_touch.c already uses. `audio_id.c` needs `<stdint.h>`
and may emit a `memset` call for its struct zeroing (the same idiom as `wifi_wfsys.c`).

Suggested linked spot checks, matching existing practice: `nab_pins_take` writes only
GPIO 0x30/0x130 SET/CLR (bits 26/27), MODE 0x3f0 nibbles 2/3 and IOCFG_BM 0x70 bits
3/13; `nau_send` rejects bytes outside the allowlist; `nau_recover` loops at most 18 times;
`nab_audio_id` returns before any mapping when `nord2audioid != 1`; and a full scan for
RCpc2 instructions (as for every Nord2 build).

## 5. CI and image

- CI as usual (Apple clang, `xnu.yml`). Record the run ID and kernel SHA-256 in NOTES.md.
- Image: the **current baseline command line plus `nord2audioid=1` only**. Keep the
  audio diagnostic in an image of its own, separate from ROM_DONE or the battery
  work, so any change in behaviour can be attributed.
  It requires `nord2input=1 nord2stopat=23` (already in the baseline). It is
  independent of all `nord2wifi*` arguments.
- Boot-image budget: the code adds a few KiB of text; check the spare figure as usual.
- This is a new image, so the usual exact-image human authorization applies. Without the
  boot-arg the code performs zero callbacks (tested), so it can also ride along
  disabled in other images.

## 6. Capture and hand-back

In the native USB log, collect every line beginning `NORD2 AUDIO` (about 21 lines; format in
AUDIO-REPORT.md §5.3), together with the boot's image SHA. They contain chip revision,
status and ADC values only, which is safe for the public report. Interpret them with
AUDIO-REPORT.md §5.4.

Expected effect on the phone: touch starts about 10-20 ms later (one-time). No change to
display, UFS, USB, Wi-Fi or keys. If `halt` or `busy` is reported, Linux return is
refused (`nord2_input_prepare_return` → -1). iOS continues, because I2C8 has no iOS
user.

## 7. What not to do
- Do not apply the `aud_gpio_i2s1_*` pinctrl states: GPIO111 is the TFA@0x34 reset.
- Do not add MT6359 audio register access on the input thread: it shares PMIF with keys
  and the radio rails.
- Do not escalate to M2/M3 writes before the M1 log has been reviewed.
