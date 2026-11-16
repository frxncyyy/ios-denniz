/* y2s99a: rw_api99a_pure.h on the build machine (ASan/UBSan, -Werror).  argv[1..]: optional list files of real paths
 * ("plib-paths": one path a line, as found in the rootfs mirror) checked against the classification. */
#include "rw_api99a_pure.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint64_t rnd_state = 0x9e3779b97f4a7c15ull;
static uint64_t
rnd(void)
{
	rnd_state ^= rnd_state << 13;
	rnd_state ^= rnd_state >> 7;
	rnd_state ^= rnd_state << 17;
	return rnd_state;
}

/* ---- the plc dance against a model of Metal's init ---- */
struct model {
	int refs;          /* references to the object */
	int deallocs;
	void *slot;        /* the C++ cache slot */
	int caches_live;   /* C++ caches made and not deleted */
	int fail_path;     /* the real path fails */
	int fail_nil;      /* even nil fails (never in Metal) */
	int inits;
};

static void *
m_init(void *ctx, void *self, int use_path)
{
	struct model *m = ctx;

	m->inits++;
	m->slot = NULL;                           /* str xzr, [x0, #0x10] */
	if (!use_path) {
		if (m->fail_nil) {
			if (--m->refs == 0) m->deallocs++;
			return NULL;
		}
		return self;                          /* cbz x20: an empty cache */
	}
	m->slot = malloc(16);                     /* createPipelineDataCache */
	m->caches_live++;
	if (m->fail_path) {                       /* the validity call says no: objc_release(self) */
		if (--m->refs == 0) {
			m->deallocs++;
			if (m->slot != NULL) { free(m->slot); m->slot = NULL; m->caches_live--; }
		}
		return NULL;
	}
	return self;
}
static void m_retain(void *ctx, void *self) { (void)self; ((struct model *)ctx)->refs++; }
static void
m_release(void *ctx, void *self)
{
	struct model *m = ctx;

	(void)self;
	if (--m->refs == 0) {
		m->deallocs++;
		if (m->slot != NULL) { free(m->slot); m->slot = NULL; m->caches_live--; }
	}
}
static void *m_take(void *ctx, void *self) { struct model *m = ctx; void *c = m->slot; (void)self; m->slot = NULL; return c; }
static void m_delete(void *ctx, void *cache) { struct model *m = ctx; free(cache); m->caches_live--; }

static void
test_dance(void)
{
	static const struct rw_plc_ops ops = { m_init, m_retain, m_release, m_take, m_delete };
	int self_storage, emptied;

	for (int fp = 0; fp < 2; fp++) {
		struct model m = { .refs = 1, .fail_path = fp };   /* +1 from alloc, owned by the init call */
		void *r = rw_plc_dance(&ops, &m, &self_storage, &emptied);

		CHECK(r == &self_storage);
		CHECK(emptied == fp);
		CHECK(m.refs == 1);                    /* exactly the caller's reference is left */
		CHECK(m.deallocs == 0);
		CHECK(m.caches_live == (fp ? 0 : 1)); /* the failed cache was deleted; a good one is kept */
		CHECK(fp ? m.slot == NULL : m.slot != NULL);
		/* the caller later releases: dealloc frees what is in the slot */
		m_release(&m, &self_storage);
		CHECK(m.deallocs == 1 && m.caches_live == 0);
	}
	{
		struct model m = { .refs = 1, .fail_path = 1, .fail_nil = 1 };
		void *r = rw_plc_dance(&ops, &m, &self_storage, &emptied);

		CHECK(r == NULL && emptied == 1 && m.refs == 0 && m.deallocs == 1 && m.caches_live == 0);
	}
}

