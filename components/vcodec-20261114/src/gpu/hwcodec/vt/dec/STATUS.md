# DEC06 — VideoToolbox decoder plug-in (`Slim.videodecoder`, `SLIMRegister`)

Owner: hw-dec06. Files: `gpu/hwcodec/vt/dec/**` only. **Host-tested only: nothing was run on the phone, nothing was
published, no commit outside my owned files.** The suite reads `gpu/video/include`, `gpu/video/sdk` and (for the adapter
check) `client/dec`, `daemon/dec` and `daemon/dec/test/stage_proto.py`; it writes only under `/tmp/n2hw-DEC06`.

## What is implemented

| File | What |
|---|---|
| `n2vtd_vt.h` | Factories `n2vtd_vt_create_avc/_hevc`, `n2vtd_vt_register_all()`, `SLIMRegister()`, the cached availability probe, and the read-only statistics struct. |
| `n2vtd_obj.c` | The object: one `CMBaseObject` of class `VTVideoDecoderGetClassID()` via `CMDerivedObjectCreate`, class record **version 2** with `startSession`, `decodeFrame`, `copySupportedPropertyDictionary`, `setProperties`, `copySerializableProperties`, `canAcceptFormatDescription`, `finishDelayedFrames`; tile slots NULL. StartSession parses the parameter sets, connects, DEC_HEADER, registers the pool, sets the pixel-buffer attributes. DecodeFrame converts the sample to Annex B, submits it, drains pictures and emits each frame token exactly once in the daemon's display order with its pts. FinishDelayedFrames FLUSHes, drains, then emits every remaining token as `FrameDropped`. Idempotent invalidate/finalize. Base `copyProperty` answers `UsingHardwareAcceleratedVideoDecoder = true` and `IsHardwareAcceleratedVideoDecoder = true`. |
| `n2vtd_nal.{h,c}` | avcC (ISO/IEC 14496-15 5.2.4.1.1) and hvcC (8.3.3.1.1) record parsers; reading the atoms out of the `CMFormatDescription` sample-description extension dictionary (with the `CMVideoFormatDescriptionGet*ParameterSetAtIndex` fallback); H.264/H.265 SPS parsers (coded/display size, conformance window, chroma, bit depth, VUI `video_full_range_flag`, interlaced flag; H.265 short-term RPS and scaling lists included so the VUI is reachable); parameter-set comparison for `CanAcceptFormatDescription`; length-prefixed (AVCC/HVCC) → Annex B conversion with in-band/Annex-B passthrough detection and NAL-type reporting. |
| `n2vtd_pixels.{h,c}` | MM21 detile to NV12 `420v`/`420f`, linear NV12 copy, I420→NV12; destination strides from the `CVPixelBuffer`; clean-aperture origin applied. The MM21 layout is **16-byte x 32-row luma tiles and 16-byte x 16-row chroma tiles (256 B)**, the libyuv-verified layout in `facts/MM21_LAYOUT.md` §2; the object uses opus-mm21's `mm21/n2_mm21.c` staged converter when it is on the include path and this file's own converter (byte-identical in the differential test) otherwise. Refuses geometry it cannot express (odd crop origin, stride not a multiple of 16, height not a multiple of 32, out-of-range crop) instead of guessing. |
| `n2vtd_client.h` | The one seam: connect, open(codec,max size,layout), header, add_pool, input, picture, release, buffer_rw(plane), free_buffers, flush, close. |
| `n2vtd_client_dec05.c` | The product adapter over DEC05's `client/dec/n2vcd_dec.h`. Compiled only when `include/n2vcd_proto.h` has the rev-2 decoder messages (`N2VCD_PROTO_REV2`); until hw-lead lands them it is an empty translation unit and the stand-in below is the default. The suite compiles it and DEC05's client against DEC05's staged rev-2 header and checks every `n2vcd_*` call resolves. |
| `n2vtd_client_proto.c` | A stand-in that speaks the rev-2 decoder messages directly over the frozen `struct n2vcd_transport` (32-byte `DEC_ADD_BUFFER` with the optional second plane, `OPEN.flags` bits 8..15 = layout, `DEC_PICTURE_OK` last word = poc). Real, seq-checked, timeout-bounded; it is what the host client test drives. HELLO uses `N2VCD_VERSION` = 1 as the lead decided on 2026-11-11 (decoder ids are additive; the daemon advertises them in `HELLO_OK.dec_codecs`, which must be non-zero). |
| `n2vtd_log.{h,c}` | One console line per event through the video work's channel (`n2vt_say`, weak): registration count, probe fallback per codec, session start (codec/geometry/range), first picture, DEC_INPUT failure. No printing when `n2vt_log.c` is not linked. |
| `n2vtd_register.c` | `VTRegisterVideoDecoderWithInfo` with `CMClassImplementationID = N2VT_DEC_ID_BASE`, `kVTVideoDecoder_IsHardwareAccelerated = true`, `Rating = 100`; switch files `nord2-video-off` (veto), `nord2-video-hwdec` (required) and `nord2-video-hwdec-no-hevc`; weak-variable checks by address (reading an absent weak variable would dereference 0). |
| `symbols.txt` | Every iOS import with its image (generated, audited by the suite). |
| `test/` | `mkvectors.py` (ffmpeg vectors), `t_apple_dec.{h,c}` (decoder-side CF/CM/CV/VT host runtime), `t_fakeclient_dec.{h,c}` (scripted decoder client), `t_support.{h,c}`, `t_nal.c`, `t_pixels.c`, `t_client.c`, `t_obj.c`, `run_tests.sh`. |

