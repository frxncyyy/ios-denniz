#!/usr/bin/env python3
"""Cross-attachment framebuffer fetch takes the pass-MRT tile job by default.

CA's fixed_frag_lph_cph has 19 of 75 framebuffer-fetch variants that write
rt0 of a two-attachment pass and read rt1 (or the mirror). A single-target
tile-fetch job cannot LD_TILE another attachment, so they fell back to memory
fetch: one GPU job per draw, no batching (C6t device: 272 batch breaks per
2 s window). Compiled MULTI_TARGET|TILE_FETCH over the pass's attachments with
only the main target written, they draw as one batched job.

Real AIR/Mali compilers, real frontend and bridge, the bridge test's fake
kernel (no GPU execution). Two captured CA corpus records (targets 80/15 +
80/0, so rt0 written only):
  a-131 reads its own rt0 (dest blend) and rt1 (inputs 0,1): before, the own
    pixel came by memory fetch, one GPU job per draw (C6t 'memfetch' breaks);
  a-216 reads only rt1 (inputs 1): before, a texel fetch of rt1's memory
    (already batched).
For each, 24 draws in a two-attachment pass, run with NORD2_METAL_PASS_MRT
unset (default: every draw routed pass-MRT, TEGU_PASS_MRT rc=0) and =0 (the
previous path). Checks: routing, a-131 24 draws -> <=2 GPU jobs (>=24 before),
a-216 not more jobs than before, every command completes without error. Reuses the CA host test's harness prelude
(gpu/build/c6f-lowering-20261026/ca_backboardd_host_test.py, read-only).
Host only: needs the host libtegu_air_bridge.so and libnord2_mali_runtime.so.
"""
import os, pathlib, re, subprocess, sys

