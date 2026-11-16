#include "render_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const unsigned char *p){return tegu_render_word(p);}
static uint32_t host_half_oracle(uint32_t n){float h=(float)n/2.0f;uint32_t bits;memcpy(&bits,&h,sizeof bits);return bits;}
static struct tegu_render_addresses addresses={0x5000000,0x5001000,0x5010000,
 0x5020000,0x5100000,0x5002000,0x5003000,0xff3366cc,0,0};
static int load_package(const char *path,unsigned char **bytes,size_t *length,
 struct tegu_render_info *info){
 FILE *f=fopen(path,"rb");if(!f)return -1;
 if(fseek(f,0,SEEK_END)){fclose(f);return -1;}long n=ftell(f);
 if(n<=0){fclose(f);return -1;}rewind(f);
 int hex=n>4&&!strcmp(path+strlen(path)-4,".hex");
 if(hex){char *text=malloc((size_t)n);*bytes=malloc((size_t)n/2);
  if(!text||!*bytes||fread(text,1,(size_t)n,f)!=(size_t)n){free(text);fclose(f);return -1;}
  fclose(f);if(n&&text[n-1]=='\n')n--;if(n&1){free(text);return -1;}
  for(long i=0;i<n;i+=2){unsigned a,b;
   char x=text[i],y=text[i+1];
   if(x>='0'&&x<='9')a=x-'0';else if(x>='a'&&x<='f')a=x-'a'+10;else{free(text);return -1;}
   if(y>='0'&&y<='9')b=y-'0';else if(y>='a'&&y<='f')b=y-'a'+10;else{free(text);return -1;}
   (*bytes)[i/2]=(unsigned char)(a*16+b);
  }free(text);*length=(size_t)n/2;
 }else{*bytes=malloc((size_t)n);
  if(!*bytes||fread(*bytes,1,(size_t)n,f)!=(size_t)n){fclose(f);return -1;}
  fclose(f);*length=(size_t)n;}
 return tegu_render_parse(*bytes,*length,info);
}
static int in(size_t off,size_t start,size_t len){return off>=start&&off<start+len;}
static int dynamic_descriptor(size_t off){
 return in(off,32,8)||in(off,40,8)||in(off,168,8)||in(off,0xa00,24)||in(off,0xa10,8);
}
static int allowed_command_diff(uint64_t a,uint64_t b){
 unsigned oa=(unsigned)(a>>56),ob=(unsigned)(b>>56),ra=(unsigned)(a>>48)&255,rb=(unsigned)(b>>48)&255;
 return oa==2&&ob==2&&ra==43&&rb==43;
}
static void check_fragment_bbox(const struct tegu_render_state *s,uint32_t extent){
 unsigned writes=0;
 for(unsigned i=0;i<s->command_count;i++) {
  uint64_t c=s->commands[i];
  if((c>>56)==2&&((c>>48)&255)==43){assert((uint32_t)c==extent);writes++;}
 }
 assert(writes>=1);
}
static void check_package(const char *path){
 unsigned char *p;size_t n;struct tegu_render_info info;
 assert(!load_package(path,&p,&n,&info));
 uint64_t vertex=info.vertex_mask?0x5004004:0,fragment=info.texture.version?0x5005004:0;
 struct tegu_render_state old,base;
 assert(!tegu_render_build_resources(p,n,&addresses,vertex,fragment,&old));
 struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,16,16,64,1024};
 assert(!tegu_render_build_target_resources(p,n,&addresses,vertex,fragment,&l,&base));
 assert(!memcmp(&old,&base,sizeof old));
 check_fragment_bbox(&base,0x000f000f);
 const unsigned dims[][3]={{32,32,128},{64,64,256},{256,256,1024},{17,31,128}};
 for(unsigned k=0;k<sizeof dims/sizeof*dims;k++){
  l.width=dims[k][0];l.height=dims[k][1];l.row_stride=dims[k][2];
  l.target_length=(uint64_t)l.row_stride*l.height;
  struct tegu_render_state scaled;memset(&scaled,0xa5,sizeof scaled);
  assert(!tegu_render_build_target_resources(p,n,&addresses,vertex,fragment,&l,&scaled));
  uint32_t extent=(l.width-1)|((l.height-1)<<16);
  check_fragment_bbox(&scaled,extent);
  assert(rd32(scaled.descriptors+32)==extent&&rd32(scaled.descriptors+40)==extent);
  assert(rd32(scaled.descriptors+168)==l.row_stride&&rd32(scaled.descriptors+172)==l.row_stride*l.height);
  assert(rd32(scaled.tiler+12)==extent);
  float hw=(float)l.width/2.0f,hh=(float)l.height/2.0f;uint32_t uw,uh;
  memcpy(&uw,&hw,4);memcpy(&uh,&hh,4);
  assert(rd32(scaled.descriptors+0xa00)==uw&&rd32(scaled.descriptors+0xa04)==uh);
  assert(rd32(scaled.descriptors+0xa10)==uw&&rd32(scaled.descriptors+0xa14)==uh);
  for(size_t i=0;i<sizeof old.descriptors;i++)if(old.descriptors[i]!=scaled.descriptors[i]){
   if(!dynamic_descriptor(i))fprintf(stderr,"unexpected descriptor change %s width=%u height=%u offset=%zu old=%02x new=%02x\n",path,l.width,l.height,i,old.descriptors[i],scaled.descriptors[i]);
   assert(dynamic_descriptor(i));
  }
  for(size_t i=0;i<sizeof old.tiler;i++)if(old.tiler[i]!=scaled.tiler[i])assert(in(i,12,4));
  assert(old.command_count==scaled.command_count);
  for(unsigned i=0;i<old.command_count;i++)if(old.commands[i]!=scaled.commands[i])assert(allowed_command_diff(old.commands[i],scaled.commands[i]));
 }
 /* Exercise every accepted dimension, including all odd values, against the
  * host IEEE-754 oracle. The production builder itself uses integer arithmetic.
  */
 size_t package_length=n;
 for(uint32_t n=16;n<=256;n++){
  l.width=n;l.height=n;l.row_stride=(n*4u+63u)&~63u;l.target_length=(uint64_t)l.row_stride*n;
  struct tegu_render_state exact;
  assert(!tegu_render_build_target_resources(p,package_length,&addresses,vertex,fragment,&l,&exact));
  check_fragment_bbox(&exact,(n-1)|((n-1)<<16));
  uint32_t oracle=host_half_oracle(n);
  assert(rd32(exact.descriptors+0xa00)==oracle&&rd32(exact.descriptors+0xa04)==oracle);
  assert(rd32(exact.descriptors+0xa10)==oracle&&rd32(exact.descriptors+0xa14)==oracle);
 }
 /* Reject malformed layouts without modifying either output or byte-count. */
 const struct tegu_render_target_layout bad[]={
  {0,16,16,64,1024},{1,0,16,64,1024},{1,16,0,64,1024},{1,257,16,1088,17408},
  {1,16,257,64,16448},{1,16,16,60,1024},{1,16,16,128,1024},
  {1,256,256,4096,1048577},{1,256,256,4096,1048575},{1,17,16,64,1024}};
 for(unsigned i=0;i<sizeof bad/sizeof*bad;i++){
  struct tegu_render_state out,keep;memset(&out,0xa5,sizeof out);keep=out;
  assert(tegu_render_build_target_resources(p,n,&addresses,vertex,fragment,&bad[i],&out));
  assert(!memcmp(&out,&keep,sizeof out));uint64_t bytes=0x1122334455667788ULL;
  assert(tegu_render_target_layout_validate(&bad[i],&bytes)<0&&bytes==0x1122334455667788ULL);
 }
 free(p);
}
int main(int argc,char **argv){assert(argc>=2);for(int i=1;i<argc;i++)check_package(argv[i]);
 puts("PASS variable-target C state: legacy 16x16 parity, dynamic bounds/scissor/viewport, TGR1/TGR5 formats, refusal preservation (GPU untested)");}
