/* Exact TGP1 regression + TGP2 header/manifest parser tests. No GPU. */
#include "pipeline_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void w(unsigned char *p,uint32_t v){tegu_pipeline_put(p,v);}
static void rec(unsigned char *p,unsigned i,uint32_t binding,uint32_t kind,uint32_t access,uint32_t table,
 uint32_t entry,uint32_t flags,uint32_t shape)
{
 unsigned char *r=p+TEGU_PIPELINE2_HEADER_BYTES+i*TEGU_PIPELINE2_MANIFEST_BYTES;
 w(r+0,0);w(r+4,binding);w(r+8,kind);w(r+12,1);w(r+16,access);w(r+20,table);w(r+24,entry);
 w(r+28,flags);w(r+48,shape);
}
static void mflags(unsigned char *p,unsigned i,uint32_t f){w(p+TEGU_PIPELINE2_HEADER_BYTES+i*TEGU_PIPELINE2_MANIFEST_BYTES+28,f);}
static size_t build(unsigned char *p,size_t cap,unsigned records,unsigned dummy)
{
 memset(p,0,cap);
 unsigned offset=TEGU_PIPELINE2_HEADER_BYTES+records*TEGU_PIPELINE2_MANIFEST_BYTES;
 w(p+0,TEGU_PIPELINE2_MAGIC);w(p+4,2);w(p+8,8);w(p+12,TEGU_PIPELINE_GPU);w(p+16,8);w(p+20,4);
 w(p+32,3);w(p+36,1);w(p+40,1);w(p+44,1);w(p+48,TEGU_PIPELINE2_HEADER_BYTES);w(p+52,records);
 w(p+56,TEGU_PIPELINE2_MANIFEST_BYTES);w(p+60,offset);w(p+64,dummy?TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER:0);
 unsigned at=0;
 rec(p,at++,0,TEGU_PIPELINE2_KIND_UBO,TEGU_PIPELINE2_ACCESS_READ,TEGU_PIPELINE2_TABLE_FAU,0,0,0);
 rec(p,at++,1,TEGU_PIPELINE2_KIND_SSBO,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,
  TEGU_PIPELINE2_TABLE_FAU,1,0,0);
 rec(p,at++,3,TEGU_PIPELINE2_KIND_TEXTURE,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
  TEGU_PIPELINE2_TABLE_TEXTURE,3,0,1);
 rec(p,at++,6,TEGU_PIPELINE2_KIND_SAMPLER,0,TEGU_PIPELINE2_TABLE_SAMPLER,6,0,0);
 if(dummy) rec(p,at++,TEGU_PIPELINE2_DUMMY_BINDING,TEGU_PIPELINE2_KIND_SAMPLER,0,
  TEGU_PIPELINE2_TABLE_SAMPLER,TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY,TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER,0);
 p[offset]=0x5a;
 return offset+8;
}
static void expect_bad(unsigned char *p,size_t n,const char *what)
{
 struct tegu_pipeline_info2 info;memset(&info,0xcc,sizeof(info));
 int rc=tegu_pipeline_parse2(p,n,&info);
 if(!rc){fprintf(stderr,"FAIL accepted: %s\n",what);exit(1);}
}
int main(void)
{
 unsigned char p[8192];struct tegu_pipeline_info2 a,b;
 size_t n=build(p,sizeof(p),4,0);
 if(tegu_pipeline_parse2(p,n,&a)){fprintf(stderr,"FAIL valid TGP2\n");return 1;}
 if(a.code_bytes!=8||a.registers!=8||a.push_words!=4||a.buffer_mask!=3||a.local[0]!=1||
    a.manifest_count!=4||a.manifest_record_bytes!=TEGU_PIPELINE2_MANIFEST_BYTES||
    a.code_offset!=n-8||a.manifest[2].kind!=TEGU_PIPELINE2_KIND_TEXTURE||
    a.manifest[2].table!=TEGU_PIPELINE2_TABLE_TEXTURE||a.manifest[2].entry!=3){
  fprintf(stderr,"FAIL TGP2 fields\n");return 1;
 }
 n=build(p,sizeof(p),5,1);
 if(tegu_pipeline_parse2(p,n,&b)||!(b.flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER)||
    b.manifest[4].entry!=TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY) {fprintf(stderr,"FAIL dummy TGP2\n");return 1;}
 /* TGP2 code is not limited to the TGP1 4KiB cap. */
 n=build(p,sizeof(p),4,0);w(p+8,5000);
 if(tegu_pipeline_parse2(p,TEGU_PIPELINE2_HEADER_BYTES+4*TEGU_PIPELINE2_MANIFEST_BYTES+5000,&a)){
  fprintf(stderr,"FAIL TGP2 5000-byte code\n");return 1;
 }
 /* Additive TLS/WLS words17/18. */
 n=build(p,sizeof(p),4,0);w(p+68,64);w(p+72,256);
 if(tegu_pipeline_parse2(p,n,&a)||a.tls_size!=64||a.wls_size!=256){
  fprintf(stderr,"FAIL TGP2 TLS/WLS fields\n");return 1;
 }
 n=build(p,sizeof(p),4,0);w(p+68,TEGU_PIPELINE2_MAX_TLS+4);expect_bad(p,n,"TLS over maximum");
 n=build(p,sizeof(p),4,0);w(p+72,TEGU_PIPELINE2_MAX_WLS+4);expect_bad(p,n,"WLS over maximum");
 n=build(p,sizeof(p),4,0);w(p+76,1);expect_bad(p,n,"reserved word19");
 /* TGP1 must stay byte-identical and v2 parsing must not reinterpret it. */
 {
  unsigned char v1[TEGU_PIPELINE_HEADER_BYTES+8];memset(v1,0,sizeof(v1));
  w(v1+0,TEGU_PIPELINE_MAGIC);w(v1+4,1);w(v1+8,8);w(v1+12,TEGU_PIPELINE_GPU);w(v1+16,8);w(v1+20,2);
  w(v1+32,1);w(v1+36,1);w(v1+40,1);w(v1+44,1);w(v1+56,1);
  struct tegu_pipeline_info i1;if(tegu_pipeline_parse(v1,sizeof(v1),&i1)||i1.code_bytes!=8||i1.bindings!=1){
   fprintf(stderr,"FAIL TGP1 regression\n");return 1;}
  if(tegu_pipeline_package_version(v1,sizeof(v1))!=1){fprintf(stderr,"FAIL TGP1 version\n");return 1;}
  struct tegu_pipeline_info2 i2;if(!tegu_pipeline_parse2(v1,sizeof(v1),&i2)){fprintf(stderr,"FAIL TGP1 parsed as TGP2\n");return 1;}
 }
 /* Header and manifest refusals. */
 n=build(p,sizeof(p),4,0);p[0]=0;expect_bad(p,n,"magic");
 n=build(p,sizeof(p),4,0);p[4]=3;expect_bad(p,n,"version");
 n=build(p,sizeof(p),4,0);p[48]=0;expect_bad(p,n,"manifest offset");
 n=build(p,sizeof(p),4,0);p[56]=32;expect_bad(p,n,"record bytes");
 n=build(p,sizeof(p),4,0);p[60]=0;expect_bad(p,n,"code offset");
 n=build(p,sizeof(p),4,0);p[64]=2;expect_bad(p,n,"dummy flag without record");
 n=build(p,sizeof(p),4,0);p[32]=4;expect_bad(p,n,"buffer mask mismatch");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+8,99);expect_bad(p,n,"kind");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+12,2);expect_bad(p,n,"array_count");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+20,1);expect_bad(p,n,"table");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+16,0);expect_bad(p,n,"access");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+24,2);expect_bad(p,n,"entry");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+4+8*0,9);expect_bad(p,n,"second buffer binding");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+4,4);expect_bad(p,n,"sampler entry mismatch");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+4,1);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+24,1);expect_bad(p,n,"buffer+sampler binding collision");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+48,0xffffu);expect_bad(p,n,"shape reserved");
 n=build(p,sizeof(p),4,0);expect_bad(p,n-1,"truncated code");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+36,1);expect_bad(p,n,"reserved view word");
 /* Flagless TEX+SAM same binding must refuse; flagged pair must parse. */
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+4,3);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+24,3);
 expect_bad(p,n,"flagless combined collision");
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+4,3);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+24,3);
 mflags(p,2,TEGU_PIPELINE2_FLAG_DECLARED_COMBINED);mflags(p,3,TEGU_PIPELINE2_FLAG_DECLARED_COMBINED);
 {int rc=tegu_pipeline_parse2(p,n,&a);if(rc){fprintf(stderr,"FAIL flagged combined pair rc=%d\n",rc);return 1;}}
 n=build(p,sizeof(p),4,0);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+4,3);w(p+TEGU_PIPELINE2_HEADER_BYTES+3*64+24,3);
 mflags(p,2,TEGU_PIPELINE2_FLAG_DECLARED_COMBINED);expect_bad(p,n,"combined counterparty mismatch");
 /* Shape/format/numeric domain (actual helper). */
 if(!tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_DIM_2D|TEGU_PIPELINE2_SHAPE_NUMERIC(TEGU_PIPELINE2_NUMERIC_FLOAT),37))return 1;
 if(!tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_DIM_2D|TEGU_PIPELINE2_SHAPE_NUMERIC(TEGU_PIPELINE2_NUMERIC_UINT),41))return 1;
 if(!tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_DIM_2D|TEGU_PIPELINE2_SHAPE_NUMERIC(TEGU_PIPELINE2_NUMERIC_SINT),108))return 1;
 if(!tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_DIM_2D|TEGU_PIPELINE2_SHAPE_NUMERIC(TEGU_PIPELINE2_NUMERIC_FLOAT),122))return 1;
 if(!tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_DIM_2D|TEGU_PIPELINE2_SHAPE_NUMERIC(TEGU_PIPELINE2_NUMERIC_FLOAT),64))return 1;
 if(tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_DIM_2D|TEGU_PIPELINE2_SHAPE_NUMERIC(TEGU_PIPELINE2_NUMERIC_UINT),37))return 1;
 if(tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_TEXTURE,TEGU_PIPELINE2_DIM_2D,41))return 1;
 if(tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_UBO,0,0)==0)return 1;
 if(tegu_pipeline2_shape_check(TEGU_PIPELINE2_KIND_SAMPLER,TEGU_PIPELINE2_DIM_2D,0))return 1;
 /* Atomic access is RMW only. */
 if(!tegu_pipeline2_valid_access(TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC))return 1;
 if(tegu_pipeline2_valid_access(TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_ATOMIC))return 1;
 if(!tegu_pipeline2_valid_access(TEGU_PIPELINE2_KIND_SSBO,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC))return 1;
 if(tegu_pipeline2_valid_access(TEGU_PIPELINE2_KIND_SSBO,TEGU_PIPELINE2_ACCESS_ATOMIC))return 1;
 printf("PASS pipeline_format v2: exact TGP1 regression, TGP2 header/manifest fields and refusals\n");
 return 0;
}