G = pathlib.Path(__file__).resolve().parents[1]
CA = G / 'build/c6f-lowering-20261026/ca_backboardd_host_test.py'
OUT = pathlib.Path(os.environ.get('PASS_MRT_TEST_OUT', str(G / 'build/host-tests/pass-mrt-cross-fetch')))
OUT.mkdir(parents=True, exist_ok=True)
text = CA.read_text()
ns = {'__file__': str(CA), '__name__': 'ca_prelude'}
exec(compile(text.split("main = r'''", 1)[0], str(CA), 'exec'), ns)
prefix, fixed_ui, tail, TEgu = ns['prefix'], ns['fixed_ui'], ns['tail'], ns['TEgu']
helpers = text.split("main = r'''", 1)[1].split('int main(int argc,char **argv){', 1)[0]
main = helpers + r'''
static void pmx_set(struct TeguFixedUIConstantRow *rows,unsigned n,const char *name,uint8_t v){
 for(unsigned i=0;i<n;i++)if(!strcmp(rows[i].name,name)){rows[i].data[0]=v;return;}
 fprintf(stderr,"PMX missing constant %s\n",name);assert(0);
}
int main(int argc,char **argv){
 assert(argc==3);const int own=!strcmp(argv[2],"a-131");assert(own||!strcmp(argv[2],"a-216"));
 tegu_host_process_backboardd=1;
 struct fake f={0};uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 long n=0;void *b=c6f_read(argv[1],&n);id err=0;TeguLibrary *ql=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&err];free(b);assert(ql);
 struct TeguFixedUIConstantRow fs[sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0])];
 struct TeguFixedUIConstantRow vs[sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0])];
 memcpy(fs,tegu_fixed_ui_fs_constants,sizeof(fs));memcpy(vs,tegu_fixed_ui_vs_constants,sizeof(vs));
 const unsigned nf=sizeof(fs)/sizeof(fs[0]),nv=sizeof(vs)/sizeof(vs[0]);
 /* CA corpus records a-216 / a-131 (fixed_vert_lph_spc + fixed_frag_lph_cph). */
 pmx_set(fs,nf,"fc_attachment_count",2);pmx_set(fs,nf,"fc_blend_function",own?0x29:0x36);pmx_set(fs,nf,"fc_framebuffer_fetch",1);
 if(own)pmx_set(fs,nf,"fc_dest_count",1);
 pmx_set(fs,nf,"fc_has_lod_bias",1);pmx_set(fs,nf,"fc_image_count",1);pmx_set(fs,nf,"fc_memoryless_index0",2);
 pmx_set(fs,nf,"fc_plane_count0",1);pmx_set(fs,nf,"fc_texcoord_count0",1);pmx_set(fs,nf,"fc_texture_function",3);
 pmx_set(vs,nv,"fc_texcoord_count0",1);
 TeguFunction *tv=tegu_fixed_ui_test_function(ql,"fixed_vert_lph_spc",vs,nv,1);
 TeguFunction *tf=tegu_fixed_ui_test_function(ql,"fixed_frag_lph_cph",fs,nf,2);
 RenderDescriptor *pd=c6f_ca_descriptor(tv,tf,80,15,0,1);
 id error=0;TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
 fprintf(stderr,"PMX_PIPE ptr=%p draw=%u mrt=%u true=%u main=%u count=%u active=%x subpass=%x\n",p,p?p->genericDraw:0,p?p->genericMrt:0,
  p?p->genericTrueMrt:0,p?p->genericMainRt:0,p?p->genericRtCount:0,p?p->genericRtActive:0,p?p->genericFsSubpass:0);
 /* The class: two attachments, rt0 written only, the fragment reads rt1. */
 assert(p&&!error&&p->genericDraw&&p->genericMrt&&!p->genericTrueMrt&&p->genericRtCount==2&&p->genericMainRt==0&&
        p->genericRtActive==1&&(p->genericFsSubpass&3u)==(own?3u:2u));
 TeguQueue *q=[d newCommandQueue];assert(q);
 TeguBuffer *vb=[d newBufferWithLength:4096 options:0];memset([vb contents],0,4096);
 TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
 TeguBuffer *fb=[d newBufferWithLength:1024 options:0];memset([fb contents],0,1024);
 TeguSampler *samp=new_sampler(d,3);TeguTexture *img=new_sample(d,80,194,237,1,2,1);assert(samp&&img);
 TeguTexture *t0=c6f_texture(d,80,1125,2436,1,5,0x20),*t1=c6f_texture(d,80,1125,2436,1,5,0x20);assert(t0&&t1);
 const unsigned draws=24;
 unsigned before=f.submitted;
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[t0 retain];pass->color->load=1;pass->color->store=1;
 pass->second=[[PassColor alloc] init];pass->second->target=[t1 retain];pass->second->load=1;pass->second->store=1;
 pass->passWidth=1125;pass->passHeight=2436;pass->arrayLength=1;pass->sampleCount=1;
 TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
 for(unsigned k=0;k<draws;k++){
  [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
  for(unsigned bi=0;bi<4;bi++){[e setFragmentBuffer:fb offset:0 atIndex:bi];if(bi!=1)[e setVertexBuffer:fb offset:0 atIndex:bi];}
  [e setFragmentTexture:img atIndex:3];[e setFragmentSamplerState:samp atIndex:0];
  [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
 }
 [e endEncoding];
 fprintf(stderr,"PMX_ENCODED invalid=%u at=%s:%u count=%u\n",c->invalid,c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count);
 assert(!c->invalid);
 [c commit];[c waitUntilCompleted];
 fprintf(stderr,"PMX_DONE status=%lu error=%p jobs=%u draws=%u\n",(unsigned long)[c status],[c error],f.submitted-before,draws);
 assert([c status]==4&&![c error]);
 printf("PMX_JOBS %u\n",f.submitted-before);
 [pass release];[t0 release];[t1 release];[img release];[samp release];[ib release];[fb release];[vb release];[p release];[pd release];
 return 0;
}
'''
src = OUT / 'pass_mrt_cross_fetch_test.m'
src.write_text(prefix + fixed_ui + main + tail)
(OUT / 'nord2_metal_bridge.h').write_bytes((G / 'metal' / 'nord2_metal_bridge.h').read_bytes())
libs = [pathlib.Path(x) for x in os.environ.get('PASS_MRT_TEST_LIBS', '').split(':') if x] or \
       [G / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release', G / 'build/mesa-build/src/panfrost/clc']
exe = OUT / 'pass_mrt_cross_fetch_test'
cmd = ['nice', '-n', '10', 'clang', '-fblocks', '-pthread', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
       '-Wno-incompatible-pointer-types', '-Wno-incompatible-function-pointer-types', '-fconstant-string-class=NXConstantString',
       '-DTEGU_HOST_REAL_V2', '-DTEGU_INERT_PIPELINES', '-DC6F_ATTACHMENTS=2',
       # the device build's render defaults (tools/build_tegu_frontend_objects.sh): JM render-pass batching,
       # tile-first framebuffer fetch, pre-frame preload, compositor on
       '-DTEGU_JM_BATCH_DEFAULT', '-DTEGU_TILE_FETCH_DEFAULT=1', '-DNORD2_PREFRAME_DEFAULT=1', '-DTEGU_COMPOSITOR_DEFAULT_ON']
cmd += ['-I' + str(x) for x in (OUT, TEgu, G / 'metal', G / 'include', G / 'build/tegu-frontend')]
cmd += [str(src), str(G / 'metal/nord2_metal_bridge.c'), str(G / 'metal/nord2_metal_render.c'),
        str(G / 'src/nord2_gpu_desc.c'), str(G / 'src/nord2_gpu_jm.c')]
cmd += ['-lobjc', '-ldl', '-lBlocksRuntime'] + ['-L' + str(x) for x in libs] + ['-ltegu_air_bridge', '-lnord2_mali_runtime', '-o', str(exe)]
subprocess.run(cmd, check=True)
lib = str(G / 'build/native/QuartzCore.metallib')
results = {}
for case, label, value in (('a-131', 'default', None), ('a-131', 'off', '0'), ('a-216', 'default', None), ('a-216', 'off', '0')):
    env = dict(os.environ, LD_LIBRARY_PATH=':'.join(map(str, libs)), ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    env.pop('NORD2_METAL_PASS_MRT', None)
    env.pop('NORD2_METAL_TRACE', None)
    if value is not None:
        env['NORD2_METAL_PASS_MRT'] = value
    r = subprocess.run(['nice', '-n', '10', str(exe), lib, case], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    err = r.stderr.decode('utf-8', 'replace')
    for line in err.splitlines():
        if line.startswith(('PMX', 'TEGU_PASS_MRT', 'TEGU_COMMAND_FAILED', 'TEGU_DRAW_SILENT', 'TEGU_GENERIC_DRAW_REFUSED',
                            'TEGU_AIR_REFUSED', 'TEGU_PASS_REFUSED')) or 'Assertion' in line or 'ERROR' in line:
            print(case, label, line)
    m = re.search(r'PMX_JOBS (\d+)', r.stdout.decode())
    results[(case, label)] = (r.returncode, int(m.group(1)) if m else -1, err)
ok = True
summary = []
for case in ('a-131', 'a-216'):
    rc, jobs, err = results[(case, 'default')]
    rc0, jobs0, err0 = results[(case, 'off')]
    routed = [l for l in err.splitlines() if l.startswith('TEGU_PASS_MRT ')]
    # every draw (trace capped at 24) routed, compiled, rt0 among the written outputs
    if rc or len(routed) != 24 or any(' rc=0 ' not in l or ' rts=2' not in l or not (int(l.split('written=')[1].split()[0], 16) & 1) for l in routed):
        print('FAIL', case, 'default: not every draw took the pass-MRT tile job', rc, routed[:2]); ok = False
    for e_, lab in ((err, 'default'), (err0, 'off')):
        if 'TEGU_COMMAND_FAILED' in e_ or 'TEGU_DRAW_SILENT' in e_ or 'TEGU_GENERIC_DRAW_REFUSED' in e_:
            print('FAIL', case, lab, 'command/draw failure'); ok = False
    if rc0 or 'TEGU_PASS_MRT ' in err0:
        print('FAIL', case, 'off: NORD2_METAL_PASS_MRT=0 still routed pass-MRT or failed', rc0); ok = False
    if case == 'a-131' and not (jobs0 >= 24 and 0 < jobs <= 2):
        print('FAIL a-131 job counts: default %d, memory-fetch %d (24 draws)' % (jobs, jobs0)); ok = False
    if case == 'a-216' and not (0 < jobs <= jobs0):
        print('FAIL a-216 job counts: default %d, before %d' % (jobs, jobs0)); ok = False
    summary.append('%s %d->%d jobs' % (case, jobs0, jobs))
print(('PASS' if ok else 'FAIL'), 'cross-attachment framebuffer fetch takes the pass-MRT tile job (24 draws each, before->after):', ', '.join(summary))
sys.exit(0 if ok else 1)
