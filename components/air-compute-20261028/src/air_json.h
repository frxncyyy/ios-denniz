/* Minimal JSON reader for the pinned translator's own metadata (trusted input:
 * the m2v metadata blob the bridge returns beside its SPIR-V). Moved verbatim
 * from mali_graphics_air.inc (lowering-air-opus) so the compute adapter
 * (mali_compute_air.inc, air-compute-opus) parses the same way. Static, so
 * each translation unit that includes it has its own copy. Needs ralloc. */
#ifndef TEGU_AIR_JSON_H
#define TEGU_AIR_JSON_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "util/ralloc.h"
enum { AJ_NULL, AJ_BOOL, AJ_NUM, AJ_STR, AJ_ARR, AJ_OBJ };
struct aj { uint32_t t,n,len; bool b; const char *s; struct aj *v; const char **k; uint32_t *kl; };
struct ajp { const char *p,*e; void *mem; };
static void aj_ws(struct ajp *P){while(P->p<P->e&&(*P->p==' '||*P->p=='\n'||*P->p=='\r'||*P->p=='\t'))P->p++;}
static bool aj_str(struct ajp *P,const char **s,uint32_t *len) {
 if(P->p>=P->e||*P->p!='"')return false;
 const char *a=++P->p;
 while(P->p<P->e&&*P->p!='"'){if(*P->p=='\\'&&++P->p>=P->e)return false;P->p++;}
 if(P->p>=P->e)return false;
 *s=a;*len=(uint32_t)(P->p-a);P->p++;return true;
}
static bool aj_val(struct ajp *P,struct aj *o,unsigned depth) {
 memset(o,0,sizeof(*o));
 if(depth>48)return false;
 aj_ws(P);if(P->p>=P->e)return false;
 char c=*P->p;
 if(c=='{'||c=='[') {
  bool obj=c=='{';char close=obj?'}':']';unsigned cap=0;
  P->p++;o->t=obj?AJ_OBJ:AJ_ARR;aj_ws(P);
  if(P->p<P->e&&*P->p==close){P->p++;return true;}
  for(;;) {
   if(o->n==cap) {
    cap=cap?cap*2:8;o->v=reralloc(P->mem,o->v,struct aj,cap);if(!o->v)return false;
    if(obj){o->k=reralloc(P->mem,o->k,const char *,cap);o->kl=reralloc(P->mem,o->kl,uint32_t,cap);if(!o->k||!o->kl)return false;}
   }
   if(obj) {
    aj_ws(P);if(!aj_str(P,&o->k[o->n],&o->kl[o->n]))return false;
    aj_ws(P);if(P->p>=P->e||*P->p!=':')return false;P->p++;
   }
   if(!aj_val(P,&o->v[o->n],depth+1))return false;
   o->n++;aj_ws(P);if(P->p>=P->e)return false;
   if(*P->p==','){P->p++;continue;}
   if(*P->p==close){P->p++;return true;}
   return false;
  }
 }
 if(c=='"'){o->t=AJ_STR;return aj_str(P,&o->s,&o->len);}
 if(P->e-P->p>=4&&!memcmp(P->p,"true",4)){o->t=AJ_BOOL;o->b=true;P->p+=4;return true;}
 if(P->e-P->p>=5&&!memcmp(P->p,"false",5)){o->t=AJ_BOOL;P->p+=5;return true;}
 if(P->e-P->p>=4&&!memcmp(P->p,"null",4)){o->t=AJ_NULL;P->p+=4;return true;}
 const char *a=P->p;
 while(P->p<P->e&&(*P->p=='-'||*P->p=='+'||*P->p=='.'||*P->p=='e'||*P->p=='E'||(*P->p>='0'&&*P->p<='9')))P->p++;
 if(P->p==a)return false;
 o->t=AJ_NUM;o->s=a;o->len=(uint32_t)(P->p-a);return true;
}
static const struct aj *aj_get(const struct aj *o,const char *key) {
 if(!o||o->t!=AJ_OBJ)return NULL;
 size_t l=strlen(key);
 for(uint32_t i=0;i<o->n;i++)if(o->kl[i]==l&&!memcmp(o->k[i],key,l))return &o->v[i];
 return NULL;
}
static bool aj_is(const struct aj *o,const char *s){return o&&o->t==AJ_STR&&o->len==strlen(s)&&!memcmp(o->s,s,o->len);}
static bool aj_u64(const struct aj *o,uint64_t *v) {
 if(!o||o->t!=AJ_NUM||!o->len||o->len>20)return false;
 uint64_t x=0;
 for(uint32_t i=0;i<o->len;i++){char c=o->s[i];if(c<'0'||c>'9')return false;uint64_t y=x*10+(uint64_t)(c-'0');if(y/10!=x&&x)return false;x=y;}
 *v=x;return true;
}
static bool aj_u32(const struct aj *o,uint32_t *v){uint64_t x;if(!aj_u64(o,&x)||x>UINT32_MAX)return false;*v=(uint32_t)x;return true;}
/* The single key of a serde externally tagged enum value. */
static const struct aj *aj_tag(const struct aj *o,const char **name,uint32_t *len) {
 if(!o||o->t!=AJ_OBJ||o->n!=1)return NULL;
 *name=o->k[0];*len=o->kl[0];return &o->v[0];
}
static bool aj_tag_is(const char *name,uint32_t len,const char *want){return len==strlen(want)&&!memcmp(name,want,len);}
#endif
