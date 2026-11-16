
#include "mali_graphics_compiler.h"
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *load(const char *p,size_t *n) {
 FILE *f=fopen(p,"rb");if(!f)exit(90);fseek(f,0,SEEK_END);long z=ftell(f);if(z<0)exit(90);rewind(f);
 *n=(size_t)z;unsigned char *d=malloc(*n+1);if(!d||fread(d+1,1,*n,f)!=*n)exit(90);fclose(f);return d+1;
}
static void save(const char *pre,const char *suf,const void *p,size_t n) {
 char b[4096];snprintf(b,sizeof b,"%s.%s",pre,suf);FILE *f=fopen(b,"wb");if(!f||fwrite(p,1,n,f)!=n)exit(91);fclose(f);
}
static void hex(const void *p,size_t n,char *out) {
 const unsigned char *b=p;for(size_t i=0;i<n&&i<32;i++)sprintf(out+2*i,"%02x",b[i]);
}
int main(int argc,char **argv) {
 if(argc<8)return 90;
 if(!strcmp(argv[1],"compute")) {
  size_t n;void *d=load(argv[2],&n);struct tegu_mali_blob b={0};char e[400]={0};
  int rc=tegu_mali_compile_compute_v2(d,n,NULL,0,&b,e,sizeof e);
  printf("RESULT {\"rc\":%d,\"size\":%zu,\"error\":\"%s\"}\n",rc,b.size,e);
  tegu_mali_free(&b);return 0;
 }
 size_t vn,fn;void *v=load(argv[1],&vn),*f=load(argv[2],&fn);
 int mode=atoi(argv[3]);unsigned flags=strtoul(argv[4],0,0),rt=strtoul(argv[5],0,0),version=strtoul(argv[6],0,0);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};
 struct tegu_mali_generic_plan plan={0};struct tegu_mali_varying_info vi={0};char err[512]={0};int rc;
 struct tegu_mali_generic_options o={version,flags,rt,mode==6?1u:0u};
 int ext=(flags&TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS)!=0;
 if(mode==7)ext=!ext; /* deliberately mismatched varyings pointer */
 memset(err,0x5a,sizeof(err)-1);err[sizeof(err)-1]=0;
 char sentinel[512];memcpy(sentinel,err,sizeof err);
 if(mode==0)rc=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&pair,&plan,err,sizeof err);
 else if(mode==1)rc=tegu_mali_compile_generic_pair_specialized(v,vn,f,fn,NULL,0,NULL,0,&vs,&fs,&pair,&plan,err,sizeof err);
 else if(mode==3)rc=tegu_mali_compile_generic_pair_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,rt,err,sizeof err);
#ifndef TEGU_NO_OPTIONS
 else if(mode==5)rc=tegu_mali_compile_generic_pair_options(v,vn,f,fn,NULL,0,NULL,0,NULL,&vs,&fs,&pair,&plan,NULL,err,sizeof err);
 else if(mode!=8)rc=tegu_mali_compile_generic_pair_options(v,vn,f,fn,NULL,0,NULL,0,&o,&vs,&fs,&pair,&plan,ext?&vi:NULL,err,sizeof err);
 /* mode 8: version2 options with TEGU_MRT="count,f0,f1,f2,f3,r0,r1,r2" (r = reserved2). */
 else {
  struct tegu_mali_generic_options2 o2={TEGU_MALI_GENERIC_OPTIONS2_VERSION,flags,rt,0,0,{0},{0}};
  const char *m=getenv("TEGU_MRT");unsigned w[8]={0};
  if(m)sscanf(m,"%u,%u,%u,%u,%u,%u,%u,%u",&w[0],&w[1],&w[2],&w[3],&w[4],&w[5],&w[6],&w[7]);
  o2.target_count=w[0];for(int k=0;k<4;k++)o2.target_formats[k]=w[1+k];for(int k=0;k<3;k++)o2.reserved2[k]=w[5+k];
  rc=tegu_mali_compile_generic_pair_options(v,vn,f,fn,NULL,0,NULL,0,(const struct tegu_mali_generic_options *)&o2,
   &vs,&fs,&pair,&plan,ext?&vi:NULL,err,sizeof err);
 }
#else
 else {(void)o;(void)ext;return 93;}
#endif
 int untouched=!vs.code&&!vs.size&&!fs.code&&!fs.size&&!pair.version&&!plan.version&&!vi.version&&!memcmp(err,sentinel,sizeof err);
 if(rc==1&&mode>=4)err[0]=0;
 char vh[65]={0},fh[65]={0};
 printf("RESULT {\"vtls\":%u,\"ftls\":%u,\"rc\":%d,\"untouched\":%d,\"vs\":%zu,\"fs\":%zu,\"vsys\":%u,\"fsys\":%u,\"vfau\":%u,\"ffau\":%u,"
  "\"pflags\":%u,\"nopersp\":%u,\"vcount\":%u,\"gsize\":%u,\"fs_ld_var\":%u,\"bindings\":%u,\"reserved\":%u,"
  "\"ext_version\":%u,\"ext_count\":%u,\"ext_vs\":%u,\"ext_fs\":%u,\"flat\":%u,\"ext_np\":%u,\"uses_flat\":%u,\"ext_gsize\":%u,\"slots\":[",
  vs.tls,fs.tls,rc,untouched,vs.size,fs.size,vs.sysval_mask,fs.sysval_mask,vs.fau_words,fs.fau_words,pair.flags,pair.noperspective_mask,
  pair.varying_count,pair.generic_size_B,pair.fs_uses_ld_var,plan.count,plan.reserved,vi.version,vi.count,vi.vs_mask,vi.fs_mask,
  vi.flat_mask,vi.noperspective_mask,vi.uses_flat_shading,vi.generic_size_B);
 for(unsigned i=0;i<vi.count&&i<TEGU_MALI_EXT_MAX_VARYINGS;i++)printf("%s[%u,%u,%u,%u,%u]",i?",":"",vi.slots[i].location,
  vi.slots[i].offset,vi.slots[i].components,vi.slots[i].alu_type,vi.slots[i].interpolation);
 printf("],\"error\":\"%s\"}\n",rc?err:"");
 (void)hex;(void)vh;(void)fh;
 if(!rc) {
  save(argv[7],"vs",vs.code,vs.size);save(argv[7],"fs",fs.code,fs.size);
  struct tegu_mali_graphics a=vs,b=fs;a.code=b.code=NULL;save(argv[7],"vm",&a,sizeof a);save(argv[7],"fm",&b,sizeof b);
  save(argv[7],"pair",&pair,sizeof pair);save(argv[7],"plan",&plan,sizeof plan);
 } else if(vs.code||fs.code||pair.version||plan.version||vi.version)return 92;
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);return 0;
}
