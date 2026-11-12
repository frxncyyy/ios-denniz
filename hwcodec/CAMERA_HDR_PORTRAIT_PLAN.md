# Camera: Apple's GNR fusion (stacking / HDR) and Portrait — plan

2026-11-12, host-only analysis. USER DECISION: stacking/HDR = option (A), make Apple's GNR fusion work on our GPU.
Labels: **[V]** verified (log line, disassembly or source read), **[I]** inferred, **[?]** open.

## 1. What the still pipeline does with a bracket today

- Device attributes: every back source has `SIS = 1`, `StillImageNoiseReductionAndFusionScheme = 2` [V, diag lines in
  autotest/runs/q10ik-vt12/all.log]. Scheme 2 builds `FigCaptureStillImageSinkPipeline` with a `BWSISNode`
  (`initWithCameraTuningDictionary:sensorIDDictionary:fusionScheme:`) and a `BWGNRNode` whose processor is
  `GNRProcessor` of `/System/Library/VideoProcessors/GNR.videoprocessor` (shared cache image 0x1f205b000..0x1f2094000) [V].
- In the dark the back camera asks a bracket: `AEBracketedCaptureParams[4] ExposureBracketingMode ProvidePreBracketedFrame`;
  our camera serves 1 pre-bracket frame + 4 numbered frames [V, q10ik-vt12 l.16748..]. **All 4 frames are delivered with the
  same exposure** (60 ms x 16 x 2): the bracket's per-frame EV is not applied by rwcam/kernel [V from the 5 s summaries; the
  per-frame AE path ignores AEBracketedCaptureParams — I, not re-read in the newest tree]. For GNR (noise-reduction fusion of
  equal exposures, scheme 2) this is acceptable; a real HDR (EV-) frame needs per-frame exposure later (milestone M5).
- GNR per frame: `_appendFrames:cfp:` -> `_registerImages:NO` -> `_perFrameProcessing` -> `processSampleBuffer` (each gave 0),
  then `finishPendingProcessing` -> `_registerImages:YES` **gave -12780** -> `_multiFrameProcessing` -> `_prepareOutput:params:`
  **-17401** -> `processSequence` -17401 [V, q10ik-vt12 l.16802-16970]. The node then passes the reference frame on:
  `BWPhotoEncoderNode ... Processing succeeded`, `Delivering still image ... err:0` [V l.17017-17019]. **So every photo saved
  so far is an UN-fused single frame** (that is the "stacking is ignored").
- Neural Engine / CoreML: nothing in the GNR path asks for them (no ANE/CoreML/Espresso lines in any photo run; GNR's 52
  Metal functions are listed in `gpu/build/y2s63-vt23/src/gnr.spec`) [V for the logs; I for the binary as a whole].
  GNR = Metal compute + render (fragment) passes only.

## 2. What -17400 / -17401 / -17402 mean (disassembly of GNR.videoprocessor) [V]

The only three sites are in one function (0x1f2061740..0x1f2061cbc, `_prepareOutput:params:` by its arguments): it maps the
processor's error state `self+0x40` to the return value: `0` -> success, `1` -> **-17401**, `6` -> **-17402**, anything else
-> **-17400** (e.g. 8 = no output pixel buffer, 10 = the fusion call failed).
State 1 is written at exactly four places, each right after `[self <stub 0x1f2091cc0>:BOOL]` returned non-zero; the only
GNRProcessor method of signature `i20@0:8B16` is `_registerImages:` (the diag wraps it with that type), and the YES call
site 0x1f207a004 is the finishPendingProcessing one. Hence:

- **-17401 = image registration (frame alignment) failed** — in q10ik-vt12 `_registerImages:YES` gave -12780.
- -17400 (older runs user2..user6, q10ih, hm1-heic) came with `_prepareOutput:params:(.., 0)` i.e. NO params, before any
  GPU fault: an earlier stage (capture settings / metadata of the one-picture path) — those runs predate the camera
  bundle's metadata fixes; q10ik-vt12 is the first to reach registration.

## 3. Why registration fails: GPU job FAULTS, not hangs [V]

- q10ik-vt12 l.16821-16824: `NORD2 GPU WA: TTRX_3485 completed` (the reset path re-running the Mali workaround), then the
  winsys wait `np call c3 ... errno=5` returns **at once** (EIO), and air_metal.c reports it as "did not finish the dispatch
  within two seconds" — the message is wrong for this case.
