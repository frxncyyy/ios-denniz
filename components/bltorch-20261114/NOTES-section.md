
## 2026-11-13 q10jch on the phone: slider dead + very dim, no flashlight button -- causes, 0501 / 0502, guest bltorch1, image q10jci (host build only, NOT on the phone) -- gpu/build/bltorch-20261114
- Evidence (scratchpad hf1_cap/xnu-live.log, first 9 min of q10jch): `NORD2 BL: AppleARMBacklight published`, backboardd + SpringBoard match it, ONE
  `brightness-nits 0.00 nits -> DBV 16`, `NORD2 BL SUM ... requests 2 sends 3 fails 0` (the 2 = brightness-nits + IOMFBBrightnessLevel, both 0; the extra sends are
  re-sends after display on), nothing more over 4 display off/on cycles and the user's slider moves. `NORD2 TORCH: /dev/redwood-torch made`, never opened.
- (a) brightness, cause [V-disasm CoreBrightness 20H392, bright-fix-20261114/re/dcwid.dis 0x1af0d88bc..0x1af0d8a18]: DisplayCreateWithID reads the backlight
  service's property "new-backlight-architecture" and takes the nits path only if it is kCFBooleanTrue (display+0x20 = 1); otherwise it starts the legacy
  brightness thread (display+0x24 = 1). 0495 did not publish that property. [V-disasm cb.dis ___DisplaySetPhysicalBrightnessLegacyOnThread 0x1af0ee798, key array
  ___brightnessKeys 0x1ddc6a820 decoded from the binary] the legacy path sends IORegistryEntrySetCFProperties(backlight, {"brightness": SInt32 =
  ceil(level*factor*(max-min))+min from IODisplayParameters.brightness, "FadePeriod", "backlight-level"}); [V-code] AppleARMBacklight::setProperties of 0495
  returned success for it and used nothing. [V-disasm] initial factor = (value != 0), initial level = (value-min)/(max-min) = 1024/2047.
  [I] the single 0.00-nits write = a side path that writes the never-computed nits value (PLCEnable handler 0x1aefea5e8 -> SetBLDriverNitsValueIfNotInLPM(display+0x44c);
  the exact caller cannot be told from a kernel log). [I] that the slider reached the legacy writer in that run (0495 logged nothing for it).
  NOT the cause: automatic brightness (MobileGestalt "ambient-light-sensor" = an IOService with PrimaryUsagePage 0xff00 / PrimaryUsage 4, none here, cached false;
  DisplaySetProperty: "Refuse to turn on AAB without ALS"), sandbox / entitlement (the nits write did arrive), IOMFB selectors.
- 0501 (SweetFramebuffer.cpp, sweet_fb_backlight.h): "brightness" 0..2047 (the published scale) = DBV, in both architectures; boot-arg n2bl 1 (default) = legacy
  architecture: nits keys counted and not used; n2bl=2 or n2gdv +64 (n2gdv=73 fits the line, " n2bl=2" does not) = "new-backlight-architecture" published,
  nits are the level (untested fallback). Safe floor kern.n2bl_safe = DBV 300 until ONE request above it has been seen (then kern.n2bl_floor 16 as before): a
  requester stuck at 0 can no longer leave a dark panel; no request at all = the panel keeps LK's level. FadePeriod is logged, not followed (the level jumps).
  New logs: `NORD2 BL ARCH:`, `NORD2 BL: brightness N of 2047 (FadePeriod F) -> DBV D (floor ..)` (first 24 changes), `NORD2 BL: <key> x nits not used` (<= 4),
  `NORD2 BL: property "<name>" ... accepted, not used` (<= 12), `NORD2 BL SUM: ... requests R (brightness X, nits Y of them Z not used) sends S fails F`;
  kern.n2blstat has arch / req_raw / ask / safe / trusted / raw_reqs / nits_ignored.
