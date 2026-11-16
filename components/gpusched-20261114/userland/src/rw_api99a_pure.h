/*
 * y2s99a (API gaps): the decisions of the y2s99a features, as pure C so that the build machine can test them
 * (rw-api99a-host-test.sh: ASan/UBSan, -Werror, fuzz).  redwoodmetal.m and redwooddrawable.m include this file and do the
 * Objective-C around it.  Nothing here calls Metal, IOKit or the kernel.
 *
 * Features (each with its own switch file /var/db/nord2-metal/no-<name>, and all of them with no-api99a):
 *   plib     MTLPipelineLibrary (newPipelineLibraryWithFilePath:error:) for the libraries whose per-GPU cache file this GPU
 *            cannot have: the per-GPU cache is opened EMPTY, so Metal's own MTLPipelineLibrary takes every pipeline from its
 *            descriptor in pipelines.desc and the device compiles it from AIR like any other pipeline.  Evidence: 20H392
 *            Metal MTLPipelineLibraryBuilder::newLibraryWithFile (0x181a3f31c) refuses the library with "Could not open
 *            pipeline cache file: %s" when pipelines.<targetDeviceInfo name> is missing (0x181a3f764); -[_MTLPipelineCache
 *            initWithFilePath:readOnly:deviceInfo:] (0x181a3e2ec) gives a valid EMPTY cache for a nil path (cbz x20).
 *            Users seen in the 20H392 cache: ActivityRingsUI (FitnessWidget: "this draw has no pipeline state", 165 a
 *            boot), RenderBox (SwiftUI), VectorKit (Maps), NanoTimeKit/Pride (watch faces), SiriUICore (Siri orb),
 *            LocalAuthenticationPrivateUI, PassKitUIFoundation (Wallet cards), PencilKit, TextInputUI.
 *            QuartzCore's, RenderBox's and VectorKit's are NOT taken (their callers already fall back to descriptors and work
 *            that way) unless /var/db/nord2-metal/plib-all exists: see rw_plib_excluded.
 *   subrect  a texture over an IOSurface smaller than the surface (Metal allows width/height <= the surface's; the texture
 *            is the top-left corner at the surface's own row length): "no drawable: surface N is 64x64 and the descriptor
 *            asks for 52x52" (mediaserverd, 288 in the logs).
 *   fence    -newFence answers an object (was nil); the encoders' fence calls already are no-ops, which is exact here:
 *            this driver runs one command buffer at a time, in order.
 *   events   -encodeSignalEvent:value: sets the event's signaledValue when the command buffer has completed (was a
 *            no-op: a listener or a CPU waiter never saw it); -encodeWaitForEvent:value: makes the commit wait (bounded)
 *            for the value.
 *   faces    -numFaces said 6 for 2DArray (3) and 2DMultisample (4) and 1 for Cube (5): MTLTextureType numbers fixed.
 *   famlog   every distinct -supportsFamily:/-supportsFeatureSet: question, with the image that asked and Metal's answer
 *            (log only: the answer is never changed).
 *   emptyslot the "left empty" texture warning says whether the slot held nil or a texture this driver could not bind.
 */
#ifndef RW_API99A_PURE_H
#define RW_API99A_PURE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- plib ------------------------------------------------------------------------------------------------------------ */

/*
 * 1 if `path` (a .pipelinelib directory) is one this driver leaves to Metal's own refusal.  Left alone unless `allow_all`
 * (/var/db/nord2-metal/plib-all): the three whose callers already compile every pipeline from a descriptor when the library
 * is nil, and work that way today -- QuartzCore (CA::OGL::new_metal_context: "Metal failed to load pipeline library",
 * then the descriptor path), RenderBox (RB::Device::render_pipeline_state: "no precompiled pipeline", then load_library),
 * VectorKit (Maps: its pipelines are made from descriptors in the logs, and its library has 372 names -- a prewarm of all
 * of them would be a cost Maps does not pay today).
 */
