/*
 * (q10jn) Host test of zero copy in rwcam_render.c: rwcam_render_frame_memory (the nodes' own contexts' slots) and a zero_copy renderer
 * rendering a frame that lies in a slot with that node's context (made-up library: it says DID_ZERO_COPY only when the frame is its own slot's,
 * as air_isp.c). Checks: off -> NULL; on -> the library's memory; a zero_copy renderer with a frame in a slot renders with the node's context
 * (no context of its own made), zero copy; the same renderer with a frame elsewhere, or a renderer that is not zero_copy, uses its own context
 * and uploads; the pictures are the processor's; forget forgets the node contexts too; a slot asked larger moves (the old place is not used).
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "camera_isp.h"
#include "rwcam_render.h"
#include "rwcam_log.h"

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures;
void rwcam_say(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
void rwcam_say_once(const char *what, const char *name, const char *detail) { (void)what; (void)name; (void)detail; }
void rwcam_say_keep(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
unsigned long redwood_cache_slide(void) { return 0; }

#define FW 256
#define FH 192
#define STRIDE (FW * 5 / 4)     /* 320: a multiple of 64 */
#define PW 96
#define PH 72
#define HDR 120

struct surface { uint8_t y[PW * PH], uv[PW * PH / 2]; };
struct rwa_isp { int id; uint8_t *slot[4]; size_t bytes[4]; int forgets; };
static struct rwa_isp ctxs[8];
static int created, zero_copies, uploads, last_ctx = -1;
static uint8_t *moved[16]; static int nmoved;      /* (the places slots moved from: freed at the end) */
static unsigned lib_version(void) { return 1; }
static struct rwa_isp *lib_create(char *why, size_t n) { (void)why; (void)n; ctxs[created].id = created; return &ctxs[created++]; }
static void lib_destroy(struct rwa_isp *i) { (void)i; }
static void *lib_memory(struct rwa_isp *i, unsigned slot, size_t h, size_t b, char *why, size_t n)
{
	(void)why; (void)n;
	if (slot > 3) { return NULL; }
	if (i->bytes[slot] < h + b) {           /* (moves: the old place is poisoned) */
		if (i->slot[slot] != NULL) { memset(i->slot[slot], 0xee, i->bytes[slot]); moved[nmoved++] = i->slot[slot]; }
		i->slot[slot] = malloc(h + b + 64);
		i->bytes[slot] = h + b;
	}
	return i->slot[slot];
}
static int lib_wait(struct rwa_isp *i, char *why, size_t n) { (void)i; (void)why; (void)n; return 0; }
static void lib_forget(struct rwa_isp *i) { i->forgets++; }
static int lib_render(struct rwa_isp *ctx, const struct rwa_isp_frame *fr, const struct rwa_isp_params *p, const struct rwa_isp_output *o,
    struct rwa_isp_result *res, char *why, size_t n)
{
	struct surface *s = o->iosurface;
	struct isp isp;
	struct redwood_camera_frame f;
	bool zc = false;

	(void)why; (void)n;
	for (int k = 0; k < 4; k++) { if (ctx->slot[k] != NULL && fr->raw == ctx->slot[k] + HDR && fr->stride % 64 == 0) { zc = true; } }
	zero_copies += zc; uploads += !zc; last_ctx = ctx->id;
	isp_init(&isp);
	memcpy(isp.wb, p->wb, sizeof(isp.wb)); isp.digital = p->digital;
	for (int i = 0; i < 9; i++) { isp.ccm[i / 3][i % 3] = p->ccm[i]; }
	isp.falloff = p->falloff; isp.zoom = p->zoom; isp.flip = p->flip; memcpy(isp.gamma, p->gamma, sizeof(isp.gamma));
	memset(&f, 0, sizeof(f));
	f.width = fr->width; f.height = fr->height; f.stride = fr->stride; f.format = fr->format; f.bayer = fr->bayer; f.black = fr->black;
	isp_render_nv12(&isp, &f, fr->raw, s->y, PW, s->uv, PW, o->width, o->height, 0, o->height, p->video_range != 0);
	if (res != NULL) { res->flags = zc ? RWA_ISP_DID_ZERO_COPY : 0; }
	return RWA_ISP_OK;
}
static const struct rwa_isp_calls lib = { lib_version, lib_create, lib_destroy, lib_memory, lib_render, lib_wait, lib_forget };
static bool map_surface(struct rwcam_picture *pic) { struct surface *s = pic->context; pic->plane[0] = s->y; pic->stride[0] = PW; pic->plane[1] = s->uv; pic->stride[1] = PW; return true; }
static void apply(size_t n, void *context, void (*work)(void *, size_t)) { for (size_t i = n; i-- > 0;) { work(context, i); } }
static uint64_t clock_ms;
static uint64_t now_ms(void) { return clock_ms; }
static struct rwcam_picture picture(struct surface *s) { struct rwcam_picture p; memset(&p, 0, sizeof(p)); p.format = RWCAM_PF_420V; p.width = PW; p.height = PH; p.iosurface = s; p.map = map_surface; p.context = s; return p; }
static void fill(uint8_t *raw) { for (size_t i = 0; i < (size_t)STRIDE * FH; i++) { raw[i] = i % 5 == 4 ? 0x1b : (uint8_t)(40 + (i * 7 + i / 320 * 3) % 180); } }

