import sys,struct
sys.path.insert(0,'$NORD2_WORK/gpu/build/sbshift-20261114/tools')
from dsc import *
from xref import adrp_target
# literal 14.667 address
a=0x189abab38; i0,i1=struct.unpack('<II',read(a,8)); pg=adrp_target(i0,a)[0]; lit=pg+((i1>>10)&0xfff)*8
print('lit14.667 at',hex(lit),struct.unpack('<d',read(lit,8)))
lo,hi=0x189ea52e0,0x189ea52e0+0xc768
data=read(lo,hi-lo)
vals={}
for off in range(0,len(data)-8,8):
    if (lo+off)%8: continue
    vals.setdefault(struct.unpack_from('<d',data,off)[0],[]).append(lo+off)
for d in [x/3 for x in range(-30,90)]:
    hits=[k for k in vals if abs(k-d)<1e-9]
    if hits: print('%.4f'%d, hex(vals[hits[0]][0]), repr(hits[0]))
