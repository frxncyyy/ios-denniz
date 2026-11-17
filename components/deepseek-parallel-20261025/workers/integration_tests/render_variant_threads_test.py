#!/usr/bin/env python3
"""ASan two-thread render specialized-variant publication test.

Reuses the generated real-frontend render harness prefix, then runs:
  thread A: tegu_render_generic_variant_select with distinct keys (grows cache)
  thread B: reads genericVariants[0] and the published count, as the execute
            thread does at submit time.
Rebuilds the harness source with ASan and runs it. No GPU/device work.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[4]
GPU = WORK / "gpu"
RENDER = WORK / "gpu/build/deepseek-parallel-20261025/workers/render"
HARNESS = RENDER / "vs_texture_host_test.m"
OUT = HERE / "build/render_variant_threads_test"


def main() -> int:
    if not HARNESS.exists():
        print(f"FAIL missing generated render harness {HARNESS}")
        return 2
    text = HARNESS.read_text()
    marker = "int main(int argc,char **argv){"
    if marker not in text:
        print("FAIL render harness main marker missing")
        return 2
    prefix = text.split(marker, 1)[0]
    body = r'''
#include <pthread.h>
struct variant_thread_arg {TeguRenderPipeline *pipeline; unsigned start; unsigned count;};
static volatile int variant_done;
static void *variant_grow_thread(void *raw){
 struct variant_thread_arg *a=raw;
 for(unsigned i=0;i<a->count;i++){
  TeguAIRGraphicsSpecializationPlan spec;memset(&spec,0,sizeof(spec));
  spec.version=1;spec.specialization_length=4;spec.specialization_bytes[0]=(uint8_t)(a->start+i+1);
  const struct tegu_generic_draw_shader *shader=0;const struct tegu_mali_generic_plan *compile=0;
  unsigned index=0;int runtime_spec=0;char why[160]={0};
  if(tegu_render_generic_variant_select(a->pipeline,0,&spec,0,0,&shader,&compile,&index,&runtime_spec,why,sizeof(why)))
   fprintf(stderr,"variant grow %u refused: %s\n",a->start+i,why);
 }
 __atomic_store_n(&variant_done,1,__ATOMIC_RELEASE);return 0;
}
static void *variant_read_thread(void *raw){
 struct variant_thread_arg *a=raw;uint64_t spins=0;
 while(!__atomic_load_n(&variant_done,__ATOMIC_ACQUIRE)){
  unsigned n=__atomic_load_n(&a->pipeline->genericVariantCount,__ATOMIC_ACQUIRE);
  if(n){const uint8_t *code=a->pipeline->genericVariants[0].shader.vertex_code;
   assert(code==a->pipeline->genericVariants[0].shader.vertex_code);}
  spins++;
 }
 assert(spins>0);return 0;
}
int main(int argc,char **argv){
 assert(argc>=2);
 FILE *file=fopen(argv[1],"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
 assert(size>0);void *bytes=malloc((size_t)size);assert(bytes&&fread(bytes,1,(size_t)size,file)==(size_t)size);fclose(file);
 struct fake f={0};uintptr_t b=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=b;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 id error=0;TeguLibrary *lib=[d newTeguLibraryWithBytes:bytes length:(NSUInteger)size error:&error];free(bytes);assert(lib&&!error);
 TeguFunction *vf=[lib newFunctionWithName:[NSString stringWithUTF8String:"vt_vertex"]];
 TeguFunction *ff=[lib newFunctionWithName:[NSString stringWithUTF8String:"vt_fragment"]];assert(vf&&ff);
 RenderDescriptor *pd=[[RenderDescriptor alloc] init];pd->vertex=vf;pd->fragment=ff;pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
 pd->colors=[[RenderColors alloc] init];pd->colors->colors[0]=[[RenderColor alloc] init];pd->colors->colors[0]->format=80;pd->colors->colors[0]->mask=15;
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguAIRGraphicsSpecializationPlan first;memset(&first,0,sizeof(first));first.version=1;first.specialization_length=4;first.specialization_bytes[0]=1;
const struct tegu_generic_draw_shader *shader=0;const struct tegu_mali_generic_plan *compile=0;unsigned index=0;int runtime_spec=0;char why[160]={0};
 assert(!tegu_render_generic_variant_select(pipeline,0,&first,0,0,&shader,&compile,&index,&runtime_spec,why,sizeof(why)));
 assert(pipeline->genericVariantCount>=1&&pipeline->genericVariants);
 struct variant_thread_arg grow={.pipeline=pipeline,.start=1,.count=6};
 struct variant_thread_arg read={.pipeline=pipeline};
 variant_done=0;
 pthread_t a,rr;assert(!pthread_create(&a,0,variant_grow_thread,&grow));
 assert(!pthread_create(&rr,0,variant_read_thread,&read));
 assert(!pthread_join(a,0));assert(!pthread_join(rr,0));
 assert(pipeline->genericVariantCount>=2);
 puts("PASS ASan two-thread specialized variant growth/read");
 return 0;
}
'''
    OUT.parent.mkdir(parents=True, exist_ok=True)
    src = OUT.with_suffix(".m")
    runtime = (GPU / 'metal/tegu/runtime_frontend_host_test.m').read_text()
    helper = runtime.split('NSUInteger tegu_host_constant_data_size(NSUInteger type)',1)[1]
    src.write_text(prefix + 'NSUInteger tegu_host_constant_data_size(NSUInteger type)' + helper.split('static ',1)[0] + body)
    libs = [GPU / "build/deepseek-parallel-20261025/workers/compute/cargo-target/release",
            GPU / "build/mesa-build/src/panfrost/clc"]
    base = ["clang", "-fblocks", "-pthread", "-O1", "-g",
            "-Wno-incompatible-pointer-types", "-Wno-incompatible-function-pointer-types",
            "-fconstant-string-class=NXConstantString",
            "-fsanitize=address", "-fno-omit-frame-pointer"]
    for p in (GPU / "metal/tegu", GPU / "metal", GPU / "include", GPU / "build/tegu-frontend"):
        base.append("-I" + str(p))
    sources = [str(src), str(GPU / "metal/nord2_metal_bridge.c"),
               str(GPU / "metal/nord2_metal_render.c"), str(GPU / "src/nord2_gpu_desc.c"),
               str(GPU / "src/nord2_gpu_jm.c"), "-lobjc", "-ldl"]
    for p in libs:
        base += ["-L" + str(p)]
    base += sources + ["-ltegu_air_bridge", "-lnord2_mali_runtime", "-o", str(OUT)]
    built = subprocess.run(base, capture_output=True, text=True)
    if built.returncode:
        print(built.stderr)
        return 1
    # Fixture is hand-written LLVM IR, not Apple AIR: frontend-feature test, not codegen -> m2v code path (coordinator decision a, 2026-10-28).
    env = dict(os.environ, TEGU_AIR_DIRECT="0", LD_LIBRARY_PATH=":".join(map(str, libs)),
               ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    ran = subprocess.run([str(OUT), str(RENDER / "authored/probe.air")],
                         capture_output=True, text=True, env=env, timeout=120)
    print(ran.stdout.strip())
    if ran.returncode:
        print(ran.stderr.strip())
    return ran.returncode


if __name__ == "__main__":
    raise SystemExit(main())
