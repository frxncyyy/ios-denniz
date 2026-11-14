/*
 * Host test of the pictures' two backends (src/rwcam_render.c) with a made-up rwa_isp library whose "GPU" is camera_isp.c writing into a made-up
 * IOSurface: the GPU makes the picture when it can and the processor's is the same picture; the processor makes it -- whole, the same bytes --
 * whenever the GPU is not there, not preferred, not able (BGRA, a lens shading table, no IOSurface) or fails, with the policy of GPU_ISP_API.md for
 * each error (unsupported: stays with the processor; surface: this buffer; GPU: some seconds, and for good after three in a row; no context: for
 * good); the kept surfaces are forgotten and the context given back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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
#define PW 96
#define PH 72

/* a made-up IOSurface: two planes */
struct surface { uint8_t y[PW * PH], uv[PW * PH / 2]; };

/* ---- the made-up library ---- */
struct rwa_isp { int id; };
static struct rwa_isp contexts[4];
static int created, destroyed, renders, forgets, waits, fail_next, fail_code, no_context, partly, wrong;
static unsigned version = 1;
static unsigned lib_version(void) { return version; }
static struct rwa_isp *lib_create(char *why, size_t n) { if (no_context) { snprintf(why, n, "no Nord2GPU service"); return NULL; } contexts[created % 4].id = created; return &contexts[created++ % 4]; }
static void lib_destroy(struct rwa_isp *i) { (void)i; destroyed++; }
static void *lib_memory(struct rwa_isp *i, unsigned slot, size_t h, size_t b, char *why, size_t n) { (void)i; (void)slot; (void)h; (void)b; (void)why; (void)n; return NULL; }
static int lib_wait(struct rwa_isp *i, char *why, size_t n) { (void)i; (void)why; (void)n; waits++; return 0; }
static void lib_forget(struct rwa_isp *i) { (void)i; forgets++; }
static int lib_render(struct rwa_isp *ctx, const struct rwa_isp_frame *fr, const struct rwa_isp_params *p, const struct rwa_isp_output *o,
    struct rwa_isp_result *res, char *why, size_t n)
{
	struct surface *s = o->iosurface;
	struct isp isp;
	struct redwood_camera_frame f;

	(void)ctx;
	renders++;
	if (fr->size != sizeof(*fr) || p->size != sizeof(*p) || o->size != sizeof(*o) || p->lens_shading != 0 || p->mode != RWA_ISP_MODE_CELLS) {
		snprintf(why, n, "arguments");
		return RWA_ISP_E_ARGUMENT;
	}
	if (fail_next > 0) {
		fail_next--;
		if (partly) { memset(s->y, 0xee, sizeof(s->y) / 2); }   /* (a failure may leave the surface partly written) */
		snprintf(why, n, "made-up failure %d", fail_code);
		return fail_code;
	}
	if (wrong) { memset(s->y, 0x55, sizeof(s->y)); memset(s->uv, 0x80, sizeof(s->uv)); return RWA_ISP_OK; }   /* (a GPU that runs the shaders wrongly) */
	/* the GPU's arithmetic is the processor's: the same picture from the same numbers */
	isp_init(&isp);
	memcpy(isp.wb, p->wb, sizeof(isp.wb));
	isp.digital = p->digital;
	for (int i = 0; i < 9; i++) { isp.ccm[i / 3][i % 3] = p->ccm[i]; }
	isp.falloff = p->falloff; isp.zoom = p->zoom; isp.flip = p->flip;
	memcpy(isp.gamma, p->gamma, sizeof(isp.gamma));
	memset(&f, 0, sizeof(f));
	f.width = fr->width; f.height = fr->height; f.stride = fr->stride; f.format = fr->format; f.bayer = fr->bayer; f.black = fr->black;
	isp_render_nv12(&isp, &f, fr->raw, s->y, PW, s->uv, PW, o->width, o->height, 0, o->height, p->video_range != 0);
	if (res != NULL) { res->prepare_ns = 1000000; res->queue_ns = 200000; res->wait_ns = 3000000; res->flags = RWA_ISP_DID_IMPORT; }
	return RWA_ISP_OK;
}
static const struct rwa_isp_calls lib = { lib_version, lib_create, lib_destroy, lib_memory, lib_render, lib_wait, lib_forget };

