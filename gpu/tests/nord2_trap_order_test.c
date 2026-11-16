/* The frontend's nord2_transport.inc and gpu/include/nord2_gpu_transport.h both define the IOConnectTrap6 call block.  A translation unit that includes the
 * frontend's definition first (the frontend host tests include compute_frontend.m, and then the transport header) must still compile, and the header's
 * helpers must work on the frontend's struct.  Without the #ifndef NORD2_GPU_TRAP_CALL guard this file fails with "redefinition of nord2_gpu_trap_call". */
#include <stddef.h>
#include <stdint.h>
/* ---- verbatim from gpu/metal/tegu/nord2_transport.inc ---- */
#define NORD2_GPU_TRAP_CALL 0u
#define NORD2_GPU_TRAP_MAGIC 0x4e325443u /* 'N2TC' */
#define NORD2_GPU_TRAP_DONE 0x4e325444u  /* 'N2TD' */
struct nord2_gpu_trap_call {
 uint32_t magic,selector,scalar_in_count,scalar_out_count;
 uint64_t scalar_in[4],scalar_out[4];
 uint64_t struct_in,struct_in_size,struct_out,struct_out_size;
 int32_t result;uint32_t done;
};
/* ---- end verbatim ---- */
#include "nord2_gpu_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
	struct nord2_gpu_trap_call c;
	memset(&c, 0, sizeof(c));
	c.magic = NORD2_GPU_TRAP_MAGIC;
	c.selector = NORD2_GPU_SEL_SURFACE_RETIRE; c.scalar_in_count = 2;
	assert(sizeof(c) == 120 && nord2_gpu_trap_call_ok(&c, sizeof(c)));
	uint64_t sc[1] = { 9 };
	assert(nord2_gpu_trap_finish(&c, 0, sc, 1, 0) == 0 && c.done == NORD2_GPU_TRAP_DONE && c.scalar_out[0] == 9);
	puts("nord2_trap_order_test OK");
	return 0;
}
