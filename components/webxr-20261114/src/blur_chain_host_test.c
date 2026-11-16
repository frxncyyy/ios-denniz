/*
 * y2s99b host test (no GPU, no phone): CoreAnimation's OWN backdrop-blur shaders -- QuartzCore.framework/default.metallib of the
 * iOS 16.7 mirror: downsample_blur_vert_lph, downsample_4_frag_lph, narrow_blur_<N>_frag_lph -- through this library
 * (rwa_render_create / rwa_render_draw: the translator, the pipeline, the bindings, Metal's y-down texture coordinates) on
 * softpipe, the chain CA runs for a CABackdropLayer with a gaussianBlur filter:
 *     source W x H  --downsample_4-->  W/2 x H/2  --narrow_blur_N (x)-->  --narrow_blur_N (y)-->  result
 * and every pixel against a CPU model of the same arithmetic (bilinear sampling, clamp to edge, the shader's tap order and
 * weights, half precision rounding of the taps), on synthetic pictures (edges, a gradient, noise, a white square on black).
 * What it shows: the translation and the library's binding of these exact functions compute what the functions say.
 * What it cannot show: the Mali's own filtering precision (8-bit weights) and CA's choice of offsets/weights for a radius
 * (taken here from the standard linear-sampling gaussian construction; the shaders read whatever CA puts in the uniforms).
 *   blur_chain_host_test QuartzCore.metallib
 */
#include "air_metal.h"
#include "nir_air.h"
#include "util/ralloc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAILED %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, why); exit(1); } } while (0)
static char why[512];

static const struct air_metallib_function *
find(const struct air_metallib_function *fns, unsigned n, const char *name0)
{
	char name[128];
	snprintf(name, sizeof(name), "%s", name0);
	const char *suf = getenv("BLUR_SUFFIX");          /* lpf: the float variants of the same functions */
	if (suf && strlen(name) > 3 && !strcmp(name + strlen(name) - 3, "lph"))
		memcpy(name + strlen(name) - 3, suf, 3);
	for (unsigned i = 0; i < n; i++)
		if (!strcmp(fns[i].name, name))
			return &fns[i];
	fprintf(stderr, "FAILED: %s is not in the library\n", name);
	exit(1);
}

static float
h(float x)          /* round to half, as the shader's half arithmetic does */
{
	return (float)(_Float16)x;
}

