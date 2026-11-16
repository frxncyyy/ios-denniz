#include "compositor_load_bundle_v6.h"
#include "tegu_memory.h"
#include <stdint.h>

static uint64_t load_get64(const unsigned char *p) {
 return (uint64_t)tegu_render_word(p)|((uint64_t)tegu_render_word(p+4)<<32);
}
static void cload_put64(unsigned char *p,uint64_t v) {
 tegu_render_put(p,(uint32_t)v);tegu_render_put(p+4,(uint32_t)(v>>32));
}
static int load_ov(const void *a,size_t an,const void *b,size_t bn) {
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return an&&bn&&(x<=y?y-x<an:x-y<bn);
}
static int load_ovva(uint64_t a,uint64_t an,uint64_t b,uint64_t bn) {
 return an&&bn&&(a<=b?b-a<an:a-b<bn);
}
static int load_valid_va(uint64_t p,uint64_t n) {return p&&n&&p<(1ull<<48)&&p<=((1ull<<48)-n);}
static int load_noalias(const void *out,size_t on,const void *p,size_t n) {
 return !load_ov(out,on,p,n);
}
static int load_rt_matches(const unsigned char *current,const unsigned char *mesa,uint64_t target) {
 for(unsigned i=0;i<64;i++) {
  unsigned char expected=mesa[i];
  if(i>=32&&i<40)expected=(unsigned char)(target>>(8*(i-32)));
  if(i>=48)continue; /* caller clear is retained; LOAD does not consume it */
  if(current[i]!=expected)return 0;
 }
 return 1;
}

int tegu_compositor_load_bundle_build_v6(
 const struct tegu_render_state *clear,
 const struct tegu_render_addresses *a,
 const struct tegu_render_target_layout *l,
 const struct tegu_load_mapping *m,
 const struct tegu_compositor_load_assets_v6 *x,
 size_t vertex_bytes,size_t fragment_bytes,
 struct tegu_compositor_load_bundle_v6 *out)
{
 uint64_t surface=0;
 if(!clear||!a||!l||!m||!x||!out||
    l->version!=TEGU_RENDER_TARGET_LAYOUT_VERSION||l->width<16||l->height<16||
    l->width>4096||l->height>4096||l->row_stride<l->width*4u||
    (l->row_stride&63u)||l->row_stride>4096u*16u||
    (surface=(uint64_t)l->row_stride*l->height)>(16ull<<20)||
    l->target_length<surface||l->target_length>(16ull<<20)||!vertex_bytes||!fragment_bytes||
    !x->fbd_parameters||x->fbd_bytes!=128||!x->resource_page||x->resource_bytes!=192||
    !x->dcd_page||x->dcd_bytes!=4096||!x->shader||x->shader_bytes!=128||
    !x->texture_plane||x->texture_plane_bytes!=64||!x->rt||x->rt_bytes!=64||
    !x->visibility||x->visibility_bytes!=16||
    clear->command_count>124||
    !load_valid_va(m->resource_va,4096)||!load_valid_va(m->dcd_va,4096)||!load_valid_va(m->shader_va,4096)||
    (m->resource_va&4095)||(m->dcd_va&4095)||(m->shader_va&4095)||
    m->resource_va==m->dcd_va||m->resource_va==m->shader_va||m->dcd_va==m->shader_va||
    m->dcd_va!=m->resource_va+4096||m->shader_va!=TEGU_LOAD_APERTURE_VA+TEGU_LOAD_PRELOAD_SCRATCH_PAGE*4096u||
    m->total_pages<m->target_pages+2u||
    m->page_role[0]!=TEGU_LOAD_MAP_RO_NX||m->page_role[1]!=TEGU_LOAD_MAP_RO_NX||
    m->page_role[2]!=TEGU_LOAD_MAP_RO_X||
    m->target_pages!=(l->target_length+4095)/4096||
    a->target%64||!load_valid_va(a->target,surface)||
    load_ovva(a->target,surface,m->resource_va,4096)||load_ovva(a->target,surface,m->dcd_va,4096)||
    load_ovva(a->target,surface,m->shader_va,4096)||
    !load_rt_matches(clear->descriptors+128,x->rt,a->target)||
    !load_noalias(out,sizeof(*out),clear,sizeof(*clear))||
    !load_noalias(out,sizeof(*out),a,sizeof(*a))||!load_noalias(out,sizeof(*out),l,sizeof(*l))||
    !load_noalias(out,sizeof(*out),m,sizeof(*m))||!load_noalias(out,sizeof(*out),x,sizeof(*x))||
    !load_noalias(out,sizeof(*out),x->fbd_parameters,x->fbd_bytes)||
    !load_noalias(out,sizeof(*out),x->resource_page,x->resource_bytes)||
    !load_noalias(out,sizeof(*out),x->dcd_page,x->dcd_bytes)||
    !load_noalias(out,sizeof(*out),x->shader,x->shader_bytes)||
    !load_noalias(out,sizeof(*out),x->texture_plane,x->texture_plane_bytes)||
    !load_noalias(out,sizeof(*out),x->rt,x->rt_bytes)||
    !load_noalias(out,sizeof(*out),x->visibility,x->visibility_bytes))return -1;

