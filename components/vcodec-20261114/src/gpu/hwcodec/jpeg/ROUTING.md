# ROUTING — can stock iOS photo paths use a VideoToolbox `'jpeg'` encoder instead of `AppleJPEGDriver`?

J02, 11 November 2026. Scope: the iOS 16.7.16 (20H392, iPhone X) dyld shared cache on this machine
(`$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld`), read with the tools in `gpu/video/research`
(`macho.py`, `dis.py`) and `gpu/build/camera-userland-20261109/tools/cachetools` (`dsc2.py`, `methods.py`, `xr.py`).
Addresses are unslid cache addresses. No Apple binary is copied; only names, addresses and short strings appear here.
**[V]** = read out of the cache today, with the address given. **[I]** = inference.

## 0. Answer in one paragraph

A client that itself calls `VTCompressionSessionCreate` with codec type `'jpeg'` gets the MT6893 block with our plug-in
alone: VideoToolbox selects a registered encoder by codec type and `EncoderID`/hardware flags, and the plug-in is the only
`'jpeg'` encoder that exists on the phone. **The stock photo paths do not do that.** No instruction in CMPhoto, ImageIO,
AVFoundation, AVFCapture, MediaToolbox, VideoToolbox or CMCapture builds the `'jpeg'` FourCC, and no CMPhoto section contains
it **[V]**; ImageIO does not link VideoToolbox at all **[V]**; AVFCapture links it but creates no VT session **[V]**; and
CMPhoto's hardware JPEG lever is an IOKit service named `AppleJPEGDriver` (the kext that does not exist in our XNU), not a
VideoToolbox session **[V for the query, I for the exclusive use]**. So Camera / `AVCapturePhotoOutput` JPEG stills and
ImageIO's JPEG writer need a *different hook* than the plug-in: interpose at CMPhoto's `applejpeg`/`AppleJPEGDriver` boundary
or in ImageIO, or use our own camera path. The plug-in by itself gives hardware JPEG only to VideoToolbox `'jpeg'` clients
(our probe/tool, `VTCompressionSession` apps, and M3's `VTCompressionSession('jpeg')` measurement).

## 1. What the four candidate clients really do

| Client (process) | Path on a real iPhone X | Links VideoToolbox? | Creates a VT JPEG session? | Can our plug-in alone reach it? |
|---|---|---|---|---|
| Camera / `AVCapturePhotoOutput`, "Most Compatible" (mediaserverd) | AVFCapture → CMPhoto compression session → AppleJPEG (software) or `AppleJPEGDriver` (kext) | AVFCapture yes, CMPhoto yes | **no** (see §3) | **no** **[I]** |
| Photos / ImageIO JPEG writer, `CGImageDestination` | ImageIO → `AppleJPEG` / `IIORecodeAppleJPEG_to_JPEG` / `IOSurfaceAccelerator` | **no** **[V]** | no | **no** **[V+I]** |
| `VTCompressionSession('jpeg')` client (our tools, third-party apps, M3 probe) | VideoToolbox encoder registry → our `Slim.videoencoder` | yes (it is VideoToolbox) | **yes** | **yes** **[V by construction]** |
| Movie recording / `AVAssetWriter` (`avc1`, `hvc1`) | MediaToolbox → VT video encoder | yes | creates VT sessions, but for video codecs | n/a (H.264/HEVC plug-in) |

## 2. Evidence: VideoToolbox is not asked for JPEG

- **No `'jpeg'` FourCC is ever built** in CMPhoto, ImageIO, AVFoundation, AVFCapture, MediaToolbox, VideoToolbox or CMCapture:
  a scan of every `__TEXT` for `movz wN,#0x6567` + `movk wN,#0x6a70,lsl#16` (and the X-register form) finds zero sites, and a
  byte scan for `67 65 70 6a` (little-endian `0x6a706567`) finds no hit in CMPhoto's sections **[V]**. (The constant can of course be
  produced at run time by a string→FourCC converter; see the next bullets for why that does not happen on the JPEG path.)
- **CMPhoto's VT session creations** are three call sites of `VTCompressionSessionCreate` (target `0x194cd162c`, anchored at
  CMCapture's known call `0x1a213f7bc`): `0x1a5d94c98`, `0x1a5dad524`, `0x1a5e0cb40` **[V]**. The wrapper that holds the first one
  (`0x1a5d94ae8`) takes the codec type from a caller-supplied configuration field (`ldr w25,[x1,#8]` at `0x1a5d94b2c`), so the
  codec type is not a JPEG literal either **[V]**; the second takes it from a configuration object (`ldr w3,[x20,#0x28]` at
  `0x1a5dad4f8`) **[V]**.