int main(void)
{
	static uint8_t heap_raw[STRIDE * FH];
	static struct surface ref, s;
	struct redwood_camera_frame f;
	struct isp isp;
	struct rwcam_renderer r, other;
	struct rwcam_picture p;

	memset(&f, 0, sizeof(f));
	f.width = FW; f.height = FH; f.stride = STRIDE; f.format = REDWOOD_CAMERA_FORMAT_MIPI_RAW10; f.bayer = 1; f.black = 64;
	isp_init(&isp); isp.wb[0] = 1.9f; isp.wb[2] = 1.6f;
	memset(&r, 0, sizeof(r)); r.name = "film"; r.zero_copy = true; r.check_max = -1;
	memset(&other, 0, sizeof(other)); other.name = "still"; other.check_max = -1;
	rwcam_render_checks(0, 0);              /* (no comparisons: the GPU's picture is compared below by hand) */
	rwcam_render_setup(&lib, apply, now_ms);
	fill(heap_raw);
	p = picture(&ref);
	CHECK(rwcam_render(&other, &isp, &f, heap_raw, &p, 1) == RWCAM_BACKEND_GPU && uploads == 1);     /* (the reference, uploaded) */
	int own = last_ctx;

	/* off: no frame memory */
	CHECK(rwcam_render_frame_memory(0, 0, HDR, (size_t)STRIDE * FH) == NULL);
	rwcam_render_zero_copy(true);
	uint8_t *m0 = rwcam_render_frame_memory(0, 0, HDR, (size_t)STRIDE * FH), *m1 = rwcam_render_frame_memory(0, 1, HDR, (size_t)STRIDE * FH);
	CHECK(m0 != NULL && m1 != NULL && m0 != m1);
	CHECK(rwcam_render_frame_memory(2, 0, HDR, 100) == NULL && rwcam_render_frame_memory(0, 2, HDR, 100) == NULL);
	int node_ctx = created - 1;
	fill(m0 + HDR); fill(m1 + HDR);

	/* the film's renderer, frame in slot 0 then 1: the node's context, zero copy, no context of its own */
	p = picture(&s);
	CHECK(rwcam_render(&r, &isp, &f, m0 + HDR, &p, 1) == RWCAM_BACKEND_GPU && zero_copies == 1 && last_ctx == node_ctx && r.gpu == NULL);
	CHECK(memcmp(&s, &ref, sizeof(s)) == 0);
	memset(&s, 0, sizeof(s));
	CHECK(rwcam_render(&r, &isp, &f, m1 + HDR, &p, 1) == RWCAM_BACKEND_GPU && zero_copies == 2 && last_ctx == node_ctx && memcmp(&s, &ref, sizeof(s)) == 0);
	/* the same renderer, a frame elsewhere: a context of its own, uploaded */
	CHECK(rwcam_render(&r, &isp, &f, heap_raw, &p, 1) == RWCAM_BACKEND_GPU && uploads == 2 && r.gpu != NULL && last_ctx != node_ctx);
	/* another output with the slot's frame: its own context, uploaded */
	CHECK(rwcam_render(&other, &isp, &f, m0 + HDR, &p, 1) == RWCAM_BACKEND_GPU && uploads == 3 && last_ctx == own);
	/* forget: the node's context forgets too */
	int before = ctxs[node_ctx].forgets;
	rwcam_render_forget(&r);
	CHECK(ctxs[node_ctx].forgets == before + 1);
	/* a slot asked larger moves: the old place is not taken for a slot any more */
	uint8_t *m0b = rwcam_render_frame_memory(0, 0, HDR, (size_t)STRIDE * FH * 2);
	CHECK(m0b != NULL && m0b != m0);
	fill(m0b + HDR);
	int zc = zero_copies;
	CHECK(rwcam_render(&r, &isp, &f, m0b + HDR, &p, 1) == RWCAM_BACKEND_GPU && zero_copies == zc + 1 && last_ctx == node_ctx);
	for (int k = 0; k < nmoved; k++) { free(moved[k]); }
	for (int c = 0; c < created; c++) { for (int k = 0; k < 4; k++) { free(ctxs[c].slot[k]); } }
	rwcam_render_end(&r); rwcam_render_end(&other);
	printf("t_zerocopy: %s (%d zero copies, %d uploads, %d contexts)\n", failures ? "FAILED" : "all passed", zero_copies, uploads, created);
	return failures != 0;
}