- (b) flashlight, cause [V mirror + disasm]: /System/Library/ControlCenter/Bundles/FlashlightModule.bundle/Info.plist UIRequiredDeviceCapabilities =
  [camera-flash, arm64] (the module IS in ModuleConfiguration.plist's module-identifiers); +[AVFlashlight hasFlashlight] = MGGetBoolAnswer("camera-flash");
  libMobileGestalt key jdLgKT+0BZmGrzd9J0cuPA -> 0x19c563598 = "IODeviceTree:/product/camera has a property flash". The port's /product (copied from D22AP) has the
  properties but none of the D22AP child nodes (camera, facetime, maps, haptics, audio). AND MGCopyAnswer answers from its cache first (sub_19c557110: CacheData =
  594 slots x 8 bytes + valid bytes at 0x1290; slot = u16 at +0x9a of the 0xa0-byte table entry): camera-flash = slot 0x3f, in the phone's cache valid 1 value 0.
  [V-disasm ControlCenterServices] +[CCSModuleMetadata _requiredCapabilitiesForInfoDictionary:] reads UIRequiredDeviceCapabilities;
  -[CCSModuleRepository _queue_filterModuleMetadataByGestalt:] asks MGCopyMultipleAnswers and keeps a module only if its set is a subset of the true answers.
  Open point: the same cache says still-camera / front-facing-camera / rear-facing-camera = 0 although Camera is on the home screen -> either those icons do not
  depend on it or the cache is not used by every process; both levers are provided for camera-flash.
- 0502 (SweetFramebuffer.cpp start): makes IODeviceTree:/product/camera { flash = 1 } unless boot-arg n2torch=0 (as Nord2Baseband's /baseband node). Log
  `NORD2 TORCH CAP: IODeviceTree:/product/camera made, flash = 1`. The ADT / packer are unchanged (adt sha == q10jch).
- Guest (rootfs, NOT applied): block-flash1.py = the one cache byte (slot 0x3f value 0 -> 1, file size/owner/mode kept, refuses another build/layout), receipt
  /private/var/db/nord2-stock/flash1.json; block-bright1.py = BKEnableALS false in mobile's com.apple.backboardd.plist (BKSLocalDefaults ALSEnabled; defensive
  only), receipt bright1.json (holds the file as it was). payload-bltorch1 (both) / payload-flash1; undo guests. VM on the mirror of 22:33 (root 1aeac006...):
  round trips (install / no-op / undo / no-op) RT_UNDO_DIFF 0, byte_identical True for both; offline rehearsals + catalog check outside_owned_paths [].
  One transaction: `bltorch-20261114/txn_bltorch.sh KERNEL_IMG TAG` (P=flash1, UNDO=1); needs the user's go.
- Flashlight after the button exists [V-disasm CMCapture, not on hardware]: AVFlashlight -> XPC com.apple.coremedia.flashlight (in mediaserverd's MachServices
  in the mirror) -> flashlight_setupDevice: +[BWFigCaptureDeviceVendor sharedCaptureDeviceVendor] (the method rwcam replaces) copyDeviceForClient +
  copyStreamForFlashlightWithPosition:1 deviceType:2 (back wide) -> stream setProperty TorchLevel (+ HighCurrentTorchEnabled if supported) -> rwcam torch_set ->
  /dev/redwood-torch (the installed audio-q10jae.pkg has it). No camera-userland change is needed for the button; whether the LED lights is the first hardware
  test of that chain (0496 MT6360 writes included).
- Host: test/run.sh BLTORCH_HOST_TESTS_OK (bl2_test.c: the q10jch boot replayed = nothing sent, safe floor / trust, both architectures, thermal cap, sysctl
  remaps); merge_check.sh MERGE_CHECK_OK (0491 0492 0494r 0495 0496 0500 0501 0502 regenerated identical, io_submap call sites unchanged); CI-flag compile of
  SweetFramebuffer.cpp 1410 warnings = q10jch, same messages; ml_io_map relocations unchanged.
- Image q10jci = q10jch recipe + 0501 + 0502 (EXTRA_ARGS n2gdv=9; boot line and ADT identical to q10jch):
  device-mmu-q10jci-candidate/ui-sched/nord2-ios-metal-q10jci-ui654fb60dvfs-cap3500-ram5088-sched.img 15136768 B
  sha256 8bd70072e060a5dc24ce315f3f37f144e607f7ad018661d28d4c1076d96596b1; KERNEL OK, FAILURES none, the known 0399 IMAGECHECK stop (BUILD_EXIT 1), SCANPATH_OK
  (same SCANPATH_EXTRA_ALLOWED as q10jch, rerun by hand), WIFI_MEMBERS_OK, cmdlen 603 (4 free), markers of 0501 / 0502 in the kernel (by hand).
  Rebuild: cd gpu/build && SCANPATH_EXTRA_ALLOWED='<the 0496 list>' EXTRA_ARGS="n2gdv=9" MORE_PATCHES="0491 0492 0494 0495 0496 0500 0501 0502"
  MORE_DIRS="$PWD/wifi-fix-20261114 $PWD/ccperf-20261114 $PWD/gpuas-20261114/r0494 $PWD/bright-fix-20261114 $PWD/gpuas-20261114 $PWD/bltorch-20261114"
  bash integ-20261114/run_integ.sh full <name> $PWD/device-mmu-<name>-candidate.  Fallback image if "brightness" never arrives: EXTRA_ARGS="n2gdv=73".
- NOT verified: anything on hardware. The slider needs only the kernel (no transaction); the flashlight button needs the kernel AND the flash1 cache byte.
  Not done: fades (FadePeriod), automatic brightness (no ALS), Camera flash.