- **CMPhoto's JPEG hardware lever is an IOKit service**: `0x1a5d99028` calls `IOServiceMatching("AppleJPEGDriver")`
  (`0x1a5d99058`, string at `0x1a5e27de9`) and `IOServiceGetMatchingService` (`0x1a5d99068`), stores the `io_service_t` and then
  reads the driver's properties `AppleJPEGNumCores` (`0x1a5e27df9`), `AppleJPEGSupportsAppleInterchangeFormats` (`0x1a5e27e0b`),
  `AppleJPEGSupportsMissingEOI` (`0x1a5e27e34`), `AppleJPEGSupportsRSTLogging` (`0x1a5e27e50`) **[V]**. There is no equivalent IOKit
  query for a VideoToolbox encoder on the JPEG path.
- **CMPhoto is bound to AppleJPEG**: the image has 50 distinct `__stubs` targets inside `AppleJPEG.framework` (0x1d3bdb000 +
  offsets, e.g. 0x1d3c00438, 0x1d3c0f72c, 0x1d3c14cf4) **[V]**, and links `AppleJPEG.framework` **[V]**.
- **ImageIO does not link VideoToolbox at all** (its LC_LOAD_DYLIB list has CoreFoundation, IOSurface, AppleJPEG,
  IOSurfaceAccelerator and no VideoToolbox) **[V]**. It contains `ImageIO_Use_AppleJPEG` (0x18bad1cc8),
  `IIORecodeAppleJPEG_to_JPEG` (nine references, first 0x18bc6e4a8) and `kCMPhotoCompressionOption_RequireHardwareCodec`
  (0x18bad4b80) **[V]**. Its JPEG writer therefore cannot select a VT plug-in without a new hook.
- **AVFCapture** (where `AVCapturePhotoOutput` lives) has zero `VTCompressionSessionCreate` call sites, while linking CMPhoto,
  CMCapture and VideoToolbox **[V]**. Its JPEG stills go through CMPhoto.
- **CMCapture** has exactly one `VTCompressionSessionCreate` site, `0x1a213f7bc`, inside the video compressor
  (`-[BWVideoCompressorNode ...]`, the surrounding code reads `CVPixelBuffer` properties and calls `VTCompressionSession*`), and
  its still-photo path uses `BWCMPhotoCompressionSession` (`-[BWCMPhotoCompressionSession addMainImage:metadata:options:]`
  at IMP 0x1a1f28e94, `-openContainerWithOptions:settingsID:` at 0x1a1f8ae68) **[V]**. No JPEG VT session.

## 3. What `kCMPhotoCompressionOption_RequireHardwareCodec` gates

- CMPhoto parses the option dictionary keys `PreferSoftwareCodec`, `RequireHardwareCodec`, `AllowPackedBuffers`, … into a
  per-session structure: `RequireHardwareCodec` is read at `0x1a5d6dbb4`, `0x1a5d8e134`, `0x1a5d92480`, `0x1a5dde5a4`
  (CFString `@"RequireHardwareCodec"`, the object at 0x1d805a488) and stored as a byte (e.g. `strb w0,[x19,#0x1b]` after the
  read at `0x1a5d6dbbc`) **[V]**. On one path the option is copied into a dictionary handed to a lower layer
  (`0x1a5d9062c`, `CFDictionarySetValue`) **[V]**.
- The consumer at `0x1a5d95910` reads that byte and maps the flag pair to a small enum:
  `ldrb w8,[x25,#0x1b]` (RequireHardwareCodec) `cbnz` → `…,[x25,#0x1a]` → `mov w8,#2` else `[x25,#0x18]` → `mov w8,#1`, stored
  with `str w8,[x28]` at `0x1a5d9593c`; `PreferSoftwareCodec` (`[x25,#0x19]`) takes the other branch **[V]**. In other words the
  option is a **selector between a software and a hardware JPEG codec inside CMPhoto**, not a request to VideoToolbox.
- Which implementation the enum selects is not read out line by line. All the surrounding evidence says software = the linked
  `AppleJPEG.framework`, hardware = the `AppleJPEGDriver` IOKit service. There is no VT JPEG codec anywhere, so it cannot be a VT
  encoder **[I, high confidence]**.

`RequireHardwareCodec` therefore behaves as: "do not silently use the software JPEG encoder; use the hardware one, and fail if it
is not there" (the exact failure return is not decoded) **[I]**. That is why CMPhoto on our phone is expected to fail the
"Most Compatible" still instead of falling back to `applejpeg` **[I]** — a fact M3 should measure, not assume.

## 4. Conclusion and the hooks that would be needed

