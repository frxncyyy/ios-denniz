#!/usr/bin/env python3
"""Real donor runtime entry points compiled for arch9; no GPU execution."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/callable-compiler-test'
LIB=ROOT/'build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so'
class Blob(C.Structure):
    _fields_=[('data',C.c_void_p),('size',C.c_size_t)]
class Graphics(C.Structure):
    _fields_=[('code',C.c_void_p),('size',C.c_size_t),('stage',C.c_uint32),('registers',C.c_uint32),('tls',C.c_uint32),('fau_words',C.c_uint32),('sysval_mask',C.c_uint32),('push_bytes',C.c_uint32),('preload',C.c_uint64)]

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    lib=C.CDLL(str(LIB))
    lib.tegu_mali_compile.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Blob),C.c_void_p,C.c_size_t]
    lib.tegu_mali_compile_graphics.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.POINTER(Graphics),C.c_void_p,C.c_size_t]
    lib.tegu_mali_free.argtypes=[C.POINTER(Blob)]
    lib.tegu_mali_graphics_free.argtypes=[C.POINTER(Graphics)]
    results={}
    for name,stage in [('callable.comp',0),('callable.vert',1),('callable.frag',2),('token.comp',0)]:
        path=OUT/(name+'.spv')
        subprocess.run(['glslangValidator','-V',str(ROOT/'tests/shaders'/name),'-o',str(path)],check=True,capture_output=True)
        data=path.read_bytes();source=C.create_string_buffer(data);error=C.create_string_buffer(4096)
        obj=Graphics() if stage else Blob()
        status=(lib.tegu_mali_compile_graphics(source,len(data),stage,C.byref(obj),error,len(error)) if stage else lib.tegu_mali_compile(source,len(data),C.byref(obj),error,len(error)))
        ptr=obj.code if stage else obj.data
        if name=='token.comp':
            assert status and not ptr and obj.size==0 and b'unlowered pointer' in error.value
            results[name]={'refused':error.value.decode()}
            continue
        assert status==0 and ptr and obj.size>0,(name,status,error.value)
        binary=C.string_at(ptr,obj.size)
        results[name]={'bytes':len(binary),'sha256':hashlib.sha256(binary).hexdigest()}
        if not stage:
            header=struct.unpack('<16I',binary[:64]);assert header[3]==0x90000000 and header[8]==1 and header[9:12]==(8,1,1)
            assert header[4] in range(1,65) and header[5]<=128 and header[12:14]==(0,0)
            results[name]['header']=header
            (OUT/'callable.tgp').write_bytes(binary)
            lib.tegu_mali_free(C.byref(obj));assert not obj.data and not obj.size
        else:
            assert obj.stage==stage and not obj.tls and obj.fau_words<=64
            results[name].update(registers=obj.registers,preload=obj.preload,sysval_mask=obj.sysval_mask)
            (OUT/(name+'.mali')).write_bytes(binary)
            lib.tegu_mali_graphics_free(C.byref(obj));assert not obj.code and not obj.size
    for data in (b'',b'\x00'*20,b'bad'):
        obj=Blob();error=C.create_string_buffer(4096)
        status=lib.tegu_mali_compile(C.create_string_buffer(data),len(data),C.byref(obj),error,len(error))
        assert status and not obj.data and not obj.size
    (OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS callable arch9 compute/vertex/fragment codegen; physical pointer and malformed inputs refused; no device execution')
if __name__=='__main__':main()
