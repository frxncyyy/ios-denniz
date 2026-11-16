#!/usr/bin/env python3
"""Build the reused Tegu runtime compiler interface for G77 on the host.
Only the local mutable Mesa build is modified. Not an iOS archive/device test.
"""
from pathlib import Path
import shutil, subprocess, os
root=Path(__file__).resolve().parents[1]
source=root/'build/mesa-src';build=root/'build/mesa-build';donor=root/'metal/tegu'
assert subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()=='81daea66703eeea1a232446e3a36902353fc3045'
for name in ('mali_compute_compiler.c','mali_compute_compiler.h','mali_graphics_compiler.c','mali_graphics_compiler.h','mali_graphics_resources.inc','compiler_trace_sha256.h','pipeline_format.h','nord2_target.h'):
    shutil.copyfile(donor/name,source/'src/panfrost/clc'/name)
p=source/'src/panfrost/clc/meson.build';marker='# NORD2_CALLABLE_COMPILERS_BEGIN'
s=p.read_text().split(marker)[0].rstrip();p.write_text(s+'\n\n'+marker+'''
shared_library('nord2_mali_runtime', ['mali_compute_compiler.c', 'mali_graphics_compiler.c'],
  c_args : ['-DTEGU_MALI_LIBRARY_ONLY', c_msvc_compat_args, no_override_init_args],
  link_with : [libpanfrost_compiler],
  include_directories : [inc_include, inc_src],
  dependencies : [idep_vtn, idep_nir, idep_mesautil, libpanfrost_model_dep],
  build_by_default : false)
''')
env=dict(os.environ, PATH=str(root/'build/venv/bin')+os.pathsep+os.environ['PATH'])
subprocess.run([str(root/'build/venv/bin/meson'),'setup','--reconfigure',str(build),str(source)],check=True,env=env)
subprocess.run(['ninja','-C',str(build),'-j2','src/panfrost/clc/libnord2_mali_runtime.so'],check=True,env=env)
