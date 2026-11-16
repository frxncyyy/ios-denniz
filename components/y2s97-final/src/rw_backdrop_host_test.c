/*
 * y2s97: rw_backdrop_pure.h on the build machine. (1) hand-made cases, one per rule; (2) a fuzz: random command buffers over a
 * few textures (biased towards the capture pattern) are run through an abstract executor -- every texture's content is a hash
 * of what made it, every read is logged -- before and after the rewrite redwoodmetal.m applies (rw_bdr_rewrite's mirror,
 * below); wherever rwb_find matches, final contents and every read must be identical. Prints 'rw_backdrop: ok ...'.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rw_backdrop_pure.h"

#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); exit(1); } } while (0)
#define NT 6
#define MAXS 40

static struct rwb_step
mk(int kind)
{
	struct rwb_step s;

	memset(&s, 0, sizeof(s));
	s.kind = (uint8_t)kind;
	s.depth = s.src = s.dst = -1;
	for (int i = 0; i < 8; i++) {
		s.color[i] = -1;
	}
	for (int i = 0; i < RWB_TEX; i++) {
		s.tex[i] = -1;
	}
	s.c0_base = 1;
	return s;
}
static struct rwb_step
pass(int t, int load, int sampled)
{
	struct rwb_step s = mk(RWB_DRAW);

	s.color[0] = t;
	s.ncolor = 1;
	s.load0 = (uint8_t)load;
	if (sampled >= 0) {
		s.tex[0] = sampled;
		s.ntex = 1;
	}
	return s;
}
static struct rwb_step
copy(int src, int dst, int full)
{
	struct rwb_step s = mk(RWB_COPY);

	s.src = src;
	s.dst = dst;
	s.copy_full = (uint8_t)full;
	s.copy_shape = 1;
	s.copy_bok = 1;
	return s;
}

static int
run_find(const struct rwb_step *st, unsigned n, struct rwb_match *m, int *reason)
{
	unsigned rej[RWB_R_N] = { 0 }, cand = 0;
	int r = rwb_find(st, n, m, rej, &cand);

	*reason = -1;
	for (int i = 0; i < RWB_R_N; i++) {
		if (rej[i]) {
			*reason = i;
		}
	}
	return r;
}

/* ---- the abstract executor ---- */
static uint64_t
mix(uint64_t h, uint64_t v)
{
	h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	return h * 0xff51afd7ed558ccdULL;
}
struct exec {
	uint64_t content[NT];
	uint64_t reads[MAXS * 2];   /* by original step id */
	int nreads;
};
static void
execute(const struct rwb_step *st, const int *ids, unsigned n, struct exec *e)
{
	for (int t = 0; t < NT; t++) {
		e->content[t] = mix(1000, (uint64_t)t);
	}
	memset(e->reads, 0, sizeof(e->reads));
	for (unsigned i = 0; i < n; i++) {
		const struct rwb_step *s = &st[i];
		int id = ids[i];
		uint64_t in = mix(77, (uint64_t)(id < 0 ? 999 : id));

		if (s->kind == RWB_DRAW || s->kind == RWB_COMPUTE) {
			for (unsigned j = 0; j < s->ntex; j++) {
				if (s->tex[j] >= 0) {
					in = mix(in, e->content[s->tex[j]]);
				}
			}
		}
		if (s->kind == RWB_COPY) {
			in = mix(in, e->content[s->src]);
		}
		if (id >= 0) {
			e->reads[id] = in;
		}
		if (s->kind == RWB_DRAW || s->kind == RWB_CLEAR) {
			for (unsigned j = 0; j < s->ncolor; j++) {
				int t = s->color[j];
				uint64_t prev = j == 0 ? (s->load0 == RWB_LOAD_LOAD ? e->content[t] : s->load0 == RWB_LOAD_CLEAR ? 5 : 6) : e->content[t];

				/* the pass's result in attachment j: what it started from + what it read + (the draw itself: its id) */
				e->content[t] = mix(mix(prev, s->kind == RWB_DRAW ? in : 3), (uint64_t)j);
			}
			if (s->depth >= 0) {
				e->content[s->depth] = mix(e->content[s->depth], in);
			}
		} else if (s->kind == RWB_COPY) {
			e->content[s->dst] = s->copy_full ? e->content[s->src] : mix(e->content[s->dst], e->content[s->src]);
		} else if (s->kind == RWB_COMPUTE) {
			for (unsigned j = 0; j < s->ntex; j++) {
				if (s->tex[j] >= 0) {
					e->content[s->tex[j]] = mix(e->content[s->tex[j]], in);
				}
			}
		}
	}
}

/* the mirror of redwoodmetal.m's rewrite: attachment 0 of the S passes in [f, k) -> B, drop k, whole copy B -> S before l */
static unsigned
rewrite(const struct rwb_step *st, const int *ids, unsigned n, const struct rwb_match *m, struct rwb_step *out, int *oids)
{
	unsigned o = 0;

	for (unsigned i = 0; i < n; i++) {
		if (i == m->k) {
			continue;
		}
		if (i == m->l) {
			out[o] = copy(m->b, m->s, 1);
			oids[o++] = -1;
		}
		out[o] = st[i];
		if (i >= m->f && i < m->k && (st[i].kind == RWB_DRAW || st[i].kind == RWB_CLEAR) && st[i].color[0] == m->s) {
			out[o].color[0] = m->b;
		}
		oids[o++] = ids[i];
	}
	return o;
}