 /* The clear-state shape is checked before any output writes, so malformed
  * command streams preserve the caller's destination just like bad inputs. */
 unsigned clear_index=UINT32_MAX,clear_wait_index=UINT32_MAX;
 for(unsigned i=0;i<clear->command_count;i++) {
  if(clear->commands[i]==((49ull<<56)|(1ull<<32))) {
   if(clear_index!=UINT32_MAX)return -1;
   clear_index=i;
  }
  if(clear_index!=UINT32_MAX&&i>clear_index&&clear->commands[i]==((3ull<<56)|(0xffull<<16))) {
   clear_wait_index=i;break;
  }
 }
 if(clear_index==UINT32_MAX||clear_wait_index!=clear_index+1)return -1;

 for(unsigned i=0;i<7;i++) {
  uint64_t len=i==0?4096:i==1?4096:i==2?65536:i==3?131072:i==4?surface:
    i==5?vertex_bytes:fragment_bytes;
  uint64_t base=i==0?a->descriptors:i==1?a->tiler:i==2?a->geometry:i==3?a->chunk:
    i==4?a->target:i==5?a->vertex_code:a->fragment_code;
  if(!load_valid_va(base,len)||load_ovva(base,len,m->resource_va,4096)||load_ovva(base,len,m->dcd_va,4096)||
     load_ovva(base,len,m->shader_va,4096))return -1;
 }

