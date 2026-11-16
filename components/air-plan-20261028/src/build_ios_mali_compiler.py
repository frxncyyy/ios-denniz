#!/usr/bin/env python3
"""Cross-build the pinned native Mali compiler with Xcode's iPhoneOS SDK.

Run on macOS. Produces an iOS dylib and a combined static archive for the native
probe. No GPU execution or claim that the full shader interface is implemented.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from build_mali_compiler import PIN, OPTIONS

p=argparse.ArgumentParser(description=__doc__)
for key in ('source','build','environment','output'):
    p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--jobs',type=int,default=3)
a=p.parse_args()
source,build,environment,output=(x.resolve() for x in (a.source,a.build,a.environment,a.output))
if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()!=PIN:
    p.error('require pinned Mesa source')
if a.jobs<1:p.error('positive jobs required')
sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang'],text=True).strip()
clangxx=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang++'],text=True).strip()
output.mkdir(parents=True,exist_ok=True)
cross=output/'ios-cross.ini'
def meson_array(items):return '['+', '.join(repr(x) for x in items)+']'
common=['-target','arm64-apple-ios16.0','-isysroot',sdk]
cross.write_text('[binaries]\n'+
    '\n'.join(f'{key} = '+meson_array([compiler]+common) for key,compiler in [('c',clang),('cpp',clangxx),('objc',clang)])+
    "\nar = 'ar'\nstrip = 'strip'\npkg-config = '/usr/bin/false'\n"
    "[host_machine]\nsystem = 'darwin'\ncpu_family = 'aarch64'\ncpu = 'armv8'\nendian = 'little'\n"
    "[properties]\nneeds_exe_wrapper = true\n")
here=Path(__file__).resolve().parent
for src,dst in [('mali_compute_compiler.c','tegu_mali_compute.c'),('mali_compute_compiler.h','mali_compute_compiler.h'),('pipeline_format.h','pipeline_format.h'),('mali_graphics_compiler.c','mali_graphics_compiler.c'),('mali_graphics_compiler.h','mali_graphics_compiler.h'),('mali_graphics_resources.inc','mali_graphics_resources.inc'),('compiler_trace_sha256.h','compiler_trace_sha256.h')]:
    shutil.copyfile(here/src,source/'src/panfrost/clc'/dst)
config=source/'src/panfrost/clc/meson.build'
# Depend on the GPU model data, not libpanfrost_dep: the latter pulls in
# Linux DRM/kmod and the complete hardware interface, unavailable on iOS.
# Only the runtime compiler is needed. Do not create Mesa's native host helper
# with target-iOS dependencies or accidentally run an iOS generator on macOS.
config.write_text("""# Tegu runtime compiler only; replaces unused precompile-tool definitions.
shared_library('tegu_mali_compiler', ['tegu_mali_compute.c','mali_graphics_compiler.c'],
  c_args : ['-DTEGU_MALI_LIBRARY_ONLY'],
  link_with : [libpanfrost_compiler],
  include_directories : [inc_include, inc_src],
  dependencies : [idep_vtn, idep_nir, idep_mesautil, libpanfrost_model_dep],
  build_by_default : false)
""")
env=dict(os.environ,PATH=str(environment/'bin')+os.pathsep+os.environ['PATH'])
# No host pkg-config dependencies may bleed into the iOS target. Optional
# compression/XML/SPIR-V debugging libraries are unnecessary for this compiler.
options=OPTIONS+['-Dspirv-tools=disabled','-Dzstd=disabled','-Dzlib=disabled','-Dexpat=disabled','-Dshader-cache=disabled']
setup=[str(environment/'bin/meson'),'setup']
if (build/'build.ninja').exists():setup+=['--reconfigure']
subprocess.run(setup+[str(build),str(source),'--cross-file',str(cross)]+options,check=True,env=env)
target='src/panfrost/clc/libtegu_mali_compiler.dylib'
subprocess.run(['ninja','-C',str(build),'-j',str(a.jobs),target],check=True,env=env)
shutil.copyfile(build/target,output/'libtegu_mali_compiler.dylib')
objects=list((build/'src/panfrost/clc/libtegu_mali_compiler.dylib.p').glob('*.o'))
archives=sorted(build.rglob('*.a'))
if not objects or not archives:raise SystemExit('Missing compiler objects/static dependencies')
subprocess.run(['xcrun','libtool','-static','-o',str(output/'libtegu_mali_compiler.a'),*[str(x) for x in objects+archives]],check=True)
loads=subprocess.check_output(['otool','-L',str(output/'libtegu_mali_compiler.dylib')],text=True)
if '/opt/homebrew/' in loads or '/Cellar/' in loads:raise SystemExit('Host library leaked into iOS linkage')
(output/'linkage.txt').write_text(loads)
(output/'result.json').write_text(json.dumps(dict(mesa_commit=PIN,sdk=sdk,target='arm64-apple-ios16.0',
    source_archives=[str(x.relative_to(build)) for x in archives],native_tested=False),indent=2)+'\n')
print(loads)
