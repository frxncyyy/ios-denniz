# Nord 2 camera userland (HakiOS port) - report

Built 2026-11-09 by a Sonnet subagent, compile- and host-tested only (never run on the phone).

## Deliverables (this directory)
- `out/RedwoodAudio.driver` (144 KB): audio plugin v8 unchanged (audio_plugin.o byte-identical to halplug/out-h) + camera code; links CoreFoundation, CoreMedia, CoreVideo, libobjc, libSystem by install name (CoreMedia/CoreVideo/libobjc weak).
- `out/audio.pkg` (145 KB, nord2-audio-package.py build, verify ok).
- `src/`: rwcam_device.c, rwcam_core.c, rwcam_init.c, rwcam_log.c, camera_isp.c, rwcam_abi.h (+ HakiOS MIT notice). The camera arms only from a constructor when /dev/redwood-camera exists.
- `sdk/lib/` TBD stubs (101/106 symbols verified in the 16.7 cache).
- `build_cam.sh` rebuilds bundle + package; `run_tests.sh` runs host tests (`test/t_isp.c`: RAW10 -> NV12/BGRA, 4 Bayer orders, black levels, clipping, AE, demosaic under ASan/UBSan; `test/t_core.c`: sensor-switching reader vs a fake kernel, TSan clean). 4096x3072 -> 1504x1128 NV12 preview: 24 ms on one host core.
- System bits prepared (not installed): `system/D22/AVCaptureSession.plist`, `CameraSetup.plist` (derived from Apple's: keep private), `out/gpu-isp.adt` (+ADT /arm-io/isp node for MobileGestalt still-camera), `../apps-20261104/guest-cam.sh` + `owned-cam.json` + `payload-cam/` (restores mediaserverd's capturesource/capturesession/cameraviewfinder MachServices from the nocap backup, installs the plists with .pre-cam backups).

## Verified against our iOS 16.7 cache (`tools/verify_cache.py`, `out/verify_cache.log`)
- Cache .dyldlinkedit/.symbols are APFS LZBITMAP-compressed (resource fork), decompressed for full exports.
- HakiOS' 16.5 addresses are stale: hooks resolved by class + selector + type encoding instead.
- +sharedCaptureDeviceVendor is nil here (looks for AppleH13CamIn/H10/H9/H6); vendor calls create as fn(kCFAllocatorDefault, NULL, &device).
- Derived tables: device +8 processing-session create, +0x10 request, +0x18 relinquish; stream +8 start, +0x10 stop, +0x18 add-attachments; session +8 prepare, +0x10 process, +0x18 flush.
- ~40 property-key literals match exported constants; 16.7 also reads MaxIntegrationTime and GeometricDistortionCorrectedRects (answered).
- Source list from D22/AVCaptureSession.plist (hw.target minus "AP"); streams matched by port type; back deviceType 7 = PortTypeBackSuperWide (ultra-wide, not in stock D22 plist: added). Module IDs Lens/Actuator/Integrator required: 1/1/1.

## What the kernel must provide (redwood_camera.h plus)
- /dev/redwood-camera present before mediaserverd loads the bundle.
- Header-sized read returns the next frame's header; full read header + stride*height.
- 0/EAGAIN/ETIMEDOUT retried (15 in a row = give up); EIO = fault.
- format 1 RAW10, bayer of cell (0,0), black in 10-bit steps, sensor 1/2/3; AE needs gain_unity, gain_max, frame_lines_min, margin, exposure_lines non-zero.
- REDWOOD_CAMERA_SENSOR (a = 1/2/3, b = ~0) accepted while open; stale frames dropped by the sensor field.
- Last close powers down; survives mediaserverd restart and reopen. KEEP unused.

## Risks
Not run on a device; three independent AVCaptureDevices (no virtual dual-wide); no AF/stills/video/torch; tele/dual/IR/TrueDepth removed from the plist; nominal lens data; wide/front keep iPhone X sensor IDs 0x0343/0x0354, ultra-wide 0x0355; mediaserverd (mobile) must be able to open /dev/redwood-camera rw.

## Ultra-wide lens in Camera.app (2026-11-10; built and host-tested, NOT run on the phone)
- Finding (CameraUI/AVFCapture/CMCapture 16.7 disassembly): every AVGestalt model with an ultra-wide camera also has the aggregate (dual-wide) camera, Camera.app then asks for BuiltInDualWideCamera in Photo and, when it is missing, falls back to BackWide WITHOUT a 0.5x button. CMCapture's dual-wide device needs a synchronized streams group with both streams delivering frames: not possible with one sensor at a time.
- Way taken: Camera.app's own single-camera path (`featureDevelopmentForceSingleCamera`, +[CAMZoomControlUtilities shouldEmulateWideDualCameraZoomForMode:...]): 0.5x / 1x change between the ultra-wide (`:5`, deviceType 7) and the wide (`:0`) AVCaptureDevice that the bundle already has.
- `src/rwcam_camui.c` -> `out/camui/libredwoodcamui.dylib` (`build_camui.sh`), for the Camera process: wraps -[CAMCaptureCapabilities initWithHostProcess:], forces single cameras, switches off what the iPhone 11 row adds to the session (night mode, deferred processing, responsive shutter, over-capture, stereo audio, new viewfinder). Markers in /var/db: nord2-camera-dualwide, nord2-camera-n104-all, nord2-camera-ctm, nord2-camera-noultrawide.
- Kernel has to (1) answer hw.model N104AP to the program Camera (q10hi does), (2) add DYLD_INSERT_LIBRARIES=/usr/lib/libredwoodcamui.dylib to its environment at exec (bsd/kern/kern_exec.c, next to OS_ACTIVITY_DT_MODE), (3) set bits 1 and 5 in the AMFI dyld policy answer for it (bsd/kern/sweet_amfi.c). Camera's executable must stay stock (a changed one loses its TCC entitlements: run q10gv).
- Bundle: the ultra-wide stream crops by `rwcam_view_zoom()` (rwcam_core.c): all of the sensor at zoom 1, the wide camera's view at zoom 2.0 (where the app changes device). Lens ratio 1.6 = 24 mm / 15 mm equivalent, NOMINAL; `/var/db/nord2-camera-uw-ratio` (thousandths, 1001..1999) sets it without a build.
- Root: `../apps-20261104/guest-camuw.sh` + `payload-camuw` + `owned-camuw.json` put the dylib in /usr/lib (undo: guest-camuw-undo.sh). No plist changes (system/D22 as installed by guest-cam3.sh).
- Tests: `run_tests.sh` (t_core: the zoom curve; t_camui: the dylib against a made-up Objective-C runtime).

## Dual-wide device: both back sensors at once (2026-11-10 evening; built and host-tested, NOT run on the phone)
Supersedes the single-camera fallback of the section above (libredwoodcamui.dylib / guest-camuw.sh are left in place, unused: they need a kernel mechanism nobody wants).
- CMCapture side: `system/D22/AVCaptureSession.plist` has the virtual entry `:6` (deviceType 8, `synchronizedStreamUniqueIDs` :0 + :5), shaped as the stock virtual `:3` dual entry (`tools/make_camera_plists.py`; `--no-dual` gives the three-camera file byte for byte). The device lists a `FigCaptureSynchronizedStreamsGroup` object (`rwcam_device.c`: class of FigCaptureSynchronizedStreamsGroupGetClassID, derived table version 1) with SupportedStreams, SynchronizationMaster, ActiveStreams, MasterConfiguration (+SupportedKeys), BaseZoomFactorsByPortType (ultra-wide 1, wide 2), Extrinsics (identity). Only when `/dev/redwood-camera2` exists (and not `/var/db/nord2-camera-nodual`).
- Readers: `rwcam_core.c` has one reader per kernel node. In the pair the shown camera is read whole; of the hidden one only the 120-byte headers (AE from the header's levels, white balance copied from the shown camera, frame buffer kept ready). `rwcam_pair.c` turns the group's ActiveStreams / MasterConfiguration / stream starts into that. No second node or a node giving up: one node, as before.
- Metadata: in the pair the frames get a MetadataDictionary (`stream_add_attachments`): PortType, SynchronizedStreamsIsMaster, AE state, and the hidden camera's state under SynchronizedStreamsSlaveMetadataByPortType -- what BWBravoStreamSelector needs to change up to the wide stream. `/var/db/nord2-camera-nometadata` switches it off.
- Pictures: `rwcam_render.c`, processor (always) or GPU (`rwa_isp_*` of libredwoodair.dylib, found at the first stream start; `/var/db/nord2-camera-cpu-isp`: processor only). The first GPU picture of a stream is compared with the processor's; over 24/255 the GPU is off for that stream. Frames are uploaded (rwa_isp_frame_memory, zero copy, is not used yet). `camera_isp.c` is unchanged (the GPU's arithmetic stays aligned).
- Root: `../apps-20261104/guest-camdual.sh` + `payload-camdual` + `owned-camdual.json` (the plist, and com.apple.camera preferences that switch off responsive shutter, capture/view outside the frame, auto low-light video, ...); undo `guest-camdual-undo.sh`.
- Tests (`run_tests.sh`): t_dual (two readers, fake two-node kernel, EBUSY, fallback), t_pair (the group's policy), t_render (backends), plus the earlier ones.
- Not verified without the phone: that CMCapture accepts the `:6` entry and the group object as they are, what else the dual graph asks of the streams, whether it enables the slave stream's outputs before it becomes master.

## Run q10hk (first with everything): what the log says (2026-11-10 night)
- The `:6` entry and the group were accepted: 5 sources, source 2 is deviceType 8 with `BravoSwitchOverVideoZoomFactors = (2)`; the group's six properties were read; no "Failed to open streams".
- Nothing of the pair, the second node or the GPU backend ran: every session was the FRONT camera's (the app's saved CAMUserPreferenceCaptureDevice = 1) and no stream was ever started.
- mediaserverd's "Can't add a nil node": a nil BWSISNode in the still image sink pipeline (scheme not 2; no NoiseModelParameters in the iPhone X's tuning data), in the two of three starts where Camera.app -- which crashed each time in the graphics library's shader compiler -- was gone before the still pipeline was built. Taken to be the dead client's source giving scheme 0 (not seen directly). `rwcam_diag.c` now says every scheme, makes one under 2 into 2 (`/var/db/nord2-camera-noschemefix`: says only) and says what each BWSISNode is made of.
- `../apps-20261104/guest-camback.sh` (+ undo): optional, makes Camera.app start on the back camera so that a run reaches the dual-wide device at once.

