#include "compositor_vertex_state.h"
#include "render_pipeline_format.h"
#include "tegu_memory.h"
static int compositor_cpu_overlap(const void *a,size_t an,const void *b,size_t bn) {
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return an&&bn&&(x<=y?y-x<an:x-y<bn);
}
int tegu_compositor_vertex_build(const struct tegu_vertex_attribute *attributes,size_t count,
 const struct tegu_vertex_buffer *buffer,const struct tegu_vertex_draw *draw,
 uint64_t descriptor_address,struct tegu_compositor_vertex_state *out) {
 if(!attributes||(count!=2&&count!=3)||!buffer||!draw||!out||
    compositor_cpu_overlap(out,sizeof(*out),attributes,count*sizeof(*attributes))||
    compositor_cpu_overlap(out,sizeof(*out),buffer,sizeof(*buffer))||
    compositor_cpu_overlap(out,sizeof(*out),draw,sizeof(*draw)))return -1;
 unsigned seen=0;
 for(unsigned i=0;i<count;i++) {
  const struct tegu_vertex_attribute *a=&attributes[i];
  uint32_t expected=a->location==0?109u:a->location==1?
      (count==2&&a->offset==32?97u:103u):97u;
  if(a->location>=count||a->buffer_index>1||a->format!=expected||
     (a->stride!=32&&a->stride!=40&&a->stride!=48)||a->input_rate||a->divisor!=1||
     (i&&a->buffer_index!=attributes[0].buffer_index))return -2;
  seen|=1u<<a->location;
 }
 if(seen!=((1u<<count)-1u)||draw->first_vertex||
    draw->vertex_count<3||draw->vertex_count>384||draw->vertex_count%3||draw->instance_count!=1||draw->base_instance)return -2;
 /* Small binding table on the stack; all multi-KiB output lives in `out`. */
 struct tegu_vertex_buffer bindings[TEGU_VERTEX_SLOTS]={{0}};bindings[attributes[0].buffer_index]=*buffer;
 if(tegu_vertex_fetch_build((1u<<count)-1u,attributes,count,bindings,draw,descriptor_address,&out->workspace))return -3;
 /* There are no fallible steps after the generic builder has succeeded. */
 memset(out->driver,0,sizeof(out->driver));
 for(unsigned i=0;i<count;i++) {
  unsigned location=attributes[i].location;
  memcpy(out->driver+location*32,out->workspace.descriptors+0x100+location*32,32);
  tegu_render_put(out->driver+location*32+12,17);
 }
 /* Mesa MALI_SAMPLER_header with clamp_integer_array_indices=false. This
  * driver sampler is distinct from the actual combined texture sampler. */
 tegu_render_put(out->driver+16*32,0x02a99901u);
 memcpy(out->driver+17*32,out->workspace.descriptors+0x100+(32+attributes[0].buffer_index)*32,32);
 return 0;
}
