/*
 * air_video.c -- the three video kernels of include/n2vk.h as fragment passes on the GPU.
 *
 * Included at the end of air_metal.c, after air_isp.c, and made of that file's parts: the
 * device, textures, the generated full-screen quad and make_stage_cso.  The processor's
 * answer is in kern/n2vk_ref.c; every value this file produces is checked against it,
 * bit for bit, by gpu/vid_gpu_host_test.c on softpipe and compiled for the Mali by
 * Panfrost's own Valhall compiler.
 *
 * The image of each pass:
 *
 *   me16: one fragment per macroblock.  The search window is walked in 9x9 tiles (at most
 *         81 candidates a pass, the bound the task asks for), each pass reading the
 *         previous 'best so far' texture and writing the next; for every candidate the
 *         16x16 SAD is accumulated from an R8Uint current and an R8Uint reference.  The
 *         candidate is valid only inside n2vk.h's padded rectangle, and the tie-break is
 *         the header's (cost, |dx-cx|+|dy-cy|, dy, dx) compared explicitly.
 *
 *   half:  one pass, four R8Uint texels to one.
 *
 *   fdct:  two passes.  The first stores t[y][u] as R32Sint; the second reads a column
 *         of that, does the second integer transform and divides by Q rounded half away
 *         from zero into R16Sint.  The cosine table is n2vk.h's literal 8x8, in a small
 *         R32Sint texture so the dynamic u/v index costs one texel fetch.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "rwa_vid_api.h"

/* The exported calls (the library is linked with hidden visibility everywhere else). */
unsigned rwa_vid_api_version(void);
struct rwa_vid *rwa_vid_create(char *why, size_t why_size);
void rwa_vid_destroy(struct rwa_vid *v);
int rwa_vid_me16(struct rwa_vid *v, const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad,
                 char *why, size_t why_size);
int rwa_vid_half(struct rwa_vid *v, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride,
                 uint32_t width, uint32_t height, char *why, size_t why_size);
int rwa_vid_fdct8_quant(struct rwa_vid *v, const uint8_t *src, uint32_t src_stride, uint32_t width, uint32_t height,
                        const uint16_t qtable[64], int16_t *coef, char *why, size_t why_size);
int rwa_vid_backend(n2vk_backend *out, struct rwa_vid *v);
struct rwa_device *rwa_vid_device(struct rwa_vid *v);

/* The shaders, also called by gpu/vid_gpu_host_test.c and the Panfrost compile check. */
nir_shader *rwa_vid_me16_shader(const struct nir_shader_compiler_options *options);
nir_shader *rwa_vid_half_shader(const struct nir_shader_compiler_options *options);
nir_shader *rwa_vid_dct_rows_shader(const struct nir_shader_compiler_options *options);
nir_shader *rwa_vid_dct_cols_shader(const struct nir_shader_compiler_options *options);

/* n2vk.h's c[u][x] table, the literal copy checked by kern/test/t_kern.c.  Row u, column x. */
static const int32_t vid_dct_c[64] = {
	2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896,
	4017, 3406, 2276,  799, -799, -2276, -3406, -4017,
	3784, 1567, -1567, -3784, -3784, -1567, 1567, 3784,
	3406, -799, -4017, -2276, 2276, 4017,  799, -3406,
	2896, -2896, -2896, 2896, 2896, -2896, -2896, 2896,
	2276, -4017,  799, 3406, -3406, -799, 4017, -2276,
	1567, -3784, 3784, -1567, -1567, 3784, -3784, 1567,
	 799, -2276, 3406, -4017, 4017, -3406, 2276, -799,
};

/* The one block every shader reads, in 32-bit words (the offsets are compile-time, as
 * the library's ubo extent analysis expects). */
struct vid_block {
	int32_t w, h;             /* the pass's output picture, in fragments */
	int32_t pad, range;
	int32_t tile_dx, tile_dy; /* this pass's 9x9 tile, relative to the clamped centre */
	int32_t first;            /* 1: no previous best texture (the first pass) */
	int32_t ref_w, ref_h;     /* the reference texture's size, padded */
};
enum { VID_W = 0, VID_H, VID_PAD, VID_RANGE, VID_TILE_DX, VID_TILE_DY, VID_FIRST, VID_REF_W, VID_REF_H };
_Static_assert(sizeof(struct vid_block) == 9 * 4, "struct vid_block is the shaders' block, word for word");

struct rwa_vid {
	struct rwa_device *device;
	void *shader_me16, *shader_half, *shader_dct_rows, *shader_dct_cols;
	void *sampler;

	struct rwa_texture *cur, *ref, *centre, *best[2];
	struct rwa_texture *half_src, *half_dst;
	struct rwa_texture *dct_src, *dct_rows, *dct_out, *dct_c, *dct_q;

	int32_t *words;             /* me16 best textures, 4 * mb_w * mb_h */
	size_t words_n;
	int32_t *centres;           /* me16 centre texture, 2 * mb_w * mb_h */
	size_t centres_n;
	int16_t *dct_coef;          /* fdct's image-raster readback, repacked to block-raster order */
	size_t dct_coef_n;
	uint8_t *scratch;           /* half's and fdct's tiny staging are the caller's; this is unused for now */
	size_t scratch_n;
};

/* ---- NIR helpers, in air_isp.c's style ---------------------------------------------------------------------------------------- */

/* |x - y| for non-negative 32-bit integers, without nir_builtin_builder.h. */
static nir_def *
vid_absdiff(nir_builder *b, nir_def *x, nir_def *y)
{
	return nir_bcsel(b, nir_ige(b, x, y), nir_isub(b, x, y), nir_isub(b, y, x));
}