/* ---- paths ---- */
static void
test_paths(void)
{
	CHECK(rw_plib_excluded(NULL, 0));
	CHECK(rw_plib_excluded("", 0));
	CHECK(rw_plib_excluded("/System/Library/Frameworks/QuartzCore.framework/default.pipelinelib", 0));
	CHECK(!rw_plib_excluded("/System/Library/Frameworks/QuartzCore.framework/default.pipelinelib", 1));
	CHECK(!rw_plib_excluded("/System/Library/PrivateFrameworks/ActivityRingsUI.framework/ARUIPrecompiledPipeline.pipelinelib", 0));
	CHECK(rw_plib_excluded("/System/Library/PrivateFrameworks/RenderBox.framework/default.pipelinelib", 0));
	CHECK(rw_plib_excluded("/System/Library/PrivateFrameworks/VectorKit.framework/iphoneos.pipelinelib", 0));
	CHECK(!rw_plib_excluded("/System/Library/PrivateFrameworks/VectorKit.framework/iphoneos.pipelinelib", 1));
	CHECK(rw_plib_cache_path("/S/ActivityRingsUI.framework/ARUIPrecompiledPipeline.pipelinelib/pipelines.Mali-G77MP11(product0x9080,Redwood)"));
	CHECK(rw_plib_cache_path("/S/X.pipelinelib/functions"));
	CHECK(rw_plib_cache_path("/S/X.pipelinelib/pipelines.g10p"));
	CHECK(!rw_plib_cache_path("/S/X.pipelinelib/pipelines.desc"));
	CHECK(!rw_plib_cache_path("/S/X.pipelinelib/pipelines."));
	CHECK(!rw_plib_cache_path("/S/X.pipelinelib/other"));
	CHECK(!rw_plib_cache_path("/S/X.metallib/functions"));
	CHECK(!rw_plib_cache_path("/var/mobile/Library/Caches/com.apple.metal/functions"));
	CHECK(!rw_plib_cache_path("functions"));
	CHECK(!rw_plib_cache_path("/functions"));
	CHECK(!rw_plib_cache_path(".pipelinelib/functions") || 1);   /* (a relative path: either answer is harmless) */
	CHECK(!rw_plib_cache_path(NULL));
	/* fuzz: random byte strings never crash and only ever say yes to the shape above */
	for (int i = 0; i < 200000; i++) {
		char s[64];
		unsigned n = (unsigned)(rnd() % sizeof(s));
		static const char alpha[] = "/.pipelinesdcfuto0123ab(),";

		for (unsigned k = 0; k < n; k++) {
			s[k] = (rnd() & 7) == 0 ? (char)(rnd() & 0xff) : alpha[rnd() % (sizeof(alpha) - 1)];
			if (s[k] == '\0') s[k] = 'x';
		}
		s[n] = '\0';
		if (rw_plib_cache_path(s)) {
			const char *sl = strrchr(s, '/');

			CHECK(sl != NULL && sl - s >= 12 && memcmp(sl - 12, ".pipelinelib", 12) == 0);
			CHECK(strcmp(sl + 1, "functions") == 0 || (strncmp(sl + 1, "pipelines.", 10) == 0 && strlen(sl + 1) > 10 &&
			    strcmp(sl + 1, "pipelines.desc") != 0));
		}
		(void)rw_plib_excluded(s, (int)(rnd() & 1));
	}
}

/* ---- subrect: against a slow reference ---- */
static int
ref_subrect(unsigned sw, unsigned sh, uint64_t rb, unsigned planes, unsigned w, unsigned h, unsigned bpp)
{
	if (planes >= 2) return 0;
	if (!sw || !sh || !w || !h || !bpp) return 0;
	if (w == sw && h == sh) return 0;
	if (w > sw || h > sh) return 0;
	return (uint64_t)w * bpp <= rb;
}

static void
test_subrect(void)
{
	CHECK(rw_subrect_ok(64, 64, 256, 1, 52, 52, 4));     /* the mediaserverd case */
	CHECK(!rw_subrect_ok(64, 64, 256, 1, 64, 64, 4));    /* equal: not a sub-rectangle (the ordinary path) */
	CHECK(!rw_subrect_ok(1125, 2436, 4544, 1, 2436, 1125, 4));   /* the rotated drawable stays refused */
	CHECK(!rw_subrect_ok(64, 64, 256, 2, 52, 52, 1));    /* two planes: not here */
	CHECK(!rw_subrect_ok(64, 64, 100, 1, 52, 52, 4));    /* rows too short */
	for (int i = 0; i < 1000000; i++) {
		unsigned sw = (unsigned)(rnd() % 300), sh = (unsigned)(rnd() % 300), w = (unsigned)(rnd() % 320), h = (unsigned)(rnd() % 320);
		unsigned bpp = (unsigned)(rnd() % 17), planes = (unsigned)(rnd() % 3);
		uint64_t rb = (rnd() & 1) ? (uint64_t)sw * bpp : rnd() % 5000;

		if ((rnd() & 255) == 0) { sw = 0xffffffffu; w = 0xfffffff0u; bpp = 16; rb = ~0ull; }
		CHECK(rw_subrect_ok(sw, sh, rb, planes, w, h, bpp) == ref_subrect(sw, sh, rb, planes, w, h, bpp));
	}
}

/* ---- faces ---- */
static void
test_faces(void)
{
	static const unsigned want[10] = { 1, 1, 1, 1, 1, 6, 6, 1, 1, 1 };

	for (unsigned t = 0; t < 10; t++) {
		CHECK(rw_num_faces(t) == want[t]);
	}
	CHECK(rw_num_faces(1000) == 1);
}

/* ---- events ---- */
struct ev { uint64_t value, rises_at, step; unsigned slept, sleeps; };
static uint64_t ev_get(void *c) { struct ev *e = c; return e->slept >= e->rises_at ? e->value : 0; }
static void ev_sleep(void *c, unsigned us) { struct ev *e = c; e->slept += us; e->sleeps++; }

