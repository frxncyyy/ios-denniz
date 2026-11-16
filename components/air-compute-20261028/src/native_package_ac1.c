#include "native_package_ac1.h"
#include "compiler_trace_sha256.h"
#include <stdint.h>
#include <string.h>
#define AC1_HASH_OFFSET 240u
static const char *const expected_hex[6]={
 "11c74cc42ce5137b73d44d427f75764f886656d4ddb374422d56b609ed2762d9",
 "b39bb9d68811859ed6f781c46d4c8210a72a53ffda34e2da656de6936520a6be",
 "11172acf267947099622dd8449e415e5fe7b07a1e7e1a938a0986f34ab299d6d",
 "b2272327d404768874364fd8aa15305777b9210392e69a8b60338e267ad7550e",
 "f65b226eb26a30602897f2577b9743916e739df6200b2dab81f45b7e07b7038b",
 "03209c66f0d2181a8b540bcc7d7c3e5e89bc7012f6e856d5c6da426a36dbfab6"};
static uint32_t rd32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t rd64(const uint8_t*p){return (uint64_t)rd32(p)|(uint64_t)rd32(p+4)<<32;}
static int add64(uint64_t a,uint64_t b,uint64_t*r){if(a>UINT64_MAX-b)return 0;*r=a+b;return 1;}
static int span(const void*p,size_t n){return p&&n&&(uintptr_t)p<=UINTPTR_MAX-n;}
static int overlap(const void*a,size_t an,const void*b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<y+bn&&y<x+an;}
static int hex_nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int hash_equal(const uint8_t*b,const char*hex){for(unsigned i=0;i<32;i++){int a=hex_nibble(hex[2*i]),z=hex_nibble(hex[2*i+1]);if(a<0||z<0||b[i]!=(uint8_t)((a<<4)|z))return 0;}return 1;}
static int digest_matches(const void*p,size_t n,const uint8_t expected[32]){char got[65],hex[65];static const char h[]="0123456789abcdef";if(tegu_compiler_trace_sha256(p,n,got))return 0;for(unsigned i=0;i<32;i++){hex[2*i]=h[expected[i]>>4];hex[2*i+1]=h[expected[i]&15];}hex[64]=0;return memcmp(got,hex,65)==0;}
static int exact_stage(const struct tegu_ac1_stage*s,uint32_t bytes,uint32_t stage,uint32_t regs,uint32_t tls,uint32_t fau,uint32_t sys,uint32_t push,uint64_t preload){return s->bytes==bytes&&s->stage==stage&&s->registers==regs&&s->tls==tls&&s->fau_words==fau&&s->sysval_mask==sys&&s->push_bytes==push&&!s->reserved&&s->preload==preload;}
static int exact_resource(const struct tegu_ac1_resource*r,uint32_t role,uint32_t stage,uint32_t binding,uint32_t source,uint32_t index,uint32_t declared,uint32_t push,uint32_t align,uint32_t window){return r->role==role&&r->stage==stage&&!r->set&&r->binding==binding&&r->source_class==source&&r->backend_class==1&&r->table==1&&r->index==index&&r->declared_bytes==declared&&r->offset_push_byte==push&&r->offset_alignment==align&&r->base_alignment==16&&r->window_bytes==window&&r->access==1;}
static int valid_info(const struct tegu_ac1_info*i){return i&&i->version==1&&exact_stage(&i->vs,1792,1,32,0,34,3,8,UINT64_C(0x3800000000000000))&&exact_stage(&i->fs,256,2,32,0,16,8,72,UINT64_C(0x3000000000000000))&&i->resource_count==2&&i->output_count==2&&!i->rt0_mask&&i->rt1_mask==15&&exact_resource(&i->resources[0],1,1,0,1,0,0,0,4,16)&&exact_resource(&i->resources[1],2,1,1,2,1,320,4,16,320)&&hash_equal(i->identity_sha256[0],expected_hex[0])&&hash_equal(i->identity_sha256[1],expected_hex[1])&&hash_equal(i->identity_sha256[2],expected_hex[2])&&hash_equal(i->identity_sha256[3],expected_hex[3])&&hash_equal(i->identity_sha256[4],expected_hex[4])&&hash_equal(i->identity_sha256[5],expected_hex[5]);}
static struct tegu_ac1_stage read_stage(const uint8_t*p,const uint8_t*whole,uint32_t preload_offset){struct tegu_ac1_stage s;s.bytes=rd32(p);s.stage=rd32(p+4);s.registers=rd32(p+8);s.tls=rd32(p+12);s.fau_words=rd32(p+16);s.sysval_mask=rd32(p+20);s.push_bytes=rd32(p+24);s.reserved=rd32(p+28);s.preload=rd64(whole+preload_offset);return s;}
int tegu_ac1_parse(const void*vp,size_t n,struct tegu_ac1_info*out){
 if(!span(vp,n)||!span(out,sizeof(*out))||overlap(vp,n,out,sizeof(*out)))return TEGU_AC1_EINVAL;
 if(n!=TEGU_AC1_PACKAGE_BYTES)return TEGU_AC1_EFORMAT;const uint8_t*p=(const uint8_t *)vp;
 if(memcmp(p,"TAC1",4)||rd32(p+4)!=1||rd32(p+8)!=n||rd32(p+12)!=448||rd32(p+16)!=1792||rd32(p+20)!=2240||rd32(p+24)!=256)return TEGU_AC1_EFORMAT;
 struct tegu_ac1_info t;memset(&t,0,sizeof(t));t.version=1;t.vs_bytes=1792;t.fs_bytes=256;t.vs=read_stage(p+44,p,28);t.fs=read_stage(p+76,p,36);
 t.resource_count=rd32(p+108);t.output_count=rd32(p+112);t.rt0_mask=rd32(p+116);t.rt1_mask=rd32(p+120);if(rd32(p+124))return TEGU_AC1_EFORMAT;
 for(unsigned k=0;k<2;k++){uint32_t v[14];for(unsigned j=0;j<14;j++)v[j]=rd32(p+128+k*56+j*4);t.resources[k]=(struct tegu_ac1_resource){v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13]};}
 for(unsigned k=0;k<6;k++){const uint8_t*h=p+AC1_HASH_OFFSET+k*32;if(!hash_equal(h,expected_hex[k]))return TEGU_AC1_EIDENTITY;memcpy(t.identity_sha256[k],h,32);}
 if(!digest_matches(p+448,1792,t.identity_sha256[4])||!digest_matches(p+2240,256,t.identity_sha256[5]))return TEGU_AC1_EIDENTITY;
 for(size_t j=432;j<448;j++)if(p[j])return TEGU_AC1_EFORMAT;
 if(!valid_info(&t))return TEGU_AC1_EIDENTITY;*out=t;return 0;
}
static int overlap_range(uint64_t aobj,uint64_t a,uint64_t an,uint64_t bobj,uint64_t b,uint64_t bn){uint64_t ae,be;if(aobj!=bobj)return 0;if(!add64(a,an,&ae)||!add64(b,bn,&be))return 1;return a<be&&b<ae;}
static int real_surface(const struct tegu_ac1_surface*s,uint32_t slot,uint32_t w,uint32_t h,uint64_t*range){uint64_t need,end;if(!s||s->kind!=TEGU_AC1_SURFACE_REAL||s->slot!=slot||s->format!=80||!s->token||!s->resolved_object_id||s->width!=w||s->height!=h||s->row_bytes<64||(s->row_bytes&63)||(s->offset&63)||(s->offset&4095)>3072||s->offset>s->resolved_length_bytes||s->resolved_length_bytes-s->offset<1024)return 0;if(!add64((uint64_t)(h-1)*s->row_bytes,(uint64_t)w*4,&need)||need>s->resolved_length_bytes-s->offset||!add64(s->offset,need,&end))return 0;*range=need;return 1;}
static int placeholder_surface(const struct tegu_ac1_surface*s){return s&&s->kind==TEGU_AC1_SURFACE_RESEARCH_PLACEHOLDER&&s->slot==0&&!s->format&&!s->width&&!s->height&&!s->row_bytes&&!s->resolved_length_bytes&&!s->offset&&!s->resolved_object_id&&!s->token;}
static int exact_pass(const struct tegu_ac1_pass_state*p){const struct tegu_ac1_pass_target*a=&p->targets[0],*b=&p->targets[1];
 if(p->render_x||p->render_y||p->render_width!=16||p->render_height!=16||p->sample_count!=1||p->tile_size!=16||p->color_buffer_allocation!=1024||p->render_target_count!=2||p->target_mask!=2||p->draw_write_mask!=2||p->draw_read_mask)return 0;
 if(a->logical_index!=0||a->internal_buffer_offset!=1024||a->write_enable||a->internal_format!=1||a->writeback_format||a->writeback_block_format!=1||a->writeback_msaa||a->swizzle||a->clean_tile_write_enable||a->row_stride||a->surface_stride)return 0;
 for(unsigned k=0;k<4;k++)if(a->clear_color[k])return 0;
 if(a->load_action==TEGU_AC1_LOAD_DONTCARE){if(a->store_action!=TEGU_AC1_STORE_DONTCARE)return 0;}else if(a->load_action==TEGU_AC1_LOAD){if(a->store_action!=TEGU_AC1_STORE)return 0;}else return 0;
 if(b->logical_index!=1||b->internal_buffer_offset||!b->write_enable||b->internal_format!=1||b->writeback_format!=19||b->writeback_block_format!=2||b->writeback_msaa||b->swizzle!=0x60a||!b->clean_tile_write_enable||b->row_stride!=64||b->surface_stride!=1024||b->store_action!=TEGU_AC1_STORE)return 0;
 if(b->load_action==TEGU_AC1_LOAD_CLEAR){for(unsigned k=0;k<4;k++)if(b->clear_color[k]!=0x11223344)return 0;}else if(b->load_action==TEGU_AC1_LOAD_DONTCARE){for(unsigned k=0;k<4;k++)if(b->clear_color[k])return 0;}else return 0;
 return !p->viewport_x&&!p->viewport_y&&p->viewport_width==16&&p->viewport_height==16&&!p->scissor_x&&!p->scissor_y&&p->scissor_width==16&&p->scissor_height==16&&p->sample_mask==0xffff&&!p->cull_enabled&&p->fill_mode==0&&p->rasterization_enabled&&!p->alpha_to_coverage;
}
static int same_frontend(const struct tegu_ac1_info*i,const struct tegu_ac1_frontend*f){static const char vsname[]="path_blit_vert_lph",fsname[]="attachment_clear_frag_lph";return !memcmp(f->vs_entry,vsname,sizeof(vsname))&&!memcmp(f->fs_entry,fsname,sizeof(fsname))&&!memcmp(&f->vs,&i->vs,sizeof(i->vs))&&!memcmp(&f->fs,&i->fs,sizeof(i->fs))&&f->plan.version==2&&f->plan.output_count==2&&!f->plan.rt0_mask&&f->plan.rt1_mask==15&&f->plan.position_offset==896&&f->plan.secondary_offset==1792&&!f->plan.secondary_enabled&&!f->plan.generic_size_bytes&&f->plan.draw_vertex_count==3&&f->plan.draw_instance_count==1&&!f->plan.draw_base_instance&&!f->plan.draw_indexed&&f->plan.target0_format==80&&f->plan.target1_format==80&&!f->plan.target0_blend_enabled&&!f->plan.target1_blend_enabled&&f->plan.target_descriptors_seen==3&&f->plan.resource_count==2&&!memcmp(f->plan.resources,i->resources,sizeof(i->resources))&&f->targets[0].format==80&&!f->targets[0].write_mask&&!f->targets[0].blend_enabled&&f->targets[1].format==80&&f->targets[1].write_mask==15&&!f->targets[1].blend_enabled&&f->target_count==2&&f->sample_count==1&&!f->depth_enabled&&f->metal_vertex_attribute_count==4&&!f->compiler_vertex_attribute_reads&&!f->fs_input_count&&!f->fs_resource_count&&f->fs_output_mask==3&&f->fs_output0_float4&&!f->fs_output0_index&&f->fs_output1_float4&&!f->fs_output1_index&&f->rect_slot==1&&f->rect_push_byte==0&&f->matrix_slot==2&&f->matrix_push_byte==4&&f->fs_push_span==72&&f->color_rule0_byte==64&&f->color_rule0_value==8738&&f->color_rule1_byte==68&&f->color_rule1_value==8738&&f->topology==TEGU_AC1_TOPOLOGY_TRIANGLE_LIST&&!f->indexed&&!f->vertex_start&&f->vertex_count==3&&f->instance_count==1&&!f->base_instance&&!memcmp(f->metallib_sha256,i->identity_sha256[0],32)&&!memcmp(f->profile_sha256,i->identity_sha256[1],32)&&!memcmp(f->vs_spirv_sha256,i->identity_sha256[2],32)&&!memcmp(f->fs_spirv_sha256,i->identity_sha256[3],32);}
static uint32_t round16(uint64_t n){return (uint32_t)((n+15)&~UINT64_C(15));}
int tegu_ac1_bind(const struct tegu_ac1_info*i,const struct tegu_ac1_bind_request*r,struct tegu_ac1_binding*out){
 if(!span(i,sizeof(*i))||!span(r,sizeof(*r))||!span(out,sizeof(*out))||overlap(i,sizeof(*i),out,sizeof(*out))||overlap(r,sizeof(*r),out,sizeof(*out))||overlap(i,sizeof(*i),r,sizeof(*r)))return TEGU_AC1_EALIAS;
 if(!valid_info(i))return TEGU_AC1_EIDENTITY;
 if(r->operation!=TEGU_AC1_OPERATION_DRAW)return TEGU_AC1_ECONTRACT;
 if(r->width!=16||r->height!=16||!same_frontend(i,&r->frontend)||!exact_pass(&r->pass))return TEGU_AC1_ECONTRACT;
 uint64_t rtrange[2]={0,0};int rt0real=real_surface(&r->rt[0],0,r->width,r->height,&rtrange[0]);
 if(r->mode==TEGU_AC1_MODE_PRODUCTION){if(r->research_opt_in||!rt0real||!real_surface(&r->rt[1],1,r->width,r->height,&rtrange[1]))return TEGU_AC1_ECONTRACT;}
 else if(r->mode==TEGU_AC1_MODE_RESEARCH_RT0_PLACEHOLDER){if(r->research_opt_in!=1||!placeholder_surface(&r->rt[0])||!real_surface(&r->rt[1],1,r->width,r->height,&rtrange[1]))return TEGU_AC1_ECONTRACT;}
 else return TEGU_AC1_ECONTRACT;
 if(r->rt[1].row_bytes!=r->pass.targets[1].row_stride)return TEGU_AC1_ECONTRACT;
 if(rt0real&&overlap_range(r->rt[0].resolved_object_id,r->rt[0].offset,rtrange[0],r->rt[1].resolved_object_id,r->rt[1].offset,rtrange[1]))return TEGU_AC1_EALIAS;
 if(!r->rect.token||!r->matrix.token||!r->rect.resolved_object_id||!r->matrix.resolved_object_id||r->rect.slot!=r->frontend.rect_slot||r->rect.push_byte!=r->frontend.rect_push_byte||r->matrix.slot!=r->frontend.matrix_slot||r->matrix.push_byte!=r->frontend.matrix_push_byte||r->rect.slot!=1||r->rect.push_byte!=0||r->matrix.slot!=2||r->matrix.push_byte!=4||r->rect.offset%4||r->matrix.offset%16)return TEGU_AC1_ERANGE;
 if(r->rect.token==r->matrix.token||r->rect.token==r->rt[1].token||r->matrix.token==r->rt[1].token||(rt0real&&(r->rect.token==r->rt[0].token||r->matrix.token==r->rt[0].token)))return TEGU_AC1_EALIAS;
 uint64_t rectbase=r->rect.offset&~UINT64_C(15),matrixbase=r->matrix.offset&~UINT64_C(15);
 uint32_t rectres=(uint32_t)(r->rect.offset&15),matrixres=(uint32_t)(r->matrix.offset&15);
 uint32_t rectbytes=round16((uint64_t)rectres+16),matrixbytes=round16((uint64_t)matrixres+320);
 uint64_t rectend,matrixend;if(!add64(rectbase,rectbytes,&rectend)||!add64(matrixbase,matrixbytes,&matrixend)||rectend>r->rect.resolved_length_bytes||matrixend>r->matrix.resolved_length_bytes)return TEGU_AC1_ERANGE;
 if(overlap_range(r->rect.resolved_object_id,rectbase,rectbytes,r->matrix.resolved_object_id,matrixbase,matrixbytes))return TEGU_AC1_EALIAS;
 if(overlap_range(r->rt[1].resolved_object_id,r->rt[1].offset,rtrange[1],r->rect.resolved_object_id,rectbase,rectbytes)||overlap_range(r->rt[1].resolved_object_id,r->rt[1].offset,rtrange[1],r->matrix.resolved_object_id,matrixbase,matrixbytes))return TEGU_AC1_EALIAS;
 if(rt0real&&(overlap_range(r->rt[0].resolved_object_id,r->rt[0].offset,rtrange[0],r->rect.resolved_object_id,rectbase,rectbytes)||overlap_range(r->rt[0].resolved_object_id,r->rt[0].offset,rtrange[0],r->matrix.resolved_object_id,matrixbase,matrixbytes)))return TEGU_AC1_EALIAS;
 int zero_offsets=(r->rect.offset==0&&r->matrix.offset==0);if(r->mode==TEGU_AC1_MODE_PRODUCTION&&!zero_offsets)return TEGU_AC1_ERANGE;
 struct tegu_ac1_binding t;memset(&t,0,sizeof(t));t.mode=r->mode;t.width=16;t.height=16;t.render_x=r->pass.render_x;t.render_y=r->pass.render_y;t.render_width=r->pass.render_width;t.render_height=r->pass.render_height;t.sample_count=r->pass.sample_count;t.tile_size=r->pass.tile_size;t.color_buffer_allocation=r->pass.color_buffer_allocation;t.rt_count=r->pass.render_target_count;t.target_mask=2;t.draw_write_mask=2;t.draw_read_mask=0;
 t.rt0_token=r->rt[0].token;t.rt1_token=r->rt[1].token;t.rect_token=r->rect.token;t.matrix_token=r->matrix.token;t.rect_descriptor_offset=rectbase;t.matrix_descriptor_offset=matrixbase;t.rect_descriptor_bytes=rectbytes;t.matrix_descriptor_bytes=matrixbytes;t.rect_push_residual=rectres;t.matrix_push_residual=matrixres;t.offsets_r8_qualified=zero_offsets;t.rect_fau_byte=128;t.matrix_fau_byte=132;
 t.rt0_format=80;t.rt1_format=80;t.rt0_mask=0;t.rt1_mask=15;t.blend_descriptor[0]=UINT64_C(0x0000000000000003);t.blend_descriptor[1]=UINT64_C(0x000ed00000010019);
 t.vs_table=1;t.vs_table_entries=2;t.fs_table_entries=0;t.vs_position_offset=4096+896;t.vs_secondary_enabled=0;t.vary_size=0;t.fs_fau_words=16;t.fs_push_span=72;
 t.viewport[2]=16;t.viewport[3]=16;t.scissor[2]=16;t.scissor[3]=16;t.sample_mask=0xffff;t.cull_enabled=0;t.fill_mode=0;t.rasterization_enabled=1;t.alpha_to_coverage=0;
 t.draw_topology=TEGU_AC1_TOPOLOGY_TRIANGLE_LIST;t.draw_vertex_start=0;t.draw_vertex_count=3;t.draw_instance_count=1;t.draw_indexed=0;t.draw_base_instance=0;
 t.pass_targets[0]=(struct tegu_ac1_bound_target){0};t.pass_targets[1]=(struct tegu_ac1_bound_target){0};
 for(unsigned k=0;k<2;k++){const struct tegu_ac1_pass_target*p=&r->pass.targets[k];struct tegu_ac1_bound_target*b=&t.pass_targets[k];b->logical_index=p->logical_index;b->internal_buffer_offset=p->internal_buffer_offset;b->write_enable=p->write_enable;b->internal_format=p->internal_format;b->writeback_format=p->writeback_format;b->writeback_block_format=p->writeback_block_format;b->writeback_msaa=p->writeback_msaa;b->swizzle=p->swizzle;b->clean_tile_write_enable=p->clean_tile_write_enable;b->row_stride=p->row_stride;b->surface_stride=p->surface_stride;b->load_action=p->load_action;b->store_action=p->store_action;memcpy(b->clear_color,p->clear_color,sizeof(b->clear_color));}
 t.pass_targets[0].token=r->rt[0].token;t.pass_targets[0].object_id=r->rt[0].resolved_object_id;t.pass_targets[0].offset=r->rt[0].offset;t.pass_targets[0].length=r->rt[0].resolved_length_bytes;
 t.pass_targets[1].token=r->rt[1].token;t.pass_targets[1].object_id=r->rt[1].resolved_object_id;t.pass_targets[1].offset=r->rt[1].offset;t.pass_targets[1].length=r->rt[1].resolved_length_bytes;
 *out=t;return TEGU_AC1_OK;
}
int tegu_ac1_copy_code(const struct tegu_ac1_info*i,const void*pkg,size_t n,void*vd,size_t vc,void*fd,size_t fc){
 if(!span(i,sizeof(*i))||!span(pkg,n)||!span(vd,vc)||!span(fd,fc)||overlap(i,sizeof(*i),vd,vc)||overlap(i,sizeof(*i),fd,fc)||overlap(pkg,n,vd,vc)||overlap(pkg,n,fd,fc)||overlap(vd,vc,fd,fc))return TEGU_AC1_EALIAS;
 if(!valid_info(i))return TEGU_AC1_EIDENTITY;if(n!=TEGU_AC1_PACKAGE_BYTES||vc<1792||fc<256)return TEGU_AC1_ERANGE;
 struct tegu_ac1_info parsed;int rc=tegu_ac1_parse(pkg,n,&parsed);if(rc)return rc;if(memcmp(parsed.identity_sha256,i->identity_sha256,sizeof(parsed.identity_sha256)))return TEGU_AC1_EIDENTITY;
 uint8_t vs[1792],fs[256];memcpy(vs,(const uint8_t*)pkg+448,sizeof(vs));memcpy(fs,(const uint8_t*)pkg+2240,sizeof(fs));
 if(!digest_matches(vs,sizeof(vs),i->identity_sha256[4])||!digest_matches(fs,sizeof(fs),i->identity_sha256[5]))return TEGU_AC1_EIDENTITY;
 memcpy(vd,vs,sizeof(vs));memcpy(fd,fs,sizeof(fs));return TEGU_AC1_OK;
}
int tegu_ac1_layout_build(const struct tegu_ac1_info*i,uint32_t cap,struct tegu_ac1_layout*out){if(!span(i,sizeof(*i))||!span(out,sizeof(*out))||overlap(i,sizeof(*i),out,sizeof(*out)))return TEGU_AC1_EALIAS;if(!valid_info(i))return TEGU_AC1_EIDENTITY;struct tegu_ac1_layout t={4096,6144,8192};if(cap<t.total_bytes)return TEGU_AC1_ERANGE;*out=t;return TEGU_AC1_OK;}
