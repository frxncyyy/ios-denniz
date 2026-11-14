# DEC08 - the VideoToolbox video-decoder plug-in ABI of iOS 16.7.16

Owner: hw-dec08. Read-only: this file and `abi_tools/`; nothing here was run on the phone.

This is the decoder counterpart of `gpu/video/include/n2vt_abi.h` (the encoder ABI that `gpu/video` already
froze). It was read out of the user's own dyld shared cache (iOS 16.7.16, 20H392, iPhone X, arm64) with
`abi_tools/n2decabi.py`; regenerate it with `--write`, verify it with `--check`. Every address below is an unslid
cache address. `[V]` = read out of the cache in this package (record bytes, instruction, or string);
`[I]` = inferred, with the reason given. No Apple code is copied; the excerpts are disassembly facts.

The frozen decoder header built from these facts is `gpu/hwcodec/include/n2vt_dec_abi.h`; its companion facts
file is `gpu/hwcodec/facts/VT_DECODER_ABI.md` (opus-dec3, same cache). This document is the full evidence dump
behind both: five real class records (including H264SW), the complete 23-entry loader list, every slot's caller,
and the session callbacks. Where the two overlap they agree; section 6 records the proposal and the landed names.

The three questions a plug-in author has are answered in this order: **what the loader calls** (section 1),
**what object and class records to build** (sections 2-4), and **how VideoToolbox chooses and retries**
(section 5). Section 6 is the proposed header text; section 7 lists what is *not* verified.

## 0. Short answer

* The daemon/plug-in path is `/System/Library/VideoDecoders/Slim.videodecoder`, entry point `SLIMRegister`.
  VideoToolbox `dlopen`s it with `RTLD_LOCAL` and calls the entry with **no arguments** (section 1).
* A decoder is a `CMBaseObject` whose vtable is three pointers `{ 0, &baseClass, &decoderClass }`. The base
  class record is exactly `n2_CMBaseClass` from `n2vt_abi.h`; the decoder class record is **11 slots, 0x58 bytes**
  (section 2) - not 21 like the encoder class. Class versions seen in this cache: H264SW 3, VCH263 1, VCPMP4V 1,
  AV1SW 3, Paravirtualized 3.
* `decoderClass->version` gates only the optional tail: `canAcceptFormatDescription` and
  `finishDelayedFrames` need version >= 2; the three tile slots need version >= 3. The other slots are called
  whenever non-NULL.
* Register with `VTRegisterVideoDecoderWithInfo(codecType, info, createInstance)`; `createInstance` is
  `OSStatus (*)(uint32_t codecType, CFAllocatorRef, VTVideoDecoderRef *)` and normally does
  `CMDerivedObjectCreate(allocator, &vtable, VTVideoDecoderGetClassID(), out)`.
* If the info dictionary does not mention `kVTVideoDecoder_IsHardwareAccelerated`, VideoToolbox sets it to
  **true**; `kVTVideoDecoderInfo_Rating` defaults to **0** (section 4).
* Selection filters by codec type and the hardware specifications, sorts by rating (descending, hardware first on
  a tie) and calls each factory in turn; a factory error or NULL object tries the next decoder. A **StartSession
  error is fatal** - no other decoder is tried (section 5).

## 1. How VideoToolbox loads the plug-in [V]

`_VTLoadVideoDecoders` @ `0x194cc7ea4` is a fixed list of 23 (path, entry) pairs; for each it calls
`_VTLoadVideoDecoder(path, entry)` @ `0x194cc807c`, which is:

```
194cc807c: stp x20, x19, [sp, #-0x20]!
194cc8080: stp x29, x30, [sp, #0x10]
194cc8084: add x29, sp, #0x10
194cc8088: mov x19, x1
194cc808c: mov w1, #0x4                // =4
194cc8090: bl 0x194fd7d5c <.text+0x30fce0>
194cc8094: cbz x0, 0x194cc80b0 <.text+0x34>
194cc8098: mov x1, x19
194cc809c: bl 0x19b71fddc <.text+0x6a57d60>
194cc80a0: cbz x0, 0x194cc80b0 <.text+0x34>
194cc80a4: ldp x29, x30, [sp, #0x10]
194cc80a8: ldp x20, x19, [sp], #0x20
194cc80ac: br x0
194cc80b0: ldp x29, x30, [sp, #0x10]
194cc80b4: ldp x20, x19, [sp], #0x20
```

The GOT slot read at `0x194cc8090` is `_dlopen` (libdyld.dylib, `0x1a6c81c04`) and the branch at `0x194cc809c`
resolves to `_dlsym` (`0x1a6c81c88`). So the contract is:

```c
void *h = dlopen("/System/Library/VideoDecoders/Slim.videodecoder", RTLD_LOCAL);
void (*fn)(void) = (void (*)(void))dlsym(h, "SLIMRegister");
if (fn) fn();          /* no arguments are set: br x0 with stale x1..x7 */
```

`_VTLoadVideoDecoders` is itself called once from `_vtCreateAndPopulateVideoDecoderRegistry` @ `0x194ed0bbc`
(call at `0x194ed0e58`, inside a `FigThreadRunOnce`), i.e. lazily when the decoder registry is first populated -
not at process start. The full list, in call order, is:

