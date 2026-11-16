#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "fullscreen_job_contract.h"
#include "tegu_gpu_codecheck_map.h"
#include "tegu_gpu_codecheck_readback.h"
#define TEGU_GPU_QUEUE_VA UINT64_C(0x05000000)
#define GPU_IF_BARRIER() ((void)0)
static uint64_t captured[169];
static unsigned captured_count,calls;
static uint64_t tegu_gpu_ring_append(volatile uint32_t *ring,uint64_t insert,
 uint64_t extract,const uint64_t *words,unsigned n)
{
    (void)ring;(void)insert;(void)extract;
    assert(n<=169);memcpy(captured,words,n*8);captured_count=n;calls++;return 1234;
}
#include "actual_append.inc"

static void check(struct tegu_full_render_job *job,int enabled,unsigned prefix)
{
    unsigned before=calls;
    assert(tg_fullscreen_append(NULL,0,0,123,job,enabled)==1234&&calls==before+1);
    assert(captured_count==prefix+job->command_count+7);
    if(prefix){uint64_t words[34];assert(tegu_codecheck_readback_prefix(words)==prefix);
        assert(!memcmp(words,captured,prefix*8));}
    assert(captured[prefix]==(UINT64_C(23)<<56));
    assert(captured[prefix+1]==((UINT64_C(2)<<56)|(UINT64_C(74)<<48)));
    assert(captured[prefix+2]==((UINT64_C(36)<<56)|(UINT64_C(74)<<40)|0x233));
    assert(captured[prefix+3]==((UINT64_C(3)<<56)|(UINT64_C(1)<<16)));
    assert(!memcmp(captured+prefix+4,job->commands,job->command_count*8));
    unsigned tail=prefix+4+job->command_count;
    assert(captured[tail]==((UINT64_C(1)<<56)|(TEGU_GPU_QUEUE_VA+0x3800)));
    assert(captured[tail+1]==((UINT64_C(2)<<56)|(UINT64_C(2)<<48)|123));
    assert(captured[tail+2]==((UINT64_C(21)<<56)|(UINT64_C(2)<<48)|(UINT64_C(1)<<16)));
}
int main(void)
{
    struct fs_page pages[112]={0};volatile uint64_t ptes[512]={0};
    struct fs_pt_page l3={0};l3.entries=ptes;
    struct fs_request req={.scratch=pages,.scratch_count=112,.l3=&l3,.l3_count=1,
        .scratch_exec_mask=UINT64_C(0x007f00000000000c)};
    for(unsigned i=0;i<112;i++){pages[i].pa=UINT64_C(0x883000000)+4096*i;ptes[i]=fs_leaf_value(&req,i);}
    uint64_t commands[128];for(unsigned i=0;i<128;i++)commands[i]=UINT64_C(0x12340000)+i;
    struct tegu_full_render_job job={.commands=commands,.request=&req};
    const unsigned counts[]={1,58,128};
    for(unsigned i=0;i<3;i++){job.command_count=counts[i];check(&job,0,0);check(&job,1,34);}
    ptes[49]^=0x1000;check(&job,1,0);ptes[49]^=0x1000;
    ptes[79]^=0x1000;check(&job,1,0);ptes[79]^=0x1000;
    req.scratch_exec_mask^=UINT64_C(1)<<49;ptes[49]=fs_leaf_value(&req,49);check(&job,1,0);
    req.scratch_exec_mask^=UINT64_C(1)<<49;ptes[49]=fs_leaf_value(&req,49);
    req.scratch_count=64;check(&job,1,0);req.scratch_count=112;
    unsigned before=calls;job.command_count=129;assert(!tg_fullscreen_append(NULL,0,0,123,&job,1));
    job.command_count=0;assert(!tg_fullscreen_append(NULL,0,0,123,&job,1));
    job.command_count=1;job.commands=NULL;assert(!tg_fullscreen_append(NULL,0,0,123,&job,1));
    assert(calls==before);return 0;
}
