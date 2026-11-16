#include "compositor_load_assets_v6.h"
#include "load_mesa_runtime_templates.h"
#include "tegu_memory.h"

static int range_end(const void *p,size_t n,uintptr_t *end)
{
 uintptr_t b=(uintptr_t)p;if(!p||n>UINTPTR_MAX-b)return 0;*end=b+n;return 1;
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
 uintptr_t ae,be;if(!range_end(a,an,&ae)||!range_end(b,bn,&be))return 1;
 return an&&bn&&(uintptr_t)a<be&&(uintptr_t)b<ae;
}

int tegu_compositor_load_assets_build_v6(
 const struct tegu_render_target_layout *l,uint64_t target,
 unsigned char resource[4096],unsigned char dcd[4096],unsigned char shader[4096],
 struct tegu_compositor_load_asset_scratch_v6 *s,
 struct tegu_compositor_load_assets_v6 *out)
{
 if(!l||!resource||!dcd||!shader||!s||!out||
    l->version!=TEGU_RENDER_TARGET_LAYOUT_VERSION||l->width<16||l->height<16||
    l->width>4096||l->height>4096||l->row_stride<l->width*4u||
    (l->row_stride&63u)||l->row_stride>65536u||
    (uint64_t)l->row_stride*l->height>16u*1024u*1024u||
    l->target_length<(uint64_t)l->row_stride*l->height||!target||
    target>=(1ull<<48)||target&63u||target+(uint64_t)l->row_stride*l->height>=(1ull<<48))return -1;
 if(overlaps(resource,4096,dcd,4096)||overlaps(resource,4096,shader,4096)||
    overlaps(dcd,4096,shader,4096)||overlaps(resource,4096,s,sizeof(*s))||
    overlaps(dcd,4096,s,sizeof(*s))||overlaps(shader,4096,s,sizeof(*s))||
    overlaps(resource,4096,out,sizeof(*out))||overlaps(dcd,4096,out,sizeof(*out))||
    overlaps(shader,4096,out,sizeof(*out))||overlaps(s,sizeof(*s),out,sizeof(*out))||
    overlaps(resource,4096,l,sizeof(*l))||overlaps(dcd,4096,l,sizeof(*l))||
    overlaps(shader,4096,l,sizeof(*l))||overlaps(s,sizeof(*s),l,sizeof(*l))||
    overlaps(out,sizeof(*out),l,sizeof(*l)))return -1;
 uint32_t extent=(l->width-1u)|((l->height-1u)<<16);
 memset(resource,0,4096);memset(dcd,0,4096);memset(shader,0,4096);
 memcpy(resource,tegu_load_template_resource,sizeof(tegu_load_template_resource));
 memcpy(dcd,tegu_load_template_dcd,sizeof(tegu_load_template_dcd));
 memcpy(shader,tegu_load_template_shader,sizeof(tegu_load_template_shader));
 memcpy(s->fbd,tegu_load_template_fbd,sizeof(s->fbd));
 memcpy(s->texture_plane,tegu_load_template_texture,sizeof(s->texture_plane));
 memcpy(s->rt,tegu_load_template_rt,sizeof(s->rt));
 memcpy(s->visibility,tegu_load_template_sync,sizeof(s->visibility));
 /* These are the complete dynamic fields observed from Mesa at 16x16,
  * 32x32, 256x256 and 1080x2424; tests compare full generated objects. */
 tegu_render_put(s->fbd+32,extent);tegu_render_put(s->fbd+40,extent);
 tegu_render_put(s->fbd+96,extent);tegu_render_put(s->fbd+104,extent);
 tegu_render_put(s->texture_plane+4,extent);
 tegu_render_put(s->texture_plane+36,(uint32_t)((uint64_t)l->row_stride*l->height));
 tegu_render_put(s->texture_plane+48,l->row_stride);
 tegu_render_put(s->texture_plane+60,extent);
 tegu_render_put(s->rt+40,l->row_stride);
 tegu_render_put(s->rt+44,(uint32_t)((uint64_t)l->row_stride*l->height));
 tegu_render_put(dcd+0x184,l->width|(l->height<<16));
 for(unsigned i=0;i<8;i++)s->rt[32+i]=(unsigned char)(target>>(8*i));
 *out=(struct tegu_compositor_load_assets_v6){
   s->fbd,sizeof(s->fbd),resource,192,dcd,4096,shader,128,
   s->texture_plane,sizeof(s->texture_plane),s->rt,sizeof(s->rt),
   s->visibility,sizeof(s->visibility)
 };
 return 0;
}
