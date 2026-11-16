#!/usr/bin/env python3
"""y2s65-cam-stats: the camera ISP's frame statistics on the GPU, as edits of a copy of y2s63-vt23/src (applied once to ./src; refuses twice).
(First built as y2s64-vt24 at 11:05, libredwoodair f1b4fd9f...; that directory was removed by another agent: this script is the record.)
  air_metal.c     device->isp.stats[2] (the statistics shaders, per frame format)
  rwa_isp_api.h   struct rwa_isp_output: stats, stats_floats; struct rwa_isp_result: stats_w, stats_h, stats_step (API version stays 1:
                  smaller structs of earlier callers are taken as before)
  air_isp.c       isp_stats_shader + one more pass in isp_render_locked + the read-back in isp_finish; rwa_isp_want_stats (tests)
usage: python3 apply_camstats.py   (from this directory)"""
import os, sys
H = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'src')


def edit(name, pairs):
    p = os.path.join(H, name)
    s = open(p).read()
    for a, b, *n in pairs:
        cnt = n[0] if n else 1
        assert s.count(a) == cnt, (name, s.count(a), a[:90])
        s = s.replace(a, b)
    open(p, 'w').write(s)


assert 'isp_stats_shader' not in open(os.path.join(H, 'air_isp.c')).read(), 'already applied'
edit('air_metal.c', [('      void *cells[2], *full[2], *luma, *chroma, *nearest;',
                      '      void *cells[2], *full[2], *luma, *chroma, *nearest;\n'
                      '      void *stats[2];           /* y2s65: the frame\'s statistics, per frame format (air_isp.c isp_stats_shader) */')])
edit('rwa_isp_api.h', [('''	uint32_t        width, height;  /* the picture: the surface's own size */
};''', '''	uint32_t        width, height;  /* the picture: the surface's own size */
	/*
	 * y2s65 (still API version 1; a caller's smaller struct, ending here, is taken as before): the frame's statistics, made in the same
	 * render by one more pass and read back after its wait (not with RWA_ISP_OUT_ASYNC). stats: room for stats_floats floats, NULL: none.
	 * The library says what it wrote in result->stats_w / stats_h (0: nothing -- an older library leaves them as the caller set them).
	 * The layout: blocks of RWA_ISP_STATS_BLOCK x RWA_ISP_STATS_BLOCK of the cells camera_isp.c's isp_measure() looks at (every
	 * step-th 2x2 cell, step as it chooses for the frame), stats_w x stats_h of them, row by row; for each RWA_ISP_STATS_KINDS x 4 floats:
	 *   kind 0      R, G, B sums (above black) and the number of cells      -- isp_stats.mean
	 *   kind 1      the same of the 'fair' cells (neither near black nor clipped) -- isp_stats.fair
	 *   kind 2      the same of the 'grey' ones among them                   -- isp_stats.grey
	 *   kinds 3-18  the green histogram's 64 bins of 16 steps, four a kind   -- isp_stats.hist (and from it bright)
	 *   kind 19     sharpness: the sum of (right - here)^2 + (below - here)^2 of the cells' brightness, the sum of here, the count
	 * Summed over all blocks the first four are isp_measure()'s numbers exactly (whole numbers, exact in floats).
	 */
	float           *stats;
	uint32_t        stats_floats;
};
#define RWA_ISP_OUTPUT_SIZE_1   ((uint32_t)offsetof(struct rwa_isp_output, stats))
#define RWA_ISP_STATS_BLOCK     8
#define RWA_ISP_STATS_KINDS     20'''),
                       ('''	uint64_t        wait_ns;        /* waiting for the GPU to finish them (0 with RWA_ISP_OUT_ASYNC) */
};''', '''	uint64_t        wait_ns;        /* waiting for the GPU to finish them (0 with RWA_ISP_OUT_ASYNC) */
	uint32_t        stats_w, stats_h;       /* y2s65: the statistics written (blocks across, down); 0: none */
	uint32_t        stats_step;             /* y2s65: the cells' step the blocks are of */
};
#define RWA_ISP_RESULT_SIZE_1   ((uint32_t)offsetof(struct rwa_isp_result, stats_w))''')])

