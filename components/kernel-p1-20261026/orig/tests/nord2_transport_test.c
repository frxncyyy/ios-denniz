/* Production ABI validation: malformed calls must fail before dereferencing. */
#include "nord2_gpu_transport.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
int main(void) {
        uint64_t in[4] = {0}, out[3] = {0}, size = 0;
        struct nord2_gpu_status status;
        struct nord2_gpu_compute_request req = {0};
        struct nord2_gpu_fault_info fault;
        char text[241]={0};
        assert(nord2_gpu_call_valid(NORD2_GPU_SEL_LOG,0,0,240,0,0,0,text,0));
        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_LOG,0,0,241,0,0,0,text,0));
        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_LOG,0,0,0,0,0,0,0,0));
        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_LOG,0,0,1,0,0,0,0,0));
        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_LOG,1,0,1,0,in,0,text,0));
        assert(nord2_gpu_allocation_size(128, 0, &size) && size == 16384);
        assert(nord2_gpu_allocation_size(4097, 0, &size) && size == 16384);
        assert(!nord2_gpu_allocation_size(0, 0, &size));
        assert(!nord2_gpu_allocation_size(UINT64_MAX, 0, &size));
        assert(!nord2_gpu_allocation_size(128, (64ull<<20)-4096, &size));
        assert(nord2_gpu_allocation_size(128, (64ull<<20)-16384, &size));
        assert(nord2_gpu_call_valid(0,0,0,0,sizeof(status),0,0,0,&status));
        assert(!nord2_gpu_call_valid(0,0,0,0,sizeof(status),0,0,0,0));
        assert(!nord2_gpu_call_valid(0,0,0,0,sizeof(status)-1,0,0,0,&status));
        assert(nord2_gpu_call_valid(1,2,3,0,0,in,out,0,0));
        assert(!nord2_gpu_call_valid(1,0,3,0,0,0,out,0,0));
        assert(!nord2_gpu_call_valid(1,2,2,0,0,in,out,0,0));
        assert(!nord2_gpu_call_valid(1,2,3,0,0,in,0,0,0));
        assert(nord2_gpu_call_valid(2,1,0,0,0,in,0,0,0));
        assert(nord2_gpu_call_valid(3,0,1,sizeof(req),0,0,out,&req,0));
        assert(!nord2_gpu_call_valid(3,0,1,sizeof(req),0,0,out,0,0));
        assert(!nord2_gpu_call_valid(3,0,1,sizeof(req)-1,0,0,out,&req,0));
        assert(nord2_gpu_call_valid(4,2,1,0,sizeof(fault),in,out,0,&fault));
        assert(nord2_gpu_call_valid(4,2,1,0,0,in,out,0,0));
        assert(!nord2_gpu_call_valid(4,2,1,0,1,in,out,0,&fault));
        assert(nord2_gpu_call_valid(5,0,0,0,sizeof(fault),0,0,0,&fault));
        assert(nord2_gpu_call_valid(6,0,0,0,0,0,0,0,0));
        assert(nord2_gpu_call_valid(7,0,1,0,0,0,out,0,0));
        assert(nord2_gpu_call_valid(8,4,3,0,0,in,out,0,0));
        assert(!nord2_gpu_call_valid(8,3,3,0,0,in,out,0,0));
        assert(!nord2_gpu_call_valid(9,0,0,0,0,0,0,0,0));
        struct nord2_gpu_submit_request raw = {0};
        assert(sizeof(raw) == 160);
        assert(nord2_gpu_call_valid(9,0,1,sizeof(raw),0,0,out,&raw,0));
        assert(!nord2_gpu_call_valid(9,0,1,sizeof(raw)-1,0,0,out,&raw,0));
        assert(!nord2_gpu_call_valid(9,0,1,sizeof(raw),0,0,out,0,0));
        assert(nord2_gpu_call_valid(10,2,0,0,0,in,0,0,0));
        assert(!nord2_gpu_call_valid(10,1,0,0,0,in,0,0,0));
        puts("transport ABI v2 and allocation bounds passed");
}
