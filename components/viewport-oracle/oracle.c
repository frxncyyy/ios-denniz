
#define main original_render_test_main
#include "nord2_metal_render_test.c"
#undef main
#include <math.h>
#define PAN_ARCH 9
struct pipe_viewport_state {float scale[3],translate[3];};
struct pipe_scissor_state {unsigned minx,miny,maxx,maxy;};
struct pipe_rasterizer_state {bool scissor,clip_halfz;};
struct panfrost_rasterizer {struct pipe_rasterizer_state base;};
struct panfrost_context {struct pipe_viewport_state pipe_viewport;struct pipe_scissor_state scissor;struct panfrost_rasterizer *rasterizer;};
struct panfrost_batch {struct panfrost_context *ctx;struct {unsigned width,height;} key;bool scissor_culls_everything;float minimum_z,maximum_z;struct mali_scissor_packed scissor;};
static void util_viewport_zmin_zmax(const struct pipe_viewport_state*v,bool half,float*lo,float*hi){
 float a=v->translate[2]-(half?0:v->scale[2]),b=v->translate[2]+v->scale[2];*lo=MIN2(a,b);*hi=MAX2(a,b);
}
static void panfrost_batch_union_scissor(struct panfrost_batch*b,unsigned x,unsigned y,unsigned u,unsigned v){(void)b;(void)x;(void)y;(void)u;(void)v;}
static uint64_t
panfrost_emit_viewport(struct panfrost_batch *batch)
{
   struct panfrost_context *ctx = batch->ctx;
   const struct pipe_viewport_state *vp = &ctx->pipe_viewport;
   const struct pipe_scissor_state *ss = &ctx->scissor;
   const struct pipe_rasterizer_state *rast = &ctx->rasterizer->base;

   /* Derive min/max from translate/scale. Note since |x| >= 0 by
    * definition, we have that -|x| <= |x| hence translate - |scale| <=
    * translate + |scale|, so the ordering is correct here. */
   float vp_minx = vp->translate[0] - fabsf(vp->scale[0]);
   float vp_maxx = vp->translate[0] + fabsf(vp->scale[0]);
   float vp_miny = vp->translate[1] - fabsf(vp->scale[1]);
   float vp_maxy = vp->translate[1] + fabsf(vp->scale[1]);

   float minz, maxz;
   util_viewport_zmin_zmax(vp, rast->clip_halfz, &minz, &maxz);
   /* Hardware requires a clamped depth ranges, but util_viewport_zmin_zmax
    * may return bounds outside [0,1] when the translate/scale fields are set
    * directly instead of through _mesa_set_depth_range. This occurs in
    * u_blitter. */
   minz = SATURATE(minz);
   maxz = SATURATE(maxz);

   /* Scissor to the intersection of viewport and to the scissor, clamped
    * to the framebuffer */

   unsigned minx = MIN2(batch->key.width, MAX2((int)vp_minx, 0));
   unsigned maxx = MIN2(batch->key.width, MAX2((int)vp_maxx, 0));
   unsigned miny = MIN2(batch->key.height, MAX2((int)vp_miny, 0));
   unsigned maxy = MIN2(batch->key.height, MAX2((int)vp_maxy, 0));

   if (ss && rast->scissor) {
      minx = MAX2(ss->minx, minx);
      miny = MAX2(ss->miny, miny);
      maxx = MIN2(ss->maxx, maxx);
      maxy = MIN2(ss->maxy, maxy);
   }

   /* Set the range to [1, 1) so max values don't wrap round */
   if (maxx == 0 || maxy == 0)
      maxx = maxy = minx = miny = 1;

   panfrost_batch_union_scissor(batch, minx, miny, maxx, maxy);
   batch->scissor_culls_everything = (minx >= maxx || miny >= maxy);

   /* [minx, maxx) and [miny, maxy) are exclusive ranges in the hardware */
   maxx--;
   maxy--;

   batch->minimum_z = minz;
   batch->maximum_z = maxz;

#if PAN_ARCH <= 7
   struct pan_ptr T = pan_pool_alloc_desc(&batch->pool.base, VIEWPORT);

   if (!T.cpu)
      return 0;

   pan_cast_and_pack(T.cpu, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;

      cfg.minimum_z = batch->minimum_z;
      cfg.maximum_z = batch->maximum_z;
   }

   return T.gpu;
#else
   pan_cast_and_pack(&batch->scissor, SCISSOR, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;
   }

   return 0;
#endif
}
int main(void){
 original_render_test_main();
 _Alignas(64) uint8_t data[N2R_BLOCK];
 struct tegu_generic_draw_request q={.width=64,.height=64,.row_bytes=256,.target_format=80,.load_action=2,.vertex_count=3,.scissor={0,0,64,64},.viewport_valid=1,.binding_count=1};
 q.bindings[0]=(struct tegu_generic_draw_binding){.kind=TEGU_GENERIC_DRAW_COMBINED,.binding=0,.memory={.token=6,.bytes=16384},.width=64,.height=64,.row_bytes=256,.format=80};
 struct n2r_addresses a={.command=0x1234000000ull,.frame=0x1234000000ull,.heap=0x1280000000ull,.heap_bytes=2<<20,.vertex_code=0x12a0000000ull,.fragment_code=0x12b0000000ull,.target=0x12c0000000ull,.binding={0x12d0000000ull}};
 const float cases[][4]={{16,16,32,32},{-16,-16,32,32},{70,70,32,32},{-40,-40,16,16},{.75f,3.25f,12.5f,26.25f},{0,0,.25f,.25f},{0,0,64,64},{-5,7,160,160}};
 unsigned runs=0,empty=0;
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)for(unsigned sc=0;sc<3;sc++)for(unsigned reverse=0;reverse<2;reverse++){
  memcpy(q.viewport,cases[i],sizeof(cases[i]));q.viewport[4]=reverse?.8f:.2f;q.viewport[5]=reverse?.2f:.8f;
  const unsigned scissors[3][4]={{0,0,64,64},{24,8,16,48},{48,48,16,16}};memcpy(q.scissor,scissors[sc],sizeof(q.scissor));
  struct panfrost_rasterizer rast={.base={.scissor=true,.clip_halfz=true}};
  struct panfrost_context ctx={.rasterizer=&rast,.scissor={q.scissor[0],q.scissor[1],q.scissor[0]+q.scissor[2],q.scissor[1]+q.scissor[3]}};
  ctx.pipe_viewport=(struct pipe_viewport_state){.scale={q.viewport[2]/2,-q.viewport[3]/2,q.viewport[5]-q.viewport[4]},.translate={q.viewport[0]+q.viewport[2]/2,q.viewport[1]+q.viewport[3]/2,q.viewport[4]}};
  struct panfrost_batch batch={.ctx=&ctx,.key={64,64}};panfrost_emit_viewport(&batch);
  assert(!n2r_build(data,&n2_preload_shader,&q,&a,2,a.command+32768));
  pan_unpack((const struct mali_job_header_packed*)(data+N2R_JOB),JOB_HEADER,header);
  assert(header.dependency_1==1&&header.next==a.command+32768);
  if(batch.scissor_culls_everything){assert(header.type==MALI_JOB_TYPE_NULL);empty++;}
  else{
   assert(header.type==MALI_JOB_TYPE_MALLOC_VERTEX);
   assert(!memcmp(data+N2R_JOB+104,&batch.scissor,sizeof(batch.scissor)));
  }
  pan_unpack((const struct mali_draw_packed*)(data+N2R_JOB+128),DRAW,draw);
  assert(fabsf(draw.minimum_z-batch.minimum_z)<.000001f&&fabsf(draw.maximum_z-batch.maximum_z)<.000001f);
  pan_unpack((const struct mali_job_header_packed*)(data+N2R_FRAGMENT),JOB_HEADER,fragment);
  assert(fragment.type==MALI_JOB_TYPE_FRAGMENT);runs++;
 }
 /* Independent pixel-centre geometry: the old unbounded viewport predicts
  * precisely the 752 extra pixels observed on every hardware target. */
 unsigned old=0,clipped=0;
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
  float px=x+.5f,py=y+.5f;/* The diagonal is the excluded edge under the configured raster tie rule. */
  bool triangle=px>=16&&py<=48&&px-py<32;
  old+=triangle;clipped+=triangle&&x>=16&&x<48&&y>=16&&y<48;
 }
 assert(old==1776&&clipped==1024&&old-clipped==752);
 printf("Mesa arch9 unchanged viewport oracle: %u cases (%u fully clipped), depth/chaining/fragment preservation PASS; observed752-pixel error reproduced\n",runs,empty);
 return 0;
}
