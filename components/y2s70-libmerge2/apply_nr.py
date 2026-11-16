#!/usr/bin/env python3
"""y2s70-libmerge2 = y2s67-libmerge1 + NR2: the camera ISP's noise reduction on the GPU (rwa_isp API version 2: struct rwa_isp_params
denoise / denoise_before, the bundle's rwcam_render.c denoise() bit for bit). Edits of ./src (a copy of y2s67-libmerge1/src); refuses twice.
  rwa_isp_api.h  RWA_ISP_API_VERSION 2; params denoise, denoise_before (a smaller struct of a version-1 caller is taken: no noise reduction)
  air_metal.c    device->isp.nr[...] (the noise passes' shaders)
  air_isp.c      the passes: the planes into the context's own textures, the colours smoothed once (twice at 2), the brightness at 2 (not
                 across an edge, 16), then for denoise_before the blocks' difference from the picture before (read back, one byte a block),
                 with_before()'s numbers on the processor (a few hundred blocks), and the picture drawn towards the one before where still;
                 the result into the surface and kept as the next picture before.
usage: python3 apply_nr.py"""
import os
H = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'src')


def edit(name, pairs):
    p = os.path.join(H, name)
    s = open(p).read()
    for a, b, *n in pairs:
        cnt = n[0] if n else 1
        assert s.count(a) == cnt, (name, s.count(a), a[:100])
        s = s.replace(a, b)
    open(p, 'w').write(s)


assert 'isp_nr_smooth_shader' not in open(os.path.join(H, 'air_isp.c')).read(), 'already applied'
edit('rwa_isp_api.h', [
    ('#define RWA_ISP_API_VERSION     1', '#define RWA_ISP_API_VERSION     2       /* 2: struct rwa_isp_params has denoise and denoise_before (y2s70 NR2) */'),
    ('''	const uint8_t   *gamma;         /* isp->gamma: 4096 bytes; read during the call, uploaded again only when its bytes change */
};''', '''	const uint8_t   *gamma;         /* isp->gamma: 4096 bytes; read during the call, uploaded again only when its bytes change */
	/*
	 * API version 2 (a caller built for version 1 gives a `size` that ends at `gamma`: no noise reduction, as before).
	 * denoise: 0 none; 1, 2: rwcam_render.c's levels (gain 3 and more, 8 and more), done on the GPU after the picture is made, bit for
	 * bit as the processor does it: the colour planes smoothed (once, twice), at 2 the brightness smoothed away from edges.
	 * denoise_before: not 0: the picture is also drawn towards the one this context made before it, by 8 x 8 blocks that are still
	 * (rwcam_render.c with_before()); the picture before is kept by the context as a texture and never read back -- what IS read back,
	 * in the call, is one byte for each block (how much it differs), so such a render waits for the GPU once even with
	 * RWA_ISP_OUT_ASYNC. 0: nothing is kept (a still). The context forgets the picture before whenever a render does not ask for this,
	 * or the picture's size changes.
	 */
	uint32_t        denoise;
	uint32_t        denoise_before;
};
#define RWA_ISP_PARAMS_SIZE_1   ((uint32_t)offsetof(struct rwa_isp_params, denoise))'''),
])
edit('air_metal.c', [('      void *stats[2];           /* y2s65: the frame\'s statistics, per frame format (air_isp.c isp_stats_shader) */',
                      '      void *stats[2];           /* y2s65: the frame\'s statistics, per frame format (air_isp.c isp_stats_shader) */\n'
                      '      void *nr[7];              /* y2s70 NR2: smooth luma, smooth chroma, block difference, apply luma, apply chroma, copy luma, copy chroma */')])