static inline int
rw_plib_excluded(const char *path, int allow_all)
{
	static const char *const kept[] = { "/QuartzCore.framework/", "/RenderBox.framework/", "/VectorKit.framework/" };

	if (path == NULL || path[0] == '\0') {
		return 1;
	}
	if (!allow_all) {
		for (size_t i = 0; i < sizeof(kept) / sizeof(kept[0]); i++) {
			if (strstr(path, kept[i]) != NULL) {
				return 1;
			}
		}
	}
	return 0;
}

/* 1 if `cache_path` is a per-GPU or function cache file of a pipeline library ("<lib>.pipelinelib/pipelines.X" or
 * "<lib>.pipelinelib/functions"), the only paths the empty-cache substitution may answer */
static inline int
rw_plib_cache_path(const char *cache_path)
{
	const char *slash, *dir;

	if (cache_path == NULL) {
		return 0;
	}
	slash = strrchr(cache_path, '/');
	if (slash == NULL || slash == cache_path) {
		return 0;
	}
	/* the directory part must end in ".pipelinelib" */
	dir = slash;
	if ((size_t)(dir - cache_path) < sizeof(".pipelinelib") - 1 ||
	    memcmp(dir - (sizeof(".pipelinelib") - 1), ".pipelinelib", sizeof(".pipelinelib") - 1) != 0) {
		return 0;
	}
	if (strcmp(slash + 1, "functions") == 0) {
		return 1;
	}
	if (strncmp(slash + 1, "pipelines.", 10) == 0 && slash[11] != '\0' && strcmp(slash + 1, "pipelines.desc") != 0) {
		return 1;
	}
	return 0;
}

/*
 * The retain/init/delete/init-again sequence of rw_plc_init_hook, over callbacks so that the build machine can run it against
 * a model of Metal's init (which releases the object when it fails, leaving its C++ cache in the object's slot).
 *   init(ctx, self, use_path)  Metal's -initWithFilePath:readOnly:deviceInfo: with the real path (1) or nil (0); self or NULL
 *   retain / release(ctx, self)
 *   take_cache(ctx, self)      reads and clears the C++ cache slot (+0x10), NULL if empty
 *   delete_cache(ctx, cache)   what -dealloc does with it (vtable +0x8)
 * Returns what the caller of init gets; *emptied = 1 when the empty cache was given.
 */
struct rw_plc_ops {
	void *(*init)(void *ctx, void *self, int use_path);
	void (*retain)(void *ctx, void *self);
	void (*release)(void *ctx, void *self);
	void *(*take_cache)(void *ctx, void *self);
	void (*delete_cache)(void *ctx, void *cache);
};

static inline void *
rw_plc_dance(const struct rw_plc_ops *ops, void *ctx, void *self, int *emptied)
{
	void *r, *cache;

	*emptied = 0;
	ops->retain(ctx, self);
	r = ops->init(ctx, self, 1);
	if (r != NULL) {
		ops->release(ctx, self);
		return r;
	}
	cache = ops->take_cache(ctx, self);
	if (cache != NULL) {
		ops->delete_cache(ctx, cache);
	}
	*emptied = 1;
	return ops->init(ctx, self, 0);    /* consumes the reference taken above, as an init does */
}

/* ---- subrect --------------------------------------------------------------------------------------------------------- */

/*
 * May a texture of want_w x want_h texels of `bpp` bytes be laid over the top-left corner of a one-plane surface of
 * surf_w x surf_h at row_bytes a row?  1 yes (and it is a proper sub-rectangle), 0 no (the sizes are equal, or bigger,
 * or something is zero).  The row check is the one the import makes anyway; it is repeated so that the answer stands
 * alone.
 */
