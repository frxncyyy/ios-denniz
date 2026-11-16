#include "fixed_ui_render_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t word(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t first_reg(const struct tegu_render_state *s,unsigned wanted){for(unsigned n=0;n<s->command_count;n++){uint64_t c=s->commands[n];if((c>>56)==2&&((c>>48)&255)==wanted)return (uint32_t)c;}assert(0);return 0;}
static uint32_t reg(const struct tegu_render_state *s,unsigned wanted){uint32_t value=0;int found=0;for(unsigned n=0;n<s->command_count;n++){uint64_t c=s->commands[n];if((c>>56)==2&&((c>>48)&255)==wanted){value=(uint32_t)c;found=1;}}assert(found);return value;}
static void fixture(struct tegu_ui1_info *i,struct tegu_ui1_binding *b){
 memset(i,0,sizeof(*i));memset(b,0,sizeof(*b));i->version=1;i->target_format=80;i->target_write_mask=15;i->sample_count=1;i->topology=3;
 i->stages[0]=(struct tegu_ui1_stage){4096,768,1,32,0,33,3,4,UINT64_C(0x3800000000000000)};
 i->stages[1]=(struct tegu_ui1_stage){1024,4864,2,32,0,50,8,72,UINT64_C(0x3000000000000000)};
 i->pair.position_offset=1792;i->pair.secondary_offset=3584;i->pair.secondary_preload=UINT64_C(0x3800000000000000);i->pair.generic_size_bytes=16;
 i->attributes[0]=(struct tegu_ui1_attribute){0,109,0,1,48,0,1};i->attributes[1]=(struct tegu_ui1_attribute){1,97,32,1,48,0,1};
 b->width=1080;b->height=2424;b->target_format=80;b->target_row_bytes=4352;b->load_action=2;b->store_action=1;b->vertex_stride=48;b->topology=3;b->vertex_count=6;b->instance_count=1;
 b->render[2]=b->viewport[2]=b->scissor[2]=b->width;b->render[3]=b->viewport[3]=b->scissor[3]=b->height;
 b->clear_color[0]=0x44332211;memcpy(b->vertex_push,(uint8_t[]){19,0,0,0},4);memcpy(b->fragment_push,(uint8_t[]){21,0,0,0,0x10,0x32,0x54,0x76},8);
}
static void refuses(const struct tegu_ui1_info *i,const struct tegu_ui1_binding *b,const struct tegu_render_addresses *a,uint64_t table,const struct tegu_render_target_layout *l){struct tegu_render_state s,before;memset(&s,0xa5,sizeof(s));before=s;assert(tegu_fixed_ui_render_build(i,b,a,table,l,&s)<0);assert(!memcmp(&s,&before,sizeof(s)));}
int main(void){
 struct tegu_ui1_info i;struct tegu_ui1_binding b;fixture(&i,&b);
 struct tegu_render_addresses a={.descriptors=0x100000,.tiler=0x110000,.geometry=0x120000,.chunk=0x200000,.target=0x500000,.vertex_code=0x1000000,.fragment_code=0x1100000,.clear_rgba=0x44332211,.chunk_bytes=0x200000,.hierarchy_mask=0x82};
 struct tegu_render_target_layout l={1,1080,2424,4352,(uint64_t)4352*2424};struct tegu_render_state s;
 assert(!tegu_fixed_ui_render_build(&i,&b,&a,0x1200004,&l,&s));
 assert(s.command_count&&s.command_count<=128&&word(s.descriptors+168)==4352&&word(s.descriptors+172)==4352u*2424u);
 assert(word(s.descriptors+128)==(1u<<26));
 assert(word(s.descriptors+132)==(1u|(19u<<3)|(2u<<8)|(0x60au<<16)|(1u<<31)));
 assert(reg(&s,33)==6&&reg(&s,57)==3);
 assert(word(s.descriptors+0xa80)==19&&word(s.descriptors+0xb80+64)==21&&word(s.descriptors+0xb80+68)==0x76543210);
 i.target_blend_enabled=1;assert(!tegu_fixed_ui_render_build(&i,&b,&a,0x1200004,&l,&s)&&reg(&s,57)==2);i.target_blend_enabled=0;
 /* Metal NDC +Y is the top row: the viewport Y scale is negated, offsets stay positive. */
 assert(word(s.descriptors+0xa04)==(word(s.descriptors+0xa14)|0x80000000u)&&!(word(s.descriptors+0xa00)>>31));
 assert(first_reg(&s,42)==0&&first_reg(&s,43)==(1079u|(2423u<<16)));
 struct tegu_ui1_binding bad=b;bad.load_action=1;assert(!tegu_fixed_ui_render_build(&i,&bad,&a,0x1200004,&l,&s));
 bad=b;bad.scissor[0]=10;bad.scissor[1]=20;bad.scissor[2]=100;bad.scissor[3]=50;
 assert(!tegu_fixed_ui_render_build(&i,&bad,&a,0x1200004,&l,&s)&&first_reg(&s,42)==(10u|(20u<<16))&&first_reg(&s,43)==(109u|(69u<<16)));
 bad.scissor[0]=1075;bad.scissor[2]=10;refuses(&i,&bad,&a,0x1200004,&l);bad.scissor[0]=0;bad.scissor[2]=0;refuses(&i,&bad,&a,0x1200004,&l);
 {const uint32_t good[3]={12,54,384},badn[4]={0,4,387,768};
  for(unsigned k=0;k<3;k++){bad=b;bad.vertex_count=good[k];assert(!tegu_fixed_ui_render_build(&i,&bad,&a,0x1200004,&l,&s)&&reg(&s,33)==good[k]);}
  for(unsigned k=0;k<4;k++){bad=b;bad.vertex_count=badn[k];refuses(&i,&bad,&a,0x1200004,&l);}}
 bad=b;bad.viewport[2]--;refuses(&i,&bad,&a,0x1200004,&l);
 struct tegu_ui1_info bi=i;bi.attributes[1].format=103;refuses(&bi,&b,&a,0x1200004,&l);
 struct tegu_render_addresses ba=a;ba.fragment_code=a.vertex_code;refuses(&i,&b,&ba,0x1200004,&l);
 puts("PASS fixed UI BGRA native render state: Metal Y-down viewport, scissor box, LOAD, 3..384 vertices, exact pushes and refusal immutability");return 0;
}