SHADER = r'''
/*
 * y2s65: THE FRAME'S STATISTICS (rwa_isp_api.h, struct rwa_isp_output's stats), camera_isp.c's isp_measure() on the GPU: a texture of
 * stats_w * RWA_ISP_STATS_KINDS x stats_h RGBA32F texels; texel (bx * KINDS + k, by) is kind k of block (bx, by), the block being
 * RWA_ISP_STATS_BLOCK x RWA_ISP_STATS_BLOCK of the cells isp_measure() looks at (cell (si * step, sj * step), si < ceil(cells_w / step)).
 * Every fragment walks its block's 64 cells (unrolled) with isp_measure()'s whole numbers and keeps what its kind sums. Words of the
 * block: q[0] step, q[1] cells_w, q[2] cells_h, q[3] fair_top (white * 85 / 100), q[4] the blocks across; black, r_place, b_place as ever.
 */
static nir_def *
isp_cell_value(nir_builder *b, unsigned format, nir_def *cx, nir_def *cy, unsigned n, nir_def *black)
{
   nir_def *px = nir_iadd_imm(b, nir_ishl_imm(b, cx, 1), n & 1), *py = nir_iadd_imm(b, nir_ishl_imm(b, cy, 1), n >> 1);

   return nir_imax(b, nir_isub(b, isp_raw(b, format, px, py), black), nir_imm_int(b, 0));
}

static nir_shader *
isp_stats_shader(const struct nir_shader_compiler_options *options, unsigned format)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_stats", &uv, &out);
   nir_def *x, *y;

   isp_pixel_xy(&b, uv, &x, &y);
   nir_def *step = isp_word(&b, ISP_U_Q), *cw = isp_word(&b, ISP_U_Q + 1), *ch = isp_word(&b, ISP_U_Q + 2);
   nir_def *ft = isp_word(&b, ISP_U_Q + 3), *black = isp_word(&b, ISP_U_BLACK);
   nir_def *rp = isp_word(&b, ISP_U_R_PLACE), *bp = isp_word(&b, ISP_U_B_PLACE);
   nir_def *bx = nir_udiv_imm(&b, x, RWA_ISP_STATS_KINDS), *k = nir_isub(&b, x, nir_imul_imm(&b, bx, RWA_ISP_STATS_KINDS)), *by = y;
   nir_def *base = nir_imul_imm(&b, nir_iadd_imm(&b, k, -3), 4);           /* the first histogram bin of kinds 3..18 */
   nir_def *zero = nir_imm_float(&b, 0.0f);
   nir_def *acc[4] = { zero, zero, zero, zero };
   nir_def *cw1 = nir_iadd_imm(&b, cw, -1), *ch1 = nir_iadd_imm(&b, ch, -1);
   nir_def *is_hist = nir_iand(&b, nir_ige_imm(&b, k, 3), nir_ilt_imm(&b, k, 19)), *is_edge = nir_ieq_imm(&b, k, 19);

   for (unsigned j = 0; j < RWA_ISP_STATS_BLOCK; j++) {
      for (unsigned i = 0; i < RWA_ISP_STATS_BLOCK; i++) {
         nir_def *cx = nir_imul(&b, nir_iadd_imm(&b, nir_imul_imm(&b, bx, RWA_ISP_STATS_BLOCK), i), step);
         nir_def *cy = nir_imul(&b, nir_iadd_imm(&b, nir_imul_imm(&b, by, RWA_ISP_STATS_BLOCK), j), step);
         nir_def *valid = nir_iand(&b, nir_ilt(&b, cx, cw), nir_ilt(&b, cy, ch));
         nir_def *cxc = nir_imin(&b, cx, cw1), *cyc = nir_imin(&b, cy, ch1), *v[4];

         for (unsigned n = 0; n < 4; n++)
            v[n] = isp_cell_value(&b, format, cxc, cyc, n, black);
         nir_def *all = nir_iadd(&b, nir_iadd(&b, v[0], v[1]), nir_iadd(&b, v[2], v[3]));
         nir_def *vr = nir_bcsel(&b, nir_ieq_imm(&b, rp, 0), v[0], nir_bcsel(&b, nir_ieq_imm(&b, rp, 1), v[1],
                                 nir_bcsel(&b, nir_ieq_imm(&b, rp, 2), v[2], v[3])));
         nir_def *vb = nir_bcsel(&b, nir_ieq_imm(&b, bp, 0), v[0], nir_bcsel(&b, nir_ieq_imm(&b, bp, 1), v[1],
                                 nir_bcsel(&b, nir_ieq_imm(&b, bp, 2), v[2], v[3])));
         nir_def *g = nir_ushr_imm(&b, nir_isub(&b, nir_isub(&b, all, vr), vb), 1);
         nir_def *fair = nir_iand(&b, valid, nir_iand(&b, nir_ige_imm(&b, g, 8), nir_iand(&b, nir_ilt(&b, vr, ft),
                                  nir_iand(&b, nir_ilt(&b, g, ft), nir_ilt(&b, vb, ft)))));
         nir_def *g10 = nir_imul_imm(&b, g, 10);
         nir_def *grey = nir_iand(&b, fair, nir_iand(&b, nir_ige(&b, g, vr), nir_iand(&b, nir_ige(&b, nir_imul_imm(&b, vr, 28), g10),
                                  nir_iand(&b, nir_ige(&b, g10, nir_imul_imm(&b, vb, 11)), nir_ige(&b, nir_imul_imm(&b, vb, 36), g10)))));
         nir_def *bin = nir_imin(&b, nir_ushr_imm(&b, g, 4), nir_imm_int(&b, 63));
         /* sharpness: this cell and its right and lower neighbours (isp_sharpness), where they are in the frame */
         nir_def *edge_ok = nir_iand(&b, valid, nir_iand(&b, nir_ilt(&b, nir_iadd_imm(&b, cx, 1), cw), nir_ilt(&b, nir_iadd_imm(&b, cy, 1), ch)));
         nir_def *cxr = nir_imin(&b, nir_iadd_imm(&b, cxc, 1), cw1), *cyb = nir_imin(&b, nir_iadd_imm(&b, cyc, 1), ch1);
         nir_def *r4 = nir_imm_int(&b, 0), *b4 = nir_imm_int(&b, 0);

         for (unsigned n = 0; n < 4; n++) {
            r4 = nir_iadd(&b, r4, isp_cell_value(&b, format, cxr, cyc, n, black));
            b4 = nir_iadd(&b, b4, isp_cell_value(&b, format, cxc, cyb, n, black));
         }
         nir_def *here = nir_i2f32(&b, all), *dr = nir_fsub(&b, nir_i2f32(&b, r4), here), *db = nir_fsub(&b, nir_i2f32(&b, b4), here);
         nir_def *edges = nir_fadd(&b, nir_fmul(&b, dr, dr), nir_fmul(&b, db, db));
         nir_def *fr = nir_i2f32(&b, vr), *fg = nir_i2f32(&b, g), *fb = nir_i2f32(&b, vb);
         nir_def *take = nir_bcsel(&b, nir_ieq_imm(&b, k, 0), valid, nir_bcsel(&b, nir_ieq_imm(&b, k, 1), fair, grey));
         nir_def *c3[4];

         for (unsigned c = 0; c < 4; c++)
            c3[c] = nir_b2f32(&b, nir_iand(&b, valid, nir_ieq(&b, bin, nir_iadd_imm(&b, base, c))));
         nir_def *t = nir_b2f32(&b, take), *e = nir_b2f32(&b, edge_ok);
         nir_def *add[4] = {
            nir_bcsel(&b, is_edge, nir_fmul(&b, edges, e), nir_bcsel(&b, is_hist, c3[0], nir_fmul(&b, fr, t))),
            nir_bcsel(&b, is_edge, nir_fmul(&b, here, e), nir_bcsel(&b, is_hist, c3[1], nir_fmul(&b, fg, t))),
            nir_bcsel(&b, is_edge, e, nir_bcsel(&b, is_hist, c3[2], nir_fmul(&b, fb, t))),
            nir_bcsel(&b, is_edge, zero, nir_bcsel(&b, is_hist, c3[3], t)),
         };
         for (unsigned c = 0; c < 4; c++)
            acc[c] = nir_fadd(&b, acc[c], add[c]);
      }
   }
   nir_store_var(&b, out, nir_vec4(&b, acc[0], acc[1], acc[2], acc[3]), 0xf);
   return isp_fragment_end(&b, 1);
}

/* y2s65: camera_isp.c's isp_measure() choice of step for a frame of cw x ch cells (the whole frame) */
static uint32_t
isp_stats_step(uint32_t cw, uint32_t ch)
{
   uint32_t step = cw >= 1024 && ch >= 768 ? 8 : cw >= 256 && ch >= 192 ? 4 : 2;

   return cw < 256 ? 4 : step;
}
'''