**With the plug-in alone (no other change), hardware JPEG is available to:**
1. our own `rwjpeg_tool`/probe and any app that calls `VTCompressionSessionCreate(kCMVideoCodecType_JPEG, …)` **[V by
   construction]**;
2. `VTCompressionSession` clients that select by `EncoderID` `com.nord2.videotoolbox.videoencoder.jpeg.mtk` or set
   `RequireHardwareAcceleratedVideoEncoder` (VideoToolbox's filter honours both keys: `EnableHardwareAcceleratedVideoEncoder`
   and `RequireHardwareAcceleratedVideoEncoder` are VideoToolbox `__cstring`s at 0x194fdd28f / 0x194fdd2f7 and are used by
   `vtFilterRegistryItemByCodecTypeAndVideoEncoderSpecification` @0x194ed8910 per `gpu/video/PLAN.md` §1.3) **[V]**.

**Not reachable with the plug-in alone, and the hook each needs:**
1. **Camera / `AVCapturePhotoOutput` JPEG stills.** CMPhoto asks IOKit for `AppleJPEGDriver`, not VideoToolbox. Options:
   (a) interpose the CMPhoto → AppleJPEG calls in mediaserverd (the `applejpeg_encode_*` entry points; 50 stub targets give the
   exact addresses) and route them to an XPC connection to our encoder daemon/tool; (b) interpose the CMPhoto codec-session
   factory itself; (c) bypass CMPhoto with our own capture path. (a) is the smallest hook but lives in Apple's process; (b) is
   more code but avoids decoding Apple's AppleJPEG ABI. A fake `AppleJPEGDriver` IOKit service is a kernel project.
2. **Photos / ImageIO JPEG export.** ImageIO has no VideoToolbox link; it calls `AppleJPEG` (and `IOSurfaceAccelerator` for
   scaling). A hook would have to interpose `AppleJPEG`'s encode entry points in the exporting process, or replace ImageIO's
   writer. This is the same AppleJPEG ABI problem as (1) but without mediaserverd.
3. **AVAssetWriter / MediaToolbox movie recording** is unaffected: it asks VideoToolbox for `avc1`/`hvc1`, which the other
   plug-ins provide; it does not produce JPEG.

**Recommendation for M3:** measure `VTCompressionSession('jpeg')` end to end on the phone with the plug-in (it should work and is
the only stock-API route), and measure whether CMPhoto's "Most Compatible" still fails or falls back. If it fails, the next
decision is whether to interpose `AppleJPEG` in mediaserverd — a new package, not a change to this plug-in. Do not claim Camera
JPEG support from the plug-in alone.

## 5. Corrections to `gpu/video/PLAN.md` noticed while checking

- `-[BWIntermediateJPEGCompressor _setupJPEGCompressionOptions]` **does not exist in this cache** **[V]**: the CMCapture method
  table (10 363 methods) has no selector containing `setupJPEG` or `CompressionOptions`, and no `setupJPEGCompressionOptions`
  string exists anywhere in the cache. The `RequireHardwareCodec` parsing is in CMPhoto (`0x1a5d6dbb4` etc.), not in that class.
  The PLAN's statement that this method sets the option should be re-checked against its original evidence; the effect it
  describes (CMPhoto is told to require a hardware codec) is real, but the method name/owner is not.
- The claim "CMPhoto calls `applejpeg_encode_*` directly (41 call sites)" is consistent with what is here **[V for the link and
  the 50 AppleJPEG stub targets; the exact 41 count was not re-derived]**.

## 6. How to reproduce (no bulk dumps)

Preparation (private cache, read-only): `RWCAM_WORK` must contain the decompressed linkedit subcaches; see
`gpu/build/camera-userland-20261109/tools/setup_cache_work.sh`. Then:

```sh
# exports / CFString / xrefs: gpu/video/research/dis.py and xr.py, with an allsyms.pkl for the cache
RWCAM_WORK=<dir> python3 gpu/video/research/macho.py 'CMPhoto.framework/CMPhoto'
RWCAM_WORK=<dir> python3 gpu/build/camera-userland-20261109/tools/cachetools/methods.py /tmp/cmcap.pkl
# the scans quoted above are small scripts over dsc2.rd()/dis.follow(); they print addresses only, never file contents
```

The numbers above were produced with: `methods.methods(base)` for IMPs; `xr.scan` with
`RequireHardware|AppleJPEG|HardwareCodec`; a `VTCompressionSessionCreate` call-site scan comparing `dis.follow(bl target)` to
`0x194cd162c` (anchored at CMCapture `0x1a213f7bc`); `dis.dis()` on the addresses in §2-§3.
