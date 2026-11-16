#!/usr/bin/env python3
"""Link the native compiler/library tests with the qualified GPU frontend probe.

Stubs must be extracted from the actual donor OS with ipsw dyld tbd, including
libSystem.B.dylib and libunwind.dylib. No guessed symbol providers or flat lookup.
The resulting probe still uses explicit device creation, not system Metal.
"""
import argparse
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for key in ('out','bridge','air','profile','expected-spirv','pipeline','stubs'):
    p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--mali',type=Path,help='iOS Mali compiler static archive')
p.add_argument('--cxx-stub',type=Path,help='libc++.1.dylib TBD extracted from donor OS')
p.add_argument('--metal-base',type=Path,help='Actual donor Metal.tbd; test _MTLDevice superclass')
p.add_argument('--feature-init',action='store_true',help='Experimental framework limits/feature initialization preflight')
p.add_argument("--protocols",action="store_true",help="Experimental adoption of actual donor Metal protocols; requires --feature-init")
a=p.parse_args()
if a.protocols and not a.feature_init:p.error("--protocols requires --feature-init")
if a.feature_init and not a.metal_base:p.error('--feature-init requires --metal-base')
if bool(a.mali)!=bool(a.cxx_stub):p.error('--mali and --cxx-stub must be supplied together')
source=Path(__file__).resolve().parent
out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
stubs=a.stubs.resolve()
for name in ('libSystem.B.dylib.tbd','libunwind.dylib.tbd'):
    if not (stubs/name).is_file():p.error('missing donor stub '+name)
# Rust objects ask the linker for -lunwind; do not let it find Linux's library.
(out/'libunwind.tbd').write_bytes((stubs/'libunwind.dylib.tbd').read_bytes())
subprocess.run(['bash',str(source/'build_native_probe.sh'),str(out),str(a.pipeline.resolve())],check=True)
inputs={'tegu_air_fixture':a.air,'tegu_air_profile':a.profile,'tegu_air_expected_spirv':a.expected_spirv}
(out/'tegu-air-fixture.h').write_text('\n'.join(
    'static const unsigned char '+name+'[]={'+','.join(map(str,path.read_bytes()))+'};'
    for name,path in inputs.items())+'\n')
(out/'tegu-air-profile.h').write_text('static const unsigned char tegu_native_profile[]={'+','.join(map(str,a.profile.read_bytes()))+'};\n')
flags=['clang', '-fblocks','--target=arm64-apple-ios16.0','-ffreestanding','-O2','-Wall','-Wextra','-Werror',
       '-fno-stack-protector','-DTEGU_PIPELINE_FIXTURE','-DTEGU_AIR_BRIDGE','-DTEGU_AIR_FIXTURE','-I'+str(out)]
if a.mali:flags+=['-DTEGU_MALI_BRIDGE']
if a.metal_base:flags+=['-DTEGU_METAL_BASE']
if a.feature_init:flags+=['-DTEGU_METAL_FEATURE_INIT']
if a.protocols:flags+=['-DTEGU_METAL_PROTOCOLS']
for name in ('native_probe.c','compute_frontend.m','native_air_probe.c'):
    subprocess.run(flags+['-c',str(source/name),'-o',str(out/(Path(name).stem+'.o'))],check=True)
# Additional Foundation classes used by library enumeration/file input.
foundation=out/'Foundation.tbd'
foundation.write_text(foundation.read_text().replace('NSObject, NSError','NSObject, NSError, NSString, NSMutableArray, NSData'))
subprocess.run(['ld64.lld','-arch','arm64','-platform_version','ios','16.0','16.0',
    '-adhoc_codesign','-dead_strip','-e','_main','-L'+str(out),
    *[str(out/(name+'.o')) for name in ('native_probe','transport_probe','compute_frontend','native_air_probe')],
    str(a.bridge.resolve()),*([str(a.metal_base.resolve())] if a.metal_base else []),*([str(a.mali.resolve()),str(a.cxx_stub.resolve())] if a.mali else []),str(stubs/'libSystem.B.dylib.tbd'),str(foundation),str(out/'objc.tbd'),
    '-o',str(out/'tegu-metal-probe')],check=True)
print('Built combined compiler/library/GPU probe:',out/'tegu-metal-probe','(not device-tested)')
