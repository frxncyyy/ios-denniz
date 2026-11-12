/* 0398 host test: sweet_surface_use.h (IOSurface use count) against a reference model.
 *   cc -O1 -g -fsanitize=address,undefined -pthread -I../b/iokit/Kernel/arm usecount_host_test.c -o t && ./t
 *   cc -O1 -g -fsanitize=thread -pthread -I../b/iokit/Kernel/arm usecount_host_test.c -o tt && ./tt
 * Prints ALL OK or exits 1 at the first failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "sweet_surface_use.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; if (fails > 10) exit(1); } } while (0)

/* ---------------------------------------------------------------- unit cases */
static void unit(void)
{
	struct sweet_use_stats st; memset(&st, 0, sizeof st);
	volatile uint32_t w[2] = { 0, 0 }, kern[2] = { 0, 0 };
	uint8_t a = 0, b = 0;

	/* two processes hold category 0, one category 1 */
	CHECK(sweet_use_user(&a, w, 0, true, SWEET_USE_MODE_ALL, &st), "a inc0");
	CHECK(sweet_use_user(&b, w, 0, true, SWEET_USE_MODE_ALL, &st), "b inc0");
	CHECK(sweet_use_user(&b, w, 1, true, SWEET_USE_MODE_ALL, &st), "b inc1");
	CHECK(w[0] == 2 && w[1] == 1, "words %u %u", w[0], w[1]);
	/* IsInUse = u64 at +0x18 != 0; IsInUseForCategory / AnyOther read the halves */
	uint64_t u64 = (uint64_t)w[0] | ((uint64_t)w[1] << 32);
	CHECK(u64 != 0, "in use");
	/* duplicate increment (framework calls trap 0 only on 0->1: a second one is an error, kept as one hold) */
	CHECK(!sweet_use_user(&a, w, 0, true, SWEET_USE_MODE_ALL, &st) && st.user_inc_dup == 1 && w[0] == 2, "dup");
	/* bad category */
	CHECK(!sweet_use_user(&a, w, 2, true, SWEET_USE_MODE_ALL, &st) && w[0] == 2, "cat 2");
	/* unmatched decrement */
	CHECK(!sweet_use_user(&a, w, 1, false, SWEET_USE_MODE_ALL, &st) && st.user_dec_unmatched == 1 && w[1] == 1, "unmatched");
	/* a decrements: still in use by b */
	CHECK(sweet_use_user(&a, w, 0, false, SWEET_USE_MODE_ALL, &st) && w[0] == 1, "a dec0");
	/* kernel display + GPU holds: category 0 */
	CHECK(sweet_use_kern(kern, w, SWEET_USE_DISPLAY, 1, SWEET_USE_MODE_ALL, &st) && w[0] == 2 && kern[0] == 1, "display +");
	CHECK(sweet_use_kern(kern, w, SWEET_USE_GPU, 1, SWEET_USE_MODE_ALL, &st) && w[0] == 3 && kern[1] == 1, "gpu +");
	CHECK(st.live == 4 && st.live_kind[0] == 1 && st.live_kind[1] == 1, "live %lld", (long long)st.live);
	/* b dies holding both categories: dropped at release */
	CHECK(sweet_use_user_drop(&b, w, &st) == 2 && b == 0 && w[0] == 2 && w[1] == 0 && st.dropped == 2 && st.dropped_maps == 1, "drop");
	CHECK(!sweet_use_free_check(w, kern, &st) && st.free_nonzero == 1, "free with holds is flagged");
	st.free_nonzero = 0;
	CHECK(sweet_use_kern(kern, w, SWEET_USE_DISPLAY, -1, SWEET_USE_MODE_ALL, &st) && sweet_use_kern(kern, w, SWEET_USE_GPU, -1, SWEET_USE_MODE_ALL, &st), "kern -");
	CHECK(w[0] == 0 && w[1] == 0 && st.live == 0, "all back to 0: %u %u live %lld", w[0], w[1], (long long)st.live);
	CHECK(sweet_use_free_check(w, kern, &st) && st.free_nonzero == 0, "free clean");
	/* kernel give-back without a hold: refused, counted in mode 2, silent below */
	CHECK(!sweet_use_kern(kern, w, SWEET_USE_DISPLAY, -1, SWEET_USE_MODE_ALL, &st) && st.kern_unmatched[0] == 1, "kern unmatched 2");
	CHECK(!sweet_use_kern(kern, w, SWEET_USE_DISPLAY, -1, SWEET_USE_MODE_USER, &st) && st.kern_unmatched[0] == 1, "kern unmatched 1 silent");
	/* mode 1: user only; mode 0: nothing new */
	CHECK(!sweet_use_kern(kern, w, SWEET_USE_GPU, 1, SWEET_USE_MODE_USER, &st) && w[0] == 0 && st.refused_off == 1, "mode1 kern refused");
	CHECK(sweet_use_user(&a, w, 0, true, SWEET_USE_MODE_USER, &st) && w[0] == 1, "mode1 user ok");
	CHECK(!sweet_use_user(&b, w, 0, true, SWEET_USE_MODE_OFF, &st) && w[0] == 1, "mode0 user refused");
	/* switching to 0 still drains what exists */
	CHECK(sweet_use_user(&a, w, 0, false, SWEET_USE_MODE_OFF, &st) && w[0] == 0, "mode0 drains");
	/* saturation */
	w[0] = SWEET_USE_SATURATE;
	CHECK(!sweet_use_user(&a, w, 0, true, SWEET_USE_MODE_ALL, &st) && st.saturated == 1 && a == 0 && w[0] == SWEET_USE_SATURATE, "saturate");
	w[0] = 0;
	/* underflow can never happen even if a hold bit says so (word already 0) */
	a = 1;
	CHECK(!sweet_use_user(&a, w, 0, false, SWEET_USE_MODE_ALL, &st) && w[0] == 0, "no underflow");
	CHECK(st.max_word >= 3, "max_word %u", st.max_word);
}

