/* IOConnectTrap6 fast path (0111): ABI of the call block, validation and reply packing, checked against the frontend's own copy of the
 * struct (gpu/metal/tegu/nord2_transport.inc, copied verbatim below) and against the MIG path's validator. */
#include "nord2_gpu_transport.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---- verbatim from nord2_transport.inc ---- */
#define FE_TRAP_CALL 0u
#define FE_TRAP_MAGIC 0x4e325443u /* 'N2TC' */
#define FE_TRAP_DONE 0x4e325444u  /* 'N2TD' */
struct fe_trap_call {
 uint32_t magic,selector,scalar_in_count,scalar_out_count;
 uint64_t scalar_in[4],scalar_out[4];
 uint64_t struct_in,struct_in_size,struct_out,struct_out_size;
 int32_t result;uint32_t done;
};
/* ---- end verbatim ---- */

#define SAME(f) assert(offsetof(struct fe_trap_call,f)==offsetof(struct nord2_gpu_trap_call,f))
static struct nord2_gpu_trap_call good(void)
{
	struct nord2_gpu_trap_call c;
	memset(&c, 0, sizeof(c));
	c.magic = NORD2_GPU_TRAP_MAGIC;
	return c;
}

int main(void)
{
	/* layout and constants are the frontend's */
	assert(sizeof(struct fe_trap_call) == sizeof(struct nord2_gpu_trap_call) && sizeof(struct nord2_gpu_trap_call) == 120);
	SAME(magic); SAME(selector); SAME(scalar_in_count); SAME(scalar_out_count); SAME(scalar_in); SAME(scalar_out);
	SAME(struct_in); SAME(struct_in_size); SAME(struct_out); SAME(struct_out_size); SAME(result); SAME(done);
	assert(FE_TRAP_CALL == NORD2_GPU_TRAP_CALL && FE_TRAP_MAGIC == NORD2_GPU_TRAP_MAGIC && FE_TRAP_DONE == NORD2_GPU_TRAP_DONE);
	assert(NORD2_GPU_TRAP_MAGIC == (('N' << 24) | ('2' << 16) | ('T' << 8) | 'C') && NORD2_GPU_TRAP_DONE == (('N' << 24) | ('2' << 16) | ('T' << 8) | 'D'));
	/* the frontend's availability probe expects an 88-byte STATUS reply */
	assert(sizeof(struct nord2_gpu_status) == 88);
	assert(NORD2_GPU_TRAP_STRUCT_OUT_MAX >= sizeof(struct nord2_gpu_status) && NORD2_GPU_TRAP_STRUCT_OUT_MAX >= sizeof(struct nord2_gpu_fault_info));
	assert(NORD2_GPU_TRAP_STRUCT_IN_MAX >= sizeof(struct nord2_gpu_submit_request) && NORD2_GPU_TRAP_STRUCT_IN_MAX == 16384);

	/* validation */
	struct nord2_gpu_trap_call c = good();
	assert(nord2_gpu_trap_call_ok(&c, sizeof(c)));
	assert(!nord2_gpu_trap_call_ok(NULL, sizeof(c)));
	assert(!nord2_gpu_trap_call_ok(&c, sizeof(c) - 1) && !nord2_gpu_trap_call_ok(&c, sizeof(c) + 8) && !nord2_gpu_trap_call_ok(&c, 0));
	c = good(); c.magic ^= 1; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.done = NORD2_GPU_TRAP_DONE; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));         /* a stale reply is not a request */
	c = good(); c.done = 1; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.scalar_in_count = 4; c.scalar_out_count = 4; assert(nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.scalar_in_count = 5; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.scalar_out_count = 5; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.struct_in = 0x1000; c.struct_in_size = NORD2_GPU_TRAP_STRUCT_IN_MAX; assert(nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c.struct_in_size++; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.struct_in_size = 16; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));                  /* size without a pointer */
	c = good(); c.struct_out = 0x1000; c.struct_out_size = NORD2_GPU_TRAP_STRUCT_OUT_MAX; assert(nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c.struct_out_size++; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.struct_out_size = 88; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.struct_in = 0x1000; c.struct_in_size = 0; assert(nord2_gpu_trap_call_ok(&c, sizeof(c)));  /* pointer without size: nothing is read */
	c = good(); c.struct_in_size = ~0ull; c.struct_in = 1; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));
	c = good(); c.struct_out_size = ~0ull; c.struct_out = 1; assert(!nord2_gpu_trap_call_ok(&c, sizeof(c)));

	/* the calls the frontend makes pass the SAME validator as the MIG path (counts and sizes as bridge/probe build them) */
	uint64_t in[4] = { 0 }, out[4] = { 0 };
	struct nord2_gpu_status st; struct nord2_gpu_submit_request rq; struct nord2_gpu_fault_info fi;
	c = good(); c.selector = NORD2_GPU_SEL_STATUS; c.struct_out = 0x1000; c.struct_out_size = sizeof(st);     /* the availability probe */
	assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && nord2_gpu_call_valid(c.selector, c.scalar_in_count, c.scalar_out_count, 0, c.struct_out_size, in, out, NULL, &st));
	c = good(); c.selector = NORD2_GPU_SEL_SUBMIT_JM; c.scalar_out_count = 1; c.struct_in = 0x1000; c.struct_in_size = sizeof(rq);
	assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && nord2_gpu_call_valid(c.selector, 0, c.scalar_out_count, c.struct_in_size, 0, in, out, &rq, NULL));
	c = good(); c.selector = NORD2_GPU_SEL_WAIT_SEQ; c.scalar_in_count = 2; c.scalar_out_count = 3;
	assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && nord2_gpu_call_valid(c.selector, 2, 3, 0, 0, in, out, NULL, NULL));
	c.struct_out = 0x1000; c.struct_out_size = sizeof(fi);
	assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && nord2_gpu_call_valid(c.selector, 2, 3, 0, c.struct_out_size, in, out, NULL, &fi));
	/* 0112: the reservation calls as the frontend issues them (probe n=0; RESERVE n=2 with a handle list; RETIRE id,seq) */
	{
		uint32_t hs[2] = { 3, 4 }; uint64_t rin[2] = { 0, 0 };
		c = good(); c.selector = NORD2_GPU_SEL_SURFACE_RESERVE; c.scalar_in_count = 1; c.scalar_out_count = 1; c.scalar_in[0] = 0;
		assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && nord2_gpu_call_valid(c.selector, 1, 1, 0, 0, rin, out, NULL, NULL));
		c = good(); c.selector = NORD2_GPU_SEL_SURFACE_RESERVE; c.scalar_in_count = 1; c.scalar_out_count = 1; c.scalar_in[0] = rin[0] = 2;
		c.struct_in = 0x1000; c.struct_in_size = sizeof(hs);
		assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && nord2_gpu_call_valid(c.selector, 1, 1, c.struct_in_size, 0, rin, out, hs, NULL));
		c.struct_in_size = sizeof(hs) - 4;   /* the trap block accepts it; the shared validator does not */
		assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && !nord2_gpu_call_valid(c.selector, 1, 1, c.struct_in_size, 0, rin, out, hs, NULL));
		c = good(); c.selector = NORD2_GPU_SEL_SURFACE_RETIRE; c.scalar_in_count = 2; c.scalar_in[0] = 1; c.scalar_in[1] = 42;
		assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && nord2_gpu_call_valid(c.selector, 2, 0, 0, 0, c.scalar_in, NULL, NULL, NULL));
		/* the reply carries the reservation id */
		uint64_t idsc[1] = { 5 }; c = good(); c.selector = NORD2_GPU_SEL_SURFACE_RESERVE;
		assert(nord2_gpu_trap_finish(&c, 0, idsc, 1, 0) == 0 && c.done == NORD2_GPU_TRAP_DONE && c.scalar_out_count == 1 && c.scalar_out[0] == 5);
	}
	c = good(); c.selector = NORD2_GPU_SEL_LOG; c.struct_in = 0x1000; c.struct_in_size = 16384;
	assert(nord2_gpu_trap_call_ok(&c, sizeof(c)));
	/* a wrong shape that the trap block accepts is still refused by the shared validator (the handler runs the same check) */
	c = good(); c.selector = NORD2_GPU_SEL_WAIT_SEQ; c.scalar_in_count = 1; c.scalar_out_count = 3;
	assert(nord2_gpu_trap_call_ok(&c, sizeof(c)) && !nord2_gpu_call_valid(c.selector, 1, 3, 0, 0, in, out, NULL, NULL));

	/* reply packing */
	c = good(); c.selector = NORD2_GPU_SEL_WAIT_SEQ; c.scalar_in_count = 2; c.scalar_out_count = 3;
	uint64_t sc[8] = { 4, 7, 0, 9, 9, 9, 9, 9 };
	assert(nord2_gpu_trap_finish(&c, 0, sc, 3, 0) == 0 && c.done == NORD2_GPU_TRAP_DONE && c.result == 0);
	assert(c.scalar_out_count == 3 && c.scalar_out[0] == 4 && c.scalar_out[1] == 7 && c.scalar_out[2] == 0 && c.scalar_out[3] == 0);
	c = good(); assert(nord2_gpu_trap_finish(&c, (int32_t)0xe00002c2u, sc, 8, 0) == 0);                      /* more than 4 scalars: clamped */
	assert(c.scalar_out_count == 4 && c.scalar_out[3] == 9 && c.result == (int32_t)0xe00002c2u && c.done == NORD2_GPU_TRAP_DONE);
	c = good(); c.scalar_out[3] = 77; (void)nord2_gpu_trap_finish(&c, 0, sc, 1, 0);
	assert(c.scalar_out_count == 1 && c.scalar_out[1] == 0 && c.scalar_out[3] == 0);                           /* unused slots are cleared */
	c = good(); assert(nord2_gpu_trap_finish(&c, 0, sc, 0, 88) == 88 && c.struct_out_size == 88);
	c = good(); assert(nord2_gpu_trap_finish(&c, 0, sc, 0, 100000) == NORD2_GPU_TRAP_STRUCT_OUT_MAX && c.struct_out_size == NORD2_GPU_TRAP_STRUCT_OUT_MAX);
	/* a failed method still completes the trap: the result carries the error, done proves it ran */
	c = good(); (void)nord2_gpu_trap_finish(&c, (int32_t)0xe00002bcu, sc, 0, 0);
	assert(c.done == NORD2_GPU_TRAP_DONE && (uint32_t)c.result == 0xe00002bcu);
	puts("nord2_trap_abi_test OK");
	return 0;
}
