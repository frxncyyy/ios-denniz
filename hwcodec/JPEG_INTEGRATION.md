# JPEG_INTEGRATION — routing Camera JPEG stills to `/dev/redwood-jpeg` from inside mediaserverd

12 November 2026. Design only: nothing built, nothing run on the phone. Source: the iOS 16.7.16 (20H392, iPhone X) shared
cache on this machine, read with `gpu/video/research/{macho,dis,xr,calls,syms}.py` and
`gpu/build/camera-userland-20261109/tools/cachetools` (`RWCAM_WORK` = a prepared work directory, ROUTING.md section 6), and
the tree's own sources. Addresses are unslid cache addresses. No Apple binary or listing is copied: names, addresses and
short descriptions only. **[V]** = read out of the cache / a source file today (address or file:line given).
**[I]** = inference. Scratch: `/tmp/n2hw-opus-jpeg-fable/`.

## 0. Result in one paragraph

Three assumptions of JPEG_PHONE_TEST.md section 6b do not hold. (1) CMPhoto's calls into AppleJPEG do **not** go through a
GOT: every `applejpeg_encode_*` call branches to a *stub island* outside the image whose entries are direct
`adrp + add + br` to the function **[V]**, so rebinding a GOT slot changes nothing, and dyld interposing has nothing to
patch **[I]**. (2) The kernel-side `DYLD_INSERT_LIBRARIES` + AMFI dyld-policy mechanism is **not in the tree** **[V]**:
REPORT.md describes it as something the kernel "has to" do and later as "a kernel mechanism nobody wants".
(3) The AppleJPEG boundary is wider than "3-6 entry points": CMPhoto drives 13 encode functions, passes its own
quantisation tables (the hardware only has fixed quality codes), switches restart markers on, records restart offsets and
inserts its own table segment **[V]**. **Recommendation: hook one level up, with the mechanism the camera bundle already
uses on the phone (`method_setImplementation`): wrap `-[BWCMPhotoCompressionSession addMainImage:metadata:options:]` in
CMCapture.** The input there is the pixel buffer itself, fallback is "call the stock IMP with the untouched arguments", and
no text page, kernel or dyld change is needed. Open point that decides the size of the work: whether
`CMPhotoCompressionSessionAddCompressedImage` accepts a ready JPEG for a JFIF container (section 4, step 1).

## 1. What AppleJPEG exports and who calls it for encode

AppleJPEG.framework: image base 0x1d3bdb000, `__text` 0x1d3bdc588 + 0x39324, 117 symbols in the image's range, 101 of them
`applejpeg_*`, the rest `AJReducer*` **[V]**. There is no `AppleJPEGCompressSurface`-style name; the encode family is
(25 names) **[V]**:

| Export | Address | CMPhoto call sites | ImageIO call sites |
|---|---|---|---|
| `applejpeg_encode_create` | 0x1d3be0a00 | 2 (0x1a5defe14, 0x1a5df7530) | 2 (0x18bb3431c, 0x18bc6dde8) |
| `applejpeg_encode_destroy` | 0x1d3c15440 | 3 | 2 |
| `applejpeg_encode_set_option_type_baseline` | 0x1d3c14774 | 2 (0x1a5df03c4, 0x1a5df75ec) | - |
| `applejpeg_encode_set_option_type_lossless` | 0x1d3c14928 | 2 | - |
| `applejpeg_encode_set_option_q_tables` | 0x1d3c145f4 | 2 (0x1a5df03ec, 0x1a5df7614) | - |
| `applejpeg_encode_set_option_restart_markers` | 0x1d3c144a4 | 1 (0x1a5df0470) | - |
| `applejpeg_encode_set_option_num_threads` | 0x1d3c13f84 | 1 (0x1a5df0484) | - |
| `applejpeg_encode_set_option_record_rst_offsets` | 0x1d3c14b94 | 2 (0x1a5df052c, 0x1a5df0554) | - |
| `applejpeg_encode_set_option_insert_segment` | 0x1d3c14b10 | 1 (0x1a5df0608) | - |
| `applejpeg_encode_set_option_strip_encode` | 0x1d3c14a94 | 1 (0x1a5df0664) | - |
| `applejpeg_encode_commit_options_for_file` | 0x1d3c14cf4 | 2 (0x1a5df0678, 0x1a5df762c) | - |
| `applejpeg_encode_image_all` | 0x1d3c14ee8 | 2 (0x1a5df0764, 0x1a5df7644) | - |
| `applejpeg_encode_image_all_with_bitstream_offset` | 0x1d3c153ec | 1 (0x1a5df069c) | - |
| `applejpeg_encode_options_init` | 0x1d3be76ec | - | 2 |
| `applejpeg_encode_set_options_file` / `_mem` | 0x1d3be19ec / 0x1d3c13ed0 | - | 1 / 1 |
| `applejpeg_encode_image_row` | 0x1d3be1678 | - | 2 (0x18bb3474c, 0x18bc6de94) |

