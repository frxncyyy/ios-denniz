#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# locfix1: patch iOS 16.7 (20H392) /usr/libexec/locationd so it never asks CoreLocation (= itself, over the synchronous
# XPC service) for an authorization status from inside the daemon. locdiag1 (2026-11-12) showed both locationd instances
# deadlocked: a silo thread in CLEmergencyController's constructor (locationd 0x100b0323c -> 0x100eb1f50 -> 0x1006ef180)
# sits in +[CLLocationManager authorizationStatusForBundle:] -> CoreLocation+0xca6c synchronous NSXPC
# getAuthorizationStatusForBundleID:... to com.apple.locationd.synchronous, while the main thread and another thread wait in
# dispatch_sync on the silo it holds. Nothing in locationd answers again (Settings > Privacy, Subway Surfers hang).
# Patch: every call of the class methods authorizationStatusForBundle: / authorizationStatusForBundlePath: becomes
# "mov w0, #3" (kCLAuthorizationStatusAuthorizedAlways: these are Apple's own LocationBundles). The ad-hoc CodeDirectory
# (flags 0x2, SHA-256, 4K pages) is kept and only the hashes of the changed pages are recomputed; entitlements, requirements
# and every other byte stay identical (same method as payload-ring's re-signed callservicesd, which runs on the phone).
# usage: patch_locationd.py IN OUT   -> prints the patched sha256
import hashlib, struct, sys
ORIG = '75066240cfcc9792b2314bc90847811d164cf702756890078bbd42131f9ef4b9'   # /usr/libexec/locationd 20H392 (rootfs mirror)
SITES = [  # (vmaddr, original BL bytes, what)
    (0x1006ef17c, '01973294', 'CLEmergencyController ctor helper: +[CLLocationManager authorizationStatusForBundle:] (the deadlock)'),
    (0x100f8bab0, 'b4241094', '#rwl: +[CLLocationManager authorizationStatusForBundle:]'),
    (0x100f87870, '4c351094', 'CLDurianLocationMonitor: +[CLLocationManager authorizationStatusForBundlePath:]'),
]
MOV_W0_3 = bytes.fromhex('60008052')
def main():
    src, dst = sys.argv[1:3]
    d = bytearray(open(src, 'rb').read())
    h0 = hashlib.sha256(d).hexdigest()
    assert h0 == ORIG, 'input is not the 20H392 locationd: %s' % h0
    ncmds = struct.unpack_from('<I', d, 16)[0]; off = 32; cs = text = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', d, off)
        if cmd == 0x1d: cs = struct.unpack_from('<II', d, off + 8)
        if cmd == 0x19 and d[off + 8:off + 15] == b'__TEXT\0': text = struct.unpack_from('<QQQQ', d, off + 24)
        off += size
    assert cs and text and text[0] == 0x100000000 and text[2] == 0, 'unexpected layout'
    pages = set()
    for va, old, what in SITES:
        fo = va - text[0]
        assert d[fo:fo + 4].hex() == old, 'site %x is %s, expected %s (not the 20H392 locationd?)' % (va, d[fo:fo + 4].hex(), old)
        d[fo:fo + 4] = MOV_W0_3; pages.add(fo >> 12)
        print('patched %x (%s)' % (va, what))
    do = cs[0]; m, l, c = struct.unpack_from('>3I', d, do); assert m == 0xfade0cc0
    blobs = dict(struct.unpack_from('>II', d, do + 12 + 8 * i) for i in range(c))
    cdo = do + blobs[0]
    magic, cdlen, ver, flags, hoff, ioff, nspec, ncode, limit, hs, ht, pl, ps = struct.unpack_from('>9I4B', d, cdo)
    assert magic == 0xfade0c02 and flags == 2 and hs == 32 and ht == 2 and ps == 12 and limit == do, 'not the expected ad-hoc CD'
    assert 0x1000 not in blobs, 'alternate CodeDirectory present'
    for p in sorted(pages):
        h = hashlib.sha256(bytes(d[p << 12:min((p + 1) << 12, limit)])).digest()
        d[cdo + hoff + 32 * p:cdo + hoff + 32 * p + 32] = h
    for i in range(ncode):                         # full self-check
        assert d[cdo + hoff + 32 * i:cdo + hoff + 32 * i + 32] == hashlib.sha256(bytes(d[i << 12:min((i + 1) << 12, limit)])).digest(), i
    open(dst, 'wb').write(d)
    print('in  %s\nout %s (%d pages rehashed, %d checked)' % (h0, hashlib.sha256(d).hexdigest(), len(pages), ncode))
main()