SHADERS = r'''
/*
 * y2s70 NR2: THE NOISE (rwcam_render.c denoise(), smooth(), with_before()) on the GPU, every number the processor's. The planes are 8-bit
 * textures (R8Unorm brightness, RG8Unorm colours: one texel = the Cb, Cr pair, so the processor's `step' of 2 bytes is the next texel); a
 * texel is read as the float an 8-bit attachment holds and turned back into its byte exactly. Block words: q[0..4] per shader (below);
 * out_w, out_h the size of what the pass writes.
 */
static nir_def *
isp_nr_byte(nir_builder *b, nir_def *v)
{
   return nir_f2i32(b, nir_fround_even(b, nir_fmul_imm(b, v, 255.0)));
}

static nir_def *
isp_nr_unorm(nir_builder *b, nir_def *i)
{
   return nir_fmul_imm(b, nir_i2f32(b, i), 1.0 / 255.0);
}

/* smooth_band(): 1 2 1 each way from the neighbours as they were; the first and last row and column as they are; q[0] near (0: always) */
static nir_shader *
isp_nr_smooth_shader(const struct nir_shader_compiler_options *options, unsigned channels)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, channels == 1 ? "isp_nr_smooth_y" : "isp_nr_smooth_c", &uv, &out);
   nir_def *x, *y;

   isp_pixel_xy(&b, uv, &x, &y);
   nir_def *w = isp_word(&b, ISP_U_OUT_W), *h = isp_word(&b, ISP_U_OUT_H), *near = isp_word(&b, ISP_U_Q);
   nir_def *x1 = nir_iadd_imm(&b, w, -1), *y1 = nir_iadd_imm(&b, h, -1);
   nir_def *inner = nir_iand(&b, nir_iand(&b, nir_ige_imm(&b, x, 1), nir_ilt(&b, x, x1)), nir_iand(&b, nir_ige_imm(&b, y, 1), nir_ilt(&b, y, y1)));
   nir_def *xl = nir_imax(&b, nir_iadd_imm(&b, x, -1), nir_imm_int(&b, 0)), *xr = nir_imin(&b, nir_iadd_imm(&b, x, 1), x1);
   nir_def *ya = nir_imax(&b, nir_iadd_imm(&b, y, -1), nir_imm_int(&b, 0)), *yb = nir_imin(&b, nir_iadd_imm(&b, y, 1), y1);
   nir_def *t_here = isp_fetch(&b, 0, x, y, nir_type_float32), *t_l = isp_fetch(&b, 0, xl, y, nir_type_float32);
   nir_def *t_r = isp_fetch(&b, 0, xr, y, nir_type_float32), *t_a = isp_fetch(&b, 0, x, ya, nir_type_float32);
   nir_def *t_b = isp_fetch(&b, 0, x, yb, nir_type_float32);
   nir_def *o[4] = { nir_imm_float(&b, 0.0f), nir_imm_float(&b, 0.0f), nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f) };

   for (unsigned c = 0; c < channels; c++) {
      nir_def *hh = isp_nr_byte(&b, nir_channel(&b, t_here, c));
      nir_def *v = nir_ishr_imm(&b, nir_iadd_imm(&b, nir_iadd(&b, nir_iadd(&b, isp_nr_byte(&b, nir_channel(&b, t_a, c)), isp_nr_byte(&b, nir_channel(&b, t_b, c))),
                                nir_iadd(&b, nir_iadd(&b, isp_nr_byte(&b, nir_channel(&b, t_l, c)), isp_nr_byte(&b, nir_channel(&b, t_r, c))), nir_ishl_imm(&b, hh, 2))), 4), 3);
      nir_def *close = nir_ior(&b, nir_ige(&b, nir_imm_int(&b, 0), near),
                               nir_iand(&b, nir_ilt(&b, nir_isub(&b, v, hh), near), nir_ilt(&b, nir_isub(&b, hh, v), near)));

      o[c] = isp_nr_unorm(&b, nir_bcsel(&b, nir_iand(&b, inner, close), v, hh));
   }
   nir_store_var(&b, out, nir_vec4(&b, o[0], o[1], o[2], o[3]), 0xf);
   return isp_fragment_end(&b, 1);
}

/* with_before()'s first loop: each 8 x 8 block's mean |difference| over every other pixel, in quarters (sum * 4 / count), at most 255.
 * Textures: 0 the brightness now, 1 the brightness before. q[0], q[1]: the picture's width and height. */
static nir_shader *
isp_nr_diff_shader(const struct nir_shader_compiler_options *options)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_nr_diff", &uv, &out);
   nir_def *x, *y;

   isp_pixel_xy(&b, uv, &x, &y);
   nir_def *w = isp_word(&b, ISP_U_Q), *h = isp_word(&b, ISP_U_Q + 1);
   nir_def *sum = nir_imm_int(&b, 0), *count = nir_imm_int(&b, 0);

   for (unsigned yy = 0; yy < 8; yy += 2) {
      for (unsigned xx = 0; xx < 8; xx += 2) {
         nir_def *px = nir_iadd_imm(&b, nir_ishl_imm(&b, x, 3), xx), *py = nir_iadd_imm(&b, nir_ishl_imm(&b, y, 3), yy);
         nir_def *ok = nir_iand(&b, nir_ilt(&b, px, w), nir_ilt(&b, py, h));
         nir_def *pxc = nir_imin(&b, px, nir_iadd_imm(&b, w, -1)), *pyc = nir_imin(&b, py, nir_iadd_imm(&b, h, -1));
         nir_def *d = nir_iabs(&b, nir_isub(&b, isp_nr_byte(&b, nir_channel(&b, isp_fetch(&b, 0, pxc, pyc, nir_type_float32), 0)),
                                         isp_nr_byte(&b, nir_channel(&b, isp_fetch(&b, 1, pxc, pyc, nir_type_float32), 0))));

         sum = nir_iadd(&b, sum, nir_bcsel(&b, ok, d, nir_imm_int(&b, 0)));
         count = nir_iadd(&b, count, nir_b2i32(&b, ok));
      }
   }
   nir_def *q = nir_bcsel(&b, nir_ilt_imm(&b, count, 1), nir_imm_int(&b, 0), nir_udiv(&b, nir_ishl_imm(&b, sum, 2), nir_imax(&b, count, nir_imm_int(&b, 1))));

   q = nir_imin(&b, q, nir_imm_int(&b, 255));
   nir_store_var(&b, out, nir_vec4(&b, isp_nr_unorm(&b, q), nir_imm_float(&b, 0.0f), nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f)), 0xf);
   return isp_fragment_end(&b, 2);
}

/* before_piece(): a block that is still (not all moving, no moving block among its nine) draws each value `keep' of 256 of the way to the one
 * before where they differ by less than `near'. Textures: 0 now, 1 before, 2 the blocks' moves (R8: 0 or 1). q[0] near, q[1] keep,
 * q[2] all moving, q[3], q[4] the blocks across and down; chroma: a texel's block is (x / 4, y / 4), the brightness's (x / 8, y / 8). */
static nir_shader *
isp_nr_apply_shader(const struct nir_shader_compiler_options *options, unsigned channels)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, channels == 1 ? "isp_nr_apply_y" : "isp_nr_apply_c", &uv, &out);
   nir_def *x, *y;

   isp_pixel_xy(&b, uv, &x, &y);
   nir_def *near = isp_word(&b, ISP_U_Q), *keep = isp_word(&b, ISP_U_Q + 1), *all = isp_word(&b, ISP_U_Q + 2);
   nir_def *bw = isp_word(&b, ISP_U_Q + 3), *bh = isp_word(&b, ISP_U_Q + 4);
   unsigned sh = channels == 1 ? 3 : 2;
   nir_def *bx = nir_ushr_imm(&b, x, sh), *by = nir_ushr_imm(&b, y, sh);
   nir_def *still = nir_ieq_imm(&b, all, 0);

   for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
         nir_def *nx = nir_iadd_imm(&b, bx, dx), *ny = nir_iadd_imm(&b, by, dy);
         nir_def *in = nir_iand(&b, nir_iand(&b, nir_ige_imm(&b, nx, 0), nir_ilt(&b, nx, bw)), nir_iand(&b, nir_ige_imm(&b, ny, 0), nir_ilt(&b, ny, bh)));
         nir_def *nxc = nir_imin(&b, nir_imax(&b, nx, nir_imm_int(&b, 0)), nir_iadd_imm(&b, bw, -1));
         nir_def *nyc = nir_imin(&b, nir_imax(&b, ny, nir_imm_int(&b, 0)), nir_iadd_imm(&b, bh, -1));
         nir_def *m = nir_ine_imm(&b, isp_nr_byte(&b, nir_channel(&b, isp_fetch(&b, 2, nxc, nyc, nir_type_float32), 0)), 0);

         still = nir_iand(&b, still, nir_inot(&b, nir_iand(&b, in, m)));
      }
   }
   nir_def *now = isp_fetch(&b, 0, x, y, nir_type_float32), *was = isp_fetch(&b, 1, x, y, nir_type_float32);
   nir_def *o[4] = { nir_imm_float(&b, 0.0f), nir_imm_float(&b, 0.0f), nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f) };

   for (unsigned c = 0; c < channels; c++) {
      nir_def *n = isp_nr_byte(&b, nir_channel(&b, now, c)), *d = nir_isub(&b, n, isp_nr_byte(&b, nir_channel(&b, was, c)));
      nir_def *take = nir_iand(&b, still, nir_ilt(&b, nir_iabs(&b, d), near));

      o[c] = isp_nr_unorm(&b, nir_bcsel(&b, take, nir_isub(&b, n, nir_ishr_imm(&b, nir_imul(&b, d, keep), 8)), n));
   }
   nir_store_var(&b, out, nir_vec4(&b, o[0], o[1], o[2], o[3]), 0xf);
   return isp_fragment_end(&b, 3);
}

/* a plane as it is (into the surface, or kept as the picture before) */
static nir_shader *
isp_nr_copy_shader(const struct nir_shader_compiler_options *options, unsigned channels)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, channels == 1 ? "isp_nr_copy_y" : "isp_nr_copy_c", &uv, &out);
   nir_def *x, *y;

   isp_pixel_xy(&b, uv, &x, &y);
   nir_def *t = isp_fetch(&b, 0, x, y, nir_type_float32);

   nir_store_var(&b, out, nir_vec4(&b, nir_channel(&b, t, 0), channels > 1 ? nir_channel(&b, t, 1) : nir_imm_float(&b, 0.0f),
                                   nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f)), 0xf);
   return isp_fragment_end(&b, 1);
}

'''