/* bilinear, clamp to edge, normalized coordinates, RGBA float image */
static void
sample(const float *img, int w, int hgt, float u, float v, float out[4])
{
	float x = u * w - 0.5f, y = v * hgt - 0.5f;
	int x0 = (int)floorf(x), y0 = (int)floorf(y);
	float fx = x - x0, fy = y - y0;
	for (int c = 0; c < 4; c++) {
		float s = 0;
		for (int j = 0; j < 2; j++)
			for (int i = 0; i < 2; i++) {
				int xx = x0 + i, yy = y0 + j;
				xx = xx < 0 ? 0 : xx >= w ? w - 1 : xx;
				yy = yy < 0 ? 0 : yy >= hgt ? hgt - 1 : yy;
				s += img[(yy * w + xx) * 4 + c] * (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
			}
		out[c] = s;
	}
}

static struct rwa_color_blend g_blend;   /* (the y2s99b blend-colour check sets it for one pipeline) */
static int g_blend_on;
static const float *g_blend_color;

struct pass {
	struct rwa_render *render;
	struct rwa_buffer *mvp, *texmat, *pos, *tc, *col, *u, *edr;
};

static struct rwa_buffer *
buf(struct rwa_device *d, const void *bytes, size_t n)
{
	struct rwa_buffer *b = rwa_buffer_create(d, n < 16 ? 16 : n, why, sizeof(why));
	CHECK(b != NULL);
	memcpy(rwa_buffer_map(b), bytes, n);
	return b;
}

static void
make_pass(struct rwa_device *d, struct pass *p, const struct air_metallib_function *vs, const struct air_metallib_function *fs,
	  unsigned format)
{
	struct rwa_render_shape shape = { .color_format = { format }, .color_count = 1, .sample_count = 1 };
	if (g_blend_on) {
		shape.color_blend[0] = g_blend;
		shape.color_blend_count = 1;
	}
	/* CoreAnimation's own layout for these functions (trace of the phone: a0=fmt31@0/b1 a1=fmt29@16/b1 a2=fmt29@24/b1
	 * a3=fmt27@32/b1, b1 stride 48): one interleaved stream at buffer index 1 */
	shape.vertex_attribute[0] = (struct rwa_vertex_attribute){ 31, 0, 1 };
	shape.vertex_attribute[1] = (struct rwa_vertex_attribute){ 29, 16, 1 };
	shape.vertex_attribute[2] = (struct rwa_vertex_attribute){ 29, 24, 1 };
	shape.vertex_attribute[3] = (struct rwa_vertex_attribute){ 27, 32, 1 };
	shape.vertex_layout[1] = (struct rwa_vertex_layout){ .stride = 48, .step_function = 1 };
	struct rwa_render_info info;
	p->render = rwa_render_create(d, vs->bitcode, vs->bitcode_size, vs->name, fs->bitcode, fs->bitcode_size, fs->name, &shape,
				      &info, why, sizeof(why));
	if (g_blend_on == 2)
		return;              /* (the caller checks the refusal) */
	CHECK(p->render != NULL);
	static const float mvp[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, texmat[4] = { 1, 1, 0, 0 };
	/* a strip over the whole target; Metal's texture coordinates are y-down: (-1, 1) is the top-left, texcoord (0, 0) */
	static const float pos[16] = { -1, -1, 0, 1, 1, -1, 0, 1, -1, 1, 0, 1, 1, 1, 0, 1 }, tc[8] = { 0, 1, 1, 1, 0, 0, 1, 0 };
	static const uint16_t col[16] = { 0 };
	static float inter[4][12];
	for (int v = 0; v < 4; v++) {
		memcpy(&inter[v][0], &pos[4 * v], 16);
		memcpy(&inter[v][4], &tc[2 * v], 8);
		memcpy(&inter[v][6], &tc[2 * v], 8);
		memset(&inter[v][8], 0, 16);
	}
	static const float zero16[16] = { 0 };
	p->mvp = buf(d, getenv("BLUR_ZERO_MVP") ? zero16 : mvp, sizeof(mvp));
	static const float texmat_shift[4] = { 0, 0, 0.5f, 0.5f };
	p->texmat = buf(d, getenv("BLUR_TEXMAT_SHIFT") ? texmat_shift : texmat, sizeof(texmat));
	p->pos = buf(d, inter, sizeof(inter));
	p->tc = buf(d, tc, sizeof(tc));
	p->col = buf(d, col, sizeof(col));
}

static void
run_pass(struct pass *p, struct rwa_device *d, struct rwa_texture *src, struct rwa_sampler *smp, struct rwa_texture *dst,
	 const void *uniforms, size_t usize, float edr)
{
	_Float16 e = (_Float16)edr;
	float ef = edr;
	if (getenv("BLUR_SUFFIX")) {
		/* the _lpf layout: float4 weights[2] (+ float flag, padded to 112) and a float edr_scale */
		static unsigned char fu[112];
		memset(fu, 0, sizeof(fu));
		memcpy(fu, uniforms, 64);
		const _Float16 *hw = (const _Float16 *)((const unsigned char *)uniforms + 64);
		float *fw = (float *)(fu + 64);
		for (int i = 0; i < 8; i++)
			fw[i] = (float)hw[i];
		if (usize == 88)
			fw[8] = (float)hw[8];
		p->u = buf(d, fu, usize == 88 ? 112 : 96);
		p->edr = buf(d, &ef, sizeof(ef));
	} else {
		p->u = buf(d, uniforms, usize);
		p->edr = buf(d, &e, sizeof(e));
	}
	struct rwa_buffer *vbufs[4] = { NULL, NULL, p->mvp, p->texmat };
	struct rwa_buffer *fbufs[7] = { NULL, p->u, NULL, NULL, NULL, NULL, p->edr };
	struct rwa_texture *ftex[4] = { src, src, src, src };    /* (slot 3 is the one read; the others filled so no NULL view is bound) */
	/* softpipe (TGSI TEX/TXL) samples texture unit n with sampler n, so the sampler is bound at the texture's index too;
	 * Panfrost takes the two indices apart, as Metal does */
	struct rwa_sampler *fsmp[4] = { smp, smp, smp, smp };
	struct rwa_vertex_stream streams[2] = { { NULL, 0 }, { p->pos, 0 } };
	struct rwa_texture *const color[1] = { dst };
	static const float clear[4] = { 1, 0, 1, 1 };     /* magenta: a pixel the draw missed is seen */
	struct rwa_draw draw = {
		.color = color, .color_count = 1, .clear_color = clear,
		.vertex = { .buffers = vbufs, .buffer_count = 4 },
		.fragment = { .buffers = fbufs, .buffer_count = 7, .textures = ftex, .texture_count = 4, .samplers = fsmp,
			      .sampler_count = 4 },
		.primitive = 4, .vertex_start = 0, .vertex_count = 4, .instance_count = 1,
		.vertex_streams = streams, .vertex_stream_count = 2,
		.fb_magic = RWA_FB_MAGIC, .blend_color = g_blend_color, .bc_magic = RWA_BC_MAGIC,
	};
	if (getenv("BLUR_NO_FBMAGIC"))
		draw.fb_magic = 0;
	CHECK(rwa_render_draw(p->render, &draw, why, sizeof(why)) == 0);
	CHECK(rwa_finish(d, why, sizeof(why)) == 0);
}

/* the CPU model of narrow_blur_<N>_frag_lph: taps at tc - o[k] and tc + o[k], k < pairs; sum of w[k] * (a + b) in the
 * shader's order; unpremultiply off; rgb * edr */
static void
model_narrow(const float *in, int w, int hgt, const float off[8][2], const float wt[8], int pairs, float *out)
{
	for (int y = 0; y < hgt; y++)
		for (int x = 0; x < w; x++) {
			float u = (x + 0.5f) / w, v = (y + 0.5f) / hgt, acc[4] = { 0, 0, 0, 0 };
			for (int k = 0; k < pairs; k++) {
				float a[4], b[4];
				sample(in, w, hgt, u - off[k][0], v - off[k][1], a);
				sample(in, w, hgt, u + off[k][0], v + off[k][1], b);
				for (int c = 0; c < 4; c++)
					acc[c] += h(wt[k]) * (h(a[c]) + h(b[c]));
			}
			for (int c = 0; c < 4; c++)
				out[(y * w + x) * 4 + c] = acc[c];
		}
}

static void
model_down4(const float *in, int w, int hgt, int ow, int oh, const float off[4][2], const float wt[4], float *out)
{
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++) {
			float u = (x + 0.5f) / ow, v = (y + 0.5f) / oh, acc[4] = { 0, 0, 0, 0 };
			for (int k = 0; k < 4; k++) {
				float a[4];
				sample(in, w, hgt, u + off[k][0], v + off[k][1], a);
				for (int c = 0; c < 4; c++)
					acc[c] += h(wt[k]) * h(a[c]);
			}
			for (int c = 0; c < 4; c++)
				out[(y * ow + x) * 4 + c] = acc[c];
		}
}

static void
picture(int kind, int w, int hgt, float *img)
{
	srand(7 + kind);
	for (int y = 0; y < hgt; y++)
		for (int x = 0; x < w; x++) {
			float *p = img + (y * w + x) * 4;
			switch (kind) {
			case 0: p[0] = x < w / 3 ? 1 : 0; p[1] = y < hgt / 2 ? 1 : 0.25f; p[2] = (x + y) % 2; p[3] = 1; break;  /* edges */
			case 1: p[0] = (float)x / (w - 1); p[1] = (float)y / (hgt - 1); p[2] = 0.5f; p[3] = 1; break;            /* gradient */
			case 2: for (int c = 0; c < 4; c++) p[c] = (float)(rand() % 256) / 255.0f; break;                      /* noise */
			default: { int in = abs(x - w / 2) < w / 8 && abs(y - hgt / 2) < hgt / 8; p[0] = p[1] = p[2] = in; p[3] = 1; }
			}
			for (int c = 0; c < 4; c++)
				p[c] = roundf(p[c] * 255.0f) / 255.0f;         /* as stored in RGBA8 */
		}
}

static void
upload(struct rwa_texture *t, const float *img, int w, int hgt, unsigned format)
{
	unsigned origin[3] = { 0, 0, 0 }, size[3] = { (unsigned)w, (unsigned)hgt, 1 };
	if (format == 70) {
		uint8_t *b = malloc((size_t)w * hgt * 4);
		for (int i = 0; i < w * hgt * 4; i++)
			b[i] = (uint8_t)lroundf(img[i] * 255.0f);
		CHECK(rwa_texture_write(t, 0, 0, origin, size, b, (size_t)w * 4, why, sizeof(why)) == 0);
		free(b);
	} else {
		_Float16 *b = malloc((size_t)w * hgt * 8);
		for (int i = 0; i < w * hgt * 4; i++)
			b[i] = (_Float16)img[i];
		CHECK(rwa_texture_write(t, 0, 0, origin, size, b, (size_t)w * 8, why, sizeof(why)) == 0);
		free(b);
	}
}

static void
download(struct rwa_texture *t, float *img, int w, int hgt, unsigned format)
{
	unsigned origin[3] = { 0, 0, 0 }, size[3] = { (unsigned)w, (unsigned)hgt, 1 };
	if (format == 70) {
		uint8_t *b = malloc((size_t)w * hgt * 4);
		CHECK(rwa_texture_read(t, 0, 0, origin, size, b, (size_t)w * 4, why, sizeof(why)) == 0);
		for (int i = 0; i < w * hgt * 4; i++)
			img[i] = b[i] / 255.0f;
		free(b);
	} else {
		_Float16 *b = malloc((size_t)w * hgt * 8);
		CHECK(rwa_texture_read(t, 0, 0, origin, size, b, (size_t)w * 8, why, sizeof(why)) == 0);
		for (int i = 0; i < w * hgt * 4; i++)
			img[i] = (float)b[i];
		free(b);
	}
}

/* the standard linear-sampling gaussian: 2*pairs discrete taps each side of the centre folded into pairs bilinear taps,
 * the centre tap split over the first pair (the shaders have no centre tap: narrow_blur_N reads 2*pairs taps) */
static void
gauss_pairs(int pairs, float sigma, float texel, float off[8][2], float wt[8], int axis)
{
	float d[20], sum = 0;
	int taps = 2 * pairs;               /* discrete taps 1..taps on each side; tap 0 = centre */
	for (int i = 0; i <= taps; i++) {
		d[i] = expf(-(float)(i * i) / (2 * sigma * sigma));
		sum += i ? 2 * d[i] : d[i];
	}
	for (int i = 0; i <= taps; i++)
		d[i] /= sum;
	memset(off, 0, sizeof(float) * 16);
	for (int k = 0; k < pairs; k++) {
		float a = k == 0 ? d[0] / 2 : d[2 * k], b = d[2 * k + 1];
		float pa = k == 0 ? 0 : 2 * k, pb = 2 * k + 1;
		wt[k] = a + b;
		off[k][axis] = (pa * a + pb * b) / (a + b) * texel;
	}
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s QuartzCore.metallib\n", argv[0]);
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");
	CHECK(f != NULL);
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *lib = malloc((size_t)n);
	CHECK(fread(lib, 1, (size_t)n, f) == (size_t)n);
	fclose(f);
	void *mem = ralloc_context(NULL);
	struct air_metallib ml = { 0 };
	const char *perr = NULL;
	CHECK(air_metallib_parse(mem, lib, (size_t)n, &ml, &perr));
	const struct air_metallib_function *fns = ml.functions;
	unsigned count = ml.num_functions;

	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_device *d = rwa_device_open(why, sizeof(why));
	CHECK(d != NULL);
	const struct air_metallib_function *vs = find(fns, count, "downsample_blur_vert_lph");
	struct rwa_sampler_state ss = { .address = { 0, 0, 0 }, .min_filter = 1, .mag_filter = 1, .mip_filter = 0,
					.max_anisotropy = 1, .normalized_coordinates = 1, .lod_min = 0, .lod_max = 1000 };
	struct rwa_sampler *smp = rwa_sampler_create(d, &ss, why, sizeof(why));
	CHECK(smp != NULL);

	static const int widths[] = { 7, 11, 15, 19, 23, 27 };
	const int W = 96, H = 64, w2 = W / 2, h2 = H / 2;
	unsigned formats[2] = { 70, 115 };    /* RGBA8Unorm, RGBA16Float */
	int failures = 0;
	double worst_all = 0;
	for (int fi = 0; fi < (getenv("BLUR_LIB_ONLY") ? 0 : 2); fi++) {
		unsigned fmt = formats[fi];
		float tol = fmt == 70 ? 1.5f / 255.0f + 2e-3f : 4e-3f;
		struct rwa_texture_shape shape = { .pixel_format = fmt, .type = 2, .width = W, .height = H, .depth = 1, .array_length = 1,
						   .mip_levels = 1, .sample_count = 1, .usage = 1 | 4 };
		struct rwa_texture *src = rwa_texture_create(d, &shape, why, sizeof(why));
		shape.width = w2; shape.height = h2;
		struct rwa_texture *t1 = rwa_texture_create(d, &shape, why, sizeof(why)), *t2 = rwa_texture_create(d, &shape, why, sizeof(why)),
				   *t3 = rwa_texture_create(d, &shape, why, sizeof(why));
		CHECK(src && t1 && t2 && t3);
		float *img = malloc(sizeof(float) * W * H * 4), *m1 = malloc(sizeof(float) * w2 * h2 * 4), *m2 = malloc(sizeof(float) * w2 * h2 * 4),
		      *m3 = malloc(sizeof(float) * w2 * h2 * 4), *g = malloc(sizeof(float) * w2 * h2 * 4);
		for (int wi = 0; wi < 6; wi++) {
			int taps = widths[wi], pairs = (taps + 1) / 4;    /* narrow_blur_19 reads 5 pairs (10 taps) */
			char name[64];
			snprintf(name, sizeof(name), "narrow_blur_%d_frag_lph", taps);
			struct pass down = { 0 }, bx = { 0 }, by = { 0 };
			make_pass(d, &down, vs, find(fns, count, "downsample_4_frag_lph"), fmt);
			if (getenv("BLUR_ONLY_DOWN"))
				exit(0);
			make_pass(d, &bx, vs, find(fns, count, name), fmt);
			make_pass(d, &by, vs, find(fns, count, name), fmt);
			for (int kind = 0; kind < 4; kind++) {
				picture(kind, W, H, img);
				upload(src, img, W, H, fmt);
				if (getenv("BLUR_DEBUG") && kind == 1 && wi == 0) {
					float *chk = malloc(sizeof(float) * W * H * 4);
					download(src, chk, W, H, fmt);
					printf("  debug src readback px(5,3) %g %g %g %g want %g %g %g %g\n", chk[(3 * W + 5) * 4], chk[(3 * W + 5) * 4 + 1],
					       chk[(3 * W + 5) * 4 + 2], chk[(3 * W + 5) * 4 + 3], img[(3 * W + 5) * 4], img[(3 * W + 5) * 4 + 1],
					       img[(3 * W + 5) * 4 + 2], img[(3 * W + 5) * 4 + 3]);
					free(chk);
				}
				/* downsample_4: four bilinear taps at the corners of each 2x2 block's neighbourhood, weights 1/4 each */
				struct { float off[8][2]; _Float16 w[8]; } du = { 0 };
				float doff[4][2] = { { -0.5f / W, -0.5f / H }, { 0.5f / W, -0.5f / H }, { -0.5f / W, 0.5f / H }, { 0.5f / W, 0.5f / H } };
				float dw[4] = { 0.25f, 0.25f, 0.25f, 0.25f };
				for (int k = 0; k < 4; k++) {
					du.off[k][0] = doff[k][0]; du.off[k][1] = doff[k][1]; du.w[k] = (_Float16)dw[k];
				}
				if (getenv("BLUR_SRC_NARROW") && kind == 1 && wi == 0) {
					struct { float off[8][2]; _Float16 w[8]; _Float16 unpremul; _Float16 pad[3]; } q = { 0 };
					q.w[0] = (_Float16)0.5f;          /* one pair at offset 0: 0.5 * (s + s) = s */
					struct rwa_texture_shape fs2 = shape; fs2.width = W; fs2.height = H;
					struct rwa_texture *tf = rwa_texture_create(d, &fs2, why, sizeof(why));
					run_pass(&bx, d, src, smp, tf, &q, 88, 1.0f);
					float *chk = malloc(sizeof(float) * W * H * 4);
					download(tf, chk, W, H, fmt);
					printf("  debug narrow on src px(5,3) %g %g %g %g (want the source pixel)\n", chk[(3 * W + 5) * 4],
					       chk[(3 * W + 5) * 4 + 1], chk[(3 * W + 5) * 4 + 2], chk[(3 * W + 5) * 4 + 3]);
					free(chk);
				}
				run_pass(&down, d, src, smp, t1, &du, 80, 1.0f);
				model_down4(img, W, H, w2, h2, doff, dw, m1);
				/* the two separable passes, each from the GPU's previous result (so each pass is judged alone) */
				struct { float off[8][2]; _Float16 w[8]; _Float16 unpremul; _Float16 pad[3]; } nu = { 0 };
				float off[8][2], wt[8] = { 0 };
				float sigma = taps / 6.0f;
				download(t1, g, w2, h2, fmt);
				gauss_pairs(pairs, sigma, 1.0f / w2, off, wt, 0);
				memcpy(nu.off, off, sizeof(off));
				for (int k = 0; k < 8; k++)
					nu.w[k] = (_Float16)wt[k];
				run_pass(&bx, d, t1, smp, t2, &nu, 88, 1.0f);
				model_narrow(g, w2, h2, off, wt, pairs, m2);
				download(t2, g, w2, h2, fmt);
				gauss_pairs(pairs, sigma, 1.0f / h2, off, wt, 1);
				memcpy(nu.off, off, sizeof(off));
				for (int k = 0; k < 8; k++)
					nu.w[k] = (_Float16)wt[k];
				run_pass(&by, d, t2, smp, t3, &nu, 88, 0.875f);       /* and an EDR scale on the last */
				model_narrow(g, w2, h2, off, wt, pairs, m3);
				for (int i = 0; i < w2 * h2; i++)
					for (int c = 0; c < 3; c++)
						m3[i * 4 + c] *= h(0.875f);
				float *gpu[3] = { NULL, NULL, NULL }, *model[3] = { m1, m2, m3 };
				struct rwa_texture *tex[3] = { t1, t2, t3 };
				for (int s = 0; s < 3; s++) {
					gpu[s] = malloc(sizeof(float) * w2 * h2 * 4);
					download(tex[s], gpu[s], w2, h2, fmt);
					double worst = 0;
					int bad = 0;
					for (int i = 0; i < w2 * h2 * 4; i++) {
						float want = model[s][i];
						if (fmt == 70)
							want = want < 0 ? 0 : want > 1 ? 1 : want;
						double e = fabs(gpu[s][i] - want);
						worst = e > worst ? e : worst;
						bad += e > tol;
					}
					worst_all = worst > worst_all ? worst : worst_all;
					if (getenv("BLUR_DEBUG") && s == 0 && kind == 1 && wi == 0)
						printf("  debug fmt %u: gpu %g %g %g %g model %g %g %g %g (pixel 5,3: gpu %g model %g)\n", fmt, gpu[s][0],
						       gpu[s][1], gpu[s][2], gpu[s][3], model[s][0], model[s][1], model[s][2], model[s][3],
						       gpu[s][(3 * w2 + 5) * 4], model[s][(3 * w2 + 5) * 4]);
					if (bad) {
						failures++;
						printf("  fmt %u %s picture %d stage %d: %d components off by more than %g (worst %g)\n", fmt, name, kind,
						       s, bad, tol, worst);
					}
					free(gpu[s]);
				}
			}
			rwa_render_destroy(down.render);
			rwa_render_destroy(bx.render);
			rwa_render_destroy(by.render);
			printf("  fmt %-3u %-24s 4 pictures x (downsample_4, blur x, blur y): checked, worst so far %.5f\n", fmt, name, worst_all);
		}
	}
	/* ---- y2s99b library checks ---- */
	{
		const int w = 32, hh = 16;
		struct rwa_texture_shape shape = { .pixel_format = 70, .type = 2, .width = w, .height = hh, .depth = 1, .array_length = 1,
						   .mip_levels = 1, .sample_count = 1, .usage = 1 | 4 };
		struct rwa_texture *a = rwa_texture_create(d, &shape, why, sizeof(why)), *t = rwa_texture_create(d, &shape, why, sizeof(why));
		CHECK(a && t);
		float *img = malloc(sizeof(float) * w * hh * 4), *got = malloc(sizeof(float) * w * hh * 4), *m = malloc(sizeof(float) * w * hh * 4);
		picture(2, w, hh, img);
		upload(a, img, w, hh, 70);
		struct { float off[8][2]; _Float16 w[8]; } du = { 0 };
		float doff[4][2], dw[4] = { 0.25f, 0.25f, 0.25f, 0.25f };
		for (int k = 0; k < 4; k++) {
			doff[k][0] = du.off[k][0] = 0; doff[k][1] = du.off[k][1] = 0; du.w[k] = (_Float16)0.25f;
		}
		/* A. BLEND COLOUR: BlendAlpha / OneMinusBlendAlpha against a cleared magenta target, blend colour alpha 0.25 */
		g_blend = (struct rwa_color_blend){ .blending_enabled = 1, .rgb_blend_operation = 0, .alpha_blend_operation = 0,
						    .source_rgb_blend_factor = 13, .destination_rgb_blend_factor = 14,
						    .source_alpha_blend_factor = 13, .destination_alpha_blend_factor = 14, .write_mask = 0xf };
		rwa_shader99_config(RWA_S99_XLAT | RWA_S99_TGMEM);          /* switched off: refused, as before y2s99b */
		g_blend_on = 2;
		struct pass pb = { 0 };
		make_pass(d, &pb, vs, find(fns, count, "downsample_4_frag_lph"), 70);
		int refused_off = pb.render == NULL;
		if (pb.render) rwa_render_destroy(pb.render);
		rwa_shader99_config(RWA_S99_XLAT | RWA_S99_TGMEM | RWA_S99_BLENDCOLOR | RWA_S99_BORDERCOLOR);
		g_blend_on = 1;
		static const float bc[4] = { 0, 0, 0, 0.25f };
		g_blend_color = bc;
		make_pass(d, &pb, vs, find(fns, count, "downsample_4_frag_lph"), 70);
		run_pass(&pb, d, a, smp, t, &du, 80, 1.0f);
		download(t, got, w, hh, 70);
		{       /* the same pass unblended: what the function wrote (the reference the blend is judged against) */
			g_blend_on = 0;
			struct pass pu = { 0 };
			make_pass(d, &pu, vs, find(fns, count, "downsample_4_frag_lph"), 70);
			run_pass(&pu, d, a, smp, t, &du, 80, 1.0f);
			download(t, m, w, hh, 70);
			rwa_render_destroy(pu.render);
			g_blend_on = 1;
		}
		printf("  debug blend px0 got %g %g %g %g unblended %g %g %g %g\n", got[0], got[1], got[2], got[3], m[0], m[1], m[2], m[3]);
		static const float clear[4] = { 1, 0, 1, 1 };
		int bad = 0;
		for (int i = 0; i < w * hh * 4; i++)
			bad += fabsf(got[i] - (0.25f * m[i] + 0.75f * clear[i % 4])) > 2.5f / 255.0f;
		printf("  blend colour: refused with the switch off %s; BlendAlpha 0.25 over magenta: %d components wrong\n",
		       refused_off ? "yes" : "NO", bad);
		failures += bad != 0 || !refused_off;
		rwa_render_destroy(pb.render);
		g_blend_on = 0; g_blend_color = NULL;
		/* B. BORDER COLOUR: clamp to border, opaque white, every tap a whole texture away -> white; switched off -> transparent */
		for (int on = 1; on >= 0; on--) {
			rwa_shader99_config(RWA_S99_XLAT | RWA_S99_TGMEM | (on ? RWA_S99_BORDERCOLOR : 0u));
			struct rwa_sampler_state bs = { .address = { 5, 5, 5 }, .min_filter = 1, .mag_filter = 1, .max_anisotropy = 1,
							.normalized_coordinates = 1, .lod_min = 0, .lod_max = 1000, .border_color = 2,
							.bc_magic = RWA_BC_MAGIC };
			struct rwa_sampler *bsmp = rwa_sampler_create(d, &bs, why, sizeof(why));
			CHECK(bsmp != NULL);
			for (int k = 0; k < 4; k++) { du.off[k][0] = 2.0f; du.off[k][1] = 0; }
			struct pass pd = { 0 };
			make_pass(d, &pd, vs, find(fns, count, "downsample_4_frag_lph"), 70);
			run_pass(&pd, d, a, bsmp, t, &du, 80, 1.0f);
			download(t, got, w, hh, 70);
			printf("  debug border px0 %g %g %g %g\n", got[0], got[1], got[2], got[3]);
			int wrong = 0;
			for (int i = 0; i < w * hh * 4; i++)
				wrong += fabsf(got[i] - (on ? 1.0f : 0.0f)) > 1.5f / 255.0f;
			printf("  border colour %s: opaque white asked, %d components not %s\n", on ? "on" : "off (switch)", wrong,
			       on ? "white" : "transparent black");
			failures += wrong != 0;
			rwa_render_destroy(pd.render);
			rwa_sampler_destroy(bsmp);
		}
		rwa_shader99_config(RWA_S99_DEFAULT);
	}
	if (getenv("TGMEM_BC")) {
		/* C. DISPATCH-SIZED THREADGROUP MEMORY beside a static array */
		FILE *k = fopen(getenv("TGMEM_BC"), "rb");
		CHECK(k != NULL);
		static unsigned char bc[1 << 16];
		size_t kn = fread(bc, 1, sizeof(bc), k);
		fclose(k);
		struct rwa_compute_info ci;
		if (getenv("TGMEM_NO_XLAT"))       /* negative control: the buffer back at offset 0, over the static array */
			rwa_shader99_config(RWA_S99_TGMEM);
		struct rwa_compute *c = rwa_compute_create(d, bc, kn, "nord2_tgmem", &ci, why, sizeof(why));
		CHECK(c != NULL);
		struct rwa_buffer *out = rwa_buffer_create(d, 1024, why, sizeof(why));
		CHECK(out != NULL);
		memset(rwa_buffer_map(out), 0xee, 128 * 4);
		struct rwa_buffer *bufs[1] = { out };
		struct rwa_bindings bind = { .buffers = bufs, .buffer_count = 1 };
		unsigned groups[3] = { 2, 1, 1 }, size[3] = { 64, 1, 1 };
		int no_len = rwa_dispatch(c, &bind, groups, size, why, sizeof(why));
		CHECK(rwa_dispatch_tg(c, &bind, NULL, 0, groups, size, 256, 0, why, sizeof(why)) == 0);
		const uint32_t *o = rwa_buffer_map(out);
		int wrong = 0;
		for (unsigned t = 0; t < 128; t++) {
			unsigned l = t % 64;
			wrong += o[t] != ((l + 1) % 64) * 3 + (((l + 3) % 64) * 7 + 1) * 1000;
		}
		printf("  threadgroup length: without it refused %s; static %u bytes + 256 dispatch-sized: %d of 128 words wrong\n",
		       no_len != 0 ? "yes" : "NO", ci.static_shared_bytes, wrong);
		failures += wrong != 0 || no_len == 0;
	}
	printf("blur_chain_host_test: %d stage results outside tolerance; worst component error %.5f on %s\n", failures, worst_all,
	       rwa_device_name(d));
	if (failures == 0)
		printf("blur_chain: ok\n");
	return failures != 0;
}
