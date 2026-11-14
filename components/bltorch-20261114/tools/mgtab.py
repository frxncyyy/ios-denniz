#!/usr/bin/env python3
# mgtab.py MACHO KEY... : find the libMobileGestalt table entry for obfuscated keys; dump neighbouring words
import sys,struct
f=open(sys.argv[1],'rb').read()
segs=[];secs=[]
ncmds=struct.unpack_from('<I',f,16)[0]; off=32
for i in range(ncmds):
    cmd,sz=struct.unpack_from('<II',f,off)
    if cmd==0x19:
        name=f[off+8:off+24].rstrip(b'\0').decode(); vm,vs,fo,fs=struct.unpack_from('<QQQQ',f,off+24)
        segs.append((name,vm,vs,fo,fs))
    off+=sz
def v2o(v):
    for n,vm,vs,fo,fs in segs:
        if vm<=v<vm+fs: return fo+v-vm
def o2v(o):
    for n,vm,vs,fo,fs in segs:
        if fo<=o<fo+fs and fs: return vm+o-fo
def cstr(v):
    o=v2o(v)
    if o is None: return None
    e=f.index(b'\0',o); return f[o:e].decode(errors='replace')
for n,vm,vs,fo,fs in segs: print(n,hex(vm),hex(vs),hex(fo),hex(fs))
for key in sys.argv[2:]:
    o=f.find(key.encode()+b'\0')
    v=o2v(o); print(key,'file',hex(o),'vm',hex(v))
    # search pointers (low 36 bits match)
    for n,vm,vs,fo,fs in segs:
        if not n.startswith('__DATA') and not n.startswith('__AUTH'): continue
        for p in range(fo,fo+fs-7,8):
            q=struct.unpack_from('<Q',f,p)[0]
            if q&0xfffffffff==v&0xfffffffff and q!=0:
                print('  ref at',hex(o2v(p)),'in',n,'raw',hex(q))
                for j in range(-2,8):
                    w=struct.unpack_from('<Q',f,p+8*j)[0]; t=w&0xfffffffff
                    s=None
                    if v2o(t) is not None and 0x1000<v2o(t)<len(f):
                        try:
                            s=cstr(t)
                            if s is not None and (len(s)<3 or not s.isprintable()): s=None
                        except Exception: s=None
                    print('    [%+d] %#x %s'%(j,w,repr(s) if s else ''))