/* ---------------------------------------------------------------- GPU table */
static void gpu_table(void)
{
	static struct sweet_use_gpu_table t; memset(&t, 0, sizeof t);
	int s1, s2, s3; void *out[SWEET_USE_GPU_SLOTS];
	CHECK(sweet_use_gpu_put(&t, 0, &s1) < 0 && sweet_use_gpu_put(&t, 5, NULL) < 0, "bad args");
	CHECK(sweet_use_gpu_put(&t, 10, &s1) >= 0 && sweet_use_gpu_put(&t, 10, &s2) >= 0 && sweet_use_gpu_put(&t, 11, &s1) >= 0, "put");
	CHECK(sweet_use_gpu_has(&t, 10, &s1) && !sweet_use_gpu_has(&t, 10, &s3), "has");
	uint64_t live[] = { 11 };
	unsigned k = sweet_use_gpu_reap(&t, live, 1, out, SWEET_USE_GPU_SLOTS);
	CHECK(k == 2 && t.used == 1 && ((out[0] == &s1 && out[1] == &s2) || (out[0] == &s2 && out[1] == &s1)), "reap job 10: k %u used %u", k, t.used);
	k = sweet_use_gpu_reap(&t, NULL, 0, out, SWEET_USE_GPU_SLOTS);
	CHECK(k == 1 && out[0] == &s1 && t.used == 0, "reap all");
	for (unsigned i = 0; i < SWEET_USE_GPU_SLOTS; i++) CHECK(sweet_use_gpu_put(&t, 100 + i, &s3) >= 0, "fill %u", i);
	CHECK(sweet_use_gpu_put(&t, 999, &s3) < 0 && t.full == 1, "full");
	/* out_cap smaller than ready: the rest stays */
	k = sweet_use_gpu_reap(&t, NULL, 0, out, 7);
	CHECK(k == 7 && t.used == SWEET_USE_GPU_SLOTS - 7, "partial reap");
	k = sweet_use_gpu_reap(&t, NULL, 0, out, SWEET_USE_GPU_SLOTS);
	CHECK(k == SWEET_USE_GPU_SLOTS - 7 && t.used == 0 && t.max_used == SWEET_USE_GPU_SLOTS, "rest");
}

