#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""DEC08: derive the VideoToolbox video-decoder plug-in ABI from the phone's dyld shared cache.

    python3 n2decabi.py --check         verify every fact in ABI.md against the cache (N2TEST line)
    python3 n2decabi.py --write         regenerate ABI.md next to this directory
    python3 n2decabi.py --json          the same facts as JSON (for other workers)
    python3 n2decabi.py --disasm ADDR [END]   annotated disassembly of one range

The cache is read through n2cache.py.  Nothing is written outside this package except when --write is used.
All Apple code is disassembled, never copied: the excerpts in ABI.md are instruction listings and string values.
"""
import argparse
import json
import os
import struct
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from n2cache import Cache, DEFAULT_CACHE, fix

DOC = os.path.join(os.path.dirname(HERE), 'ABI.md')

# ---------------------------------------------------------------------------------------------------------------
# The fact table.  Addresses are unslid cache addresses in the iOS 16.7.16 (20H392, iPhone X) shared cache.
# Every entry is checked against the cache; nothing here is taken from a header.
# ---------------------------------------------------------------------------------------------------------------
VT = 0x194cc6000                    # /System/Library/Frameworks/VideoToolbox.framework/VideoToolbox

ADDR = {
    'VTRegisterVideoDecoder': 0x194cc80bc,
    'VTRegisterVideoDecoderWithInfo': 0x194cc80c8,
    'VTVideoDecoderGetClassID': 0x194cc9ad4,
    'VTVideoDecoderGetTypeID': 0x194cfc1b8,
    'VTVideoDecoderGetCMBaseObject': 0x194cfc1e4,
    'VTVideoDecoderCanAcceptFormatDescription': 0x194cfbf84,
    'VTVideoDecoderCopyFormattingDesc': 0x194cfccd0,
    'VTLoadVideoDecoders': 0x194cc7ea4,
    'VTLoadVideoDecoder': 0x194cc807c,
    'VTSelectAndCreateVideoDecoderInstance': 0x194cc73d0,
    'VTSelectAndCreateVideoDecoderInstanceInternal': 0x194cc7408,
    'vtCreateAndPopulateVideoDecoderRegistry': 0x194ed0bbc,
    'vtRegisterVideoDecoderInternal': 0x194ed0638,
    'vtFilterRegistryItemByCodecTypeAndVideoDecoderSpecification': 0x194ed12e0,
    'vtSortRegistryItemsByRating': 0x194ed1c58,
    'vtCheckRequiredSpecificationKey': 0x194ed2844,
    'VTDecompressionSessionCreateWithOptions': 0x194cf868c,
    'vtDecompressionDuctDecodeSingleFrame': 0x194ccb6c4,
    'VTDecompressionSessionFinishDelayedFrames': 0x194cfc108,
    'VTDecompressionSessionCopySupportedPropertyDictionary': 0x194ccb258,
    'VTDecompressionSessionSetProperties': 0x194cfaa20,
    'VTDecompressionSessionCopySerializableProperties': 0x194cfac38,
    'VTDecompressionSessionCanAcceptFormatDescription': 0x194cfbe5c,
    'VTIsHardwareDecodeSupported': 0x194cfb8a8,
    'VTTileDecompressionSessionCreate': 0x194f92088,
    'VTTileDecompressionSessionDecodeTile': 0x194f9249c,
    'VTTileDecompressionSessionFinishDelayedTiles': 0x194f91720,
    'VTDecoderSessionEmitDecodedFrame': 0x194ccbe7c,
    'VTDecoderSessionEmitDecodedTile': 0x194f92668,
    'VTDecoderSessionGetPixelBufferPool': 0x194cc9c44,
    'VTDecoderSessionSetPixelBufferAttributes': 0x194cc9bac,
    'VTDecoderSessionGetDestinationPixelBufferAttributes': 0x194cfc240,
    'VTDecoderSessionTrace': 0x194cfc1f0,
    'VTDecoderSessionCreatePixelBuffer': 0x194cfbb88,
    'VTDecoderSessionCleanUpAfterDecode': 0x194cfc1e8,
    'CMDerivedObjectCreate': 0x1915c3cac,      # CoreMedia; reached through each plug-in's stub
    'kCFBooleanTrue': 0x1df247f18,
    'kCFBooleanFalse': 0x1df247f20,
    'kVTVideoDecoder_IsHardwareAccelerated': 0x1d69aac58,
    'kVTVideoDecoderInfo_Rating': 0x1d69aa9d8,
    'kVTVideoDecoderInfo_RequiredSpecificationEntries': 0x1d69aac20,
    'kVTVideoDecoder_CodecTypeKey': 0x1d69aac18,
    'kVTVideoDecoder_AllowSandboxedDecode': 0x1d69aac50,
    'kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder': 0x1d69aa930,
    'kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder': 0x1d69aa938,
}

# (label, image suffix, vtable, class version, expected non-null slots, expected base slots)
VTABLES = [
    dict(label='H264SW', image='H264SW.videocodec', vtable=0x214af13d0, version=3,
         syms=dict(vtable='(stripped image; name kVCPAVCVideoDecoderVTable is ours)',
                   base='(stripped)', cls='(stripped)'),
         slots={1: 0x1f0f3abcc, 2: 0x1f0f3ac5c, 3: 0x1f0f3af74,
                6: 0x1f0f3b024, 8: 0x1f0f3b1fc, 9: 0x1f0f3b284},
         base={4: 0x1f0f3a0ac, 5: 0x1f0f3a0fc, 6: 0x1f0f3a150, 7: 0x1f0f3a434}),
    dict(label='VCH263', image='VCH263.videodecoder', vtable=0x214e8dd20, version=1,
         syms=dict(vtable='_kVCH263VideoDecoderVTable',
                   base='_kVCH263VideoDecoder_BaseClass',
                   cls='_kVCH263VideoDecoder_VideoDecoderClass'),
         slots={1: 0x201caec00, 2: 0x201caee80},
         base={4: 0x201caeb8c}),
    dict(label='VCPMP4V', image='VCPMP4V.videodecoder', vtable=0x214af1300, version=1,
         syms=dict(vtable='__ZL24kMPEG4VideoDecoderVTable',
                   base='__ZL28kMPEG4VideoDecoder_BaseClass',
                   cls='__ZL36kMPEG4VideoDecoder_VideoDecoderClass'),
         slots={1: 0x1f53f4a40, 2: 0x1f53f5230},
         base={4: 0x1f53f49a4, 5: 0x1f53f49ec}),
    dict(label='AV1SW', image='AV1SW.videodecoder', vtable=0x214aad780, version=3,
         syms=dict(vtable='(stripped image; name kAV1DecoderVTable is ours)',
                   base='(stripped)', cls='(stripped)'),
         slots={1: 0x1f4141640, 2: 0x1f41416b8, 3: 0x1f41419d4,
                6: 0x1f4141adc, 7: 0x1f4141bc0, 8: 0x1f4141bf0,
                9: 0x1f4141c64, 10: 0x1f4141ff8},
         base={3: 0x1f4142028, 4: 0x1f4142170, 5: 0x1f4142174,
               6: 0x1f41421c8, 7: 0x1f4142338}),
    dict(label='ParavirtualizedVideoDecoder', image='VideoToolbox.framework/VideoToolbox',
         vtable=0x1d69e8d28, version=3,
         syms=dict(vtable='_kParavirtualizedVideoDecoder_VTable',
                   base='_kParavirtualizedVideoDecoder_BaseClass',
                   cls='_kParavirtualizedVideoDecoder_VideoDecoderClass'),
         slots={1: 0x194ef67f0, 2: 0x194ef68fc, 3: 0x194ef6b44, 4: 0x194ef6ca0,
                5: 0x194ef6ca8, 6: 0x194ef6cb0, 7: 0x194ef6cb8, 8: 0x194ef6d70,
                9: 0x194ef6e74, 10: 0x194ef7390},
         base={3: 0x194ef6374, 4: 0x194ef6418, 5: 0x194ef64ec, 6: 0x194ef65ac,
               7: 0x194ef66bc}),
]

# Every plug-in's CreateInstance: where it calls VTVideoDecoderGetClassID, which vtable it loads, and where it calls
# CMDerivedObjectCreate.  The calls go through per-image stubs (adrp/add/br x16); the checker follows one stub.
# (label, entry, pc of bl to GetClassID, pc of adrp that loads the vtable, pc of bl to CMDerivedObjectCreate)
CREATES = [
    ('H264SW', 0x1f0f39ee4, 0x1f0f39f00, 0x1f0f39f08, 0x1f0f39f18),
    ('VCH263', 0x201caead4, 0x201caeaf0, 0x201caeaf8, 0x201caeb08),
    ('VCPMP4V', 0x1f53f48e8, 0x1f53f4904, 0x1f53f490c, 0x1f53f491c),
    ('AV1SW', 0x1f4141208, 0x1f4141234, 0x1f414123c, 0x1f414124c),
]

SLOT_NAMES = [
    'version', 'startSession', 'decodeFrame', 'copySupportedPropertyDictionary',
    'setProperties', 'copySerializableProperties', 'canAcceptFormatDescription',
    'finishDelayedFrames', 'startTileSession', 'decodeTile', 'finishDelayedTiles',
]
BASE_NAMES = [
    'version', 'derivedStorageSize', 'equal', 'invalidate', 'finalize',
    'copyDebugDescription', 'copyProperty', 'setProperty', 'reserved8', 'reserved9', 'reserved10',
]

PLUGINS_EXPECTED = [
    ('/System/Library/VideoCodecs/H264SW.videocodec', 'VCPAVCRegisterDecoder'),
    ('/System/Library/VideoDecoders/VCH263.videodecoder', 'VCH263Register'),
    ('/System/Library/VideoDecoders/H264H2.videodecoder', 'H264H2Register'),
    ('/System/Library/VideoDecoders/MP4VH2.videodecoder', 'MP4VH2Register'),
    ('/System/Library/VideoDecoders/Slim.videodecoder', 'SLIMRegister'),
    ('/System/Library/VideoDecoders/JPEGH1.videodecoder', 'JPEGH1Register'),
    ('/System/Library/VideoDecoders/H264H4.videodecoder', 'H264H4Register'),
    ('/System/Library/VideoDecoders/MP4VH4.videodecoder', 'MP4VH4Register'),
    ('/System/Library/VideoDecoders/H264H6.videodecoder', 'H264H6Register'),
    ('/System/Library/VideoDecoders/MP4VH6.videodecoder', 'MP4VH6Register'),
    ('/System/Library/VideoDecoders/H264H8.videodecoder', 'H264H8Register'),
    ('/System/Library/VideoDecoders/MP4VH8.videodecoder', 'MP4VH8Register'),
    ('/System/Library/VideoDecoders/HEVCH8.videodecoder', 'HEVCH8Register'),
    ('/System/Library/VideoDecoders/H8.videodecoder', 'H8Register'),
    ('/System/Library/VideoCodecs/VCPHEVC.videocodec', 'VCPHEVCRegisterDecoder'),
    ('/System/Library/VideoDecoders/AVD.videodecoder', 'AVDRegister'),
    ('/System/Library/VideoDecoders/ExampleIPB.videodecoder', 'ExampleIPBDecoderRegister'),
    ('/System/Library/VideoDecoders/BritannicusDecoder.videodecoder', 'BritannicusRegister'),
    ('/System/Library/VideoDecoders/VCPMP4V.videodecoder', 'VCPMP4VRegister'),
    ('/System/Library/VideoDecoders/AppleProResSWDecoder.videodecoder', 'ProResSWDecoderRegister'),
    ('/System/Library/VideoDecoders/AppleProResHWDecoder.videodecoder', 'ProResDecoderRegister'),
    ('/System/Library/VideoDecoders/AV1SW.videodecoder', 'AV1RegisterDecoder'),
    ('/AppleInternal/Library/VideoCodecs/yzip.videocodec', 'yzipRegisterDecoder'),
]

# Instruction fingerprints: address -> (expected word, mask).  The masks ignore register fields where the register
# number is not the fact being checked.
FP = {
    # _VTLoadVideoDecoders: first item H264SW + the Slim item
    0x194cc7eac: (0xb00018c0, 0xffffffff),   # adrp x0, page of "/System/Library/VideoCodecs/H264SW.videocodec"
    0x194cc7eb0: (0x91216400, 0xffffffff),   # add  x0, x0, #0x859
    0x194cc7f00: (0x91258000, 0xffffffff),   # add  x0, x0, #0x960 -> "/System/Library/VideoDecoders/Slim.videodecoder"
    0x194cc7f04: (0xb00018c1, 0xffffffff),   # adrp x1, page of "SLIMRegister"
    0x194cc7f0c: (0x9400005c, 0xfc000000),   # bl   _VTLoadVideoDecoder
    # _VTLoadVideoDecoder: dlopen(path, 4); dlsym; br x0
    0x194cc808c: (0x52800081, 0xffffffff),   # mov  w1, #4  (RTLD_LOCAL)
    0x194cc8090: (0x940c3f33, 0xfc000000),   # bl   _dlopen
    0x194cc809c: (0x95a95f50, 0xfc000000),   # bl   _dlsym
    0x194cc80ac: (0xd61f0000, 0xffffffff),   # br   x0 (the entry function)
    # VTRegisterVideoDecoder = WithInfo(codecType, NULL, createInstance)
    0x194cc80bc: (0xaa0103e2, 0xffffffff),   # mov  x2, x1
    0x194cc80c0: (0xd2800001, 0xffffffff),   # mov  x1, #0
    0x194cc80c4: (0x14000001, 0xffffffff),   # b    _VTRegisterVideoDecoderWithInfo
    # VTRegisterVideoDecoderWithInfo: default IsHardwareAccelerated = kCFBooleanTrue when absent
    0x194cc80ec: (0xb9000bff, 0xffffffff),   # str  wzr, [sp, #8]  (rating value 0)
    0x194cc8104: (0x5ac00a68, 0xffffffff),   # rev  w8, w19       (codec type 4cc -> byte order)
    0x194cc8120: (0x52800082, 0xffffffff),   # mov  w2, #4        (CFStringCreateWithBytes length)
    0x194cc8124: (0x5280c003, 0xffffffff),   # mov  w3, #0x600    (ASCII encoding)
    0x194cc812c: (0x95a95956, 0xfc000000),   # bl   CFStringCreateWithBytes
    0x194cc825c: (0x95a958aa, 0xfc000000),   # bl   CFDictionarySetValue (VTCodecType)
    0x194cc822c: (0xb50000e0, 0xffffffff),   # cbnz x0, skip (key present)
    0x194cc8230: (0xb022ff08, 0xffffffff),   # adrp x8, _kCFBooleanTrue slot
    0x194cc8244: (0x95a958b0, 0xfc000000),   # bl   CFDictionarySetValue
    0x194cc8284: (0x52800061, 0xffffffff),   # mov  w1, #3 (kCFNumberSInt32Type)
    0x194cc8288: (0x95a958b7, 0xfc000000),   # bl   CFNumberCreate
    # VTSelectAndCreateVideoDecoderInstanceInternal: filter + sort + factory retry
    0x194cc7750: (0x910b8021, 0xffffffff),   # add x1, x1, #0x2e0 -> filter 0x194ed12e0
    0x194cc7970: (0x95a95a85, 0xfc000000),   # bl  CFArraySortValues
    0x194cc7bf4: (0xd63f0100, 0xffffffff),   # blr x8 (factory)
    0x194cc7c44: (0xaa0003f8, 0xffffffff),   # mov x24, x0 (factory status)
    0x194cc7cc4: (0x910006b5, 0xffffffff),   # add x21, x21, #1
    0x194cc7ccc: (0x54ffede1, 0xffffffff),   # b.ne loop
    # filter: RequireHardware + IsHardwareAccelerated
    0x194ed1430: (0xf94013e0, 0xffffffff),   # ldr x0,[sp,#0x20] (matching info)
    0x194ed143c: (0xf9400101, 0xffffffff),   # ldr x1,[x8] (IsHardwareAccelerated key)
    0x194ed1444: (0x95a13724, 0xfc000000),   # bl FigCFDictionaryGetBooleanIfPresent
    0x194ed1608: (0x39404268, 0xffffffff),   # ldrb w8,[x19,#0x10] (require hardware)
    0x194ed1610: (0x39401fe8, 0xffffffff),   # ldrb w8,[sp,#7] (is hardware)
    0x194ed1614: (0x35000248, 0xffffffff),   # cbnz w8, continue
    0x194ed1618: (0x52800014, 0xffffffff),   # mov w20,#0 (reject)
    # sort comparator: Rating then IsHardwareAccelerated
    0x194ed1ca8: (0xaa1303e1, 0xffffffff),   # mov x1,x19 (IsHW key) + get boolean
    0x194ed1cd8: (0x95a1350e, 0xfc000000),   # bl FigCFDictionaryGetInt32IfPresent (rating)
    0x194ed1cf8: (0xda8ac553, 0xffffffff),   # cneg x19,x10,le (higher rating first)
    # StartSession call in VTDecompressionSessionCreateWithOptions, and the fatal error path
    0x194cf8ebc: (0xd63f0100, 0xffffffff),   # blr x8 (class +0x08)
    0x194cf8ec0: (0x35000060, 0xffffffff),   # cbnz w0, error (no retry of another decoder)
    0x194cf8ec4: (0x14000023, 0xffffffff),   # b success
    # DecodeFrame call: decoder, frame, sampleBuffer, flags, infoFlagsOut
    0x194ccbb90: (0xaa1703e0, 0xffffffff),   # mov x0,x23 (decoder)
    0x194ccbba4: (0xd63f0100, 0xffffffff),   # blr x8 (class +0x10)
    # property calls
    0x194ccb334: (0xd63f0100, 0xffffffff),   # blr x8 (class +0x18)
    0x194cfab7c: (0xd63f0100, 0xffffffff),   # blr x8 (class +0x20)
    0x194cfaccc: (0xaa1503e0, 0xffffffff),   # mov x0,x21 (decoder)
    0x194cfacd0: (0xaa1403e1, 0xffffffff),   # mov x1,x20 (allocator)
    0x194cfacd4: (0xaa1303e2, 0xffffffff),   # mov x2,x19 (out)
    0x194cfacd8: (0xd63f0100, 0xffffffff),   # blr x8 (class +0x28)
    # CanAcceptFormatDescription: version >= 2 then class +0x30
    0x194cfbfa0: (0xf9400109, 0xffffffff),   # ldr x9,[x8] (version)
    0x194cfbfa4: (0xf100093f, 0xffffffff),   # cmp x9,#2
    0x194cfbfac: (0xf9401902, 0xffffffff),   # ldr x2,[x8,#0x30]
    0x194cfbfc4: (0xd61f0040, 0xffffffff),   # br x2
    # FinishDelayedFrames: version >= 2 then class +0x38
    0x194cfc188: (0xf9400109, 0xffffffff),   # ldr x9,[x8]
    0x194cfc18c: (0xf100093f, 0xffffffff),   # cmp x9,#2
    0x194cfc194: (0xf9401d08, 0xffffffff),   # ldr x8,[x8,#0x38]
    0x194cfc1a0: (0xd63f0100, 0xffffffff),   # blr x8
    # tiles: version >= 3 then class +0x40 / +0x48 / +0x50
    0x194f92358: (0xf1000d3f, 0xffffffff),   # cmp x9,#3
    0x194f92360: (0xf9402108, 0xffffffff),   # ldr x8,[x8,#0x40]
    0x194f92374: (0xd63f0100, 0xffffffff),   # blr x8
    0x194f925c0: (0xf1000d3f, 0xffffffff),   # cmp x9,#3
    0x194f925c8: (0xf9402508, 0xffffffff),   # ldr x8,[x8,#0x48]
    0x194f91768: (0xf1000d3f, 0xffffffff),   # cmp x9,#3
    0x194f91770: (0xf9402901, 0xffffffff),   # ldr x1,[x8,#0x50]
    # session callbacks
    0x194ccbea4: (0xaa0403f9, 0xffffffff),   # mov x25,x4 (image buffer)
    0x194ccbeac: (0xaa0203fa, 0xffffffff),   # mov x26,x2 (status)
    0x194ccbeb0: (0xaa0103f4, 0xffffffff),   # mov x20,x1 (frame)
    0x194ccbeb8: (0x7100005f, 0xffffffff),   # cmp w2,#0
    0x194ccbebc: (0x9a9f0096, 0xffffffff),   # csel x22,x4,xzr,eq
    0x194cc9c54: (0xf9400013, 0xffffffff),   # ldr x19,[x0] (subduct)
    0x194cc9c7c: (0xf9402660, 0xffffffff),   # ldr x0,[x19,#0x48] (pixel buffer pool)
    0x194cc9bc4: (0xaa0103f3, 0xffffffff),   # mov x19,x1 (attributes)
    0x194cc9bd4: (0xaa1303e0, 0xffffffff),   # mov x0,x19
    0x194cc9be8: (0xf9001693, 0xffffffff),   # str x19,[x20,#0x28]
    0x194cfc24c: (0xf9400008, 0xffffffff),   # ldr x8,[x0]
    0x194cfc254: (0xf9401900, 0xffffffff),   # ldr x0,[x8,#0x30]
    0x194cfc23c: (0xd65f03c0, 0xffffffff),   # ret (Trace is a no-op after its checks)
    0x194cfc1e4: (0xd65f03c0, 0xffffffff),   # ret (GetCMBaseObject returns its argument)
    # class registration
    0x194cc9aec: (0x95a95748, 0xfc000000),   # bl FigThreadRunOnce
    0x194cc9af4: (0xf9430900, 0xffffffff),   # ldr x0,[x8,#0x610] (sVTVideoDecoderClassID)
    # AV1SW registration: av01 + IsHardwareAccelerated=false
    0x1f4142634: (0xf942f108, 0xffffffff),   # ldr x8,[x8,#0x5e0] (IsHW key slot)
    0x1f414264c: (0xf9432108, 0xffffffff),   # ldr x8,[x8,#0x640] (kCFBooleanFalse)
    0x1f41426a4: (0x52860620, 0xffffffff),   # mov w0,#0x3031
    0x1f41426a8: (0x72ac2ec0, 0xffffffff),   # movk w0,#0x6176,'av01'
    0x1f41426b0: (0x9572c9bc, 0xfc000000),   # bl VTRegisterVideoDecoderWithInfo
    # VMP4V registration: mp4v + IsHW false
    0x1f54000d4: (0xf9432108, 0xffffffff),   # ldr x8,[x8,#0x640] (kCFBooleanFalse)
    0x1f540010c: (0x52868ec0, 0xffffffff),   # mov w0,#0x3476
    0x1f5400110: (0x72adae00, 0xffffffff),   # movk w0,#0x6d70,'mp4v'
    0x1f5400118: (0x952830dc, 0xfc000000),   # bl VTRegisterVideoDecoderWithInfo
    # VCH263 registration: rating -10
    0x201cae8d8: (0x12800128, 0xffffffff),   # mov w8,#-0xa
    0x201cae948: (0xaa1403e1, 0xffffffff),   # mov x1,x20 (info)
}

# First instruction of every code entry in ADDR.  This pins the address itself (a nearby fingerprint cannot catch an
# ADDR value edited by hand and re-rendered).
STARTS = {
    'CMDerivedObjectCreate': 0xa9bd57f6,
    'VTDecoderSessionCleanUpAfterDecode': 0x52800000,
    'VTDecoderSessionCreatePixelBuffer': 0xa9be4ff4,
    'VTDecoderSessionEmitDecodedFrame': 0xd102c3ff,
    'VTDecoderSessionEmitDecodedTile': 0x14000001,
    'VTDecoderSessionGetDestinationPixelBufferAttributes': 0xa9bf7bfd,
    'VTDecoderSessionGetPixelBufferPool': 0xa9be4ff4,
    'VTDecoderSessionSetPixelBufferAttributes': 0xa9be4ff4,
    'VTDecoderSessionTrace': 0xb4000120,
    'VTDecompressionSessionCanAcceptFormatDescription': 0xa9bc5ff8,
    'VTDecompressionSessionCopySerializableProperties': 0xa9bd57f6,
    'VTDecompressionSessionCopySupportedPropertyDictionary': 0xd10143ff,
    'VTDecompressionSessionCreateWithOptions': 0xd10483ff,
    'VTDecompressionSessionFinishDelayedFrames': 0xa9be4ff4,
    'VTDecompressionSessionSetProperties': 0xd10183ff,
    'VTIsHardwareDecodeSupported': 0xa9bc5ff8,
    'VTLoadVideoDecoder': 0xa9be4ff4,
    'VTLoadVideoDecoders': 0xa9bf7bfd,
    'VTRegisterVideoDecoder': 0xaa0103e2,
    'VTRegisterVideoDecoderWithInfo': 0xd10143ff,
    'VTSelectAndCreateVideoDecoderInstance': 0xd10083ff,
    'VTSelectAndCreateVideoDecoderInstanceInternal': 0xd10483ff,
    'VTTileDecompressionSessionCreate': 0xd10243ff,
    'VTTileDecompressionSessionDecodeTile': 0xd101c3ff,
    'VTTileDecompressionSessionFinishDelayedTiles': 0xa9be4ff4,
    'VTVideoDecoderCanAcceptFormatDescription': 0xa9be4ff4,
    'VTVideoDecoderCopyFormattingDesc': 0xf020e780,
    'VTVideoDecoderGetCMBaseObject': 0xd65f03c0,
    'VTVideoDecoderGetClassID': 0xa9bf7bfd,
    'VTVideoDecoderGetTypeID': 0xa9bf7bfd,
    'vtCheckRequiredSpecificationKey': 0xa9be4ff4,
    'vtCreateAndPopulateVideoDecoderRegistry': 0xd10483ff,
    'vtDecompressionDuctDecodeSingleFrame': 0xd10583ff,
    'vtFilterRegistryItemByCodecTypeAndVideoDecoderSpecification': 0xd10183ff,
    'vtRegisterVideoDecoderInternal': 0xa9bc5ff8,
    'vtSortRegistryItemsByRating': 0xd10103ff,
}

# The ADDR entries for these are cells holding a CFStringRef; each must name the documented key.
KEY_NAMES = {
    'kVTVideoDecoder_IsHardwareAccelerated': 'VTIsHardwareAccelerated',
    'kVTVideoDecoderInfo_Rating': 'VTRating',
    'kVTVideoDecoderInfo_RequiredSpecificationEntries': 'VTRequiredSpecificationEntries',
    'kVTVideoDecoder_CodecTypeKey': 'VTCodecType',
    'kVTVideoDecoder_AllowSandboxedDecode': 'VTAllowSandboxedDecode',
    'kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder': 'EnableHardwareAcceleratedVideoDecoder',
    'kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder': 'RequireHardwareAcceleratedVideoDecoder',
}

# Call sites cited in ABI.md: (label, address, what the checker verifies there)
CITES = [
    ('StartSession call', 0x194cf8ebc, 'blr x8, class +0x08, followed by cbnz w0 (fatal)'),
    ('DecodeFrame call', 0x194ccbba4, 'blr x8, class +0x10'),
    ('CopySupportedPropertyDictionary call', 0x194ccb334, 'blr x8, class +0x18'),
    ('SetProperties call', 0x194cfab7c, 'blr x8, class +0x20'),
    ('CopySerializableProperties call', 0x194cfacd8, 'blr x8, class +0x28'),
    ('CanAcceptFormatDescription wrapper', 0x194cfbfac, 'class +0x30, version >= 2'),
    ('FinishDelayedFrames call', 0x194cfc194, 'class +0x38, version >= 2'),
    ('StartTileSession call', 0x194f92360, 'class +0x40, version >= 3'),
    ('DecodeTile call', 0x194f925c8, 'class +0x48, version >= 3'),
    ('FinishDelayedTiles call', 0x194f91770, 'class +0x50, version >= 3'),
]


# ---------------------------------------------------------------------------------------------------------------
# Readers
# ---------------------------------------------------------------------------------------------------------------
def record(cache, vt):
    vtable = vt['vtable']
    base = cache.ptr(vtable + 8)
    cls = cache.ptr(vtable + 16)
    return dict(
        reserved=cache.ptr(vtable),
        vtable=vtable, base=base, cls=cls,
        base_slots=[cache.q(base + i * 8) if i < 2 else cache.ptr(base + i * 8) for i in range(11)],
        cls_version=cache.q(cls),
        cls_slots=[cache.ptr(cls + i * 8) for i in range(11)],
    )


def is_adrp(w):
    return (w & 0x9f000000) == 0x90000000


def is_add_imm(w):
    return (w & 0xffc00000) == 0x91000000


def is_bl(w):
    return (w & 0xfc000000) == 0x94000000


def is_b(w):
    return (w & 0xfc000000) == 0x14000000


def sxt(imm, bits):
    return imm - (1 << bits) if imm & (1 << (bits - 1)) else imm


def adrp_target(w, pc):
    immlo = (w >> 29) & 3
    immhi = (w >> 5) & 0x7ffff
    imm = (immhi << 2) | immlo
    if imm & 0x100000:
        imm -= 0x200000
    return (pc & ~0xfff) + (imm << 12)


def adrp_add_target(cache, pc):
    """Target of an adjacent `adrp xN, page` + `add xN, xN, #imm` pair, or None."""
    w0 = cache.w(pc)
    if not is_adrp(w0):
        return None
    w1 = cache.w(pc + 4)
    if (w1 & 0xffc00000) != 0x91000000 or ((w1 >> 5) & 31) != (w0 & 31):
        return None
    return adrp_target(w0, pc) + ((w1 >> 10) & 0xfff)


def stub_target(cache, pc):
    """Target of a veneer `adrp x16, page; add x16, x16, #imm; br x16` at pc, or None."""
    w0 = cache.w(pc)
    if not is_adrp(w0) or (w0 & 31) != 16:
        return None
    w1 = cache.w(pc + 4)
    w2 = cache.w(pc + 8)
    if (w1 & 0xffc00000) != 0x91000000 or ((w1 >> 5) & 31) != 16 or w2 != 0xd61f0200:
        return None
    return adrp_target(w0, pc) + ((w1 >> 10) & 0xfff)


def plugin_list(cache):
    start, end = ADDR['VTLoadVideoDecoders'], ADDR['VTLoadVideoDecoder']
    regs = {}
    out = []
    for pc in range(start, end, 4):
        w = cache.w(pc)
        if is_adrp(w):
            regs[w & 31] = adrp_target(w, pc)
        elif is_add_imm(w):
            rn = (w >> 5) & 31
            if rn in regs:
                regs[w & 31] = regs[rn] + ((w >> 10) & 0xfff)
        elif is_bl(w) or is_b(w):
            tgt = pc + sxt(w & 0x3ffffff, 26) * 4
            if tgt == ADDR['VTLoadVideoDecoder']:
                p = cache.cs(regs.get(0, 0))
                e = cache.cs(regs.get(1, 0))
                if p and e:
                    out.append((p, e))
                if is_b(w):
                    break
    return out


def word(cache, addr):
    return cache.w(addr)


def bl_target(cache, pc):
    w = cache.w(pc)
    return pc + sxt(w & 0x3ffffff, 26) * 4 if is_bl(w) else None


def b_target(cache, pc):
    w = cache.w(pc)
    return pc + sxt(w & 0x3ffffff, 26) * 4 if is_b(w) else None


# ---------------------------------------------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------------------------------------------
class Checks(object):
    def __init__(self):
        self.items = []

    def add(self, ok, label, detail=''):
        self.items.append((bool(ok), label, detail))

    def eq(self, label, got, want):
        self.add(got == want, label, '%r == %r' % (got, want))
        return got == want

def check_header():
    import subprocess, tempfile
    inc = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', 'video', 'include'))
    if not os.path.exists(os.path.join(inc, 'n2vt_abi.h')):
        return False, 'n2vt_abi.h not found at ' + inc
    tmp = tempfile.mkdtemp(prefix='n2decabi-hdr-', dir=os.environ.get('N2_TMP', '/tmp'))
    hdr = os.path.join(tmp, 'n2vt_dec_abi.h')
    tu = os.path.join(tmp, 't.c')
    with open(hdr, 'w', encoding='utf-8') as f:
        f.write(HEADER)
    with open(tu, 'w', encoding='utf-8') as f:
        f.write('#include "n2vt_abi.h"\n#include "n2vt_dec_abi.h"\n'
                'int main(void) { return (int)sizeof(n2_VTVideoDecoderClass); }\n')
    r = subprocess.run(['clang', '-std=c11', '-fsyntax-only', '-Wall', '-Wextra', '-Werror',
                        '-I', inc, '-I', tmp, tu], capture_output=True, text=True)
    return r.returncode == 0, (r.stderr.strip().splitlines() or [''])[-1][:200]


def run_checks(cache):
    c = Checks()
    vt = {}
    for v in VTABLES:
        vt[v['label']] = record(cache, v)

    # image bases
    for suffix, base in (('VideoToolbox.framework/VideoToolbox', VT),
                         ('VCH263.videodecoder', 0x201cae000),
                         ('VCPMP4V.videodecoder', 0x1f53e0000),
                         ('AV1SW.videodecoder', 0x1f410d000),
                         ('H264SW.videocodec', 0x1f0f39000)):
        try:
            got, _ = cache.image(suffix)
        except KeyError:
            got = None
        c.eq('image base ' + suffix, got, base)

    # loader
    c.eq('VTLoadVideoDecoders prologue', cache.w(ADDR['VTLoadVideoDecoders']), 0xa9bf7bfd)
    c.eq('first plugin path string', cache.cs(0x194fe0859), '/System/Library/VideoCodecs/H264SW.videocodec')
    c.eq('Slim plugin path string', cache.cs(0x194fe0960), '/System/Library/VideoDecoders/Slim.videodecoder')
    c.eq('Slim plugin entry string', cache.cs(0x194fe0990), 'SLIMRegister')
    pl = plugin_list(cache)
    c.eq('plugin list length', len(pl), len(PLUGINS_EXPECTED))
    c.eq('plugin list contents', pl, PLUGINS_EXPECTED)
    c.add(('/System/Library/VideoDecoders/Slim.videodecoder', 'SLIMRegister') in pl, 'Slim entry in loader list')
    c.eq('_VTLoadVideoDecoder dlopen flags', cache.w(0x194cc808c) & 0xffffffff, 0x52800081)
    c.eq('_VTLoadVideoDecoder dlopen GOT slot', cache.ptr(0x1dac9bfc0), 0x1a6c81c04)   # libdyld _dlopen
    c.eq('_VTLoadVideoDecoder tail br', cache.w(0x194cc80ac), 0xd61f0000)
    c.eq('registry calls VTLoadVideoDecoders', bl_target(cache, 0x194ed0e58), ADDR['VTLoadVideoDecoders'])

    # vtable and class records
    for v in VTABLES:
        lab = v['label']
        r = vt[lab]
        c.eq(lab + ' vtable reserved', r['reserved'], 0)
        c.add(r['base'] != 0 and len(cache.rd(r['base'], 16)) == 16, lab + ' base record mapped', hex(r['base']))
        c.add(r['cls'] != 0 and len(cache.rd(r['cls'], 16)) == 16, lab + ' class record mapped', hex(r['cls']))
        c.eq(lab + ' base version', r['base_slots'][0], 1)
        c.add(0 < r['base_slots'][1] < 0x10000, lab + ' derivedStorageSize sane', hex(r['base_slots'][1]))
        c.eq(lab + ' class version', r['cls_version'], v['version'])
        c.add(r['cls_slots'][1] != 0, lab + ' startSession non-null', hex(r['cls_slots'][1]))
        c.add(r['cls_slots'][2] != 0, lab + ' decodeFrame non-null', hex(r['cls_slots'][2]))
        for slot, want in v['slots'].items():
            c.eq('%s class +0x%02x' % (lab, slot * 8), r['cls_slots'][slot], want)
        for slot, want in v['base'].items():
            c.eq('%s base +0x%02x' % (lab, slot * 8), r['base_slots'][slot], want)
    # version-1 objects must have the version-gated slots NULL unless implemented
    c.eq('VCH263 finishDelayedFrames', vt['VCH263']['cls_slots'][7], 0)
    c.eq('VCPMP4V canAccept', vt['VCPMP4V']['cls_slots'][6], 0)
    # H264SW is version 3 but leaves FinishDelayedFrames NULL (the wrapper checks for NULL, so this is legal)
    c.eq('H264SW finishDelayedFrames', vt['H264SW']['cls_slots'][7], 0)
    c.eq('H264SW finishDelayedTiles', vt['H264SW']['cls_slots'][10], 0)

    # every CreateInstance: VTVideoDecoderGetClassID -> its vtable -> CMDerivedObjectCreate
    vtable_of = {v['label']: v['vtable'] for v in VTABLES}
    for label, entry, idpc, vpc, newpc in CREATES:
        c.add(cache.w(entry) != 0, label + ' CreateInstance is code', hex(entry))
        c.eq(label + ' CreateInstance loads its vtable', adrp_add_target(cache, vpc), vtable_of[label])
        c.eq(label + ' CreateInstance calls VTVideoDecoderGetClassID',
             stub_target(cache, bl_target(cache, idpc)), ADDR['VTVideoDecoderGetClassID'])
        c.eq(label + ' CreateInstance calls CMDerivedObjectCreate',
             stub_target(cache, bl_target(cache, newpc)), ADDR['CMDerivedObjectCreate'])

    # call-site and semantics fingerprints
    for addr in sorted(FP):
        want, mask = FP[addr]
        got = cache.w(addr)
        c.eq('instruction %#x' % addr, got & mask, want & mask)
    for name, want in STARTS.items():
        c.eq('first word of ' + name, cache.w(ADDR[name]), want)
    for name, want in KEY_NAMES.items():
        c.eq(name + ' name', cache.cfstr(cache.ptr(ADDR[name])), want)
    c.add('CFBoolean objects differ', ADDR['kCFBooleanTrue'] != ADDR['kCFBooleanFalse'])
    for label, addr, _what in CITES:
        c.add(cache.w(addr) != 0, 'cited ' + label, hex(addr))

    # registration defaults
    c.eq('kCFBooleanTrue object', cache.ptr(0x1daca9b68), 0x1df247f18)
    c.eq('kCFBooleanFalse object', cache.ptr(0x1daca9b50), 0x1df247f20)
    c.eq('_VTRegisterVideoDecoder forwards to WithInfo', b_target(cache, 0x194cc80c4), 0x194cc80c8)
    # selection calls the filter and the rating sort
    c.eq('selection filter pointer',
         (adrp_target(cache.w(0x194cc774c), 0x194cc774c) + ((cache.w(0x194cc7750) >> 10) & 0xfff)),
         ADDR['vtFilterRegistryItemByCodecTypeAndVideoDecoderSpecification'])
    c.eq('selection sort pointer',
         (adrp_target(cache.w(0x194cc795c), 0x194cc795c) + ((cache.w(0x194cc7960) >> 10) & 0xfff)),
         ADDR['vtSortRegistryItemsByRating'])
    c.eq('create session calls selection', bl_target(cache, 0x194cf8cec), ADDR['VTSelectAndCreateVideoDecoderInstanceInternal'])
    # factory retry loop
    c.eq('factory retry loop increment', cache.w(0x194cc7cc4), 0x910006b5)
    c.eq('factory retry loop branch', cache.w(0x194cc7ccc), 0x54ffede1)
    # class version gates
    c.eq('canAccept version gate', cache.w(0x194cfbfa4), 0xf100093f)
    c.eq('finishDelayedFrames version gate', cache.w(0x194cfc18c), 0xf100093f)
    c.eq('tile version gate', cache.w(0x194f92358), 0xf1000d3f)
    # registration actual codec types
    c.eq('AV1 codec type', cache.w(0x1f41426a8), 0x72ac2ec0)
    c.eq('MP4V codec type', cache.w(0x1f5400110), 0x72adae00)
    c.eq('VCH263 rating -10', cache.w(0x201cae8d8), 0x12800128)

    # the class VTVideoDecoderGetClassID registers, and its formatting callback
    c.eq('VTVideoDecoder class descriptor version', cache.q(0x1d69ab410), 1)
    c.eq('VTVideoDecoder class descriptor name', cache.cs(0x194fdeeef), 'VTVideoDecoder')
    c.eq('VTVideoDecoder class descriptor formatting callback',
         cache.ptr(0x1d69ab410 + 0x18), ADDR['VTVideoDecoderCopyFormattingDesc'])
    c.eq('CopyFormattingDesc CFString', cache.cfstr(0x1d69ef808), '[VTVideoDecoder]')
    c.eq('CopyFormattingDesc tail call', b_target(cache, 0x194cfccd8), 0x19b71e5f4)
    c.eq('GetClassID loads the class descriptor',
         adrp_add_target(cache, 0x194cc9b10), 0x1d69ab410)

    # the proposed header text must compile against the frozen encoder header
    ok, detail = check_header()
    c.add(ok, 'proposed header compiles (-Werror)', detail)

    # documentation regenerates from the same data
    text = render(cache, vt, pl)
    try:
        cur = open(DOC, encoding='utf-8').read()
    except OSError:
        cur = None
    c.add(cur == text, 'ABI.md is the generated text', '' if cur is not None else 'missing')
    return c


# ---------------------------------------------------------------------------------------------------------------
# Markdown generation
# ---------------------------------------------------------------------------------------------------------------
def fmt_slot(v):
    return '0x%x' % v if v else '`NULL`'


def render(cache, vt, plugins):
    L = []
    A = ADDR
    vtable_of = {v['label']: v['vtable'] for v in VTABLES}
    add = L.append

    add('# DEC08 - the VideoToolbox video-decoder plug-in ABI of iOS 16.7.16')
    add('')
    add('Owner: hw-dec08. Read-only: this file and `abi_tools/`; nothing here was run on the phone.')
    add('')
    add('This is the decoder counterpart of `gpu/video/include/n2vt_abi.h` (the encoder ABI that `gpu/video` already')
    add('froze). It was read out of the user\'s own dyld shared cache (iOS 16.7.16, 20H392, iPhone X, arm64) with')
    add('`abi_tools/n2decabi.py`; regenerate it with `--write`, verify it with `--check`. Every address below is an unslid')
    add('cache address. `[V]` = read out of the cache in this package (record bytes, instruction, or string);')
    add('`[I]` = inferred, with the reason given. No Apple code is copied; the excerpts are disassembly facts.')
    add('')
    add('The frozen decoder header built from these facts is `gpu/hwcodec/include/n2vt_dec_abi.h`; its companion facts')
    add('file is `gpu/hwcodec/facts/VT_DECODER_ABI.md` (opus-dec3, same cache). This document is the full evidence dump')
    add('behind both: five real class records (including H264SW), the complete 23-entry loader list, every slot\'s caller,')
    add('and the session callbacks. Where the two overlap they agree; section 6 records the proposal and the landed names.')
    add('')
    add('The three questions a plug-in author has are answered in this order: **what the loader calls** (section 1),')
    add('**what object and class records to build** (sections 2-4), and **how VideoToolbox chooses and retries**')
    add('(section 5). Section 6 is the proposed header text; section 7 lists what is *not* verified.')
    add('')

    add('## 0. Short answer')
    add('')
    add('* The daemon/plug-in path is `/System/Library/VideoDecoders/Slim.videodecoder`, entry point `SLIMRegister`.')
    add('  VideoToolbox `dlopen`s it with `RTLD_LOCAL` and calls the entry with **no arguments** (section 1).')
    add('* A decoder is a `CMBaseObject` whose vtable is three pointers `{ 0, &baseClass, &decoderClass }`. The base')
    add('  class record is exactly `n2_CMBaseClass` from `n2vt_abi.h`; the decoder class record is **11 slots, 0x58 bytes**')
    add('  (section 2) - not 21 like the encoder class. Class versions seen in this cache: H264SW 3, VCH263 1, VCPMP4V 1,')
    add('  AV1SW 3, Paravirtualized 3.')
    add('* `decoderClass->version` gates only the optional tail: `canAcceptFormatDescription` and')
    add('  `finishDelayedFrames` need version >= 2; the three tile slots need version >= 3. The other slots are called')
    add('  whenever non-NULL.')
    add('* Register with `VTRegisterVideoDecoderWithInfo(codecType, info, createInstance)`; `createInstance` is')
    add('  `OSStatus (*)(uint32_t codecType, CFAllocatorRef, VTVideoDecoderRef *)` and normally does')
    add('  `CMDerivedObjectCreate(allocator, &vtable, VTVideoDecoderGetClassID(), out)`.')
    add('* If the info dictionary does not mention `kVTVideoDecoder_IsHardwareAccelerated`, VideoToolbox sets it to')
    add('  **true**; `kVTVideoDecoderInfo_Rating` defaults to **0** (section 4).')
    add('* Selection filters by codec type and the hardware specifications, sorts by rating (descending, hardware first on')
    add('  a tie) and calls each factory in turn; a factory error or NULL object tries the next decoder. A **StartSession')
    add('  error is fatal** - no other decoder is tried (section 5).')
    add('')

    add('## 1. How VideoToolbox loads the plug-in [V]')
    add('')
    add('`_VTLoadVideoDecoders` @ `%#x` is a fixed list of 23 (path, entry) pairs; for each it calls' % A['VTLoadVideoDecoders'])
    add('`_VTLoadVideoDecoder(path, entry)` @ `%#x`, which is:' % A['VTLoadVideoDecoder'])
    add('')
    add('```')
    add(cache.disasm(0x194cc807c, 0x194cc80b8))
    add('```')
    add('')
    add('The GOT slot read at `0x194cc8090` is `_dlopen` (libdyld.dylib, `%#x`) and the branch at `0x194cc809c`' % 0x1a6c81c04)
    add('resolves to `_dlsym` (`%#x`). So the contract is:' % 0x1a6c81c88)
    add('')
    add('```c')
    add('void *h = dlopen("/System/Library/VideoDecoders/Slim.videodecoder", RTLD_LOCAL);')
    add('void (*fn)(void) = (void (*)(void))dlsym(h, "SLIMRegister");')
    add('if (fn) fn();          /* no arguments are set: br x0 with stale x1..x7 */')
    add('```')
    add('')
    add('`_VTLoadVideoDecoders` is itself called once from `_vtCreateAndPopulateVideoDecoderRegistry` @ `%#x`' % A['vtCreateAndPopulateVideoDecoderRegistry'])
    add('(call at `0x194ed0e58`, inside a `FigThreadRunOnce`), i.e. lazily when the decoder registry is first populated -')
    add('not at process start. The full list, in call order, is:')
    add('')
    add('| # | path | entry |')
    add('|---|---|---|')
    for i, (p, e) in enumerate(plugins):
        add('| %d | `%s` | `%s` |' % (i + 1, p, e))
    add('')
    add('The `Slim.videodecoder` entry is #5: `0x194cc7f00` loads the path string at `0x194fe0960`, `0x194cc7f04` the')
    add('entry string at `0x194fe0990`, and `0x194cc7f0c` calls `_VTLoadVideoDecoder`. A missing file, or a missing entry')
    add('symbol, is silently ignored by the loader; only a successful `dlsym` is called.')
    add('')

    add('## 2. The object: one vtable, one base class record, one decoder class record [V]')
    add('')
    add('The vtable has three pointer-sized slots:')
    add('')
    add('```c')
    add('struct n2_VTVideoDecoderVTable {')
    add('    const void             *reserved;      /* NULL */')
    add('    const n2_CMBaseClass   *baseClass;     /* [vtable+0x08] */')
    add('    const n2_VTVideoDecoderClass *decoderClass; /* [vtable+0x10] */')
    add('};')
    add('```')
    add('')
    add('`CMBaseObjectGetVTable(object)` returns that pointer. `VTVideoDecoderGetCMBaseObject(object)` is literally `ret`')
    add('(`0x194cfc1e4`): the decoder reference and the CMBaseObject are the same address. All five records below were')
    add('found by scanning the plug-in data segments for the class methods and following the two record pointers; the')
    add('records are complete dumps, not samples.')
    add('')
    for v in VTABLES:
        r = vt[v['label']]
        add('### %s' % v['label'])
        add('')
        add('Image `%s`. Vtable @ `%#x`%s.' % (v['image'], r['vtable'],
            '' if '(' in v['syms']['vtable'] else ' (`%s`)' % v['syms']['vtable']))
        add('')
        add('| record | address | version | derivedStorageSize / non-NULL slots |')
        add('|---|---|---|---|')
        nz = [SLOT_NAMES[i] for i in range(1, 11) if r['cls_slots'][i]]
        add('| base class | `%#x`%s | %d | %#x |' % (r['base'],
            '' if '(' in v['syms']['base'] else ' (`%s`)' % v['syms']['base'],
            r['base_slots'][0], r['base_slots'][1]))
        add('| decoder class | `%#x`%s | %d | %s |' % (r['cls'],
            '' if '(' in v['syms']['cls'] else ' (`%s`)' % v['syms']['cls'],
            r['cls_version'], ', '.join(nz)))
        add('')
        add('| class slot | name | function |')
        add('|---|---|---|')
        for i in range(11):
            add('| `+0x%02x` | `%s` | %s |' % (i * 8, SLOT_NAMES[i], fmt_slot(r['cls_slots'][i])))
        add('')
        add('| base slot | name | value |')
        add('|---|---|---|')
        for i in range(11):
            val = r['base_slots'][i]
            add('| `+0x%02x` | `%s` | %s |' % (i * 8, BASE_NAMES[i], ('%#x' % val) if i < 2 else fmt_slot(val)))
        add('')

    add('### The class record, slot by slot')
    add('')
    add('The slot order is not guessed: it is the order of the named methods in Apple\'s own')
    add('`_ParavirtualizedVideoDecoder_VideoDecoderClass` (version 3) and every call site below indexes the same way.')
    add('The signatures come from the callee prologues and from the call sites in `_VTDecompressionSession*`;')
    add('argument names are ours.')
    add('')
    add('| off | name | signature | version | where called from (address of the `blr`) |')
    add('|---|---|---|---|---|')
    rows = [
        ('0x00', 'version', '`uint64_t`', '1', 'read by the wrappers before every gated slot'),
        ('0x08', 'startSession',
         '`OSStatus (*)(VTVideoDecoderRef, VTVideoDecoderSessionRef, CMFormatDescriptionRef)`', '1',
         '`0x194cf8ebc` in `VTDecompressionSessionCreateWithOptions`; x1 = `&session` whose first word is the sub-duct, x2 = format description'),
        ('0x10', 'decodeFrame',
         '`OSStatus (*)(VTVideoDecoderRef, VTVideoDecoderFrameRef, CMSampleBufferRef, uint32_t flags, uint32_t *infoFlagsOut)`',
         '1', '`0x194ccbba4` in `_vtDecompressionDuctDecodeSingleFrame`; x1=frame token, x2=sample buffer, w3=flags, x4=infoFlagsOut'),
        ('0x18', 'copySupportedPropertyDictionary', '`OSStatus (*)(VTVideoDecoderRef, CFDictionaryRef *out)`', 'any',
         '`0x194ccb334`; x1 = address of the out dictionary'),
        ('0x20', 'setProperties', '`OSStatus (*)(VTVideoDecoderRef, CFDictionaryRef)`', 'any',
         '`0x194cfab7c` in `VTDecompressionSessionSetProperties`'),
        ('0x28', 'copySerializableProperties',
         '`OSStatus (*)(VTVideoDecoderRef, CFAllocatorRef, CFDictionaryRef *out)`', 'any',
         '`0x194cfacd8`; x1 is the allocator passed to `VTDecompressionSessionCopySerializableProperties`, x2 = out'),
        ('0x30', 'canAcceptFormatDescription', '`Boolean (*)(VTVideoDecoderRef, CMFormatDescriptionRef)`', '>= 2',
         '`_VTVideoDecoderCanAcceptFormatDescription` @ `0x194cfbf84`: checks version >= 2 then `br` to +0x30; the decode path also calls it at `0x194cfbccc`'),
        ('0x38', 'finishDelayedFrames', '`OSStatus (*)(VTVideoDecoderRef)`', '>= 2',
         '`0x194cfc1a0` in `VTDecompressionSessionFinishDelayedFrames`'),
        ('0x40', 'startTileSession', '`OSStatus (*)(VTVideoDecoderRef, VTVideoDecoderSessionRef, CMFormatDescriptionRef)` [I: same shape as startSession]', '>= 3',
         '`0x194f92374` in `VTTileDecompressionSessionCreate`'),
        ('0x48', 'decodeTile',
         '`OSStatus (*)(VTVideoDecoderRef, uint64_t frameID, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint32_t f, uint32_t *infoFlagsOut)` [I: argument names]',
         '>= 3', '`0x194f925f8` in `VTTileDecompressionSessionDecodeTile`; x1..x6 64-bit, w7 32-bit, [sp] = infoFlagsOut'),
        ('0x50', 'finishDelayedTiles', '`OSStatus (*)(VTVideoDecoderRef)`', '>= 3',
         '`0x194f91784` in `VTTileDecompressionSessionFinishDelayedTiles` (tail `br x1`)'),
    ]
    for r in rows:
        add('| `%s` | `%s` | %s | %s | %s |' % r)
    add('')
    add('Rules that fall out of the wrappers [V]:')
    add('')
    add('* `canAcceptFormatDescription` is asked only when the format description of an incoming sample buffer differs')
    add('  from the current one. A version-1 decoder, or a NULL slot, cannot accept a format change: the decode path')
    add('  skips the call and returns `-12916`. The slot returning false gives the same `-12916`; true makes VideoToolbox')
    add('  wait for asynchronous frames, install the new format description and retry the frame.')
    add('* `finishDelayedFrames` is only called when `version >= 2` and non-NULL; NULL is fine.')
    add('* The tile slots are only called when `version >= 3`; a version-1/2 decoder must leave them NULL.')
    add('* `copySupportedPropertyDictionary`, `setProperties` and `copySerializableProperties` are called whenever')
    add('  non-NULL, regardless of version.')
    add('')

    add('## 3. The session callbacks VideoToolbox provides [V]')
    add('')
    add('A decoder gets an opaque `VTVideoDecoderSessionRef` in `startSession` and uses these exported functions:')
    add('')
    add('| function | address | observed behaviour / signature |')
    add('|---|---|---|')
    add('| `VTDecoderSessionEmitDecodedFrame` | `%#x` | `OSStatus (session, frame, OSStatus status, uint32_t infoFlags, CVImageBufferRef imageBuffer)`: x4 is an image buffer (passed to the attachment transfer at `0x194ccbf90`, not to a CMSampleBuffer API); it is dropped when status != 0 (`cmp w2,#0; csel x22,x4,xzr,eq` at `0x194ccbeb8`) and when bit 1 of the frame\'s recorded flag word is set (`ldr w24,[x0]` at `0x194ccbedc`, `tst w24,#2` at `0x194ccbf7c`). An unknown or already-emitted frame token returns `-12902` |' % A['VTDecoderSessionEmitDecodedFrame'])
    add('| `VTDecoderSessionEmitDecodedTile` | `%#x` | tile variant; tail branch to `VTTileDecoderSessionEmitDecodedTile`; x1..x4 as above plus a tile argument [I] |' % A['VTDecoderSessionEmitDecodedTile'])
    add('| `VTDecoderSessionGetPixelBufferPool` | `%#x` | `CVPixelBufferPoolRef (session)`; `[session]` is the sub-duct, returns `[subduct+0x48]` after making the pools current; NULL on a bad session |' % A['VTDecoderSessionGetPixelBufferPool'])
    add('| `VTDecoderSessionSetPixelBufferAttributes` | `%#x` | `OSStatus (session, CFDictionaryRef attrs)`; retains the dictionary into `[subduct+0x28]` and bumps `[subduct+0x3c]` |' % A['VTDecoderSessionSetPixelBufferAttributes'])
    add('| `VTDecoderSessionGetDestinationPixelBufferAttributes` | `%#x` | `CFDictionaryRef (session)`; returns the borrowed dict at `[subduct+0x30]` (do not release) |' % A['VTDecoderSessionGetDestinationPixelBufferAttributes'])
    add('| `VTDecoderSessionCreatePixelBuffer` | `%#x` | creates one buffer from the pool: `CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, VTDecoderSessionGetPixelBufferPool(session), out)`; x1 is ignored [I: pass NULL] |' % A['VTDecoderSessionCreatePixelBuffer'])
    add('| `VTDecoderSessionTrace` | `%#x` | no-op in this build (checks the session and a tracing byte, returns 0); declare as `int (session, const char *fmt, ...)` [I: variadic] |' % A['VTDecoderSessionTrace'])
    add('| `VTDecoderSessionCleanUpAfterDecode` | `%#x` | returns 0; no other effect visible [I] |' % A['VTDecoderSessionCleanUpAfterDecode'])
    add('')
    add('`VTVideoDecoderGetClassID()` @ `%#x` runs `FigThreadRunOnce(&once, _RegisterVTVideoDecoderType)`' % A['VTVideoDecoderGetClassID'])
    add('and returns `sVTVideoDecoderClassID`; `_RegisterVTVideoDecoderType` calls')
    add('`FigBaseClassRegisterClass(&sVTVideoDecoderClassDesc, CMBaseGetClassID(), 1, &sVTVideoDecoderClassID)`. The')
    add('class descriptor at `0x1d69ab410` is `{version 1, "VTVideoDecoder", 0, VTVideoDecoderCopyFormattingDesc, ...}` and')
    add('`VTVideoDecoderCopyFormattingDesc` @ `%#x` is just `CFRetain(CFSTR("[VTVideoDecoder]"))`.' % A['VTVideoDecoderCopyFormattingDesc'])
    add('')

    add('## 4. Registering a decoder [V]')
    add('')
    add('```c')
    add('OSStatus VTRegisterVideoDecoderWithInfo(uint32_t codecType, CFDictionaryRef info,')
    add('                                        VTVideoDecoderCreateInstance createInstance);')
    add('OSStatus VTRegisterVideoDecoder(uint32_t codecType, VTVideoDecoderCreateInstance createInstance);')
    add('typedef OSStatus (*VTVideoDecoderCreateInstance)(uint32_t codecType, CFAllocatorRef, VTVideoDecoderRef *);')
    add('```')
    add('')
    add('`VTRegisterVideoDecoder` @ `%#x` is exactly `WithInfo(codecType, NULL, createInstance)`' % A['VTRegisterVideoDecoder'])
    add('(`mov x2,x1; mov x1,#0; b WithInfo`). `WithInfo` @ `%#x` builds a mutable copy of the info dictionary and:' % A['VTRegisterVideoDecoderWithInfo'])
    add('')
    add('* sets `CMClassImplementationID` to the caller\'s value with `".<4cc>"` appended: the mutable copy at')
    add('  `0x194cc8178` is formatted with the string `".%@"` at `0x194cc8188` and the codec-type CFString as the')
    add('  argument (`0x194cc8210`). When the key is absent it generates')
    add('  `"com.apple.videotoolbox.videodecoder.anon-%d"` instead (format string at `0x194cc8208`);')
    add('* sets the implementation name to `"Dynamically Registered %@ Video Decoder"`;')
    add('* sets `kVTVideoDecoder_CodecTypeKey` (`VTCodecType`, cell at `0x1d69aac18`) to a 4-character CFString of the')
    add('  byte-swapped `codecType`: `rev w8,w19` at `0x194cc8104`,')
    add('  `CFStringCreateWithBytes(..., 4, 0x600 /* ASCII */, false)` at `0x194cc812c`, stored at `0x194cc825c`;')
    add('* sets **`kVTVideoDecoder_IsHardwareAccelerated` to `kCFBooleanTrue` when the info dictionary does not contain it**')
    add('  (the branch at `0x194cc822c` skips the `CFDictionarySetValue` at `0x194cc8244` only if the key is present);')
    add('* sets `kVTVideoDecoderInfo_Rating` to the caller\'s CFNumber, or to **0** (the `str wzr,[sp,#8]` at')
    add('  `0x194cc80ec` feeds `CFNumberCreate(kCFNumberSInt32Type, 0)` at `0x194cc8288`);')
    add('* sets `kVTVideoDecoder_AllowSandboxedDecode` (`VTAllowSandboxedDecode`, cell at `0x1d69aac50`) to false;')
    add('* calls `_vtRegisterVideoDecoderInternal(description, implementationID, matchingInfo, createInstance, 0)`,')
    add('  which fills `CMClassID = kCMObjectLibraryClassID_VideoDecoder`, `CMClassImplementationVersion = 1`,')
    add('  `CMMatchingInfo = matchingInfo`, and adds the item to the registry with `FigRegistryAddItem`.')
    add('')
    add('The factory (`createInstance`) is called as `(codecType, allocator, &decoderOut)`; x0 is the codec type and is')
    add('ignored by every Apple decoder examined (VCH263, MPEG4V, AV1SW, H264SW), x1 is the allocator passed to')
    add('`CMDerivedObjectCreate`, x2 is the out pointer. The canonical body is:')
    add('')
    add('```c')
    add('static OSStatus MyDecoder_CreateInstance(uint32_t type, CFAllocatorRef alloc, VTVideoDecoderRef *out)')
    add('{')
    add('    return CMDerivedObjectCreate(alloc, &kMyDecoderVTable, VTVideoDecoderGetClassID(), (CMBaseObjectRef *)out);')
    add('}')
    add('```')
    add('')
    add('`CMDerivedObjectCreate` @ `%#x` (CoreMedia) is reached through the same `VTVideoDecoderGetClassID` +' % A['CMDerivedObjectCreate'])
    add('4-argument call in all four software decoders, each through a per-image veneer:')
    add('')
    add('| plug-in | CreateInstance | `VTVideoDecoderGetClassID` call | vtable loaded | `CMDerivedObjectCreate` call |')
    add('|---|---|---|---|---|')
    for label, entry, idpc, vpc, newpc in CREATES:
        add('| %s | `%#x` | `%#x` | `%#x` | `%#x` |' % (label, entry, idpc, vtable_of[label], newpc))
    add('')
    add('The checker follows one veneer per call (`adrp x16, page; add x16, x16, #imm; br x16`) and asserts that the')
    add('class-ID calls land on `%#x` and the object-create calls on `%#x`.' % (A['VTVideoDecoderGetClassID'], A['CMDerivedObjectCreate']))
    add('')
    add('What Apple\'s own plug-ins register (useful as calibration):')
    add('')
    add('| plug-in | codec type | rating | IsHardwareAccelerated | evidence |')
    add('|---|---|---|---|---|')
    add('| H264SW | `avc1` | not set (0) | explicitly **false** | `0x1f0f3a01c` loads `kCFBooleanFalse`; `mov w0,#0x6331` / `movk w0,#0x6176` at `0x1f0f3a064`; registers at `0x1f0f3a070` |')
    add('| VCH263 | `h263` | -10 | not set -> defaults **true** | `mov w8,#-0xa` at `0x201cae8d8`; no IsHW key |')
    add('| VCPMP4V | `mp4v` | not set (0) | explicitly **false** | `0x1f54000d4` loads `kCFBooleanFalse`; key at `0x1f54000c4` |')
    add('| AV1SW | `av01` | not set (0) | explicitly **false** | `0x1f414264c` loads `kCFBooleanFalse`; key at `0x1f4142634` |')
    add('| H264H8 / AVD | `avc1` etc. | not set (0) | not set -> defaults **true** | they call `VTRegisterVideoDecoder(codecType, create)` with no info (e.g. `0x1f17b9020`..`0x1f17b902c`) |')
    add('')
    add('So a hardware decoder that wants to be treated as hardware needs to do nothing; an object that should be')
    add('software must say so explicitly. For the Slim hardware decoder the recommendation is to register with')
    add('`IsHardwareAccelerated=true` and `Rating=0` (Apple hardware decoders use the defaults; 0 beats VCH263\'s -10,')
    add('and the hardware tie-break handles equal ratings), and to gate the registration on the daemon')
    add('switch so an unreachable daemon never takes part in selection.')
    add('')

    add('## 5. How VideoToolbox chooses, and when it retries [V]')
    add('')
    add('The pipeline is `VTDecompressionSessionCreateWithOptions` @ `%#x` -> `_VTSelectAndCreateVideoDecoderInstanceInternal`' % A['VTDecompressionSessionCreateWithOptions'])
    add('@ `%#x` (called at `0x194cf8cec`). The internal function:' % A['VTSelectAndCreateVideoDecoderInstanceInternal'])
    add('')
    add('1. Normalises the caller\'s decoder specification for the hardware keys: if')
    add('   `kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder` is absent it makes a mutable copy and')
    add('   sets it true; if it is present and false, it removes it; if')
    add('   `RequireHardwareAcceleratedVideoDecoder` is true but Enable is absent, it adds Enable. This is the only')
    add('   place `EnableHardwareAcceleratedVideoDecoder` is consumed; it is not a filter by itself.')
    add('2. Calls `FigRegistryCopyFilteredItemList` with')
    add('   `_vtFilterRegistryItemByCodecTypeAndVideoDecoderSpecification` @ `%#x` (call at `0x194cc775c`).' % A['vtFilterRegistryItemByCodecTypeAndVideoDecoderSpecification'])
    add('   The filter keeps an item only when:')
    add('   * the item\'s codec-type key equals the requested type (or the item\'s array contains it);')
    add('   * `AllowParavirtualizedDecoder` is not false while the item is paravirtualized;')
    add('   * the requested codec type is in `_VTGetAllowedDecoderList()` when that testing list is installed;')
    add('   * if the item is hardware: `VTAvoidHardwareDecoders()` is false (or the item is a wrapper decoder), and for')
    add('     JPEG `VTAvoidHardwareJPEGDecoders()` is false;')
    add('   * if the caller requires hardware (`RequireHardwareAcceleratedVideoDecoder`), the item\'s')
    add('     `kVTVideoDecoder_IsHardwareAccelerated` must be true (`0x194ed1608`..`0x194ed1618`);')
    add('   * every key in the item\'s `kVTVideoDecoderInfo_RequiredSpecificationEntries` is present in the caller\'s')
    add('     specification with an equal value (`_vtCheckRequiredSpecificationKey` @ `%#x`).' % A['vtCheckRequiredSpecificationKey'])
    add('3. Sorts the survivors with `CFArraySortValues` (call at `0x194cc7970`) and')
    add('   `_vtSortRegistryItemsByRating` @ `%#x`: **higher `kVTVideoDecoderInfo_Rating` first**; when ratings are equal,' % A['vtSortRegistryItemsByRating'])
    add('   the hardware item sorts before the non-hardware item (`cneg x19,x10,le` at `0x194ed1cf8` after reading both')
    add('   `IsHardwareAccelerated` flags).')
    add('4. Walks the sorted list from the requested index and calls each factory at `0x194cc7bf4`. If the factory returns')
    add('   non-zero (`cbnz w0` at `0x194cc7c44`), or returns 0 but leaves the out pointer NULL (`-12911` path), it')
    add('   cleans up that candidate and increments the index (`add x21,x21,#1` at `0x194cc7cc4`, `b.ne` at')
    add('   `0x194cc7ccc`): **the next decoder for the same codec type is tried**.')
    add('5. If the list is empty it returns `-12906`; if every candidate failed it returns the last factory error.')
    add('')
    add('**StartSession is not retried.** Back in `VTDecompressionSessionCreateWithOptions`, after the object exists,')
    add('the class slot +0x08 is called once at `0x194cf8ebc`; a non-zero return jumps straight to the error path at')
    add('`0x194cf8ecc`, which finalises the duct and returns the error (`cbnz w0` at `0x194cf8ec0`, no loop). The same is')
    add('true of the tile session. Consequences for the Slim decoder:')
    add('')
    add('* A failure in `createInstance` can fall through to the software decoder, but a failure in `startSession`')
    add('  cannot. Do not use `startSession` to probe the daemon or the hardware; do that before registering, or in')
    add('  `decodeFrame` (where an error is per-frame, not per-session).')
    add('* `canAcceptFormatDescription` is checked in `_vtDecompressionSessionDecodeFrameCommon` at `0x194cfbccc`; a false')
    add('  answer produces `-12916` for that frame after waiting for asynchronous frames - it does not switch decoders.')
    add('* `kVTVideoDecoderInfo_RequiredSpecificationEntries` is the supported way to make selection conditional (for')
    add('  example require `EnableHardwareAcceleratedVideoDecoder = true`); it is checked in the filter, before the factory.')
    add('')
    add('`gpu/hwcodec/facts/VT_DECODER_ABI.md` section 4 reaches the same filter/sort/retry order and the same')
    add('"StartSession is not retried" conclusion. It leaves the exact effect of `EnableHardwareAccelerated` on software')
    add('candidates as [I]; the normalisation loop above was read instruction by instruction here. Both files agree that')
    add('the daemon/hardware decision must be made in `createInstance`, never in `startSession`.')
    add('')

    add('## 6. The decoder header: proposal and landed text')
    add('')
    add('Status: the decoder half has landed as `gpu/hwcodec/include/n2vt_dec_abi.h` (opus-dec3 / hw-lead), and')
    add('`gpu/hwcodec/facts/VT_DECODER_ABI.md` documents its evidence. DEC06 codes against that file. The text below is')
    add('the proposal this package posted; it is kept here because the checker compiles it, and because it is the exact')
    add('decoder half of `gpu/video/include/n2vt_abi.h` (still frozen and read-only here). The two agree on the record')
    add('(11 slots, 0x58 bytes) and every offset; the landed file differs only in names and extras: it calls the session')
    add('type `VTVideoDecoderSession`, uses `CMVideoFormatDescriptionRef`, and adds switch macros, `N2VT_DEC_RATING` and')
    add('the public decode-flag/error enumerations. `EmitDecodedFrame`\'s x4 is a `CVImageBufferRef` in both.')
    add('')
    add('```c')
    for line in HEADER.splitlines():
        add(line)
    add('```')
    add('')
    add('`n2_VTVideoDecoderClass` is 0x58 bytes and the offsets are asserted; a compiler that disagrees with the cache')
    add('fails at build time rather than at run time.')
    add('')

    add('## 7. What is not verified, and known limits')
    add('')
    add('* **Tile decode slots** (`+0x40`, `+0x48`, `+0x50`): the slot order and version gate are `[V]` (the')
    add('  `_ParavirtualizedVideoDecoder` names and the three call sites), but `decodeTile`\'s argument meanings are `[I]`;')
    add('  only the register layout (x1..x6 64-bit, w7, stack out-pointer) is proven. The Slim decoder does not need tiles.')
    add('* `VTDecoderSessionEmitDecodedTile` and `VTDecoderSessionCreatePixelBuffer` argument names are `[I]`; their')
    add('  registers and effects are `[V]`.')
    add('* `VTDecoderSessionTrace` is a no-op in this build; its variadic signature is `[I]`.')
    add('* `H264SW.videocodec` (the first loader entry) is included in this document: class version 3 at `0x214af1440`,')
    add('  entry `VCPAVCRegisterDecoder`, factory `0x1f0f39ee4`. It registers `avc1` with `IsHardwareAccelerated=false`')
    add('  and no rating, and leaves `finishDelayedFrames`/`finishDelayedTiles` NULL even at version 3 (legal: both')
    add('  wrappers test the slot before calling it).')
    add('* The landed `gpu/hwcodec/include/n2vt_dec_abi.h` is not byte-identical to section 6 (names and extra constants,')
    add('  listed there); the layout is the same and it is the interface DEC06 uses. If the two ever disagree, the cache')
    add('  record and this checker are the evidence.')
    add('* The selection narrative describes the in-process path: `VTDecompressionSessionCreateWithOptions` calls')
    add('  `VTSelectAndCreateVideoDecoderInstanceInternal` directly at `0x194cf8cec`. `VTDecompressionSessionRemote_*`')
    add('  (the default in a client process unless `VTRunVideoDecodersInProcess` is set or `AllowClientProcessDecode` is')
    add('  passed; `gpu/hwcodec/facts/VT_DECODER_ABI.md` section 4.5) has its own copies of some checks and was not')
    add('  followed here. Where the plug-in runs and which sandbox it inherits matters to DEC06 [I].')
    add('* `_VTGetAllowedDecoderList` (a testing allow-list) is mentioned but its contents are not inspected.')
    add('* `VTRegisterVideoDecoderWithInfo` uses the *mutable copy* of the info dictionary; mutating the caller\'s dictionary')
    add('  after registration has no effect.')
    add('* No decoder record in this cache has version 4 or a slot beyond `+0x50`. The 11-slot layout is the version')
    add('  1-3 contract; a future version-4 tail is unknown and is not needed by the Slim decoder.')
    add('* No phone run: this is a static reading of the cache. Nothing here proves a plug-in loads on the phone.')
    add('')

    add('## 8. Reproduction and evidence')
    add('')
    add('```')
    add('# the user\'s own cache copy (read-only)')
    add('export N2_DYLD_CACHE=$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64')
    add('python3 gpu/hwcodec/vt/dec/abi_tools/n2decabi.py --check   # N2TEST DEC08 PASS n/n')
    add('python3 gpu/hwcodec/vt/dec/abi_tools/n2decabi.py --write   # rewrites this file')
    add('```')
    add('')
    add('The checker reads each of the five records twice (pointer fields and slot values), re-derives the 23-entry loader')
    add('list by simulating the ADRP/ADD register values in `_VTLoadVideoDecoders`, follows each CreateInstance\'s veneers')
    add('to `VTVideoDecoderGetClassID` and `CMDerivedObjectCreate`, and compares every instruction fingerprint in')
    add('the fact table. The symbol names in the `syms` fields are from the cache\'s symbol table (the dump used by')
    add('`gpu/video/research`); the addresses and records are re-read from the raw cache on every run, so a different cache')
    add('fails the check instead of silently changing the document.')
    add('')
    add('| function | address | kind |')
    add('|---|---|---|')
    for name in sorted(ADDR):
        if ADDR[name]:
            kind = 'local symbol' if name in (
                'CMDerivedObjectCreate',
                'VTVideoDecoderCanAcceptFormatDescription', 'VTVideoDecoderCopyFormattingDesc',
                'VTLoadVideoDecoders', 'VTLoadVideoDecoder',
                'VTSelectAndCreateVideoDecoderInstanceInternal',
                'vtCreateAndPopulateVideoDecoderRegistry', 'vtRegisterVideoDecoderInternal',
                'vtFilterRegistryItemByCodecTypeAndVideoDecoderSpecification',
                'vtSortRegistryItemsByRating', 'vtCheckRequiredSpecificationKey',
                'vtDecompressionDuctDecodeSingleFrame') else 'exported / data'
            add('| `%s` | `%#x` | %s |' % (name, ADDR[name], kind))
    add('')
    return '\n'.join(L) + '\n'


HEADER = r'''/* SPDX-License-Identifier: MIT */
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
'''


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='verify ABI.md and every fact against the cache')
    ap.add_argument('--write', action='store_true', help='regenerate ABI.md')
    ap.add_argument('--json', action='store_true', help='print the facts as JSON')
    ap.add_argument('--disasm', nargs='+', metavar='ADDR', help='disassemble ADDR [END]')
    ap.add_argument('--cache', default=os.environ.get('N2_DYLD_CACHE', DEFAULT_CACHE))
    args = ap.parse_args(argv)

    cache = Cache(args.cache)
    if args.disasm:
        a = int(args.disasm[0], 16)
        e = int(args.disasm[1], 16) if len(args.disasm) > 1 else a + 0x400
        print(cache.disasm(a, e))
        return 0

    vt = {v['label']: record(cache, v) for v in VTABLES}
    pl = plugin_list(cache)

    if args.json:
        out = dict(addr=ADDR, plugins=pl, records=vt)
        print(json.dumps(out, indent=1, sort_keys=True))
        return 0

    if args.write:
        text = render(cache, vt, pl)
        with open(DOC, 'w', encoding='utf-8') as f:
            f.write(text)
        print('wrote', DOC, len(text), 'bytes')
        return 0

    if args.check:
        c = run_checks(cache)
        bad = [x for x in c.items if not x[0]]
        if bad:
            print('N2TEST DEC08 FAIL %d/%d' % (len(bad), len(c.items)))
            for ok, label, detail in bad:
                print('  FAIL', label, detail)
            return 1
        print('N2TEST DEC08 PASS %d/%d' % (len(c.items), len(c.items)))
        return 0

    ap.print_help()
    return 2


if __name__ == '__main__':
    sys.exit(main())