static nir_def *
vid_word(nir_builder *b, unsigned word)
{
	return nir_load_ubo(b, 1, 32, nir_imm_int(b, 0), nir_imm_int(b, (int)word * 4), .align_mul = 4, .align_offset = 0,
	                    .range_base = word * 4, .range = 4);
}

/* A texel by its whole-number place, level 0.  `type' is the texture's element type:
 * nir_type_uint32 for R8Uint/R16Uint/R32Uint, nir_type_int32 for the SINT formats. */
static nir_def *
vid_fetch(nir_builder *b, unsigned unit, nir_def *x, nir_def *y, nir_alu_type type)
{
	nir_tex_instr *tex = nir_tex_instr_create(b->shader, 2);

	tex->op = nir_texop_txf;
	tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
	tex->dest_type = type;
	tex->coord_components = 2;
	tex->texture_index = unit;
	tex->sampler_index = unit;
	tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_vec2(b, x, y));
	tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_int(b, 0));
	nir_def_init(&tex->instr, &tex->def, 4, 32);
	nir_builder_instr_insert(b, &tex->instr);
	return &tex->def;
}

static nir_def *
vid_channel(nir_builder *b, unsigned unit, nir_def *x, nir_def *y, unsigned channel, nir_alu_type type)
{
	return nir_channel(b, vid_fetch(b, unit, x, y, type), channel);
}

/* The fragment's own pixel, from the quad's coordinate times the block's picture size. */
static void
vid_pixel_xy(nir_builder *b, nir_variable *uv, nir_def **x, nir_def **y)
{
	nir_def *at = nir_load_var(b, uv);

	*x = nir_f2i32(b, nir_fmul(b, nir_channel(b, at, 0), nir_i2f32(b, vid_word(b, VID_W))));
	*y = nir_f2i32(b, nir_fmul(b, nir_channel(b, at, 1), nir_i2f32(b, vid_word(b, VID_H))));
}

static nir_builder
vid_fragment_begin(const struct nir_shader_compiler_options *options, const char *name, const glsl_type *out_type,
                   nir_variable **uv, nir_variable **out)
{
	nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, options, "%s", name);

	*uv = nir_variable_create(b.shader, nir_var_shader_in, glsl_vec4_type(), "uv");
	(*uv)->data.location = VARYING_SLOT_VAR0;
	(*uv)->data.driver_location = 0;
	(*uv)->data.interpolation = INTERP_MODE_SMOOTH;
	*out = nir_variable_create(b.shader, nir_var_shader_out, out_type, "color");
	(*out)->data.location = FRAG_RESULT_DATA0;
	(*out)->data.driver_location = 0;
	return b;
}

static nir_shader *
vid_fragment_end(nir_builder *b, unsigned textures)
{
	b->shader->num_inputs = 1;
	b->shader->num_outputs = 1;
	b->shader->info.num_textures = textures;
	b->shader->info.num_ubos = 1;
	for (unsigned i = 0; i < textures; i++) {
		BITSET_SET(b->shader->info.textures_used, i);
		BITSET_SET(b->shader->info.samplers_used, i);
	}
	return b->shader;
}

static nir_variable *
vid_temp(nir_builder *b, const char *name)
{
	return nir_local_variable_create(b->impl, glsl_int_type(), name);
}

/* i = 0; loop { if (i >= limit) break; ...; i++; } */
static nir_loop *
vid_loop_open(nir_builder *b, nir_variable *i, int limit)
{
	nir_store_var(b, i, nir_imm_int(b, 0), 0x1);
	nir_loop *loop = nir_push_loop(b);

	nir_break_if(b, nir_ige_imm(b, nir_load_var(b, i), (uint64_t)limit));
	return loop;
}

static void
vid_loop_close(nir_builder *b, nir_variable *i, nir_loop *loop)
{
	nir_store_var(b, i, nir_iadd_imm(b, nir_load_var(b, i), 1), 0x1);
	nir_pop_loop(b, loop);
}

/* ---- K1: me16 ---------------------------------------------------------------------------------------------------------------- */

