#include "fixed_ui_texture_render_state.h"
#include "blend_state.h"
#include "tegu_memory.h"
static void ui2_put64(unsigned char *p,uint64_t v){tegu_render_put(p,(uint32_t)v);tegu_render_put(p+4,(uint32_t)(v>>32));}
static uint32_t ui2_half(uint32_t n){unsigned e=31u-(unsigned)__builtin_clz(n);return ((126u+e)<<23)|((n-(1u<<e))<<(23u-e));}
static void ui2_emit(struct tegu_render_state *s,uint64_t v){s->commands[s->command_count++]=v;}
static void ui2_move32(struct tegu_render_state *s,unsigned r,uint32_t v){ui2_emit(s,(2ULL<<56)|((uint64_t)r<<48)|v);}
static void ui2_move64(struct tegu_render_state *s,unsigned r,uint64_t v){if(v<(1ULL<<48))ui2_emit(s,(1ULL<<56)|((uint64_t)r<<48)|v);else{ui2_move32(s,r,(uint32_t)v);ui2_move32(s,r+1,(uint32_t)(v>>32));}}
static void ui2_wait(struct tegu_render_state *s){ui2_emit(s,(3ULL<<56)|(0xffULL<<16));}
static int ui2_full_rect(const uint32_t r[4],uint32_t w,uint32_t h){return !r[0]&&!r[1]&&r[2]==w&&r[3]==h;}
static int ui2_inner_rect(const uint32_t r[4],uint32_t w,uint32_t h){return r[2]&&r[3]&&r[0]<w&&r[1]<h&&r[2]<=w-r[0]&&r[3]<=h-r[1];}
int tegu_fixed_ui_texture_render_build(const struct tegu_ui2_info *i,const struct tegu_ui2_binding *b,
 const struct tegu_render_addresses *a,uint64_t table,const struct tegu_render_target_layout *l,
 struct tegu_render_state *out){
 if(!i||!b||!a||!l||!out||i->version!=2||i->target_format!=80||i->target_write_mask!=15||
    i->target_blend_enabled>1||i->sample_count!=1||i->topology!=3||!i->stages[0].bytes||!i->stages[1].bytes||
    i->stages[0].bytes>TEGU_UI2_STAGE_MAX||i->stages[1].bytes>TEGU_UI2_STAGE_MAX||
    i->pair.position_offset>=i->pair.secondary_offset||i->pair.secondary_offset>=i->stages[0].bytes||
    !i->pair.generic_size_bytes||i->pair.generic_size_bytes>64||
    i->attributes[0].format!=109||i->attributes[1].format!=103||
    i->attributes[2].format!=97||i->attributes[0].offset||i->attributes[1].offset!=16||i->attributes[2].offset!=32||
    b->target_format!=80||b->width!=l->width||b->height!=l->height||b->target_row_bytes!=l->row_stride||
    b->load_action>2||b->store_action!=1||b->vertex_stride!=48||
    b->topology!=3||b->vertex_start||(b->vertex_count<3||b->vertex_count>384||b->vertex_count%3)||b->instance_count!=1||b->base_instance||
    !ui2_full_rect(b->render,b->width,b->height)||!ui2_full_rect(b->viewport,b->width,b->height)||
    !ui2_inner_rect(b->scissor,b->width,b->height)||!table||(table&4095u)!=4||table>=(1ULL<<48))return -1;
 if(l->version!=TEGU_RENDER_TARGET_LAYOUT_VERSION||l->width<16||l->height<16||l->width>4096||l->height>4096||
    (l->row_stride&63)||l->row_stride<(uint64_t)l->width*4||l->row_stride>65536||
    l->target_length<(uint64_t)l->row_stride*l->height||l->target_length>(16u*1024u*1024u))return -2;
 uint32_t chunk=a->chunk_bytes?a->chunk_bytes:131072u,hm=a->hierarchy_mask?a->hierarchy_mask:1u;
 if((chunk!=131072u&&chunk!=0x200000u)||(hm!=1&&hm!=0x82)||((chunk==0x200000u)!=(hm==0x82)))return -2;
 uint64_t surface=(uint64_t)l->row_stride*l->height;
 uint64_t start[8]={a->descriptors,a->tiler,a->geometry,a->chunk,a->target,a->vertex_code,a->fragment_code,table&~4095ULL};
 uint64_t len[8]={4096,4096,65536,chunk,surface,i->stages[0].bytes,i->stages[1].bytes,4096};
 for(unsigned x=0;x<8;x++){if(!start[x]||(start[x]&((x<4||x==7)?4095u:63u))||start[x]>((1ULL<<48)-len[x]))return -2;
  for(unsigned y=0;y<x;y++)if(start[x]<start[y]+len[y]&&start[y]<start[x]+len[x])return -2;}
 /* Premultiplied source-over, or the opaque descriptor of the same R85 shader. */
 struct tegu_blend_inputs blendin={1,i->target_blend_enabled,0,1,i->target_blend_enabled?5u:0u,0,1,i->target_blend_enabled?5u:0u,15};struct tegu_blend_state blend;
 if(tegu_blend_build_format(80,&blendin,&blend))return -3;
 struct tegu_render_state s;memset(&s,0,sizeof(s));unsigned char *d=s.descriptors,*t=s.tiler;uint64_t va=a->descriptors;
 #define D32(o,v) tegu_render_put(d+(o),(v))
 #define D64(o,v) ui2_put64(d+(o),(v))
 uint32_t extent=(l->width-1u)|((l->height-1u)<<16);
 D64(16,va+0x100);D32(32,extent);D32(40,extent);D32(44,(1u<<24)|(8u<<9)|(2u<<6));D32(48,1u<<16);D64(56,a->tiler);
 /* Qualified BGRA RT layout: internal format in word 0, writeback format in word 1. */
 D32(128,1u<<26);D32(132,1u|(19u<<3)|(2u<<8)|(0x60au<<16)|(1u<<31));D64(160,a->target);D32(168,l->row_stride);D32(172,(uint32_t)surface);
 for(unsigned x=0;x<4;x++)D32(176+x*4,b->clear_color[0]);
 D32(0x100,0x00800080);D32(0x180,0x00800080);D32(0x400,7u|(7u<<16));D32(0x410,(7u<<29)|(1u<<22)|(1u<<23)|(2u<<25));
 for(unsigned x=0;x<4;x++)D32(0x440+x*4,blend.words[x]);D32(0x4c4,31);
 D32(0x600,0x80000038u);D32(0x604,(uint32_t)(i->stages[0].preload>>48));D64(0x608,a->vertex_code+i->pair.position_offset);
 D32(0x700,0x80000038u);D32(0x704,(uint32_t)(i->pair.secondary_preload>>48));D64(0x708,a->vertex_code+i->pair.secondary_offset);
 D32(0x480,0x80000128u);D32(0x484,(uint32_t)(i->stages[1].preload>>48));D64(0x488,a->fragment_code);
 memcpy(d+0xa80,b->vertex_push,8);memcpy(d+0xb80+64,b->fragment_push,12);
 uint32_t hw=ui2_half(l->width),hh=ui2_half(l->height);D32(0xa00,hw);D32(0xa04,hh|0x80000000u);D32(0xa08,0x3f800000);D32(0xa10,hw);D32(0xa14,hh);D32(0xa20,1);
 D64(0xb30,(uint64_t)blend.words[2]|((uint64_t)blend.words[3]<<32));
 tegu_render_put(t+8,hm);tegu_render_put(t+12,extent);ui2_put64(t+24,a->tiler+0x80);ui2_put64(t+32,a->geometry|16u);
 tegu_render_put(t+0x80,0x29);tegu_render_put(t+0x84,chunk);ui2_put64(t+0x88,a->chunk);ui2_put64(t+0x90,a->chunk+64);ui2_put64(t+0x98,a->chunk+chunk);
 ui2_emit(&s,23ULL<<56);ui2_move64(&s,72,a->tiler+0xc0);ui2_emit(&s,(48ULL<<56)|(72ULL<<40));ui2_emit(&s,49ULL<<56);ui2_wait(&s);ui2_emit(&s,(34ULL<<56)|0xe);
 const unsigned cleared[]={0,4,12,18,28,46,54};for(unsigned x=0;x<7;x++)ui2_move64(&s,cleared[x],cleared[x]==0||cleared[x]==4?table:cleared[x]==12?(va+0xb00)|((uint64_t)((i->stages[1].fau_words+1)/2)<<56):cleared[x]==18?va+0x700:0);
 ui2_move64(&s,8,(va+0xa00)|((uint64_t)((i->stages[0].fau_words+1)/2)<<56));ui2_move64(&s,16,va+0x600);ui2_move64(&s,20,va+0x480);ui2_move64(&s,24,va+0x4c0);
 for(unsigned r=32;r<40;r++)ui2_move32(&s,r,r==33?b->vertex_count:r==34?1:0);ui2_move64(&s,40,a->tiler);ui2_move64(&s,42,((uint64_t)(((b->scissor[1]+b->scissor[3]-1u)<<16)|(b->scissor[0]+b->scissor[2]-1u))<<32)|((b->scissor[1]<<16)|b->scissor[0]));ui2_move32(&s,44,0);ui2_move32(&s,45,0x3f800000);ui2_move32(&s,48,i->pair.generic_size_bytes);ui2_move64(&s,50,(va+0x440)|1);ui2_move64(&s,52,va+0x400);ui2_move32(&s,56,(3u<<16)|(1u<<22));ui2_move32(&s,57,blend.reads_destination?2u:3u);ui2_move32(&s,58,0x1ffff);ui2_move64(&s,60,0x3f800000);
 ui2_emit(&s,(6ULL<<56)|(1ULL<<33)|(1ULL<<38)|8|(1ULL<<18));ui2_emit(&s,9ULL<<56);ui2_wait(&s);ui2_emit(&s,(49ULL<<56)|(1ULL<<32));ui2_wait(&s);ui2_move64(&s,40,va|1);ui2_move32(&s,42,0);ui2_move32(&s,43,extent);ui2_move64(&s,44,0);ui2_move32(&s,46,0);ui2_emit(&s,7ULL<<56);ui2_wait(&s);ui2_emit(&s,(49ULL<<56)|(3ULL<<32));ui2_wait(&s);ui2_move32(&s,74,0);ui2_emit(&s,(36ULL<<56)|(74ULL<<40)|0x211);ui2_emit(&s,(3ULL<<56)|(1ULL<<16));ui2_emit(&s,34ULL<<56);
 #undef D32
 #undef D64
 *out=s;return 0;
}