Not called by either: `_set_option_quality` (0x1d3c14584), `_set_option_max_filesize`, `_set_option_format`,
`_set_option_type_progressive`, `_set_option_skip_app0_header`, `_set_option_callback`, `_reset_options`,
`_commit_options_for_mem` **[V]**. CMCapture calls no `applejpeg_*` at all **[V]**; it calls
`CMPhotoCompressionSession*` (section 3c). ImageIO uses a *different* API shape (`options_init` + `set_options_file/mem` +
row-by-row `encode_image_row`), so one hook cannot serve both clients **[V]**. Both also use the `applejpeg_recode_*`
family (JPEG to JPEG) which is not an encode from pixels.

**The CMPhoto software encoder** is one static function at 0x1a5defd10 (no symbol; called from 0x1a5d6b7d4 and
0x1a5d6b8f8, inside the function that J02/rwcam_diag cite at 0x1a5d6a990) **[V]**. A second, smaller user of the same
calls is around 0x1a5df7530..0x1a5df7650. Named neighbours: `CMPhotoUnifiedJPEGEncoderCreate` 0x1a5d942f0, `...Encode`
0x1a5d8fb50, `...EncodeStripToStream` 0x1a5defc3c, `...IsHardwareAvailable` 0x1a5def680, `...DidUseHW` 0x1a5defcdc (the
`AppleJPEGDriver`-capable wrapper; `...Encode` is called from 0x1a5d6c454 and the JFIF transcode functions) **[V]**.

Argument shapes read at 0x1a5defd10 **[V unless marked]**:
- The function itself: x0 = encoder context (CFAllocator at +0; restart-table state at +0x20..+0x40), x1 = the
  `CVPixelBuffer`, x2 = CMPhoto's parsed option structure (+0x4/+0x5 strip-mode bytes, +0x18 strip index, +0x88 lossless
  byte, +0xb0/+0xb8/+0xc0 three quantisation-table pointers), x5 = optional output stream object (CMBaseObject vtable
  write); without it the output is a `CFMutableData`.
- `applejpeg_encode_create(const callbacks *)`: one argument, a 24-byte block of three pointers copied from CMPhoto
  `__DATA_CONST,__const` 0x1d8057768 (memory callbacks **[I]**); returns the session, NULL = failure.
- Every `set_option_*`: `(session, options *, value...)`, returns 0 for success. `options` is a caller-owned block on the
  stack, zeroed over 0x70 bytes, **filled in line by CMPhoto, not by calls**: +0x0 width, +0x4 height (u32, from the
  integral crop rectangle), +0x8.. a format descriptor written by an internal helper from the pixel format, +0x50 16 bytes
  (row bytes **[I]**). `type_baseline(session, opts, w2)`, `q_tables(session, opts, 3, pointer array)`,
  `restart_markers(session, opts, 1)`, `num_threads(session, opts, 2)`, `record_rst_offsets(session, opts, count, u32
  array, base offset)`, `insert_segment(session, opts, length, bytes, &position)`, `strip_encode(session, opts, first,
  last, rst index, w5)`.
- `commit_options_for_file(session, opts, io *)`: `io` is a 0x38-byte block with a write callback (0x1a5df0c0c) at +0x8 and
  its context at +0x20. The JPEG leaves through that callback, not through a returned buffer.
