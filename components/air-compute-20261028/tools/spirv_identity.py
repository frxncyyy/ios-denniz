#!/usr/bin/env python3
"""SPIR-V path byte identity of two libnord2_mali_runtime builds: every .spv in DIR through
tegu_mali_compile, then tegu_mali_compile_compute_v2 (creation order), forked per case.
usage: spirv_identity.py DIR LIB_A LIB_B"""
import ctypes as C, glob, hashlib, os, sys
class MB(C.Structure): _fields_=[('data',C.c_void_p),('size',C.c_size_t)]
def run(lib,spv):
    m=C.CDLL(lib)
    for fn in ('tegu_mali_compile',):
        getattr(m,fn).argtypes=[C.c_void_p,C.c_size_t,C.POINTER(MB),C.c_void_p,C.c_size_t]
    m.tegu_mali_compile_compute_v2.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(MB),C.c_void_p,C.c_size_t]
    e=C.create_string_buffer(512);p=MB()
    rc=m.tegu_mali_compile(spv,len(spv),C.byref(p),e,512)
    if rc:
        p=MB();rc=m.tegu_mali_compile_compute_v2(spv,len(spv),None,0,C.byref(p),e,512)
    return 'rc=%d %s'%(rc,hashlib.sha256(C.string_at(p.data,p.size)).hexdigest()[:16] if not rc else e.value[:60])
def fork(lib,spv):
    r,w=os.pipe();pid=os.fork()
    if pid==0:
        os.close(r);os.dup2(os.open(os.devnull,os.O_WRONLY),2)
        try: s=run(lib,spv)
        except Exception as x: s='exc '+repr(x)
        os.write(w,s.encode());os._exit(0)
    os.close(w);d=b''
    while True:
        c=os.read(r,4096)
        if not c: break
        d+=c
    os.waitpid(pid,0);return d.decode() or 'crash'
same=diff=0
for f in sorted(glob.glob(sys.argv[1]+'/*.spv')):
    spv=open(f,'rb').read()
    a=fork(sys.argv[2],spv);b=fork(sys.argv[3],spv)
    if a==b: same+=1
    else: diff+=1;print('DIFF',f,a,b)
print('same',same,'diff',diff)