nir_shader *
rwa_vid_me16_shader(const struct nir_shader_compiler_options *options)
{
	nir_variable *uv, *out;
	nir_builder b = vid_fragment_begin(options, "rwa_vid_me16", glsl_ivec4_type(), &uv, &out);
	nir_variable *ji = vid_temp(&b, "tile_y"), *ii = vid_temp(&b, "tile_x"), *kk = vid_temp(&b, "sample");
	nir_variable *bs = vid_temp(&b, "best_sad"), *bx = vid_temp(&b, "best_dx"), *by = vid_temp(&b, "best_dy");
	nir_variable *sad = vid_temp(&b, "sad");
	nir_def *mx, *my, *zero = nir_imm_int(&b, 0);

	vid_pixel_xy(&b, uv, &mx, &my);
	nir_def *mb_w = vid_word(&b, VID_W), *mb_h = vid_word(&b, VID_H);
	nir_def *pad = vid_word(&b, VID_PAD), *range = vid_word(&b, VID_RANGE);
	nir_def *lo_x = nir_isub(&b, nir_imul_imm(&b, mx, -16), pad);
	nir_def *hi_x = nir_iadd(&b, nir_imul_imm(&b, nir_isub(&b, nir_isub(&b, mb_w, nir_imm_int(&b, 1)), mx), 16), pad);
	nir_def *lo_y = nir_isub(&b, nir_imul_imm(&b, my, -16), pad);
	nir_def *hi_y = nir_iadd(&b, nir_imul_imm(&b, nir_isub(&b, nir_isub(&b, mb_h, nir_imm_int(&b, 1)), my), 16), pad);
	nir_def *cx = vid_channel(&b, 2, mx, my, 0, nir_type_int32);
	nir_def *cy = vid_channel(&b, 2, mx, my, 1, nir_type_int32);

	cx = nir_imin(&b, nir_imax(&b, cx, lo_x), hi_x);
	cy = nir_imin(&b, nir_imax(&b, cy, lo_y), hi_y);

	/* the best so far: none on the first pass, the previous pass's answer otherwise */
	nir_def *first = nir_ine_imm(&b, vid_word(&b, VID_FIRST), 0);
	nir_def *old = vid_fetch(&b, 3, mx, my, nir_type_int32);

	nir_store_var(&b, bs, nir_bcsel(&b, first, nir_imm_int(&b, INT32_MAX), nir_channel(&b, old, 2)), 0x1);
	nir_store_var(&b, bx, nir_bcsel(&b, first, zero, nir_channel(&b, old, 0)), 0x1);
	nir_store_var(&b, by, nir_bcsel(&b, first, zero, nir_channel(&b, old, 1)), 0x1);

	nir_loop *lj = vid_loop_open(&b, ji, 9);
	nir_def *j = nir_load_var(&b, ji);

	nir_loop *li = vid_loop_open(&b, ii, 9);
	{
		nir_def *i = nir_load_var(&b, ii);
		nir_def *dx = nir_iadd(&b, nir_iadd(&b, cx, vid_word(&b, VID_TILE_DX)), i);
		nir_def *dy = nir_iadd(&b, nir_iadd(&b, cy, vid_word(&b, VID_TILE_DY)), j);
		nir_def *inside = nir_iand(&b,
			nir_iand(&b, nir_ige(&b, dx, lo_x), nir_ige(&b, hi_x, dx)),
			nir_iand(&b, nir_ige(&b, dy, lo_y), nir_ige(&b, hi_y, dy)));
		/* the tile is 9x9; the last one may stick out of the square of side
		 * 2*range+1, and those places are not candidates */
		nir_def *in_range = nir_iand(&b,
			nir_iand(&b, nir_ige(&b, dx, nir_isub(&b, cx, range)), nir_ige(&b, nir_iadd(&b, cx, range), dx)),
			nir_iand(&b, nir_ige(&b, dy, nir_isub(&b, cy, range)), nir_ige(&b, nir_iadd(&b, cy, range), dy)));
		nir_def *valid = nir_iand(&b, inside, in_range);

		nir_store_var(&b, sad, zero, 0x1);
		nir_loop *lk = vid_loop_open(&b, kk, 256);
		{
			nir_def *k = nir_load_var(&b, kk);
			nir_def *xx = nir_iand_imm(&b, k, 15), *yy = nir_ishr_imm(&b, k, 4);
			nir_def *cxat = nir_iadd(&b, nir_imul_imm(&b, mx, 16), xx);
			nir_def *cyat = nir_iadd(&b, nir_imul_imm(&b, my, 16), yy);
			/* Clamped for invalid candidates (whose cost is discarded): every fetch
			 * is inside the texture, so no coordinate depends on the padding being
			 * readable at the edge of what this pass bound. */
			nir_def *rx = nir_imin(&b, nir_imax(&b, nir_iadd(&b, nir_iadd(&b, cxat, dx), pad), zero),
			                            nir_isub(&b, vid_word(&b, VID_REF_W), nir_imm_int(&b, 1)));
			nir_def *ry = nir_imin(&b, nir_imax(&b, nir_iadd(&b, nir_iadd(&b, cyat, dy), pad), zero),
			                            nir_isub(&b, vid_word(&b, VID_REF_H), nir_imm_int(&b, 1)));
			nir_def *c = vid_channel(&b, 0, cxat, cyat, 0, nir_type_uint32);
			nir_def *r = vid_channel(&b, 1, rx, ry, 0, nir_type_uint32);

			nir_store_var(&b, sad, nir_iadd(&b, nir_load_var(&b, sad), vid_absdiff(&b, c, r)), 0x1);
		}
		vid_loop_close(&b, kk, lk);

		nir_def *cost = nir_load_var(&b, sad);
		nir_def *best_sad = nir_load_var(&b, bs), *best_dx = nir_load_var(&b, bx), *best_dy = nir_load_var(&b, by);
		nir_def *rank = nir_iadd(&b, vid_absdiff(&b, dx, cx), vid_absdiff(&b, dy, cy));
		nir_def *best_rank = nir_iadd(&b, vid_absdiff(&b, best_dx, cx), vid_absdiff(&b, best_dy, cy));
		nir_def *better = nir_ior(&b, nir_ilt(&b, cost, best_sad),
			nir_iand(&b, nir_ieq(&b, cost, best_sad),
				nir_ior(&b, nir_ilt(&b, rank, best_rank),
					nir_iand(&b, nir_ieq(&b, rank, best_rank),
						nir_ior(&b, nir_ilt(&b, dy, best_dy),
							nir_iand(&b, nir_ieq(&b, dy, best_dy), nir_ilt(&b, dx, best_dx)))))));

		better = nir_iand(&b, better, valid);
		nir_store_var(&b, bs, nir_bcsel(&b, better, cost, best_sad), 0x1);
		nir_store_var(&b, bx, nir_bcsel(&b, better, dx, best_dx), 0x1);
		nir_store_var(&b, by, nir_bcsel(&b, better, dy, best_dy), 0x1);
	}
	vid_loop_close(&b, ii, li);
	vid_loop_close(&b, ji, lj);

	nir_store_var(&b, out, nir_vec4(&b, nir_load_var(&b, bx), nir_load_var(&b, by), nir_load_var(&b, bs), zero), 0xf);
	return vid_fragment_end(&b, 4);
}

/* ---- K2: half ---------------------------------------------------------------------------------------------------------------- */