- `encode_image_all(session, image *, out *)`: `image` starts with the two plane base addresses (from
  `CVPixelBufferLockBaseAddress` + a plane helper), further fields not decoded; `out` is one 8-byte cell.
  `..._with_bitstream_offset` has a fourth pointer argument and is the strip-mode variant.
- Pixel formats the function tests: 420f, 420v, 422 and 444 8-bit pairs, the 10-bit `x420/xf20/x422/xf22/x444` family and
  packed variants; it reads `kCVImageBufferYCbCrMatrixKey` and compares with ITU-R 601.

Unknown: the full layout of `options` and `image`, the meaning of the return codes other than 0, what
`image_all` does when a write callback fails.

## 2. Which process encodes Camera stills on iOS 16.7

**mediaserverd.** Evidence: (a) the shared cache has no image whose path contains `cameracapture` and CMCapture's
`__cstring` has no `cameracaptured`, while it has `mediaserverd` five times (0x1a2358ae0, 0x1a236a2c5, 0x1a237ba7c,
0x1a237c776, ...) **[V]**; the separate capture daemon belongs to later releases **[I]**. (b) `rwcam_init.c:17-22`:
mediaserverd's `main()` starts CMCapture's servers (`FigMediaServerStart` -> `FigStartCaptureServers`), and our bundle,
loaded there as part of RedwoodAudio.driver, starts them itself **[V source; run on the phone per REPORT.md]**. (c) The
encoder node is CMCapture's: `BWPhotoEncoderNode` (init 0x1a204c470), `BWCMPhotoCompressionSession` (11 methods),
`BWIntermediateJPEGCompressor` **[V]**. (d) Camera photographs JPEG today because `rwcam_diag.c:110-149` removes
HEVC/HEIF/PrefersHEVC from the source attributes **[V source]**.

Correction to ROUTING.md section 5: the class `BWIntermediateJPEGCompressor` exists
(`-newJPEGSampleBufferFromUncompressedSampleBuffer:` 0x1a1f10838, `-_setupJPEGCompressionSession` calls
`CMPhotoCompressionSessionCreate` at 0x1a1f6c1bc) **[V]**; only the method name `_setupJPEGCompressionOptions` does not.

## 3. Hook mechanisms

Facts common to all: the cache is `dyld_v1   arm64` (not arm64e: no PAC, no auth stubs/auth GOT) **[V header]**; header
`cacheType` = 2 (universal), `cacheSubType` (0x1c8) = 1 **[V]**; the directory holds no `.development` sub-cache file
**[V]**. What the camera code in mediaserverd does today: `objc_getClass` + `class_getInstanceMethod` +
`method_setImplementation` (`rwcam_diag.c:374-386` `wrap()`, ten wrapped methods at `:1536-1550`; `rwcam_init.c:384,402`),
`os_log_set_hook`, `objc_setExceptionPreprocessor`, `dlsym(RTLD_DEFAULT, ...)`, `_dyld_register_func_for_add_image`, and
writes to two VideoToolbox statics in writable `__DATA` (`rwcam_init.c:139-215`) **[V]**. It uses no `vm_protect`/
`mprotect`, no GOT rebinding, no `__interpose`, no text patch **[V: grep over src/]**.

### 3a. Rebinding CMPhoto's GOT entries: does not work
- CMPhoto's `bl` for every `applejpeg_encode_*` goes to a stub island at 0x1a9450cc0..0x1a9450d50 (12-byte entries, e.g.
  0x1a9450ccc create, 0x1a9450ce4 image_all, 0x1a9450cf0 with_bitstream_offset), in sub-cache `.12`, mapping 0x1a9420000 +
  0x5c000 with mapping flags 8 (text stubs), max/init protection r-x. Each entry is `adrp x16; add x16; br x16` straight
  to the AppleJPEG function: **no data load** **[V]**. ImageIO's island is at 0x18dbdd318..0x18dbdd354, same form;
  CMCapture's entry for `CMPhotoCompressionSessionAddImage` is 0x1a25dc88c, same form **[V]**.
- Counts: CMPhoto 9852 calls through island/direct entries against 765 through its own `__stubs` (GOT-loading form);
  ImageIO 17995 / 8252; CMCapture 37014 / 2227 **[V]**. None of the applejpeg calls is in the GOT group **[V]**.
