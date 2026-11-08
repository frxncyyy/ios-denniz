#!/usr/bin/env python3
"""0277u: callservicesd (iOS 16.7.x 20H392, the binary in the rootfs mirror) - -[CSDCall unsuppressRingtoneIfNecessary]
un-suppresses the ringtone at once instead of starting an SFCoordinatedAlertRequest whose completion never reaches it on this
device (q10cj: "Starting coordinated alert request to determine ringtone un-suppression" without "...came back with bestIsMe",
so every TUCall keeps the shouldSuppressRingtone = YES its initializer sets, and callservicesd logs "Not creating interruption").
At 0x1000be708 (after "if (!self.shouldSuppressRingtone) return;"):  mov x0,x19 ; mov w2,#0 ; bl setShouldSuppressRingtone: ; b exit
Then every CodeDirectory's hash of the changed page is recomputed (ad-hoc/platform signatures are checked page by page by the
kernel; SWEET has no AMFI CMS check). usage: patch_csd.py IN OUT"""
import struct, sys, hashlib
src, dst = sys.argv[1], sys.argv[2]
b = bytearray(open(src, 'rb').read())
def segs():
    n = struct.unpack_from('<I', b, 16)[0]; off = 32; out = []; cs = None
    for _ in range(n):
        cmd, sz = struct.unpack_from('<II', b, off)
        if cmd == 0x19:
            name = bytes(b[off+8:off+24]).rstrip(b'\0'); vm, vs, fo, fs = struct.unpack_from('<QQQQ', b, off+24); out.append((name, vm, vs, fo, fs))
        if cmd == 0x1d: cs = struct.unpack_from('<II', b, off+8)
        off += sz
    return out, cs
S, cs = segs()
def fo(a):
    for name, vm, vs, f, fs in S:
        if vm <= a < vm + vs: return f + a - vm
def enc_b(frm, to, link):
    d = (to - frm) // 4
    assert -(1 << 25) <= d < (1 << 25)
    return (0x94000000 if link else 0x14000000) | (d & 0x3ffffff)
expect = {0x1000be704: 0x34000820}   # cbz w0, 0x1000be808
at = 0x1000be708
cur = struct.unpack_from('<I', b, fo(0x1000be704))[0]
assert cur == expect[0x1000be704], hex(cur)
old = struct.unpack_from('<4I', b, fo(at))
assert old == (0xd0001cc0, 0x911de000, 0xd0001cc1, 0x911e6021), [hex(x) for x in old]   # adrp/add/adrp/add of the class name
new = (0xaa1303e0, 0x52800002, enc_b(at + 8, 0x100345500, True), enc_b(at + 12, 0x1000be808, False))
struct.pack_into('<4I', b, fo(at), *new)
# code signature: SuperBlob 0xfade0cc0 with CodeDirectories 0xfade0c02
csoff, cssize = cs
magic, length, count = struct.unpack_from('>III', b, csoff)
assert magic == 0xfade0cc0
changed_page = None
for i in range(count):
    typ, boff = struct.unpack_from('>II', b, csoff + 12 + 8 * i)
    cd = csoff + boff
    if struct.unpack_from('>I', b, cd)[0] != 0xfade0c02: continue
    (m, ln, ver, flags, hashOff, identOff, nSpecial, nCode, codeLimit, hashSize, hashType, platform, pageSizeLog) = \
        struct.unpack_from('>IIIIIIIIIBBBB', b, cd)
    ps = 1 << pageSizeLog
    page = fo(at) // ps
    algo = {1: hashlib.sha1, 2: hashlib.sha256}[hashType]
    for p in sorted({fo(at) // ps, (fo(at) + 15) // ps}):
        start = p * ps; end = min(start + ps, codeLimit)
        h = algo(bytes(b[start:end])).digest()[:hashSize]
        slot = cd + hashOff + p * hashSize
        b[slot:slot + hashSize] = h
    print('CodeDirectory type %d hash %d page size %d: page %d rehashed' % (typ, hashType, ps, page))
open(dst, 'wb').write(b)
print('patched', dst)