- Kernel counters in the same window: `GPU JOBS: n=1578 fault=24`, next window `fault=6`; mediaserverd's
  `np stats ... faults=30` [V l.16946, 17057, 17072]. 30 faulted jobs = the 25 "did not finish" lines + draws.
- Cascade: `handle_mmu_irqs` (kernel nord2_gpu_core.c) marks the context FAULTED, stops its running jobs, cancels the queued
  ones and disables its address space [V source]; so after the FIRST fault every GNR job of that burst fails [I: needs the
  fault decode to confirm it is an MMU fault and not a JS fault].
- The kernel returns the JS status, MMU FAULTSTATUS and FAULTADDRESS in WAIT_SEQ's output (`nord2_gpu_fault_info`,
  Nord2GPU.cpp:1420) and **nord2_pan.c never read it** [V]. Nobody logged which kernel faulted either.
- GNR kernels compiled fine (PSO lines: `regpyr_deriv_compute_frag`, `warpFrame`, `LTM`, `detectFlickerForHDR`,
  `pyramid_downsample`, `bg_*`, ... and render passes R8 (fmt10) + RG8 (fmt30) pyramids 2016x1512 .. 252x189) [V].
- Not the cause [V]: `MinFilter1Radius0` (dynamic threadgroup memory refused) happens at camera start, long before GNR;
  no `is bound at offset` (SSBO offset refusal) line in any run; the "unaligned buffer offsets" of the older note do not
  appear in the logs kept.
- Candidates for the fault, to be decided by the diagnostic [I]: (a) a texture over a buffer (`create2DTextureFromBuffer`,
  first one RG16Float 2016x1512 8064 B/row) at pyramid levels whose row pitch is not a multiple of 64 (504x4 = 2016 B,
  252x4 = 1008 B) read with a Panfrost linear layout of a different pitch -> read past the buffer; (b) a Metal buffer offset
  on a `device` binding / argument buffer member mis-applied; (c) a compiler bug (invalid encoding / out-of-range index) in
  one of the registration kernels; (d) half-float image stores (`half_store.ll` work).

## 4. DIAGNOSTIC PAYLOAD (built, host-checked, NOT run on the phone): `gpu/build/y2s63-gnr1`

