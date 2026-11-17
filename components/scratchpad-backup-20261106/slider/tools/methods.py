import struct, sys, re
data = open(sys.argv[1], 'rb').read()
pat = sys.argv[2] if len(sys.argv) > 2 else 'inger'
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
o = ml[2]; end = ml[2] + ml[1]
res = []
while o < end:
    entsize, count = struct.unpack_from('<II', data, o)
    if entsize & 0x80000000 == 0 or (entsize & 0xffff) != 12 or count > 5000:
        o += 4; continue
    base = o + 8
    for i in range(count):
        e = base + i*12
        n, t, imp = struct.unpack_from('<iii', data, e)
        ea = e - ml[2] + ml[0]
        selref = ea + n
        so = a2o(selref)
        try:
            name = cstr(ptr(struct.unpack_from('<Q', data, so)[0]))
        except Exception: name = '?'
        res.append((name, ea + 8 + imp))
    o = base + count*12
    o = (o + 3) & ~3
for name, imp in res:
    if re.search(pat, name): print(hex(imp), name)
