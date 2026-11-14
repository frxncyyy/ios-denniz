#!/usr/bin/env python3
# SPDX-License-Identifier: MIT -- checks sign_ent.py output: every page hash, the entitlements slot hash, LINKEDIT covers the blob
import hashlib, struct, sys, plistlib
d = open(sys.argv[1], 'rb').read()
ncmds = struct.unpack_from('<I', d, 16)[0]; off = 32; cs = le = None
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    if cmd == 0x1d: cs = struct.unpack_from('<II', d, off + 8)
    if cmd == 0x19 and d[off + 8:off + 18].rstrip(b'\0') == b'__LINKEDIT': le = struct.unpack_from('<QQQQ', d, off + 24)
    off += size
dataoff, datasize = cs; assert le[2] + le[3] == dataoff + datasize == len(d), (le, cs, len(d))
magic, length, count = struct.unpack_from('>3I', d, dataoff); assert magic == 0xfade0cc0
blobs = {t: o for t, o in (struct.unpack_from('>II', d, dataoff + 12 + 8 * i) for i in range(count))}
cdo = dataoff + blobs[0]; cd = d[cdo:]
_, cdlen, ver, flags, hoff, ioff, nspec, ncode, limit, hs, ht = struct.unpack_from('>9I2B', cd, 0)
assert ver == 0x20400 and hs == 32 and limit == dataoff
for i in range(ncode):
    want = cd[hoff + 32 * i:hoff + 32 * i + 32]; got = hashlib.sha256(d[i * 4096:min((i + 1) * 4096, limit)]).digest()
    assert want == got, 'page %d' % i
eo = dataoff + blobs[5]; m, elen = struct.unpack_from('>II', d, eo); assert m == 0xfade7171
assert cd[hoff - 32 * 5:hoff - 32 * 4] == hashlib.sha256(d[eo:eo + elen]).digest(), 'entitlements slot'
print('SIG OK pages', ncode, 'ident', cd[ioff:cd.index(b'\0', ioff)].decode(), 'entitlements', sorted(plistlib.loads(d[eo + 8:eo + elen])))
