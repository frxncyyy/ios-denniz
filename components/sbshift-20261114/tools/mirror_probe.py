#!/usr/bin/env python3
"""read-only: catalog record + bytes at given offsets of one file of the HFSX mirror. usage: mirror_probe.py IMG PATH off:len ..."""
import sys, struct, hashlib, importlib.util
spec=importlib.util.spec_from_file_location('hx','$NORD2_REPO/research/nord2/display-capabilities/hfsx-read.py')
hx=importlib.util.module_from_spec(spec); spec.loader.exec_module(hx)
img=sys.argv[1]; path=sys.argv[2]
f=open(img,'rb'); import os; size=os.path.getsize(img)
def fetch(o,n): f.seek(o); return f.read(n)
r=hx.Reader(fetch,size,True)
r.read_bytes=-10**12
parts=path.strip('/').split('/'); parent=2
for i,name in enumerate(parts):
    rec=r.lookup(parent,name)
    if rec is None: print('NOTFOUND',name); sys.exit(1)
    if i<len(parts)-1: parent=hx.U32(rec,8)
print('fdtype',rec[48:56],'special',hx.U32(rec,44))
if rec[48:52]==b'hlnk':
    pd=r.lookup(2,'\x00\x00\x00\x00HFS+ Private Data'); pid=hx.U32(pd,8)
    rec=r.lookup(pid,'iNode%d'%hx.U32(rec,44)); print('resolved iNode')
print('type',hx.U16(rec,0),'len',len(rec),'flags41 %#x'%rec[41],'mode %o'%hx.U16(rec,42),'uid',hx.U32(rec,32),'gid',hx.U32(rec,36),'cnid',hx.U32(rec,8))
o=88; logical=hx.U64(rec,o); total=hx.U32(rec,o+12)
ext=[(hx.U32(rec,o+16+i*8),hx.U32(rec,o+20+i*8)) for i in range(8)]
print('logical',logical,'blocks',total,'extents',ext,'covered',sum(c for s,c in ext))
def pread(off,n):
    base=0
    for s,c in ext:
        if not c: break
        end=base+c*4096
        if base<=off<end: return fetch(s*4096+(off-base),n)
        base=end
    return None
for a in sys.argv[3:]:
    off,n=[int(x,0) for x in a.split(':')]
    b=pread(off,n); print(hex(off), b.hex() if b else 'beyond direct extents')
