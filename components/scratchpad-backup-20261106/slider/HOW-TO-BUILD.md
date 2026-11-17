# 0279 alert slider -> iOS ring/silent switch

Files:
- `0279-nord2-alert-slider-ringer-switch.patch`: apply with `patch -p1` after 0278.
  It touches `bsd/dev/sweet_touch.c`, the new file `bsd/dev/nord2_slider.inc` and
  `iokit/Kernel/arm/hid/SweetHID.cpp`.
- `build_p1_candidate-0279.diff`: needed by the builder, which does not copy `hid/` into
  its stage and does not recompile `SweetHID.cpo` without it.
- `patched-sources-0279/`: the three files after the patch, for review only.
- `test/slider_host_test.c`: a host test with a fake I2C9 bus and IST8801/MXM1120 models.
  Run it with `cc -fsanitize=address,undefined test/slider_host_test.c`.

To add it to a build (example: q10ck -> q10cl):

    O=$W/gpu/build/device-mmu-q10cl-candidate; mkdir $O
    cp $W/gpu/build/device-mmu-q10ck-candidate/{build_p1_candidate.py,pack_with_args.py} $O/
    (cd $O && patch -p1 < $SCRATCH/slider/build_p1_candidate-0279.diff)
    sed 's/q10ck/q10cl/g' $W/gpu/build/device-mmu-q10ck-candidate/build_q10ck.sh > $O/build_q10cl.sh
    # in build_q10cl.sh: append ,0279 to S (after 0278) and add
    #   --patch-dir $SCRATCH/slider
    # next to the bb1d and audiofix --patch-dir arguments.

0279 requires 0278 in the series, because only the 0278 branch recompiles
`sweet_touch.o`; the builder diff asserts this. The receipt then also contains
`SweetHID.cpo`.

Runtime:
- The slider is on by default when the NORD2 INPUT thread runs (`nord2input=1`,
  `nord2stopat=23`). Boot-arg `nord2slider=0` disables it and leaves GPIO120/121
  untouched.
- Log lines to check:
  - `NORD2 INPUT: pin=120/121 mux=...`: the original mux, which should be 1 (SCL9/SDA9).
  - `NORD2 SLIDER: IST8801|MXM1120 ...`: the probe result.
  - `NORD2 SLIDER: initial <pos> up=.. down=.. -> iOS ringer on|silent`: the boot state.
  - `SWEET HID: ringer on|silent (N clients)`: the HID event dispatch.

## Update 2 (after the q10cm hardware run): SpringBoard ignored the switch

Cause: SpringBoard's `-[SpringBoard _ringerChanged:]` calls
`-_updateRingerState:withVisuals:updatePreferenceRegister:`. That method first
calls `MGGetBoolAnswer("ringer-switch")` (SpringBoardUI 0x1f0f17378) and returns
at once when the answer is false. libMobileGestalt answers `ringer-switch`
(table entry `hx2qJfJRLZ9Sseb37IcQow`, getter 0x19c5664d0) with "does
IODeviceTree:/buttons have a property named `function-button_ringerab`"
(name hash `Ah5yfSlY2yycIQb92HbmrA`). The real D22AP ADT has that property; the
Nord2 ADT (playback.adt /buttons) does not.

Kernel fix, in this patch: `SweetHIDButtons::publishRingerSwitch()` adds
`function-button_ringerab` to IODeviceTree:/buttons. It runs at buttons-service
start, which is before launchd. Expected log line:
`SWEET HID: IODeviceTree:/buttons function-button_ringerab present`.

Rootfs step, required once: MobileGestalt keeps a persistent answer cache, and
`ringer-switch` has a cache slot in it. Delete
`/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist`
in the same rootfs delta as the kernel. MobileGestalt rebuilds it at the next boot.
Otherwise a cached "false" can keep SpringBoard ignoring the switch.

Check on device: SpringBoard logs "SBRingerControl setRingerMuted: ..." on each
change, mediaserverd logs "-CMSMDevState- ... Ringer switch changed to: OFF
(Device is silenced)", and the Silent Mode HUD appears.
