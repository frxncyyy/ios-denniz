import struct, sys, re
data = open(sys.argv[1], 'rb').read()

# parse load commands for sections
ncmds = struct.unpack_from('<I', data, 16)[0]
off = 32; secs = {}
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', data, off)
    if cmd == 0x19:
        nsect = struct.unpack_from('<I', data, off+64)[0]
        for i in range(nsect):
            s = off + 72 + i*80
            sect = data[s:s+16].rstrip(b'\0').decode(); seg = data[s+16:s+32].rstrip(b'\0').decode()
            addr, sz, foff = struct.unpack_from('<QQI', data, s+32)
            secs[(seg, sect)] = (addr, sz, foff)
    off += size
def a2o(a):
    for (addr, sz, foff) in secs.values():
        if addr <= a < addr + sz: return a - addr + foff
    return None
def ptr(v):
    t = v & 0xfffffffff
    return t if t >= 0x100000000 else t + 0x100000000
def cstr(a):
    o = a2o(a); e = data.index(b'\0', o); return data[o:e].decode(errors='replace')
ml = secs[('__TEXT', '__objc_methlist')]