edit('air_isp.c', [
    ('''   struct rwa_texture *columns, *rows, *gamma, *rgb;''', '''   struct rwa_texture *columns, *rows, *gamma, *rgb;
   /* y2s70 NR2: the planes as made (y0, c0), smoothed (y1, c1, c2), the picture before (by, bc: valid while nr_before) and the next one
    * (by2, bc2), the blocks' difference and moves */
   struct rwa_texture *nr_y0, *nr_y1, *nr_c0, *nr_c1, *nr_c2, *nr_by, *nr_bc, *nr_by2, *nr_bc2, *nr_diff, *nr_moves;
   bool nr_before;'''),
    ('''/* y2s65: camera_isp.c's isp_measure() choice of step''', SHADERS + '''
/* y2s65: camera_isp.c's isp_measure() choice of step'''),
    ('''   void *fs[8] = { device->isp.cells[0], device->isp.cells[1], device->isp.full[0], device->isp.full[1], device->isp.luma,
                   device->isp.chroma, device->isp.stats[0], device->isp.stats[1] };''', '''   void *fs[15] = { device->isp.cells[0], device->isp.cells[1], device->isp.full[0], device->isp.full[1], device->isp.luma,
                    device->isp.chroma, device->isp.stats[0], device->isp.stats[1], device->isp.nr[0], device->isp.nr[1], device->isp.nr[2],
                    device->isp.nr[3], device->isp.nr[4], device->isp.nr[5], device->isp.nr[6] };'''),
    ('''   for (unsigned i = 0; i < 8; i++)
      any = any || fs[i] != NULL;''', '''   for (unsigned i = 0; i < 15; i++)
      any = any || fs[i] != NULL;'''),
    ('''   for (unsigned i = 0; i < 8; i++)
      if (fs[i])''', '''   for (unsigned i = 0; i < 15; i++)
      if (fs[i])'''),
    ('p->size < sizeof(*p)', 'p->size < RWA_ISP_PARAMS_SIZE_1'),
    ('params->size < sizeof(*params)', 'params->size < RWA_ISP_PARAMS_SIZE_1'),
    # the passes
    ('''       isp_pass(device, luma, fs_luma, rgb_texture, 1, &block, "luma", why, why_size) != 0 ||
       isp_pass(device, chroma, fs_chroma, rgb_texture, 1, &block, "chroma", why, why_size) != 0)
      return RWA_ISP_E_GPU;''', '''       isp_nr_planes(isp, p, luma, chroma, fs_luma, fs_chroma, rgb_texture, &block, width, height, why, why_size) != 0)
      return RWA_ISP_E_GPU;'''),
    ('''   struct rwa_texture *own[6] = { isp->raw_own, isp->columns, isp->rows, isp->gamma, isp->rgb, isp->stats };

   for (int i = 0; i < 6; i++)''', '''   struct rwa_texture *own[17] = { isp->raw_own, isp->columns, isp->rows, isp->gamma, isp->rgb, isp->stats, isp->nr_y0, isp->nr_y1, isp->nr_c0,
                                   isp->nr_c1, isp->nr_c2, isp->nr_by, isp->nr_bc, isp->nr_by2, isp->nr_bc2, isp->nr_diff, isp->nr_moves };

   for (int i = 0; i < 17; i++)'''),
])

