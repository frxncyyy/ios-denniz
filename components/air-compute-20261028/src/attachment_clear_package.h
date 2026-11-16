#ifndef TEGU_ATTACHMENT_CLEAR_PACKAGE_H
#define TEGU_ATTACHMENT_CLEAR_PACKAGE_H
#include "mali_graphics_compiler.h"

/* Userspace package for the one exact path-blit/attachment-clear class.  The
 * format is intentionally separate from TGR1..TGR7: kernels which do not know
 * TAC1 reject it instead of interpreting its two-target state as a legacy
 * single-target pipeline. */
#define TEGU_ATTACHMENT_CLEAR_PACKAGE_BYTES 2496u
#define TEGU_ATTACHMENT_CLEAR_HEADER_BYTES 448u

static void tegu_attachment_clear_put32(unsigned char *p,uint32_t v) {
 p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static void tegu_attachment_clear_put64(unsigned char *p,uint64_t v) {
 tegu_attachment_clear_put32(p,(uint32_t)v);tegu_attachment_clear_put32(p+4,(uint32_t)(v>>32));
}
static int tegu_attachment_clear_hex32(const char *hex,unsigned char out[32]) {
 if(!hex)return -1;
 for(unsigned i=0;i<32;i++) {
  unsigned a=(unsigned char)hex[2*i],b=(unsigned char)hex[2*i+1];
  a=a>='0'&&a<='9'?a-'0':a>='a'&&a<='f'?a-'a'+10:99;
  b=b>='0'&&b<='9'?b-'0':b>='a'&&b<='f'?b-'a'+10:99;
  if(a>15||b>15)return -1;out[i]=(unsigned char)((a<<4)|b);
 }
 return hex[64]? -1:0;
}
static int tegu_attachment_clear_hash(const void *p,NSUInteger n,unsigned char out[32]) {
 char hex[65];return tegu_compiler_identity_sha256(p,n,hex)||tegu_attachment_clear_hex32(hex,out);
}
static int tegu_attachment_clear_plan_exact(const struct tegu_mali_attachment_clear_plan_v2 *p) {
 static const uint32_t resource[2][14]={
  {1,1,0,0,1,1,1,0,0,0,4,16,16,1},
  {2,1,0,1,2,1,1,1,320,4,16,16,320,1}};
 if(!p||p->version!=2||p->output_count!=2||p->rt0_write_mask||p->rt1_write_mask!=15||
    p->position_offset!=896||p->secondary_offset!=1792||p->secondary_enabled||p->generic_size_B||
    p->draw_vertex_count!=3||p->draw_instance_count!=1||p->draw_base_instance||p->draw_indexed||
    p->target0_format!=80||p->target1_format!=80||p->target0_blend_enabled||p->target1_blend_enabled||
    p->target_descriptors_seen!=3||p->resource_count!=2)return 0;
 for(unsigned i=0;i<2;i++)if(memcmp(&p->resources[i],resource[i],sizeof(resource[i])))return 0;
 return 1;
}
static int tegu_attachment_clear_stage_exact(const struct tegu_mali_graphics *s,unsigned fragment) {
 return s&&s->code&&s->size==(fragment?256u:1792u)&&s->stage==(fragment?2u:1u)&&s->registers==32&&
  !s->tls&&s->fau_words==(fragment?16u:34u)&&s->sysval_mask==(fragment?8u:3u)&&
  s->push_bytes==(fragment?72u:8u)&&s->preload==(fragment?0x3000000000000000ULL:0x3800000000000000ULL);
}
static int tegu_attachment_clear_make_package(const char librarySHA[65],const void *profile,NSUInteger profileBytes,
 const TeguAIRBlob *vsSPIRV,const TeguAIRBlob *fsSPIRV,const struct tegu_mali_graphics *vs,
 const struct tegu_mali_graphics *fs,const struct tegu_mali_attachment_clear_plan_v2 *plan,
 unsigned char **out,NSUInteger *outBytes) {
 static const char expected[6][65]={
  "11c74cc42ce5137b73d44d427f75764f886656d4ddb374422d56b609ed2762d9",
  "b39bb9d68811859ed6f781c46d4c8210a72a53ffda34e2da656de6936520a6be",
  "11172acf267947099622dd8449e415e5fe7b07a1e7e1a938a0986f34ab299d6d",
  "b2272327d404768874364fd8aa15305777b9210392e69a8b60338e267ad7550e",
  "f65b226eb26a30602897f2577b9743916e739df6200b2dab81f45b7e07b7038b",
  "03209c66f0d2181a8b540bcc7d7c3e5e89bc7012f6e856d5c6da426a36dbfab6"};
 if(!out)return -1;
 if(!outBytes)return -5;
 if(*out)return -6;
 if(*outBytes)return -7;
 if(!librarySHA||!profile||!profileBytes)return -8;
 if(!vsSPIRV||!vsSPIRV->bytes||!vsSPIRV->length)return -9;
 if(!fsSPIRV||!fsSPIRV->bytes||!fsSPIRV->length)return -16;
 /* Keep structural failures distinct from the six identity hashes below.  A
  * device compiler can otherwise report only the catch-all -1 and hide which
  * native ABI field differs from the host-qualified package. */
 if(!tegu_attachment_clear_stage_exact(vs,0))return -2;
 if(!tegu_attachment_clear_stage_exact(fs,1))return -3;
 if(!tegu_attachment_clear_plan_exact(plan))return -4;
 unsigned char hashes[6][32];
 if(tegu_attachment_clear_hex32(librarySHA,hashes[0])||tegu_attachment_clear_hash(profile,profileBytes,hashes[1])||
    tegu_attachment_clear_hash(vsSPIRV->bytes,vsSPIRV->length,hashes[2])||
    tegu_attachment_clear_hash(fsSPIRV->bytes,fsSPIRV->length,hashes[3])||
    tegu_attachment_clear_hash(vs->code,vs->size,hashes[4])||tegu_attachment_clear_hash(fs->code,fs->size,hashes[5]))return -1;
 for(unsigned i=0;i<6;i++){unsigned char exact[32];if(tegu_attachment_clear_hex32(expected[i],exact)||memcmp(hashes[i],exact,32))return -10-(int)i;}
 unsigned char *b=malloc(TEGU_ATTACHMENT_CLEAR_PACKAGE_BYTES);if(!b)return -1;memset(b,0,TEGU_ATTACHMENT_CLEAR_PACKAGE_BYTES);
 memcpy(b,"TAC1",4);const uint32_t base[6]={1,2496,448,1792,2240,256};
 for(unsigned i=0;i<6;i++)tegu_attachment_clear_put32(b+4+i*4,base[i]);
 tegu_attachment_clear_put64(b+28,vs->preload);tegu_attachment_clear_put64(b+36,fs->preload);
 const struct tegu_mali_graphics *stage[2]={vs,fs};
 for(unsigned i=0;i<2;i++){unsigned char *q=b+44+i*32;const uint32_t words[8]={(uint32_t)stage[i]->size,stage[i]->stage,stage[i]->registers,stage[i]->tls,stage[i]->fau_words,stage[i]->sysval_mask,stage[i]->push_bytes,0};for(unsigned j=0;j<8;j++)tegu_attachment_clear_put32(q+j*4,words[j]);}
 tegu_attachment_clear_put32(b+108,2);tegu_attachment_clear_put32(b+112,2);
 tegu_attachment_clear_put32(b+116,0);tegu_attachment_clear_put32(b+120,15);
 for(unsigned i=0;i<2;i++){const uint32_t *r=(const uint32_t *)&plan->resources[i];for(unsigned j=0;j<14;j++)tegu_attachment_clear_put32(b+128+i*56+j*4,r[j]);}
 memcpy(b+240,hashes,sizeof(hashes));memcpy(b+448,vs->code,vs->size);memcpy(b+2240,fs->code,fs->size);
 *out=b;*outBytes=TEGU_ATTACHMENT_CLEAR_PACKAGE_BYTES;return 0;
}
#endif
