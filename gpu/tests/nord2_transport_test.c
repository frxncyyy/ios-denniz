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
        assert(!nord2_gpu_allocation_size(128, NORD2_GPU_PRIVATE_LIMIT-4096, &size));
        assert(nord2_gpu_allocation_size(128, NORD2_GPU_PRIVATE_LIMIT-16384, &size));
        /* 0088: 160 MiB default private cap, boot-arg ceiling, device-wide budget. */
        assert(NORD2_GPU_PRIVATE_LIMIT == (160ull<<20));
        assert(nord2_gpu_allocation_size(NORD2_GPU_PRIVATE_LIMIT, 0, &size) && size == NORD2_GPU_PRIVATE_LIMIT);
        assert(!nord2_gpu_allocation_size(NORD2_GPU_PRIVATE_LIMIT + 1, 0, &size));
        assert(nord2_gpu_allocation_size_limit(64ull<<20, (64ull<<20)-1, 64ull<<20, &size) == false); /* old 64 MiB behaviour via explicit limit */
        assert(nord2_gpu_allocation_size_limit(16384, (64ull<<20)-16384, 64ull<<20, &size) && size == 16384);
        assert(!nord2_gpu_allocation_size_limit(16384, UINT64_MAX, 64ull<<20, &size));
        assert(nord2_gpu_budget_mib(0, 16, 256, NORD2_GPU_PRIVATE_LIMIT) == NORD2_GPU_PRIVATE_LIMIT);
        assert(nord2_gpu_budget_mib(1, 16, 256, 0) == (16ull<<20));      /* clamped up */
        assert(nord2_gpu_budget_mib(96, 16, 256, 0) == (96ull<<20));
        assert(nord2_gpu_budget_mib(4096, 16, 256, 0) == (256ull<<20));  /* clamped down */
        assert(nord2_gpu_global_private_ok(0, 16384, NORD2_GPU_GLOBAL_PRIVATE_LIMIT));
        assert(nord2_gpu_global_private_ok(NORD2_GPU_GLOBAL_PRIVATE_LIMIT-16384, 16384, NORD2_GPU_GLOBAL_PRIVATE_LIMIT));
        assert(!nord2_gpu_global_private_ok(NORD2_GPU_GLOBAL_PRIVATE_LIMIT-16384, 32768, NORD2_GPU_GLOBAL_PRIVATE_LIMIT));
        assert(!nord2_gpu_global_private_ok(NORD2_GPU_GLOBAL_PRIVATE_LIMIT+1, 16384, NORD2_GPU_GLOBAL_PRIVATE_LIMIT));
        assert(NORD2_GPU_PRIVATE_LIMIT <= NORD2_GPU_PRIVATE_MAX && NORD2_GPU_GLOBAL_PRIVATE_LIMIT <= NORD2_GPU_GLOBAL_PRIVATE_MAX);
        assert(NORD2_GPU_CONTEXT_LIMIT == (384ull<<20));
        {   /* 0095: n2mhz alias */
                uint32_t k = 7;
                assert(nord2_gpu_mhz_alias_to_khz(654, &k) && k == 654000);
                assert(nord2_gpu_mhz_alias_to_khz(886, &k) && k == 886000 && nord2_gpu_mhz_alias_to_khz(100, &k) && k == 100000);
                k = 7;
                assert(!nord2_gpu_mhz_alias_to_khz(99, &k) && !nord2_gpu_mhz_alias_to_khz(1001, &k) && !nord2_gpu_mhz_alias_to_khz(-654, &k) && !nord2_gpu_mhz_alias_to_khz(654, NULL) && k == 7);
        }
        /* 0090: WAIT_SEQ validation and the completion polling schedule. */
        uint64_t seqin[2]={1,100},seqout[3];
        assert(nord2_gpu_call_valid(NORD2_GPU_SEL_WAIT_SEQ,2,3,0,0,seqin,seqout,0,0));
        assert(nord2_gpu_call_valid(NORD2_GPU_SEL_WAIT_SEQ,2,3,0,sizeof(fault),seqin,seqout,0,&fault));
        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_WAIT_SEQ,2,3,0,sizeof(fault)-1,seqin,seqout,0,&fault));
        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_WAIT_SEQ,1,3,0,0,seqin,seqout,0,0));
        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_WAIT_SEQ,2,1,0,0,seqin,seqout,0,0));
        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_WAIT_SEQ,2,3,8,0,seqin,seqout,seqin,0));
        assert(NORD2_GPU_SEL_WAIT_SEQ != NORD2_GPU_SEL_SYNC && NORD2_GPU_SEL_WAIT_SEQ != NORD2_GPU_SEL_LOG);
        assert(NORD2_GPU_MAX_INFLIGHT == 96u);
        /* 0112: display-surface reservation selectors (contract with the frontend: 13 RESERVE, 14 RETIRE). */
        {
                assert(NORD2_GPU_SEL_SURFACE_RESERVE == 13 && NORD2_GPU_SEL_SURFACE_RETIRE == 14 && NORD2_GPU_RESERVE_MAX == 8u);
                assert(NORD2_GPU_SEL_SURFACE_RESERVE != NORD2_GPU_SEL_WAIT_SEQ && NORD2_GPU_SEL_SURFACE_RETIRE != NORD2_GPU_SEL_SYNC);
                uint32_t hs[8] = {1, 2, 3, 4, 5, 6, 7, 8};
                uint64_t rin[1], rout[1], tin[2] = {5, 9}, tout[1];
                rin[0] = 0;   /* the availability probe: one scalar in, one out, no structure */
                assert(nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, 0, 0, rin, rout, NULL, NULL));
                assert(nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, 0, 0, rin, rout, hs, NULL));
                assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, 4, 0, rin, rout, hs, NULL));          /* n=0 carries no handles */
                for (unsigned n = 1; n <= 8; n++) {
                        rin[0] = n;
                        assert(nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, n * 4, 0, rin, rout, hs, NULL));
                        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, n * 4 - 4, 0, rin, rout, hs, NULL));
                        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, n * 4 + 4, 0, rin, rout, hs, NULL));   /* count and size must agree */
                        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, n * 4, 0, rin, rout, NULL, NULL));    /* handles required */
                        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 0, n * 4, 0, rin, rout, hs, NULL));      /* the id is returned */
                        assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 0, 1, n * 4, 0, rin, rout, hs, NULL));
                }
                rin[0] = 9;
                assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, 36, 0, rin, rout, hs, NULL));         /* n > 8 */
                rin[0] = UINT64_MAX;
                assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, 0, 0, rin, rout, hs, NULL));          /* no wrap to a small size */
                rin[0] = 0x100000000ull;
                assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, 0, 0, rin, rout, hs, NULL));
                assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RESERVE, 1, 1, 0, 0, NULL, rout, hs, NULL));
                assert(nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RETIRE, 2, 0, 0, 0, tin, NULL, NULL, NULL));
                assert(nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RETIRE, 2, 1, 0, 0, tin, tout, NULL, NULL));          /* extra scalar out is tolerated like everywhere else */
                assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RETIRE, 1, 0, 0, 0, tin, NULL, NULL, NULL));
                assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RETIRE, 3, 0, 0, 0, tin, NULL, NULL, NULL));
                assert(!nord2_gpu_call_valid(NORD2_GPU_SEL_SURFACE_RETIRE, 2, 0, 4, 0, tin, NULL, hs, NULL));
                assert(!nord2_gpu_call_valid(15, 0, 0, 0, 0, NULL, NULL, NULL, NULL));                                  /* nothing after 14 yet */
        }
        assert(nord2_gpu_wait_step_us(0) == 20 && nord2_gpu_wait_step_us(1499) == 20);
        assert(nord2_gpu_wait_step_us(1500) == 100 && nord2_gpu_wait_step_us(5999) == 100);
        assert(nord2_gpu_wait_step_us(6000) == 0 && nord2_gpu_wait_step_us(UINT64_MAX) == 0);
        /* Imported IOSurface pages use their own budget; three BackBoard display
         * surfaces (11239424 B, 2744 pages) plus textures fit with margin. */
        assert(nord2_gpu_import_size(11239424ull+1024, 0, &size) == false); /* unaligned */
        assert(nord2_gpu_import_size(2744ull*4096, 0, &size) && size == 11239424ull);
        assert(nord2_gpu_import_size(2744ull*4096, 9ull*2744*4096, &size));
        assert(!nord2_gpu_import_size(2744ull*4096, 11ull*2744*4096, &size)); /* hard bound */
        assert(!nord2_gpu_import_size(0, 0, &size));
        assert(!nord2_gpu_import_size(NORD2_GPU_IMPORT_LIMIT + 4096, 0, &size));
        assert(nord2_gpu_import_size(NORD2_GPU_IMPORT_LIMIT, 0, &size));
        assert(!nord2_gpu_import_size(4096, NORD2_GPU_IMPORT_LIMIT, &size));
        assert(!nord2_gpu_import_size(4096, UINT64_MAX, &size));
        assert(NORD2_GPU_PRIVATE_MAX + NORD2_GPU_IMPORT_LIMIT <= NORD2_GPU_CONTEXT_LIMIT);
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
        {       /* P8 cold-start OPP policy */
                assert(nord2_gpu_default_opp_khz(true) == 654000u && nord2_gpu_default_opp_khz(false) == 0u);
                assert(nord2_gpu_opp_battery_allows(350000, true, 9999) && nord2_gpu_opp_battery_allows(100000, true, 9999));       /* the minimum is never refused */
                assert(nord2_gpu_opp_battery_allows(654000, false, 9999) && nord2_gpu_opp_battery_allows(886000, false, -1));      /* unreadable gauge never blocks */
                assert(nord2_gpu_opp_battery_allows(654000, true, 3999) && !nord2_gpu_opp_battery_allows(654000, true, 4000) && !nord2_gpu_opp_battery_allows(654000, true, 5000));
                assert(nord2_gpu_opp_battery_allows(600000, true, 3999) && !nord2_gpu_opp_battery_allows(600000, true, 4000) && nord2_gpu_opp_battery_allows(351000, true, 3999));   /* 351..654: 40 C */
                assert(!nord2_gpu_opp_battery_allows(775000, true, 3800) && nord2_gpu_opp_battery_allows(775000, true, 3799));      /* above 654: 38 C */
                assert(nord2_gpu_opp_battery_allows(886000, true, 3799) && !nord2_gpu_opp_battery_allows(886000, true, 3800) && !nord2_gpu_opp_battery_allows(655000, true, 3800));
                assert(nord2_gpu_opp_battery_allows(886000, true, -500));                                                            /* cold is fine */
        }
        puts("transport ABI v2 and allocation bounds passed");
}
