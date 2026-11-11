/* Host test for the /dev/redwood-mic jitter buffer (audio_mic.inc, NMIC_RING_ONLY): priming, underrun, overflow drop of
 * the oldest, sample order across wraps, gain saturation, and a long run of two drifting clocks (worker 96 samples
 * every 2 ms by the AFE clock vs the HAL reading 1024 per 21.33 ms by mach time, +-200 ppm). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NMIC_RING_ONLY
#include "../src/bsd/dev/audio_mic.inc"

static struct nmic_ring q;

static void
drift(double ppm, unsigned prime, unsigned max, uint64_t *under, uint64_t *drop, uint64_t *gaps, uint32_t *maxlat)
{
	/* 10 minutes. Producer: AFE at 48000*(1+ppm) samples/s in 2 ms bursts (+-1 ms worker jitter). Consumer: 1024 frames
	 * every 1024/48000 s. Samples carry a running counter so order/discontinuities are visible. */
	int16_t buf[2048], out[1024];
	uint16_t next_in = 0, expect = 0;
	double t = 0, prod_t = 0, cons_t = 0, acc = 0;
	unsigned seed = 1;
	*gaps = 0; *maxlat = 0;
	nmic_ring_reset(&q, prime, max);
	while (t < 600.0) {
		double np = prod_t + 0.002 + ((seed = seed * 1103515245u + 12345u) >> 16 & 1023) / 1023.0 * 0.001 - 0.0005;
		double nc = cons_t + 1024.0 / 48000.0;
		if (np < nc) {
			acc += (np - prod_t) * 48000.0 * (1 + ppm * 1e-6);
			unsigned n = (unsigned)acc; acc -= n;
			for (unsigned i = 0; i < n; i++) buf[i] = (int16_t)next_in++;
			nmic_ring_push(&q, buf, n);
			if (q.count > *maxlat) *maxlat = q.count;
			prod_t = t = np;
		} else {
			unsigned got = nmic_ring_pop(&q, out, 1024);
			for (unsigned i = 0; i < got; i++) {
				if ((uint16_t)out[i] != expect) (*gaps)++;
				expect = (uint16_t)(out[i] + 1);
			}
			cons_t = t = nc;
		}
	}
	*under = q.underruns; *drop = q.dropped;
}

int
main(void)
{
	int16_t in[9000], out[9000];
	for (unsigned i = 0; i < 9000; i++) in[i] = (int16_t)(i * 7 - 30000);

	/* priming: nothing until prime samples, then everything that is there */
	nmic_ring_reset(&q, 1000, 4000);
	nmic_ring_push(&q, in, 999);
	assert(nmic_ring_pop(&q, out, 512) == 0 && q.count == 999 && !q.primed);
	nmic_ring_push(&q, in + 999, 1);
	assert(nmic_ring_pop(&q, out, 512) == 512 && q.primed && memcmp(out, in, 512 * 2) == 0);
	assert(nmic_ring_pop(&q, out, 512) == 488 && memcmp(out, in + 512, 488 * 2) == 0);
	assert(!q.primed && q.underruns == 1 && q.count == 0);                 /* ran dry: primes again */
	nmic_ring_push(&q, in, 500);
	assert(nmic_ring_pop(&q, out, 100) == 0);
	/* an exact drain (take == n) is not an underrun */
	nmic_ring_reset(&q, 4, 4000); nmic_ring_push(&q, in, 8);
	assert(nmic_ring_pop(&q, out, 8) == 8 && q.primed && q.underruns == 0);

	/* overflow: the oldest go, the newest max stay, in order */
	nmic_ring_reset(&q, 100, 4000);
	nmic_ring_push(&q, in, 3000);
	nmic_ring_push(&q, in + 3000, 2000);                                   /* down to (100 + 4000) / 2 = 2050 */
	assert(q.count == 2050 && q.dropped == 2950 && q.in == 5000);
	assert(nmic_ring_pop(&q, out, 4000) == 2050 && memcmp(out, in + 2950, 2050 * 2) == 0);
	/* a single push larger than max keeps its tail */
	nmic_ring_reset(&q, 100, 4000);
	nmic_ring_push(&q, in, 9000);
	assert(q.count == 4000 && q.dropped == 5000 && nmic_ring_pop(&q, out, 9000) == 4000 && memcmp(out, in + 5000, 4000 * 2) == 0);
	/* limits clamp: max <= NMIC_RING, prime <= max */
	nmic_ring_reset(&q, 99999, 99999); assert(q.max == NMIC_RING && q.prime == NMIC_RING);
	nmic_ring_reset(&q, 10, 1); assert(q.max == 256 && q.prime == 10);
	/* wrap many times with odd sizes */
	nmic_ring_reset(&q, 1, NMIC_RING);
	unsigned w = 0, r = 0;
	for (unsigned k = 0; k < 2000; k++) {
		unsigned n = 1 + k * 37 % 700;
		int16_t tmp[700];
		for (unsigned i = 0; i < n; i++) tmp[i] = (int16_t)(w + i);
		nmic_ring_push(&q, tmp, n); w += n;
		unsigned got = nmic_ring_pop(&q, out, 1 + k * 53 % 700);
		for (unsigned i = 0; i < got; i++) assert(out[i] == (int16_t)(r + i));
		r += got;
	}
	assert(q.dropped == 0 && q.peak > 0);

	/* gain */
	int16_t g[4] = { 1000, -1000, 30000, -30000 };
	nmic_gain(g, 4, 512);
	assert(g[0] == 2000 && g[1] == -2000 && g[2] == 32767 && g[3] == -32768);
	nmic_gain(g, 4, 256); assert(g[0] == 2000);
	nmic_gain(g, 4, 128); assert(g[0] == 1000 && g[1] == -1000);
	/* sound detection (stereo frames, |x| > 8) */
	int16_t pcm[8] = { 0, 8, -8, 0, 0, 0, 0, 0 };
	assert(!nmic_has_sound(pcm, 4)); pcm[7] = -9; assert(nmic_has_sound(pcm, 4)); assert(!nmic_has_sound(pcm, 3));

	/* drifting clocks with the defaults (prime 2048, max 6144) */
	static const double ppms[] = { 0, 200, -200, 50, -50 };
	for (unsigned i = 0; i < sizeof ppms / sizeof ppms[0]; i++) {
		uint64_t under, drop, gaps; uint32_t maxlat;
		setvbuf(stdout, NULL, _IONBF, 0);
		drift(ppms[i], 2048, 6144, &under, &drop, &gaps, &maxlat);
		printf("drift %+5.0f ppm, 10 min: underruns %llu dropped %llu discontinuities %llu, most waiting %u (%.1f ms)\n", ppms[i],
		    (unsigned long long)under, (unsigned long long)drop, (unsigned long long)gaps, maxlat, maxlat / 48.0);
		fflush(stdout); assert(maxlat <= 6144);
		if (ppms[i] == 0) assert(under == 0 && drop == 0 && gaps == 0);
		if (ppms[i] > 0) assert(under == 0 && gaps <= 1 + (unsigned)(ppms[i] * 600 * 48000e-6 / 2048));   /* one jump per correction */
		if (ppms[i] < 0) assert(drop == 0 && under <= 4);               /* 10 min at -200 ppm: 5760 samples short */
	}
	printf("all /dev/redwood-mic ring host tests passed\n");
	return 0;
}
