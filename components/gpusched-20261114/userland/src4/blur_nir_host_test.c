/*
 * y2s99b host test (no GPU, no phone): CoreAnimation's OWN backdrop-blur fragment functions (QuartzCore.framework/default.metallib
 * of the iOS 16.7 mirror: downsample_4_frag_lph/_lpf, narrow_blur_{7,11,15,19,23,27}_frag_lph/_lpf) through this library's
 * translator and finalize_for_gallium (the NIR the GPU driver is handed), then EVALUATED pixel by pixel with NIR's own constant
 * folder -- half arithmetic included, exactly as NIR defines each 16-bit operation -- with the texture fetch done here (bilinear,
 * clamp to edge, level 0, the fetched texel rounded to the fetch's own width), against a CPU model of what the function computes.
 * Every pixel of a downsample + horizontal + vertical chain, on four synthetic pictures, both variants, all six widths.
 * What it shows: the translation of these exact functions (uniform layout, tap order, weights, half precision, the unpremultiply
 * branch, the EDR scale, the final swizzle) computes the blur. What it cannot show: the Mali's filtering precision, the
 * rasterizer, and CoreAnimation's own choice of uniforms (taken from the standard linear-sampling gaussian).
 *   blur_nir_host_test QuartzCore.metallib
 */
#include "air_metal.c"
#include "nir_constant_expressions.h"
#include "util/half_float.h"
#include <math.h>

static char why[512];
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAILED %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, why); exit(1); } } while (0)

struct img { int w, h; float *px; };
struct machine {
	nir_const_value (*v)[NIR_MAX_VEC_COMPONENTS];
	const struct img *tex;
	const uint8_t *ubo[8];
	size_t ubo_size[8];
	float tc[2];
	float out[4];
	int wrote;
};

