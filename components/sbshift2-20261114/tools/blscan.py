import sys,struct
sys.path.insert(0,'$NORD2_WORK/gpu/build/sbshift-20261114/tools')
from dsc import *
from xref import adrp_target
import img
want=set(sys.argv[1:])
# UIKitCore range
st=[a for a,n in img.imgs if n.endswith('/UIKitCore')][0]
i=[k for k,(a,n) in enumerate(img.imgs) if a==st][0]; en=img.imgs[i+1][0]
mm,fo,fn=v2f(st)
# text size from mach header segments
ncmds=struct.unpack_from('<I',mm,fo+16)[0]; p=fo+32; tsz=None
for _ in range(ncmds):
    cmd,cs=struct.unpack_from('<II',mm,p)
    if cmd==0x19 and mm[p+8:p+14]==b'__TEXT': tsz=struct.unpack_from('<Q',mm,p+32)[0]
    p+=cs
print('UIKitCore',hex(st),'text',hex(tsz),fn,file=sys.stderr)
cache={}
def stubname(t):
    if t in cache: return cache[t]
    nm=None
    try:
        i0,i1=struct.unpack('<II',read(t,8)); r0=adrp_target(i0,t)
        if r0 and (i1&0xffc00000)==0xf9400000:
            slot=r0[0]+((i1>>10)&0xfff)*8; q=struct.unpack('<Q',read(slot,8))[0]&0xfffffffff; nm=cstr(q)
    except Exception: pass
    cache[t]=nm; return nm
data=mm[fo:fo+tsz]
for off in range(0,tsz,4):
    ins=int.from_bytes(data[off:off+4],'little')
    if ins>>26 in (0x25,0x05):
        imm=ins&0x3ffffff
        if imm&(1<<25): imm-=1<<26
        t=st+off+imm*4
        if 0x18a300000<=t<0x18a500000:
            n=stubname(t)
            if n in want: print(n,hex(st+off),'bl' if ins>>26==0x25 else 'b')