## Tested (host, today)

```
$ bash gpu/hwcodec/vt/dec/run_tests.sh
...
T_NAL PASS 140     x2 builds   avc-high        (avcC, SPS, Annex B, ffmpeg decodes the result)
T_NAL PASS 140     x2 builds   avc-full        (full-range VUI + conformance window)
T_NAL PASS 143     x2 builds   hevc-main       (hvcC VPS/SPS/PPS, SPS through the RPS/VUI)
T_NAL PASS 143     x2 builds   hevc-full       (HEVC full range + crop)
T_NAL PASS 119     x2 builds   hevc-main10     (10-bit SPS parsed; the object refuses it per frame)
T_PIXELS PASS 259  x2 builds   MM21 detile/crop/padded strides vs an independent forward tiler, 40 random geometries vs opus-mm21/libyuv's converter, NV12, I420, refusals
T_CLIENT PASS 76   x2 builds   rev-2 messages vs an in-memory daemon, every request field, error mapping
T_OBJ PASS 166     x2 builds   registration+switches, class record, StartSession, DecodeFrame, display-order reorder, FLUSH drain/drop, geometry change (changed=1 re-header), linear NV12, properties, invalidate, failures
T_OBJ PASS 166     TSan         the same object test, including two sessions on two threads
DEC06 DEC05 adapter: client and adapter compile against the rev-2 header; every n2vcd_* call resolves
DEC06 ios imports: 65 undefined, all covered (60 TBD, 5 weak/linker)
DEC06 ios compile: 8 objects for arm64-apple-ios16.0 (7 plug-in + n2_mm21)
DEC06 checks: 2553
N2TEST DEC06 PASS 2553/2553
```

Two bugs that the package's own oracles caught and that are fixed here: (1) the full-range path used to ask DEC05 for an NV12 daemon layout (it must always ask MM21; 420f is only the CVPixelBuffer range, and DEC05 rejects NV12 with E_UNSUPPORTED, which would have failed StartSession on every full-range stream); (2) the first revision's MM21 chroma tile was 32 bytes instead of 16. Both have regression checks now: `tfd_open_format()` must be MM21 for a 420f session, and the libyuv differential covers the tile layout.

A deterministic robustness sweep (2000 random/truncated records, SPS buffers and samples, both codecs) runs under ASan+UBSan in every t_nal build: malformed input must return an error or be rejected, never read out of bounds.

Additional static check: `clang --analyze` is clean on all seven product sources (it found two real bugs during development: an uninitialised `used[]` in the HEVC short-term-RPS parser and a `1 << 64` shift on an empty NAL; both fixed).

