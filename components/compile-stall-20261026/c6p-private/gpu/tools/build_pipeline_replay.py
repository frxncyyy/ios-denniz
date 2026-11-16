#!/usr/bin/env python3
"""Build the host pipeline replay binary: the device (NORD2_JM_TRANSPORT) frontend,
real AIR translator and Mali compiler, fake JM bridge; main() = replay of
"TPC_MISS v1 air" records into pipeline cache entries.

    pipeline-replay <metallib dir> <records> <out dir>

with TEGU_PIPELINE_CACHE_BUILD_OVERRIDE = build id of the bundle the pack is for.

The translation unit is assembled the way gpu/tests/frontend_jm_test.py does it
(same pieces of the legacy host tests, same fake bridge), from a source tree:
the tree must be the one the device bundle was built from, because render
policy (which compile path a descriptor takes) lives in render_pipeline.inc.
No sanitizers (a replay is a batch job), -O1.  Host only; one clang invocation.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

GPU = Path(__file__).resolve().parents[1]
AIR_CANDIDATES = [GPU.parent / 'research/nord2-host-air/target/release',
                  GPU / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release']
NEEDED = [b'tegu_air_pipeline_native_dispatch_tg', b'tegu_air_render_pipeline_plan_generic_specialized']


def default_air():
    """First host libtegu_air_bridge.so that defines the symbols the current frontend links (a stale host library fails to link)."""
    for d in AIR_CANDIDATES:
        f = d / 'libtegu_air_bridge.so'
        if f.exists():
            data = f.read_bytes()
            if all(n in data for n in NEEDED):
                return d
    return AIR_CANDIDATES[0]


DEFAULT_AIR = default_air()
DEFAULT_MALI = GPU / 'build/mesa-build/src/panfrost/clc'

MAIN = r'''
/* ---- pipeline replay main ---- */
#include "pipeline_replay_host_test.inc"
static int scalar_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,U64 *out,unsigned *no){
 uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));
 if(ni)memcpy(a,in,ni*8);int e=n2mb_scalar(c,sel,ni?a:0,ni,out?b:0,no);
 if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;
}
static int method_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,const void *data,NSUInteger bytes,U64 *out,unsigned *no,void *output,NSUInteger *size){
 uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));
 if(ni)memcpy(a,in,ni*8);int e=n2mb_method(c,sel,ni?a:0,ni,data,bytes,out?b:0,no,output,size);
 if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;
}
static int map_adapter(TeguConnection c,unsigned h,unsigned task,U64 *a,U64 *n,unsigned flags){uint64_t x=0,y=0;int e=n2mb_map(c,h,task,&x,&y,flags);*a=x;*n=y;return e;}
static int unmap_adapter(TeguConnection c,unsigned h,unsigned task,U64 a){return n2mb_unmap(c,h,task,a);}
int main(int argc,char **argv){
 if(argc!=4){fprintf(stderr,"usage: %s <metallib dir> <records> <out dir>\n",argv[0]);return 2;}
 const char *build=getenv("TEGU_PIPELINE_CACHE_BUILD_OVERRIDE");
 if(!build||strncmp(build,"nord2-arch9-",12)){fprintf(stderr,"TEGU_PIPELINE_CACHE_BUILD_OVERRIDE must be the device build id\n");return 2;}
 struct fake f={0};uintptr_t b=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];d->connection=b;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 uint64_t caps[4];unsigned n=4;assert(!n2mb_scalar(b,0,0,0,caps,&n));d->capabilities=caps[1];
 test_pipeline_replay(d,argv[1],argv[2],argv[3]);
 d->connection=0;return 0;
}
'''


def assemble(tree: Path, out: Path) -> Path:
    t = tree / 'metal/tegu'

    def part(path, start=None, end=None):
        s = Path(path).read_text()
        if start:
            s = s.split(start, 1)[1]
            s = start + s
        if end:
            s = s.split(end, 1)[0]
        return s
    s = '#define TEGU_AIR_BRIDGE 1\n#define TEGU_MALI_BRIDGE 1\n#define TEGU_GRAPHICS_BRIDGE 1\n#define NORD2_JM_TRANSPORT 1\n'
    s += part(t / 'frontend_host_test.m', end='static unsigned allocated,freed')
    s += part(t / 'runtime_frontend_host_test.m', 'static id strings[1024]', 'static unsigned imports,submissions')
    # frontend_jm_test.py: .split(start,1)[1].split(end,1)[0].join([start,'']) == start + middle
    s += part(t / 'render_frontend_host_test.m', '@interface RenderColor', '#include "render_encoder_host_test.inc"')
    s += part(t / 'render_encoder_host_test.inc', end='@class IOSurfaceFixture')
    s += part(tree / 'tests/nord2_metal_bridge_test.c', end='static uint64_t pipeline(uintptr_t').replace('#define COUNT 300', '#define COUNT 2000')
    s += MAIN
    s += Path(t / 'runtime_frontend_host_test.m').read_text().split('NSUInteger tegu_host_constant_data_size(NSUInteger type)', 1)[1].join(
        ['NSUInteger tegu_host_constant_data_size(NSUInteger type)', ''])
    out.mkdir(parents=True, exist_ok=True)
    tu = out / 'pipeline-replay.m'
    tu.write_text(s)
    return tu


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tree', type=Path, default=GPU, help='gpu source tree the device bundle was built from (contains metal/, src/, include/)')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--air-lib', type=Path, default=DEFAULT_AIR, help='directory with libtegu_air_bridge.so (host)')
    ap.add_argument('--mali-lib', type=Path, default=DEFAULT_MALI, help='directory with libnord2_mali_runtime.so (host)')
    ap.add_argument('--profile-dir', type=Path, default=GPU / 'build/tegu-frontend', help='directory with tegu-air-profile.h of the bundle build')
    ap.add_argument('--defines', default='', help='frontend defines of the bundle build (variant.json frontend_defines: they select compile paths)')
    ap.add_argument('--assemble-only', action='store_true')
    a = ap.parse_args()
    tree = a.tree.resolve()
    tu = assemble(tree, a.out)
    if a.assemble_only:
        print(tu)
        return
    cmd = ['nice', '-n', '10', 'clang', '-fblocks', '-pthread', '-O1', '-g', '-Wno-incompatible-pointer-types',
           '-Wno-incompatible-function-pointer-types', '-fconstant-string-class=NXConstantString', *a.defines.split()]
    cmd += ['-I' + str(p) for p in [tree / 'metal/tegu', tree / 'metal', tree / 'include', a.profile_dir.resolve(),
                                       GPU / 'build/tegu-frontend']]   # generated nord2_preload.h / nord2_mesa_pack.h (not part of any pipeline)
    cmd += [str(tu)] + [str(tree / p) for p in ['metal/nord2_metal_bridge.c', 'metal/nord2_metal_render.c', 'src/nord2_gpu_desc.c', 'src/nord2_gpu_jm.c']]
    cmd += ['-lobjc', '-ldl', '-L' + str(a.air_lib), '-L' + str(a.mali_lib), '-ltegu_air_bridge', '-lnord2_mali_runtime',
            '-Wl,-rpath,' + str(a.air_lib.resolve()) + ':' + str(a.mali_lib.resolve()), '-o', str(a.out / 'pipeline-replay')]
    subprocess.run(cmd, check=True)
    info = {'tree': str(tree), 'defines': a.defines, 'binary_sha256': sha(a.out / 'pipeline-replay'),
            'air_lib': sha(a.air_lib / 'libtegu_air_bridge.so'), 'mali_lib': sha(a.mali_lib / 'libnord2_mali_runtime.so'),
            'profile_sha256': sha(a.profile_dir / 'tegu-air-profile.h'),
            'sources': {p: sha(tree / p) for p in ['metal/tegu/pipeline_cache_frontend.inc', 'metal/tegu/render_pipeline.inc',
                                                   'metal/tegu/library_frontend.inc', 'metal/tegu/pipeline_replay_host_test.inc']}}
    (a.out / 'build.json').write_text(json.dumps(info, indent=1))
    print(json.dumps(info, indent=1))


if __name__ == '__main__':
    main()