PLANES = r'''
enum { ISP_NR_SMOOTH_Y, ISP_NR_SMOOTH_C, ISP_NR_DIFF, ISP_NR_APPLY_Y, ISP_NR_APPLY_C, ISP_NR_COPY_Y, ISP_NR_COPY_C };

static void *
isp_nr_fs(struct rwa_device *device, unsigned which, char *why, size_t why_size)
{
   if (device->isp.nr[which] != NULL)
      return device->isp.nr[which];
   nir_shader *nir = which == ISP_NR_SMOOTH_Y ? isp_nr_smooth_shader(device->nir_options, 1)
                   : which == ISP_NR_SMOOTH_C ? isp_nr_smooth_shader(device->nir_options, 2)
                   : which == ISP_NR_DIFF ? isp_nr_diff_shader(device->nir_options)
                   : which == ISP_NR_APPLY_Y ? isp_nr_apply_shader(device->nir_options, 1)
                   : which == ISP_NR_APPLY_C ? isp_nr_apply_shader(device->nir_options, 2)
                   : which == ISP_NR_COPY_Y ? isp_nr_copy_shader(device->nir_options, 1) : isp_nr_copy_shader(device->nir_options, 2);

   return isp_shader(device, &device->isp.nr[which], nir, why, why_size);
}

/* a texture of the context's own, (re)made at this size and format */
static struct rwa_texture *
isp_nr_texture(struct rwa_device *device, struct rwa_texture **t, unsigned format, uint32_t w, uint32_t h, char *why, size_t why_size)
{
   if (*t != NULL && ((*t)->res->width0 != w || (*t)->res->height0 != h)) {
      rwa_texture_destroy(*t);
      *t = NULL;
   }
   if (*t == NULL)
      *t = isp_texture(device, format, w, h, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, why_size);
   return *t;
}

/* one noise pass: `fs' over `dst' from up to three textures, with the block's words q[0..4] and out = dst's size. lock held. */
static int
isp_nr_pass(struct rwa_device *device, struct rwa_texture *dst, void *fs, struct rwa_texture *t0, struct rwa_texture *t1, struct rwa_texture *t2,
            const struct isp_block *base, const int32_t q[5], const char *what, char *why, size_t why_size)
{
   struct isp_block nb = *base;
   struct rwa_texture *const tex[3] = { t0, t1, t2 };

   for (int i = 0; i < 5; i++)
      nb.q[i] = q[i];
   nb.out_w = (int32_t)dst->res->width0;
   nb.out_h = (int32_t)dst->res->height0;
   return isp_pass(device, dst, fs, tex, t2 != NULL ? 3 : t1 != NULL ? 2 : 1, &nb, what, why, why_size);
}

/*
 * y2s70 NR2: the luma and chroma passes, and the noise taken out (struct rwa_isp_params denoise, denoise_before) -- rwcam_render.c denoise():
 * the colours smoothed once (twice at 2), the brightness at 2 (near 16), then with the picture before (with_before()). Without noise
 * reduction the two passes write the surface's planes as before. lock held.
 */
static int
isp_nr_planes(struct rwa_isp *isp, const struct rwa_isp_params *p, struct rwa_texture *luma, struct rwa_texture *chroma, void *fs_luma,
              void *fs_chroma, struct rwa_texture *const *rgb_texture, const struct isp_block *block, uint32_t width, uint32_t height,
              char *why, size_t why_size)
{
   struct rwa_device *device = isp->device;
   uint32_t level = p->size >= sizeof(*p) ? p->denoise : 0, before = p->size >= sizeof(*p) ? p->denoise_before : 0;
   const int32_t z[5] = { 0, 0, 0, 0, 0 };

   level = level > 2 ? 2 : level;
   if (level == 0 || width < 8 || height < 8) {
      isp->nr_before = false;                  /* (the picture before is not this one's any more) */
      return isp_pass(device, luma, fs_luma, rgb_texture, 1, block, "luma", why, why_size) != 0 ||
             isp_pass(device, chroma, fs_chroma, rgb_texture, 1, block, "chroma", why, why_size) != 0 ? -1 : 0;
   }
   const uint32_t cw = (width + 1) / 2, ch = (height + 1) / 2;
   void *f_sy = isp_nr_fs(device, ISP_NR_SMOOTH_Y, why, why_size), *f_sc = isp_nr_fs(device, ISP_NR_SMOOTH_C, why, why_size);
   void *f_cy = isp_nr_fs(device, ISP_NR_COPY_Y, why, why_size), *f_cc = isp_nr_fs(device, ISP_NR_COPY_C, why, why_size);
   struct rwa_texture *y0 = isp_nr_texture(device, &isp->nr_y0, 10 /* R8Unorm */, width, height, why, why_size);
   struct rwa_texture *c0 = isp_nr_texture(device, &isp->nr_c0, 30 /* RG8Unorm */, cw, ch, why, why_size);
   struct rwa_texture *c1 = isp_nr_texture(device, &isp->nr_c1, 30, cw, ch, why, why_size);

   if (f_sy == NULL || f_sc == NULL || f_cy == NULL || f_cc == NULL || y0 == NULL || c0 == NULL || c1 == NULL)
      return -1;
   if (isp_pass(device, y0, fs_luma, rgb_texture, 1, block, "luma", why, why_size) != 0 ||
       isp_pass(device, c0, fs_chroma, rgb_texture, 1, block, "chroma", why, why_size) != 0)
      return -1;
   /* (no picture before, or another size: this picture as made is its own picture before -- denoise()'s `fresh') */
   bool temporal = before != 0;
   if (isp->nr_by != NULL && (isp->nr_by->res->width0 != width || isp->nr_by->res->height0 != height))
      isp->nr_before = false;                  /* (another size: the picture before is not this one's) */
   if (temporal) {
      if (isp_nr_texture(device, &isp->nr_by, 10, width, height, why, why_size) == NULL || isp_nr_texture(device, &isp->nr_bc, 30, cw, ch, why, why_size) == NULL ||
          isp_nr_texture(device, &isp->nr_by2, 10, width, height, why, why_size) == NULL || isp_nr_texture(device, &isp->nr_bc2, 30, cw, ch, why, why_size) == NULL)
         return -1;
      if (!isp->nr_before) {
         if (isp_nr_pass(device, isp->nr_by, f_cy, y0, NULL, NULL, block, z, "noise: before (brightness)", why, why_size) != 0 ||
             isp_nr_pass(device, isp->nr_bc, f_cc, c0, NULL, NULL, block, z, "noise: before (colours)", why, why_size) != 0)
            return -1;
      }
   } else {
      isp->nr_before = false;
   }
   /* the colours, once or twice; the brightness at 2 */
   struct rwa_texture *yc = y0, *cc = c1;
   if (isp_nr_pass(device, c1, f_sc, c0, NULL, NULL, block, z, "noise: colours", why, why_size) != 0)
      return -1;
   if (level == 2) {
      const int32_t near16[5] = { 16, 0, 0, 0, 0 };
      struct rwa_texture *c2 = isp_nr_texture(device, &isp->nr_c2, 30, cw, ch, why, why_size);
      struct rwa_texture *y1 = isp_nr_texture(device, &isp->nr_y1, 10, width, height, why, why_size);

      if (c2 == NULL || y1 == NULL || isp_nr_pass(device, c2, f_sc, c1, NULL, NULL, block, z, "noise: colours again", why, why_size) != 0 ||
          isp_nr_pass(device, y1, f_sy, y0, NULL, NULL, block, near16, "noise: brightness", why, why_size) != 0)
         return -1;
      cc = c2;
      yc = y1;
   }
   if (!temporal) {
      return isp_nr_pass(device, luma, f_cy, yc, NULL, NULL, block, z, "noise: out (brightness)", why, why_size) != 0 ||
             isp_nr_pass(device, chroma, f_cc, cc, NULL, NULL, block, z, "noise: out (colours)", why, why_size) != 0 ? -1 : 0;
   }
   /* with_before(): the blocks' difference, read back; its numbers here; the moves to the GPU; the picture drawn to the one before */
   const uint32_t bw = (width + 7) / 8, bh = (height + 7) / 8, n = bw * bh;
   void *f_d = isp_nr_fs(device, ISP_NR_DIFF, why, why_size), *f_ay = isp_nr_fs(device, ISP_NR_APPLY_Y, why, why_size);
   void *f_ac = isp_nr_fs(device, ISP_NR_APPLY_C, why, why_size);
   struct rwa_texture *dt = isp_nr_texture(device, &isp->nr_diff, 10 /* R8Unorm */, bw, bh, why, why_size);
   struct rwa_texture *mt = isp_nr_texture(device, &isp->nr_moves, 10, bw, bh, why, why_size);
   uint8_t *diff = malloc(2 * (size_t)n), *moves = diff != NULL ? diff + n : NULL;
   const int32_t dq[5] = { (int32_t)width, (int32_t)height, 0, 0, 0 };

   if (f_d == NULL || f_ay == NULL || f_ac == NULL || dt == NULL || mt == NULL || diff == NULL ||
       isp_nr_pass(device, dt, f_d, yc, isp->nr_by, NULL, block, dq, "noise: blocks", why, why_size) != 0) {
      free(diff);
      return -1;
   }
   const unsigned origin[3] = { 0, 0, 0 }, size[3] = { bw, bh, 1 };
   if (rwa_texture_read(dt, 0, 0, origin, size, diff, bw, why, why_size) != 0) {
      free(diff);
      return -1;
   }
   uint64_t all = 0, calm = 0, calm_n = 0;
   uint32_t moving = 0;
   int near_y = level == 2 ? 18 : 10, keep_y = level == 2 ? 128 : 90, near_c = level == 2 ? 14 : 8, keep_c = level == 2 ? 140 : 102, limit;

   for (uint32_t b = 0; b < n; b++)
      all += diff[b];
   for (uint32_t b = 0; b < n; b++) {
      if ((uint64_t)diff[b] * n <= 2 * all) {
         calm += diff[b];
         calm_n++;
      }
   }
   limit = (int)(1.7 * (calm_n ? (double)calm / (double)calm_n : 0) + 0.5);
   limit = limit < 10 ? 10 : limit > 48 ? 48 : limit;
   {
      int noise3 = (int)(3.0 * (calm_n ? (double)calm / (double)calm_n : 0) / 4 + 0.5);

      noise3 = noise3 > 40 ? 40 : noise3;
      near_y = noise3 > near_y ? noise3 : near_y;
      near_c = noise3 * 3 / 4 > near_c ? noise3 * 3 / 4 : near_c;
   }
   for (uint32_t b = 0; b < n; b++) {
      moves[b] = diff[b] > limit ? 1 : 0;
      moving += moves[b];
   }
   int error = rwa_texture_write(mt, 0, 0, origin, size, moves, bw, why, why_size);
   free(diff);
   if (error != 0)
      return -1;
   const int32_t all_moving = (double)moving > 0.35 * n ? 1 : 0;
   const int32_t qy[5] = { near_y, keep_y, all_moving, (int32_t)bw, (int32_t)bh }, qc[5] = { near_c, keep_c, all_moving, (int32_t)bw, (int32_t)bh };

   if (isp_nr_pass(device, luma, f_ay, yc, isp->nr_by, mt, block, qy, "noise: with the picture before (brightness)", why, why_size) != 0 ||
       isp_nr_pass(device, isp->nr_by2, f_ay, yc, isp->nr_by, mt, block, qy, "noise: the next picture before (brightness)", why, why_size) != 0 ||
       isp_nr_pass(device, chroma, f_ac, cc, isp->nr_bc, mt, block, qc, "noise: with the picture before (colours)", why, why_size) != 0 ||
       isp_nr_pass(device, isp->nr_bc2, f_ac, cc, isp->nr_bc, mt, block, qc, "noise: the next picture before (colours)", why, why_size) != 0)
      return -1;
   struct rwa_texture *t = isp->nr_by;
   isp->nr_by = isp->nr_by2;
   isp->nr_by2 = t;
   t = isp->nr_bc;
   isp->nr_bc = isp->nr_bc2;
   isp->nr_bc2 = t;
   isp->nr_before = true;
   return 0;
}
'''
edit('air_isp.c', [('''/* the frame as texture 0. *zero_copy''', PLANES + '''
/* the frame as texture 0. *zero_copy''')])
print('NR2 edits applied')