static int
equivalent(const struct rwb_step *st, unsigned n, const struct rwb_match *m)
{
	int ids[MAXS] = { 0 }, oids[MAXS + 1] = { 0 };
	struct rwb_step out[MAXS + 1];
	struct exec a, b;

	for (unsigned i = 0; i < n; i++) {
		ids[i] = (int)i;
	}
	unsigned on = rewrite(st, ids, n, m, out, oids);

	execute(st, ids, n, &a);
	execute(out, oids, on, &b);
	if (memcmp(a.content, b.content, sizeof(a.content)) != 0) {
		return 0;
	}
	for (unsigned i = 0; i < n; i++) {
		if (i != m->k && a.reads[i] != b.reads[i]) {
			return 0;
		}
	}
	return 1;
}

static unsigned rng = 12345;
static unsigned
rnd(unsigned n)
{
	rng = rng * 1103515245u + 12345u;
	return (rng >> 8) % n;
}

static struct rwb_step
random_step(void)
{
	unsigned k = rnd(20);
	struct rwb_step s;

	if (k < 9) {
		s = pass((int)rnd(NT), (int)rnd(3), rnd(2) ? (int)rnd(NT) : -1);
		if (rnd(6) == 0) {
			s.color[1] = (int)rnd(NT);
			s.ncolor = 2;
			if (s.color[1] == s.color[0]) {
				s.ncolor = 1;
				s.color[1] = -1;
			}
		}
		if (rnd(10) == 0) {
			s.depth = (int)rnd(NT);
		}
		if (rnd(25) == 0) {
			s.argbuf = 1;
		}
		if (rnd(25) == 0) {
			s.c0_base = 0;
		}
	} else if (k < 15) {
		int a = (int)rnd(NT), b = (int)rnd(NT);

		s = copy(a, b == a ? (a + 1) % NT : b, rnd(5) != 0);
		if (rnd(20) == 0) {
			s.copy_shape = 0;
		}
		if (rnd(20) == 0) {
			s.copy_bok = 0;
		}
	} else if (k < 17) {
		s = mk(RWB_COMPUTE);
		s.tex[0] = (int)rnd(NT);
		s.ntex = 1;
	} else if (k < 18) {
		s = mk(RWB_OPAQUE);
	} else {
		s = mk(RWB_CLEAR);
		s.color[0] = (int)rnd(NT);
		s.ncolor = 1;
		s.load0 = RWB_LOAD_CLEAR;
	}
	return s;
}

