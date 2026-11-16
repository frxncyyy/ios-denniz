/* y2s99c host test: the big-buffer slots of nord2_pan.c (code extracted verbatim by extract.py) under a random model. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#define NP_BOS 64
struct bo { uint32_t kernel; uint64_t size, last_seq; uint8_t pooled, cls, imported, heap, noexec; };
static struct { struct bo bos[NP_BOS + 1]; uint64_t completed; } D;
static int released[NP_BOS + 1];
static int bo_release_kernel(uint32_t h) { assert(D.bos[h].kernel); assert(!D.bos[h].pooled); released[h]++; memset(&D.bos[h], 0, sizeof(D.bos[h])); return 0; }
#include "big_extract.inc"
static void check(void)
{
	uint64_t sum = 0; int seen[NP_BOS + 1] = { 0 }; int z = 0;
	for (unsigned i = 0; i < NP_BIG_SLOTS; i++) {
		if (!big_h[i]) { z = 1; continue; }
		assert(!z);                         /* packed, oldest first */
		assert(!seen[big_h[i]]++);
		assert(D.bos[big_h[i]].kernel && D.bos[big_h[i]].pooled);
		sum += D.bos[big_h[i]].size;
	}
	assert(sum == big_bytes);
	assert(big_bytes <= NP_BIG_CAP);
	for (unsigned h = 1; h <= NP_BOS; h++) if (D.bos[h].pooled) assert(seen[h]);
}
int main(void)
{
	srand(12345);
	uint64_t takes = 0, hits = 0;
	for (long step = 0; step < 2000000; step++) {
		int op = rand() % 4;
		if (op == 0) {           /* a live buffer closed: park it (if free handle used) */
			unsigned h = 1 + rand() % NP_BOS;
			if (D.bos[h].kernel && !D.bos[h].pooled) {
				uint64_t mib = 17 + rand() % 40;
				(void)big_park(h); (void)mib;
			} else if (!D.bos[h].kernel) {
				D.bos[h] = (struct bo){ .kernel = h, .size = (rand() % 50 == 0 ? 120ull : 17ull + rand() % 40) << 20, .last_seq = D.completed + rand() % 3, .noexec = 1, .cls = 0xff };
			}
		} else if (op == 1) {    /* a request */
			uint64_t size = (16ull << 20) + 1 + (uint64_t)(rand() % (42 << 20));
			uint64_t ls[NP_BOS + 1];
			for (unsigned k = 0; k <= NP_BOS; k++) ls[k] = D.bos[k].last_seq;
			uint32_t h = big_take(size); takes++;
			if (h) {
				assert(ls[h] <= D.completed);      /* never a buffer the GPU may still use */
				hits++;
				assert(D.bos[h].size >= size && D.bos[h].size - size <= size / 8);
				assert(!D.bos[h].pooled && D.bos[h].last_seq == 0);
			}
		} else if (op == 2) {
			D.completed += rand() % 2;
		} else if (rand() % 1000 == 0) {
			(void)big_drain();
			assert(!big_h[0] && big_bytes == 0);
		}
		check();
	}
	printf("bigpool host test ok: 2000000 steps, %llu takes, %llu hits, parked %llu evicted %llu\n",
	       (unsigned long long)takes, (unsigned long long)hits, (unsigned long long)big_parked, (unsigned long long)big_evicted);
	return 0;
}