static inline int
rw_subrect_ok(unsigned surf_w, unsigned surf_h, uint64_t row_bytes, unsigned planes, unsigned want_w, unsigned want_h,
    unsigned bpp)
{
	if (planes > 1 || surf_w == 0 || surf_h == 0 || want_w == 0 || want_h == 0 || bpp == 0) {
		return 0;
	}
	if (want_w > surf_w || want_h > surf_h || (want_w == surf_w && want_h == surf_h)) {
		return 0;
	}
	if ((uint64_t)want_w * bpp > row_bytes) {
		return 0;
	}
	return 1;
}

/* ---- faces ----------------------------------------------------------------------------------------------------------- */

/* MTLTextureType: 1D 0, 1DArray 1, 2D 2, 2DArray 3, 2DMultisample 4, Cube 5, CubeArray 6, 3D 7, 2DMultisampleArray 8,
 * TextureBuffer 9 */
static inline unsigned
rw_num_faces(unsigned type)
{
	return (type == 5 || type == 6) ? 6u : 1u;
}

/* ---- events ---------------------------------------------------------------------------------------------------------- */

/*
 * Wait until get(ctx) >= value, polling every `step_us` for at most `timeout_us`; `sleep_us` does the sleeping (usleep
 * on the phone, a counter in the test).  Returns 1 if the value was reached, 0 on the timeout.
 */
static inline int
rw_event_wait(uint64_t (*get)(void *), void *ctx, uint64_t value, unsigned timeout_us, unsigned step_us,
    void (*sleep_us)(void *, unsigned))
{
	unsigned waited = 0;

	if (step_us == 0) {
		step_us = 1;
	}
	for (;;) {
		if (get(ctx) >= value) {
			return 1;
		}
		if (waited >= timeout_us) {
			return 0;
		}
		sleep_us(ctx, step_us);
		waited += step_us;
	}
}

/* ---- famlog ---------------------------------------------------------------------------------------------------------- */

/* a small set of (question, answer, asker) triples already said, so each distinct one is logged once a process */
#define RW_FAMSEEN 128
struct rw_famseen {
	uint64_t key[RW_FAMSEEN];
	unsigned n, dropped;
};

static inline uint64_t
rw_fam_key(unsigned kind, uint64_t family, int answer, const char *asker)
{
	uint64_t h = 1469598103934665603ull;   /* FNV-1a over the asker's name */

	for (const char *p = asker != NULL ? asker : "?"; *p != '\0'; p++) {
		h = (h ^ (unsigned char)*p) * 1099511628211ull;
	}
	h ^= ((uint64_t)kind << 62) ^ ((uint64_t)(answer != 0) << 61) ^ (family * 0x9e3779b97f4a7c15ull);
	return h == 0 ? 1 : h;
}

/* 1 if the key is new (and now remembered), 0 if seen before or the set is full (then counted as dropped) */
static inline int
rw_fam_first(struct rw_famseen *s, uint64_t key)
{
	for (unsigned i = 0; i < s->n; i++) {
		if (s->key[i] == key) {
			return 0;
		}
	}
	if (s->n >= RW_FAMSEEN) {
		s->dropped++;
		return 0;
	}
	s->key[s->n++] = key;
	return 1;
}

/* the name Metal's headers give a family / feature-set number, for the log (NULL when unknown) */
static inline const char *
rw_family_name(uint64_t f)
{
	switch (f) {
	case 1001: return "Apple1"; case 1002: return "Apple2"; case 1003: return "Apple3"; case 1004: return "Apple4";
	case 1005: return "Apple5"; case 1006: return "Apple6"; case 1007: return "Apple7"; case 1008: return "Apple8";
	case 2001: return "Mac1"; case 2002: return "Mac2";
	case 3001: return "Common1"; case 3002: return "Common2"; case 3003: return "Common3";
	case 4001: return "MacCatalyst1"; case 4002: return "MacCatalyst2";
	case 5001: return "Metal3";
	default: return NULL;
	}
}

/* ---- counters -------------------------------------------------------------------------------------------------------- */

/* report at the 1st, 2nd, 4th, 8th ... event */
static inline int
rw_pow2_report(unsigned long n)
{
	return n != 0 && (n & (n - 1)) == 0;
}

#endif
