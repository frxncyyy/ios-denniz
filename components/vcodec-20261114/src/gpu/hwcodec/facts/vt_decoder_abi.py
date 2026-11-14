#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""vt_decoder_abi.py -- regenerate the evidence behind facts/VT_DECODER_ABI.md and include/n2vt_dec_abi.h.

Prints symbols, addresses, slot layouts and a few key instruction windows only (no bulk string or code dumps).
Needs the decompressed dyld cache and the symbol pickle the gpu/video ABI work used:
    RWCAM_WORK=<dir with dsc2/ and allsyms.pkl>  N2_TMP=<scratch dir>  python3 gpu/hwcodec/facts/vt_decoder_abi.py
(a 30k-symbol subset pickle of the relevant images works as allsyms.pkl and loads in < 1 s).
Reuses gpu/video/research/{dis,macho}.py read-only.
"""
import bisect
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'video', 'research'))
import dis as D          # noqa: E402  (gpu/video/research/dis.py, not the stdlib module)
import macho             # noqa: E402

VT = 0x194cc6000
VT_TEXT = 0x326000
STUB_GET_VTABLE = 0x19b71e930   # stub island entry for CMBaseObjectGetVTable as called from VideoToolbox

VTABLES = ['_kParavirtualizedVideoDecoderVTable', '_kMuxedAlphaDecoderVTable', '_kJPEGVideoDecoderVTable',
           '_kTestIPBVideoDecoderVTable', '_kVCH263VideoDecoderVTable', '_kH1JPEGVideoDecoderVTable',
           '__ZL24kMPEG4VideoDecoderVTable', '__ZN6webrtcL22WebKitVP8DecoderVTableE']


def q(a):
    return struct.unpack('<Q', D.dsc2.rd(a, 8))[0]


def addr(name):
    i = D.N.index(name)
    return D.K[i]


def short(x):
    r = q(x)
    if r == 0:
        return '0'
    v = D.ptr(x)
    n = D.exact(v)
    return n if n else hex(r)


def class_records():
    print('## 1. class records (vtable = {0, base, class})')
    for nm in VTABLES:
        try:
            vt = addr(nm)
        except ValueError:
            print('  missing', nm)
            continue
        b, c = D.ptr(vt + 8), D.ptr(vt + 16)
        print('%s @%x  [0]=%x base=%x class=%x' % (nm, vt, q(vt), b, c))
        print('  base  ver=%d size=0x%x | %s' % (q(b), q(b + 8), ', '.join(short(b + 8 * i) for i in range(2, 11))))
        print('  class ver=%d | %s' % (q(c), ', '.join('+0x%x:%s' % (8 * i, short(c + 8 * i)) for i in range(1, 11))))


def slot_callers():
    """bl CMBaseObjectGetVTable; ldr xA,[x0,#0x10] (derived class); [ldr xV,[xA]] ; ldr xF,[xA,#off]."""
    print('## 2. VideoToolbox callers of decoder-class slots (Decoder/Decompression functions only)')
    code = D.dsc2.rd(VT, VT_TEXT)
    n = len(code) // 4
    W = struct.unpack('<%dI' % n, code[:n * 4])
    res = {}
    for i, w in enumerate(W):
        if (w & 0xfc000000) != 0x94000000:
            continue
        off = w & 0x3ffffff
        if off & 0x2000000:
            off -= 0x4000000
        if VT + i * 4 + off * 4 != STUB_GET_VTABLE:
            continue
        cls = None
        guarded = False
        for j in range(i + 1, min(n, i + 14)):
            x = W[j]
            if (x & 0xffc00000) == 0xf9400000:
                rt, rn, im = x & 31, (x >> 5) & 31, ((x >> 10) & 0xfff) * 8
                if rn == 0 and im == 0x10 and cls is None:
                    cls = rt
                    continue
                if cls is not None and rn == cls:
                    if im == 0:
                        guarded = True
                        continue
                    fn = D.name(VT + i * 4).split('+')[0]
                    if re.search(r'Decod|Decompression', fn):
                        res.setdefault(im, set()).add(fn + (' (version-guarded)' if guarded else ''))
                    break
    for im in sorted(res):
        print('  +0x%02x %s' % (im, ', '.join(sorted(res[im]))))


def exports():
    print('## 3. VideoToolbox exports a decoder plug-in links against')
    macho.info('VideoToolbox.framework/VideoToolbox', True,
               r'_VTRegisterVideoDecoder|_VTVideoDecoderGet|_VTDecoderSession|_kVTVideoDecoder_IsHardwareAccelerated@'
               r'|_kVTVideoDecoderInfo_Rating@|_VTSelectAndCreateVideoDecoderInstance@')


def windows():
    print('## 4. key instruction windows')
    for title, a, e, pat in [
        ('VTLoadVideoDecoder (dlopen RTLD_LOCAL=4, dlsym, tail call)', 0x194cc807c, 0x194cc80bc, None),
        ('VTLoadVideoDecoders: the Slim entry', 0x194cc7efc, 0x194cc7f10, None),
        ('VTRegisterVideoDecoderWithInfo: ID suffix, defaults', 0x194cc8138, 0x194cc82e4, r' ; '),
        ('vtSortRegistryItemsByRating', 0x194ed1c94, 0x194ed1d24, None),
        ('select loop: factory call, error => next candidate', 0x194cc7be0, 0x194cc7cd0, None),
        ('session create: StartSession and its error path', 0x194cf8e98, 0x194cf8ef0, None),
        ('Apple H264SW registration (IsHardwareAccelerated value)', 0x1f0f3a01c, 0x1f0f3a074, r' ; |mov|movk'),
        ('Apple VCPHEVC registration', 0x1bf94da34, 0x1bf94da8c, r' ; |mov|movk'),
    ]:
        print('### %s' % title)
        for line in D.dis(a, e).splitlines():
            if 'adrp' in line:
                continue
            if pat is None or re.search(pat, line):
                print('  ' + line)


def main():
    class_records()
    slot_callers()
    exports()
    windows()
    for nm in ('_kCFBooleanTrue', '_kCFBooleanFalse'):
        try:
            print('%s @%x' % (nm, addr(nm)))
        except ValueError:
            print('%s not in this symbol pickle (full allsyms.pkl: True=0x1df247f18, False=0x1df247f20)' % nm)


if __name__ == '__main__':
    main()
