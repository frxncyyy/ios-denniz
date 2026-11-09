import struct,os,mmap,bisect
WORK=os.environ.get('RWCAM_WORK','.')
C=WORK+'/dsc2/dyld_shared_cache_arm64'
def opn(p):
    fh=open(p,'rb'); 
    try: return mmap.mmap(fh.fileno(),0,access=mmap.ACCESS_READ)
    except ValueError: return b''
M=opn(C)
maps=[]  # (addr,size,data,fileoff)
def addmaps(d):
    if len(d)<0x200: return
    mo,mc=struct.unpack('<II',d[16:24])
    for i in range(mc):
        a,s,fo,mp,ip=struct.unpack('<QQQII',d[mo+i*32:mo+i*32+32]); maps.append((a,s,d,fo))
addmaps(M)
sco,scc=struct.unpack('<II',M[0x188:0x190])
for i in range(scc):
    e=M[sco+i*56:sco+i*56+56]; suf=e[24:56].split(b'\0')[0].decode()
    p=C+suf
    if os.path.exists(p) and os.path.getsize(p)>0: addmaps(opn(p))
maps.sort(key=lambda m:m[0])
def rd(a,n):
    for b,s,d,fo in maps:
        if b<=a<b+s: return bytes(d[fo+a-b:fo+a-b+n])
    return b''
def cs(a):
    b=rd(a,200); return b.split(b'\0')[0].decode('latin1') if b else None
io,ic=struct.unpack('<II',M[0x1c0:0x1c8])
images=[]
for i in range(ic):
    a,mt,ino,po,pad=struct.unpack('<QQQII',M[io+i*32:io+i*32+32])
    images.append((a,M[po:po+300].split(b'\0')[0].decode()))
def segs(base):
    h=rd(base,32); 
    if len(h)<32: return []
    ncmds,sz=struct.unpack('<II',h[16:24]); lc=rd(base+32,sz); o=0; out=[]
    for _ in range(ncmds):
        cmd,cs_=struct.unpack('<II',lc[o:o+8])
        if cmd==0x19:
            name=lc[o+8:o+24].split(b'\0')[0].decode(); vm,vs=struct.unpack('<QQ',lc[o+24:o+40]); ns=struct.unpack('<I',lc[o+64:o+68])[0]
            secs=[]
            for k in range(ns):
                so=o+72+k*80; sn=lc[so:so+16].split(b'\0')[0].decode(); sa,ss=struct.unpack('<QQ',lc[so+32:so+48]); secs.append((sn,sa,ss))
            out.append((name,vm,vs,secs))
        o+=cs_
    return out
