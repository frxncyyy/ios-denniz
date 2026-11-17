import struct, glob, os, array, re
D='$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld'
maps=[]
for f in sorted(glob.glob(D+'/dyld_shared_cache_arm64*')):
    if os.path.getsize(f) < 0x400 or ':' in f or f.endswith('.symbols'): continue
    d=open(f,'rb').read(0x400)
    if not d.startswith(b'dyld_v1'): continue
    mo,mc=struct.unpack_from('<II',d,0x10)
    for i in range(mc):
        a,s,o,mx,ip=struct.unpack_from('<QQQII',d,mo+32*i)
        maps.append((a,s,o,f))
_files={}
def fdata(f):
    if f not in _files: _files[f]=open(f,'rb').read()
    return _files[f]
def v2f(a):
    for (b,s,o,f) in maps:
        if b<=a<b+s: return f,a-b+o
    return None,None
def read(a,n):
    f,o=v2f(a); return fdata(f)[o:o+n]
def cstr(a):
    f,o=v2f(a); d=fdata(f); e=d.index(b'\0',o); return d[o:e].decode(errors='replace')
def f2v(f,o):
    for (b,s,oo,ff) in maps:
        if ff==f and oo<=o<oo+s: return b+o-oo
def adrp_xrefs(f, target, lo=None, hi=None):
    """find ADRP(+ADD/LDR) pairs in file f referencing target vmaddr"""
    d=fdata(f); base=[m for m in maps if m[3]==f][0]
    b,s,o,_=base
    w=array.array('I'); w.frombytes(d[o:o+s - (s%4)])
    page=target & ~0xfff; off=target & 0xfff
    res=[]
    for i,x in enumerate(w):
        if (x & 0x9f000000)==0x90000000:
            pc=b+4*i
            imm=((x>>29)&3) | (((x>>5)&0x7ffff)<<2)
            if imm & (1<<20): imm-= (1<<21)
            if ((pc & ~0xfff) + (imm<<12)) & 0xffffffffffffffff == page:
                rd=x&31
                for j in range(1,6):
                    if i+j>=len(w): break
                    y=w[i+j]
                    if (y & 0xffc00000)==0x91000000 and ((y>>5)&31)==rd and ((y>>10)&0xfff)==off:
                        res.append(pc); break
                    if (y & 0xffc00000)==0xf9400000 and ((y>>5)&31)==rd and ((y>>10)&0xfff)*8==off:
                        res.append(pc); break
    return res

def images():
    f=D+'/dyld_shared_cache_arm64'; d=fdata(f)
    io,ic=struct.unpack_from('<II',d,0x1c0)
    out=[]
    for i in range(ic):
        a,mt,ino,po,pad=struct.unpack_from('<QQQII',d,io+32*i)
        e=d.index(b'\0',po); out.append((d[po:e].decode(),a))
    return out
def sections(hdr):
    d=read(hdr, 0x8000)
    ncmds=struct.unpack_from('<I',d,16)[0]; off=32; secs={}
    for _ in range(ncmds):
        cmd,size=struct.unpack_from('<II',d,off)
        if cmd==0x19:
            seg=d[off+8:off+24].rstrip(b'\0').decode()
            vm,vs=struct.unpack_from('<QQ',d,off+24)
            secs[(seg,'')]=(vm,vs)
            ns=struct.unpack_from('<I',d,off+64)[0]
            for i in range(ns):
                s=off+72+80*i
                sn=d[s:s+16].rstrip(b'\0').decode(); sg=d[s+16:s+32].rstrip(b'\0').decode()
                a,sz=struct.unpack_from('<QQ',d,s+32); secs[(sg,sn)]=(a,sz)
        off+=size
    return secs
def selbase():
    for p,a in images():
        if p.endswith('/libobjc.A.dylib'):
            s=sections(a); oa,_=s[('__TEXT','__objc_opt_ro')]
            v=struct.unpack('<q', read(oa+40,8))[0]
            return oa+v
def methods(hdr):
    s=sections(hdr); base=selbase()
    a,sz=s[('__TEXT','__objc_methlist')]; d=read(a,sz); o=0; res=[]
    while o+8<=sz:
        es,cnt=struct.unpack_from('<II',d,o)
        if (es & 0x80000000)==0 or (es&0xfffc)!=12 or cnt>10000 or cnt==0: o+=4; continue
        direct = bool(es & 0x40000000)
        for i in range(cnt):
            e=o+8+12*i; n,t,imp=struct.unpack_from('<iii',d,e)
            ea=a+e
            if direct: name=cstr(base+n)
            else:
                q=struct.unpack('<Q',read(ea+n,8))[0]&0xfffffffff
                if q<0x180000000: q+=0x180000000
                name=cstr(q)
            res.append((name, ea+8+imp))
        o+=8+12*cnt; o=(o+3)&~3
    return res