static void
sample(const struct img *t, float u, float v, float out[4])
{
	float x = u * t->w - 0.5f, y = v * t->h - 0.5f;
	int x0 = (int)floorf(x), y0 = (int)floorf(y);
	float fx = x - x0, fy = y - y0;
	for (int c = 0; c < 4; c++) {
		float s = 0;
		for (int j = 0; j < 2; j++)
			for (int i = 0; i < 2; i++) {
				int xx = x0 + i, yy = y0 + j;
				xx = xx < 0 ? 0 : xx >= t->w ? t->w - 1 : xx;
				yy = yy < 0 ? 0 : yy >= t->h ? t->h - 1 : yy;
				s += t->px[(yy * t->w + xx) * 4 + c] * (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
			}
		out[c] = s;
	}
}

static float
cv_float(nir_const_value v, unsigned bits)
{
	return (float)nir_const_value_as_float(v, bits);
}

static int
evaluate(nir_shader *nir, struct machine *m)
{
	nir_function_impl *impl = nir_shader_get_entrypoint(nir);
	if (nir_start_block(impl) != nir_impl_last_block(impl)) {
		fprintf(stderr, "the function is not one block after flattening\n");
		return -1;
	}
	nir_foreach_instr(instr, nir_start_block(impl)) {
		switch (instr->type) {
		case nir_instr_type_load_const: {
			nir_load_const_instr *k = nir_instr_as_load_const(instr);
			memcpy(m->v[k->def.index], k->value, sizeof(nir_const_value) * k->def.num_components);
			break;
		}
		case nir_instr_type_undef: {
			nir_undef_instr *u = nir_instr_as_undef(instr);
			memset(m->v[u->def.index], 0, sizeof(nir_const_value) * u->def.num_components);
			break;
		}
		case nir_instr_type_alu: {
			nir_alu_instr *alu = nir_instr_as_alu(instr);
			nir_const_value src[NIR_MAX_VEC_COMPONENTS][NIR_MAX_VEC_COMPONENTS], *srcs[NIR_MAX_VEC_COMPONENTS];
			unsigned bit_size = alu->def.bit_size;
			for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
				for (unsigned c = 0; c < nir_ssa_alu_instr_src_components(alu, i); c++)
					src[i][c] = m->v[alu->src[i].src.ssa->index][alu->src[i].swizzle[c]];
				srcs[i] = src[i];
				if (nir_alu_type_get_type_size(nir_op_infos[alu->op].input_types[i]) == 0)
					bit_size = alu->src[i].src.ssa->bit_size;
			}
			if (nir_alu_type_get_type_size(nir_op_infos[alu->op].output_type) == 0)
				bit_size = alu->def.bit_size;
			nir_eval_const_opcode(alu->op, m->v[alu->def.index], alu->def.num_components, bit_size, srcs,
					      nir->info.float_controls_execution_mode);
			break;
		}
		case nir_instr_type_tex: {
			nir_tex_instr *t = nir_instr_as_tex(instr);
			int ci = nir_tex_instr_src_index(t, nir_tex_src_coord);
			if (ci < 0 || (t->op != nir_texop_tex && t->op != nir_texop_txl && t->op != nir_texop_txb)) {
				fprintf(stderr, "a texture operation this evaluator does not do (op %d)\n", t->op);
				return -1;
			}
			nir_def *cd = t->src[ci].src.ssa;
			float u = cv_float(m->v[cd->index][0], cd->bit_size), v = cv_float(m->v[cd->index][1], cd->bit_size), o[4];
			sample(m->tex, u, v, o);
			for (unsigned c = 0; c < t->def.num_components; c++)
				m->v[t->def.index][c] = nir_const_value_for_float(o[c], t->def.bit_size);   /* (to half for a half fetch) */
			break;
		}
		case nir_instr_type_intrinsic: {
			nir_intrinsic_instr *x = nir_instr_as_intrinsic(instr);
			switch (x->intrinsic) {
			case nir_intrinsic_load_barycentric_pixel:
			case nir_intrinsic_load_barycentric_centroid:
				break;
			case nir_intrinsic_load_interpolated_input:
			case nir_intrinsic_load_input:
				for (unsigned c = 0; c < x->def.num_components; c++)
					m->v[x->def.index][c] = nir_const_value_for_float(m->tc[(nir_intrinsic_component(x) + c) & 1],
											x->def.bit_size);
				break;
			case nir_intrinsic_load_ubo: {
				unsigned idx = (unsigned)nir_const_value_as_uint(m->v[x->src[0].ssa->index][0], 32);
				unsigned off = (unsigned)nir_const_value_as_uint(m->v[x->src[1].ssa->index][0], 32);
				unsigned bytes = x->def.bit_size / 8;
				if (idx >= 8 || !m->ubo[idx] || off + bytes * x->def.num_components > m->ubo_size[idx]) {
					fprintf(stderr, "a uniform read outside buffer %u (offset %u)\n", idx, off);
					return -1;
				}
				for (unsigned c = 0; c < x->def.num_components; c++) {
					uint64_t raw = 0;
					memcpy(&raw, m->ubo[idx] + off + c * bytes, bytes);
					m->v[x->def.index][c] = nir_const_value_for_raw_uint(raw, x->def.bit_size);
				}
				break;
			}
			case nir_intrinsic_store_output: {
				nir_def *s = x->src[0].ssa;
				for (unsigned c = 0; c < s->num_components; c++)
					m->out[nir_intrinsic_component(x) + c] = cv_float(m->v[s->index][c], s->bit_size);
				m->wrote = 1;
				break;
			}
			default:
				fprintf(stderr, "an intrinsic this evaluator does not know: %s\n", nir_intrinsic_infos[x->intrinsic].name);
				return -1;
			}
			break;
		}
		default:
			fprintf(stderr, "an instruction this evaluator does not know (type %d)\n", instr->type);
			return -1;
		}
	}
	return m->wrote ? 0 : -1;
}