| # | path | entry |
|---|---|---|
| 1 | `/System/Library/VideoCodecs/H264SW.videocodec` | `VCPAVCRegisterDecoder` |
| 2 | `/System/Library/VideoDecoders/VCH263.videodecoder` | `VCH263Register` |
| 3 | `/System/Library/VideoDecoders/H264H2.videodecoder` | `H264H2Register` |
| 4 | `/System/Library/VideoDecoders/MP4VH2.videodecoder` | `MP4VH2Register` |
| 5 | `/System/Library/VideoDecoders/Slim.videodecoder` | `SLIMRegister` |
| 6 | `/System/Library/VideoDecoders/JPEGH1.videodecoder` | `JPEGH1Register` |
| 7 | `/System/Library/VideoDecoders/H264H4.videodecoder` | `H264H4Register` |
| 8 | `/System/Library/VideoDecoders/MP4VH4.videodecoder` | `MP4VH4Register` |
| 9 | `/System/Library/VideoDecoders/H264H6.videodecoder` | `H264H6Register` |
| 10 | `/System/Library/VideoDecoders/MP4VH6.videodecoder` | `MP4VH6Register` |
| 11 | `/System/Library/VideoDecoders/H264H8.videodecoder` | `H264H8Register` |
| 12 | `/System/Library/VideoDecoders/MP4VH8.videodecoder` | `MP4VH8Register` |
| 13 | `/System/Library/VideoDecoders/HEVCH8.videodecoder` | `HEVCH8Register` |
| 14 | `/System/Library/VideoDecoders/H8.videodecoder` | `H8Register` |
| 15 | `/System/Library/VideoCodecs/VCPHEVC.videocodec` | `VCPHEVCRegisterDecoder` |
| 16 | `/System/Library/VideoDecoders/AVD.videodecoder` | `AVDRegister` |
| 17 | `/System/Library/VideoDecoders/ExampleIPB.videodecoder` | `ExampleIPBDecoderRegister` |
| 18 | `/System/Library/VideoDecoders/BritannicusDecoder.videodecoder` | `BritannicusRegister` |
| 19 | `/System/Library/VideoDecoders/VCPMP4V.videodecoder` | `VCPMP4VRegister` |
| 20 | `/System/Library/VideoDecoders/AppleProResSWDecoder.videodecoder` | `ProResSWDecoderRegister` |
| 21 | `/System/Library/VideoDecoders/AppleProResHWDecoder.videodecoder` | `ProResDecoderRegister` |
| 22 | `/System/Library/VideoDecoders/AV1SW.videodecoder` | `AV1RegisterDecoder` |
| 23 | `/AppleInternal/Library/VideoCodecs/yzip.videocodec` | `yzipRegisterDecoder` |

The `Slim.videodecoder` entry is #5: `0x194cc7f00` loads the path string at `0x194fe0960`, `0x194cc7f04` the
entry string at `0x194fe0990`, and `0x194cc7f0c` calls `_VTLoadVideoDecoder`. A missing file, or a missing entry
symbol, is silently ignored by the loader; only a successful `dlsym` is called.

## 2. The object: one vtable, one base class record, one decoder class record [V]

The vtable has three pointer-sized slots:

```c
struct n2_VTVideoDecoderVTable {
    const void             *reserved;      /* NULL */
    const n2_CMBaseClass   *baseClass;     /* [vtable+0x08] */
    const n2_VTVideoDecoderClass *decoderClass; /* [vtable+0x10] */
};
```

`CMBaseObjectGetVTable(object)` returns that pointer. `VTVideoDecoderGetCMBaseObject(object)` is literally `ret`
(`0x194cfc1e4`): the decoder reference and the CMBaseObject are the same address. All five records below were
found by scanning the plug-in data segments for the class methods and following the two record pointers; the
records are complete dumps, not samples.

### H264SW

Image `H264SW.videocodec`. Vtable @ `0x214af13d0`.

| record | address | version | derivedStorageSize / non-NULL slots |
|---|---|---|---|
| base class | `0x214af13e8` | 1 | 0x480 |
| decoder class | `0x214af1440` | 3 | startSession, decodeFrame, copySupportedPropertyDictionary, canAcceptFormatDescription, startTileSession, decodeTile |

| class slot | name | function |
|---|---|---|
| `+0x00` | `version` | 0x3 |
| `+0x08` | `startSession` | 0x1f0f3abcc |
| `+0x10` | `decodeFrame` | 0x1f0f3ac5c |
| `+0x18` | `copySupportedPropertyDictionary` | 0x1f0f3af74 |
| `+0x20` | `setProperties` | `NULL` |
| `+0x28` | `copySerializableProperties` | `NULL` |
| `+0x30` | `canAcceptFormatDescription` | 0x1f0f3b024 |
| `+0x38` | `finishDelayedFrames` | `NULL` |
| `+0x40` | `startTileSession` | 0x1f0f3b1fc |
| `+0x48` | `decodeTile` | 0x1f0f3b284 |
| `+0x50` | `finishDelayedTiles` | `NULL` |

| base slot | name | value |
|---|---|---|
| `+0x00` | `version` | 0x1 |
| `+0x08` | `derivedStorageSize` | 0x480 |
| `+0x10` | `equal` | `NULL` |
| `+0x18` | `invalidate` | `NULL` |
| `+0x20` | `finalize` | 0x1f0f3a0ac |
| `+0x28` | `copyDebugDescription` | 0x1f0f3a0fc |
| `+0x30` | `copyProperty` | 0x1f0f3a150 |
| `+0x38` | `setProperty` | 0x1f0f3a434 |
| `+0x40` | `reserved8` | `NULL` |
| `+0x48` | `reserved9` | `NULL` |
| `+0x50` | `reserved10` | `NULL` |

### VCH263

Image `VCH263.videodecoder`. Vtable @ `0x214e8dd20` (`_kVCH263VideoDecoderVTable`).

| record | address | version | derivedStorageSize / non-NULL slots |
|---|---|---|---|
| base class | `0x214e8dd38` (`_kVCH263VideoDecoder_BaseClass`) | 1 | 0x38 |
| decoder class | `0x214e8dd90` (`_kVCH263VideoDecoder_VideoDecoderClass`) | 1 | startSession, decodeFrame |

| class slot | name | function |
|---|---|---|
| `+0x00` | `version` | 0x1 |
| `+0x08` | `startSession` | 0x201caec00 |
| `+0x10` | `decodeFrame` | 0x201caee80 |
| `+0x18` | `copySupportedPropertyDictionary` | `NULL` |
| `+0x20` | `setProperties` | `NULL` |
| `+0x28` | `copySerializableProperties` | `NULL` |
| `+0x30` | `canAcceptFormatDescription` | `NULL` |
| `+0x38` | `finishDelayedFrames` | `NULL` |
| `+0x40` | `startTileSession` | `NULL` |
| `+0x48` | `decodeTile` | `NULL` |
| `+0x50` | `finishDelayedTiles` | `NULL` |

| base slot | name | value |
|---|---|---|
| `+0x00` | `version` | 0x1 |
| `+0x08` | `derivedStorageSize` | 0x38 |
| `+0x10` | `equal` | `NULL` |
| `+0x18` | `invalidate` | `NULL` |
| `+0x20` | `finalize` | 0x201caeb8c |
| `+0x28` | `copyDebugDescription` | `NULL` |
| `+0x30` | `copyProperty` | `NULL` |
| `+0x38` | `setProperty` | `NULL` |
| `+0x40` | `reserved8` | `NULL` |
| `+0x48` | `reserved9` | `NULL` |
| `+0x50` | `reserved10` | `NULL` |

### VCPMP4V

Image `VCPMP4V.videodecoder`. Vtable @ `0x214af1300` (`__ZL24kMPEG4VideoDecoderVTable`).