nir_shader *
rwa_vid_half_shader(const struct nir_shader_compiler_options *options)
{
	nir_variable *uv, *out;
	nir_builder b = vid_fragment_begin(options, "rwa_vid_half", glsl_uvec4_type(), &uv, &out);
	nir_def *x, *y, *zero = nir_imm_int(&b, 0);

	vid_pixel_xy(&b, uv, &x, &y);
	nir_def *x0 = nir_ishl_imm(&b, x, 1), *y0 = nir_ishl_imm(&b, y, 1);
	nir_def *sum = zero;

	for (int i = 0; i < 4; i++)
		sum = nir_iadd(&b, sum, vid_channel(&b, 0, nir_iadd_imm(&b, x0, i & 1), nir_iadd_imm(&b, y0, i >> 1), 0,
		                                    nir_type_uint32));
	nir_def *v = nir_ushr_imm(&b, nir_iadd_imm(&b, sum, 2), 2);

	nir_store_var(&b, out, nir_vec4(&b, v, zero, zero, zero), 0xf);
	return vid_fragment_end(&b, 1);
}

/* ---- K3: fdct + quant, pass 1 (rows) ------------------------------------------------------------------------------------------ */

nir_shader *
rwa_vid_dct_rows_shader(const struct nir_shader_compiler_options *options)
{
	nir_variable *uv, *out;
	nir_builder b = vid_fragment_begin(options, "rwa_vid_dct_rows", glsl_ivec4_type(), &uv, &out);
	nir_def *x, *y, *zero = nir_imm_int(&b, 0);

	vid_pixel_xy(&b, uv, &x, &y);
	nir_def *u = nir_iand_imm(&b, x, 7), *bx = nir_isub(&b, x, u);
	nir_def *sum = zero;

	for (int t = 0; t < 8; t++) {
		nir_def *f = nir_isub(&b, vid_channel(&b, 0, nir_iadd_imm(&b, bx, t), y, 0, nir_type_uint32), nir_imm_int(&b, 128));
		nir_def *c = vid_channel(&b, 1, nir_iadd_imm(&b, nir_imul_imm(&b, u, 8), t), zero, 0, nir_type_int32);

		sum = nir_iadd(&b, sum, nir_imul(&b, c, f));
	}
	nir_def *v = nir_ishr_imm(&b, nir_iadd_imm(&b, sum, 512), 10);

	nir_store_var(&b, out, nir_vec4(&b, v, zero, zero, zero), 0xf);
	return vid_fragment_end(&b, 2);
}

/* ---- K3: fdct + quant, pass 2 (columns and the division) ----------------------------------------------------------------------- */

nir_shader *
rwa_vid_dct_cols_shader(const struct nir_shader_compiler_options *options)
{
	nir_variable *uv, *out;
	nir_builder b = vid_fragment_begin(options, "rwa_vid_dct_cols", glsl_ivec4_type(), &uv, &out);
	nir_def *x, *y, *zero = nir_imm_int(&b, 0);

	vid_pixel_xy(&b, uv, &x, &y);
	nir_def *u = nir_iand_imm(&b, x, 7), *v = nir_iand_imm(&b, y, 7);
	nir_def *bx = nir_isub(&b, x, u), *by = nir_isub(&b, y, v);
	nir_def *sum = zero;

	for (int t = 0; t < 8; t++) {
		nir_def *tv = vid_channel(&b, 0, nir_iadd(&b, bx, u), nir_iadd_imm(&b, by, t), 0, nir_type_int32);
		nir_def *c = vid_channel(&b, 1, nir_iadd_imm(&b, nir_imul_imm(&b, v, 8), t), zero, 0, nir_type_int32);

		sum = nir_iadd(&b, sum, nir_imul(&b, c, tv));
	}
	nir_def *f = nir_ishr_imm(&b, nir_iadd_imm(&b, sum, 32768), 16);
	nir_def *q = vid_channel(&b, 2, u, v, 0, nir_type_uint32);
	nir_def *magnitude = nir_iabs(&b, f);
	nir_def *rounded = nir_idiv(&b, nir_iadd(&b, magnitude, nir_ushr_imm(&b, q, 1)), q);
	nir_def *signed_q = nir_bcsel(&b, nir_ilt_imm(&b, f, 0), nir_isub(&b, zero, rounded), rounded);

	nir_store_var(&b, out, nir_vec4(&b, signed_q, zero, zero, zero), 0xf);
	return vid_fragment_end(&b, 3);
}

/* ---- the context ------------------------------------------------------------------------------------------------------------- */

static struct rwa_texture *
vid_texture(struct rwa_vid *v, struct rwa_texture **slot, unsigned format, unsigned width, unsigned height, unsigned usage,
            char *why, size_t why_size)
{
	if (*slot != NULL && ((*slot)->res->width0 != width || (*slot)->res->height0 != height)) {
		rwa_texture_destroy(*slot);
		*slot = NULL;
	}
	if (*slot == NULL) {
		const struct rwa_texture_shape shape = { .pixel_format = format, .type = RWA_TYPE_2D, .width = width, .height = height,
		                                         .depth = 1, .array_length = 1, .mip_levels = 1, .sample_count = 1, .usage = usage };

		*slot = rwa_texture_create(v->device, &shape, why, why_size);
	}
	return *slot;
}

static int
vid_write(struct rwa_texture *t, unsigned w, unsigned h, const void *bytes, size_t stride, char *why, size_t why_size)
{
	const unsigned origin[3] = { 0, 0, 0 }, size[3] = { w, h, 1 };

	return rwa_texture_write(t, 0, 0, origin, size, bytes, stride, why, why_size) == 0 ? RWA_VID_OK : RWA_VID_E_GPU;
}