/* ---------------------------------------------------------------- random model
 * NS surfaces, NP processes.  Each process has IOSurface.framework's local counters per surface per category (saturating u32) and a
 * mapping (or not).  Kernel holders: display references (a multiset), GPU jobs (table).  After every step the words must equal the
 * reference: number of (mapped process, category) with local count > 0 (as seen by the kernel), + display refs + GPU holds. */
#define NS 24
#define NP 7
struct surf { volatile uint32_t w[2], kern[2]; int display_refs; int gpu_holds; };
struct proc { int mapped[NS]; uint8_t held[NS]; uint32_t local[NS][2]; };
static struct surf S[NS];
static struct proc P[NP];
static struct sweet_use_stats ST;
static struct sweet_use_gpu_table GT;
static uint64_t job_next = 1;
static uint64_t jobs_live[64]; static unsigned njobs;

static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(uint32_t n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng % n); }

/* the framework's IncrementUseCountForCategory: local count +1 (saturating), trap 0 on the 0 -> 1 step */
static void fw_inc(int p, int s, int c, int mode)
{
	uint32_t old = P[p].local[s][c];
	if (old != 0xffffffffu) P[p].local[s][c] = old + 1;
	if (old == 0 && P[p].mapped[s]) sweet_use_user(&P[p].held[s], S[s].w, (uint32_t)c, true, mode, &ST);
}
static void fw_dec(int p, int s, int c, int mode)
{
	uint32_t old = P[p].local[s][c];
	if (old != 0) P[p].local[s][c] = old - 1;
	if (old == 1 && P[p].mapped[s]) sweet_use_user(&P[p].held[s], S[s].w, (uint32_t)c, false, mode, &ST);
}

static void check_all(const char *when, unsigned step)
{
	int64_t live = 0;
	for (int s = 0; s < NS; s++) {
		uint32_t exp[2] = { 0, 0 };
		for (int p = 0; p < NP; p++) for (int c = 0; c < 2; c++) if (P[p].held[s] & (1u << c)) exp[c]++;
		exp[0] += (uint32_t)(S[s].kern[0] + S[s].kern[1]);
		CHECK(S[s].w[0] == exp[0] && S[s].w[1] == exp[1], "%s step %u surface %d: words %u/%u expected %u/%u", when, step, s, S[s].w[0], S[s].w[1], exp[0], exp[1]);
		CHECK((int)S[s].kern[0] == S[s].display_refs || S[s].kern[0] <= (uint32_t)S[s].display_refs, "display holds %u > refs %d", S[s].kern[0], S[s].display_refs);
		CHECK((int)S[s].kern[1] == S[s].gpu_holds, "gpu holds %u != %d", S[s].kern[1], S[s].gpu_holds);
		live += S[s].w[0] + S[s].w[1];
	}
	CHECK(ST.live == live, "%s step %u: live %lld != sum of words %lld", when, step, (long long)ST.live, (long long)live);
	int64_t incs = (int64_t)(ST.user_inc + ST.kern_inc[0] + ST.kern_inc[1]), decs = (int64_t)(ST.user_dec + ST.kern_dec[0] + ST.kern_dec[1] + ST.dropped);
	CHECK(incs - decs == ST.live, "%s step %u: incs %lld - decs %lld != live %lld", when, step, (long long)incs, (long long)decs, (long long)ST.live);
}