- A GOT slot per symbol does exist (uniqued, outside every image's segments: 0x1dacd2b50..0x1dacd2bd0, e.g. image_all at
  0x1dacd2b68; shared by CMPhoto's and ImageIO's unused in-image stubs; mapping `.27.dylddata` flags 4, max rw-, init
  r--, so `vm_protect` to rw would succeed **[I]**), but nothing on the call path reads it **[V]**.

### 3b. `DYLD_INSERT_LIBRARIES` + `__interpose`: not available, and would not reach these calls
- The kernel mechanism is not implemented: `grep -rn DYLD_INSERT_LIBRARIES` over `gpu/build` and `xnu` finds only the
  stock `libsyscall/wrappers/spawn/posix_spawn_filtering.c`, `build_camui.sh`, `rwcam_camui.c` and REPORT.md;
  `gpu/build/xnu-stage/bsd/kern/kern_exec.c` has neither `OS_ACTIVITY_DT_MODE` nor an insert; `sweet_amfi.c` (383 lines)
  has no dyld-policy answer; none of the kernel patch directories (`hwmodel-20261110`, `camfix-…`, …) has one **[V]**.
  REPORT.md:36 states the requirement, REPORT.md:42 says libredwoodcamui is "left in place, unused: they need a kernel
  mechanism nobody wants" **[V]**.
