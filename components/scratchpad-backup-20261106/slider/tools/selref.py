import struct, sys
exec(open(sys.argv[0].replace('selref.py','common.py')).read())
want = sys.argv[2]
sr = secs[('__DATA_CONST', '__objc_selrefs')] if ('__DATA_CONST', '__objc_selrefs') in secs else secs[('__DATA', '__objc_selrefs')]
for i in range(sr[1] // 8):
    v = struct.unpack_from('<Q', data, sr[2] + 8*i)[0]
    if cstr(ptr(v)) == want:
        print('selref', hex(sr[0] + 8*i))
