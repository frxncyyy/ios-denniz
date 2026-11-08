#!/usr/bin/env python3
"""0283 (on top of 0277u): callservicesd never asks BackBoard for a proximity detection mode (always 0, "none").
The Nord2 has no proximity sensor iOS can read: in a call on the receiver callservicesd asked for mode 3 ("active"), backboardd
then put the digitizer in faceExpected mode and dropped every touch, so the phone looked frozen for the whole call (q10eb).
At 0x10001a880, in the method that logs "Asking BackBoard to set the proximity detection mode to (%u)" and calls the BackBoard
request with that mode: mov x19,x1 -> mov x19,#0. The changed page is rehashed in every CodeDirectory. usage: IN OUT"""
import struct, sys, hashlib
src, dst = sys.argv[1], sys.argv[2]
b = bytearray(open(src, 'rb').read())
assert hashlib.sha256(b).hexdigest() == '86db594cbfcad8ef9c507d6d795b6fabb1c1ce006ca38a9062cf7bd01e029aeb', 'not callservicesd.0277u'
n = struct.unpack_from('<I', b, 16)[0]; off = 32; S = []; cs = None
for _ in range(n):
    cmd, sz = struct.unpack_from('<II', b, off)
    if cmd == 0x19:
        vm, vs, f, fs = struct.unpack_from('<QQQQ', b, off + 24); S.append((vm, vs, f))
    if cmd == 0x1d: cs = struct.unpack_from('<II', b, off + 8)
    off += sz
def fo(a):
    for vm, vs, f in S:
        if vm <= a < vm + vs: return f + a - vm
at = 0x10001a880
ctx = struct.unpack_from('<3I', b, fo(at))
assert ctx[0] == 0xaa0103f3, hex(ctx[0])                       # mov x19, x1
assert struct.unpack_from('<I', b, fo(0x10001a8c4))[0] == 0x9117dc63   # add x3, x3, #0x5f7 ("Asking BackBoard ...")
struct.pack_into('<I', b, fo(at), 0xd2800013)                   # mov x19, #0
csoff, cssize = cs
magic, length, count = struct.unpack_from('>III', b, csoff); assert magic == 0xfade0cc0
for i in range(count):
    typ, boff = struct.unpack_from('>II', b, csoff + 12 + 8 * i); cd = csoff + boff
    if struct.unpack_from('>I', b, cd)[0] != 0xfade0c02: continue
    (m, ln, ver, flags, hashOff, identOff, nSpecial, nCode, codeLimit, hashSize, hashType, platform, pageSizeLog) = \
        struct.unpack_from('>IIIIIIIIIBBBB', b, cd)
    ps = 1 << pageSizeLog; p = fo(at) // ps; algo = {1: hashlib.sha1, 2: hashlib.sha256}[hashType]
    start = p * ps; end = min(start + ps, codeLimit)
    b[cd + hashOff + p * hashSize: cd + hashOff + (p + 1) * hashSize] = algo(bytes(b[start:end])).digest()[:hashSize]
    print('CodeDirectory type %d hash %d page %d rehashed' % (typ, hashType, p))
open(dst, 'wb').write(b); print('patched', dst, hashlib.sha256(b).hexdigest())
