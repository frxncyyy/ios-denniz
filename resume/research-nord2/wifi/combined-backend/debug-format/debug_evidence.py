"""Captured-only EID27 policy for future combined-stage parser integration.
No raw packet replay or debug payload/body verification is claimed.
"""
def validate_debug(*, header_words, inner_type, eid, kind, frame_bytes,
                   debug_type, debug_bytes):
    values = (inner_type, eid, kind, frame_bytes, debug_type, debug_bytes)
    if any(type(x) is not int for x in values):
        return False
    if len(header_words) != 9 or any(type(x) is not int or not 0 <= x <= 0xffffffff for x in header_words):
        return False
    raw = b''.join(x.to_bytes(4, 'little') for x in header_words)
    return (0 <= inner_type <= 65535 and eid == 0x27 and kind == 2 and
            48 <= frame_bytes <= 3840 and debug_type == 3 and
            0 <= debug_bytes <= min(1200, frame_bytes - 48) and
            int.from_bytes(raw[2:4], 'little') & 0x380f == 0x3800 and
            raw[28] == eid and int.from_bytes(raw[26:28], 'little') == inner_type)

if __name__ == '__main__':
    for inner in (0x3800, 0xe000, 0xffff, 7):
        raw = bytearray(36); raw[3] = 0x38; raw[26:28] = inner.to_bytes(2, 'little'); raw[28] = 0x27
        row = dict(header_words=[int.from_bytes(raw[n:n+4], 'little') for n in range(0,36,4)],
                   inner_type=inner, eid=0x27, kind=2, frame_bytes=48, debug_type=3, debug_bytes=0)
        assert validate_debug(**row)
        for key,value in [('eid',0x28),('kind',4),('frame_bytes',47),('debug_type',2),('debug_bytes',1),('inner_type',inner^1)]:
            bad = dict(row); bad[key] = value; assert not validate_debug(**bad)
    print('PASS captured-only EID27 parser policy; body metadata remains source-trusted, not replay proof')
