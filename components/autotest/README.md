# Nord 2 hands-free testing (autotest, kernel patch 0296)

Drive the phone and see its screen from the PC while iOS runs, with nobody touching it: synthetic touches (one or two
fingers), hardware keys, pictures of the panel, the kernel log, and a reset back to LK fastboot for the next flash.

Everything goes over the USB cable, through the kernel's existing "Nord2 XNU console" device (the one
`tools/nord2-usb-console.py` reads). No daemon on the phone, no entitlements, no Wi-Fi, no change to the root file system.

## Use

    cd gpu/build/autotest
    python3 phone.py wait                 # until the phone answers (after a flash/boot: about 20 s)
    python3 phone.py status               # JSON: kernel side state, log bytes
    python3 phone.py launch camera        # wake, unlock/home, Control Center, Camera button, "Cancel" on the location question
    python3 phone.py screenshot a.png --scale 4          # 270x600; --scale 2 = 540x1200; --scale 1 = 1080x2400
    python3 phone.py screenshot b.png --scale 1 --crop 0,340,1080,1440    # a part of the panel at full resolution
    python3 phone.py shutter              # tap the shutter / record button      (--key: volume-up instead)
    python3 phone.py lens 0.5             # 0.5 | 1 | flip (front/back)
    python3 phone.py mode video           # or photo; reads the current mode from the screen, swipes, checks (mode state: just read)
    python3 phone.py shutter --watch 8 --out dir/x   # press, then watch the thumbnail up to 8 s; saves x-thumb-before/after.png
    python3 phone.py pinch 540 1000 200 800 --ms 700     # centre x y, finger distance from -> to (pixels): zoom in
    python3 phone.py pinch 540 1000 800 200              # zoom out
    python3 phone.py focus 540 900        # tap in the viewfinder
    python3 phone.py tap X Y | press X Y --ms 800 | swipe X0 Y0 X1 Y1 --ms 300 | drag X0 Y0 X1 Y1
    python3 phone.py key power|volup|voldown|menu [--ms 80]
    python3 phone.py home | wake | sleep | unlock | abort
    python3 phone.py logs out.log [--seconds 30]         # appends what the kernel printed since the last call
    python3 phone.py reset fastboot       # sync, reset; the phone is in LK fastboot about 15 s later
    python3 phone.py scenario photo runs/my-test --wait-s 5   # scripted run: screenshots + a log slice per step + steps.jsonl
    python3 phone.py scenario list        # built in: photo, lenses, zoom, video; or give a file (one step per line)

Coordinates are panel pixels, 1080x2400, origin top-left; a pixel of a `--scale S` screenshot is `S` times that
(`--shot S` converts for you, `--pt` takes iOS points of the 375x812 layout). `phone.py --help` lists everything.

Camera positions used by the verbs (iOS 16.7 Camera, iPhone X layout on this panel, PHOTO mode): shutter 540,2080;
0.5x 480,1748; 1x 600,1748; flip 968,2084; VIDEO label 332,1912 (PHOTO label 752,1912 when VIDEO is selected);
Control Center's Camera button 164,1468; "Cancel" of the location question 744,1372.

A typical unattended test:

    python3 phone.py wait && python3 phone.py launch camera
    python3 phone.py screenshot 1.png --scale 4
    python3 phone.py shutter; sleep 3
    python3 phone.py screenshot 2.png --scale 4; python3 phone.py logs run.log
    python3 phone.py reset fastboot       # then flash the next image; `fastboot reboot` alone boots the same one again

## What is in the kernel (0296-nord2-autotest.patch)

