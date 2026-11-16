#!/usr/bin/env python3
"""Test real runtime compute API/compilers with a simulated GPU transport."""
import argparse
import hashlib
import os
import re
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser(description=__doc__)
for key in ('bridge','mali','air','profile'):p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--graphics',type=Path,help='Graphics compiler library; selects the render test (supply render AIR)')
p.add_argument('--mali-graphics',type=Path,help='Optional resource-aware graphics compiler library')
p.add_argument("--constant-air",type=Path,help="Authored function-constant render AIR; enables specialization/lifetime tests")
p.add_argument("--vertex-air",type=Path,help="Authored attribute7/Metal buffer1 render AIR; requires constant-air")
p.add_argument("--varying-air",type=Path,help="Authored float4 varying AIR; requires vertex-air and constant-air")
p.add_argument("--texture-air",type=Path,help="Authored Metal texture+sampler AIR; requires graphics and the native texture pipeline gate")
p.add_argument("--bgra-air",type=Path,help="Real varying AIR used to test format80 unblended and bounded blend pipelines")
p.add_argument('--bgra-references',type=Path,help='Reference outputs from check_bgra_air_blend.py; required with bgra-air')
p.add_argument('--compositor-metallib',type=Path,help='Optional local QuartzCore metallib; exercise typed draw snapshots with the supplied profile')
p.add_argument('--compositor-golden',type=Path,help='Optional local vs.mali/fs.mali directory; also test actual compositor PSO creation through submission')
p.add_argument('--compositor-profile',type=Path,help='Separate translator profile for compositor PSOs; legacy pipelines still use --profile')
p.add_argument('--attachment-clear-profile',type=Path,help='Exact translator profile for the qualified TAC1 pipeline')
p.add_argument('--tile-fetch-fixtures',type=Path,help='Authored SPIR-V fixture directory from check_tile_fetch_compiler.py')
p.add_argument('--tile-fetch',action='store_true',help='Exercise the opt-in tile compiler path (requires new graphics library)')
p.add_argument('--render-trace',action='store_true',help='Compile opt-in bounded UI pass/draw trace and assert its production output bounds')
p.add_argument('--compiler-trace',action='store_true',help='Exercise bounded compositor compiler phases and image-address diagnostics')
a=p.parse_args()
if a.tile_fetch and not a.tile_fetch_fixtures:p.error("tile-fetch requires authored tile-fetch-fixtures")
if a.compositor_metallib and not a.graphics:p.error('compositor-metallib requires graphics')
if a.compositor_golden and not a.compositor_metallib:p.error('compositor-golden requires compositor-metallib')
if a.compositor_profile and not a.graphics:p.error('compositor-profile requires graphics')
if a.attachment_clear_profile and not (a.graphics and a.compositor_metallib):p.error('attachment-clear-profile requires graphics and compositor-metallib')
if a.compositor_golden and not a.compositor_profile:p.error('compositor-golden requires explicit compositor-profile')
if a.compiler_trace and not a.compositor_golden:p.error('compiler-trace requires compositor-golden')
if a.bgra_air and not a.bgra_references:p.error('bgra-air requires bgra-references')
if a.varying_air and not a.vertex_air:p.error("varying-air requires vertex-air")
if a.vertex_air and not a.constant_air:p.error("vertex-air requires constant-air")
if a.texture_air and not a.graphics:p.error("texture-air requires graphics")
if a.texture_air and not (a.constant_air and a.vertex_air and a.varying_air):
    p.error("texture-air requires constant-air, vertex-air and varying-air to keep the positional host fixture inputs unambiguous")
if a.bgra_air and not (a.graphics and a.constant_air and a.vertex_air and a.varying_air and a.texture_air):
    p.error("bgra-air requires graphics, constant-air, vertex-air, varying-air and texture-air")
