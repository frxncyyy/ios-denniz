#!/usr/bin/env python3
"""C6r regression: every draw of a render-pass batch must run its OWN shader code.

draw_batch_locked builds the draws in descending order (k = count-1 .. 0) and
reuses the previous draw's code BO when two draws share the shader bytes.
C6r compared dr[k] with dr[k-1] but reused the code of dr[k+1], so a batch
[A, A, B] ran B's program for draw 1 (device: broken textures/icons since
C6r).  This test batches mixed-shader patterns through the production bridge
against the bridge test's transport fake and checks, per draw block, that the
SHADER_PROGRAM.binary words point at code BOs holding exactly that draw's
vertex/fragment bytes.  Host only; no device.
"""
from pathlib import Path
import os, subprocess, sys

gpu = Path(__file__).resolve().parents[1]
out = Path(os.environ.get('BATCH_REUSE_OUT', gpu / 'build/host-tests/batch-code-reuse'))
out.mkdir(parents=True, exist_ok=True)
fake = (gpu / 'tests/nord2_metal_bridge_test.c').read_text().split('static uint64_t pipeline(uintptr_t', 1)[0]
main = r'''
static uint64_t resource(uintptr_t b){uint64_t bytes=2048,out[2];unsigned n=2;assert(!n2mb_scalar(b,1,&bytes,1,out,&n)&&n==2&&out[1]==16384);return out[0];}
static unsigned char vs_b[384],fs_b[128];
static const unsigned char *code_at(struct fake *f,uint64_t va){
 for(unsigned h=1;h<=f->next;h++){struct buffer *b=&f->buffers[h];
  if(b->active&&b->data&&va>=b->va&&va<b->va+b->size)return b->data+(va-b->va);}
 return NULL;
}
int main(void){
 memcpy(vs_b,n2_preload_vs,sizeof(vs_b));vs_b[sizeof(vs_b)-1]^=0x5a; /* tail padding: different bytes, same program */
 memcpy(fs_b,n2_preload_fs,sizeof(fs_b));fs_b[sizeof(fs_b)-1]^=0x5a;
 struct tegu_generic_draw_shader A=n2_preload_shader,B=n2_preload_shader;B.vertex_code=vs_b;B.fragment_code=fs_b;
 const struct tegu_generic_draw_shader *const P[][4]={
  {&A,&A,&B,0},{&B,&A,&A,0},{&A,&B,&A,0},{&A,&B,&B,0},{&B,&B,&A,0},{&A,&A,&B,&B},{&B,&A,&A,&B}};
 unsigned bad=0;
 for(unsigned p=0;p<sizeof(P)/sizeof(P[0]);p++){
  unsigned count=P[p][3]?4:3;
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  struct tegu_generic_draw_request q[4];struct n2mb_batch_draw dr[4];
  for(unsigned k=0;k<count;k++){
   q[k]=(struct tegu_generic_draw_request){.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
   q[k].bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
   dr[k]=(struct n2mb_batch_draw){P[p][k],&q[k],NULL,NULL};
  }
  uint64_t o[2];unsigned n=2;
  assert(!n2mb_draw_batch(b,dr,count,o,&n)&&f.submitted==1&&n2mb_batch_last_count(b)==count);
  /* The command BO: count draw blocks + one preload block, the only BO of that size. */
  const unsigned char *cmd=NULL;
  for(unsigned h=1;h<=f.next;h++)if(f.buffers[h].active&&f.buffers[h].size==(uint64_t)(count+1)*N2R_BLOCK){assert(!cmd);cmd=f.buffers[h].data;}
  assert(cmd);
  for(unsigned k=0;k<count;k++){
   const struct tegu_generic_draw_shader *s=P[p][k];const unsigned char *blk=cmd+(size_t)k*N2R_BLOCK;
   uint64_t vs=get64(blk+1536+8)-s->position_offset,fs=get64(blk+1536+64+8);
   const unsigned char *vc=code_at(&f,vs),*fc=code_at(&f,fs);
   int ok=vc&&fc&&!memcmp(vc,s->vertex_code,s->vertex_bytes)&&!memcmp(fc,s->fragment_code,s->fragment_bytes);
   if(!ok){bad++;printf("FAIL pattern %u draw %u runs another draw's %s code\n",p,k,
    vc&&!memcmp(vc,s->vertex_code,s->vertex_bytes)?"fragment":"vertex");}
  }
  close_bridge(b,&f);
 }
 if(bad){printf("FAIL %u draw(s) of mixed-shader batches run the wrong program\n",bad);return 1;}
 puts("PASS every draw of a mixed-shader batch runs its own vertex/fragment code");
 return 0;
}
'''
src = out / 'batch_code_reuse_test.c'
src.write_text(fake + main)
cc = os.environ.get('CC', 'cc')
san = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
inc = ['-I' + str(gpu / 'include'), '-I' + str(gpu / 'metal'), '-I' + str(gpu / 'build/tegu-frontend')]
nice = ['nice', '-n', '10']
render_o = out / 'render.o'
subprocess.run(nice + [cc] + san + ['-std=c11'] + inc + ['-c', str(gpu / 'metal/nord2_metal_render.c'), '-o', str(render_o)], check=True)
exe = out / 'batch_code_reuse_test'
subprocess.run(nice + [cc] + san + ['-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation'] + inc +
               ['-o', str(exe), str(src), str(gpu / 'metal/nord2_metal_bridge.c'), str(gpu / 'src/nord2_gpu_desc.c'),
                str(gpu / 'src/nord2_gpu_jm.c'), str(render_o), str(gpu / 'metal/tegu/blend_state.c'), '-lm'], check=True)
sys.exit(subprocess.run(nice + [str(exe)]).returncode)