- `bsd/dev/nord2_autotest.inc` (new, included by `sweet_touch.c`): one kernel thread runs the commands. Fingers 8 and 9
  and the keys go through `sweet_hid_touch()` / `sweet_hid_key()`, the calls the FT3518 driver itself makes, so iOS cannot
  tell them from the real panel. Vendor IN requests 0x70 status, 0x71 register, 0x72 command, 0x73 picture bytes (the
  file's head comment is the protocol). Commands: gesture, key, picture, abort, reset, log rewind.
- `iokit/Kernel/arm/SweetFramebuffer.cpp`: `AppleCLCD::snap()` copies the panel buffer the overlay is scanning out
  (what is really shown, after all of backboardd's layers), subsampled and/or cropped, as R,G,B or run-length packed.
- `bsd/dev/sweet_usb.c`: calls `nat_usb()` where it called `nal_usb()` (which `nat_usb` asks first). The archive's
  `sweet_usb.o` is a CI object nobody recompiles, so `build_p1_candidate.py` (the q10hq script + this step) renames that one
  undefined symbol in a copy of the member; the patch shows the equivalent source line.
- Reset: the daily kernels arm the RTC fast-boot bit at every boot ("NORD2 AUTOFB") but never reset by themselves; command 5
  syncs the file systems and does the TOPRGU reset of `sweet_reset_now()`.
- Log rewind: the live log (sweet_early.c, 4 MiB, append-only) is full after about 9 minutes of Camera. Command 6 sets its
  size back to 0 when the host has read all of it. The size word is static in an object that is not recompiled; its address
  is taken from the code of `nord2_log_stream_info()` only when that code is exactly the known seven instructions and the
  word reads what the accessor returns (otherwise the command answers "not possible" and touches nothing).

Build: `./build_q10at1.sh` (env `NAME`, `SERIES`, `EXTRA_PATCH_DIRS`, `AUDIO_PKG`) = the q10hq/q10ic recipe + `0296`.
For any other image tonight: add `--patch-dir gpu/build/autotest` and `0296` to the series and use this directory's
`build_p1_candidate.py` (it needs `0278` in the series; `patched-sources-0296` -> `kernel/b` satisfies its source check).
0295 is left free. Host tests: `test/run_tests.sh` (command engine against a made-up kernel, picture packer; ASan/UBSan).
Flash (boot only, phone in fastboot): `./flash_q10at.sh IMG TAG`.

## Scenarios

`phone.py scenario NAME|FILE OUTDIR` runs one step per line, each as its own phone.py call, and goes on after a failed step
(exit code 1 at the end if any failed). A line is any phone.py verb with its arguments, or `sleep S`,
`shot NAME [--scale N] [--crop X,Y,W,H]` (-> `OUTDIR/NN-NAME.png`), `logs` (-> `OUTDIR/NN-log.txt`, what the kernel printed
since the previous `logs`, also appended to `OUTDIR/kernel.log`). `{wait}` is replaced by `--wait-s` (default 5). Lines
starting with `#` are comments. `OUTDIR/steps.jsonl` has time, duration, exit code and output of every step.

## Verified on the phone / not

Run `runs/q10at1` (2026-11-11 01:52-02:01, image q10at1 sha 9d496ecd, 0296 version 1), screenshots in `runs/q10at1/shots`:

| what | state |
|---|---|
| channel up after boot (`wait`, `status`) | verified: 22 s after the flash |
| `key power` (wake / sleep) | verified |
| screenshot, subsampled, plain | verified (lock screen, home, Control Center, Camera); scale 4 takes 10 s |
| tap, swipe (unlock, page, Control Center) | verified: 13 gestures, none refused |
| `launch camera` | verified ("exec Camera" in the log, Camera on screen); its automatic "Cancel" of the location question is NOT verified (the question was answered by hand-typed taps in that run and did not come back) |
| `shutter` (tap) | verified: "StillImage: picture N of the photograph asked for (4032x3024)" in the log |
| `lens 0.5`, `lens flip` | verified (0.5x highlighted; "Front Camera" stream in the log) |
| `pinch` (two fingers) | verified: zoom label 1x -> 1.7x |
| `mode video` | verified (VIDEO selected). Recording itself did not start on that image (timer stayed 00:00:00): a Camera matter, not looked into here |
| `logs` | verified up to the 4 MiB the version-1 kernel keeps (full after about 9 min of Camera) |
| `reset fastboot` | verified: fastboot 14 s later |
| `shutter --key`, `key volup/voldown/menu`, `press`, `drag`, `focus`, `home`, `mode photo`, `--pt`, `--shot` | NOT run on the phone (same code path as the verified gestures/keys) |
| version 2 (run `runs/q10at4`, 02:34-02:47, image q10at4 sha 5739401c = q10at3 kernel d83fa42e + audio package a9dafe36): packed screenshots | verified: 270x600 of Camera in 6 s (was 10), 540x1200 in 22 s (was about 40) |
| `--crop` | verified: mode strip 1080x100 at full resolution in 0.6 s; thumbnail 156x156 in 0.2 s |
| log rewind | verified: "live log size word found", `log_rewinds` 1, the log went on from 0 and later lines were read |
| `mode photo` / `mode video` / `mode state` (read from the screen, swipe, check) | verified: PORTRAIT -> PHOTO -> VIDEO -> PHOTO, one move each. SLO-MO / TIME-LAPSE / PANO as start not tried |
| `launch camera` with the automatic "Cancel" of the location question | verified (`location_dialog_dismissed: true` on the first launch after boot) |
| `shutter --watch N --out PREFIX` and `scenario photo` | verified to run; the thumbnail did not change in that run (no photo reached the library on that image), so the "changed" branch is NOT verified |
| scenarios `lenses`, `zoom`, `video` | NOT run |

## Things to know

- There is no Camera icon in SpringBoard or the App Library on this phone (MobileGestalt `still-camera` is 0), and the
  lock screen's swipe-left / camera button do nothing with synthetic or real touches of this kind (the button wants a force
  press). Control Center's Camera button is the way in; `launch camera` does that.
- The lock screen turns the panel off after about 8 s; a screenshot then says `"panel_off": 1`. Inside Camera it stays on.
- USB EP0 moves about 50 KB/s (64-byte packets, the kernel's USB loop sleeps 1 ms per packet). Plain 270x600 = 10 s.
  Version 2 packs runs of equal pixels (UI screens shrink several times; a noisy viewfinder does not) and can send a part.
- `presents_during_copy` >= 2 in the screenshot's JSON means the frame may be torn (phone.py already retried twice).
- One command at a time: a second `phone.py` gesture while one runs is retried until the first is done.
- Anyone on the USB cable can now type on the phone and reset it. This is a development kernel; do not ship 0296.