 /* Validate Mesa templates before relocation. Target and resource graph are
  * explicit: texture.surfaces -> plane, plane.base -> target, DCD -> resource
  * table/descriptor array/programs. The shader VA only lands in RO/X storage. */
 if(tegu_render_word(x->resource_page+0x80)!=0x05056000u||
    tegu_render_word(x->resource_page+0x84)!=0x01000000u||
    tegu_render_word(x->resource_page+0x88)!=64u||
    tegu_render_word(x->texture_plane+0)!=0x2ec01022u||
    tegu_render_word(x->texture_plane+4)!=((l->width-1u)|((l->height-1u)<<16))||
    tegu_render_word(x->texture_plane+8)!=0x688u||
    load_get64(x->texture_plane+16)!=0x05056040ull||
    tegu_render_word(x->texture_plane+32)!=0x0200021au||
    tegu_render_word(x->texture_plane+36)!=(uint32_t)surface||
    load_get64(x->texture_plane+40)!=0x05054000ull||
    tegu_render_word(x->texture_plane+48)!=l->row_stride||
    tegu_render_word(x->texture_plane+56)!=0||
    tegu_render_word(x->texture_plane+60)!=((l->width-1u)|((l->height-1u)<<16))||
    load_get64(x->dcd_page+0x28)!=0x050571e0ull||
    load_get64(x->dcd_page+0x30)!=0x050571c1ull||
    load_get64(x->dcd_page+0x60)!=0x05056084ull||
    load_get64(x->dcd_page+0x68)!=0x050571a0ull||
    load_get64(x->dcd_page+0x70)!=0x05057200ull||
    load_get64(x->dcd_page+0x78)!=0x05057180ull||
    tegu_render_word(x->dcd_page+0x180)!=0||
    tegu_render_word(x->dcd_page+0x184)!=(l->width|(l->height<<16))||
    load_get64(x->dcd_page+0x1a8)!=0x05058000ull||
    load_get64(x->visibility)!=0x2400000000000200ull||
    load_get64(x->visibility+8)!=0x0300000000010000ull||
    (tegu_render_word(x->fbd_parameters+64)&7u)!=2u||
    load_get64(x->fbd_parameters+64+24)!=0x05057000ull||
    tegu_render_word(x->fbd_parameters+64+32)!=(l->width-1u|((l->height-1u)<<16)))return -2;
 /* Every check is complete: from here operations cannot fail. */
 out->draw=*clear;
 memset(out->resource_page,0,sizeof(out->resource_page));
 memset(out->shader_page,0,sizeof(out->shader_page));
 memcpy(out->resource_page,x->resource_page,192);
 memcpy(out->resource_page+0x20,x->texture_plane,64);
 memcpy(out->dcd_page,x->dcd_page,4096);
 memcpy(out->shader_page,x->shader,128);
 memcpy(out->draw.descriptors,x->fbd_parameters+64,64);
 /* Keep all existing TGR6 target descriptor bytes exact; only FBD parameters
  * above them are replaced by the LOAD-enabled Mesa descriptor. */
 cload_put64(out->resource_page+0x30,m->resource_va+0x40);
 cload_put64(out->resource_page+0x48,a->target);
 /* Resource[0] is a descriptor-bearing resource span; preserve its bit-56
  * contains_descriptors flag while rebasing the 56-bit VA. */
 tegu_render_put(out->resource_page+0x80,(uint32_t)m->resource_va);
 tegu_render_put(out->resource_page+0x84,0x01000000u|(uint32_t)(m->resource_va>>32));
 cload_put64(out->resource_page+0x20+16,m->resource_va+0x40);
 cload_put64(out->resource_page+0x40+8,a->target);
 cload_put64(out->dcd_page+0x28,m->dcd_va+0x1e0);
 cload_put64(out->dcd_page+0x30,(m->dcd_va+0x1c0)|1);
 cload_put64(out->dcd_page+0x60,(m->resource_va+0x80)|4);
 cload_put64(out->dcd_page+0x68,m->dcd_va+0x1a0);
 cload_put64(out->dcd_page+0x70,m->dcd_va+0x200);
 cload_put64(out->dcd_page+0x78,m->dcd_va+0x180);
 cload_put64(out->dcd_page+0x1a8,m->shader_va);
 cload_put64(out->draw.descriptors+16,a->descriptors+0x100);
 cload_put64(out->draw.descriptors+24,m->dcd_va);
 cload_put64(out->draw.descriptors+56,a->tiler);
 /* Replace the one-clear command+wait pair with the previous-job visibility
  * sequence. Do not alter draw/store commands or RT descriptor. */
 unsigned dst=0,removed=0;
 for(unsigned i=0;i<clear->command_count;i++) {
  uint64_t c=clear->commands[i];
  if(!removed&&c==((49ull<<56)|(1ull<<32))) {removed=1;continue;}
  if(removed==1&&c==(3ull<<56|0xffull<<16)) {removed=2;continue;}
  if(dst<128)out->draw.commands[dst++]=c;
 }
 /* A full prior-job wait, then the independently Mesa-packed invalidate and
  * its signal wait, precede this LOAD job. */
 for(unsigned i=dst;i>0;i--)out->draw.commands[i+2]=out->draw.commands[i-1];
 out->draw.commands[0]=(3ull<<56)|(0xffull<<16);
 out->draw.commands[1]=load_get64(x->visibility);
 out->draw.commands[2]=load_get64(x->visibility+8);
 out->draw.command_count=dst+3;
 out->page_count=2;out->resource_readonly_nx=1;out->dcd_readonly_nx=1;out->shader_readonly_x=1;
 return 0;
}
