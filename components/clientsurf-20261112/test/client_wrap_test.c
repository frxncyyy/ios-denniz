/* 0360 host test: the model of what a client-memory surface wraps (sweet_client_wrap_bytes) and the page list importSurface builds
 * over it (4 KiB GPU pages over a 16 KiB-paged wrap, the surface offset applied once). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../b/iokit/Kernel/arm/sweet_surface_abi.h"

/* importSurface's page list: page i of the import at surface offset `offset` (4 KiB aligned) covers wrap bytes offset+i*4096 */
static int import_pages(uint64_t wrap_len, uint64_t alloc, uint64_t offset, uint64_t bytes, uint64_t *first, uint64_t *last)
{
	uint64_t mapped = (bytes + 4095) & ~4095ull;
	if ((offset & 4095) || offset > alloc || mapped > alloc - offset || offset > wrap_len || mapped > wrap_len - offset)
		return -1;
	*first = offset; *last = offset + mapped - 4096;
	return (int)(mapped / 4096);
}

int main(void)
{
	const uint64_t P = 16384;
	/* Photos' thumbnail: 375x667 '420f', 1032192 bytes, page-aligned creator memory covering the allocation */
	assert(sweet_client_wrap_bytes(0x12c654000ull, 1032192, 1032192, P) == 1032192 + (P - 1032192 % P) % P);
	/* SpringBoard's RGhA image from the boot log: 10754624 used of 10764288: the used range rounded to pages covers the alloc */
	uint64_t w = sweet_client_wrap_bytes(0x12c654000ull, 10754624, 10764288, P);
	assert(w == 10764288 && w % P == 0);
	/* used much shorter than the allocation: only the used range (rounded), the surface is shortened to it */
	assert(sweet_client_wrap_bytes(0x100000000ull, 20000, 1000000, P) == 32768);
	/* unaligned creator memory, no address, nothing used, used past the allocation, wrap-around: keep the snapshot */
	assert(sweet_client_wrap_bytes(0x12c654010ull, 1032192, 1032192, P) == 0);
	assert(sweet_client_wrap_bytes(0, 1032192, 1032192, P) == 0);
	assert(sweet_client_wrap_bytes(0x12c654000ull, 0, 1032192, P) == 0);
	assert(sweet_client_wrap_bytes(0x12c654000ull, 2000000, 1032192, P) == 0);
	assert(sweet_client_wrap_bytes(0xffffffffffffc000ull, 32768, 32768, P) == 0);
	assert(sweet_client_wrap_bytes(0x12c654000ull, 1, 1, 3) == 0);   /* page not a power of two */
	/* the import over the wrapped memory: whole surface at offset 0 -> 252 pages, the last at 1028096 */
	uint64_t f, l;
	uint64_t wl = sweet_client_wrap_bytes(0x12c654000ull, 1032192, 1032192, P);
	assert(import_pages(wl, 1032192, 0, 1032192, &f, &l) == 252 && f == 0 && l == 1028096);
	/* an import past a shortened surface is refused (the driver then converts on the CPU) */
	assert(import_pages(32768, 32768, 0, 1000000, &f, &l) == -1);
	/* the legacy snapshot range helper still behaves */
	assert(sweet_client_range_bytes(0x1000, 0, 1000, 1032192, 1032192) == 1032192);
	printf("CLIENT_WRAP_TEST_OK\n");
	return 0;
}