Base: newest driver tree y2s63-vt23 (copied; a343b4f1's directories untouched). Control: y2s63-vt23's own src rebuilt gives
the installed `libredwoodair.dylib` 6df1af45... byte for byte [V].
Output: `gpu/build/y2s63-gnr1/payload/Nord2Metal.bundle/`
- `libredwoodair.dylib` **e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90** (changed)
- `Nord2Metal` 59212d38... (identical to y2s63: install only the dylib, or the bundle as for vt23)

Hunks (all marked GNR1, separable; `gpu/build/y2s63-gnr1/GNR1.diff`, 8 hunks, originals in `y2s63-gnr1/orig/`):
1. `src/nord2_pan.c` (copy of the shared tree's redwood-drm/nord2_pan.c): ring of the last 128 submits; on a failed job
   one line `np GNR1 FAULT pid=.. job .. (fragment|vertex/tiler/compute, chain 0x.., N bos[, list FULL]) JS status 0x..
   (name) MMU faultstatus 0x.. (translation/permission/..., read/write/execute access) address 0x.. INSIDE bo N (va, size,
   +offset, imported, surface) | in NO buffer object; K bytes past the end of bo N ...`; first 24 per process. Exported
   `nord2_gnr1_bo_info()`.
2. `src/air_metal.c`: after every dispatch's wait — on failure `GNR1 FAILED DISPATCH <kernel> grid .. group .. shared ..`
   plus one line per buffer (size, Metal offset, offset%64, bo, va range, stride) and per texture (format, WxH, usage,
   view, bo, va range, Panfrost stride/offset); first 12 per process. With `/var/db/nord2-gpu-gnr1-trace` present: one
   line per successful dispatch (`GNR1 dispatch <kernel> grid x group ms`). `#include <time.h>`.
3. `src/build.sh`: compiles `src/redwood_drm.c` (unchanged copy) so that its `#include "nord2_pan.c"` takes this
   directory's copy; the shared Mesa tree is not written.
Host tests: the native-Mesa host test suite (host-tests.sh) was NOT run (needs a full Mesa host build); the changes are
logging only, compiled with the production command line (-Werror) [V].

### 4.1 Installable payload (rehearsed, NOT run on the phone)

All in `gpu/build/apps-20261104/`:
- `payload-gnr1/` (libredwoodair.dylib e3d8eb88..., plan.json), `payload-gnr1-undo/` (plan.json)
- `guest-gnr1.sh` / `guest-gnr1-undo.sh` (modelled on guest-vt23.sh): require libredwoodair = 6df1af45 (install) and
  Nord2Metal = y2s63-vt23 59212d38 **or y2s64-vt24 6482590e**; backup `/private/var/db/nord2-metal/libredwoodair.dylib.pre-gnr1`;
  receipt inventory updated; creates `/private/var/db/nord2-gpu-gnr1-trace` only when absent, with the marker
  `/private/var/db/nord2-metal/gnr1-trace.made-by-gnr1`; undo removes the switch only when the marker is there.
- `owned-gnr1.json` / `owned-gnr1-undo.json` = owned-union.json (191) + the 3 paths above = 194.
- `rehearse-gnr1.sh` -> `rehearse-gnr1.log`: REHEARSAL_OK (install, idempotent re-install, undo, undo again, foreign switch
  kept, foreign dylib refused with nothing changed, and vt24 -> gnr1 -> undo gnr1 -> undo vt24: byte-identical root).
- vt23 / vt24: gnr1 changes only libredwoodair.dylib; vt24 changes only Nord2Metal and y2s64's air_metal.c/build.sh are
  identical to y2s63's -> no rebase needed, either order of Nord2Metal works for gnr1. BUT guest-vt24.sh asserts
  libredwoodair = 6df1af45, so install **vt24 before gnr1** (or undo gnr1 first). NOTE for a343b4f1: guest-vt24.sh (11:11)
  does not parse: `print('CC_JOB_ADDED ... the GPU's view ...')` — an apostrophe inside a single-quoted string (rehearsal used
  a copy with it removed).

Transaction (from `gpu/build`):
```
cd gpu/build && PAYLOAD=payload-gnr1 GUEST=guest-gnr1.sh OWNED=$PWD/apps-20261104/owned-gnr1.json \
  bash autocycle/txn.sh $PWD/device-mmu-q10jn-candidate/ui-sched/nord2-ios-metal-q10jn-ui654fb60dvfs-cap3500-ram3776-sched.img gnr1
# undo:
cd gpu/build && PAYLOAD=payload-gnr1-undo GUEST=guest-gnr1-undo.sh OWNED=$PWD/apps-20261104/owned-gnr1-undo.json \
  bash autocycle/txn.sh $PWD/device-mmu-q10jn-candidate/ui-sched/nord2-ios-metal-q10jn-ui654fb60dvfs-cap3500-ram3776-sched.img gnr1-undo
```
(image sha256 cffbcf1271f46db7...). Device-install log should show `GNR1_INSTALLED`, `GNR1_SWITCH created`, `GNR1_BUNDLE`.

## 5. Phone experiments (in this order)

E1 — GNR fault decode (needs payload-gnr1 installed; camera bundle as now):
1. The trace switch is created by the install (remove it with the undo, or `rm /var/db/nord2-gpu-gnr1-trace`: noisy).
2. `python3 phone.py logs /dev/null` (rewind), launch Camera, back camera, PHOTO, point at a DIM scene (bracket of 4 only
   happens in the dark: in light the one-picture path fails earlier, see REPORT.md user6).
3. `python3 phone.py scenario photo runs/gnr1-photo --wait-s 8` (or `shutter --watch 10 --out runs/gnr1/x`), wait 15 s,
   `python3 phone.py logs runs/gnr1/all.log --seconds 30`.
4. Grep: `np GNR1 FAULT`, `GNR1 FAILED DISPATCH`, `GNR1   buffer`, `GNR1   texture`, `GNR1 dispatch` (the last ~30 before the
   first fault), `TTRX_3485`, `GPU JOBS: .*fault=[1-9]`, `_registerImages`, `_prepareOutput`, `processSequence`,
   `Delivering still image`. Send me the first FAULT line + the FAILED DISPATCH block.
E2 — Portrait evidence (any image; best together with E1's install): from `gpu/build/autotest`:
   `python3 phone.py scenario scenario-portrait-e2.txt runs/e2-portrait --wait-s 6` then
   `bash e2-portrait-grep.sh runs/e2-portrait`. The scenario: launch camera, `mode photo`, one right-to-left swipe to
   PORTRAIT, `mode state` (PORTRAIT label ~197 px), screenshots (whole + mode strip), wait, shutter with thumbnail watch,
   screenshots, logs, back to PHOTO. The grep script sorts the log into: session depth/portrait config, depth formats asked or
   refused, Camera.app Portrait lines, matte/segmentation/ANE/CoreML, errors, still outcome, driver faults.

## 6. Milestones for (A)

| # | what | effort | host-testable |
|---|---|---|---|
| M0 | E1 with y2s63-gnr1: which kernel/draw, which fault, which address/binding | 1 phone run | — |
| M1 | fix the faulting construct in the driver (expected: stride/pitch of texture-over-buffer, buffer offset, or a translator bug); host test with the exact kernel from GNR.videoprocessor's metallib through `metallib_compile_host_test.c` + the binding shapes from E1 | 0.5-2 days | yes |
| M2 | the wrong message: EIO from the wait = "the GPU job FAULTED (<decoded>)", not "did not finish within two seconds" (air_metal.c finish_or_say_why gets errno) | 1 h | yes |
| M3 | repeat E1 until `_registerImages:YES` = 0 and `processSequence` = 0; check fused output (compare the saved photo's noise vs a single frame, host script on the DCIM file) | 1-3 runs | partly |
| M4 | one-picture path in light (-1 / -17400 with params 0: the `name NO stream` guard in rwcam_diag.c) — separate from the GPU | 0.5 day | no |
| M5 | real HDR: honour `AEBracketedCaptureParams` per frame (EV- frame) in rwcam_core/kernel sensor writes, metadata per frame (exposure, gain) as GNR's `frameProperties_t.exposureParameters` expects | 1-2 days | yes (reader/AE unit tests) |

The other camera agents' trees are untouched; M4/M5 need a NEW copy of the newest camera tree (camera-userland-20261112-*).

## 7. Portrait (second priority)

- [V] Our `AVCaptureSession.plist` (D22) has no depth anything: no `DepthData*`, `Portrait*`, `Disparity`, `Matte`
  keys; the virtual `:6` dual-wide entry copies the stock dual "none of its depth data"; TrueDepth `:99`, tele `:2`, IR `:4`
  removed (tools/make_camera_plists.py).
- [V] The Camera app nevertheless shows PORTRAIT in the mode strip (phone.py README, run q10at4) and its sessions carry
  `VideoPreview depth:0 filters:1 [] Portrait Lighting effect ...` sinks; mediaserverd's
  `+[BWCoreImageFilterRenderer prewarmPortraitFilterVersion:...]` is stubbed by our bundle ("portrait filter prewarm
  skipped", rwcam_init.c:403).
- [V] Apple's depth/portrait Metal kernels compile on our driver (PSO lines: `roughDisparity`, `disparity_refinement_*`,
  `sparserendering_*`, `tvl1_*`, `kernelNccMatch`, `segmentation_fusion_apply`, `facemask_*`, ...) — creation only, never
  executed.
- [I] What PORTRAIT does today when tapped: the app asks a session with depth data delivery (or, on a single-camera device
  identity, a person-segmentation matte); with no depth format CMCapture either refuses (-12780 family) or the app falls
  back to a plain photo — E2 decides.
- [I] For a real Portrait two routes: (P1) declare depth on the `:6` dual-wide device (a depth companion format: 
  `_csp_configureMultiStreamCameraNode` refuses "depth without a depth companion format" [V REPORT.md]) and serve a
  disparity map from main+ultra-wide stereo (baseline ~1-2 cm: weak beyond 1.5 m); (P2) the single-camera Portrait of
  iPhone XR/SE: segmentation matte (Vision/Espresso person segmentation) — needs the ANE or CoreML on GPU/CPU; this port has
  no ANE [?: whether Espresso falls back to CPU/MPS on iOS 16.7 here is unverified]. Minimal viable: P2-like fake depth =
  our own GPU segmentation-free blur from face rect + focus distance served as a depth companion format. Effort 3-5 days
  after E2; not started.
