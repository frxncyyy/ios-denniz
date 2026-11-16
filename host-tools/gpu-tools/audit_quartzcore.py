#!/usr/bin/env python3
"""Compile the actual iOS QuartzCore fixed-UI variants through AIR and G77.
Input Apple metallib remains local. This is compiler coverage, not pixel proof.
"""
from pathlib import Path
import argparse
import ctypes as C
import hashlib
import json
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'metal/tegu'))
import check_fixed_ui_live_variants as live

class Binding(C.Structure):
    _fields_ = [(name,C.c_uint32) for name in ['binding','kind','stage_mask','declared_bytes','input_attachment_index']] + [('reserved',C.c_uint32*3)]
class Plan(C.Structure):
    _fields_ = [(name,C.c_uint32) for name in ['version','count','reads_framebuffer','reserved']] + [('bindings',Binding*16)]

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--metallib',type=Path,required=True)
p.add_argument('--bridge',type=Path,required=True)
p.add_argument('--compiler',type=Path,required=True)
p.add_argument('--profile',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
raw=a.metallib.read_bytes()
lib=live.configure_bridge(a.bridge)
buffer=C.create_string_buffer(raw);error=C.create_string_buffer(2048)
base=lib.tegu_air_library_open(buffer,len(raw),error,len(error))
if not base:raise SystemExit(error.value.decode())
compiler=live.configure_compiler(a.compiler)
compiler.tegu_mali_compile_generic_pair.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(live.Graphics),C.POINTER(live.Graphics),C.POINTER(live.Pair),C.POINTER(Plan),C.c_void_p,C.c_size_t]
compiler.tegu_mali_compile_generic_pair.restype=C.c_int
profile=live.profile_bytes(a.profile)
result={'metallib_sha256':hashlib.sha256(raw).hexdigest(),'profile_sha256':hashlib.sha256(profile).hexdigest(),'hardware_executed':False,'variants':[]}
try:
 for serial,target_format,masked in [(s,f,False) for s in sorted(live.VARIANTS) for f in (10,70,80,115)]+[(4,f,True) for f in (10,70,80,115)]:
    row={'serial':serial,'format':target_format,'masked_blend':masked}
    try:
        blend=live.Blend(1,1,0,4,5,0,1,0,10) if masked else None
        vertex,fragment,metadata=live.translate(lib,base,profile,serial,target_format,blend)
        vs,fs,pair,plan=live.Graphics(),live.Graphics(),live.Pair(),Plan()
        vb,fb=C.create_string_buffer(vertex),C.create_string_buffer(fragment)
        error.value=b''
        rc=compiler.tegu_mali_compile_generic_pair(vb,len(vertex),fb,len(fragment),C.byref(vs),C.byref(fs),C.byref(pair),C.byref(plan),error,len(error))
        row.update(status=rc,error=error.value.decode(errors='replace'),vertex_bytes=vs.size,fragment_bytes=fs.size,
                   vertex_registers=vs.registers,fragment_registers=fs.registers,secondary_registers=pair.secondary_registers,
                   tls=[vs.tls,fs.tls],fau=[vs.fau_words,fs.fau_words],varying_bytes=pair.generic_size_B,
                   binding_count=plan.count,framebuffer_fetch=plan.reads_framebuffer)
        if not rc:
            assert vs.code and fs.code and not vs.tls and not fs.tls
            assert vs.registers<=64 and fs.registers<=64 and pair.secondary_registers<=64
            assert pair.generic_size_B<=64 and vs.fau_words<=64 and fs.fau_words<=64
        compiler.tegu_mali_graphics_free(C.byref(vs));compiler.tegu_mali_graphics_free(C.byref(fs))
    except Exception as exc:row.update(status=-1,error=str(exc))
    result['variants'].append(row);print(json.dumps(row),flush=True)
finally:lib.tegu_air_library_close(base)
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(result,indent=2)+'\n')
if any(row['status'] for row in result['variants']):raise SystemExit(1)