static int
vid_read(struct rwa_texture *t, unsigned w, unsigned h, void *bytes, size_t stride, char *why, size_t why_size)
{
	const unsigned origin[3] = { 0, 0, 0 }, size[3] = { w, h, 1 };

	return rwa_texture_read(t, 0, 0, origin, size, bytes, stride, why, why_size) == 0 ? RWA_VID_OK : RWA_VID_E_GPU;
}

static struct pipe_sampler_view *
vid_view(struct rwa_device *device, struct rwa_texture *t)
{
	if (!t->view) {
		struct pipe_sampler_view tmpl = { 0 };

		u_sampler_view_default_template(&tmpl, t->res, t->format);
		tmpl.target = t->target;
		t->view = device->pipe->create_sampler_view(device->pipe, t->res, &tmpl);
	}
	return t->view;
}

/* One full-screen pass into `dst', with `count' textures at units 0..count-1 and `block'
 * as constant buffer 0.  Lock held (the device's). */
static int
vid_pass(struct rwa_vid *v, struct rwa_texture *dst, void *fs, struct rwa_texture *const *textures, unsigned count,
         const struct vid_block *block, const char *what, char *why, size_t why_size)
{
	struct rwa_device *device = v->device;
	struct pipe_context *pipe = device->pipe;
	struct pipe_sampler_view *views[5] = { NULL };
	void *samplers[5] = { NULL }, *none[5] = { NULL };
	const unsigned w = dst->res->width0, h = dst->res->height0;

	if (!ycbcr_state(device, why, why_size))
		return RWA_VID_E_GPU;
	for (unsigned i = 0; i < count; i++) {
		views[i] = vid_view(device, textures[i]);
		samplers[i] = v->sampler;
		if (!views[i]) {
			say(why, why_size, "%s would not make a sampler view for the video %s pass", rwa_device_name(device), what);
			return RWA_VID_E_GPU;
		}
	}
	struct pipe_surface *surface = texture_surface(device, dst, 0, 0);

	if (!surface) {
		say(why, why_size, "%s would not make a render surface for the video %s pass", rwa_device_name(device), what);
		return RWA_VID_E_GPU;
	}
	struct pipe_framebuffer_state fb = { .width = w, .height = h, .nr_cbufs = 1, .samples = 1 };

	fb.cbufs[0] = surface;
	pipe->set_framebuffer_state(pipe, &fb);
	pipe->set_viewport_states(pipe, 0, 1, &(struct pipe_viewport_state){
		.scale = { w / 2.0f, h / 2.0f, 1.0f }, .translate = { w / 2.0f, h / 2.0f, 0.0f } });
	pipe->bind_blend_state(pipe, device->ycbcr.blend);
	pipe->bind_depth_stencil_alpha_state(pipe, device->ycbcr.dsa);
	pipe->set_stencil_ref(pipe, (struct pipe_stencil_ref){ 0 });
	pipe->bind_rasterizer_state(pipe, device->ycbcr.rast);
	pipe->bind_vs_state(pipe, device->ycbcr.vs);
	pipe->bind_fs_state(pipe, fs);
	pipe->bind_vertex_elements_state(pipe, device->ycbcr.velem);
	{
		struct pipe_vertex_buffer vb = { 0 };

		pipe_resource_reference(&vb.buffer.resource, device->ycbcr.vbuf);
		pipe->set_vertex_buffers(pipe, 1, &vb);
	}
	pipe->set_sampler_views(pipe, PIPE_SHADER_FRAGMENT, 0, count, 0, false, views);
	pipe->bind_sampler_states(pipe, PIPE_SHADER_FRAGMENT, 0, count, samplers);
	{
		struct pipe_constant_buffer cb = { .buffer_size = sizeof(*block), .user_buffer = block };

		pipe->set_constant_buffer(pipe, PIPE_SHADER_FRAGMENT, 0, false, &cb);
	}
	{
		union pipe_color_union zero = { .f = { 0.0f, 0.0f, 0.0f, 0.0f } };
		struct pipe_draw_info info = {
			.mode = MESA_PRIM_TRIANGLE_STRIP, .index_bounds_valid = true,
			.min_index = 0, .max_index = 3, .instance_count = 1,
		};
		struct pipe_draw_start_count_bias sc = { .start = 0, .count = 4 };
		struct pipe_scissor_state scissor = { 0, 0, (uint16_t)MIN2(w, 65535u), (uint16_t)MIN2(h, 65535u) };

		pipe->clear(pipe, PIPE_CLEAR_COLOR0, NULL, &zero, 0.0, 0);
		pipe->set_scissor_states(pipe, 0, 1, &scissor);
		pipe->draw_vbo(pipe, &info, 0, NULL, &sc, 1);
	}
	pipe->set_constant_buffer(pipe, PIPE_SHADER_FRAGMENT, 0, false, NULL);
	pipe->set_sampler_views(pipe, PIPE_SHADER_FRAGMENT, 0, 0, count, false, NULL);
	pipe->bind_sampler_states(pipe, PIPE_SHADER_FRAGMENT, 0, count, none);
	pipe->set_framebuffer_state(pipe, &(struct pipe_framebuffer_state){ 0 });
	return RWA_VID_OK;
}

static void *
vid_shader(struct rwa_vid *v, void **slot, nir_shader *nir, unsigned format, char *why, size_t why_size)
{
	if (*slot == NULL && nir != NULL) {
		unsigned f = format;

		*slot = make_stage_cso(v->device, nir, &f, 1, false, NULL, why, why_size);
	}
	if (nir != NULL)
		ralloc_free(nir);
	return *slot;
}

unsigned
rwa_vid_api_version(void)
{
	return RWA_VID_API_VERSION;
}

struct rwa_device *
rwa_vid_device(struct rwa_vid *v)
{
	return v != NULL ? v->device : NULL;
}

