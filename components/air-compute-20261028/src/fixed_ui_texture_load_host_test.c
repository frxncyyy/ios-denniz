#include "fixed_ui_texture_render_state.h"
#include "compositor_load_assets_v6.h"
#include "compositor_load_bundle_v6.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t reg(const struct tegu_render_state *s,unsigned wanted){uint32_t value=0;int found=0;for(unsigned n=0;n<s->command_count;n++){uint64_t c=s->commands[n];if((c>>56)==2&&((c>>48)&255)==wanted){value=(uint32_t)c;found=1;}}assert(found);return value;}

int main(void)
{
 struct tegu_ui2_info i={0};i.version=2;i.target_format=80;i.target_write_mask=15;i.target_blend_enabled=1;i.sample_count=1;i.topology=3;
 i.stages[0].bytes=4096;i.stages[0].registers=32;i.stages[0].fau_words=34;i.stages[0].preload=UINT64_C(0x3800000000000000);
 i.stages[1].bytes=1152;i.stages[1].registers=32;i.stages[1].fau_words=51;i.stages[1].preload=UINT64_C(0x3000000000000000);
 i.pair.position_offset=1792;i.pair.secondary_offset=3584;i.pair.generic_size_bytes=8;
 i.attributes[0]=(struct tegu_ui2_attribute){0,109,0,1,48,0,1};i.attributes[1]=(struct tegu_ui2_attribute){1,103,16,1,48,0,1};i.attributes[2]=(struct tegu_ui2_attribute){2,97,32,1,48,0,1};
 struct tegu_ui2_binding b={0};b.width=1125;b.height=2436;b.target_format=80;b.target_row_bytes=4608;b.load_action=1;b.store_action=1;b.vertex_stride=48;b.topology=3;b.vertex_count=6;b.instance_count=1;
 uint32_t full[4]={0,0,1125,2436};memcpy(b.render,full,sizeof(full));memcpy(b.viewport,full,sizeof(full));memcpy(b.scissor,full,sizeof(full));
 const uint64_t surface=UINT64_C(4608)*2436;
 struct tegu_render_addresses a={.descriptors=0x06000000,.tiler=0x06001000,.geometry=0x06100000,.chunk=0x09000000,.target=0x07000000,.vertex_code=0x06200000,.fragment_code=0x06202000,.chunk_bytes=0x200000,.hierarchy_mask=0x82};
 struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,1125,2436,4608,surface};
 struct tegu_render_state clear={0};assert(!tegu_fixed_ui_texture_render_build(&i,&b,&a,0x08000004,&l,&clear));
 assert(reg(&clear,33)==6&&reg(&clear,57)==2);
 assert(tegu_render_word(clear.descriptors+128)==(1u<<26));
 assert(tegu_render_word(clear.descriptors+132)==(1u|(19u<<3)|(2u<<8)|(0x60au<<16)|(1u<<31)));
 unsigned char *resource=calloc(1,4096),*dcd=calloc(1,4096),*shader=calloc(1,4096);assert(resource&&dcd&&shader);
 struct tegu_compositor_load_asset_scratch_v6 scratch={0};struct tegu_compositor_load_assets_v6 assets={0};
 assert(!tegu_compositor_load_assets_build_v6(&l,a.target,resource,dcd,shader,&scratch,&assets));
 struct tegu_load_mapping m={.resource_va=0x0c000000,.dcd_va=0x0c001000,.shader_va=0x0600e000,.target_pages=(uint32_t)((surface+4095)/4096),.total_pages=(uint32_t)((surface+4095)/4096+2),.page_role={TEGU_LOAD_MAP_RO_NX,TEGU_LOAD_MAP_RO_NX,TEGU_LOAD_MAP_RO_X}};
 struct tegu_compositor_load_bundle_v6 *bundle=calloc(1,sizeof(*bundle));assert(bundle);
 assert(!tegu_compositor_load_bundle_build_v6(&clear,&a,&l,&m,&assets,4096,1152,bundle));
 assert(bundle->page_count==2&&bundle->draw.command_count==clear.command_count+1);
 assert(bundle->draw.commands[0]==((3ull<<56)|(0xffull<<16)));
 unsigned clears=0;for(unsigned n=0;n<bundle->draw.command_count;n++)clears+=bundle->draw.commands[n]==((49ull<<56)|(1ull<<32));assert(!clears);
 b.load_action=3;struct tegu_render_state before;memset(&before,0xa5,sizeof(before));struct tegu_render_state bad=before;
 assert(tegu_fixed_ui_texture_render_build(&i,&b,&a,0x08000004,&l,&bad)&&!memcmp(&before,&bad,sizeof(bad)));
 free(bundle);free(resource);free(dcd);free(shader);puts("PASS UI2 LOAD state and qualified preload bundle");return 0;
}