/* ---- the picture's memory, as the bundle maps a CVPixelBuffer ---- */
static int maps, applied;
static bool map_surface(struct rwcam_picture *pic)
{
	struct surface *s = pic->context;

	maps++;
	pic->plane[0] = s->y; pic->stride[0] = PW; pic->plane[1] = s->uv; pic->stride[1] = PW;
	return true;
}
static bool map_refused(struct rwcam_picture *pic) { (void)pic; return false; }
static void apply(size_t n, void *context, void (*work)(void *, size_t)) { applied++; for (size_t i = n; i-- > 0;) { work(context, i); } }
static uint64_t clock_ms;
static uint64_t now_ms(void) { return clock_ms; }

static struct rwcam_picture picture(struct surface *s, uint32_t format, bool with_surface)
{
	struct rwcam_picture p;

	memset(&p, 0, sizeof(p));
	p.format = format; p.width = PW; p.height = PH; p.iosurface = with_surface ? s : NULL; p.map = map_surface; p.context = s;
	return p;
}

int main(void)
{
	static uint8_t raw[FW * 5 / 4 * FH];
	static struct surface by_cpu, by_gpu, s;
	struct redwood_camera_frame f;
	struct isp isp;
	struct rwcam_renderer r, r2;
	struct rwcam_picture p;

	memset(&f, 0, sizeof(f));
	f.width = FW; f.height = FH; f.stride = FW * 5 / 4; f.format = REDWOOD_CAMERA_FORMAT_MIPI_RAW10; f.bayer = 1; f.black = 64;
	for (size_t i = 0; i < sizeof(raw); i++) { raw[i] = i % 5 == 4 ? 0x1b : (uint8_t)(40 + (i * 7 + i / 320 * 3) % 180); }
	isp_init(&isp);
	isp.zoom = 1.7f; isp.flip = ISP_FLIP_H; isp.wb[0] = 1.9f; isp.wb[2] = 1.6f; isp.falloff = 0.25f;
	memset(&r, 0, sizeof(r)); r.name = "test";
	memset(&r2, 0, sizeof(r2)); r2.name = "test2";

	rwcam_render_checks(1, 0);      /* (sections 1 to 8: the first GPU picture alone is compared) */
	/* 1. no library: the processor, in pieces through apply; the reference picture */
	rwcam_render_setup(NULL, apply, now_ms);
	CHECK(!rwcam_render_gpu_there() && rwcam_render_preferred() == RWCAM_BACKEND_CPU);
	p = picture(&by_cpu, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 6) == RWCAM_BACKEND_CPU && maps == 1 && applied == 1 && p.mapped && renders == 0);
	{
		unsigned long sum = 0;

		for (size_t i = 0; i < sizeof(by_cpu.y); i++) { sum += by_cpu.y[i]; }
		CHECK(sum > sizeof(by_cpu.y) * 8);              /* (a picture, not nothing) */
	}
	/* one piece or many, the same picture */
	p = picture(&s, RWCAM_PF_420F, false);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 1) == RWCAM_BACKEND_CPU && memcmp(&s, &by_cpu, sizeof(s)) == 0);
	/* a library that is too old is no library */
	version = 0;
	rwcam_render_setup(&lib, apply, now_ms);
	CHECK(!rwcam_render_gpu_there());
	version = 1;

	/* 2. the library: the GPU makes it, the buffer is not mapped, and it is the processor's picture */
	rwcam_render_setup(&lib, apply, now_ms);
	CHECK(rwcam_render_gpu_there() && rwcam_render_preferred() == RWCAM_BACKEND_GPU);
	maps = 0;
	p = picture(&by_gpu, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 6) == RWCAM_BACKEND_GPU && renders == 1 && created == 1);
	CHECK(memcmp(&by_gpu, &by_cpu, sizeof(by_gpu)) == 0 && r.made[RWCAM_BACKEND_GPU] == 1 && r.timed == 1 && r.wait_ns == 3000000);
	/* (the first GPU picture was compared with the processor's: the same, and the buffer was mapped for that once) */
	CHECK(r.checked && r.check_max == 0 && maps == 1 && p.mapped);
	maps = 0;
	p = picture(&by_gpu, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 6) == RWCAM_BACKEND_GPU && maps == 0 && !p.mapped && renders == 2);
	{
		char text[300];

		rwcam_render_summary(&r, text, sizeof(text));
		if (getenv("VERBOSE")) { printf("   | %s\n", text); }
		CHECK(strstr(text, "GPU 2 (prepare 1.00 ms, queue 0.20 ms, wait 3.00 ms), processor 2") != NULL && strstr(text, "against the processor's: 1 compared, largest difference 0") != NULL);
		rwcam_render_summary(&r, text, sizeof(text));
		CHECK(strstr(text, "GPU 0, processor 0") != NULL);
	}
	renders = 1;
	/* 3. what the GPU is not asked for: BGRA, a lens shading table, a buffer without an IOSurface, a buffer mapped already, the processor preferred */
	p = picture(&s, RWCAM_PF_BGRA, true);
	p.map = map_refused;
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 2) == -1 && renders == 1);     /* (and no memory for the processor: nothing made, said so) */
	isp.lsc.on = true;
	for (int c = 0; c < 3; c++) { for (int y = 0; y < LSC_H; y++) { for (int x = 0; x < LSC_W; x++) { isp.lsc.gain[c][y][x] = 1; } } }
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 2) == RWCAM_BACKEND_CPU && renders == 1);
	isp.lsc.on = false;
	p = picture(&s, RWCAM_PF_420F, false);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 2) == RWCAM_BACKEND_CPU && renders == 1);
	rwcam_render_prefer(RWCAM_BACKEND_CPU);
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 2) == RWCAM_BACKEND_CPU && renders == 1 && rwcam_render_preferred() == RWCAM_BACKEND_CPU);
	rwcam_render_prefer(RWCAM_BACKEND_GPU);
	/* 4. a surface the GPU cannot write: the processor makes that one, whole, and the GPU the next */
	fail_next = 1; fail_code = RWA_ISP_E_SURFACE; partly = 1;
	memset(&s, 0, sizeof(s));
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && renders == 2 && memcmp(&s, &by_cpu, sizeof(s)) == 0 && r.fallbacks == 1);
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_GPU && renders == 3);
	/* 5. the GPU fails: the processor now and for the next seconds, then the GPU is asked again; three in a row: the processor for good */
	fail_next = 1; fail_code = RWA_ISP_E_GPU;
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && renders == 4 && memcmp(&s, &by_cpu, sizeof(s)) == 0);
	clock_ms += 1000;
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && renders == 4);
	clock_ms += 5000;
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_GPU && renders == 5 && r.gpu_failures == 0);
	fail_next = 3; fail_code = RWA_ISP_E_MEMORY;
	for (int i = 0; i < 3; i++) {
		p = picture(&s, RWCAM_PF_420F, true);
		CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU);
		clock_ms += 6000;
	}
	CHECK(renders == 8 && r.gpu_off);
	clock_ms += 600000;
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && renders == 8);
	/* 6. another stream's renderer is not touched by that; unsupported stays with the processor until the stream's buffers change */
	p = picture(&s, RWCAM_PF_420V, true);
	CHECK(rwcam_render(&r2, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_GPU && renders == 9 && created == 2);
	fail_next = 1; fail_code = RWA_ISP_E_UNSUPPORTED;
	p = picture(&s, RWCAM_PF_420V, true);
	CHECK(rwcam_render(&r2, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && renders == 10 && r2.gpu_unsupported);
	p = picture(&s, RWCAM_PF_420V, true);
	CHECK(rwcam_render(&r2, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && renders == 10);
	rwcam_render_forget(&r2);
	CHECK(forgets == 1 && !r2.gpu_unsupported);
	p = picture(&s, RWCAM_PF_420V, true);
	CHECK(rwcam_render(&r2, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_GPU && renders == 11);
	rwcam_render_end(&r2);
	rwcam_render_end(&r);
	CHECK(destroyed == 2 && r.gpu == NULL && r2.gpu == NULL && waits == 0);
	/* 7. a library that gives no context (the process cannot have the GPU): the processor, and it is not asked again */
	no_context = 1;
	memset(&r, 0, sizeof(r)); r.name = "test3";
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && r.gpu_off && renders == 11 && memcmp(&s, &by_cpu, sizeof(s)) == 0);
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && renders == 11);
	/* 8. a GPU whose first picture is not the processor's: found by the comparison, the picture given is the processor's, and the GPU is off for the stream */
	no_context = 0; wrong = 1;
	memset(&r, 0, sizeof(r)); r.name = "test4";
	memset(&s, 0, sizeof(s));
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && r.gpu_off && r.check_max > RWCAM_RENDER_CHECK_LIMIT && memcmp(&s, &by_cpu, sizeof(s)) == 0);
	p = picture(&s, RWCAM_PF_420F, true);
	{
		int before = renders;
		char text[300];

		CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && renders == before);
		rwcam_render_summary(&r, text, sizeof(text));
		CHECK(strstr(text, "GPU off for this stream") != NULL && strstr(text, "off the processor's") != NULL);
	}
	wrong = 0;
	/*
	 * 9. as the bundle has it: the first RWCAM_RENDER_CHECK_FIRST pictures compared, then one every RWCAM_RENDER_CHECK_MS. A GPU that goes wrong
	 * after its first pictures (its picture 12) is found at the next comparison: that picture is the processor's and the GPU is off.
	 */
	rwcam_render_checks(RWCAM_RENDER_CHECK_FIRST, RWCAM_RENDER_CHECK_MS);
	memset(&r, 0, sizeof(r)); r.name = "test5";
	for (int i = 0; i < RWCAM_RENDER_CHECK_FIRST + 3; i++) {
		maps = 0;
		p = picture(&s, RWCAM_PF_420F, true);
		clock_ms += 33;
		CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_GPU && maps == (i < RWCAM_RENDER_CHECK_FIRST));
	}
	CHECK(r.checks == RWCAM_RENDER_CHECK_FIRST && r.check_max == 0 && !r.gpu_off);
	wrong = 1;
	memset(&s, 0, sizeof(s));
	p = picture(&s, RWCAM_PF_420F, true);
	clock_ms += 33;
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_GPU && s.y[0] == 0x55);   /* (not compared: the wrong picture goes out, for 2 s at most) */
	clock_ms += RWCAM_RENDER_CHECK_MS;
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && r.gpu_off && memcmp(&s, &by_cpu, sizeof(s)) == 0 && r.checks == RWCAM_RENDER_CHECK_FIRST + 1);
	wrong = 0;
	/* a good GPU is compared again every RWCAM_RENDER_CHECK_MS and stays */
	memset(&r, 0, sizeof(r)); r.name = "test6";
	for (int i = 0; i < 300; i++) {
		p = picture(&s, RWCAM_PF_420F, true);
		clock_ms += 33;
		CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_GPU);
	}
	CHECK(r.checks >= RWCAM_RENDER_CHECK_FIRST + 4 && r.checks <= RWCAM_RENDER_CHECK_FIRST + 5 && r.check_max == 0);
	/* a GPU picture that cannot be looked at (the buffer cannot be mapped) is not trusted: nothing is given, the GPU is off */
	memset(&r, 0, sizeof(r)); r.name = "test7";
	p = picture(&s, RWCAM_PF_420F, true);
	p.map = map_refused;
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == -1 && r.gpu_off);
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && memcmp(&s, &by_cpu, sizeof(s)) == 0);
	/* 10. the caller saw a black picture of a frame that is not black: the GPU is off for that stream, the picture asked for again is the processor's */
	memset(&r, 0, sizeof(r)); r.name = "test8";
	p = picture(&s, RWCAM_PF_420F, true);
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_GPU);
	memset(&s, 0, sizeof(s));
	rwcam_render_distrust(&r, "black");
	CHECK(rwcam_render(&r, &isp, &f, raw, &p, 3) == RWCAM_BACKEND_CPU && r.gpu_off && memcmp(&s, &by_cpu, sizeof(s)) == 0);
	/*
	 * 11. noise (run q10hz). The same scene in frames with another noise each, taken with gain 12: from the third picture on the pictures
	 * differ from one another far less than the same frames' pictures do without the cleaning, and an edge in them is still an edge; a
	 * frame without gain is not touched; something that moves is where it is, not a ghost of where it was.
	 */
	{
		static uint8_t noisy[4][FW * 5 / 4 * FH];
		static struct surface out[2][4], moved;
		unsigned sd = 99, rough[2] = { 0, 0 };

		for (int n = 0; n < 4; n++) {
			for (int y = 0; y < FH; y++) {
				for (int x = 0; x < FW; x++) {
					size_t at = (size_t)y * (FW * 5 / 4) + (size_t)(x / 4) * 5 + (size_t)(x % 4);
					int v;

					sd = sd * 1103515245u + 12345u;
					v = (x < FW / 2 ? 30 : 90) + (int)((sd >> 16) % 25) - 12;      /* (an edge down the middle, and noise) */
					noisy[n][at] = (uint8_t)v;
				}
			}
		}
		for (int with = 0; with < 2; with++) {
			rwcam_render_denoise(with ? 2 : 0);
			memset(&r, 0, sizeof(r)); r.name = "noise";
			rwcam_render_prefer(RWCAM_BACKEND_CPU);
			isp.zoom = 1; isp.flip = 0;
			f.gain_unity = 256; f.gain = 12 * 256;
			for (int n = 0; n < 4; n++) {
				p = picture(&out[with][n], RWCAM_PF_420F, false);
				CHECK(rwcam_render(&r, &isp, &f, noisy[n], &p, 2) == RWCAM_BACKEND_CPU);
			}
			for (size_t i = 0; i < sizeof(out[0][0].y); i++) {
				int d = (int)out[with][3].y[i] - (int)out[with][2].y[i];

				rough[with] += (unsigned)(d < 0 ? -d : d);
			}
			if (with) {
				/* the edge: the left half's and the right half's mean brightness as far apart as without the cleaning */
				long l[2] = { 0, 0 }, rt[2] = { 0, 0 };

				for (int w = 0; w < 2; w++) {
					for (int y = 0; y < PH; y++) {
						for (int x = 4; x < PW / 2 - 4; x++) { l[w] += out[w][3].y[y * PW + x]; rt[w] += out[w][3].y[y * PW + PW / 2 + 4 + x - 4]; }
					}
				}
				CHECK(labs((rt[1] - l[1]) - (rt[0] - l[0])) < (rt[0] - l[0]) / 12);
				/* something that moves: the scene's halves exchanged in the next frame -- the picture follows at once */
				for (int y = 0; y < FH; y++) {
					for (int x = 0; x < FW; x++) {
						size_t at = (size_t)y * (FW * 5 / 4) + (size_t)(x / 4) * 5 + (size_t)(x % 4);

						noisy[0][at] = (uint8_t)(x < FW / 2 ? 90 : 30);
					}
				}
				p = picture(&moved, RWCAM_PF_420F, false);
				CHECK(rwcam_render(&r, &isp, &f, noisy[0], &p, 2) == RWCAM_BACKEND_CPU);
				CHECK(moved.y[PH / 2 * PW + PW / 4] > out[1][3].y[PH / 2 * PW + PW / 4] + 20);
				/* no gain: not touched (the picture is the one made without any of this) */
				f.gain = 256;
				p = picture(&out[1][0], RWCAM_PF_420F, false);
				CHECK(rwcam_render(&r, &isp, &f, noisy[1], &p, 2) == RWCAM_BACKEND_CPU);
				rwcam_render_denoise(0);
				p = picture(&out[0][0], RWCAM_PF_420F, false);
				CHECK(rwcam_render(&r2, &isp, &f, noisy[1], &p, 2) == RWCAM_BACKEND_CPU && memcmp(&out[1][0], &out[0][0], sizeof(out[0][0])) == 0);
			}
			rwcam_render_end(&r);
		}
		/*
		 * No trail (run q10ia): a bright thing two cells wide moves across a dim, noisy scene a little each frame; where it WAS one frame
		 * ago the picture is the scene's again at once, not something between the two.
		 */
		{
			static uint8_t fr[FW * 5 / 4 * FH];
			static struct surface now;
			int trail = 0, places = 0;

			rwcam_render_denoise(2);
			memset(&r, 0, sizeof(r)); r.name = "trail";
			rwcam_render_prefer(RWCAM_BACKEND_CPU);
			f.gain_unity = 256; f.gain = 12 * 256;
			for (int n = 0; n < 10; n++) {
				int at = 40 + n * 16;                          /* (16 sensor pixels a frame: 6 of the picture's) */

				for (int y = 0; y < FH; y++) {
					for (int x = 0; x < FW; x++) {
						size_t i = (size_t)y * (FW * 5 / 4) + (size_t)(x / 4) * 5 + (size_t)(x % 4);

						sd = sd * 1103515245u + 12345u;
						fr[i] = (uint8_t)((x >= at && x < at + 24 ? 120 : 30) + (int)((sd >> 16) % 13) - 6);
					}
				}
				p = picture(&now, RWCAM_PF_420F, false);
				CHECK(rwcam_render(&r, &isp, &f, fr, &p, 2) == RWCAM_BACKEND_CPU);
				if (n >= 3) {
					/* the picture's columns where the thing was one frame ago and is no more */
					int was = (at - 16) * PW / FW, is = at * PW / FW;

					for (int x = was + 1; x < is - 1; x++) {
						places++;
						trail += now.y[PH / 2 * PW + x] > now.y[PH / 2 * PW + 2] + 25;
					}
				}
			}
			CHECK(places > 10 && trail == 0);
			rwcam_render_end(&r);
		}
		if (getenv("VERBOSE")) { printf("   noise: one picture to the next, summed: %u without, %u with\n", rough[0], rough[1]); }
		CHECK(rough[1] * 100 < rough[0] * 62);
		rwcam_render_denoise(2);
		rwcam_render_prefer(RWCAM_BACKEND_GPU);
		f.gain = 0; f.gain_unity = 0;
	}
	printf(failures ? "t_render: %d FAILURES\n" : "t_render: all passed (%d pictures asked of the made-up GPU, %d contexts)\n", failures ? failures : renders, created);
	return failures != 0;
}
