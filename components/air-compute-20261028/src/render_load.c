#include "render_load.h"
#include <string.h>
#include "load_mesa_templates.h"

static void load_put64(unsigned char *p,uint64_t x) {
 for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(x>>(8*i));
}
static int span48(uint64_t a,uint64_t n) { return a && a<(1ULL<<48) && n && a<=((1ULL<<48)-n); }
static int overlaps(uint64_t a,uint64_t an,uint64_t b,uint64_t bn) {
 return a<b+bn && b<a+an;
}
static int cpu_span_valid(const void *p,size_t n) {
 uintptr_t a=(uintptr_t)p;
 return p && n<=UINTPTR_MAX-a;
}
static int cpu_spans_overlap(const void *a,size_t an,const void *b,size_t bn) {
 uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
 if(!an||!bn)return 0;
 if(an>UINTPTR_MAX-aa||bn>UINTPTR_MAX-bb)return 1;
 return aa<bb+bn&&bb<aa+an;
}
int tegu_render_build_load_pass(struct tegu_render_state *draw,
 const void *package,size_t package_bytes,const struct tegu_render_addresses *a,
 const struct tegu_render_target_layout *l,struct tegu_render_load_state *out) {
 struct tegu_render_info info;
 /* Validate CPU ranges and all output/input separation before parsing or any
  * write. Both draw and out are outputs; every other pointer is read-only. */
 if(!draw||!package||!a||!l||!out||
    !cpu_span_valid(draw,sizeof(*draw))||!cpu_span_valid(package,package_bytes)||
    !cpu_span_valid(a,sizeof(*a))||!cpu_span_valid(l,sizeof(*l))||
    !cpu_span_valid(out,sizeof(*out))||
    cpu_spans_overlap(draw,sizeof(*draw),out,sizeof(*out))||
    cpu_spans_overlap(draw,sizeof(*draw),package,package_bytes)||
    cpu_spans_overlap(draw,sizeof(*draw),a,sizeof(*a))||
    cpu_spans_overlap(draw,sizeof(*draw),l,sizeof(*l))||
    cpu_spans_overlap(out,sizeof(*out),package,package_bytes)||
    cpu_spans_overlap(out,sizeof(*out),a,sizeof(*a))||
    cpu_spans_overlap(out,sizeof(*out),l,sizeof(*l))||
    tegu_render_parse(package,package_bytes,&info)||info.version!=1||
    info.color_format!=70||info.vertex_mask||info.texture.version||info.secondary_offset||
    l->version!=TEGU_RENDER_TARGET_LAYOUT_VERSION||l->width!=16||l->height!=16||
    l->row_stride!=64||l->target_length!=1024||(a->target&63)||!span48(a->target,1024)||
    (TEGU_LOAD_RESOURCES_VA&4095)||(TEGU_LOAD_DCD_VA&4095)||(TEGU_LOAD_SHADER_VA&4095)||
    !span48(TEGU_LOAD_RESOURCES_VA,4096)||!span48(TEGU_LOAD_DCD_VA,4096)||
    !span48(TEGU_LOAD_SHADER_VA,4096)||
    overlaps(TEGU_LOAD_RESOURCES_VA,4096,TEGU_LOAD_DCD_VA,4096)||
    overlaps(TEGU_LOAD_RESOURCES_VA,4096,TEGU_LOAD_SHADER_VA,4096)||
    overlaps(TEGU_LOAD_DCD_VA,4096,TEGU_LOAD_SHADER_VA,4096)||
    !span48(a->descriptors,4096)||!span48(a->tiler,4096)||
    !span48(a->geometry,65536)||!span48(a->chunk,131072)||
    !span48(a->vertex_code,info.stages[0].code_bytes)||
    !span48(a->fragment_code,info.stages[1].code_bytes)||
    overlaps(a->target,1024,TEGU_LOAD_RESOURCES_VA,4096)||
    overlaps(a->target,1024,TEGU_LOAD_DCD_VA,4096)||
    overlaps(a->target,1024,TEGU_LOAD_SHADER_VA,4096)||
    overlaps(TEGU_LOAD_RESOURCES_VA,4096,a->descriptors,4096)||
    overlaps(TEGU_LOAD_DCD_VA,4096,a->descriptors,4096)||
    overlaps(TEGU_LOAD_SHADER_VA,4096,a->descriptors,4096)||
    overlaps(TEGU_LOAD_RESOURCES_VA,4096,a->tiler,4096)||
    overlaps(TEGU_LOAD_DCD_VA,4096,a->tiler,4096)||
    overlaps(TEGU_LOAD_SHADER_VA,4096,a->tiler,4096)||
    overlaps(TEGU_LOAD_RESOURCES_VA,4096,a->geometry,65536)||
    overlaps(TEGU_LOAD_DCD_VA,4096,a->geometry,65536)||
    overlaps(TEGU_LOAD_SHADER_VA,4096,a->geometry,65536)||
    overlaps(TEGU_LOAD_RESOURCES_VA,4096,a->chunk,131072)||
    overlaps(TEGU_LOAD_DCD_VA,4096,a->chunk,131072)||
    overlaps(TEGU_LOAD_SHADER_VA,4096,a->chunk,131072)||
    overlaps(TEGU_LOAD_RESOURCES_VA,4096,a->vertex_code,info.stages[0].code_bytes)||
    overlaps(TEGU_LOAD_DCD_VA,4096,a->vertex_code,info.stages[0].code_bytes)||
    overlaps(TEGU_LOAD_SHADER_VA,4096,a->vertex_code,info.stages[0].code_bytes)||
    overlaps(TEGU_LOAD_RESOURCES_VA,4096,a->fragment_code,info.stages[1].code_bytes)||
    overlaps(TEGU_LOAD_DCD_VA,4096,a->fragment_code,info.stages[1].code_bytes)||
    overlaps(TEGU_LOAD_SHADER_VA,4096,a->fragment_code,info.stages[1].code_bytes))return -1;

 /* No validation can fail beyond this point. The caller supplies this output
  * inside retained heap backing, avoiding a 12 KiB kernel-stack object. */
 memset(out,0,sizeof(*out));
 memcpy(out->resources,tegu_load_resources,sizeof(tegu_load_resources));
 memcpy(out->dcd,tegu_load_dcd,sizeof(tegu_load_dcd));
 memcpy(out->shader_page,tegu_load_shader,sizeof(tegu_load_shader));
 load_put64(out->resources+0x30,TEGU_LOAD_RESOURCES_VA+0x40);
 load_put64(out->resources+0x48,a->target);
 load_put64(out->dcd+0x28,TEGU_LOAD_DCD_VA+0x1e0);
 load_put64(out->dcd+0x30,(TEGU_LOAD_DCD_VA+0x1c0)|1);
 load_put64(out->dcd+0x60,(TEGU_LOAD_RESOURCES_VA+0x80)|4);
 load_put64(out->dcd+0x68,TEGU_LOAD_DCD_VA+0x1a0);
 load_put64(out->dcd+0x70,TEGU_LOAD_DCD_VA+0x200);
 load_put64(out->dcd+0x78,TEGU_LOAD_DCD_VA+0x180);
 load_put64(out->dcd+0x1a8,TEGU_LOAD_SHADER_VA);
 unsigned char fbd[192];memcpy(fbd,tegu_load_fbd,sizeof(fbd));
 load_put64(fbd+0x10,a->descriptors+0x100);
 load_put64(fbd+0x28,UINT64_C(0x01001080000f000f));
 load_put64(fbd+0x30,UINT64_C(0x10000));
 load_put64(fbd+0x18,TEGU_LOAD_DCD_VA);
 load_put64(fbd+0x38,a->tiler);
 load_put64(fbd+0xa0,a->target);
 out->readonly_page_mask=0x3u;out->executable_page_mask=0x4u;
 out->page_start=TEGU_LOAD_PAGE_START;out->page_count=TEGU_LOAD_PAGE_COUNT;
 memcpy(draw->descriptors,fbd,64);
 memcpy(draw->descriptors+128,fbd+128,64);
 return 0;
}