## Run q10hl-dual: -12787 at every start of the dual-wide session; a log that fills the console; the GPU no longer the default (bundle eaedaba3)

**The -12787 (verified by disassembly, consistent with the log).** `-[BWFigVideoCaptureStream initWithCaptureStream:attributes:sensorIDDictionary:
synchronizedStreamsGroup:...]` (0x1a207fd20) sets its byte +0x245 when the group's `activePortTypes` are more than one (0x1a207ff80..90) and then
reads `SynchronizedStreamsSlaveConfiguration` of the stream with the strict `getProperty:error:` (0x1a2080064); any error ends the init
(`cbnz w0, 0x1a2080570`). The key was not in our streams' SupportedPropertiesDictionary: BWFigCaptureStream answers -12787 by itself, our object is
never asked (which is why the log had no line for it). Now listed (ReadWrite) and answered: a dictionary of `StreamingOutputsEnabled` (boolean),
`MasterToSlaveFrameRateRatio` (int), `FrameSkippingEnabled` (number), `TimeMachineEnabled` (boolean) -- the shape
`-[BWFigVideoCaptureSynchronizedStreamsGroup _slaveConfigurationForStream:]` (0x1a1f5e9ec) builds; the four key strings read out of the cache.
The value is what was last set (on the stream, or for the slave in the group's MasterConfiguration), else outputs off / ratio 1.

**Census of the dual path** (group and group-specific stream accesses; the single-stream path ran in q10hf): group, strict: SupportedPropertiesDictionary,
SynchronizationMaster, SupportedStreams, ActiveStreams (get/set), Extrinsics, BaseZoomFactorsByPortType, MasterConfigurationSupportedKeys,
MasterConfiguration (set) -- all answered. Group, only when listed as supported (verified gates): MaximumNumberOfSlaveStreamsWithoutFrameSkipping,
...WithFrameProcessingEnabled, MaximumNumberOfEnabledSlaveTimeMachines, MinimumMasterToSlaveFrameRateRatio, MidFrameSynchronizationEnabled -- not
listed, not asked. Stream: SynchronizedStreamsSlaveConfiguration (get strict: fixed; set only when the group has no MasterConfiguration: taken
anyway). In the same init, set only when the key is listed: HighCurrentTorchEnabled, RawBufferCompandingEnabled,
NoiseReductionAndSharpeningConfiguration; IfSupported: CalibrationValidRadius..., FocusPositionToLensMakersPFLLinearFit. Not followed to the end:
what a capture (not the viewfinder) asks (TimeMachinePTSRange in _ubSuspendTimeMachines...).

**So that the next one is in the log:** rwcam_diag.c wraps the strict getProperty:error: / copyProperty:error: / setProperty:value: of
BWFigCaptureStream, BWFigCaptureSynchronizedStreamsGroup and BWFigCaptureDevice and says every call that ends in an error
(`diag: PROPERTY ERROR <n> ...: getProperty <key> of <object>`), including the -12787 our objects never see. Our own objects say every key they
answer with an error and every set they take without using, each time (no "once per key").

**The log's limits** (src/rwcam_loglimit.c, test/t_log.c): a line of one shape (numbers and addresses apart) three times, then one per window
(30 s doubling to 10 min) with the count of those not said; all lines within 300 KB + 1.5 KB/s (at most 750 KB in five minutes). Summaries are
`rwcam_say_keep`: not counted by shape, last to go without. q10hl's loop replayed in the test: 40 KB in five minutes instead of 833 KB in two.

**Pictures:** the processor is the default again; the GPU only with `/var/db/nord2-camera-gpu-isp` (`nord2-camera-cpu-isp` still forces the
processor). With the GPU: the first 8 pictures of each output and one every 2 s are compared with the processor's (a picture that cannot be
compared counts as wrong), and a GPU picture that is black (mean luma < 4) of a frame whose header says green >= 24 of 1023 turns the GPU off for
that output and is made again by the processor. The CPU path's arithmetic (camera_isp.c) is unchanged.

