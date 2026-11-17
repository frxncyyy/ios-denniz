import struct, sys
exec(open(sys.argv[0].replace('cfstr.py','common.py')).read())
for a in sys.argv[2:]:
    a = int(a, 16); o = a2o(a)
    # CFString: isa(8) flags(8) ptr(8) len(8)
    p = struct.unpack_from('<Q', data, o+16)[0]; n = struct.unpack_from('<Q', data, o+24)[0]
    try: print(hex(a), repr(cstr(ptr(p))[:80]), n)
    except Exception as e: print(hex(a), 'raw', data[o:o+40].hex())