struct rwa_vid *
rwa_vid_create(char *why, size_t why_size)
{
	struct rwa_vid *v = calloc(1, sizeof(*v));

	if (v == NULL) {
		say(why, why_size, "out of memory for a video-kernel context");
		return NULL;
	}
	v->device = rwa_device_open(why, why_size);
	if (v->device == NULL) {
		free(v);
		return NULL;
	}
	v->sampler = v->device->pipe->create_sampler_state(v->device->pipe,
		&(struct pipe_sampler_state){
			.wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE, .wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE, .wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
			.min_img_filter = PIPE_TEX_FILTER_NEAREST, .mag_img_filter = PIPE_TEX_FILTER_NEAREST,
			.min_mip_filter = PIPE_TEX_MIPFILTER_NONE });
	if (v->sampler == NULL) {
		say(why, why_size, "%s would not make the video kernels' sampler", rwa_device_name(v->device));
		rwa_device_close(v->device);
		free(v);
		return NULL;
	}
	return v;
}

static void
vid_destroy_textures(struct rwa_vid *v)
{
	struct rwa_texture **all[] = { &v->cur, &v->ref, &v->centre, &v->best[0], &v->best[1], &v->half_src, &v->half_dst,
	                               &v->dct_src, &v->dct_rows, &v->dct_out, &v->dct_c, &v->dct_q };

	for (unsigned i = 0; i < ARRAY_SIZE(all); i++)
		if (*all[i] != NULL) {
			rwa_texture_destroy(*all[i]);
			*all[i] = NULL;
		}
}

void
rwa_vid_destroy(struct rwa_vid *v)
{
	if (v == NULL)
		return;
	v->device->pipe->flush(v->device->pipe, NULL, 0);
	vid_destroy_textures(v);
	{
		void *fs[4] = { v->shader_me16, v->shader_half, v->shader_dct_rows, v->shader_dct_cols };

		v->device->pipe->bind_fs_state(v->device->pipe, NULL);
		for (unsigned i = 0; i < 4; i++)
			if (fs[i] != NULL)
				v->device->pipe->delete_fs_state(v->device->pipe, fs[i]);
	}
	if (v->sampler != NULL) {
		void *none[1] = { NULL };

		v->device->pipe->bind_sampler_states(v->device->pipe, PIPE_SHADER_FRAGMENT, 0, 1, none);
		v->device->pipe->delete_sampler_state(v->device->pipe, v->sampler);
	}
	free(v->words);
	free(v->centres);
	free(v->dct_coef);
	free(v->scratch);
	rwa_device_close(v->device);
	free(v);
}

static int
vid_me_check(const n2vk_me16_args *a, char *why, size_t why_size)
{
	if (a == NULL || a->size < sizeof(*a) || a->cur == NULL || a->ref == NULL) {
		say(why, why_size, "me16 was given no arguments, a struct smaller than this library's, or no picture");
		return RWA_VID_E_ARGUMENT;
	}
	if (a->mb_w < 1 || a->mb_w > 256 || a->mb_h < 1 || a->mb_h > 144 || a->pad > 64 || a->range > 32) {
		say(why, why_size, "me16's %ux%u macroblocks, pad %u, range %u are outside n2vk.h's limits", a->mb_w, a->mb_h, a->pad,
		    a->range);
		return RWA_VID_E_ARGUMENT;
	}
	if (a->cur_stride < 16u * a->mb_w || a->ref_stride < 16u * a->mb_w + 2u * a->pad) {
		say(why, why_size, "me16's strides (%u and %u bytes) do not hold a %u-wide picture with %u samples of padding",
		    a->cur_stride, a->ref_stride, 16u * a->mb_w, a->pad);
		return RWA_VID_E_ARGUMENT;
	}
	if ((a->centre_x == NULL) != (a->centre_y == NULL)) {
		say(why, why_size, "me16 has one centre array and not the other");
		return RWA_VID_E_ARGUMENT;
	}
	return RWA_VID_OK;
}