What the oracle actually is: ffmpeg/libx264/libx265 make the records and samples; `ffprobe` supplies the expected
width/height/full-range/bit-depth; each converted Annex B stream is decoded by ffmpeg and must give the same frame count
and size. The MM21 test tiles a synthetic picture with its own forward loop (the opposite direction from the code under
test) and compares every pixel; the object test compares the emitted `CVPixelBuffer` with the pattern the fake daemon
wrote for that pts. The iOS dylib is linked with `ld64.lld` against the SDK TBDs plus a supplementary TBD for the
decoder exports, then `llvm-nm -u` is audited. The TSan run has no warnings.

## Not done / unresolved

1. **No phone run, no daemon end-to-end.** The object was driven against the scripted client and, for the adapter, only
   compiled against DEC05's real client (their suite proves the daemon side). The first on-phone run must check:
   the plug-in loads (it is a new file at `/System/Library/VideoDecoders/Slim.videodecoder`), `SLIMRegister` runs inside
   mediaserverd, `CreateInstance`'s 250 ms HELLO succeeds, and the first H.264/HEVC frame comes out.
2. **DEC05's daemon/client are newer than my first stand-in and the rev-2 header is not landed.** The product adapter
   needs `include/n2vcd_proto.h` rev 2 (hw-lead, DEC05 posted the paste-ready text). Until then the default is the
   protocol stand-in, which speaks the same wire format. The adapter is compile-checked against the staged rev-2 header.
3. **Crop origin is always 0.** DEC05's `DEC_HEADER_OK` has no crop-origin field and its AP adapter refuses a stream
   whose `vsi.crop.left/top != 0` (E_UNSUPPORTED). Measured streams have 0,0 (DEC03). If the vendor ever reports a
   nonzero origin, the plug-in would need the field (DEC05/opus-mm21 own that).
4. **MM21 tile order is [V-oracle], not yet [V-phone].** The layout used here is opus-mm21's (facts/MM21_LAYOUT.md §2),
   which is byte-identical to libyuv's `MM21ToNV12` on 13 geometries; my converter is differentially identical to
   opus-mm21's on the fixed cases and 40 random geometries. That the phone's VDEC writes this layout is still [I] until
   the first phone frame is compared with ffmpeg. Widths whose stride is not a multiple of 16 bytes, and heights not a
   multiple of 32, are **refused** (`copies_failed`) rather than guessed; the daemon reports `buf_h` aligned to 32 for
   MM21 (DEC05 clamps it to 32).
   **Correction:** an earlier revision of this package assumed a 32-byte chroma tile (16 pairs) and would have produced
   wrong colours on every frame; the libyuv differential caught it. The chroma tile is 16 BYTES x 16 rows.
5. **10-bit / non-4:2:0 / interlaced are refused per frame** (`-12910`), never mis-decoded. `StartSession` cannot be used
   to probe (VideoToolbox does not retry it), so the session is created and every frame fails. The nested software
   `VTDecompressionSession` fallback (DEC08 rule 4) is **not implemented**; with these streams the client gets an error.
6. **In-band parameter-set changes are not tracked by the object.** They are passed to the daemon inside the access unit
   (the vendor parses in-band sets), but the daemon's geometry and our pixel-buffer attributes stay at the header's
   values; `CanAcceptFormatDescription` only compares a *new format description*. A mid-stream SPS change in an `hvc1`
   stream is therefore not a supported resolution change yet.
7. **One output copy per frame is CPU-side** (read the planes out of the daemon buffer, detile into the CVPixelBuffer).
   No NEON, no Metal, no zero-copy. Fine for correctness; a 1080p frame is a few MB of memcpy.
8. **`setProperties` / `copySerializableProperties` return `kVTUnimplementedErr` (-12782).** That is the documented
   "use the base class" reply (DEC08: VT then calls base `SetProperty` per key, which this object accepts and ignores);
   no property is silently misinterpreted. `copySupportedPropertyDictionary` returns an empty dictionary.
9. **Emit happens while the object's session mutex is held.** VideoToolbox's emit takes its own session mutex; it does not
   call back into the decoder for the same session in the code paths read by DEC08, but this is worth watching in the
   first phone run.
10. **`n2vtd_register_set_switch_dir()`** exists so the suite can exercise the switch files in `/tmp`; the product path is
    `/private/var/db`. It is a staging hook, not a behaviour change.
11. The tile-aligned `buf_h`/stride requirement means 4:2:0 streams whose width is not a multiple of 16 (e.g. 176 is
    fine now; a width like 100 is not) rely on the daemon reporting the padded chroma pitch through `out_bytes`; the
    object derives it and refuses if it is not a multiple of 16. 320/1920 (the measured cases) are fine.