| record | address | version | derivedStorageSize / non-NULL slots |
|---|---|---|---|
| base class | `0x214af1318` (`__ZL28kMPEG4VideoDecoder_BaseClass`) | 1 | 0x28 |
| decoder class | `0x214af1370` (`__ZL36kMPEG4VideoDecoder_VideoDecoderClass`) | 1 | startSession, decodeFrame |

| class slot | name | function |
|---|---|---|
| `+0x00` | `version` | 0x1 |
| `+0x08` | `startSession` | 0x1f53f4a40 |
| `+0x10` | `decodeFrame` | 0x1f53f5230 |
| `+0x18` | `copySupportedPropertyDictionary` | `NULL` |
| `+0x20` | `setProperties` | `NULL` |
| `+0x28` | `copySerializableProperties` | `NULL` |
| `+0x30` | `canAcceptFormatDescription` | `NULL` |
| `+0x38` | `finishDelayedFrames` | `NULL` |
| `+0x40` | `startTileSession` | `NULL` |
| `+0x48` | `decodeTile` | `NULL` |
| `+0x50` | `finishDelayedTiles` | `NULL` |

| base slot | name | value |
|---|---|---|
| `+0x00` | `version` | 0x1 |
| `+0x08` | `derivedStorageSize` | 0x28 |
| `+0x10` | `equal` | `NULL` |
| `+0x18` | `invalidate` | `NULL` |
| `+0x20` | `finalize` | 0x1f53f49a4 |
| `+0x28` | `copyDebugDescription` | 0x1f53f49ec |
| `+0x30` | `copyProperty` | `NULL` |
| `+0x38` | `setProperty` | `NULL` |
| `+0x40` | `reserved8` | `NULL` |
| `+0x48` | `reserved9` | `NULL` |
| `+0x50` | `reserved10` | `NULL` |

### AV1SW

Image `AV1SW.videodecoder`. Vtable @ `0x214aad780`.

| record | address | version | derivedStorageSize / non-NULL slots |
|---|---|---|---|
| base class | `0x214aad798` | 1 | 0x8 |
| decoder class | `0x214aad7f0` | 3 | startSession, decodeFrame, copySupportedPropertyDictionary, canAcceptFormatDescription, finishDelayedFrames, startTileSession, decodeTile, finishDelayedTiles |

| class slot | name | function |
|---|---|---|
| `+0x00` | `version` | 0x3 |
| `+0x08` | `startSession` | 0x1f4141640 |
| `+0x10` | `decodeFrame` | 0x1f41416b8 |
| `+0x18` | `copySupportedPropertyDictionary` | 0x1f41419d4 |
| `+0x20` | `setProperties` | `NULL` |
| `+0x28` | `copySerializableProperties` | `NULL` |
| `+0x30` | `canAcceptFormatDescription` | 0x1f4141adc |
| `+0x38` | `finishDelayedFrames` | 0x1f4141bc0 |
| `+0x40` | `startTileSession` | 0x1f4141bf0 |
| `+0x48` | `decodeTile` | 0x1f4141c64 |
| `+0x50` | `finishDelayedTiles` | 0x1f4141ff8 |

| base slot | name | value |
|---|---|---|
| `+0x00` | `version` | 0x1 |
| `+0x08` | `derivedStorageSize` | 0x8 |
| `+0x10` | `equal` | `NULL` |
| `+0x18` | `invalidate` | 0x1f4142028 |
| `+0x20` | `finalize` | 0x1f4142170 |
| `+0x28` | `copyDebugDescription` | 0x1f4142174 |
| `+0x30` | `copyProperty` | 0x1f41421c8 |
| `+0x38` | `setProperty` | 0x1f4142338 |
| `+0x40` | `reserved8` | `NULL` |
| `+0x48` | `reserved9` | `NULL` |
| `+0x50` | `reserved10` | `NULL` |

### ParavirtualizedVideoDecoder

Image `VideoToolbox.framework/VideoToolbox`. Vtable @ `0x1d69e8d28` (`_kParavirtualizedVideoDecoder_VTable`).

| record | address | version | derivedStorageSize / non-NULL slots |
|---|---|---|---|
| base class | `0x1d69e8d60` (`_kParavirtualizedVideoDecoder_BaseClass`) | 1 | 0x60 |
| decoder class | `0x1d69e8db8` (`_kParavirtualizedVideoDecoder_VideoDecoderClass`) | 3 | startSession, decodeFrame, copySupportedPropertyDictionary, setProperties, copySerializableProperties, canAcceptFormatDescription, finishDelayedFrames, startTileSession, decodeTile, finishDelayedTiles |

| class slot | name | function |
|---|---|---|
| `+0x00` | `version` | 0x3 |
| `+0x08` | `startSession` | 0x194ef67f0 |
| `+0x10` | `decodeFrame` | 0x194ef68fc |
| `+0x18` | `copySupportedPropertyDictionary` | 0x194ef6b44 |
| `+0x20` | `setProperties` | 0x194ef6ca0 |
| `+0x28` | `copySerializableProperties` | 0x194ef6ca8 |
| `+0x30` | `canAcceptFormatDescription` | 0x194ef6cb0 |
| `+0x38` | `finishDelayedFrames` | 0x194ef6cb8 |
| `+0x40` | `startTileSession` | 0x194ef6d70 |
| `+0x48` | `decodeTile` | 0x194ef6e74 |
| `+0x50` | `finishDelayedTiles` | 0x194ef7390 |

| base slot | name | value |
|---|---|---|
| `+0x00` | `version` | 0x1 |
| `+0x08` | `derivedStorageSize` | 0x60 |
| `+0x10` | `equal` | `NULL` |
| `+0x18` | `invalidate` | 0x194ef6374 |
| `+0x20` | `finalize` | 0x194ef6418 |
| `+0x28` | `copyDebugDescription` | 0x194ef64ec |
| `+0x30` | `copyProperty` | 0x194ef65ac |
| `+0x38` | `setProperty` | 0x194ef66bc |
| `+0x40` | `reserved8` | `NULL` |
| `+0x48` | `reserved9` | `NULL` |
| `+0x50` | `reserved10` | `NULL` |

### The class record, slot by slot

The slot order is not guessed: it is the order of the named methods in Apple's own
`_ParavirtualizedVideoDecoder_VideoDecoderClass` (version 3) and every call site below indexes the same way.
The signatures come from the callee prologues and from the call sites in `_VTDecompressionSession*`;
argument names are ours.