**Self-diagnosis:** `stream <camera>: 5.0 s: ...` every 5 s for a stream that gets frames (frames, the frame's green and exposure, format, zoom,
flip, whether the graph started it, and per output: size and pixel format asked, pictures given, by whom, and why none); the output's summary
has the size/format, mean/min/max luma and the count of black pictures; `core: node N: NO FRAME from the kernel for ... ms` every 5 s of a read
that gives nothing (also before a session's first frame, which was silent before).

## Run q10hm: "Input can't be nil" with the dual-wide device; the front camera's black viewfinder (bundle b4e019ee)

**The exception (verified by disassembly).** `-[FigCaptureStillImageSinkPipeline _buildStillImageSinkPipelineWithConfiguration:...]` (0x1a1f22fa4)
is the builder `captureSession_buildGraphWithConfiguration` takes for every still image scheme but 3 and 4 (0x1a1f14464 `sub w26, w19, #3`,
0x1a1f14750 `cmp w26, #1; b.hi`; 3 and 4 get FigCaptureStillImageUnifiedBracketingSinkPipeline). It makes its coordinator nodes
(BWStillImageTimeMachineFrameCoordinatorNode when zero shutter lag is on, BWStillImageCoordinatorNode) with a list of at most three port types --
the device's captureStream's, bravoTelephotoCaptureStream's, pearlInfraredCaptureStream's (0x1a1f23324, 0x1a1f239b0..ec) -- and then connects
every entry of sourceOutputsByPortType to `[coordinator inputForPortType:]` (0x1a1f23b64, connect at 0x1a1f23b8c). For deviceType 8 the device's
captureStream is the wide one and the ultra-wide is neither of the other two (0x1a218e49c..4f0), while every RGB stream's source node has its
still image output on (`_csp_configureMultiStreamCameraNode`, 0x1a1fb4afc) and so is in that dictionary: nil input, exception. The scheme forced
to 2 in rwcam_diag.c is not part of it (the choice of builder reads the source's scheme itself, which is 2 here anyway).
**Fix** (rwcam_diag.c, `/var/db/nord2-camera-nostillfix` switches it off): the source node of a stream that is none of the device's three gets no
still image output (its configuration's setStillImageOutputEnabled:YES is made NO), and what is not one of the three port types is taken out
of sourceOutputsByPortType before the pipeline is built. The rest of the builder then runs as for the wide camera alone; its bravo-only nodes
(multi-camera doser, dual photo face propagator, stereo fusion, bravo disparity) hang on stereo fusion / constituent photo delivery / depth, none
of which the `:6` entry declares. A photograph while the ultra-wide camera is shown has no still image output to come from yet.
**Not done:** catching Objective-C exceptions around the graph's construction. The unwinding would leave CMCapture's session and graph half built
with whatever locks were held; a mediaserverd that hangs is worse than one that is started again.

**The front camera.** The pictures given were not black: mean luma 44, 61, 68 ... 92 of 255 in the 5 s summaries (2 black pictures, the first
two, before the exposure had settled), 30 a second, 2929 in all. The sensor's frames ARE dark (green 7 to 25 of 1023 at 33 ms x 16x, the bundle's
own measure and the kernel's header agree, and the header reports the exposure asked for: 5236 of 5284 lines x 16.00), which the digital gain of
4 makes up for. But when the session stopped the viewfinder's BWImageQueueSinkNode said "# frames received: 0" (q10hf, where the front camera
was seen: 1226 and 1549), while BWIrisStagingNode counted 30 a second: the pictures are lost inside the graph, between the source node and the
preview sink. The app (as an iPhone 11) now asks 1654x1240 for PrimaryScaler (q10hf: 1504x1128). VTMTSComputeFunction is refused by the graphics
library once a frame in both runs, so that alone is not it.
**For the next run:** every output of every node of the graph is counted (sample buffers, dropped samples, node errors) and said 5 s after the
graph starts, every minute and at its stop (`diag: flow: <class> '<name>' output '<name>': ...`); the first dropped samples and node errors of
an output are said with their description. `/var/db/nord2-camera-noflow` switches it off.

**PROPERTY ERROR lines of q10hm, all -12787 and all followed by the session going on:** getProperty WhiteBalanceGain (every stream, when the
sources are listed), setProperty FaceDetectionConfiguration, SceneClassifierVersion, LuxAdaptiveParameters, AEStability, AEConvergenceSpeed,
TimeMachineResumeNow.

## Run q10ho: the preview stitcher drops every picture; "Output can't be nil" in the still pipeline (bundle 0b43e386)

**A. The black viewfinder (verified by disassembly, but for the last step).** The preview pipeline has a BWPreviewStitcherNode when its
configuration's overCaptureEnabled (+0x74) or primaryCaptureRectModificationEnabled (+0x75) is set
(`-[FigCapturePreviewSinkPipeline _buildVideoPreviewSinkPipeline:...]`, 0x1a1f9b0c0..cc; +0x75 alone: `initWithStitchingDisabled`). +0x75 is the
client's `-[FigCaptureVideoPreviewSinkConfiguration primaryCaptureRectModificationEnabled]` (0x1a1f13c0c), which Camera.app as an iPhone 11
sets; +0x74 is `_cs_shouldEnableOverCapture` (0x1a1f52e48): deviceType 4/8/9, Photo or High, and the client's spatialOverCaptureEnabled (0 here).
The node's only way to a dropped sample ("StitchingFailure", 0x1a201f8c4) in this mode is `-_newStitchedSampleBufferFromWide:...` (0x1a2022208)
giving nil: no buffer from the output's pool (0x1a2022388) or VTPixelRotationSessionRotateSubImage failing (0x1a20249d8, 24b58, 24e0c). Which of
the two is NOT known (inferred as more likely: the rotation, whose Metal function the graphics library refuses once a frame). Nothing our
buffers lack is asked for on this path. **Fix:** in mediaserverd the client's flag reads NO (`/var/db/nord2-camera-keepstitcher`: as set): no
stitcher, the preview pipeline of run q10hf. The dropped samples said in the log now carry their reason and time.

**B. "Output can't be nil" (verified).** After the ultra-wide stream's output was out, the builder reached its BWCrossoverNode (0x1a1f2404c),
made when `[captureDevice isBravoVariant]` ([sp,#0xfc]; deviceType 4, 8, 9) and [sp,#0x130] is 0, and connected `[router bravoTelephotoOutput]`
(nil: no telephoto port type) to its second input (0x1a1f240dc). Every bravo branch of the builder means wide + telephoto. **Fix:** while
`-[FigCaptureStillImageSinkPipeline initWith...]` runs, on its thread, the device answers NO to isBravoVariant. [sp,#0xfc] is then 0 at each of
its uses (0x1a1f23264, 235e4, 23e30, 24028, 24534, 252c4, 25508, 257b0), [sp,#0x130] and [sp,#0xbc] stay 0 (no constituent photos, no depth, no
stereo fusion in the `:6` entry), bravoTelephotoCaptureStream is nil either way: the builder does what it does for the back wide camera alone.
**And a guard:** `-[BWGraph connectOutput:toInput:pipelineStage:]` with a nil end is said (`diag: A CONNECTION WITH A NIL ...`, the caller's
address before the slide, the other end's node) and passed over instead of thrown (`/var/db/nord2-camera-nilconnect-throw`: as before).

**C.** The reader asks for its camera's sensor before the first read on a node (and at a change of camera on it); refused, it does as before.
The front camera's exposure in q10ho cannot be judged: its session had 84 frames, under the 5 s of a summary.

## Run q10hp: SIGSEGV in FigImageControl_Focus with the pair; the Preview Scaler's -12914 (bundle 54b5d495)

**A (verified).** `_processBuffer` (0x1a1f0e56c; the device's autofocus sample buffer processor, called for every frame from
`-[BWFigVideoCaptureDevice _updateStateUsingVideoSampleBuffer:fromCaptureStream:]`) does nothing for a sample buffer without a MetadataDictionary
(0x1a1f0e5e0) -- every stream outside the group -- and otherwise runs FigImageControl_Sharpness, _ToneMapping and _Focus. `_FigImageControl_Focus`
(0x1a1f30a04) takes the dictionary from the sample buffer's IMAGE BUFFER (0x1a1f30a70..94); when there is none it makes one and attaches it, but
then asks the one that was not there for PortType (0x1a1f30cfc): CFDictionaryGetValue(NULL). Our metadata was on the sample buffer only.
**Fix:** the same dictionary on the image buffer too; and AEStable, AELocked, AELimitsReached, AWBStable, SynchronizedStreamsIsMaster as numbers
(they were booleans; the C code reads them with CFNumberGetValue / FigCFDictionaryGetInt32IfPresent), AFStable added.
**Census of the C code on that path** (the Objective-C readers take nil): _processBuffer: AFStatus, AFStable, FocusPeakSumArray,
CurrentFocusPosition, PortType, NormalizedSNR, AEStable, Histogram_R/G/BChannel, DynamicToneCurve*, GlobalToneCurveLookUpTable, AETarget,
AEAverage; _Focus: PortType, QuadraBinningFactor, PinholeCameraFocalLength, AFStatus, AFStable, FocusingMethod, FocusNowCommandID,
FocusPeakSumArray, CurrentFocusPosition, AEStable, AELimitsReached, AWBStable, FlashCaptureSequence, AFFaceRect; _ToneMapping: AWBR/G/BGain, AGC,
sensorDGain, ispDGain, Histogram_R/G/B/YChannel. Each is used after a nil check or through an ...IfPresent call (by the listing; the three
histogram fetches in a row were read by hand for _processBuffer only). With metadata FigImageControl now RUNS for the pair's master: it sets
properties on our stream (LuxAdaptiveParameters, GainAdaptiveParameters, HistogramParams, ColorSaturation, ColorTables, focus commands), none of
which is listed, so none arrives. `/var/db/nord2-camera-nometadata` takes the metadata, and with it FigImageControl, away.

**B (verified but for the last step).** 'Preview Scaler': "rotation session TransferImage returned error code -12914"
(kVTPixelRotationNotSupportedErr). Its input is 1654x1240 where run q10hf had 1504x1128: 10% of video stabilization overscan, which
captureSession_buildGraphWithConfiguration adds when the client's `-[FigCaptureIrisSinkConfiguration momentCaptureMovieRecordingEnabled]` is set
(0x1a1f13c24: 0.1 or 0; also 0x1a1f96818 and 0x1a1f5fe84). A rotation with a change of size is what VideoToolbox needs its Metal function for
(inferred: VTMTSComputeFunction is refused by the graphics library at every picture in both runs, and the rotation alone worked in q10hf).
**Fix:** the flag reads NO in mediaserverd (`/var/db/nord2-camera-keepquicktake`: as set). For the graphics library: VTMTSComputeFunction with
`air.pack.unorm4x8.v4f16`, and the variant with a bitcast from 4 x 16 bits to 2 x 32 bits; with it both this and the stitcher could be given back.

**C.** A header-only read is one blocking read per frame of the hidden camera (rwcam_core.c), which by KERNEL_ABI_DUAL.md marks that frame
delivered: the reader follows the sensor's rhythm and holds nothing. Whether the kernel's queue frees the buffer on such a read is the kernel's.

## Run q10hs: the front viewfinder is seen; the dual-wide device's overscan and zoom (bundle 10bca0bb)

**The overscan of the dual-wide device (verified).** `-[FigCaptureSourceConfiguration bravoShiftMitigationEnabled]` (0x1a1fd2660) is YES for a
source of DeviceType 4, 8 or 9 unless depth is asked for; `_csp_configureMultiStreamCameraNode` then gives the node
`overscanForBravoShiftMitigation` 0.1 (0x1a1fb48ec..4998, the constant read from the cache): PrimaryScaler 1654x1240 and the zoom divided by
1.1, and the 'Preview Scaler' has to change the size as well as turn (-12914). **Fix:** it reads NO in mediaserverd
(`/var/db/nord2-camera-keepbravoshift`: as CMCapture has it). Inferred: PrimaryScaler is then 1504x1128 as for the front camera.

**The zoom in the group (verified).** `-[BWFigVideoCaptureDevice _setSynchronizedStreamsGroupProperty:value:]` (0x1a1f46998) sets ZoomFactor on
the GROUP when the group lists it, else on the device's captureStream (the wide one) alone; the value is the DEVICE's zoom (2.0 at "1x", here
1.818 with the overscan). Our group did not list it: the wide stream took 1.818 for its own zoom (a crop of 1.82 at "1x") and the ultra-wide
stream was never told. **Fix:** the group lists ZoomFactor and gives it to both streams as the device's zoom: the wide camera shows the middle
1/(zoom/2) of its sensor (all of it at 2.0 and under), the ultra-wide one what rwcam_view_zoom makes of it (all at 1.0, the wide camera's view
at 2.0). A stream started by itself has its own zoom as before.

**Not a fault:** "longest 2089 ms" in the wide stream's summary is the time the ultra-wide stream was the master (its pictures 1 to 3 are in
the log in that gap). **AE coming down** (by the code, isp_ae): at most x0.38 a step, a step every 2 to 3 frames: about a second from the
darkest setting to daylight.

## Run q10ht: zoom between the lenses, the viewfinder standing still while zooming, the flicker (bundle 735f8091)

**The viewfinder standing still (verified).** `-[BWMultiStreamCameraSourceNode _updateZoomForOutputIndex:...]` compares the zoom wanted for a
picture (BWZoomCommandHandler) with the zoom the ISP gave it: `_ispAppliedZoomFactorFromSampleBufferMetadataDictionary:outputIndex:`
(0x1a1ef5b8c) = the stream's base zoom factor x the metadata's `ZoomFactor`; what is missing goes into the metadata as FinalCropRect
(0x1a215d7d0) and the 'Preview Scaler' cuts it out and enlarges (RotateSubImage: -12914 here). Our metadata had no ZoomFactor: every picture
lacked all of the zoom, at every zoom but 1x and 0.5x. Now each picture's buffer carries the zoom it was made with and the metadata gives it
(by the stream's own measure). A stream outside the group has no metadata and never had the problem.

**The zoom between the lenses.** CMCapture changes its master early (to the wide stream at 1.83, to the ultra-wide one as soon as the zoom is
under 2) and fills the difference with a margin of picture (the overscan, and the 'Preview Scaler' again). Here instead: (1) the SENSOR shown
is chosen by the device's zoom -- ultra-wide under 2.0, wide from 2.0 -- and its pictures go to whichever stream is the master (rwcam_pair.c,
sink_frame); (2) the ultra-wide camera's window at 2.0 is the wide camera's picture: the lenses' ratio and the offset of their axes
(rwcam_uw_view; struct isp shift_x/shift_y); (3) ratio and offset are FOUND: while the pair runs the hidden camera's reader reads a whole frame
now and then (every 2.5 s at first, then 8 s, then 30 s), and small pictures of both cameras are compared (rwcam_align.c: correlation over
ratio 1.2..2.3 and offsets up to 0.15 of the picture; the middle of the last findings is used). By hand: `/var/db/nord2-camera-uw-ratio`,
`/var/db/nord2-camera-uw-offset` ("dx dy" in ten-thousandths); `/var/db/nord2-camera-noalign`: nominal 1.6, no offset.

**The flicker (verified in the log: 40 to 113 changes of exposure in 150 frames).** The kernel's header has a new exposure when the registers
are written; the sensor takes it a frame or two later; AE corrected again by frames taken with the old one. isp_ae now waits ISP_AE_SETTLE (2)
frames that say the new exposure, stays put within an eighth of its target once there, and gives exposure times that are whole multiples of
10 ms (50 Hz; `/var/db/nord2-camera-60hz`, `/var/db/nord2-camera-noantiflicker`) whenever 10 ms or more is wanted. test/t_isp.c: a sensor
with 0 to 2 + 0 to 2 frames of delay, three lights. The log's summaries have the luma's movement from one picture to the next and the exposure
in ms.

**camera_isp.c, for the GPU's copy of it:** the pictures' arithmetic is unchanged; `render_line_window` has the window's origin moved by
shift_x / shift_y (0, 0: bit for bit as before), `render_line` takes that path when either is not 0, and rwcam_render.c does not ask the GPU
then. isp_ae and isp_init changed (exposure only).

**The log:** its table of shapes was full (512) after the start, and 549 lines of one shape (FigImageControl's AFWindowParams, refused) used
up the budget; 4096 places now, the oldest given up when sixteen in a row are taken.

## Run q10hy: the change of sensor during a pinch (bundle: see the hash in the last line of this section)

**In the log (verified):** no picture is lost in the graph (1516 + 538 from the two source nodes = 2054 out of 'Video Capture CrossOver' = 2054
out of 'Preview Scaler', 0 dropped samples). The comparison of the two cameras' pictures ran on the HIDDEN camera's reader and took 157 to
416 ms each time (16 times in that session): a change of the camera shown in that time waited for it. The findings were 1/1.52 to 1/1.68 and
the first two (1.525) were used at once.
**By the code (inferred):** a hidden camera is read by its headers, so after the change its first whole frame is the one after the next
header (up to two frames), and its exposure came from the header's levels of its whole field, not of the part shown.
**Changed:** the comparison runs on a thread of its own; near the change (device zoom 1.55 to 2.6) the hidden sensor is read whole too
(rwcam_core_pair_warm; not rendered, not given to anybody) and is shown from its next frame; the exposure of both back cameras goes by the
part of the picture that is shown (unless the user chose a place); three agreeing findings before the lenses' ratio is used; each change of
sensor is said (`group: the sensor shown changed ...: N ms ...`).
**Not the native way:** CMCapture's bravo shift mitigation needs 10 % of picture around the wide camera's field (this sensor has none: the
wide stream would be asked for zoom 0.91) and places the pictures by its own calibration of an iPhone's lenses.

## Run q10hz: autofocus; noise in little light

**Autofocus as it was (verified, code and log):** every search put the lens at 0 and went over the whole range in tenths of it (102 of 1023
a step, three frames each), then in quarters of a tenth around the best: two seconds of the picture breathing through everything. It began
only when the sharpness had been 40 % off for 20 frames; and a search that found nothing (a dark picture: sharpness 0) left the lens at 0
(`lens at 0 of 1023 ... sharpness 0.00` in the log). The kernel writes the AK7377 once for each REDWOOD_CAMERA_FOCUS (the last one given
wins), in its own loop; nothing ramps. focus_max is 0 for the IMX355 and the IMX615 (kernel patch 0290: only N2C_IMX766 has it) and isp_af
does nothing then.
**Now (camera_isp.c isp_af, rwcam_core.c):** from where the lens is, a step of a fortieth of the range one way, the other way if that was
not sharper, on while it gets sharper (the step doubling after three), then the top of the curve through the last three places; at most a
32nd of the range a frame; begins after 8 frames a quarter off the sharpness it was left with, once the picture is steady; neither way
sharper: the lens stays. Only when a small step tells nothing AND the picture is far from sharp (or it never was in focus, or a tap) does it
go over the range, without stopping, taking every frame's sharpness with that frame's lens position. A tap is the SHOWN sensor's.
**Phase detection:** not in the frames this driver captures (inferred: the IMX766's phase data leaves the sensor as a data stream of its own,
which the kernel's CAMSV path does not take, and needs the module's calibration to be read).

**Noise (verified in the logs: 30 ms x 64 x 4).** AE (isp_ae): one frame's time, then gain to 4, then frames up to twice as long (15 a
second; `/var/db/nord2-camera-10fps`: three times; `/var/db/nord2-camera-30fps`: never), then gain to 16 at most and digital gain to 2;
once long, frames stay long until a third less would do. The kernel takes it as it is (0292: frame length written to 0x0340/1 for every
sensor, frame_lines_max 0xffff, its own stall time three frames). rwcam_render.c: pictures taken with gain 3 or more have their colour
planes smoothed and each pixel drawn towards the picture before where they differ by little; with gain 8 or more, more of both and the
brightness smoothed away from edges (`/var/db/nord2-camera-nodenoise`: none). After either backend; the GPU's library has no such thing yet.

## Run q10ia: continuous autofocus that never acted; trails behind moving things in little light

**Autofocus (verified in the log):** the sharpness of one frame is off the next one's by a third and more (21.9, 32.0, 6.5, 15.6, 35.4 for
scenes that stayed), searches ended at once where they began with less than the best they had seen (`lens at 0 ... it was at 0 ... sharpness
21.92 (best of the search 43.19)`), and the lens began at 0 and stayed there. So: "a quarter off for 8 frames, steady within 12 %" hardly
ever held, and "neither way better NOR worse by 1.5 %" (the way to the look over the range) never did.
**Changed (isp_af):** the watching goes by a running mean (a quarter of each new frame); off by 30 % (or up 60 %) for 10 frames and steady
within 8 %, or the scene's brightness (level over exposure x gain) off by a third, begins a search; every place of a search is two frames'
mean, better means 5 %; small steps that tell nothing with the picture under 0.7 of what the lens was left with -- or that ended somewhere
with it under 0.6 -- go over the range (once a search); a tap as before (over the range only when neither way is better nor worse); what the
lens is left with is the mean of the six frames after it arrived.

**Trails (rwcam_render.c, with_before() only; denoise() calls it once for both planes).** Was: per pixel, level 1: 45 % of the picture
before where the two differ by less than 12 (colours 50 %, 10); level 2: 60 % under 26 (colours 65 %, 20). Now, by blocks:
 1. for each 8 x 8 block of the brightness plane: D = the mean of |now - before| over every other pixel of it (16 samples), in quarters of a level;
 2. the noise N = the mean of D over the blocks whose D is at most twice the mean of all D;
 3. a block MOVES when D > 1.7 N (that limit kept between 2.5 and 12 levels); when more than 35 % of the blocks move, all do;
 4. a block that moves, or has a neighbour (of its eight) that does, is left as it is, brightness and colours, and becomes the picture before;
 5. in the other blocks each sample s (before b): if |s - b| < near then s - keep x (s - b) / 256, else s; the result is the picture before.
    near / keep: level 1 brightness 10 / 90 (35 %), colours 8 / 102 (40 %); level 2 brightness 18 / 128 (50 %), colours 14 / 140 (55 %);
    near is raised to 3 N (colours 0.75 x that), 40 at most.
The smoothing in space (smooth()) and the levels' gains (3, 8) are as they were. The 60 ms exposures blur what moves by themselves; that
is the sensor's, `/var/db/nord2-camera-30fps` takes it away at the price of the gain.

## Run q10ib: the autofocus's speed (isp_af only)

The log has no times for a refocus (the bundle's lines carry none), so "before" is by the code and the host test's sensor (a frame shows
the lens of two frames before): the trigger 14 to 17 frames (a running mean, 10 frames off, 3 steady); each place of a search 5 frames (1
moving, 2 waiting, 2 looked at); over the range 32 frames one way and the small steps again after it: 51 to 71 frames for a change of 450
of 1023 in the test (1.7 to 2.4 s), more with the trigger.
Now: a frame under 0.4 of what the lens was left with counts five times (a search after 2 to 3 such frames); under 0.3 the lens goes over
the range at once, the longer side first, a 21st of the range a frame, every frame looked at, and stays at the best place if it is as
sharp there as it was in passing (else the small steps); the first small step is 2 or 4 times as long when the sharpness fell to under a
half or a quarter; far to go: a 16th of the range a frame, the last bit a 32nd; a place is looked at in the first frame whose header says
the lens is there (not after two frames guessed), in one frame when it differs from the one before by a fifth, else in two. In the test:
26 to 32 frames for the same change (0.9 to 1.1 s at 30 a second with the two frames' delay of the test's sensor), small corrections 8 to
10 frames. Not done: counting time instead of frames at 15 frames a second; another measure of sharpness.

## Run q10ig (video mode): the session ends with -12780 — where it can come from, a diagnostic that says which, and the film's pictures

Not run on the phone (it was in use): what follows is read from `autotest/runs/q10ig-video3-rec/all2.log` and the cache, and built + host-tested.

**The log.** Camera commits `AVCaptureSessionPresetHigh`, source `WideBravo back 420v/1920x1080, 30-30` (a photograph's is `420f/4032x3024,
15-30`). Each start: both streams taken, the wide camera's source node made (`set DeferAdditionOfAttachments`), then at once `streams given
back (2)` and `Running -> Idle`; AVFoundation says -11800 (-12780) and Camera starts again, for ever. Nothing is asked of our streams in
between: no FormatIndex, no output configuration, no PROPERTY ERROR, no exception.

**The code.** `_csp_configureMultiStreamCameraNode` (0x1a1fb3950) fills the node's configuration and ends with
`-[BWMultiStreamCameraSourceNode configure:]` (0x1a1fb3130); the second node is only made after it. -12780 comes out of it from:

| where | when |
|---|---|
| `_updateFormatIndex` 0x1a1f9e8c8 | the configuration's format index is not one of the stream's formats (`-[FigCaptureSourceVideoFormat formatIndex]` is -1 when `_csu_resolveFormatIndexForNamedStreamFormat` 0x1a20bb0a8 found none) |
| `_updateOutputIDMappingsForStreamingOutputs` 0x1a1f84248 / 0x1a1f84250 | a viewfinder AND a film output at once (video mode only) and no scaler for the film: `_isVideoCaptureOutputSupportedByOutputID:` 0x1a2159754 wants the format's SupportedOutputs entry, the film's pixel format among its SupportedPixelFormats, and with video stabilization only the PrimaryScaler; or neither scaler "available" (`primaryScalerDecoupled`, `secondaryScalerIsNotAvailable`) |
| `_updateOutputRequirements` 0x1a1f83d94 | a sensor raw still output asked (soft ISP) of a format without a `Raw` output |
| `_outputConfigurationsForAllOutputs` 0x1a1fc9700 | scaler chaining (GDC on the streaming outputs) with an empty crop |
| `_csp_configureMultiStreamCameraNode` itself 0x1a1fb5b10 | no underlying source format; depth without a depth companion format; zero shutter lag with stills taken from the video stream |

What our streams offer passes every one of these as far as the code can be read without the values: `1080p30` is `Video-60`
(SensorWidth 4224, SensorHeight 2376, MaxFrameRate 60) and the back streams' second format is exactly that (the matcher compares SensorWidth,
SensorHeight, VideoIsBinned, VideoMaxFrameRate, FES, HDR, quadra, SIFR); both scalers are in SupportedOutputs with 420v. So the value that
fails is one the node is GIVEN (stabilization method, a scaler said not to be there, the format index after companions): it has to be seen.

**Changed.**
- `rwcam_diag.c`: `node_configure` wraps `-[BWMultiStreamCameraSourceNode configure:]` and says, at each configure, its result, the
  format index against the number of formats, the film's pixel format, which outputs are on, video stabilization, the two scaler flags,
  GDC, depth, quadra, the film's and the viewfinder's sizes, and the output IDs the node chose (`diag: source node configure: ...`). Changes
  nothing. One line of the next video run names the row of the table.
- `camera_isp.c/.h`: `isp_shrink_420` (a 4:2:0 picture made smaller from one already made, video/full range either way), tested in `t_isp`.
- `rwcam_device.c` (`deliver`, `twin_made`, `twin_piece`, the outputs' loop, `stream_set_property`): with two scalers on, the second
  one's picture of a frame is the first one's made smaller, not a second pass over the sensor's frame (same proportions to a hundredth, both
  4:2:0, not larger) — the film at 1920x1080 plus the viewfinder costs one ISP pass and a shrink on 16 pieces; both get the same time.
  `/var/db/nord2-camera-notwin`: each makes its own. `MinimumFrameRate` >= 24 (a film's 30-30): `rwcam_core_even_rate` — no longer frames
  in the dark while it is on (a photograph's 15-30 keeps them).
- `rwcam_core.c/.h`: `rwcam_core_even_rate`.

**Room in the package.** 253640 bytes. The 8 KB to N2AP_MAX are not the room there is: the file is made of 16 KiB pages and `__TEXT` ends
984 bytes before its next one (`__DATA` 724): one more kilobyte of code or text anywhere and the package grows by 16384 at once, over the
limit (it did here: 269432). Taken out for room: the client session monitor's diagnostics of runs q10go to q10gr (`condition_update`,
`map_rbs`, `layout_update`, `probe_client`, `monitor_init`, `audio_interruption`, `wrap_class_method`: state changes, RunningBoard's mapping,
display layout, the tccd probe, audio interruptions). Room without touching N2AP_MAX: the linker puts `LC_ENCRYPTION_INFO_64` in and for it
starts `__text` on the second page; `-no_encryption` in build_cam.sh's link starts it at 0x730 and leaves 15.5 KB before the same cliff
(trial link only, NOT in the build: it changes the layout of the bundle the audio plug-in is in, and was not loaded on the phone).

## Run q10ii (video mode): the -12780 is the film's output mapping — an IntermediateTap output

The probe's line of the failing configure: format index 1 of 2 (fine), film 420v 1920x1080, **viewfinder 0, film 1** (one streaming output:
the viewfinder is made from the film's pictures in the graph, as in photo mode), **PrimaryScaler decoupled 1, no SecondaryScaler 1**, no
output chosen. Both flags are the pipeline's own (`_csp_configureMultiStreamCameraNode`: `setPrimaryScalerDecoupled:` 0x1a1fb43dc from the
stabilization methods' list, `setSecondaryScalerIsNotAvailable:` 0x1a1fb43bc from the builder), not read from the stream's formats. With them
`_primaryScalerIsAvailable` (0x1a1fd572c: `1 & ~decoupled`) and `_secondaryScalerIsAvailable` (0x1a1f92288) are both NO, and
`_updateOutputIDMappingsForStreamingOutputs` has the IntermediateTap left for the film (0x1a1f84218 -> -12780 when
`_isVideoCaptureOutputSupportedByOutputID:` says the format has none). A SecondaryScaler in the format's dictionary (it is there) cannot help.
- `rwcam_device.c`: a fourth output, `IntermediateTap`, in the SupportedOutputs of every format but a camera's first (the photograph's keeps
  what it had), given like the PrimaryScaler (`LEAD`: even times, the pieces, the twin's source); `format`, `deliver`, the names, the status
  line.
- the stream says it has `DecouplePrimaryScalerFromStreaming` (taken, not used): the node's `_supportsDecouplingPrimaryScalerOnly`
  (0x1a1fa118c) is then YES and `_updateOutputIDMappingsForOnDemandStills` (0x1a215a060) keeps the photographs on `StillImage` instead of
  `StillImageIntermediateTap`, which is not made here.
Not known until it runs: the size the graph asks of the IntermediateTap (1920x1080, or the format's 4224x2376 with its own scaler after:
`_videoCaptureOutputRequiresScaling`) — the first-picture line and the probe's `outputs chosen` say. Package 253640, `__TEXT` 808 bytes
before its page.

## Run q10ij (a film is recorded, two frames long): no video stabilization; even times on every streaming output

The log: the film's node now configures (0), film from the `SecondaryScaler` this time ("no SecondaryScaler 0": the flag is not the same from
run to run), 2304x1296 = 1920x1080 plus the stabilizer's 20% overscan; `BWVISNode 'Tail 0 VIS Pipeline Stabilizer (ISP-strip)'` passes 2
pictures and drops 176 (`VideoStabilizationFailure`). It needs the ISP's motion data per frame; nothing here makes it.
- `rwcam_diag.c`: `vis_method` — `-[FigVideoCaptureConnectionConfiguration videoStabilizationMethod]` (0x1a1ee9e78) answers 0 in
  mediaserverd, for every camera and format: no VIS node, no overscan (the film's pictures should be 1920x1080).
  `/var/db/nord2-camera-keepvis`: as asked. The plist still says the formats can be stabilized (`VideoStabilizationSupported`): taking that
  out is a rootfs change, not needed for this.
- `rwcam_device.c`: `LEAD` is every output but StillImage. The film came from the SecondaryScaler alone and its pictures had the kernel's
  raw stamps (22 to 42 ms apart, run q10hc), not the even times the PrimaryScaler's get: now they are frame_lines x line_ns apart.
Package 253736; `__TEXT` 520 bytes before its page.

## Run user1 (the user's own 15 minutes): heat, a film's frame rate, the 0.5x photograph

**Measured (runs/user1/all.log; the whole session was in the dark: 60 ms x 16 x 2.00 on all three cameras, green 13 to 50 of 1023).**
| | cores busy of 8 (`cpu_busy%` avg) | pictures | ISP per picture |
|---|---|---|---|
| nothing (camera closed, or in the background: streams given back) | 0.2 (2 %) | | |
| photo viewfinder, back | 3.0 to 4.0 (38 to 50 %) | 1504x1128, 10 to 14 a second | 55 to 60 ms on all cores (20 ms the first, no noise taken out yet) |
| photo viewfinder, front | 3.3 to 3.8 | 1504x1128, 15 a second | 56 ms |
| film (recording), back | 4.0 to 4.7 (50 to 59 %) | 1920x1080, 9 to 13 a second | 65 to 74 ms; the encoder 27 ms more on its own thread |
- the hidden ultra-wide sensor was read WHOLE, every frame, for as long as the camera stood at "1x" (`0 headers only` of 84 to 150 frames in
  5 s): 1x is device zoom 2.0, the middle of the warm window (1.55 to 2.6). A 10 MB copy and its measures (7 to 9 ms) 17 to 30 times a second.
- the film: 9.4 pictures a second because one ISP pass with the noise taken out takes 65 to 74 ms, not because of the sensor (30 frames a
  second made, 150 in 5 s, half of them not read).
- the bundle's own log: 246 lines in 2000 of the kernel's (about a tenth of the bytes); the rest is the kernel's (NORD2 HANG stacks, GPU and
  SYS counters) -- not this tree's.
- the GPU is 40 to 45 % busy during the viewfinder (PRFCNT gpu/cycles) without our ISP on it: the compositor and CMCapture's own nodes.

**Changed.**
- `rwcam_pair.c`, `rwcam_core.c/.h`: warm lasts `RWCAM_WARM_MS` (2.5 s) from the last MOVE of the zoom inside the window; at rest the hidden
  sensor is headers only again, at 1x too.
- `rwcam_device.c` `small_made` (+ `deliver`): the streaming pictures are made smaller than asked and then made the size asked
  (`isp_shrink_420`, which now makes larger too, in separable passes: 9 ms on one core of the PC for 902x676 -> 1504x1128): 3/5 a side for a
  photograph's viewfinder (36 % of the pixels through ISP and noise), 2/3 for a film (1280x720 -> 1920x1080, 44 %). Photographs untouched.
  Not when the GPU makes the pictures. `/var/db/nord2-camera-fullsize`: as before. Softer: the viewfinder hardly (the screen is 1080 wide),
  a film visibly on a large screen -- the price of 30 a second on this ISP; the real cure is the ISP's inner loop (NEON) or the GPU.
- `rwcam_device.c` `stream_still_now`, `frame` loop: a photograph asked of a stream of the pair whose StillImage is not on is the other
  stream's; and a stream's pending photograph is made of the frame of the sensor SHOWN even when the pictures go to the other stream (0.5x:
  the master is the ultra-wide stream, whose node has no still output since the still fix). INFERRED from the code -- the session has no
  shutter at 0.5x or on the front camera in its log (three, all at 1x on the back). The front camera's: nothing found to fix without a log
  of one (`phone.py lens flip; shutter`; the phone was off USB when tried).
- `rwcam_diag.c`, for room: `node_configure` (the probe of q10ii) and the client probe of the q10g runs (`client_probe*`,
  `source_reply_probe`, `say_connections`, `authorized_always`, `str`; it ran only with /var/db/nord2-camera-clientprobe) taken out.
  `__TEXT` now 6.4 KB before its page.

**Expected, not measured:** viewfinder in the dark about 1.5 to 2 cores instead of 3 to 4 (ISP+noise on 36 % of the pixels, plus the
enlarging, less the hidden sensor); a film's ISP pass 65-74 ms -> about 30-35 ms in the dark, under 15 in light.

**Starting.** No clock on `exec Camera`; by the 2 s SYS windows the first picture comes less than 2 s after it, and it is the ULTRA-WIDE
sensor's (streaming at once); the wide one's first frame is 1.17 s later -- the kernel writes the IMX766's register table through that
time (`table write 0..`), not this tree. Then the exposure climbs from the sensor's 16.7 ms x 2 by at most 2.6x a step, two frames
between steps: about a second more in the dark. Not changed here; next: start a camera with the exposure it had last (kept in
mediaserverd), which takes that second away.

## Run user3: memory (surfaces), the pinch, the front camera's and the 0.5x photograph

**Measured in runs/user3/all.log (bundle 4c9fd85a).**
- The pictures: 1504x1128 made in 6.3 to 7.4 ms at 30 a second in light (20 before), 1920x1080 in 10; cores busy 2 to 4 of 8 still, so most
  of what is left is not the ISP.
- Surfaces: the kernel names creations only when it refuses them (43 of mediaserverd's good ones are in the log), so WHOSE the 450 to 548
  are cannot be read. 1214 MiB / 450 is 2.7 MiB each: the viewfinder's 1504x1128 420f is 2.52 and a photograph's 4032x3024 17.5 -- about 430
  viewfinder pictures and 8 photographs fit. This tree makes no pool of its own for them: the pictures are made in buffers of the GRAPH's
  pools and handed to the graph. Whoever keeps 430 of them is in the graph; the Live Photo staging (BWIrisStagingNode, whose intermediate JPEG
  compressor has no hardware here) is the one node that keeps seconds of the viewfinder on purpose. INFERRED.
- The pinch: nothing of ours slows down in the log -- pictures 30 a second and 6 to 7 ms each through every change of sensor (0.5x <-> 1x
  taps; no pinch could be told apart in it), the graph's handler 0.2 ms. The 23 a second windows are the 1.15 s start of the wide sensor.
- The front camera's photograph: asked with `TimeMachineCaptureParams` (zero shutter lag: a frame from the firmware's ring of past frames);
  two pictures given, the coordinator took none and flushed the request with -16800 five seconds later. Back camera requests have all been
  brackets, never this.
- 0.5x: no request reached a stream at 0.5x. The fourth photograph (captureID 15, taken at 1x, four pictures given like the three that
  worked) never completed in the graph; the coordinator stayed in "Capturing" and every later shutter did nothing until the session ended
  (-16800 at its stop). Why that one hung is in the still pipeline (GNR), not found here.

**Changed.**
- `rwcam_device.c` `buffer_counted`/`buffer_back`: every buffer taken from a graph's pool is counted until the graph lets go of it (an
  attachment whose deallocator counts back); `stream ...: buffers the graph holds: PrimaryScaler N (M MiB), ...` every 5 s. The next run
  says whether the 430 are the viewfinder's.
- `rwcam_diag.c`: `isZeroShutterLagSupported` reads NO for the front camera's photo format (`/var/db/nord2-camera-keepzsl`: as before);
  with `/var/db/nord2-camera-noiris` (NOT by default) `irisMovieCaptureEnabled` reads NO: no Live Photo pipeline.
- `rwcam_core.c`: warm, the hidden sensor is read whole every third frame, not every one.
Package 253752; `__TEXT` 4 KB before its page.

## Run user4: the front camera's photograph ends mediaserverd; a film's viewfinder while the zoom moves

**The front camera's photograph (verified).** Asked now as one picture, no bracket (`options:` empty; zero shutter lag is off). The first
picture given has NO MetadataDictionary -- a stream by itself got metadata on a still only when the picture carried its bracket number --
and `-[BWStillImageCoordinatorNode renderSampleBuffer:forInput:]`'s block (0x1a1f1c714) takes PortType from it and uses it as a dictionary
key (0x1a1f1c7b0): `NSInvalidArgumentException ... key cannot be nil`, mediaserverd ends and starts again, at each of the three shutters.
Changed (`rwcam_device.c`, `deliver` + `stream_add_attachments`): every photograph's picture is tagged (0: no number) and gets its metadata.
**Not solved, and not the front camera's alone:** the back camera's one-picture photograph (request 4, in light: no bracket) has its
metadata, goes into BWGNRNode ("took picture 16: it now expects 1 and has 0, processing type 1"), nothing comes out and the coordinator
flushes it with -16800 five seconds later (`GNRProcessor _registerImages:` -12780 at the flush). With the metadata there the front
camera's will get that far and no further: the one-picture path of the still pipeline (GNR) does not complete on either camera. The three
photographs that were delivered are all brackets of four. No request at 0.5x is in the log.

**A film's viewfinder during a pinch (verified, cause inferred).** While the zoom moved (2.00 -> 2.135 -> 2.08, film format) mediaserverd's
GPU commits of one compute pass took 46 to 70 ms each, 83 of them in 400 lines of log; 9 before the pinch, 6 after it with the zoom at
rest at 2.08. Not ours (the film's picture is zoomed by the ISP: `view 1.04`; made in 33 ms, 20 a second, the graph's handler 0.2 ms): it is
the graph's own enlarging of what a picture lacks of the zoom wanted (FinalCropRect, the 'Preview Scaler' / 'Movie File Zoomer'), in the
Metal/VideoToolbox driver's compute path. Why a film's pictures lack zoom and a photograph's do not: INFERRED -- they come 20 a second, not
30 (the zoom handler pairs each picture with the request it expects after a fixed number of frames).
Changed (`rwcam_render.c`): the noise passes (`smooth`, three a picture at level 2, and `with_before`'s pixels) ran on ONE thread, 25 of a
film's 33 ms; now in 8 bands on the cores, the same result to the byte (a band's edge rows are copied first). Expected: a film's picture
about 12 ms in that light, 30 a second, and the dark viewfinder as much faster. If the viewfinder still crawls during a pinch at 30 a
second, the cure is in the driver's cropped transfer (50 to 60 ms a commit).
Package 253816; `__TEXT` 5.5 KB before its page.

## Run user5: a film's viewfinder lags and freezes; the 0.5x photograph

**The film (verified, runs/user5/live.log).** The counter of run user3 says it: while the film was recorded the graph held more and more of
the film's pictures (PrimaryScaler, 1920x1080, 3 MiB each): 21, 89, 161, 214, 307, 355 of the wide stream's and up to 147 of the ultra-wide
one's -- 1.35 GB. We gave 1325 pictures in 66 s (made in 10 to 12 ms, 17 to 23 a second); the encoder session ended with 832 (12.6 a second).
The pictures wait in the graph's movie pipeline (its zoomer and compressor take them slower than they come), the viewfinder -- made by the
graph from the same pictures -- shows them seconds late ("lags absurdly"), and at 2046 of 2048 MiB the kernel refuses surfaces
(`SWEET SURF: refused`, 94 times): the pool gives no buffer, 5.8 pictures a second, the viewfinder stands. After the stop the count falls
(355 -> 67) as the queue is encoded. No reconfigure of the streams during a pinch (no `streams given`, no configure); the zoom is ours
(`view`), the sensors change at 2.0 as meant; in photo mode the graph holds 1 or 2.
Changed (`rwcam_device.c`, the frame's loop): in a film format no more than `HELD_MOST` (12) pictures of an output are with the graph at
once; a frame that would be one more is not made. The film then has as many pictures a second as the graph takes (about 13 with the zoom
moving in this run, by the encoder's count) instead of all of them late: less fluid than the file was, no lag, no freeze. To have 30: the
graph's side (the cropped transfer of 50 to 60 ms a commit, the encoder).
**0.5x (verified).** The request comes to the wide stream ("not numbered, one picture"), the picture is given at once -- of the ultra-wide
sensor, as meant -- and the still pipeline fails on it: "the capture settings (type 4, flags 0x0) name NO stream" (the photo side's guard
puts the wide one), then `GNRProcessor _prepareOutput:params:` -1 after `FigMetalContext create2DTextureFromBuffer` NIL ("no pixel buffer",
1008x756), node errors down to the encoder, `err:-1`. The front camera's one-picture photograph goes through the same nodes and works; what
is different at 0.5x is the capture settings made with the ultra-wide stream as master. In the still pipeline (GNR), not in the stream:
nothing changed here for it.
Package 253896; `__TEXT` 5 KB before its page.

## Run user6: what holds a film at 21 a second; which photographs fail

**The film (verified).** With the cap the graph holds 12 pictures and no more; 151 frames read in 5 s (30 a second, none missed), 113
pictures given: the other 38 were not made because the graph had its 12. So the film's rate is the rate the graph TAKES pictures at (the
encoder: 814 in 38.7 s, 22.6 ms each, on its thread), and all 8 cores are 56 to 58 % busy while it does. Ours in that: the measures of a
frame 12.2 ms on one thread (exposure, white balance, sharpness: 0.37 of a core at 30 a second), a picture 11.9 ms on all cores (the ISP at
1280x720 and the enlarging to 1920x1080; 16 to 17 ms for the ultra-wide sensor's), the read 9 ms. The noise passes are no longer the cost
(photo viewfinder 8.2 ms a picture at gain 8 and over, 11.2 at 3 to 8, 7.6 under 3; it was 55 to 60). 1080p at 30 with the cores under
half is not reachable by trimming this ISP: it needs the ISP's loop in NEON or on the GPU (the GPU path exists behind
/var/db/nord2-camera-gpu-isp and was not judged here). Nothing changed for it in this round.

**The photographs (verified).** Every one that failed is a ONE-PICTURE photograph of the dual-wide device, at 1x as at 0.5x (16 of them:
`not numbered (one picture)` on Back Camera); every bracket of four worked (5), and the front camera's one-picture photograph worked. The
failing ones all have: "the capture settings (type 4, flags 0x0 or 0x800) name NO stream to take the photograph with: the back wide camera's
is put there" (the photo side's guard), then in BWGNRNode `FigMetalContext create2DTextureFromBuffer` NIL for a 1008x756 texture ("no pixel
buffer": a quarter of 4032x3024 a side) and `GNRProcessor _prepareOutput:params:` -1 -> `err:-1`; from the third such shutter on nothing
is delivered at all. The front camera's goes through the same node without that texture. So it is not the ultra-wide sensor and not the
light as such: it is the one-picture capture of the DUAL device, whose settings come without a stream (CMCapture resolves the capture type
in `-[BWFigVideoCaptureDevice _resolveStillImageCaptureTypeFromStatisticsByPortType:masterCaptureStream:...]`, 0x1a1f3542c; in the dark it
is a bracket and works). A photograph in good light with the back camera fails for this, at any zoom. Where to fix: the guard that fills
the missing stream settings (rwcam_diag.c, the photo side's) -- what GNR lacks is whatever those settings give it for the quarter-size
buffer; a crop of the wide sensor for 0.5x would not help, the wide camera's own one-picture photograph fails the same way. Nothing changed
here.

## After user6: threads, the ISP a line at a time (not in the bundle), the back cameras' 4096x3072 photo format

- **Threads** (`rwcam_device.c` `apply_pieces`): the pieces of a picture (ISP, enlarging, noise) are done by at most 4 threads, each taking
  the next piece, not one a piece on all 8 cores. `/var/db/nord2-camera-allcores`: as before. Expected: a picture takes somewhat longer by
  the clock (still far under a frame), the graph's encoder has cores left. To be measured on the phone: `made in`, the encoder's pictures a
  second, `cpu_busy%`.
- **The ISP in vectors: written, proven the same, NOT faster, not in the bundle.** `render_rows` (camera_isp.c, `-DISP_LINES`): a line's
  sensor values into arrays, gains/clipping/matrix and the Y Cb Cr as loops the compiler makes NEON (arm64: widths 8 and 16, checked with
  -Rpass) or SSE of; byte for byte the pixel-by-pixel picture (t_isp `lines`: 48 pictures, every Bayer order, zoom, flip, place, range).
  On the PC, one thread, 1280x720 of a 4096x3072 frame: 48 ms against 49. The time is not in the arithmetic: it is in taking each pixel's
  four values out of the packed frame (two rows of the 16 MB frame per line, a place computed per pixel). That part does not vectorize as
  written. What would: taking a whole run of the frame apart at once (5 bytes -> 4 pixels, 16 at a time) into a small row and picking from
  it -- the next step, not done. No arm64 code can be run here (no emulator): numbers from the phone only.
- **4096x3072** (`rwcam_device.c`: the back cameras' third format; `tools/make_camera_plists.py --still-4096`;
  `system/D22-still4096/AVCaptureSession.plist`): the bundle offers 4032x3024 (format 0) AND 4096x3072 (format 2) -- with the plist as
  installed nothing changes; with the new plist (the `Photo` stream format and the `12MP` device format of the wide and ultra-wide entries
  at 4096x3072) CMCapture resolves format 2 and the photographs, brackets too, are 4096x3072: the wide sensor's frame as it is, the
  ultra-wide one's enlarged as it was to 4032. The viewfinder's size is the plist's PreferredPreview (unchanged). Not run: whether the
  bracket's GNR likes 4096x3072 as it likes 4032x3024 is for the phone to say; the plist is the switch, and the way back.
- **PortType on every photograph's picture**: since run user4 every StillImage picture is tagged and gets the stream's metadata
  (`metadata_of`: PortType first), numbered or not, the frame before a bracket too, on the front camera (`stream_add_attachments`' lone
  path) and on the pair (its group path, also when the picture is the ultra-wide sensor's given through the wide stream). Only
  /var/db/nord2-camera-nometadata takes it away.
Package 253952; `__TEXT` 4.6 KB before its page.

## Where things stand (after run user6) -- one place to read

**On the phone:** bundle 8bde9ff1. **Next:** 1aea9991 (this section's change: a camera starts as it was last). The 4096x3072 plist is
prepared, not installed (apps-20261104/guest-still4096.sh).

**Starting (changed in 1aea9991, `rwcam_core.c`).** Each camera's exposure (lines, gain, frame length), white balance and picture gain
are remembered whenever its exposure is steady, and a camera taken up again asks the sensor for that exposure before its first frame is
read (`core: camera N starts as it was last: ... (asked | REFUSED)`). In memory only (mediaserverd stays; after a reboot the first start is
the old one) -- no file yet. Not changed: the two frames AE waits after a step, the step's limit. Measured before (run user1): first
picture within 2 s of `exec Camera`, the ULTRA-WIDE sensor's; the wide sensor's first frame 1.17 s later while the kernel writes the
IMX766's register table (`table write 0 .. 160` at every start of the stream: yes, every time); then about a second of climbing in the
dark, which is the part this takes away. Keeping a sensor powered after Camera closes: not done -- it is the kernel's table write that
costs, and a sensor left streaming is heat for nothing most of the time; a kernel that keeps the table and only restarts streaming would
give the 1.2 s back.
**A film's viewfinder during a pinch.** Our side, by the logs (user5, user6): the film's pictures are given at the zoom asked (`view`),
10 to 12 ms each, the same buffers the film gets (one output: the viewfinder is made by the graph from them), their times one frame apart;
no reconfigure, no restart while the zoom moves; since the cap never more than 12 with the graph. What is slow is after us.
**0.5x photographs today.** With the plist as installed (4032x3024): at 0.5x a photograph is a one-picture capture, asked of the wide
stream, made of the ULTRA-WIDE sensor's frame (what the viewfinder shows, not a crop of the wide one) -- and it fails in GNR (err -1,
nothing saved), as the back camera's one-picture photograph at 1x in good light does. In the dark at 1x the photograph is a bracket of
four and works. With the 4096x3072 plist both are meant to work; not seen yet.

**Switches** (empty files in /var/db; none is needed for normal use):
| file `nord2-camera-...` | what it does |
|---|---|
| `fullsize` | streaming pictures made at the size asked (not 3/5 or 2/3 and enlarged) |
| `allcores` | a picture's pieces on all 8 cores (not 4 threads) |
| `notwin` | a second scaler's picture made from the frame, not from the first's |
| `nodenoise` | no noise taken out |
| `10fps` / `30fps` | in the dark frames up to 3 times as long / never longer (default 2 times) |
| `60hz` / `noantiflicker` | mains at 60 Hz / exposures not tied to the mains |
| `coldstart` | a camera starts from the sensor's own exposure |
| `gpu-isp` / `cpu-isp` | pictures by the GPU / never by the GPU (default: the processor) |
| `noalign`, `uw-ratio`, `uw-offset` | the two back lenses not compared / their ratio and offset by hand |
| `awb-grayworld`, `nofalloff`, `ctm` | older white balance / no lens falloff correction / colour matrix by hand |
| `keepvis` | video stabilization as CMCapture asks (then the film has two frames) |
| `keepzsl` | zero shutter lag on the front camera (then its photograph times out) |
| `noiris` | no Live Photo pipeline (not default: untested with a photograph that asks for the movie) |
| `keepstitcher`, `keepquicktake`, `keepbravoshift`, `keephevc` | CMCapture's own answers for those four |
| `nostillfix`, `nosinglenumber`, `noschemefix`, `nom2mstub`, `emptysettings-throw`, `nilconnect-throw` | the still pipeline's fixes off (photo side) |
| `nometadata` | no metadata on pictures (photographs then end mediaserverd) |
| `still-all`, `still-sync` | StillImage pictures every frame / made on the reader's thread |
| `backonly`, `nodual`, `noultrawide`, `dualwide`, `n104-all`, `prewarm`, `focusprops`, `flip-*` | which cameras and properties are offered (set by the install transactions) |
| `mark`, `nomark`, `testpattern` | bars drawn into black pictures / never / a test pattern |
| `nokick` | a stalled sensor session is not started again |
| `noflow`, `nopropdiag`, `nognrdiag`, `notap`, `clientprobe` | diagnostics off (the last: gone from the code) |