static void gpu_reap(unsigned keep)
{
	/* jobs finish oldest-first except a random one may finish early */
	while (njobs > keep) {
		unsigned i = rnd(4) == 0 ? rnd(njobs) : 0;
		jobs_live[i] = jobs_live[--njobs];
	}
	void *out[SWEET_USE_GPU_SLOTS];
	unsigned k = sweet_use_gpu_reap(&GT, jobs_live, njobs, out, SWEET_USE_GPU_SLOTS);
	for (unsigned i = 0; i < k; i++) {
		struct surf *s = (struct surf *)out[i];
		CHECK(sweet_use_kern(s->kern, s->w, SWEET_USE_GPU, -1, SWEET_USE_MODE_ALL, &ST), "gpu give back");
		s->gpu_holds--;
	}
}

static void model(unsigned steps)
{
	int mode = SWEET_USE_MODE_ALL;
	for (unsigned step = 0; step < steps; step++) {
		int p = (int)rnd(NP), s = (int)rnd(NS), c = rnd(8) == 0 ? 1 : 0;
		switch (rnd(12)) {
		case 0: case 1: case 2: fw_inc(p, s, c, mode); break;
		case 3: case 4: case 5: fw_dec(p, s, c, mode); break;
		case 6:     /* lookup (map) / release (unmap: drops what the mapping held; the process forgets the surface) */
			if (!P[p].mapped[s]) { P[p].mapped[s] = 1; P[p].local[s][0] = P[p].local[s][1] = 0; }
			else if (rnd(3) == 0) { sweet_use_user_drop(&P[p].held[s], S[s].w, &ST); P[p].mapped[s] = 0; P[p].local[s][0] = P[p].local[s][1] = 0; }
			break;
		case 7:     /* display swap: takes holds on 1..3 surfaces, gives back an older one */
			if (rnd(2)) { if (sweet_use_kern(S[s].kern, S[s].w, SWEET_USE_DISPLAY, 1, mode, &ST)) S[s].display_refs++; else if (mode == 2) CHECK(0, "display take refused in mode 2"); }
			else if (S[s].display_refs > 0) { S[s].display_refs--; sweet_use_kern(S[s].kern, S[s].w, SWEET_USE_DISPLAY, -1, mode, &ST); }
			break;
		case 8: {   /* GPU submit listing 1..4 surfaces (dups allowed: held once per job) */
			uint64_t job = job_next++;
			if (njobs < 64) jobs_live[njobs++] = job; else break;
			unsigned n = 1 + rnd(4);
			for (unsigned i = 0; i < n; i++) {
				struct surf *x = &S[rnd(NS)];
				if (sweet_use_gpu_has(&GT, job, x)) continue;
				int slot = sweet_use_gpu_put(&GT, job, x);
				if (slot < 0) continue;
				if (!sweet_use_kern(x->kern, x->w, SWEET_USE_GPU, 1, mode, &ST)) { GT.s[slot].surface = NULL; GT.s[slot].job = 0; GT.used--; GT.recorded--; continue; }
				x->gpu_holds++;
			}
			break;
		}
		case 9: gpu_reap(rnd(8)); break;
		case 10:    /* process death: all its mappings released */
			if (rnd(20) == 0) for (int t = 0; t < NS; t++) if (P[p].mapped[t]) { sweet_use_user_drop(&P[p].held[t], S[t].w, &ST); P[p].mapped[t] = 0; P[p].local[t][0] = P[p].local[t][1] = 0; }
			break;
		case 11:    /* mode changes (rare): 2 -> 1 -> 0 -> 2 */
			if (rnd(500) == 0) mode = mode == 2 ? 1 : mode == 1 ? 0 : 2;
			break;
		}
		check_all("model", step);
		if (fails) return;
	}
	/* wind down: every process dies, display lets go, GPU finishes -> every word 0, live 0, no leak */
	for (int p = 0; p < NP; p++) for (int t = 0; t < NS; t++) if (P[p].mapped[t]) { sweet_use_user_drop(&P[p].held[t], S[t].w, &ST); P[p].mapped[t] = 0; }
	for (int t = 0; t < NS; t++) while (S[t].display_refs > 0) { S[t].display_refs--; sweet_use_kern(S[t].kern, S[t].w, SWEET_USE_DISPLAY, -1, SWEET_USE_MODE_ALL, &ST); }
	gpu_reap(0);
	check_all("end", steps);
	for (int t = 0; t < NS; t++) CHECK(sweet_use_free_check(S[t].w, S[t].kern, &ST), "surface %d not 0 at the end", t);
	CHECK(ST.live == 0 && ST.free_nonzero == 0 && GT.used == 0, "end: live %lld free_nonzero %llu gpu used %u", (long long)ST.live,
	    (unsigned long long)ST.free_nonzero, GT.used);
	printf("model: %u steps, user inc/dec %llu/%llu, display %llu/%llu, gpu %llu/%llu, dropped %llu, dup %llu, unmatched %llu, refused-off %llu, max-live %lld, max-word %u, gpu max %u\n",
	    steps, (unsigned long long)ST.user_inc, (unsigned long long)ST.user_dec, (unsigned long long)ST.kern_inc[0], (unsigned long long)ST.kern_dec[0],
	    (unsigned long long)ST.kern_inc[1], (unsigned long long)ST.kern_dec[1], (unsigned long long)ST.dropped, (unsigned long long)ST.user_inc_dup,
	    (unsigned long long)ST.user_dec_unmatched, (unsigned long long)ST.refused_off, (long long)ST.max_live, ST.max_word, GT.max_used);
	CHECK(ST.user_inc_dup == 0, "the framework model never increments twice");
}

