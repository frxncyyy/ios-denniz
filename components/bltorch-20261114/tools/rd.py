#!/usr/bin/env python3
# rd.py MACHO VMADDR [n] -- read n pointers at vmaddr; decode CFString if the pointer falls into __cfstring
import sys,struct
f=open(sys.argv[1],'rb').read()
segs=[]
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
    return None
def cstr(v):
    o=v2o(v)
    if o is None: return None
    e=f.index(b'\0',o); return f[o:e].decode(errors='replace')
def cfstr(v):
    o=v2o(v)
    if o is None: return None
    isa,flags,p,l=struct.unpack_from('<QQQQ',f,o)
    p&=0xfffffffff
    return cstr(p)
a=int(sys.argv[2],16); n=int(sys.argv[3]) if len(sys.argv)>3 else 4
for i in range(n):
    o=v2o(a+8*i)
    if o is None: print(hex(a+8*i),'unmapped'); continue
    p=struct.unpack_from('<Q',f,o)[0]
    q=p & 0xfffffffff
    print(hex(a+8*i),hex(p),hex(q),repr(cfstr(q)))