| off | name | signature | version | where called from (address of the `blr`) |
|---|---|---|---|---|
| `0x00` | `version` | `uint64_t` | 1 | read by the wrappers before every gated slot |
| `0x08` | `startSession` | `OSStatus (*)(VTVideoDecoderRef, VTVideoDecoderSessionRef, CMFormatDescriptionRef)` | 1 | `0x194cf8ebc` in `VTDecompressionSessionCreateWithOptions`; x1 = `&session` whose first word is the sub-duct, x2 = format description |
| `0x10` | `decodeFrame` | `OSStatus (*)(VTVideoDecoderRef, VTVideoDecoderFrameRef, CMSampleBufferRef, uint32_t flags, uint32_t *infoFlagsOut)` | 1 | `0x194ccbba4` in `_vtDecompressionDuctDecodeSingleFrame`; x1=frame token, x2=sample buffer, w3=flags, x4=infoFlagsOut |
| `0x18` | `copySupportedPropertyDictionary` | `OSStatus (*)(VTVideoDecoderRef, CFDictionaryRef *out)` | any | `0x194ccb334`; x1 = address of the out dictionary |
| `0x20` | `setProperties` | `OSStatus (*)(VTVideoDecoderRef, CFDictionaryRef)` | any | `0x194cfab7c` in `VTDecompressionSessionSetProperties` |
| `0x28` | `copySerializableProperties` | `OSStatus (*)(VTVideoDecoderRef, CFAllocatorRef, CFDictionaryRef *out)` | any | `0x194cfacd8`; x1 is the allocator passed to `VTDecompressionSessionCopySerializableProperties`, x2 = out |
| `0x30` | `canAcceptFormatDescription` | `Boolean (*)(VTVideoDecoderRef, CMFormatDescriptionRef)` | >= 2 | `_VTVideoDecoderCanAcceptFormatDescription` @ `0x194cfbf84`: checks version >= 2 then `br` to +0x30; the decode path also calls it at `0x194cfbccc` |
| `0x38` | `finishDelayedFrames` | `OSStatus (*)(VTVideoDecoderRef)` | >= 2 | `0x194cfc1a0` in `VTDecompressionSessionFinishDelayedFrames` |
| `0x40` | `startTileSession` | `OSStatus (*)(VTVideoDecoderRef, VTVideoDecoderSessionRef, CMFormatDescriptionRef)` [I: same shape as startSession] | >= 3 | `0x194f92374` in `VTTileDecompressionSessionCreate` |
| `0x48` | `decodeTile` | `OSStatus (*)(VTVideoDecoderRef, uint64_t frameID, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint32_t f, uint32_t *infoFlagsOut)` [I: argument names] | >= 3 | `0x194f925f8` in `VTTileDecompressionSessionDecodeTile`; x1..x6 64-bit, w7 32-bit, [sp] = infoFlagsOut |
| `0x50` | `finishDelayedTiles` | `OSStatus (*)(VTVideoDecoderRef)` | >= 3 | `0x194f91784` in `VTTileDecompressionSessionFinishDelayedTiles` (tail `br x1`) |

Rules that fall out of the wrappers [V]:

* `canAcceptFormatDescription` is asked only when the format description of an incoming sample buffer differs
  from the current one. A version-1 decoder, or a NULL slot, cannot accept a format change: the decode path
  skips the call and returns `-12916`. The slot returning false gives the same `-12916`; true makes VideoToolbox
  wait for asynchronous frames, install the new format description and retry the frame.
* `finishDelayedFrames` is only called when `version >= 2` and non-NULL; NULL is fine.
* The tile slots are only called when `version >= 3`; a version-1/2 decoder must leave them NULL.
* `copySupportedPropertyDictionary`, `setProperties` and `copySerializableProperties` are called whenever
  non-NULL, regardless of version.

## 3. The session callbacks VideoToolbox provides [V]

A decoder gets an opaque `VTVideoDecoderSessionRef` in `startSession` and uses these exported functions:

| function | address | observed behaviour / signature |
|---|---|---|
| `VTDecoderSessionEmitDecodedFrame` | `0x194ccbe7c` | `OSStatus (session, frame, OSStatus status, uint32_t infoFlags, CVImageBufferRef imageBuffer)`: x4 is an image buffer (passed to the attachment transfer at `0x194ccbf90`, not to a CMSampleBuffer API); it is dropped when status != 0 (`cmp w2,#0; csel x22,x4,xzr,eq` at `0x194ccbeb8`) and when bit 1 of the frame's recorded flag word is set (`ldr w24,[x0]` at `0x194ccbedc`, `tst w24,#2` at `0x194ccbf7c`). An unknown or already-emitted frame token returns `-12902` |
| `VTDecoderSessionEmitDecodedTile` | `0x194f92668` | tile variant; tail branch to `VTTileDecoderSessionEmitDecodedTile`; x1..x4 as above plus a tile argument [I] |
| `VTDecoderSessionGetPixelBufferPool` | `0x194cc9c44` | `CVPixelBufferPoolRef (session)`; `[session]` is the sub-duct, returns `[subduct+0x48]` after making the pools current; NULL on a bad session |
| `VTDecoderSessionSetPixelBufferAttributes` | `0x194cc9bac` | `OSStatus (session, CFDictionaryRef attrs)`; retains the dictionary into `[subduct+0x28]` and bumps `[subduct+0x3c]` |
| `VTDecoderSessionGetDestinationPixelBufferAttributes` | `0x194cfc240` | `CFDictionaryRef (session)`; returns the borrowed dict at `[subduct+0x30]` (do not release) |
| `VTDecoderSessionCreatePixelBuffer` | `0x194cfbb88` | creates one buffer from the pool: `CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, VTDecoderSessionGetPixelBufferPool(session), out)`; x1 is ignored [I: pass NULL] |
| `VTDecoderSessionTrace` | `0x194cfc1f0` | no-op in this build (checks the session and a tracing byte, returns 0); declare as `int (session, const char *fmt, ...)` [I: variadic] |
| `VTDecoderSessionCleanUpAfterDecode` | `0x194cfc1e8` | returns 0; no other effect visible [I] |

`VTVideoDecoderGetClassID()` @ `0x194cc9ad4` runs `FigThreadRunOnce(&once, _RegisterVTVideoDecoderType)`
and returns `sVTVideoDecoderClassID`; `_RegisterVTVideoDecoderType` calls
`FigBaseClassRegisterClass(&sVTVideoDecoderClassDesc, CMBaseGetClassID(), 1, &sVTVideoDecoderClassID)`. The
class descriptor at `0x1d69ab410` is `{version 1, "VTVideoDecoder", 0, VTVideoDecoderCopyFormattingDesc, ...}` and
`VTVideoDecoderCopyFormattingDesc` @ `0x194cfccd0` is just `CFRetain(CFSTR("[VTVideoDecoder]"))`.