/* ---------------------------------------------------------------- threads: atomicity of the shared words */
#define NT 6
#define ITER 200000
static volatile uint32_t TW[2], TK[2];
static struct sweet_use_stats TST;
static void *worker(void *arg)
{
	long id = (long)arg;
	uint8_t held = 0;
	for (int i = 0; i < ITER; i++) {
		if (id & 1) {
			if (sweet_use_kern(TK, TW, (int)(id % 4 == 1), 1, SWEET_USE_MODE_ALL, &TST)) sweet_use_kern(TK, TW, (int)(id % 4 == 1), -1, SWEET_USE_MODE_ALL, &TST);
		} else {
			uint32_t c = (uint32_t)(i & 1);
			sweet_use_user(&held, TW, c, true, SWEET_USE_MODE_ALL, &TST);
			if (i % 3) sweet_use_user(&held, TW, c, false, SWEET_USE_MODE_ALL, &TST);
		}
	}
	sweet_use_user_drop(&held, TW, &TST);
	return NULL;
}
static void threads(void)
{
	pthread_t t[NT];
	for (long i = 0; i < NT; i++) pthread_create(&t[i], NULL, worker, (void *)i);
	for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
	CHECK(TW[0] == 0 && TW[1] == 0 && TK[0] == 0 && TK[1] == 0 && TST.live == 0 && TST.live_kind[0] == 0 && TST.live_kind[1] == 0,
	    "threads: words %u/%u kern %u/%u live %lld", TW[0], TW[1], TK[0], TK[1], (long long)TST.live);
	CHECK(TST.user_dec_unmatched == 0 && TST.kern_unmatched[0] == 0 && TST.kern_unmatched[1] == 0, "threads: unmatched");
	printf("threads: %d x %d ops, max-live %lld, max-word %u\n", NT, ITER, (long long)TST.max_live, TST.max_word);
}