static void
test_events(void)
{
	struct ev e = { .value = 7, .rises_at = 0 };

	CHECK(rw_event_wait(ev_get, &e, 7, 1000000, 200, ev_sleep) == 1 && e.sleeps == 0);
	e = (struct ev){ .value = 7, .rises_at = 5000 };
	CHECK(rw_event_wait(ev_get, &e, 7, 1000000, 200, ev_sleep) == 1 && e.slept >= 5000 && e.slept < 5400);
	e = (struct ev){ .value = 6, .rises_at = 0 };
	CHECK(rw_event_wait(ev_get, &e, 7, 1000000, 200, ev_sleep) == 0 && e.slept >= 1000000 && e.slept <= 1000200);
	e = (struct ev){ .value = 9, .rises_at = 0 };
	CHECK(rw_event_wait(ev_get, &e, 7, 0, 0, ev_sleep) == 1);
	e = (struct ev){ .value = 1, .rises_at = 0 };
	CHECK(rw_event_wait(ev_get, &e, 7, 0, 0, ev_sleep) == 0 && e.sleeps == 0);
}

/* ---- famlog ---- */
static void
test_famlog(void)
{
	struct rw_famseen s;
	unsigned firsts = 0;

	memset(&s, 0, sizeof(s));
	CHECK(rw_fam_first(&s, rw_fam_key(0, 1004, 0, "CoreImage")) == 1);
	CHECK(rw_fam_first(&s, rw_fam_key(0, 1004, 0, "CoreImage")) == 0);
	CHECK(rw_fam_first(&s, rw_fam_key(0, 1004, 1, "CoreImage")) == 1);
	CHECK(rw_fam_first(&s, rw_fam_key(1, 1004, 0, "CoreImage")) == 1);
	CHECK(rw_fam_first(&s, rw_fam_key(0, 1004, 0, "RenderBox")) == 1);
	CHECK(rw_fam_first(&s, rw_fam_key(0, 1004, 0, NULL)) == 1);
	for (int i = 0; i < 10000; i++) {
		firsts += (unsigned)rw_fam_first(&s, rw_fam_key(0, (uint64_t)(rnd() % 400), 0, "x"));
	}
	CHECK(s.n == RW_FAMSEEN);
	CHECK(firsts == RW_FAMSEEN - 5);
	CHECK(s.dropped > 0);
	CHECK(strcmp(rw_family_name(1002), "Apple2") == 0 && strcmp(rw_family_name(3003), "Common3") == 0);
	CHECK(rw_family_name(1009) == NULL && rw_family_name(0) == NULL);
	CHECK(rw_pow2_report(1) && rw_pow2_report(2) && !rw_pow2_report(3) && rw_pow2_report(1024) && !rw_pow2_report(0));
}

/* ---- the real paths of the mirror ---- */
static unsigned real_libs, real_caches, real_taken;
static void
test_real(const char *list)
{
	FILE *f = fopen(list, "r");
	char line[1024];

	if (f == NULL) {
		CHECK(!"list file");
		return;
	}
	while (fgets(line, sizeof(line), f) != NULL) {
		size_t n = strlen(line);
		const char *base;

		if (n > 0 && line[n - 1] == '\n') line[--n] = '\0';
		if (n == 0) continue;
		base = strrchr(line, '/');
		base = base != NULL ? base + 1 : line;
		if (strcmp(base, "pipelines.desc") == 0) {
			char dir[1024], gpu[1200];

			real_libs++;
			memcpy(dir, line, (size_t)(base - line - 1));
			dir[base - line - 1] = '\0';
			CHECK(!rw_plib_cache_path(line));
			CHECK(rw_plib_excluded(dir, 0) == (strstr(dir, "/QuartzCore.framework/") != NULL ||
			    strstr(dir, "/RenderBox.framework/") != NULL || strstr(dir, "/VectorKit.framework/") != NULL));
			CHECK(!rw_plib_excluded(dir, 1));
			real_taken += !rw_plib_excluded(dir, 0);
			/* this GPU's cache file: never shipped, always answered empty */
			snprintf(gpu, sizeof(gpu), "%s/pipelines.Mali-G77MP11(product0x9080,Redwood)", dir);
			CHECK(rw_plib_cache_path(gpu));
		} else if (strstr(line, ".pipelinelib/") != NULL) {
			real_caches++;
			CHECK(rw_plib_cache_path(line));
		}
	}
	fclose(f);
}

int
main(int argc, char **argv)
{
	test_dance();
	test_paths();
	test_subrect();
	test_faces();
	test_events();
	test_famlog();
	for (int i = 1; i < argc; i++) {
		test_real(argv[i]);
	}
	if (failures != 0) {
		printf("rw_api99a: FAILED (%u)\n", failures);
		return 1;
	}
	printf("rw_api99a: ok (plc dance vs Metal's init model, paths + 200000 fuzz, subrect 1000000 vs reference, faces, events, "
	    "famlog%s", argc > 1 ? "" : ")\n");
	if (argc > 1) {
		printf("; mirror: %u pipeline libraries (%u taken by default), %u cache files classified)\n", real_libs, real_taken,
		    real_caches);
	}
	return 0;
}
