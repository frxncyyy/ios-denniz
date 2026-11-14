## 2026-11-14 ~20:45 CAMERA PREVIEW COST (Magnifier 10-14 fps): cause in CMCapture's sizing + camera bundle camprev1 + image q10jcam1 (host build + tests only, NOT on the phone) -- gpu/build/camprev-20261114
Cause [V-log perf1a + V-disasm 20H392 dsc]:
- Magnifier = AVCaptureSessionPresetPhoto (12MP device format 4096x3072, PreferredPreview 1504x1128) + a preview layer + a video data output (VDO)
  that it configures itself (MagnifierSupport 0x200f1ced8: automaticallyConfiguresOutputBufferDimensions NO; later deliversPreviewSized NO): the
  VDO is given the format's whole 4096x3072 (rotated 3072x4096). CMCapture's _csp_videoCaptureDimensionsFromConnectionConfigurations (0x1a1fcb14c)
  sizes the stream's video capture output (= our PrimaryScaler) as max(VDO nonRotatedOutputDimensions, previewDimensions) -> 4096x3072 pool
  (log: 'PrimaryScaler: the graph's own buffer pool: 4096x3072'). Camera.app (Photo preset, no big VDO) gets a 1504x1128 PrimaryScaler (hm1 log):
  the full-size path is specific to apps with an unsized VDO (Magnifier, scanners).
- Per frame then: our GPU ISP render of 4096x3072 (wait 39-42 ms, 'the ISP pass over ... places', noise 2), the processor's 1504x1128
  SecondaryScaler from it (20 ms CPU), VideoToolbox 'Video Data Converter/Rotator/Zoomer' (VTMTSComputeFunction 48x128 groups of 16x16 = 3072x4096 at
  4x2 px/thread, 39.7 ms mean incl. queueing) + 'Preview Scaler' (18x47 = 1128x1504). Sensor: mode 0 4096x3072 at 16.5 frames/s made (60 ms frames,
  dark scene AE), 10/s read because the pictures thread takes ~100 ms.
- Sensor modes [V-code]: kernel 0340 exposes only 0 = 4096x3072/30, 1 = 2048x1152/60 (16:9), 2 = 4096x2304/60: no 4:3 binned mode; not needed
  for this fix (the ISP crops+scales on the GPU; output size is what costs).
Fix camprev1 (camera bundle, rwcam_diag.c, in the boot image's audio.pkg): in mediaserverd, right after FigCaptureSessionConfiguration
  initWithXPCEncoding: (the existing config_init wrapper), every FigVideoCaptureConnectionConfiguration whose sink is a
  FigCaptureVideoDataSinkConfiguration and whose output is 4:3 (+-1 %) and > 6 MP gets outputWidth/outputHeight halved (16-aligned width, even
  height): 4096x3072 -> 2048x1536 (portrait 1536x2048), 4032x3024 -> 2016x1512, front 3088x2320 -> 1536x1152. The setters are plain ivar stores
  (0x1a1eed404) and nonRotatedOutputDimensions reads the same ivars, so CMCapture then sizes the PrimaryScaler 2048x1536 by itself.
  Expected [I]: ISP render ~4x cheaper (~10 ms), VT converter 2048x1536 -> 1536x2048 (no up/downscale, ~4x fewer pixels), CPU SecondaryScaler from a
  4x smaller picture; GPU busy in Magnifier from 87 % to roughly 30-40 %, UI queue waits back to the Maps-like range. Zoom quality unchanged above
  2x (the bridge zooms on the sensor picture; at 9.7x the ISP pass was over 434x326 places anyway); at 1x the VDO is 1536x2048 instead of 3072x4096
  (still above the 1080-wide screen).
  Not touched: StillImage output (own 4096x3072 pool -> still4096 / GNR / flash path unchanged), films (16:9 formats, IntermediateTap), movie file
  outputs, Camera.app (no such VDO), VDOs that are already <= 6 MP or not 4:3.
  Switches (files, read once per mediaserverd): /var/db/nord2-camera-fullvdo = as asked (kill), /var/db/nord2-camera-vdo-quarter = 1/4 (1024x768).
  Log: 'redwood: camera: preview: a video data output of 3072x4096 made 1536x2048 (1/2; ...)', then 'PrimaryScaler: the graph's own buffer pool: 2048x1536'.
- Build: control build of the camflash userland copy is bit-identical to the phone's 924feca7; camprev1 audio.pkg 255352 B (same size, < 256 KiB)
  sha256 e86a64f28a32730042215477385315d2c41fa036afca5dcd3072470399176ff0. Host: run_tests.sh all suites pass + new T_VDO (the function as built,
  extracted from rwcam_diag.c: 18 fixed cases incl. kill switch / films / square / tiny, + sweep of all sizes 16..8192 step 8: smaller, aligned,
  same shape within 1 %).
- Image q10jcam1 = q10jperf1 recipe (same pieces 0560..0640, all rebased pieces identical to perf-20261114/integ/p, same MORE_PATCHES/MORE_DIRS,
  same commandline) with the camprev1 package: device-mmu-q10jcam1-candidate/ui-sched/nord2-ios-metal-q10jcam1-ui654fb60dvfs-cap3500-ram5088-sched.img
  15198208 B sha256 5b91de8ee19beb25c9d66dd511b5d8984443af866179957874cdeadbbe330466; FAILURES none, known 0399 IMAGECHECK stop (BUILD_EXIT 1),
  SCANPATH_OK, WIFI_MEMBERS_OK, 'audio package replaced ... e86a64f2'. No kernel source change (the bundle lives in the boot image only; the
  kernel's audio_install rewrites the rootfs copy every boot, so a payload cannot replace it).
- Payloads (optional switches, camflashoff pattern): camvdofull (kill), camvdoquarter (test), each -undo and roundtrip, owned-*.json (1 path each);
  rehearse-vm.sh (current mirror): mirror root ba2f4e2f: offline install camvdofull / camvdoquarter EXIT 0, catalog 1 changed, outside_owned_paths []; undos on the mirror
  no-op ("nothing to undo", 0 outside owned); VM round trips install / again (already) / undo (removed) / again (nothing) EXIT 0, mirror unchanged.
  Transaction (not executed by me): camprev-20261114/txn_camprev.sh (copy of txn_camflash.sh; P=camvdofull|camvdoquarter, UNDO=1) -- not needed
  for the default: q10jcam1 alone carries the fix.
- Tool: camprev_check.py LOG (shrink lines, pools, PrimaryScaler pictures/s + made-in, ISP output sizes, VTMTS dispatch table, mediaserverd slow
  commits, frames read/made; verdict CAMPREV_ACTIVE / CAMPREV_NOT_SEEN / CAMPREV_FULL_POOL_AFTER_SHRINK). Baseline perf1a: PrimaryScaler
  4096x3072 10.0/s made in 79.4 ms, VTMTS 48x128 132 x 39.7 ms, 18x47 130 x 43.2 ms, mediaserverd slow commits 263 x 52.3 ms, CAMPREV_NOT_SEEN.
What to read in the next capture (Magnifier ~40 s, Camera photo + flash photo + video 10 s): camprev_check.py: CAMPREV_ACTIVE, pool 2048x1536,
  VTMTS grid 24x64 (expected) instead of 48x128 and its ms, PrimaryScaler made-in (expect ~20-30 ms) and pictures/s (sensor-limited 16/s dark,
  up to 30/s bright); n2perf.py Magnifier phase fps / GPU in flight / queue wait; StillImage pool still 4096x3072 and the photo in Photos 4096x3072;
  any 'PixelTransferFailure' / node errors in 'diag: flow' lines (would mean a sink disliked the new size -> camvdofull). Only the user can judge
  Magnifier's picture quality.
Not done: splitting VT/our ISP compute dispatches into smaller GPU jobs (Nord2Metal/kernel scheduler: no preemption, one job per slot) -- with
  4x fewer pixels each job should drop to ~10 ms; revisit only if queue waits stay > 20 ms. A 4:3 binned sensor mode (2048x1536) would cut the
  RAW upload/ISP further but needs a kernel imx766 mode table entry (OnePlus driver has no 4:3 binning mode in 0340's list) -- not attempted.