/* ---------------------------------------------------------------- display pool: registry, classification, admission */
static void pool_helpers(void)
{
	static struct sweet_pool_reg r; memset(&r, 0, sizeof r);
	static int objs[SWEET_POOL_REG_MAX + 2];
	for (unsigned i = 0; i < SWEET_POOL_REG_MAX; i++) CHECK(sweet_pool_reg_add(&r, &objs[i]), "add %u", i);
	CHECK(!sweet_pool_reg_add(&r, &objs[SWEET_POOL_REG_MAX]) && r.overflow == 1 && r.n == SWEET_POOL_REG_MAX, "overflow");
	CHECK(sweet_pool_reg_remove(&r, &objs[5]) && !sweet_pool_reg_remove(&r, &objs[5]) && r.n == SWEET_POOL_REG_MAX - 1, "remove once");
	CHECK(sweet_pool_reg_add(&r, &objs[SWEET_POOL_REG_MAX + 1]) && r.n == SWEET_POOL_REG_MAX, "slot reused");
	/* random add/remove against a reference set */
	memset(&r, 0, sizeof r);
	int in[SWEET_POOL_REG_MAX] = { 0 };
	for (unsigned step = 0; step < 200000; step++) {
		unsigned k = rnd(SWEET_POOL_REG_MAX);
		if (in[k]) { CHECK(sweet_pool_reg_remove(&r, &objs[k]), "rm %u", k); in[k] = 0; }
		else { CHECK(sweet_pool_reg_add(&r, &objs[k]), "add %u", k); in[k] = 1; }
		if (fails) return;
	}
	unsigned cnt = 0; for (unsigned k = 0; k < SWEET_POOL_REG_MAX; k++) cnt += in[k];
	CHECK(r.n == cnt, "registry count %u != %u", r.n, cnt);
	/* classification: the fbhwlive shapes (now 400 s, limit 30) */
	CHECK(sweet_pool_classify(400, 390, 0, 30) == SWEET_POOL_YOUNG, "young");
	CHECK(sweet_pool_classify(400, 30, 399, 30) == SWEET_POOL_ACTIVE, "backboardd framebuffer: active");
	CHECK(sweet_pool_classify(400, 60, 80, 30) == SWEET_POOL_STALE, "app drawable of a closed app: stale");
	CHECK(sweet_pool_classify(400, 60, 0, 30) == SWEET_POOL_NEVER, "switcher snapshot: never shown");
	CHECK(sweet_pool_classify(400, 370, 0, 30) == SWEET_POOL_NEVER && sweet_pool_classify(400, 371, 0, 30) == SWEET_POOL_YOUNG, "limit edge");
	/* admission */
	CHECK(sweet_pool_admit(SWEET_POOL_POLICY_ANY, "SpringBoard"), "policy 0 admits all");
	CHECK(!sweet_pool_admit(SWEET_POOL_POLICY_NO_SPRINGBOARD, "SpringBoard") && sweet_pool_admit(SWEET_POOL_POLICY_NO_SPRINGBOARD, "backboardd") &&
	    sweet_pool_admit(SWEET_POOL_POLICY_NO_SPRINGBOARD, "MobileSlideShow") && sweet_pool_admit(SWEET_POOL_POLICY_NO_SPRINGBOARD, "SpringBoardX") &&
	    sweet_pool_admit(SWEET_POOL_POLICY_NO_SPRINGBOARD, "Spring"), "policy 1");
	CHECK(sweet_pool_admit(SWEET_POOL_POLICY_BACKBOARDD, "backboardd") && !sweet_pool_admit(SWEET_POOL_POLICY_BACKBOARDD, "MobileSlideShow") &&
	    !sweet_pool_admit(SWEET_POOL_POLICY_BACKBOARDD, ""), "policy 2");
	printf("pool helpers: registry %u live after 200k random steps, classification and admission ok\n", r.n);
}

int main(int argc, char **argv)
{
	unsigned steps = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 400000;
	unit();
	gpu_table();
	pool_helpers();
	model(steps);
	threads();
	if (fails) { printf("%d FAILURE(S)\n", fails); return 1; }
	printf("ALL OK\n");
	return 0;
}
