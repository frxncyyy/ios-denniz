#include "fixed_ui_resources.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p,uint32_t x) {
 p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
static uint32_t get32(const uint8_t *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return get32(p)|((uint64_t)get32(p+4)<<32);}

static struct tegu_ui1_binding valid_binding(void) {
 struct tegu_ui1_binding b;memset(&b,0,sizeof(b));
 b.resources[0]=(struct tegu_ui1_bound_resource){9,10,0,96,64,1,2,1,0,0,19};
 b.resources[1]=(struct tegu_ui1_bound_resource){9,10,256,192,168,2,1,1,1,64,21};
 put32(b.vertex_push,19);put32(b.fragment_push,21);put32(b.fragment_push+4,0xaabbccddu);
 memcpy(b.constant,(const uint8_t[]){0,126,0,126,0,126,0,126},8);
 b.constant_stage=TEGU_FIXED_UI_PUSH_STAGE_FRAGMENT;return b;
}
static struct tegu_fixed_ui_resource_input valid(const struct tegu_ui1_binding *b) {
 struct tegu_fixed_ui_resource_input in;memset(&in,0,sizeof(in));
 in.version=TEGU_FIXED_UI_STATE_VERSION;in.binding=b;
 in.table0=(struct tegu_fixed_ui_span){0x400000,256};
 in.resource_table=(struct tegu_fixed_ui_span){0x500000,64};
 in.descriptors=(struct tegu_fixed_ui_span){0x510000,128};
 /* Both resources resolve to one allocation, at distinct valid windows. */
 in.vertex_buffer=(struct tegu_fixed_ui_allocation_span){9,10,0x520000,512};
 in.fragment_buffer=in.vertex_buffer;return in;
}
static void refuses(struct tegu_fixed_ui_resource_input in) {
 struct tegu_fixed_ui_resource_state out,before;memset(&out,0xa5,sizeof(out));before=out;
 assert(tegu_fixed_ui_resource_state_build(&in,&out)<0);assert(!memcmp(&out,&before,sizeof(out)));
}
static void refuses_binding(struct tegu_ui1_binding b) {
 struct tegu_fixed_ui_resource_input in=valid(&b);refuses(in);
}
int main(void) {
 struct tegu_ui1_binding b=valid_binding();struct tegu_fixed_ui_resource_input in=valid(&b);
 struct tegu_fixed_ui_resource_state out;
 assert(tegu_fixed_ui_resource_state_build(&in,&out)==0);
 assert(out.resource_table_handle==0x500004&&out.vertex_binding==0&&out.fragment_binding==1);
 assert(out.vertex_resource_stage==1&&out.fragment_resource_stage==2);
 assert(out.vertex_push_stage==1&&out.fragment_push_stage==16);
 assert(out.vertex_push_offset==0&&out.vertex_push_length==4);
 assert(out.fragment_push_offset==64&&out.fragment_push_length==8);
 assert((get64(out.resource_table)&((1ull<<56)-1))==0x400000&&get64(out.resource_table+8)==256);
 assert((get64(out.resource_table+16)&((1ull<<56)-1))==0x510000&&get64(out.resource_table+24)==128);
 assert(get32(out.descriptors)==0x19&&get32(out.descriptors+4)==96&&get64(out.descriptors+8)==0x520000);
 assert(get32(out.descriptors+32)==0x19&&get32(out.descriptors+36)==192&&get64(out.descriptors+40)==0x520100);
 for(unsigned i=16;i<32;i++)assert(!out.descriptors[i]);
 for(unsigned i=48;i<64;i++)assert(!out.descriptors[i]);
 assert(get32(out.descriptors+64)==0x19&&get32(out.descriptors+68)==16&&get64(out.descriptors+72)==0x510060);
 for(unsigned i=80;i<96;i++)assert(!out.descriptors[i]);
 assert(!memcmp(out.descriptors+96,b.constant,8));
 for(unsigned i=104;i<128;i++)assert(!out.descriptors[i]);
 uint8_t pushes[12];memcpy(pushes,b.vertex_push,4);memcpy(pushes+4,b.fragment_push,8);
 assert(!memcmp(out.push_bytes,pushes,12));assert(!memcmp(out.constant_blob,b.constant,8));

 /* Exact live BackBoard allocation: VS buffer2 begins 176 bytes into a
  * 240-byte descriptor and FS buffer1 uses 168 of 176 bytes. Descriptor
  * bases are 32-byte aligned; their bounded lengths need not be. */
 struct tegu_ui1_binding live=b;
 live.resources[0].descriptor_bytes=240;live.resources[0].push_residual=176;
 live.resources[1].descriptor_offset=0;live.resources[1].descriptor_bytes=176;
 live.resources[1].push_residual=0;
 put32(live.vertex_push,176);put32(live.fragment_push,0);
 struct tegu_fixed_ui_resource_input live_in=valid(&live);
 assert(!tegu_fixed_ui_resource_state_build(&live_in,&out));
 assert(get32(out.descriptors+4)==240&&get64(out.descriptors+8)==0x520000);
 assert(get32(out.descriptors+36)==176&&get64(out.descriptors+40)==0x520000);

 struct tegu_ui1_binding bad=b;bad.resources[1].metal_stage=16;refuses_binding(bad);
 bad=b;bad.resources[0].metal_stage=2;refuses_binding(bad);
 bad=b;bad.resources[0].descriptor_bytes=80;refuses_binding(bad);
 bad=b;bad.resources[0].descriptor_offset=16;bad.resources[0].descriptor_bytes=96;
 put32(bad.vertex_push,3);bad.resources[0].push_residual=3;refuses_binding(bad);
 bad=b;bad.resources[1].descriptor_offset=384;refuses_binding(bad);
 bad=b;bad.resources[0].window_bytes=63;refuses_binding(bad);
 bad=b;bad.resources[1].push_offset=68;refuses_binding(bad);
 bad=b;bad.resources[0].push_residual=18;refuses_binding(bad);
 bad=b;bad.resources[1].token=11;refuses_binding(bad);
 bad=b;bad.resources[0].object_id=12;refuses_binding(bad);
 bad=b;bad.constant_stage=2;refuses_binding(bad);

 struct tegu_fixed_ui_resource_input invalid=in;invalid.descriptors.address=invalid.vertex_buffer.address;refuses(invalid);
 invalid=in;invalid.resource_table.address=invalid.fragment_buffer.address+64;refuses(invalid);
 invalid=in;invalid.descriptors.address=invalid.resource_table.address;refuses(invalid);
 invalid=in;invalid.vertex_buffer.bytes=95;refuses(invalid);
 invalid=in;invalid.vertex_buffer.token=0;refuses(invalid);
 invalid=in;invalid.fragment_buffer.address+=0x1000;refuses(invalid);
 invalid=in;invalid.version=1;refuses(invalid);
 invalid=in;invalid.binding=NULL;refuses(invalid);

 union {struct tegu_fixed_ui_resource_input in;struct tegu_fixed_ui_resource_state out;} alias;
 memset(&alias,0x5a,sizeof(alias));alias.in.version=TEGU_FIXED_UI_STATE_VERSION;alias.in.binding=&b;
 assert(tegu_fixed_ui_resource_state_build(&alias.in,&alias.out)<0);
 union {struct tegu_ui1_binding binding;struct tegu_fixed_ui_resource_state out;} binding_alias;
 memset(&binding_alias,0x5a,sizeof(binding_alias));invalid=in;invalid.binding=&binding_alias.binding;
 assert(tegu_fixed_ui_resource_state_build(&invalid,&binding_alias.out)<0);

 puts("fixed-ui-resources: validated UI1 binding, aliased allocation and stage namespaces PASS");
 return 0;
}
