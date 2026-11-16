#ifndef TEGU_GPU_CODECHECK_READBACK_H
#define TEGU_GPU_CODECHECK_READBACK_H
#include <stdint.h>

#define TEGU_CODECHECK_READBACK_WORDS 34u
#define TEGU_CODECHECK_READBACK_SRC UINT64_C(0x06031a00)
#define TEGU_CODECHECK_READBACK_DST UINT64_C(0x0604f000)
#ifndef TEGU_CODECHECK_READBACK_DONE
#define TEGU_CODECHECK_READBACK_DONE UINT32_C(0x8f31a80d)
#define TEGU_CODECHECK_READBACK_SENTINEL UINT32_C(0x6d5a3c91)
#endif

static inline uint64_t tegu_codecheck_rb_move48(unsigned reg,uint64_t value)
{return (UINT64_C(1)<<56)|((uint64_t)reg<<48)|(value&UINT64_C(0x0000ffffffffffff));}
static inline uint64_t tegu_codecheck_rb_move32(unsigned reg,uint32_t value)
{return (UINT64_C(2)<<56)|((uint64_t)reg<<48)|value;}
static inline uint64_t tegu_codecheck_rb_ls(unsigned opcode,unsigned addr,unsigned base,unsigned mask,unsigned offset)
{return ((uint64_t)opcode<<56)|((uint64_t)base<<48)|((uint64_t)addr<<40)|((uint64_t)(mask&0xffffu)<<16)|(uint16_t)offset;}
static inline uint64_t tegu_codecheck_rb_wait(void)
{return (UINT64_C(3)<<56)|(UINT64_C(1)<<16);}
static inline uint64_t tegu_codecheck_rb_flush(unsigned mode)
{return (UINT64_C(36)<<56)|(UINT64_C(66)<<40)|((uint64_t)mode<<4)|mode;}

/* Exact 34-word prefix hardware-qualified by the standalone raw test. */
static inline unsigned tegu_codecheck_readback_prefix(uint64_t out[TEGU_CODECHECK_READBACK_WORDS])
{
    if(!out)return 0;
    unsigned n=0;
    out[n++]=tegu_codecheck_rb_move32(66,0);
    out[n++]=tegu_codecheck_rb_flush(3); /* L2 + LSC clean/invalidate */
    out[n++]=tegu_codecheck_rb_wait();
    for(unsigned i=0;i<4;i++){
        out[n++]=tegu_codecheck_rb_move48(80,TEGU_CODECHECK_READBACK_SRC);
        out[n++]=tegu_codecheck_rb_move48(82,TEGU_CODECHECK_READBACK_DST);
        out[n++]=tegu_codecheck_rb_ls(20,80,32,0xffffu,i*64u);
        out[n++]=tegu_codecheck_rb_wait();
        out[n++]=tegu_codecheck_rb_ls(21,82,32,0xffffu,i*64u);
        out[n++]=tegu_codecheck_rb_wait();
    }
    out[n++]=tegu_codecheck_rb_move48(82,TEGU_CODECHECK_READBACK_DST);
    out[n++]=tegu_codecheck_rb_move32(48,TEGU_CODECHECK_READBACK_DONE);
    out[n++]=tegu_codecheck_rb_ls(21,82,48,1,0x100);
    out[n++]=tegu_codecheck_rb_wait();
    out[n++]=tegu_codecheck_rb_move32(66,0);
    out[n++]=tegu_codecheck_rb_flush(1); /* L2 + LSC clean for CPU readback */
    out[n++]=tegu_codecheck_rb_wait();
    return n;
}
#endif