12. An access unit whose Annex B form exceeds `OPEN_OK.in_buf_bytes` is rejected with `BadData`. For 4-byte length
    prefixes the conversion is size-preserving, so this only matters for 1-byte-prefixed streams (the start codes add up
    to 3 bytes per NAL); no such stream was seen from ffmpeg's MP4 muxer.

## Integration (for hw-ip / the lead)

1. **Build**: the decoder dylib needs `n2vtd_nal.c n2vtd_pixels.c n2vtd_client_proto.c n2vtd_client_dec05.c n2vtd_obj.c
   n2vtd_register.c n2vtd_log.c` plus opus-mm21's `mm21/n2_mm21.c` (recommended: the libyuv-verified staged converter;
   without it the object falls back to `n2vtd_pixels.c`) and, for console output, `gpu/video/vt/n2vt_log.c` (weak
   import, optional) compiled with `-I gpu/video/include -I gpu/hwcodec/include
   -I gpu/hwcodec/vt/dec -I gpu/hwcodec/mm21` and linked with
   P03's transport (`client/n2vcd_transport.c`) plus, once rev 2 lands, DEC05's client (`client/dec/n2vcd_dec*.c`).
   `SLIMRegister` is already `visibility("default")` by virtue of not being hidden (`-fvisibility=hidden` needs the entry
   exported: add `__attribute__((visibility("default")))` to `SLIMRegister` in that build, or compile
   `n2vtd_register.c` without hidden visibility).
2. **Install**: separate file `/System/Library/VideoDecoders/Slim.videodecoder` (the opus-dec3 two-dylib proposal), plus
   the switch `/private/var/db/nord2-video-hwdec`; `nord2-video-off` vetoes and must be honoured by the installer too.
3. **Fallback**: Apple's H264SW/VCPHEVC register themselves with `IsHardwareAccelerated=false`, rating 0; ours registers
   `true`, rating 100, and `CreateInstance` returns `-12913` when the daemon cannot be reached within 250 ms, which makes
   VideoToolbox try the next decoder. A **StartSession** error is not retried (DEC08 [V]), so the probe lives in
   `CreateInstance` only.

## Proposed additions to frozen headers (owner: hw-lead / gpu-video)

`gpu/video/include/n2_apple.h` (declared in `vt/dec/n2vtd_apple.h` meanwhile):

```c
extern CMTime CMSampleBufferGetPresentationTimeStamp(CMSampleBufferRef sb);
extern const CFStringRef kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms;
extern CFTypeRef CMFormatDescriptionGetExtension(CMFormatDescriptionRef fmt, CFStringRef key);
extern OSStatus CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(CMFormatDescriptionRef fmt, size_t index,
    const uint8_t **ps, size_t *size, size_t *count, int *nalLength);
extern CFTypeID CFDataGetTypeID(void);
extern const uint8_t *CFDataGetBytePtr(CFDataRef data);
extern CFIndex CFDataGetLength(CFDataRef data);
```

All seven are real iOS 16 exports; P04's `vt/symbols.txt` already proposes the HEVC accessor.
`include/n2vcd_proto.h`: rev 2 as posted by DEC05 (`daemon/dec/n2vcd_proto_dec_rev2.h.txt`).
`gpu/video/fake`: it has no decoder half (its `CMDerivedObjectCreate` accepts only the encoder class ID and it has no
CFData), so `vt/dec/test/t_apple_dec.c` supplies the decoder-side runtime. If the owner wants it shared, the file is
self-contained and can move.

## Open questions for DEC05 / the lead

* Does the daemon need the *first slice* in DEC_HEADER, or are the parameter sets enough? The object sends the parameter
  sets only; the first access unit follows as DEC_INPUT (DEC05's own doc says the header START does not consume a picture).
* After `FLUSH` (RESET), does the vendor keep the parsed SPS/PPS? The object does not re-send DEC_HEADER; if the daemon
  needs it, it should prepend its cached parameter sets to the first access unit after a flush.
* The daemon's `dpb_frames` must be >= 2 and <= 16 (DEC05's limit); the object accepts up to 32 but DEC05 caps at 16.
