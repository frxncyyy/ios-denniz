#include "compositor_resource_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct tegu_compositor_resource_input valid(void){
 struct tegu_compositor_resource_input i={0};i.version=1;
 i.table0=(struct tegu_gpu_span){0x400000,256};
 i.resource_table=(struct tegu_gpu_span){0x500000,64};
 i.descriptors=(struct tegu_gpu_span){0x510000,192};
 i.plane=(struct tegu_gpu_span){0x520000,32};i.texels=(struct tegu_gpu_span){0x530000,1024};
 uint64_t sizes[4]={64,16,80,16};for(unsigned n=0;n<4;n++)i.ubos[n]=(struct tegu_gpu_span){0x600000+0x1000*n,sizes[n]};return i;
}
static void refusal(struct tegu_compositor_resource_input *i){
 struct tegu_compositor_resource_state out,before;memset(&out,0xa7,sizeof(out));before=out;
 assert(tegu_compositor_resource_state_build(i,&out)<0);assert(!memcmp(&out,&before,sizeof(out)));
}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(int argc,char **argv){assert(argc==4);FILE *f=fopen(argv[1],"rb");assert(f);unsigned char golden[64+192+32];assert(fread(golden,1,sizeof(golden),f)==sizeof(golden));assert(fgetc(f)==EOF);fclose(f);
 struct tegu_compositor_resource_input i=valid();struct tegu_compositor_resource_state out;memset(&out,0xcc,sizeof(out));
 assert(tegu_compositor_resource_state_build(&i,&out)==0);assert(out.resource_table_handle==0x500004);
 assert(!memcmp(out.resource_table,golden,64));assert(!memcmp(out.descriptors,golden+64,192));assert(!memcmp(out.plane,golden+256,32));
 unsigned char producer_texture[32],producer_plane[32];
 f=fopen(argv[2],"rb");assert(f&&fread(producer_texture,1,32,f)==32&&fgetc(f)==EOF);fclose(f);
 f=fopen(argv[3],"rb");assert(f&&fread(producer_plane,1,32,f)==32&&fgetc(f)==EOF);fclose(f);
 /* The pinned Mesa pan_sampled_texture_emit_v10 output is the source oracle.
  * Only relocate the plane pointer and texel pointer to this test's spans. */
 for(unsigned k=16;k<24;k++)producer_texture[k]=out.descriptors[128+k];
 for(unsigned k=8;k<16;k++)producer_plane[k]=out.plane[k];
 assert(!memcmp(out.descriptors+128,producer_texture,32));
 assert(!memcmp(out.plane,producer_plane,32));
 /* Entries 2/3 are the producer's zero-filled resource-table alignment pad. */
 assert(out.resource_table[32]==0&&out.resource_table[48]==0);
 /* Buffers 0..3 occupy set0 binding slots 0..3; texture4 then sampler5. */
 for(unsigned n=0;n<4;n++)assert(out.descriptors[n*32]==0x19);
 assert(out.descriptors[128]!=0&&out.descriptors[160]==0x01);
 struct tegu_compositor_resource_input bad=i;bad.version=0;refusal(&bad);
 bad=i;bad.ubos[2].bytes=79;refusal(&bad);
 bad=i;bad.ubos[2].bytes=81;refusal(&bad);
 bad=i;bad.ubos[1].address++;refusal(&bad);
 bad=i;bad.ubos[0].address=bad.descriptors.address;refusal(&bad);
 bad=i;bad.plane.address=bad.texels.address;refusal(&bad);
 bad=i;bad.resource_table.address=UINT64_MAX-31;refusal(&bad);
 bad=i;bad.ubos[3].address=(1ull<<48)-8;refusal(&bad);
 bad=i;bad.descriptors.bytes=224;refusal(&bad);
 /* Reject partial overlap too, for every emitted range against each live
  * input, without altering any output byte. This protects future mapping
  * consumers from descriptor writes destroying a retained resource. */
 for(unsigned output=0;output<3;output++)for(unsigned input=0;input<6;input++) {
  bad=i;
  struct tegu_gpu_span *outputs[]={&bad.resource_table,&bad.descriptors,&bad.plane};
  struct tegu_gpu_span *inputs[]={&bad.table0,&bad.ubos[0],&bad.ubos[1],&bad.ubos[2],&bad.ubos[3],&bad.texels};
  outputs[output]->address=inputs[input]->address;
  refusal(&bad);
 }
 for(unsigned input=0;input<4;input++) {
  bad=i;bad.ubos[input].address=(1ull<<48)-16;bad.ubos[input].bytes=32;refusal(&bad);
  bad=i;bad.ubos[input].bytes=(uint64_t)UINT32_MAX+1;refusal(&bad);
 }
 bad=i;bad.texels.bytes=1023;refusal(&bad);
 bad=i;bad.texels.bytes=4097;refusal(&bad);
 bad=i;bad.texels.address=(1ull<<48)-4096;bad.texels.bytes=4096;
 assert(tegu_compositor_resource_state_build(&bad,&out)==0);
 bad=i;bad.table0.bytes=0;struct tegu_compositor_resource_state zero;assert(tegu_compositor_resource_state_build(&bad,&zero)==0);assert(zero.resource_table[0]==0);
 union { struct tegu_compositor_resource_input in; struct tegu_compositor_resource_state out; unsigned char raw[sizeof(struct tegu_compositor_resource_state)]; } alias;
 memset(&alias,0x5a,sizeof(alias));unsigned char snapshot[sizeof(alias)];memcpy(snapshot,&alias,sizeof(alias));
 assert(tegu_compositor_resource_state_build(&alias.in,&alias.out)<0);assert(!memcmp(snapshot,&alias,sizeof(alias)));
 /* B3's packer remains dimension-safe in isolation; selector16 separately
  * admits only the captured 776x24 level-0 view. */
 struct tegu_compositor_resource_input b3=valid();b3.ubos[2].bytes=96;
 assert(!tegu_compositor_resource_state_build_b3(&b3,16,16,64,&out));
 assert(get32(out.descriptors+128)==0x2ec01022u&&get32(out.descriptors+132)==0x000f000fu);
 b3.texels.bytes=74496;
 assert(!tegu_compositor_resource_state_build_b3(&b3,776,24,3104,&out));
 assert(get32(out.descriptors+128)==0x2ec01022u&&get32(out.descriptors+132)==0x00170307u);
 assert(get32(out.plane+4)==74496u&&get32(out.plane+16)==3104u&&get32(out.plane+28)==0x00170307u);
 puts("resource-state: Mesa-v10-golden exact, bounds/overlap/refusal-preservation PASS");return 0;
}
