/* Host tests: Mesa pack-macro goldens, refusal matrix with unchanged output,
 * boundary/overflow/alias and clean padding. argv[1]: canonical page dump. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "texture_state.h"
#include "texture_state_mesa_golden.h"
static unsigned failures,checks;
#define CHECK(c,...) do{checks++;if(!(c)){failures++;printf("FAIL %s:%d ",__FILE__,__LINE__);printf(__VA_ARGS__);printf("\n");}}while(0)
static const uint64_t TOP=1ULL<<48;
static struct tegu_texture_input valid(uint64_t d,uint64_t t,uint64_t n) {
 struct tegu_texture_input in;memset(&in,0,sizeof(in));
 in.version=1;in.format=70;in.width=16;in.height=16;in.levels=1;in.layers=1;in.row_stride=64;
 in.normalized=1;in.max_anisotropy=1;in.descriptor_address=d;in.texel_address=t;in.texel_length=n;
 return in;
}
static struct tegu_texture_state out,before;
static void prefill(unsigned char v) {memset(&out,v,sizeof(out));memcpy(&before,&out,sizeof(out));}
static void accept_golden(uint64_t d,uint64_t t,uint64_t n,const char *what) {
 struct tegu_texture_input in=valid(d,t,n);unsigned char g[4096];
 prefill(0xff);int r=tegu_texture_state_build(&in,&out);
 CHECK(r==0,"%s accept r=%d d=%#llx t=%#llx n=%llu",what,r,(unsigned long long)d,(unsigned long long)t,(unsigned long long)n);
 if(r)return;
 tegu_texture_mesa_golden_page(d,t,g);
 CHECK(out.resource_table==(d|4),"%s resource_table",what);
 for(unsigned i=0;i<4096;i++)if(out.descriptors[i]!=g[i]){CHECK(0,"%s byte %#x got %02x mesa %02x",what,i,out.descriptors[i],g[i]);break;}
 // Clean padding: only RESOURCE entry0 and the three 32-byte descriptors may be nonzero.
 for(unsigned i=0;i<4096;i++)if(!(i<16||(i>=0x100&&i<0x160))&&out.descriptors[i]){CHECK(0,"%s dirty pad %#x",what,i);break;}
 CHECK(!memcmp((unsigned char*)&out+offsetof(struct tegu_texture_state,descriptors)+4096,
  (unsigned char[sizeof(out)]){0},sizeof(out)-offsetof(struct tegu_texture_state,descriptors)-4096),"%s tail pad",what);
}
static void refuse(struct tegu_texture_input in,int want,const char *what) {
 prefill(0xa5);int r=tegu_texture_state_build(&in,&out);
 CHECK(r==want,"%s refuse got %d want %d",what,r,want);
 CHECK(!memcmp(&out,&before,sizeof(out)),"%s output modified on refusal",what);
}
int main(int argc,char **argv) {
 CHECK(tegu_texture_mesa_sampler_header_normalized()==1,"v10 SAMPLER header default normalized");
 const uint64_t D=0x10000000,T=0x10001000;
 // Canonical, boundaries, adjacency in both orders.
 accept_golden(D,T,1024,"canonical");
 accept_golden(D,T,4096,"len4096");
 accept_golden(TOP-4096,TOP-8192,4096,"desc at top");
 accept_golden(TOP-8192,TOP-4096,4096,"texel at top");
 accept_golden(TOP-8192,TOP-4096,1024,"texel at top len1024");
 accept_golden(0x1000,0x2000,1024,"lowest");
 accept_golden(T,D,4096,"texel immediately below desc");
 accept_golden(0x00ffffffff000ULL,0x0000100000000ULL,2048,"high32 bits set");
 uint64_t s=0x9e3779b97f4a7c15ULL;
 for(int i=0;i<4000;i++) {
  s^=s<<13;s^=s>>7;s^=s<<17;uint64_t d=((s>>8)&(TOP-1))&~4095ULL;
  s^=s<<13;s^=s>>7;s^=s<<17;uint64_t t=((s>>8)&(TOP-1))&~4095ULL;uint64_t n=1024+(s%3073);
  if(!d||!t||d>TOP-4096||t>TOP-n||(t<d+4096&&d<t+n))continue;
  accept_golden(d,t,n,"random");
 }
 if(argc>1) {
  struct tegu_texture_input in=valid(D,T,1024);
  if(tegu_texture_state_build(&in,&out)){printf("FAIL dump build\n");return 1;}
  FILE *f=fopen(argv[1],"wb");if(!f||fwrite(out.descriptors,1,4096,f)!=4096||fclose(f)){printf("FAIL dump\n");return 1;}
 }
 // Null and version.
 prefill(0xa5);CHECK(tegu_texture_state_build(NULL,&out)==-1&&!memcmp(&out,&before,sizeof(out)),"null input");
 {struct tegu_texture_input in=valid(D,T,1024);CHECK(tegu_texture_state_build(&in,NULL)==-1,"null output");}
 {struct tegu_texture_input in=valid(D,T,1024);in.version=0;refuse(in,-1,"version0");in.version=2;refuse(in,-1,"version2");}
 // Every contract field: one-off and all-ones values.
#define FIELD(f,bad) {struct tegu_texture_input in=valid(D,T,1024);in.f=bad;refuse(in,-2,#f"="#bad);}
#define FIELD2(f,bad) FIELD(f,bad) FIELD(f,0xffffffffu)
 FIELD2(format,80) FIELD2(format,71) FIELD2(width,8) FIELD2(width,17) FIELD2(height,32) FIELD2(height,0)
 FIELD2(levels,2) FIELD2(levels,0) FIELD2(layers,0) FIELD2(layers,2) FIELD2(row_stride,128) FIELD2(row_stride,0)
 FIELD2(min_filter,1) FIELD2(mag_filter,1) FIELD2(mip_filter,1) FIELD2(mip_filter,2)
 FIELD2(address_r,1) FIELD2(address_s,2) FIELD2(address_t,3) FIELD2(address_t,4)
 FIELD2(normalized,0) FIELD2(normalized,2) FIELD2(compare,1) FIELD2(compare,7) FIELD2(max_anisotropy,0) FIELD2(max_anisotropy,2)
 FIELD2(lod_min_q8,1) FIELD2(lod_min_q8,256) FIELD2(lod_max_q8,1) FIELD2(lod_max_q8,0xf00) FIELD2(options,1) FIELD2(reserved,1)
 // Addresses, alignment, length, range and overflow.
#define ADDR(d,t,n,want,what) refuse(valid(d,t,n),want,what)
 ADDR(0,T,1024,-3,"desc zero"); ADDR(D+0x100,T,1024,-3,"desc misaligned"); ADDR(D+0x800,T,1024,-3,"desc half page");
 ADDR(D,0,1024,-3,"texel zero"); ADDR(D,T+4,1024,-3,"texel misaligned"); ADDR(D,T+0x400,1024,-3,"texel 1K aligned");
 ADDR(D,T,0,-3,"len0"); ADDR(D,T,1023,-3,"len1023"); ADDR(D,T,4097,-3,"len4097"); ADDR(D,T,1ULL<<32,-3,"len2^32");
 ADDR(D,T,~0ULL,-3,"len max");
 ADDR(TOP,T,1024,-3,"desc at 2^48"); ADDR(TOP+4096,T,1024,-3,"desc past 2^48");
 ADDR(~0ULL&~4095ULL,T,1024,-3,"desc wrap"); ADDR(1ULL<<63,T,1024,-3,"desc bit63");
 ADDR(D,TOP,1024,-3,"texel at 2^48"); ADDR(D,TOP-4096,4097,-3,"texel len over");
 ADDR(D,~0ULL&~4095ULL,4096,-3,"texel wrap"); ADDR(D,(~0ULL&~4095ULL)-4096,4096,-3,"texel near wrap");
 ADDR(D,1ULL<<56|T,1024,-3,"texel bit56 smuggle"); ADDR(D|1ULL<<56,T,1024,-3,"desc bit56 smuggle");
 // VA overlap (both spans 4K aligned, length <=4096: overlap iff same page).
 ADDR(D,D,1024,-4,"texel==desc"); ADDR(D,D,4096,-4,"texel==desc full");
 ADDR(TOP-4096,TOP-4096,4096,-4,"overlap at top");
 // Host aliasing of input and output objects.
 {
  _Alignas(8) static unsigned char buf[sizeof(struct tegu_texture_state)+2*sizeof(struct tegu_texture_input)+64];
  struct tegu_texture_input v=valid(D,T,1024);
  size_t offs[]={0,8,sizeof(struct tegu_texture_state)-8,sizeof(struct tegu_texture_state)-sizeof(v)};
  for(unsigned i=0;i<4;i++) {
   memset(buf,0x5a,sizeof(buf));memcpy(buf+offs[i],&v,sizeof(v));unsigned char save[sizeof(buf)];memcpy(save,buf,sizeof(buf));
   int r=tegu_texture_state_build((const struct tegu_texture_input*)(buf+offs[i]),(struct tegu_texture_state*)buf);
   CHECK(r==-1&&!memcmp(save,buf,sizeof(buf)),"alias offset %zu r=%d",offs[i],r);
  }
  // Input inside output-before-start: in ends past out start.
  _Alignas(8) static unsigned char buf2[sizeof(struct tegu_texture_state)+sizeof(struct tegu_texture_input)];
  memset(buf2,0x5a,sizeof(buf2));memcpy(buf2,&v,sizeof(v));unsigned char save2[sizeof(buf2)];memcpy(save2,buf2,sizeof(buf2));
  int r=tegu_texture_state_build((const struct tegu_texture_input*)buf2,(struct tegu_texture_state*)(buf2+8));
  CHECK(r==-1&&!memcmp(save2,buf2,sizeof(buf2)),"alias input-before-output r=%d",r);
  // Adjacent, non-overlapping objects are accepted.
  memcpy(buf2+sizeof(struct tegu_texture_state),&v,sizeof(v));
  r=tegu_texture_state_build((const struct tegu_texture_input*)(buf2+sizeof(struct tegu_texture_state)),(struct tegu_texture_state*)buf2);
  CHECK(r==0,"adjacent objects accepted r=%d",r);
 }
 printf("%s: %u checks, %u failures\n",failures?"FAIL":"PASS",checks,failures);
 return failures!=0;
}
