/* ccperf-20261114 host test: one copy of nord2_gov.h's GPU policy behind PFX-named entry points (two translation units: 0461 as in the chain, 0492) */
#include <string.h>
static struct n2g_gpu G;
void GLUE(PFX, _reset)(unsigned max_idx, unsigned cur) { memset(&G, 0, sizeof(G)); G.max_idx = (uint8_t)max_idx; G.cur = (uint8_t)cur; }
#ifdef HAVE_MIN
void GLUE(PFX, _min)(unsigned min_idx, unsigned up_boost_pm) { G.min_idx = (uint8_t)min_idx; G.up_boost_pm = (uint16_t)up_boost_pm; }
#endif
/* one 20 ms poll: sample, decide, apply when acting; out = want, why, cap, floor, cur */
void GLUE(PFX, _step)(unsigned busy_pm, uint64_t now_ms, int boost, unsigned level, int act, unsigned out[5])
{
	n2g_gpu_sample(&G, busy_pm);
	unsigned want = n2g_gpu_decide(&G, now_ms, boost != 0, level);
	if (act && want != G.cur) { n2g_gpu_applied(&G, want, now_ms); }
	out[0] = want; out[1] = G.why; out[2] = G.cap; out[3] = G.floor; out[4] = G.cur;
}