source=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    work=Path(tmp)
    if a.compiler_trace:
        repo=source.parents[2]
        task_header=repo/'research/tegu-xnu-buildtree-0081/xnu/osfmk/mach/task_info.h'
        task_text=task_header.read_text()
        assert '#define TASK_VM_INFO            22' in task_text
        assert '((mach_msg_type_number_t) (TASK_VM_INFO_REV2_COUNT - 4))' in task_text
        assert 'mach_vm_size_t  phys_footprint;' in task_text
        assert 'struct task_vm_info {' in task_text
        trace_test=work/'compiler-trace-memory-test.c'
        trace_test.write_text(r'''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>
typedef uint64_t U64; typedef unsigned long NSUInteger;
static int calls,mode; uint32_t mach_task_self_=9;
int32_t task_info(uint32_t task,int flavor,uint32_t *info,uint32_t *count) {
 calls++; if(task!=9||flavor!=22||*count!=38)return 90;
 if(mode==1)return 5;
 if(mode==2){*count=37;return 0;}
 uint64_t *v=(uint64_t *)info; v[2]=101;v[6]=202;v[8]=303;v[10]=404;v[18]=505;return 0;
}
#define TEGU_COMPILER_TRACE 1
#define TEGU_FRONTEND_HOST 1
#include "compiler_trace.h"
int main(void) {
 struct tegu_compiler_trace t=tegu_compiler_trace_begin();
 tegu_compiler_memory(t,"ok");mode=1;tegu_compiler_memory(t,"rpcfail");
 mode=2;tegu_compiler_memory(t,"short");t.capture=16;tegu_compiler_memory(t,"capped");
 return calls==3?0:1;
}
''')
        trace_bin=work/'compiler-trace-memory-test'
        subprocess.run(['clang','-rdynamic','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-I'+str(source),str(trace_test),'-o',str(trace_bin)],check=True)
        trace_result=subprocess.run([str(trace_bin)],check=True,capture_output=True,text=True,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        memory_rows=[row for row in trace_result.stderr.splitlines() if row.startswith('TEGU_COMPILER_MEMORY ')]
        assert memory_rows==[
            'TEGU_COMPILER_MEMORY capture=0 phase=ok rc=0 count=38 resident=101 internal=202 external=303 reusable=404 footprint=505',
            'TEGU_COMPILER_MEMORY capture=0 phase=rpcfail rc=5 count=38 valid=0',
            'TEGU_COMPILER_MEMORY capture=0 phase=short rc=0 count=37 valid=0'],memory_rows
        print('PASS compiler memory trace: rev1 fields/count, RPC failure, short reply, capture cap')
        missing_test=work/'compiler-trace-lookup-failure-test.c'
        missing_test.write_text(r'''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
typedef uint64_t U64; typedef unsigned long NSUInteger;
static int calls;
int32_t task_info(uint32_t task,int flavor,uint32_t *info,uint32_t *count){(void)task;(void)flavor;(void)info;(void)count;calls++;return 0;}
void *dlsym(void *scope,const char *name){(void)scope;return !strcmp(name,"task_info")?(void *)task_info:0;}
#define TEGU_COMPILER_TRACE 1
#define TEGU_FRONTEND_HOST 1
#include "compiler_trace.h"
int main(void){struct tegu_compiler_trace t=tegu_compiler_trace_begin();tegu_compiler_memory(t,"missing");return calls;}
''')
        missing_bin=work/'compiler-trace-lookup-failure-test'
        subprocess.run(['clang','-O1','-Wall','-Wextra','-Werror','-I'+str(source),str(missing_test),'-o',str(missing_bin)],check=True)
        missing_result=subprocess.run([str(missing_bin)],check=True,capture_output=True,text=True)
        missing_rows=[row for row in missing_result.stderr.splitlines() if row.startswith('TEGU_COMPILER_MEMORY ')]
        assert missing_rows==['TEGU_COMPILER_MEMORY capture=0 phase=missing rc=-1 count=0 valid=0'],missing_rows
        print('PASS compiler memory trace lookup failure: invalid record and no Mach RPC')
        hash_test=work/'compiler-trace-hash-test.c'
        hash_test.write_text(r'''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
typedef uint64_t U64; typedef unsigned long NSUInteger;
extern void *dlsym(void *,const char *);
#define TEGU_COMPILER_TRACE 1
#define TEGU_FRONTEND_HOST 1
#include "compiler_trace.h"
int main(void){char out[65];struct tegu_compiler_trace t=tegu_compiler_trace_begin();tegu_compiler_memory(t,"unit");
 if(tegu_compiler_trace_sha256("abc",3,out)||__builtin_strcmp(out,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))return 1;
 if(tegu_compiler_trace_sha256("",0,out)==0)return 2;
 unsigned char block[32768];for(unsigned i=0;i<sizeof(block);i++)block[i]=(unsigned char)(i*17u+3u);
 const unsigned sizes[]={55,56,63,64,65,1000,32768};
 for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++){if(tegu_compiler_trace_sha256(block,sizes[i],out))return 3;printf("HASH_TEST_BYTES=%u HASH_TEST_SHA256=%s\n",sizes[i],out);}
 tegu_compiler_hash(t,"unit","known","abc",3);
 tegu_compiler_hash(t,"unit","null",0,3);
 tegu_compiler_hash(t,"unit","oversize",block,32769);
 t.capture=16;tegu_compiler_hash(t,"capped","bad_pointer",(const void *)1,3);
 return 0;}
''')
        hash_bin=work/'compiler-trace-hash-test'
        subprocess.run(['clang','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-I'+str(source),str(hash_test),'-o',str(hash_bin)],check=True)
        hash_result=subprocess.run([str(hash_bin)],check=True,capture_output=True,text=True,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        expected_hash_rows=[]
        for size in (55,56,63,64,65,1000,32768):
            data=bytes((i*17+3)&255 for i in range(size))
            expected_hash_rows.append(f'HASH_TEST_BYTES={size} HASH_TEST_SHA256={hashlib.sha256(data).hexdigest()}')
        assert hash_result.stdout.splitlines()==expected_hash_rows,hash_result.stdout
        hash_rows=[row for row in hash_result.stderr.splitlines() if row.startswith('TEGU_COMPILER_HASH ')]
        assert len(hash_rows)==3 and all('valid=0' in row for row in hash_rows[1:]),hash_rows
        assert 'sha256='+hashlib.sha256(b'abc').hexdigest() in hash_rows[0]
        print('PASS compiler SHA-256 vectors, host hashlib cross-check, invalid input and capture cap')
        disabled_test=work/'compiler-trace-disabled-test.c'
        disabled_test.write_text(r'''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
typedef uint64_t U64; typedef unsigned long NSUInteger;
int mach_task_self_,calls;
int task_info(int task,int flavor,int *info,int *count){(void)task;(void)flavor;(void)info;(void)count;calls++;return 0;}
#include "compiler_trace.h"
int main(void){struct tegu_compiler_trace t=tegu_compiler_trace_begin();tegu_compiler_memory(t,"disabled");tegu_compiler_hash(t,"disabled","bad",(const void *)1,3);return calls;}
''')
        disabled_bin=work/'compiler-trace-disabled-test'
        subprocess.run(['clang','-O1','-Wall','-Wextra','-Werror','-I'+str(source),str(disabled_test),'-o',str(disabled_bin)],check=True)
        disabled_result=subprocess.run([str(disabled_bin)],check=True,capture_output=True,text=True)
        assert not disabled_result.stderr and not disabled_result.stdout
        print('PASS compiler memory trace disabled: no Mach RPC or output')
        arm_layout=work/'compiler-trace-arm64.c'
        arm_layout.write_text(r'''typedef unsigned long long U64; typedef unsigned long NSUInteger;
extern int snprintf(char *,unsigned long,const char *,...);
extern long write(int,const void *,unsigned long);
extern void *dlsym(void *,const char *);
#define TEGU_COMPILER_TRACE 1
#include "compiler_trace.h"
int tegu_trace_arm64_probe(void) {struct tegu_compiler_trace t=tegu_compiler_trace_begin();tegu_compiler_memory(t,"probe");tegu_compiler_hash(t,"probe","hash","abc",3);return (int)sizeof(struct tegu_task_vm_info_rev1);}
''')
        arm_object=work/'compiler-trace-arm64.o'
        subprocess.run(['clang','-target','arm64-apple-ios16.0','-ffreestanding','-Wall','-Wextra','-Werror',
            '-I'+str(source),'-c',str(arm_layout),'-o',str(arm_object)],check=True)
        arm_file=subprocess.check_output(['file',str(arm_object)],text=True)
        assert 'Mach-O 64-bit arm64 object' in arm_file,arm_file
        arm_symbols=subprocess.check_output(['llvm-nm',str(arm_object)],text=True)
        assert ' U _dlsym' in arm_symbols and ' U _task_info' not in arm_symbols and ' U _mach_task_self_' not in arm_symbols,arm_symbols
        print('PASS enabled ARM64 compiler-trace object and TASK_VM_INFO rev1 layout assertions')
    (work/'tegu-air-profile.h').write_text('static const unsigned char tegu_native_profile[]={'+','.join(map(str,a.profile.read_bytes()))+'};\n')
    if a.compositor_profile:
        (work/'tegu-compositor-profile.h').write_text('static const unsigned char tegu_compositor_profile[]={'+','.join(map(str,a.compositor_profile.read_bytes()))+'};\n')
    if a.attachment_clear_profile:
        (work/'tegu-attachment-clear-profile.h').write_text('static const unsigned char tegu_attachment_clear_profile[]={'+','.join(map(str,a.attachment_clear_profile.read_bytes()))+'};\n')
    binary=work/'runtime-test'
    # Pipeline cache unit test (hit, miss, corrupt, version change, bounds).
    cache_test=work/'pipeline-cache-test'
    subprocess.run(['clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        str(source/'pipeline_cache_host_test.c'),str(source/'pipeline_cache.c'),'-o',str(cache_test)],check=True)
    (work/'pipeline-cache-unit').mkdir()
    subprocess.run([str(cache_test),str(work/'pipeline-cache-unit')],check=True)
    # Cache entries are keyed by the compilers that produced them.
    cache_build=hashlib.sha256()
    for library in (a.bridge,a.mali,*([a.mali_graphics] if a.mali_graphics else []),*([a.graphics] if a.graphics else [])):
        cache_build.update(hashlib.sha256(library.read_bytes()).digest())
    trace_rpc=work/'compiler-trace-rpc.c'
    if a.compiler_trace:
        trace_rpc.write_text(r'''#include <stdint.h>
#include <stddef.h>
uint32_t mach_task_self_=7;
int32_t task_info(uint32_t task,int flavor,uint32_t *info,uint32_t *count) {
 if(task!=7||flavor!=22||*count<38)return 90;
 uint64_t *v=(uint64_t *)info;v[2]=100;v[6]=200;v[8]=300;v[10]=400;v[18]=500;return 0;
}
''')
    subprocess.run(['clang', '-fblocks','-pthread','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        *(['-DTEGU_COMPOSITOR_PROFILE'] if a.compositor_profile else []),
        *(['-DTEGU_ATTACHMENT_CLEAR_PROFILE'] if a.attachment_clear_profile else []),
        *(['-DTEGU_RENDER_TRACE'] if a.render_trace else []),
        *(['-DTEGU_TILE_FETCH_DEFAULT'] if a.tile_fetch else []),
        *(['-DTEGU_COMPILER_TRACE'] if a.compiler_trace else []),
        *(['-rdynamic'] if a.compiler_trace else []),
        '-DTEGU_PIPELINE_CACHE_BUILD="'+cache_build.hexdigest()+'"',
        '-fconstant-string-class=NXConstantString','-I'+str(work),str(source/('render_frontend_host_test.m' if a.graphics else 'runtime_frontend_host_test.m')),
        *([str(trace_rpc)] if a.compiler_trace else []),
        '-lobjc','-ldl',str(a.bridge.resolve()),str(a.mali.resolve()),*([str(a.mali_graphics.resolve())] if a.mali_graphics else []),*([str(a.graphics.resolve())] if a.graphics else []),'-o',str(binary)],check=True)
    (work/'pipeline-cache').mkdir()
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',TEGU_PIPELINE_CACHE_DIR=str(work/'pipeline-cache'),LD_LIBRARY_PATH=os.pathsep.join(
        [*([str(a.mali_graphics.resolve().parent)] if a.mali_graphics else []),
         str(a.bridge.resolve().parent),*([str(a.graphics.resolve().parent)] if a.graphics else []),
         str(a.mali.resolve().parent),os.environ.get('LD_LIBRARY_PATH','')]))
    if a.tile_fetch_fixtures:env["TEGU_TILE_FETCH_FIXTURES"]=str(a.tile_fetch_fixtures.resolve())
    # Different research builds can share an ELF SONAME. Verify the loader
    # actually selects the libraries supplied on the command line.
    linked=subprocess.check_output(['ldd',str(binary)],text=True,env=env)
    loaded=set()
    for row in linked.splitlines():
        path=row.strip().split(' => ')[-1].rsplit(' (',1)[0]
        if path.startswith('/'):
            loaded.add(Path(path).resolve())
    for library in (a.bridge,a.mali,*([a.mali_graphics] if a.mali_graphics else []),*([a.graphics] if a.graphics else [])):
        if library.resolve() not in loaded:
            raise RuntimeError('runtime compiler library differs from requested input: '+str(library))
    if a.bgra_references:
        env['TEGU_BGRA_REFERENCES']=str(a.bgra_references.resolve())
    runtime_args=[str(binary),str(a.air.resolve()),*([str(a.constant_air.resolve())] if a.constant_air else []),*([str(a.vertex_air.resolve())] if a.vertex_air else []),*([str(a.varying_air.resolve())] if a.varying_air else []),*([str(a.texture_air.resolve())] if a.texture_air else []),*([str(a.bgra_air.resolve())] if a.bgra_air else [])]
    try:
        run_result=subprocess.run(runtime_args,check=True,env=env,timeout=60,capture_output=a.render_trace,text=a.render_trace)
    except subprocess.CalledProcessError as error:
        if a.render_trace:
            print(error.stdout or '',end='');print(error.stderr or '',end='',file=__import__('sys').stderr)
        raise
    if a.render_trace:
        trace=run_result.stderr.splitlines()
        passes=[line for line in trace if line.startswith('TEGU_UI_PASS ') and 'phase=descriptor' in line]
        accepted=[line for line in trace if line.startswith('TEGU_UI_PASS ') and 'phase=accepted' in line]
        draws=[line for line in trace if line.startswith('TEGU_UI_DRAW ')]
        assert len(passes)==16 and len(accepted)==15 and len(draws)==32,(len(passes),len(accepted),len(draws))
        assert 'target=1 width=16 height=16 stride=64 format=70 load=2 store=1' in passes[0]
        assert any('phase=refused reason=resolve_target attachment=1' in x for x in trace)
        meta=next(line for line in trace if line.startswith('TEGU_UI_PASS_META id=0 '))
        assert 'width=16 width_known=1 height=16 height_known=1 array=1 array_known=1 samples=1 samples_known=1' in meta
        assert 'viewport=not_exposed scissor=not_exposed' in meta
        rt0=next(line for line in trace if line.startswith('TEGU_UI_PASS_ATTACHMENT id=0 slot=0 '))
        rt1=next(line for line in trace if line.startswith('TEGU_UI_PASS_ATTACHMENT id=0 slot=1 '))
        rt2=next(line for line in trace if line.startswith('TEGU_UI_PASS_ATTACHMENT id=0 slot=2 '))
        assert 'present=1 texture_known=1' in rt0
        assert 'present=1 texture_known=1 resolve=1 resolve_known=1' in rt1
        assert 'present=0 texture_known=0' in rt2
        assert 'level=0 level_known=1 slice=0 slice_known=1 depth_plane=0 depth_known=1 load=2 load_known=1 store=1 store_known=1 clear_known=1' in rt1
        assert 'clear_rgba=0.1333333,0.1333333,0.2,0.2666667' in rt1
        texture0=next(line for line in trace if line.startswith('TEGU_UI_ATTACHMENT_TEXTURE id=0 slot=0 role=target '))
        texture=next(line for line in trace if line.startswith('TEGU_UI_ATTACHMENT_TEXTURE id=0 slot=1 role=target '))
        object0=int(re.search(r'object_id=(\d+)',texture0).group(1))
        object1=int(re.search(r'object_id=(\d+)',texture).group(1))
        assert object0 and object1 and object0!=object1
        resolve=next(line for line in trace if line.startswith('TEGU_UI_ATTACHMENT_TEXTURE id=0 slot=1 role=resolve '))
        assert int(re.search(r'object_id=(\d+)',resolve).group(1))==object1
        assert 'present=1 class=1 kind=2 object_id=' in texture
        assert 'width=16 height=16 format=70 stride=64' in texture
        assert 'surface_id=509' in texture
        assert 'object=0x' not in texture and 'object=0x' not in texture0
        assert 'handle=00000000a0000001' in texture
        aux=next(line for line in trace if line.startswith('TEGU_UI_PASS_AUX id=0 '))
        assert 'depth_known=1 depth_texture=0 stencil_known=1 stencil_texture=0' in aux
        assert 'visibility_known=1 visibility=0' in aux
        assert 'variant=drawPrimitives_3arg type=3 start=1 count=1 vertex_slot=4294967295' in draws[0]
        assert 'start=32 count=32' in draws[-1]
        assert max(map(len,passes+accepted+draws))<224
        metadata=[line for line in trace if line.startswith(('TEGU_UI_PASS_META ','TEGU_UI_PASS_ATTACHMENT ','TEGU_UI_ATTACHMENT_TEXTURE '))]
        assert len(metadata)<=16*(1+8*3) and max(map(len,metadata))<320
        captured=[line for line in trace if line.startswith('TEGU_UI_RT1_CAPTURE ')]
        capture_descriptors=[line for line in captured if 'phase=descriptor' in line]
        assert len(capture_descriptors)<=16
        assert any('phase=descriptor' in line for line in captured)
        assert any('phase=refused reason=resolve_target attachment=1' in line for line in captured)
        capture_meta=next(line for line in trace if line.startswith('TEGU_UI_RT1_CAPTURE_META id=0 '))
        assert 'width=16 width_known=1 height=16 height_known=1' in capture_meta
        capture_rt1=next(line for line in trace if line.startswith('TEGU_UI_RT1_CAPTURE_ATTACHMENT id=0 slot=1 '))
        assert 'present=1 texture_known=1 resolve=1 resolve_known=1' in capture_rt1
        capture_target=next(line for line in trace if line.startswith('TEGU_UI_RT1_CAPTURE_TEXTURE id=0 slot=1 role=target '))
        capture_resolve=next(line for line in trace if line.startswith('TEGU_UI_RT1_CAPTURE_TEXTURE id=0 slot=1 role=resolve '))
        capture_id=int(re.search(r'object_id=(\d+)',capture_target).group(1))
        assert capture_id and capture_id==int(re.search(r'object_id=(\d+)',capture_resolve).group(1))
        capture_metadata=[line for line in trace if line.startswith(('TEGU_UI_RT1_CAPTURE_META ','TEGU_UI_RT1_CAPTURE_ATTACHMENT ','TEGU_UI_RT1_CAPTURE_TEXTURE '))]
        assert len(capture_metadata)<=16*(1+8*3) and max(map(len,capture_metadata))<320
        ui2_bind=[line for line in trace if line.startswith('TEGU_UI2_BIND_REFUSED ')]
        assert len(ui2_bind)==2,ui2_bind
        expected_ui2={'info':664,'request':520}
        for line in ui2_bind:
            fields=dict(word.split('=',1) for word in line.split()[1:])
            size=expected_ui2[fields['kind']]
            assert fields['id']=='0' and fields['result']=='-3' and int(fields['bytes'])==size
            assert re.fullmatch(r'0{'+str(size*2)+r'}',fields['hex']),line
        print('PASS bounded UI pass trace records both attachments before preserving RT1 refusal')
    if a.compositor_metallib:
        contract=work/'typed-plan-contract'
        subprocess.run(['clang','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined',str(source/'typed_plan_cabi_host_test.c'),
            str(a.bridge.resolve()),'-o',str(contract)],check=True)
        subprocess.run([str(contract)],check=True,env=env,timeout=60)
        subprocess.run([str(binary),str(a.air.resolve()),'--compositor-snapshot',
            str(a.compositor_metallib.resolve()),str((a.compositor_profile or a.profile).resolve())],check=True,env=env,timeout=60)
        if a.attachment_clear_profile:
            subprocess.run([str(binary),str(a.air.resolve()),'--attachment-clear-pso',
                str(a.compositor_metallib.resolve())],check=True,env=env,timeout=60)
        if a.compositor_golden:
            compositor_env=dict(env,TEGU_COMPOSITOR_GOLDEN=str(a.compositor_golden.resolve()))
            try:
                result=subprocess.run([str(binary),str(a.air.resolve()),'--compositor-pso',
                    str(a.compositor_metallib.resolve()),str((a.compositor_profile or a.profile).resolve())],check=True,env=compositor_env,timeout=60,
                    capture_output=a.compiler_trace,text=a.compiler_trace)
            except subprocess.CalledProcessError as error:
                if a.compiler_trace:
                    print(error.stdout or '',end='');print(error.stderr or '',end='',file=__import__('sys').stderr)
                raise
            # Bundle pack from the cache files the run above wrote (its
            # pipeline-cache test leaves valid entries), then a corrupt copy.
            cache_files=work/'pipeline-cache'/'frontend-test'/'com.tegu.metal.pipelines'
            pack=work/'pipeline.tpcpack'
            subprocess.run(['python3',str(source/'make_pipeline_pack.py'),'--build',cache_build.hexdigest(),'--out',str(pack),
                str(cache_files)],check=True,stdout=subprocess.DEVNULL)
            bad=bytearray(pack.read_bytes());bad[-3]^=0x41;(work/'pipeline-bad.tpcpack').write_bytes(bytes(bad))
            subprocess.run([str(binary),str(a.air.resolve()),'--pipeline-pack',str(a.compositor_metallib.resolve())],check=True,
                env=dict(env,TEGU_PIPELINE_PACK_TEST=str(pack),TEGU_PIPELINE_PACK_BAD=str(work/'pipeline-bad.tpcpack')),timeout=60)
            if a.compiler_trace:
                import re
                rows=[row for row in result.stderr.splitlines() if row.startswith('TEGU_COMPILER_')]
                assert rows and max(map(len,rows))<224
                images=[row for row in rows if row.startswith('TEGU_COMPILER_IMAGE ')]
                memories=[row for row in rows if row.startswith('TEGU_COMPILER_MEMORY ')]
                hashes=[row for row in rows if row.startswith('TEGU_COMPILER_HASH ')]
                for row in memories:print(row)
                for index in range(len(images)):
                    observed=[row.split('phase=')[1].split()[0] for row in memories
                              if row.startswith('TEGU_COMPILER_MEMORY capture='+str(index)+' ')]
                    phase_rows=[line for line in rows if line.startswith('TEGU_COMPILER_PHASE capture='+str(index)+' ')]
                    if any('phase=air_begin ' in line for line in phase_rows):
                        assert observed==['air_begin','air_end','mali_begin','mali_end'],observed
                    else:
                        assert observed==[],observed
                assert 1<=len(images)<=16
                for index,row in enumerate(images):
                    assert re.fullmatch(r'TEGU_COMPILER_IMAGE capture='+str(index)+r' lookup=1 base=0x[0-9a-f]+ anchor=0x[0-9a-f]+',row),row
                    phases=[dict(word.split('=',1) for word in line.split()[1:]) for line in rows
                            if line.startswith('TEGU_COMPILER_PHASE capture='+str(index)+' ')]
                    assert phases and phases[0]['phase']=='pipeline_begin'
                successful=[line for line in rows if 'phase=import_end result=0 ' in line]
                assert successful,'actual successful compositor import was not traced'
                for terminal in successful:
                    capture=terminal.split('capture=')[1].split()[0]
                    phases=[line.split('phase=')[1].split()[0] for line in rows
                            if line.startswith('TEGU_COMPILER_PHASE capture='+capture+' ')]
                    assert phases==['pipeline_begin','air_begin','air_end','metadata_end','typed_begin','typed_end',
                                    'mali_lock_wait','mali_begin','mali_end','package_end','import_begin','import_end'],phases
                    capture=terminal.split('capture=')[1].split()[0]
                    capture_hashes=[row for row in hashes if row.startswith('TEGU_COMPILER_HASH capture='+capture+' ')]
                    assert len(capture_hashes)==5,capture_hashes
                    parsed={}
                    for row in capture_hashes:
                        fields=dict(word.split('=',1) for word in row.split()[1:])
                        assert fields.get('valid','1')=='1' and re.fullmatch(r'[0-9a-f]{64}',fields.get('sha256','')),row
                        parsed[fields['stage']]=fields
                    assert set(parsed)=={'vertex_spirv','fragment_spirv','vertex_mali','fragment_mali','render_package'},parsed
                    repo=source.parents[2]
                    spirv_golden={'vertex_spirv':repo/'research/compositor-mrt-exact-r1/downsample.vert.spv',
                                   'fragment_spirv':repo/'research/compositor-mrt-exact-r1/downsample.frag.spv'}
                    for stage,path in spirv_golden.items():
                        expected=path.read_bytes()
                        assert int(parsed[stage]['bytes'])==len(expected)
                        assert parsed[stage]['sha256']==hashlib.sha256(expected).hexdigest(),(stage,parsed[stage])
                    for stage,filename in (('vertex_mali','vs.mali'),('fragment_mali','fs.mali')):
                        expected=a.compositor_golden.joinpath(filename).read_bytes()
                        assert int(parsed[stage]['bytes'])==len(expected)
                        assert parsed[stage]['sha256']==hashlib.sha256(expected).hexdigest(),(stage,parsed[stage])
                    assert all(0<int(parsed[s]['bytes'])<=32768 for s in parsed),parsed
                print('PASS actual compositor hashes: Mali outputs match golden bytes; all stages bounded; ordered phases')
