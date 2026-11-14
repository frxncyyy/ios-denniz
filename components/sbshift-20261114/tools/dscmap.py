#!/usr/bin/env python3
"""map a dyld shared cache vaddr to (subcache file, file offset); read bytes. usage: dscmap.py DIR vaddr [n]"""
import struct, sys, os, glob
def maps(d):
    out=[]
    for f in sorted(glob.glob(os.path.join(d,'dyld_shared_cache_arm64*'))):
        if f.endswith(('.a2s','.symbols')): pass
        with open(f,'rb') as fh:
            h=fh.read(0x200)
            if not h.startswith(b'dyld_v1'): continue
            mo,mc=struct.unpack_from('<II',h,0x10)
            fh.seek(mo); m=fh.read(32*mc)
            for i in range(mc):
                a,s,o,mx,ip=struct.unpack_from('<QQQII',m,32*i)
                out.append((a,s,o,f,ip))
    return out
def locate(d,va):
    for a,s,o,f,ip in maps(d):
        if a<=va<a+s: return f,o+(va-a),ip
    raise KeyError(hex(va))
if __name__=='__main__':
    d=sys.argv[1]; va=int(sys.argv[2],0); n=int(sys.argv[3]) if len(sys.argv)>3 else 8
    f,off,ip=locate(d,va)
    with open(f,'rb') as fh: fh.seek(off); b=fh.read(n)
    print(os.path.basename(f), hex(off), 'prot',ip, b.hex(), struct.unpack('<d',b[:8])[0] if n>=8 else '')