int
rwa_vid_me16(struct rwa_vid *v, const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad,
             char *why, size_t why_size)
{
	if (v == NULL || mvx == NULL || mvy == NULL || sad == NULL) {
		say(why, why_size, "me16 was given no context or nowhere to put its answer");
		return RWA_VID_E_ARGUMENT;
	}
	int rc = vid_me_check(a, why, why_size);

	if (rc != RWA_VID_OK)
		return rc;
	RWA_GUARD();

	const unsigned mb_w = a->mb_w, mb_h = a->mb_h, pad = a->pad;
	const unsigned cw = 16 * mb_w, ch = 16 * mb_h, rw = cw + 2 * pad, rh = ch + 2 * pad;
	const size_t n = (size_t)mb_w * mb_h;
	struct rwa_texture *cur = vid_texture(v, &v->cur, 13 /* R8Uint */, cw, ch, RWA_USAGE_SHADER_READ, why, why_size);
	struct rwa_texture *ref = vid_texture(v, &v->ref, 13, rw, rh, RWA_USAGE_SHADER_READ, why, why_size);
	struct rwa_texture *centre = vid_texture(v, &v->centre, 104 /* RG32Sint */, mb_w, mb_h, RWA_USAGE_SHADER_READ, why, why_size);

	if (cur == NULL || ref == NULL || centre == NULL ||
	    vid_texture(v, &v->best[0], 124 /* RGBA32Sint */, mb_w, mb_h, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why,
	                why_size) == NULL ||
	    vid_texture(v, &v->best[1], 124, mb_w, mb_h, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, why_size) == NULL)
		return RWA_VID_E_MEMORY;

	if (v->words_n < 4 * n) {
		int32_t *words = realloc(v->words, 4 * n * sizeof(*words));

		if (words == NULL) {
			say(why, why_size, "out of memory for me16's %zu macroblocks", n);
			return RWA_VID_E_MEMORY;
		}
		v->words = words;
		v->words_n = 4 * n;
	}
	if (v->centres_n < 2 * n) {
		int32_t *centres = realloc(v->centres, 2 * n * sizeof(*centres));

		if (centres == NULL) {
			say(why, why_size, "out of memory for me16's %zu centres", n);
			return RWA_VID_E_MEMORY;
		}
		v->centres = centres;
		v->centres_n = 2 * n;
	}
	for (size_t i = 0; i < n; i++) {
		v->words[4 * i + 0] = 0;
		v->words[4 * i + 1] = 0;
		v->words[4 * i + 2] = INT32_MAX;
		v->words[4 * i + 3] = 0;
		v->centres[2 * i + 0] = a->centre_x != NULL ? a->centre_x[i] : 0;
		v->centres[2 * i + 1] = a->centre_y != NULL ? a->centre_y[i] : 0;
	}

	/* the pictures */
	if ((rc = vid_write(cur, cw, ch, a->cur, a->cur_stride, why, why_size)) != RWA_VID_OK ||
	    (rc = vid_write(ref, rw, rh, a->ref - (ptrdiff_t)pad * (ptrdiff_t)a->ref_stride - (ptrdiff_t)pad, a->ref_stride, why,
	                    why_size)) != RWA_VID_OK ||
	    (rc = vid_write(centre, mb_w, mb_h, v->centres, mb_w * 8, why, why_size)) != RWA_VID_OK)
		return rc;
	/* the first pass does not read its source, but a defined texture costs two small writes and makes the test deterministic */
	if (vid_write(v->best[0], mb_w, mb_h, v->words, mb_w * 16, why, why_size) != RWA_VID_OK ||
	    vid_write(v->best[1], mb_w, mb_h, v->words, mb_w * 16, why, why_size) != RWA_VID_OK)
		return RWA_VID_E_GPU;

	void *fs = vid_shader(v, &v->shader_me16, rwa_vid_me16_shader(v->device->nir_options), 124, why, why_size);

	if (fs == NULL)
		return RWA_VID_E_GPU;

	const unsigned span = 2 * a->range + 1, tiles = (span + 8) / 9;
	int src = 1, dst = 0;

	struct vid_block block = { .w = (int32_t)mb_w, .h = (int32_t)mb_h, .pad = (int32_t)pad, .range = (int32_t)a->range,
	                           .ref_w = (int32_t)rw, .ref_h = (int32_t)rh };
	for (unsigned ty = 0; ty < tiles; ty++) {
		for (unsigned tx = 0; tx < tiles; tx++) {
			block.tile_dx = -(int32_t)a->range + 9 * (int32_t)tx;
			block.tile_dy = -(int32_t)a->range + 9 * (int32_t)ty;
			block.first = ty == 0 && tx == 0;
			struct rwa_texture *textures[4] = { cur, ref, centre, v->best[src] };

			rc = vid_pass(v, v->best[dst], fs, textures, 4, &block, "me16", why, why_size);
			if (rc != RWA_VID_OK)
				return rc;
			int t = src;
			src = dst;
			dst = t;
		}
	}
	if (vid_read(v->best[src], mb_w, mb_h, v->words, mb_w * 16, why, why_size) != RWA_VID_OK)
		return RWA_VID_E_GPU;
	for (size_t i = 0; i < n; i++) {
		mvx[i] = (int16_t)v->words[4 * i + 0];
		mvy[i] = (int16_t)v->words[4 * i + 1];
		sad[i] = (uint32_t)v->words[4 * i + 2];
	}
	v->device->pipe->flush(v->device->pipe, NULL, 0);
	return RWA_VID_OK;
}

int
rwa_vid_half(struct rwa_vid *v, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride,
             uint32_t width, uint32_t height, char *why, size_t why_size)
{
	if (v == NULL || src == NULL || dst == NULL || width == 0 || height == 0 || width > UINT32_MAX / 2 ||
	    height > UINT32_MAX / 2 || src_stride < 2u * width || dst_stride < width) {
		say(why, why_size, "half was given no context, no picture, or a size/stride n2vk.h does not take");
		return RWA_VID_E_ARGUMENT;
	}
	RWA_GUARD();
	struct rwa_texture *in = vid_texture(v, &v->half_src, 13, 2 * width, 2 * height, RWA_USAGE_SHADER_READ, why, why_size);
	struct rwa_texture *out = vid_texture(v, &v->half_dst, 13, width, height, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why,
	                                     why_size);

	if (in == NULL || out == NULL)
		return RWA_VID_E_MEMORY;
	int rc = vid_write(in, 2 * width, 2 * height, src, src_stride, why, why_size);

	if (rc != RWA_VID_OK)
		return rc;
	void *fs = vid_shader(v, &v->shader_half, rwa_vid_half_shader(v->device->nir_options), 13, why, why_size);

	if (fs == NULL)
		return RWA_VID_E_GPU;
	struct vid_block block = { .w = (int32_t)width, .h = (int32_t)height };
	struct rwa_texture *textures[1] = { in };

	rc = vid_pass(v, out, fs, textures, 1, &block, "half", why, why_size);
	if (rc != RWA_VID_OK)
		return rc;
	if (vid_read(out, width, height, dst, dst_stride, why, why_size) != RWA_VID_OK)
		return RWA_VID_E_GPU;
	v->device->pipe->flush(v->device->pipe, NULL, 0);
	return RWA_VID_OK;
}