## 4. Registering a decoder [V]

```c
OSStatus VTRegisterVideoDecoderWithInfo(uint32_t codecType, CFDictionaryRef info,
                                        VTVideoDecoderCreateInstance createInstance);
OSStatus VTRegisterVideoDecoder(uint32_t codecType, VTVideoDecoderCreateInstance createInstance);
typedef OSStatus (*VTVideoDecoderCreateInstance)(uint32_t codecType, CFAllocatorRef, VTVideoDecoderRef *);
```

`VTRegisterVideoDecoder` @ `0x194cc80bc` is exactly `WithInfo(codecType, NULL, createInstance)`
(`mov x2,x1; mov x1,#0; b WithInfo`). `WithInfo` @ `0x194cc80c8` builds a mutable copy of the info dictionary and:

* sets `CMClassImplementationID` to the caller's value with `".<4cc>"` appended: the mutable copy at
  `0x194cc8178` is formatted with the string `".%@"` at `0x194cc8188` and the codec-type CFString as the
  argument (`0x194cc8210`). When the key is absent it generates
  `"com.apple.videotoolbox.videodecoder.anon-%d"` instead (format string at `0x194cc8208`);
* sets the implementation name to `"Dynamically Registered %@ Video Decoder"`;
* sets `kVTVideoDecoder_CodecTypeKey` (`VTCodecType`, cell at `0x1d69aac18`) to a 4-character CFString of the
  byte-swapped `codecType`: `rev w8,w19` at `0x194cc8104`,
  `CFStringCreateWithBytes(..., 4, 0x600 /* ASCII */, false)` at `0x194cc812c`, stored at `0x194cc825c`;
* sets **`kVTVideoDecoder_IsHardwareAccelerated` to `kCFBooleanTrue` when the info dictionary does not contain it**
  (the branch at `0x194cc822c` skips the `CFDictionarySetValue` at `0x194cc8244` only if the key is present);
* sets `kVTVideoDecoderInfo_Rating` to the caller's CFNumber, or to **0** (the `str wzr,[sp,#8]` at
  `0x194cc80ec` feeds `CFNumberCreate(kCFNumberSInt32Type, 0)` at `0x194cc8288`);
* sets `kVTVideoDecoder_AllowSandboxedDecode` (`VTAllowSandboxedDecode`, cell at `0x1d69aac50`) to false;
* calls `_vtRegisterVideoDecoderInternal(description, implementationID, matchingInfo, createInstance, 0)`,
  which fills `CMClassID = kCMObjectLibraryClassID_VideoDecoder`, `CMClassImplementationVersion = 1`,
  `CMMatchingInfo = matchingInfo`, and adds the item to the registry with `FigRegistryAddItem`.

The factory (`createInstance`) is called as `(codecType, allocator, &decoderOut)`; x0 is the codec type and is
ignored by every Apple decoder examined (VCH263, MPEG4V, AV1SW, H264SW), x1 is the allocator passed to
`CMDerivedObjectCreate`, x2 is the out pointer. The canonical body is:

```c
static OSStatus MyDecoder_CreateInstance(uint32_t type, CFAllocatorRef alloc, VTVideoDecoderRef *out)
{
    return CMDerivedObjectCreate(alloc, &kMyDecoderVTable, VTVideoDecoderGetClassID(), (CMBaseObjectRef *)out);
}
```

`CMDerivedObjectCreate` @ `0x1915c3cac` (CoreMedia) is reached through the same `VTVideoDecoderGetClassID` +
4-argument call in all four software decoders, each through a per-image veneer:

| plug-in | CreateInstance | `VTVideoDecoderGetClassID` call | vtable loaded | `CMDerivedObjectCreate` call |
|---|---|---|---|---|
| H264SW | `0x1f0f39ee4` | `0x1f0f39f00` | `0x214af13d0` | `0x1f0f39f18` |
| VCH263 | `0x201caead4` | `0x201caeaf0` | `0x214e8dd20` | `0x201caeb08` |
| VCPMP4V | `0x1f53f48e8` | `0x1f53f4904` | `0x214af1300` | `0x1f53f491c` |
| AV1SW | `0x1f4141208` | `0x1f4141234` | `0x214aad780` | `0x1f414124c` |

The checker follows one veneer per call (`adrp x16, page; add x16, x16, #imm; br x16`) and asserts that the
class-ID calls land on `0x194cc9ad4` and the object-create calls on `0x1915c3cac`.

What Apple's own plug-ins register (useful as calibration):

| plug-in | codec type | rating | IsHardwareAccelerated | evidence |
|---|---|---|---|---|
| H264SW | `avc1` | not set (0) | explicitly **false** | `0x1f0f3a01c` loads `kCFBooleanFalse`; `mov w0,#0x6331` / `movk w0,#0x6176` at `0x1f0f3a064`; registers at `0x1f0f3a070` |
| VCH263 | `h263` | -10 | not set -> defaults **true** | `mov w8,#-0xa` at `0x201cae8d8`; no IsHW key |
| VCPMP4V | `mp4v` | not set (0) | explicitly **false** | `0x1f54000d4` loads `kCFBooleanFalse`; key at `0x1f54000c4` |
| AV1SW | `av01` | not set (0) | explicitly **false** | `0x1f414264c` loads `kCFBooleanFalse`; key at `0x1f4142634` |
| H264H8 / AVD | `avc1` etc. | not set (0) | not set -> defaults **true** | they call `VTRegisterVideoDecoder(codecType, create)` with no info (e.g. `0x1f17b9020`..`0x1f17b902c`) |

So a hardware decoder that wants to be treated as hardware needs to do nothing; an object that should be
software must say so explicitly. For the Slim hardware decoder the recommendation is to register with
`IsHardwareAccelerated=true` and `Rating=0` (Apple hardware decoders use the defaults; 0 beats VCH263's -10,
and the hardware tie-break handles equal ratings), and to gate the registration on the daemon
switch so an unreachable daemon never takes part in selection.

## 5. How VideoToolbox chooses, and when it retries [V]

The pipeline is `VTDecompressionSessionCreateWithOptions` @ `0x194cf868c` -> `_VTSelectAndCreateVideoDecoderInstanceInternal`
@ `0x194cc7408` (called at `0x194cf8cec`). The internal function:

1. Normalises the caller's decoder specification for the hardware keys: if
   `kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder` is absent it makes a mutable copy and
   sets it true; if it is present and false, it removes it; if
   `RequireHardwareAcceleratedVideoDecoder` is true but Enable is absent, it adds Enable. This is the only
   place `EnableHardwareAcceleratedVideoDecoder` is consumed; it is not a filter by itself.
2. Calls `FigRegistryCopyFilteredItemList` with
   `_vtFilterRegistryItemByCodecTypeAndVideoDecoderSpecification` @ `0x194ed12e0` (call at `0x194cc775c`).
   The filter keeps an item only when:
   * the item's codec-type key equals the requested type (or the item's array contains it);
   * `AllowParavirtualizedDecoder` is not false while the item is paravirtualized;
   * the requested codec type is in `_VTGetAllowedDecoderList()` when that testing list is installed;
   * if the item is hardware: `VTAvoidHardwareDecoders()` is false (or the item is a wrapper decoder), and for
     JPEG `VTAvoidHardwareJPEGDecoders()` is false;
   * if the caller requires hardware (`RequireHardwareAcceleratedVideoDecoder`), the item's
     `kVTVideoDecoder_IsHardwareAccelerated` must be true (`0x194ed1608`..`0x194ed1618`);
   * every key in the item's `kVTVideoDecoderInfo_RequiredSpecificationEntries` is present in the caller's
     specification with an equal value (`_vtCheckRequiredSpecificationKey` @ `0x194ed2844`).
3. Sorts the survivors with `CFArraySortValues` (call at `0x194cc7970`) and
   `_vtSortRegistryItemsByRating` @ `0x194ed1c58`: **higher `kVTVideoDecoderInfo_Rating` first**; when ratings are equal,
   the hardware item sorts before the non-hardware item (`cneg x19,x10,le` at `0x194ed1cf8` after reading both
   `IsHardwareAccelerated` flags).
4. Walks the sorted list from the requested index and calls each factory at `0x194cc7bf4`. If the factory returns
   non-zero (`cbnz w0` at `0x194cc7c44`), or returns 0 but leaves the out pointer NULL (`-12911` path), it
   cleans up that candidate and increments the index (`add x21,x21,#1` at `0x194cc7cc4`, `b.ne` at
   `0x194cc7ccc`): **the next decoder for the same codec type is tried**.
5. If the list is empty it returns `-12906`; if every candidate failed it returns the last factory error.

**StartSession is not retried.** Back in `VTDecompressionSessionCreateWithOptions`, after the object exists,
the class slot +0x08 is called once at `0x194cf8ebc`; a non-zero return jumps straight to the error path at
`0x194cf8ecc`, which finalises the duct and returns the error (`cbnz w0` at `0x194cf8ec0`, no loop). The same is
true of the tile session. Consequences for the Slim decoder:

* A failure in `createInstance` can fall through to the software decoder, but a failure in `startSession`
  cannot. Do not use `startSession` to probe the daemon or the hardware; do that before registering, or in
  `decodeFrame` (where an error is per-frame, not per-session).
* `canAcceptFormatDescription` is checked in `_vtDecompressionSessionDecodeFrameCommon` at `0x194cfbccc`; a false
  answer produces `-12916` for that frame after waiting for asynchronous frames - it does not switch decoders.
* `kVTVideoDecoderInfo_RequiredSpecificationEntries` is the supported way to make selection conditional (for
  example require `EnableHardwareAcceleratedVideoDecoder = true`); it is checked in the filter, before the factory.

`gpu/hwcodec/facts/VT_DECODER_ABI.md` section 4 reaches the same filter/sort/retry order and the same
"StartSession is not retried" conclusion. It leaves the exact effect of `EnableHardwareAccelerated` on software
candidates as [I]; the normalisation loop above was read instruction by instruction here. Both files agree that
the daemon/hardware decision must be made in `createInstance`, never in `startSession`.

## 6. The decoder header: proposal and landed text

Status: the decoder half has landed as `gpu/hwcodec/include/n2vt_dec_abi.h` (opus-dec3 / hw-lead), and
`gpu/hwcodec/facts/VT_DECODER_ABI.md` documents its evidence. DEC06 codes against that file. The text below is
the proposal this package posted; it is kept here because the checker compiles it, and because it is the exact
decoder half of `gpu/video/include/n2vt_abi.h` (still frozen and read-only here). The two agree on the record
(11 slots, 0x58 bytes) and every offset; the landed file differs only in names and extras: it calls the session
type `VTVideoDecoderSession`, uses `CMVideoFormatDescriptionRef`, and adds switch macros, `N2VT_DEC_RATING` and
the public decode-flag/error enumerations. `EmitDecodedFrame`'s x4 is a `CVImageBufferRef` in both.

