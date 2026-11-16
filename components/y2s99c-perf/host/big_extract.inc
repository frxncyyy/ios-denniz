#define NP_BIG_SLOTS    4u
#define NP_BIG_MIN      (16ull << 20)
#define NP_BIG_CAP      (100ull << 20)
static uint32_t big_h[NP_BIG_SLOTS];            /* parked handles, oldest first; 0 = free */
static uint64_t big_bytes, big_hits, big_miss, big_parked, big_evicted;
static int big_off = 0;
/* y2s99c: the parked big buffers, oldest first (caller holds D.lock) */
static void big_remove(unsigned i)
{
	for (; i + 1 < NP_BIG_SLOTS; i++)
		big_h[i] = big_h[i + 1];
	big_h[NP_BIG_SLOTS - 1] = 0;
}

static unsigned big_drain(void)
{
	unsigned n = 0;

	while (big_h[0]) {
		uint32_t h = big_h[0];

		big_remove(0);
		big_bytes -= D.bos[h].size;
		D.bos[h].pooled = 0;
		if (bo_release_kernel(h) == 0)
			n++;
	}
	return n;
}

/* a parked buffer for a request of size bytes: same size or up to 1/8 larger, the GPU done with it; the closest fit */
static uint32_t big_take(uint64_t size)
{
	unsigned best = NP_BIG_SLOTS;

	for (unsigned i = 0; i < NP_BIG_SLOTS && big_h[i]; i++) {
		const struct bo *b = &D.bos[big_h[i]];

		if (b->size < size || b->size - size > size / 8 || b->last_seq > D.completed)
			continue;
		if (best == NP_BIG_SLOTS || b->size < D.bos[big_h[best]].size)
			best = i;
	}
	if (best == NP_BIG_SLOTS)
		return 0;
	uint32_t h = big_h[best];

	big_remove(best);
	big_bytes -= D.bos[h].size;
	D.bos[h].pooled = 0;
	D.bos[h].last_seq = 0;
	return h;
}

/* park a closed big buffer; returns 1 if it was kept */
static int big_park(uint32_t h)
{
	struct bo *b = &D.bos[h];
	unsigned i;

	if (b->size > NP_BIG_CAP)
		return 0;
	while (big_h[0] && (big_bytes + b->size > NP_BIG_CAP || big_h[NP_BIG_SLOTS - 1])) {     /* the oldest goes back first */
		uint32_t o = big_h[0];

		big_remove(0);
		big_bytes -= D.bos[o].size;
		D.bos[o].pooled = 0;
		(void)bo_release_kernel(o);
		big_evicted++;
	}
	for (i = 0; i < NP_BIG_SLOTS && big_h[i]; i++)
		;
	if (i == NP_BIG_SLOTS)
		return 0;
	big_h[i] = h;
	big_bytes += b->size;
	b->pooled = 1;
	big_parked++;
	return 1;
}