edit('air_isp.c', [
    ('result->size >= sizeof(*result)', 'result->size >= RWA_ISP_RESULT_SIZE_1', 3),
    ('output->size < sizeof(*output)', 'output->size < RWA_ISP_OUTPUT_SIZE_1'),
    ('''   struct pipe_fence_handle *pending[ISP_PENDING];
   unsigned pendings;
};''', '''   struct pipe_fence_handle *pending[ISP_PENDING];
   unsigned pendings;
   /* y2s65: the frame's statistics (isp_stats_shader): the texture, and what the caller asked of this render */
   struct rwa_texture *stats;
   uint32_t stats_tw, stats_th;                 /* the texture's size */
   float *stats_out;
   uint32_t stats_floats, stats_w, stats_h, stats_step;   /* stats_w 0: not made by this render */
};'''),
    ('''static void
isp_state_destroy(struct rwa_device *device)''', SHADER + '''
static void
isp_state_destroy(struct rwa_device *device)'''),
    ('''   void *fs[6] = { device->isp.cells[0], device->isp.cells[1], device->isp.full[0], device->isp.full[1], device->isp.luma,
                   device->isp.chroma };''', '''   void *fs[8] = { device->isp.cells[0], device->isp.cells[1], device->isp.full[0], device->isp.full[1], device->isp.luma,
                   device->isp.chroma, device->isp.stats[0], device->isp.stats[1] };'''),
    ('''   for (unsigned i = 0; i < 6; i++)
      any = any || fs[i] != NULL;''', '''   for (unsigned i = 0; i < 8; i++)
      any = any || fs[i] != NULL;'''),
    ('''   for (unsigned i = 0; i < 6; i++)
      if (fs[i])''', '''   for (unsigned i = 0; i < 8; i++)
      if (fs[i])'''),
    ('''       isp_pass(device, chroma, fs_chroma, rgb_texture, 1, &block, "chroma", why, why_size) != 0)
      return RWA_ISP_E_GPU;
   device->pipe->flush(device->pipe, fence, 0);''', '''       isp_pass(device, chroma, fs_chroma, rgb_texture, 1, &block, "chroma", why, why_size) != 0)
      return RWA_ISP_E_GPU;
   /* y2s65: the frame's statistics, one more pass over the frame alone, when the caller asked for them and has room */
   isp->stats_w = isp->stats_h = isp->stats_step = 0;
   if (isp->stats_out != NULL) {
      uint32_t cw = f->width / 2, ch = f->height / 2, step = isp_stats_step(cw, ch);
      uint32_t nw = (cw + step - 1) / step, nh = (ch + step - 1) / step;
      uint32_t gw = (nw + RWA_ISP_STATS_BLOCK - 1) / RWA_ISP_STATS_BLOCK, gh = (nh + RWA_ISP_STATS_BLOCK - 1) / RWA_ISP_STATS_BLOCK;
      uint32_t tw = gw * RWA_ISP_STATS_KINDS, th = gh;

      if ((uint64_t)tw * th * 4 <= isp->stats_floats && tw <= 8192 && th <= 8192) {
         void *fs_stats = device->isp.stats[variant] != NULL ? device->isp.stats[variant]
                        : isp_shader(device, &device->isp.stats[variant], isp_stats_shader(device->nir_options, f->format), why, why_size);

         if (isp->stats != NULL && (isp->stats_tw != tw || isp->stats_th != th)) {
            rwa_texture_destroy(isp->stats);
            isp->stats = NULL;
         }
         if (fs_stats != NULL && isp->stats == NULL) {
            isp->stats = isp_texture(device, 125 /* RGBA32Float */, tw, th, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, why_size);
            isp->stats_tw = tw;
            isp->stats_th = th;
         }
         if (fs_stats != NULL && isp->stats != NULL) {
            struct isp_block sb = block;
            struct rwa_texture *const stats_textures[1] = { frame };
            int white = 1023 - (int)f->black;

            white = white < 64 ? 64 : white;
            sb.q[0] = (int32_t)step;
            sb.q[1] = (int32_t)cw;
            sb.q[2] = (int32_t)ch;
            sb.q[3] = white * 85 / 100;
            sb.q[4] = (int32_t)gw;
            sb.out_w = (int32_t)tw;
            sb.out_h = (int32_t)th;
            if (isp_pass(device, isp->stats, fs_stats, stats_textures, 1, &sb, "statistics", why, why_size) == 0) {
               isp->stats_w = gw;
               isp->stats_h = gh;
               isp->stats_step = step;
            }
         }
      }
   }
   device->pipe->flush(device->pipe, fence, 0);'''),
    ('''   uint64_t t = os_time_get_nano();
   int error = isp_fence_wait(isp, fence, why, why_size);

   if (result != NULL && result->size >= RWA_ISP_RESULT_SIZE_1)
      result->wait_ns = os_time_get_nano() - t;
   return error;
}''', '''   uint64_t t = os_time_get_nano();
   int error = isp_fence_wait(isp, fence, why, why_size);

   if (result != NULL && result->size >= RWA_ISP_RESULT_SIZE_1)
      result->wait_ns = os_time_get_nano() - t;
   /* y2s65: the statistics, read back now that the passes are done */
   uint32_t sw = 0, sh = 0;

   if (error == RWA_ISP_OK && isp->stats_out != NULL && isp->stats_w != 0 && isp->stats != NULL) {
      const unsigned origin[3] = { 0, 0, 0 }, size[3] = { isp->stats_tw, isp->stats_th, 1 };

      if (rwa_texture_read(isp->stats, 0, 0, origin, size, isp->stats_out, (size_t)isp->stats_tw * 16, why, why_size) == 0) {
         sw = isp->stats_w;
         sh = isp->stats_h;
      }
   }
   if (result != NULL && result->size >= sizeof(*result)) {
      result->stats_w = sw;
      result->stats_h = sh;
      result->stats_step = sw != 0 ? isp->stats_step : 0;
   }
   isp->stats_out = NULL;
   return error;
}'''),
    ('''   if (async) {
      struct pipe_fence_handle *oldest = NULL;
''', '''   if (async) {
      struct pipe_fence_handle *oldest = NULL;

      isp->stats_out = NULL;                   /* (y2s65: no statistics without the wait) */
      if (result != NULL && result->size >= sizeof(*result))
         result->stats_w = result->stats_h = result->stats_step = 0;
'''),
    ('''      error = isp_surface_planes(isp, output, params->video_range, &luma, &chroma, &imported, why, why_size);''',
     '''      /* y2s65: the statistics, when the caller's struct has their fields and gave room */
      isp->stats_out = output->size >= sizeof(*output) && output->stats != NULL && !(output->flags & RWA_ISP_OUT_ASYNC) ? output->stats : NULL;
      isp->stats_floats = isp->stats_out != NULL ? output->stats_floats : 0;
      error = isp_surface_planes(isp, output, params->video_range, &luma, &chroma, &imported, why, why_size);'''),
    ('''/* the device the contexts share, for a caller of rwa_isp_render_textures to make its textures on */''',
     '''/* y2s65, for tests (not in rwa_isp_api.h): the next rwa_isp_render_textures also makes the statistics into `stats' */
void rwa_isp_want_stats(struct rwa_isp *isp, float *stats, uint32_t floats);
void
rwa_isp_want_stats(struct rwa_isp *isp, float *stats, uint32_t floats)
{
   if (isp != NULL) {
      isp->stats_out = stats;
      isp->stats_floats = stats != NULL ? floats : 0;
   }
}

/* the device the contexts share, for a caller of rwa_isp_render_textures to make its textures on */'''),
    ('''   struct rwa_texture *own[5] = { isp->raw_own, isp->columns, isp->rows, isp->gamma, isp->rgb };

   for (int i = 0; i < 5; i++)''', '''   struct rwa_texture *own[6] = { isp->raw_own, isp->columns, isp->rows, isp->gamma, isp->rgb, isp->stats };

   for (int i = 0; i < 6; i++)'''),
])
print('y2s65-cam-stats edits applied')