```c
/* SPDX-License-Identifier: MIT */
/*
 * Proposal: the decoder half of gpu/video/include/n2vt_abi.h (frozen; gpu/video owns it).
 * Decoder side of the VideoToolbox plug-in ABI of iOS 16.7.16, read out of the cache by DEC08.
 * Landed as gpu/hwcodec/include/n2vt_dec_abi.h (hw-lead / opus-dec3); this is the text DEC08 posted.
 * Regenerate the evidence with vt/dec/abi_tools/n2decabi.py.
 */
#ifndef N2VT_DEC_ABI_H
#define N2VT_DEC_ABI_H

#include "n2vt_abi.h"       /* n2_CMBaseClass, CMTypeID, CMDerivedObjectCreate, N2STR, N2VT_DECODER_PLUGIN_* */

typedef struct OpaqueVTVideoDecoder         *VTVideoDecoderRef;
typedef struct OpaqueVTVideoDecoderSession  *VTVideoDecoderSessionRef;
typedef struct OpaqueVTVideoDecoderFrame    *VTVideoDecoderFrameRef;
typedef struct CVPixelBufferPool            *CVPixelBufferPoolRef;      /* add to n2_apple.h if used elsewhere */

/* 11 slots, 0x58 bytes.  version 1: slots 1-2.  version 2 adds the canAccept/finishDelayed pair (slots 6-7).
 * version 3 adds tiles (slots 8-10).  Unused slots must be NULL. */
typedef struct n2_VTVideoDecoderClass {
	uint64_t version;                       /* 1, 2 or 3 */
	OSStatus (*startSession)(VTVideoDecoderRef dec, VTVideoDecoderSessionRef session,
	                         CMFormatDescriptionRef formatDescription);
	OSStatus (*decodeFrame)(VTVideoDecoderRef dec, VTVideoDecoderFrameRef frame, CMSampleBufferRef sampleBuffer,
	                        uint32_t flags, uint32_t *infoFlagsOut);
	OSStatus (*copySupportedPropertyDictionary)(VTVideoDecoderRef dec, CFDictionaryRef *dictionaryOut);
	OSStatus (*setProperties)(VTVideoDecoderRef dec, CFDictionaryRef properties);
	OSStatus (*copySerializableProperties)(VTVideoDecoderRef dec, CFAllocatorRef allocator,
	                                       CFDictionaryRef *dictionaryOut);
	Boolean  (*canAcceptFormatDescription)(VTVideoDecoderRef dec, CMFormatDescriptionRef formatDescription); /* v>=2 */
	OSStatus (*finishDelayedFrames)(VTVideoDecoderRef dec);                                                  /* v>=2 */
	/* version 3 (tiled decode); our object leaves these NULL.  Argument meanings are [I], the register layout is [V]. */
	OSStatus (*startTileSession)(VTVideoDecoderRef dec, VTVideoDecoderSessionRef session,
	                             CMFormatDescriptionRef formatDescription);
	OSStatus (*decodeTile)(VTVideoDecoderRef dec, uint64_t frameID, uint64_t a, uint64_t b, uint64_t c,
	                       uint64_t d, uint64_t e, uint32_t f, uint32_t *infoFlagsOut);
	OSStatus (*finishDelayedTiles)(VTVideoDecoderRef dec);
} n2_VTVideoDecoderClass;

typedef struct n2_VTVideoDecoderVTable {
	const void                   *reserved;     /* NULL */
	const n2_CMBaseClass         *baseClass;    /* [vtable+0x08] */
	const n2_VTVideoDecoderClass *decoderClass; /* [vtable+0x10] */
} n2_VTVideoDecoderVTable;

_Static_assert(sizeof(n2_VTVideoDecoderClass) == 0x58, "VTVideoDecoderClass is 11 slots");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, startSession) == 0x08, "startSession slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, decodeFrame) == 0x10, "decodeFrame slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, copySupportedPropertyDictionary) == 0x18, "properties slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, setProperties) == 0x20, "setProperties slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, copySerializableProperties) == 0x28, "serializable slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, canAcceptFormatDescription) == 0x30, "canAccept slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, finishDelayedFrames) == 0x38, "finishDelayed slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, startTileSession) == 0x40, "tile slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, decodeTile) == 0x48, "decodeTile slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, finishDelayedTiles) == 0x50, "finishTiles slot");
_Static_assert(sizeof(n2_VTVideoDecoderVTable) == 0x18, "vtable is 3 slots");

typedef OSStatus (*n2_VTVideoDecoderCreateInstance)(uint32_t codecType, CFAllocatorRef allocator,
                                                    VTVideoDecoderRef *decoderOut);

extern CMTypeID  VTVideoDecoderGetClassID(void);
extern CMBaseObjectRef VTVideoDecoderGetCMBaseObject(VTVideoDecoderRef dec);   /* returns dec */
extern OSStatus  VTRegisterVideoDecoder(uint32_t codecType, n2_VTVideoDecoderCreateInstance create);
extern OSStatus  VTRegisterVideoDecoderWithInfo(uint32_t codecType, CFDictionaryRef info,
                                                n2_VTVideoDecoderCreateInstance create);

/* Session callbacks implemented by VideoToolbox (all verified in the export trie). */
extern OSStatus  VTDecoderSessionEmitDecodedFrame(VTVideoDecoderSessionRef session, VTVideoDecoderFrameRef frame,
                                                  OSStatus status, uint32_t infoFlags, CVImageBufferRef imageBuffer);
/* Tile decode and Trace are documented in ABI.md section 3; they are not declared here until their argument
 * names are proven (their registers are). */
extern CVPixelBufferPoolRef VTDecoderSessionGetPixelBufferPool(VTVideoDecoderSessionRef session);
extern OSStatus  VTDecoderSessionSetPixelBufferAttributes(VTVideoDecoderSessionRef session, CFDictionaryRef attributes);
extern CFDictionaryRef VTDecoderSessionGetDestinationPixelBufferAttributes(VTVideoDecoderSessionRef session); /* borrowed */
extern OSStatus  VTDecoderSessionCreatePixelBuffer(VTVideoDecoderSessionRef session, CFDictionaryRef unused,
                                                  CVPixelBufferRef *pixelBufferOut);                           /* [I] x1 */

/* Info-dictionary keys.  IsHardwareAccelerated defaults to TRUE when absent; Rating defaults to 0.  Higher rating
 * sorts first; hardware sorts first on equal ratings.  RequiredSpecificationEntries is a dictionary of key/value
 * pairs that the caller's decoder specification must contain (checked before the factory is called). */
extern const CFStringRef kVTVideoDecoder_IsHardwareAccelerated;
extern const CFStringRef kVTVideoDecoderInfo_Rating;
extern const CFStringRef kVTVideoDecoderInfo_RequiredSpecificationEntries;
extern const CFStringRef kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder;
extern const CFStringRef kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder;

#endif /* N2VT_DEC_ABI_H */
```

`n2_VTVideoDecoderClass` is 0x58 bytes and the offsets are asserted; a compiler that disagrees with the cache
fails at build time rather than at run time.

## 7. What is not verified, and known limits

* **Tile decode slots** (`+0x40`, `+0x48`, `+0x50`): the slot order and version gate are `[V]` (the
  `_ParavirtualizedVideoDecoder` names and the three call sites), but `decodeTile`'s argument meanings are `[I]`;
  only the register layout (x1..x6 64-bit, w7, stack out-pointer) is proven. The Slim decoder does not need tiles.
* `VTDecoderSessionEmitDecodedTile` and `VTDecoderSessionCreatePixelBuffer` argument names are `[I]`; their
  registers and effects are `[V]`.
* `VTDecoderSessionTrace` is a no-op in this build; its variadic signature is `[I]`.
* `H264SW.videocodec` (the first loader entry) is included in this document: class version 3 at `0x214af1440`,
  entry `VCPAVCRegisterDecoder`, factory `0x1f0f39ee4`. It registers `avc1` with `IsHardwareAccelerated=false`
  and no rating, and leaves `finishDelayedFrames`/`finishDelayedTiles` NULL even at version 3 (legal: both
  wrappers test the slot before calling it).