int
rwa_vid_fdct8_quant(struct rwa_vid *v, const uint8_t *src, uint32_t src_stride, uint32_t width, uint32_t height,
                    const uint16_t qtable[64], int16_t *coef, char *why, size_t why_size)
{
	if (v == NULL || src == NULL || qtable == NULL || coef == NULL || width == 0 || height == 0 || (width & 7u) != 0 ||
	    (height & 7u) != 0 || src_stride < width) {
		say(why, why_size, "fdct was given no context, no picture, a size that is not a multiple of 8, or a short stride");
		return RWA_VID_E_ARGUMENT;
	}
	for (int i = 0; i < 64; i++)
		if (qtable[i] == 0) {
			say(why, why_size, "fdct's qtable has a zero at %d: n2vk.h divides by it", i);
			return RWA_VID_E_ARGUMENT;
		}
	RWA_GUARD();
	struct rwa_texture *in = vid_texture(v, &v->dct_src, 13, width, height, RWA_USAGE_SHADER_READ, why, why_size);
	struct rwa_texture *rows = vid_texture(v, &v->dct_rows, 54 /* R32Sint */, width, height,
	                                       RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, why_size);
	struct rwa_texture *out = vid_texture(v, &v->dct_out, 24 /* R16Sint */, width, height,
	                                      RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, why_size);
	struct rwa_texture *ct = vid_texture(v, &v->dct_c, 54, 64, 1, RWA_USAGE_SHADER_READ, why, why_size);
	struct rwa_texture *qt = vid_texture(v, &v->dct_q, 23 /* R16Uint */, 8, 8, RWA_USAGE_SHADER_READ, why, why_size);

	if (in == NULL || rows == NULL || out == NULL || ct == NULL || qt == NULL)
		return RWA_VID_E_MEMORY;
	int rc = vid_write(in, width, height, src, src_stride, why, why_size);

	if (rc == RWA_VID_OK)
		rc = vid_write(ct, 64, 1, vid_dct_c, 256, why, why_size);
	if (rc == RWA_VID_OK)
		rc = vid_write(qt, 8, 8, qtable, 16, why, why_size);
	if (rc != RWA_VID_OK)
		return rc;

	void *fs_rows = vid_shader(v, &v->shader_dct_rows, rwa_vid_dct_rows_shader(v->device->nir_options), 54, why, why_size);
	void *fs_cols = vid_shader(v, &v->shader_dct_cols, rwa_vid_dct_cols_shader(v->device->nir_options), 24, why, why_size);

	if (fs_rows == NULL || fs_cols == NULL)
		return RWA_VID_E_GPU;
	struct vid_block block = { .w = (int32_t)width, .h = (int32_t)height };
	struct rwa_texture *t1[2] = { in, ct };

	rc = vid_pass(v, rows, fs_rows, t1, 2, &block, "dct rows", why, why_size);
	if (rc == RWA_VID_OK) {
		struct rwa_texture *t2[3] = { rows, ct, qt };

		rc = vid_pass(v, out, fs_cols, t2, 3, &block, "dct columns", why, why_size);
	}
	if (rc != RWA_VID_OK)
		return rc;
	/* The GPU writes one coefficient per pixel of an image-raster texture: (v, u) of
	 * block (bx, by) is at (bx + u, by + v).  n2vk.h promises the blocks' 64
	 * coefficients one after another, so the readback is repacked here.  The
	 * buffer is kept, not allocated per picture. */
	const size_t n = (size_t)width * height;

	if (v->dct_coef_n < n) {
		int16_t *tmp = realloc(v->dct_coef, n * sizeof(*tmp));

		if (tmp == NULL) {
			say(why, why_size, "out of memory for fdct's %zu coefficients", n);
			return RWA_VID_E_MEMORY;
		}
		v->dct_coef = tmp;
		v->dct_coef_n = n;
	}
	if (vid_read(out, width, height, v->dct_coef, width * 2, why, why_size) != RWA_VID_OK)
		return RWA_VID_E_GPU;
	for (uint32_t by = 0; by < height; by += 8)
		for (uint32_t bx = 0; bx < width; bx += 8) {
			int16_t *block = coef + ((size_t)(by / 8) * (width / 8) + bx / 8) * 64;

			for (uint32_t v = 0; v < 8; v++)
				for (uint32_t u = 0; u < 8; u++)
					block[v * 8 + u] = v->dct_coef[(size_t)(by + v) * width + bx + u];
		}
	v->device->pipe->flush(v->device->pipe, NULL, 0);
	return RWA_VID_OK;
}

/* ---- the n2vk backend --------------------------------------------------------------------------------------------------------- */

static const char vid_name[] = "rwa_vid (Mali NIR)";

static int
vid_backend_me16(void *ctx, const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad)
{
	char why[256];

	return rwa_vid_me16(ctx, a, mvx, mvy, sad, why, sizeof(why));
}

static int
vid_backend_half(void *ctx, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t w, uint32_t h)
{
	char why[256];

	return rwa_vid_half(ctx, src, src_stride, dst, dst_stride, w, h, why, sizeof(why));
}

static int
vid_backend_fdct(void *ctx, const uint8_t *src, uint32_t src_stride, uint32_t w, uint32_t h, const uint16_t q[64], int16_t *coef)
{
	char why[256];

	return rwa_vid_fdct8_quant(ctx, src, src_stride, w, h, q, coef, why, sizeof(why));
}

int
rwa_vid_backend(n2vk_backend *out, struct rwa_vid *v)
{
	if (out == NULL)
		return RWA_VID_E_ARGUMENT;
	if (v == NULL) {
		memset(out, 0, sizeof(*out));
		return RWA_VID_E_ARGUMENT;
	}
	out->size = sizeof(*out);
	out->name = vid_name;
	out->me16 = vid_backend_me16;
	out->half = vid_backend_half;
	out->fdct8_quant = vid_backend_fdct;
	out->ctx = v;
	return RWA_VID_OK;
}