/* the function, translated as the library does for a pipeline, then flattened to one block */
static nir_shader *
prepare(void *mem, const struct air_metallib_function *f, struct rwa_device *d)
{
	struct air_shader_info info = { 0 };
	const struct air_to_nir_options opts = { 0 };
	const char *err = NULL;
	nir_shader *nir = air_to_nir(mem, f->bitcode, f->bitcode_size, f->name, &opts, d->nir_options, &info, &err);
	if (!nir) {
		snprintf(why, sizeof(why), "%s", err ? err : "?");
		return NULL;
	}
	finalize_for_gallium(nir, d->screen);
	bool progress;
	do {
		progress = false;
		NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
		NIR_PASS(progress, nir, nir_opt_if, 0);
		NIR_PASS(progress, nir, nir_opt_peephole_select, 4096, true, true);
		NIR_PASS(progress, nir, nir_opt_dead_cf);
		NIR_PASS(progress, nir, nir_opt_dce);
		NIR_PASS(progress, nir, nir_opt_cse);
		NIR_PASS(progress, nir, nir_copy_prop);
	} while (progress);
	nir_index_ssa_defs(nir_shader_get_entrypoint(nir));
	return nir;
}

static const struct air_metallib_function *
find(const struct air_metallib *lib, const char *name)
{
	for (unsigned i = 0; i < lib->num_functions; i++)
		if (!strcmp(lib->functions[i].name, name))
			return &lib->functions[i];
	snprintf(why, sizeof(why), "%s not in the library", name);
	return NULL;
}

static void
picture(int kind, struct img *t)
{
	srand(7 + kind);
	for (int y = 0; y < t->h; y++)
		for (int x = 0; x < t->w; x++) {
			float *p = t->px + (y * t->w + x) * 4;
			switch (kind) {
			case 0: p[0] = x < t->w / 3 ? 1 : 0; p[1] = y < t->h / 2 ? 1 : 0.25f; p[2] = (x + y) % 2; p[3] = 1; break;
			case 1: p[0] = (float)x / (t->w - 1); p[1] = (float)y / (t->h - 1); p[2] = 0.5f; p[3] = 1; break;
			case 2: for (int c = 0; c < 4; c++) p[c] = (float)(rand() % 256) / 255.0f; break;   /* (alpha < 1: premultiplied-ish) */
			default: { int in = abs(x - t->w / 2) < t->w / 8 && abs(y - t->h / 2) < t->h / 8; p[0] = p[1] = p[2] = in; p[3] = 1; }
			}
		}
}

static void
gauss_pairs(int pairs, float sigma, float texel, float off[8][2], float wt[8], int axis)
{
	float d[40], sum = 0;
	int taps = 2 * pairs;
	for (int i = 0; i <= taps; i++) {
		d[i] = expf(-(float)(i * i) / (2 * sigma * sigma));
		sum += i ? 2 * d[i] : d[i];
	}
	for (int i = 0; i <= taps; i++)
		d[i] /= sum;
	memset(off, 0, sizeof(float) * 16);
	memset(wt, 0, sizeof(float) * 8);
	for (int k = 0; k < pairs; k++) {
		float a = k == 0 ? d[0] / 2 : d[2 * k], b = d[2 * k + 1];
		float pa = k == 0 ? 0 : 2 * k, pb = 2 * k + 1;
		wt[k] = a + b;
		off[k][axis] = (pa * a + pb * b) / (a + b) * texel;
	}
}