* The landed `gpu/hwcodec/include/n2vt_dec_abi.h` is not byte-identical to section 6 (names and extra constants,
  listed there); the layout is the same and it is the interface DEC06 uses. If the two ever disagree, the cache
  record and this checker are the evidence.
* The selection narrative describes the in-process path: `VTDecompressionSessionCreateWithOptions` calls
  `VTSelectAndCreateVideoDecoderInstanceInternal` directly at `0x194cf8cec`. `VTDecompressionSessionRemote_*`
  (the default in a client process unless `VTRunVideoDecodersInProcess` is set or `AllowClientProcessDecode` is
  passed; `gpu/hwcodec/facts/VT_DECODER_ABI.md` section 4.5) has its own copies of some checks and was not
  followed here. Where the plug-in runs and which sandbox it inherits matters to DEC06 [I].
* `_VTGetAllowedDecoderList` (a testing allow-list) is mentioned but its contents are not inspected.
* `VTRegisterVideoDecoderWithInfo` uses the *mutable copy* of the info dictionary; mutating the caller's dictionary
  after registration has no effect.
* No decoder record in this cache has version 4 or a slot beyond `+0x50`. The 11-slot layout is the version
  1-3 contract; a future version-4 tail is unknown and is not needed by the Slim decoder.
* No phone run: this is a static reading of the cache. Nothing here proves a plug-in loads on the phone.

## 8. Reproduction and evidence

```
# the user's own cache copy (read-only)
export N2_DYLD_CACHE=$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64
python3 gpu/hwcodec/vt/dec/abi_tools/n2decabi.py --check   # N2TEST DEC08 PASS n/n
python3 gpu/hwcodec/vt/dec/abi_tools/n2decabi.py --write   # rewrites this file
```

The checker reads each of the five records twice (pointer fields and slot values), re-derives the 23-entry loader
list by simulating the ADRP/ADD register values in `_VTLoadVideoDecoders`, follows each CreateInstance's veneers
to `VTVideoDecoderGetClassID` and `CMDerivedObjectCreate`, and compares every instruction fingerprint in
the fact table. The symbol names in the `syms` fields are from the cache's symbol table (the dump used by
`gpu/video/research`); the addresses and records are re-read from the raw cache on every run, so a different cache
fails the check instead of silently changing the document.

| function | address | kind |
|---|---|---|
| `CMDerivedObjectCreate` | `0x1915c3cac` | local symbol |
| `VTDecoderSessionCleanUpAfterDecode` | `0x194cfc1e8` | exported / data |
| `VTDecoderSessionCreatePixelBuffer` | `0x194cfbb88` | exported / data |
| `VTDecoderSessionEmitDecodedFrame` | `0x194ccbe7c` | exported / data |
| `VTDecoderSessionEmitDecodedTile` | `0x194f92668` | exported / data |
| `VTDecoderSessionGetDestinationPixelBufferAttributes` | `0x194cfc240` | exported / data |
| `VTDecoderSessionGetPixelBufferPool` | `0x194cc9c44` | exported / data |
| `VTDecoderSessionSetPixelBufferAttributes` | `0x194cc9bac` | exported / data |
| `VTDecoderSessionTrace` | `0x194cfc1f0` | exported / data |
| `VTDecompressionSessionCanAcceptFormatDescription` | `0x194cfbe5c` | exported / data |
| `VTDecompressionSessionCopySerializableProperties` | `0x194cfac38` | exported / data |
| `VTDecompressionSessionCopySupportedPropertyDictionary` | `0x194ccb258` | exported / data |
| `VTDecompressionSessionCreateWithOptions` | `0x194cf868c` | exported / data |
| `VTDecompressionSessionFinishDelayedFrames` | `0x194cfc108` | exported / data |
| `VTDecompressionSessionSetProperties` | `0x194cfaa20` | exported / data |
| `VTIsHardwareDecodeSupported` | `0x194cfb8a8` | exported / data |
| `VTLoadVideoDecoder` | `0x194cc807c` | local symbol |
| `VTLoadVideoDecoders` | `0x194cc7ea4` | local symbol |
| `VTRegisterVideoDecoder` | `0x194cc80bc` | exported / data |
| `VTRegisterVideoDecoderWithInfo` | `0x194cc80c8` | exported / data |
| `VTSelectAndCreateVideoDecoderInstance` | `0x194cc73d0` | exported / data |
| `VTSelectAndCreateVideoDecoderInstanceInternal` | `0x194cc7408` | local symbol |
| `VTTileDecompressionSessionCreate` | `0x194f92088` | exported / data |
| `VTTileDecompressionSessionDecodeTile` | `0x194f9249c` | exported / data |
| `VTTileDecompressionSessionFinishDelayedTiles` | `0x194f91720` | exported / data |
| `VTVideoDecoderCanAcceptFormatDescription` | `0x194cfbf84` | local symbol |
| `VTVideoDecoderCopyFormattingDesc` | `0x194cfccd0` | local symbol |
| `VTVideoDecoderGetCMBaseObject` | `0x194cfc1e4` | exported / data |
| `VTVideoDecoderGetClassID` | `0x194cc9ad4` | exported / data |
| `VTVideoDecoderGetTypeID` | `0x194cfc1b8` | exported / data |
| `kCFBooleanFalse` | `0x1df247f20` | exported / data |
| `kCFBooleanTrue` | `0x1df247f18` | exported / data |
| `kVTVideoDecoderInfo_Rating` | `0x1d69aa9d8` | exported / data |
| `kVTVideoDecoderInfo_RequiredSpecificationEntries` | `0x1d69aac20` | exported / data |
| `kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder` | `0x1d69aa938` | exported / data |
| `kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder` | `0x1d69aa930` | exported / data |
| `kVTVideoDecoder_AllowSandboxedDecode` | `0x1d69aac50` | exported / data |
| `kVTVideoDecoder_CodecTypeKey` | `0x1d69aac18` | exported / data |
| `kVTVideoDecoder_IsHardwareAccelerated` | `0x1d69aac58` | exported / data |
| `vtCheckRequiredSpecificationKey` | `0x194ed2844` | local symbol |
| `vtCreateAndPopulateVideoDecoderRegistry` | `0x194ed0bbc` | local symbol |
| `vtDecompressionDuctDecodeSingleFrame` | `0x194ccb6c4` | local symbol |
| `vtFilterRegistryItemByCodecTypeAndVideoDecoderSpecification` | `0x194ed12e0` | local symbol |
| `vtRegisterVideoDecoderInternal` | `0x194ed0638` | local symbol |
| `vtSortRegistryItemsByRating` | `0x194ed1c58` | local symbol |

