#include <assert.h>
#include <stdint.h>
#include <limits.h>
static uint64_t fake_now, increment;
static uint64_t readreg(const char *reg) {
    assert(reg && !__builtin_strcmp(reg,"cntpct_el0"));
    uint64_t result=fake_now; fake_now+=increment; return result;
}
#include "tegu_gpu_queue_wait.h"
int main(void) {
    volatile uint32_t word=0;
    assert(gpu_queue_wait_ticks(&word,1,1,0)==-1);
    assert(gpu_queue_wait_ticks(0,1,1,10)==-1);
    word=1;fake_now=100;increment=1;
    assert(gpu_queue_wait_ticks(&word,1,1,10)==0);
    word=0;fake_now=100;increment=5;
    assert(gpu_queue_wait_ticks(&word,1,1,10)==-1);
    /* Saturated deadline must terminate without unsigned wrap into an
     * effectively unbounded wait. */
    fake_now=UINT64_MAX-2;increment=1;
    assert(gpu_queue_wait_ticks(&word,1,1,10)==-1);
    return 0;
}