/* uniforms in the variant's layout: offsets[8] float2, then weights as half[8] (lph) or float[8] (lpf), then the flag */
static size_t
uniforms(uint8_t *u, int lpf, const float off[8][2], const float wt[8], float flag, int narrow)
{
	memset(u, 0, 128);
	memcpy(u, off, 64);
	if (lpf) {
		memcpy(u + 64, wt, 32);
		memcpy(u + 96, &flag, 4);
		return narrow ? 112 : 96;
	}
	for (int k = 0; k < 8; k++) {
		_Float16 h = (_Float16)wt[k];
		memcpy(u + 64 + 2 * k, &h, 2);
	}
	if (getenv("BLUR_MUTATE"))      /* negative control: the weights two bytes late, as a layout mistake would put them */
		memmove(u + 66, u + 64, 14);
	_Float16 hf = (_Float16)flag;
	memcpy(u + 80, &hf, 2);
	return narrow ? 88 : 80;
}

static float hr(float x, int lpf) { return lpf ? x : (float)(_Float16)x; }

int
main(int argc, char **argv)
{
	CHECK(argc == 2);
	FILE *f = fopen(argv[1], "rb");
	CHECK(f != NULL);
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *data = malloc((size_t)n);
	CHECK(fread(data, 1, (size_t)n, f) == (size_t)n);
	fclose(f);
	void *mem = ralloc_context(NULL);
	struct air_metallib lib = { 0 };
	const char *perr = NULL;
	CHECK(air_metallib_parse(mem, data, (size_t)n, &lib, &perr));
	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_device *d = rwa_device_open(why, sizeof(why));
	CHECK(d != NULL);

	const int W = 64, H = 48, w2 = W / 2, h2 = H / 2;
	static const int widths[] = { 7, 11, 15, 19, 23, 27 };
	int failures = 0;
	double worst_all = 0;
	for (int lpf = 0; lpf < 2; lpf++) {
		const char *sfx = lpf ? "lpf" : "lph";
		float tol = lpf ? 2e-5f : 6e-3f;     /* half: taps, weights and every partial sum rounded to 11 bits */
		char name[64];
		snprintf(name, sizeof(name), "downsample_4_frag_%s", sfx);
		const struct air_metallib_function *fd = find(&lib, name);
		CHECK(fd != NULL);
		void *m1 = ralloc_context(NULL);
		nir_shader *down = prepare(m1, fd, d);
		CHECK(down != NULL);
		for (int wi = 0; wi < 6; wi++) {
			int taps = widths[wi], pairs = (taps + 1) / 4;
			snprintf(name, sizeof(name), "narrow_blur_%d_frag_%s", taps, sfx);
			const struct air_metallib_function *fn = find(&lib, name);
			CHECK(fn != NULL);
			void *m2 = ralloc_context(NULL);
			nir_shader *nb = prepare(m2, fn, d);
			CHECK(nb != NULL);
			unsigned ssa_down = nir_shader_get_entrypoint(down)->ssa_alloc, ssa_nb = nir_shader_get_entrypoint(nb)->ssa_alloc;
			struct machine m = { .v = calloc(MAX2(ssa_down, ssa_nb), sizeof(*m.v)) };
			for (int kind = 0; kind < 4; kind++) {
				struct img src = { W, H, calloc((size_t)W * H * 4, sizeof(float)) }, a = { w2, h2, calloc((size_t)w2 * h2 * 4, 4) },
					   b = { w2, h2, calloc((size_t)w2 * h2 * 4, 4) }, c = { w2, h2, calloc((size_t)w2 * h2 * 4, 4) };
				picture(kind, &src);
				for (int st = 0; st < 3; st++) {
					uint8_t u[128];
					float off[8][2] = { { 0 } }, wt[8] = { 0 }, edr = st == 2 ? 0.875f : 1.0f, flag = (kind == 2 && st == 2) ? 1.0f : 0.0f;
					const struct img *in = st == 0 ? &src : st == 1 ? &a : &b;
					struct img *out = st == 0 ? &a : st == 1 ? &b : &c;
					nir_shader *s = st == 0 ? down : nb;
					int np;
					if (st == 0) {
						float o4[4][2] = { { -0.5f / W, -0.5f / H }, { 0.5f / W, -0.5f / H }, { -0.5f / W, 0.5f / H }, { 0.5f / W, 0.5f / H } };
						for (int k = 0; k < 4; k++) { off[k][0] = o4[k][0]; off[k][1] = o4[k][1]; wt[k] = 0.25f; }
						np = 4;
					} else {
						gauss_pairs(pairs, taps / 6.0f, st == 1 ? 1.0f / w2 : 1.0f / h2, off, wt, st == 1 ? 0 : 1);
						np = pairs;
					}
					size_t us = uniforms(u, lpf, off, wt, flag, st != 0);
					uint8_t e[4];
					if (lpf) memcpy(e, &edr, 4); else { _Float16 he = (_Float16)edr; memcpy(e, &he, 2); }
					m.tex = in;
					memset(m.ubo, 0, sizeof(m.ubo));
					m.ubo[1] = u; m.ubo_size[1] = us;
					m.ubo[6] = e; m.ubo_size[6] = lpf ? 4 : 2;
					int bad = 0;
					double worst = 0;
					for (int y = 0; y < out->h; y++)
						for (int x = 0; x < out->w; x++) {
							m.tc[0] = (x + 0.5f) / out->w;
							m.tc[1] = (y + 0.5f) / out->h;
							m.wrote = 0;
							if (evaluate(s, &m) != 0) {
								snprintf(why, sizeof(why), "%s did not evaluate", st == 0 ? "downsample" : name);
								CHECK(0);
							}
							/* the model: the function's own arithmetic in float, its fetches and weights at its width */
							float acc[4] = { 0, 0, 0, 0 };
							for (int k = 0; k < np; k++) {
								float p0[4], p1[4];
								if (st == 0) {
									sample(in, m.tc[0] + off[k][0], m.tc[1] + off[k][1], p0);
									for (int ch = 0; ch < 4; ch++) acc[ch] += hr(wt[k], lpf) * hr(p0[ch], lpf);
								} else {
									sample(in, m.tc[0] - off[k][0], m.tc[1] - off[k][1], p0);
									sample(in, m.tc[0] + off[k][0], m.tc[1] + off[k][1], p1);
									for (int ch = 0; ch < 4; ch++) acc[ch] += hr(wt[k], lpf) * (hr(p0[ch], lpf) + hr(p1[ch], lpf));
								}
							}
							if (flag != 0) {      /* the whole vec4 over max(1.0004e-3, a): alpha comes out ~1 (the function's own fdiv) */
								float den = fmaxf(1.0004e-3f, acc[3]);
								for (int ch = 0; ch < 4; ch++)
									acc[ch] /= den;
							}
							for (int ch = 0; ch < 3; ch++)
								acc[ch] *= hr(edr, lpf);
							for (int ch = 0; ch < 4; ch++) {
								float g = m.out[ch];
								out->px[(y * out->w + x) * 4 + ch] = g;     /* the next stage reads THIS stage's result */
								double err = fabs(g - acc[ch]) / (flag != 0 ? fmaxf(1.0f, fabsf(acc[ch])) : 1.0f);
								worst = err > worst ? err : worst;
								bad += err > tol;
							}
						}
					worst_all = worst > worst_all ? worst : worst_all;
					if (bad) {
						failures++;
						printf("  %s %s picture %d stage %d: %d components off by more than %g (worst %g)\n", sfx, name, kind, st,
						       bad, tol, worst);
					}
				}
				free(src.px); free(a.px); free(b.px); free(c.px);
			}
			free(m.v);
			printf("  %s: downsample_4 + %s x + y, 4 pictures (one with unpremultiply, last stage EDR 0.875): checked\n", sfx, name);
			ralloc_free(m2);
		}
		ralloc_free(m1);
	}
	printf("blur_nir_host_test: %d stage results outside tolerance (half %g, float %g); worst error %.6f\n", failures, 6e-3, 2e-5,
	       worst_all);
	if (failures == 0)
		printf("blur_nir: ok\n");
	return failures != 0;
}
