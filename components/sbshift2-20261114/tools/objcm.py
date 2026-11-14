import sys,struct
sys.path.insert(0,'$NORD2_WORK/gpu/build/sbshift-20261114/tools')
from dsc import *
M=0xfffffffff
def ptr(a): return struct.unpack('<Q',read(a,8))[0]&M
def ros(cls):
    out=[]
    for a in findstr(cls):
        pat=struct.pack('<Q',a)[:5]
        for b,sz,mm,o,n in maps:
            i=mm.find(pat,o,o+sz)
            while i!=-1:
                if (i-o)%8==0:
                    ro=b+i-o-0x18; fl=struct.unpack('<I',read(ro,4))[0]
                    if fl<0x10000000 and (mm[i+5:i+8]==b'\0\0\0' or True): out.append((ro,fl))
                i=mm.find(pat,i+1,o+sz)
    return out
SELBASE=None
def methods(ml):
    if not ml: return []
    ef,cnt=struct.unpack('<II',read(ml,8)); res=[]
    if cnt>2000: return [('bad',cnt)]
    if ef&0x80000000:
        for k in range(cnt):
            e=ml+8+12*k; no,to,io=struct.unpack('<iii',read(e,12))
            name=cstr(0x18229979f+no) if ef&0x40000000 else cstr(ptr(e+no))
            res.append((name,e+8+io))
    else:
        es=ef&0xfffc
        for k in range(cnt):
            e=ml+8+es*k; n,t,i=struct.unpack('<QQQ',read(e,24)); res.append((cstr(n&M),i&M))
    return res
for cls in sys.argv[1:]:
    for ro,fl in ros(cls):
        ml=ptr(ro+0x20)
        print('%s ro %#x %s flags %#x'%(cls,ro,'META' if fl&1 else 'inst',fl))
        for n,i in methods(ml): print('   %s%s %#x'%('+' if fl&1 else '-',n,i))