- Even with it: dyld applies interposing to cache images by rewriting the recorded *pointer* uses of an export (GOT and
  data slots). A production stub island has no pointer to rewrite, and the development stubs that would make it possible
  are not on the phone **[I: from dyld's design; no dyld source on this machine, so no file:line]**.

### 3a'. Text patch of the island entries (the only way to hook at the AppleJPEG boundary): possible, not recommended
Rewrite the `adrp/add` pair of the CMPhoto island entries to point at our functions (the bundle sits within the +-4 GB an
`adrp` reaches **[I]**; the original is simply the export's address from `dlsym`). Needs: `vm_protect(VM_PROT_COPY)` on
shared-cache text and execution of the modified private page. Our kernel keeps iOS defaults
(`kern_cs.c:95-96` both enforcement defaults 1; `sweet_amfi.c:256-275` marks every binary `CS_SIGNED`); whether a
modified text page is killed there has **never been measured** **[I]**. `sweet_amfi.c` implements no
`proc_check_run_cs_invalid` hook (`:309`, `:350-351` are the only hooks), so `cs_allow_invalid()` (`kern_cs.c:193`) would
permit it for a process that asks, but nothing asks today **[V source, I consequence]**. On top of the mechanism risk it
inherits the whole semantic surface of section 1 (struct layouts, q-tables, restart offsets, strip mode, write callback).

### 3c. ObjC wrap one level up: recommended
`BWCMPhotoCompressionSession` (CMCapture) is a thin ObjC wrapper over the CMPhoto session **[V]**:

| Method | IMP | What it calls |
|---|---|---|
| `-init` | 0x1a1fd1118 | `CMPhotoCompressionSessionCreate` at +0x98 |
| `-openContainerWithOptions:settingsID:` | 0x1a1f8ae68 | `CMPhotoCompressionSessionOpenEmptyContainer` at 0x1a1f8ae84 |
| `-addMainImage:metadata:options:` | 0x1a1f28e94 | `CMPhotoCompressionSessionAddImage` at +0xc (0x1a1f28ea0) |
| `-addThumbnailImage:options:` | 0x1a204bec8 | `CMPhotoCompressionSessionAddThumbnail` (0x1a204bedc) |
| `-addCompressedThumbnailImage:thumbnailImageSize:options:` | 0x1a204bee0 | - |
| `-addAuxImage:type:auxImageMetadata:options:` | 0x1a204bef0 | `CMPhotoCompressionSessionAddAuxiliaryImage` |
| `-addMetadata:` | 0x1a1f28848 | `CMPhotoCompressionSessionAddMetadataFromImageProperties` |
| `-closeContainer`, `-closeContainerAndCopySurfaceOut:` | 0x1a1f2ca4c, 0x1a1f21840 | `...CloseContainer`, `...CloseContainerAndCopyBacking` |
| `-containerSize`, `-isContainerOpen` | 0x1a1fb83d0, 0x1a1f09780 | |

`wrap("BWCMPhotoCompressionSession", "addMainImage:metadata:options:", ours, &stock)` is exactly what `rwcam_diag.c`
already does ten times in this process; it needs no address from the cache (class + selector + type-encoding check), no
kernel change and no text or `__DATA_CONST` write. CMPhoto exports `CMPhotoCompressionSessionAddCompressedImage`,
`...AddExif`, `...AddXMP`, `...AddCustomMetadata` (names in the image's symbols **[V]**; signatures not read).
Other CMPhoto users in CMCapture (`BWIntermediateJPEGCompressor` 0x1a1f109b0, `BWCompressedShotBufferNode` 0x1a1ffa6dc,
`BWDeferredBufferIntermediate` 0x1a2180450: intermediate/Live-Photo/deferred buffers) call the C function directly and stay
software; they are not the still that reaches the photo library **[I]**.

**Recommendation: 3c.** 3a is dead, 3b does not exist and would not work, 3a' needs an unmeasured kernel property and the
largest ABI. ImageIO in app processes (Photos export) is out of reach of all of these without a per-process injection
mechanism; leave it software.

## 4. What the replacement does

Inputs **[V source for our side, I for what the node passes]**: our bundle delivers stills as IOSurface-backed
`CVPixelBuffer`s in '420f' by default, '420v' or BGRA on request (`rwcam_device.c:338-369, 1631-1695`), 4032x3024 (format 0)
or 4096x3072 with the `--still-4096` plist (REPORT.md:544-545). Both fit `rwjpeg.h` (even, 16..8192, <= 4096x3072 pixels).
Whether the first argument of `addMainImage:` is the pixel buffer or a sample buffer, and the keys of `options`
(quality, codec type, crop, orientation), are **not read yet**: step 0 logs them.

Wrapped `addMainImage:metadata:options:` in order:
1. **Decide before touching anything.** Use the stock IMP unchanged when: the container is not JFIF/JPEG (HEIC with
   `keephevc`), the buffer is not 8-bit 4:2:0 bi-planar ('420f'; '420v' only after step 3 below), width or height odd or
   outside 16..8192 or over 12.6 MP, a crop/scale/rotation the options ask of the encoder, `rwjpeg_present()` is 0, or
   `/var/db/nord2-camera-swjpeg` exists (a new off switch, same style as the other markers).
2. Lock the buffer read-only, `rwjpeg_encode_nv12(dev, Y base, CbCr base, strides, w, h, q, out, cap, &bytes, &hw_us)` with
   `cap` = w*h/2 + 4096 first and one retry on `RWJPEG_E_NOSPC`. Quality: map the session's quality option (0..1) to
   1..100; the hardware has fixed codes (redwood_jpeg.h:26), so CMPhoto's own quantisation tables are **not** reproduced:
   file sizes and quality differ from stock at the same slider value **[V limit, I effect]**.
3. '420v' (video range): JFIF is full range. Either expand in a CPU pass (about the cost of the BGRA conversion) or fall
   back; start with fallback.
4. Hand the result to the container. **Variant A** (preferred, if step 1 of the plan confirms it):
   `CMPhotoCompressionSessionAddCompressedImage` on the wrapped object's session, so CMPhoto keeps writing EXIF, the
   thumbnail, auxiliary images and MPF exactly as now. **Variant B**: wrap `addMetadata:`, `addThumbnailImage:options:`,
   `closeContainer*` too and build the file ourselves: hardware stream + EXIF APP1 through
   `rwjpeg_insert_segments` (it drops the hardware's APP0 when an EXIF segment is given), EXIF bytes from ImageIO's
   metadata functions **[I]**, thumbnail 160x120 through a second small `rwjpeg` job or left out.
5. **Any failure after step 1** (`RWJPEG_E_FALLBACK`, `E_JPEG`, `E_NOMEM`, a non-zero status from the CMPhoto call):
   unlock, log one `N2JPG` line, call the stock IMP with the original arguments. In variant A nothing has been added to the
   session before that point, so the fallback is clean; that is the main reason to hook here and not below.

Threading: the node encodes on its own queue and may run two sessions (main image + Live Photo key frame) **[I]**; one
process-wide `rwjpeg` handle behind a mutex (the kernel serialises jobs anyway, rwjpeg.h "Threading"). Never hold the
mutex across the stock IMP.

## 5. Plan, effort, risks

| Step | Work | Days |
|---|---|---|
| 0 | Logging wrap only (no behaviour change): class of the image argument, pixel format, size, the `options` and `openContainer` dictionaries' keys, time spent in the stock IMP at 12 MP. One boot. Gives the software baseline that 6b lacks. | 0.5 |
| 1 | Read `CMPhotoCompressionSessionAddCompressedImage` (arguments; does a JFIF container take a compressed JPEG main image; does it re-parse or re-encode) | 1 |
| 2 | Variant A hook + host test against a made-up ObjC runtime (the `t_camui` pattern) and the `rwjpeg` ioctl double | 2 |
| 2' | Variant B instead, if step 1 says no: own JFIF assembly, EXIF, thumbnail | +3 |
| 3 | '420v' range expansion, quality mapping table against step 0's file sizes | 1 |
| 4 | Phone run: 12 MP, burst of 5, fallback forced by the marker, Photos opens the files, EXIF orientation and date | 1 |
| alt | 3a' island patch: kernel CS answer + struct RE + 13 function shims | 5-7 and one kernel patch |

Risks, most important first:
1. **12 MP single job is still unmeasured on the phone** (JPEG_PHONE_TEST.md section 7 item 2). If the block needs strips,
   the hook waits for K01's strip support or falls back above the measured limit. The design degrades to software, it
   does not break stills.
2. **Variant A may not exist for JFIF** (the function may be HEIF-only). Then variant B, 3 more days, and EXIF/MakerNote
   fidelity becomes our problem.
3. **Quality/size mismatch**: fixed hardware quality codes against CMPhoto's tables; the Camera "Most Compatible" file
   size will change. Needs a table from step 0, not a formula.
4. **Missing Apple extras**: stock files carry restart markers and CMPhoto's inserted restart-offset segment
   (`record_rst_offsets` + `insert_segment`, 0x1a5df052c / 0x1a5df0608) **[V]**; hardware files will not. Decoders do not
   need them **[I]**; Apple's own fast/parallel JPEG decode may be slower on these files **[I]**.
5. **Failure semantics of the wrapped method** (return type and what the node does with an error) are read from the type
   encoding at wrap time; a wrong guess is avoided by always ending in the stock IMP on any doubt.
6. **Gain is unmeasured**: 12 MP software time is not known (two encoder threads are requested, 0x1a5df0484 **[V]**);
   hardware estimate ~30 ms + two 18 MB copies **[I]**. Step 0 decides whether steps 1-4 are worth doing.
7. Power/clock sharing with VENC (JPEG_PHONE_TEST.md section 7 item 3) applies when a still is taken during recording.

## 6. Reproduce

```sh
export RWCAM_WORK=<prepared dir> N2_TMP=/tmp/n2hw-opus-jpeg-fable; cd gpu/video/research
python3 syms.py <cachetools dir> AppleJPEG.framework/AppleJPEG .            # the 117 names
python3 calls.py 0x1a5d69640 0xb2214 'applejpeg'                            # CMPhoto call sites per export
python3 calls.py 0x18bab1c3c 0x363e80 'applejpeg'                           # ImageIO
python3 calls.py 0x1a1ee8ef0 0x2f760c 'applejpeg|CMPhotoCompressionSession' # CMCapture
python3 dis.py 0x1a5defd10 0x1a5df0850 -k                                   # the software encoder's calls (private output)
```
Stub form: read 12 bytes at each `bl` target outside `__text` and classify `adrp+add+br` (direct) against `adrp+ldr+br`
(GOT). Mapping flags: the 56-byte mapping-with-slide records at header offset 0x138 of each sub-cache. Method table:
`cachetools/methods.py`. Kernel facts: `grep -rn` as quoted in section 3b.
