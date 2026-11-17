#!/usr/bin/env python3
"""0436 build patch of the PREBUILT (CI) archive member wifi_normal_transport.o -- no recompilation.
The CI transport admits a continuation only if its recipe is exactly one scan command:
    cmp wN,#1 ; b.ne X ; ldrb ; cmp #3 ; b.ne X ; ldrh ; cmp #1252 ; b.ne X ; ldrb ; cmp #1 ; b.ne X
four times (nct_continue_prepare, nct_scan_return_prepare, anchor x2). The four b.ne of each cluster become NOP, nothing else:
the binder (sweet_touch.o) is the only caller and sends either that one scan or [BSS activate-control x1..2] + scan.
patch(data) -> (patched bytes, [file offsets of the 16 replaced words]); asserts pattern, context and count."""
import struct, hashlib
NOP = 0xd503201f
CI_SHA256 = 'b30d3511b15e474c8f63bb6ca516aff3ae867ed975c0615484a513b938f410dd'
def _m(w, mask, val): return (w & mask) == val
def clusters(data):
    out = []
    for i in range(0, len(data) - 44, 4):
        w = struct.unpack_from('<11I', data, i)
        if (_m(w[0], 0xfffffc1f, 0x7100041f) and _m(w[1], 0xff00001f, 0x54000001) and _m(w[2], 0xffc00000, 0x39400000) and
            _m(w[3], 0xfffffc1f, 0x71000c1f) and _m(w[4], 0xff00001f, 0x54000001) and _m(w[5], 0xffc00000, 0x79400000) and
            _m(w[6], 0xfffffc1f, 0x7113901f) and _m(w[7], 0xff00001f, 0x54000001) and _m(w[8], 0xffc00000, 0x39400000) and
            _m(w[9], 0xfffffc1f, 0x7100041f) and _m(w[10], 0xff00001f, 0x54000001)):
            tgt = {i + 4*k + 4*(((w[k] >> 5) & 0x7ffff) - (1 << 19 if (w[k] >> 23) & 1 else 0)) for k in (1, 4, 7, 10)}
            assert len(tgt) == 1, ('cluster branches differ', hex(i))       # all four leave to the same refusal label
            out.append(i)
    return out
def patch(data, check_sha=True):
    if check_sha: assert hashlib.sha256(data).hexdigest() == CI_SHA256, 'wifi_normal_transport.o is not the known CI member'
    c = clusters(data); assert len(c) == 4, ('expected 4 shape clusters', [hex(x) for x in c])
    d = bytearray(data); sites = []
    for i in c:
        for k in (1, 4, 7, 10): struct.pack_into('<I', d, i + 4*k, NOP); sites.append(i + 4*k)
    assert not clusters(bytes(d)) and sum(a != b for a, b in zip(d, data)) <= 64
    return bytes(d), sites
if __name__ == '__main__':
    import sys
    data = open(sys.argv[1], 'rb').read(); new, sites = patch(data, check_sha=len(sys.argv) < 4)
    if len(sys.argv) > 2: open(sys.argv[2], 'wb').write(new)
    print('wifi_normal_transport.o: %d words -> NOP at file offsets %s; sha %s -> %s' % (len(sites), ' '.join(hex(x) for x in sites), hashlib.sha256(data).hexdigest()[:16], hashlib.sha256(new).hexdigest()[:16]))