int
main(void)
{
	struct rwb_step st[MAXS];
	struct rwb_match m;
	int why;
	unsigned n;

	/* T1 the pattern: S=0 cleared and drawn, copy 0->1 whole, blur 1->2, 2->3, S loaded and drawn sampling 3 */
	n = 0;
	st[n++] = pass(0, RWB_LOAD_CLEAR, 4);
	st[n++] = pass(0, RWB_LOAD_LOAD, 5);
	st[n++] = copy(0, 1, 1);
	st[n++] = pass(2, RWB_LOAD_DONTCARE, 1);
	st[n++] = pass(3, RWB_LOAD_DONTCARE, 2);
	st[n++] = pass(0, RWB_LOAD_LOAD, 3);
	CHECK(run_find(st, n, &m, &why) && m.f == 0 && m.k == 2 && m.l == 5 && m.s == 0 && m.b == 1, "T1 match");
	CHECK(equivalent(st, n, &m), "T1 equivalent");
	/* T2 partial copy: untouched */
	st[2].copy_full = 0;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_PARTIAL, "T2 partial");
	st[2].copy_full = 1;
	/* T3 S first loaded (not cleared) */
	st[0].load0 = RWB_LOAD_LOAD;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_FIRST, "T3 first not clear");
	st[0].load0 = RWB_LOAD_CLEAR;
	/* T4 S sampled before the copy */
	st[1].tex[0] = 0;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_SUSED, "T4 S sampled in [f,k)");
	st[1].tex[0] = 5;
	/* T5 B sampled before the copy */
	st[1].tex[0] = 1;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_BUSED, "T5 B used in [f,k)");
	st[1].tex[0] = 5;
	/* T6 B written between copy and Load pass */
	st[3].color[0] = 1;
	st[3].tex[0] = 4;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_BWRITTEN, "T6 B written in (k,l)");
	st[3].color[0] = 2;
	st[3].tex[0] = 1;
	/* T7 S sampled between copy and Load pass -> the next S use is not a Load pass */
	st[4].tex[0] = 0;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_LATER, "T7 S sampled in (k,l)");
	st[4].tex[0] = 2;
	/* T8 the later S pass clears */
	st[5].load0 = RWB_LOAD_CLEAR;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_LATER, "T8 later pass clears");
	st[5].load0 = RWB_LOAD_LOAD;
	/* T9 nothing after */
	CHECK(!run_find(st, 5, &m, &why) && why == RWB_R_NOLATER, "T9 no later use");
	/* T10 opaque blit inside */
	{
		struct rwb_step sv = st[3];

		st[3] = mk(RWB_OPAQUE);
		CHECK(!run_find(st, n, &m, &why) && why == RWB_R_OPAQUE, "T10 opaque");
		st[3] = sv;
	}
	/* T11 argument buffers */
	st[4].argbuf = 1;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_ARGBUF, "T11 argbuf");
	st[4].argbuf = 0;
	/* T12 shape / B shared */
	st[2].copy_shape = 0;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_SHAPE, "T12 shape");
	st[2].copy_shape = 1;
	st[2].copy_bok = 0;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_BSHARED, "T12 b shared");
	st[2].copy_bok = 1;
	/* T13 S as attachment 1 of a pass in [f,k) */
	st[1].color[1] = 0;
	st[1].ncolor = 2;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_SUSED, "T13 S attachment 1");
	st[1].color[1] = -1;
	st[1].ncolor = 1;
	/* T14 S at another level */
	st[1].c0_base = 0;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_SUSED, "T14 S level");
	st[1].c0_base = 1;
	/* T15 B the attachment 1 of the Load pass */
	st[5].color[1] = 1;
	st[5].ncolor = 2;
	CHECK(!run_find(st, n, &m, &why) && why == RWB_R_LATER, "T15 B attachment of l");
	st[5].color[1] = -1;
	st[5].ncolor = 1;
	/* T16 compute on B between: refused (may write) */
	{
		struct rwb_step sv = st[3];

		st[3] = mk(RWB_COMPUTE);
		st[3].tex[0] = 1;
		st[3].ntex = 1;
		CHECK(!run_find(st, n, &m, &why) && why == RWB_R_BWRITTEN, "T16 compute on B");
		st[3] = sv;
	}
	/* T17 a copy of a texture not drawn in this buffer: not a candidate at all */
	{
		struct rwb_step t[2] = { copy(4, 1, 1), pass(4, RWB_LOAD_LOAD, -1) };
		unsigned rej[RWB_R_N] = { 0 }, cand = 0;

		CHECK(!rwb_find(t, 2, &m, rej, &cand) && cand == 0, "T17 not a capture");
	}
	CHECK(run_find(st, n, &m, &why) && equivalent(st, n, &m), "T* restored");

	/* fuzz */
	unsigned tried = 0, matched = 0, rejected = 0, rej_by[RWB_R_N] = { 0 };

	for (int iter = 0; iter < 400000; iter++) {
		n = 3 + rnd(MAXS - 8);
		for (unsigned i = 0; i < n; i++) {
			st[i] = random_step();
		}
		if (rnd(2)) {   /* plant the pattern's skeleton at random places */
			unsigned a = rnd(n), b = rnd(n), c = rnd(n);
			int S = (int)rnd(NT), Bt = (S + 1 + (int)rnd(NT - 1)) % NT;

			if (a < b && b < c) {
				st[a] = pass(S, RWB_LOAD_CLEAR, rnd(2) ? (int)rnd(NT) : -1);
				st[b] = copy(S, Bt, 1);
				st[c] = pass(S, RWB_LOAD_LOAD, rnd(2) ? (int)rnd(NT) : -1);
			}
		}
		unsigned rej[RWB_R_N] = { 0 }, cand = 0;

		tried++;
		if (rwb_find(st, n, &m, rej, &cand)) {
			matched++;
			if (!equivalent(st, n, &m)) {
				printf("FAIL fuzz iteration %d: a match whose rewrite is not equivalent (f %u k %u l %u S %d B %d)\n", iter, m.f, m.k,
				    m.l, m.s, m.b);
				for (unsigned i = 0; i < n; i++) {
					printf("  %u kind %d load %d c0 %d c1 %d d %d tex %d src %d dst %d full %d\n", i, st[i].kind, st[i].load0,
					    st[i].color[0], st[i].color[1], st[i].depth, st[i].tex[0], st[i].src, st[i].dst, st[i].copy_full);
				}
				return 1;
			}
		}
		for (int i = 0; i < RWB_R_N; i++) {
			rejected += rej[i];
			rej_by[i] += rej[i];
		}
	}
	CHECK(matched > 1000, "fuzz found too few matches to mean anything");
	printf("rw_backdrop: ok (17 cases; fuzz %u buffers, %u matches all equivalent, %u candidates rejected: partial %u shape %u "
	    "b-shared %u first %u s-used %u b-used %u opaque %u later %u no-later %u b-written %u argbuf %u)\n", tried, matched,
	    rejected, rej_by[0], rej_by[1], rej_by[2], rej_by[3], rej_by[4], rej_by[5], rej_by[6], rej_by[7], rej_by[8], rej_by[9],
	    rej_by[10]);
	return 0;
}
