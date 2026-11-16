/*
 * y2s39: THE CAMERA'S ISP ON THE GPU (rwa_isp_api.h; the specification is gpu/build/camera-userland-20261109/GPU_ISP_API.md).
 *
 * Included at the end of air_metal.c: it is made of that file's parts (the device, textures, the quad and fixed state of
 * rwa_texture_convert_ycbcr, make_stage_cso) and adds nothing to what was there.
 *
 * What the camera bundle's camera_isp.c does on the processor for a picture -- isp_render_nv12() and isp_render_full_nv12() --
 * as three passes:
 *
 *   1. RAW10 to 8-bit R, G, B into an intermediate RGBA8 picture of the output's size. Two shaders, as there are two functions:
 *      "cells" (render_line / render_line_window -> pixel_q: whole numbers throughout, and the SAME whole numbers here, so the
 *      picture is the processor's bit for bit) and "full" (full_line -> pixel: the 3x3 bilinear demosaic and floating point).
 *   2. R, G, B to the luma plane (nv12_pair's whole numbers).
 *   3. R, G, B to the chroma plane, each 2x2's Cb and Cr from the sum of its four pixels (the same).
 *
 * Where the processor reads the sensor for a pixel of the picture (crop, zoom, flip: fixed point in 64 bits) is not redone in
 * the shader: this file computes, with camera_isp.c's own arithmetic, the sensor column of every column of the picture and the
 * sensor row of every row, and the shader looks them up in two small textures. The tone curve is the caller's table, a texture.
 *
 * The frame is bytes: an R8Uint texture as wide as the frame's stride, from which the shader takes each 10-bit pixel out of
 * its two bytes. On the GPU it lies in a buffer (the caller's read() goes straight into it: rwa_isp_frame_memory, or it is
 * copied there) under a texture over that buffer; on softpipe, which has no such texture, it is an ordinary texture.
 *
 * SPDX-License-Identifier: MIT
 */
#include <dlfcn.h>
#include "util/os_time.h"
#include "rwa_isp_api.h"

/* the calls of rwa_isp_api.h (which declares them as a struct of pointers: a caller looks them up) and the two beside them */
unsigned rwa_isp_api_version(void);
struct rwa_isp *rwa_isp_create(char *why, size_t why_size);
void rwa_isp_destroy(struct rwa_isp *isp);
void *rwa_isp_frame_memory(struct rwa_isp *isp, unsigned slot, size_t header_bytes, size_t frame_bytes, char *why, size_t why_size);
int rwa_isp_render(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
                   const struct rwa_isp_output *output, struct rwa_isp_result *result, char *why, size_t why_size);
int rwa_isp_wait(struct rwa_isp *isp, char *why, size_t why_size);
void rwa_isp_forget_surfaces(struct rwa_isp *isp);
int rwa_isp_render_textures(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
                            struct rwa_texture *luma, struct rwa_texture *chroma, uint32_t width, uint32_t height, uint32_t flags,
                            struct rwa_isp_result *result, char *why, size_t why_size);
struct rwa_device *rwa_isp_device(struct rwa_isp *isp);

#define ISP_LUT_WHITE   3836            /* camera_isp.c's LUT_WHITE */
#define ISP_SLOTS       4
#define ISP_SURFACES    8
#define ISP_PENDING     8
#define ISP_KEEP_BYTES  (16u << 20)     /* an intermediate picture larger than this is not kept after its render */

/* what the shaders are told, in 32-bit words (std140 has nothing to say about a block of scalars); the indices are the words */
struct isp_block {
   int32_t black, wq, knee, span;              /* 0 */
   int32_t gq[3], vq;                          /* 4 */
   int32_t q[9];                               /* 8 */
   int32_t mid_x, mid_y;                       /* 17 */
   uint32_t r2k;                               /* 19 */
   int32_t shade_off;                          /* 20: 1 when the positions are a cell's top left (render_line), 0 when a pixel's */
   int32_t r_place, b_place;                   /* 21 */
   int32_t raw_w, raw_h;                       /* 23 */
   int32_t out_w, out_h;                       /* 25 */
   int32_t identity;                           /* 27 */
   float gain[3], pad0;                        /* 28 */
   float m[9];                                 /* 32 */
   int32_t k[9], oy;                           /* 41: nv12_pair's coefficients; 50: its luma offset */
   int32_t pad1;                               /* 51 */
};
enum {
   ISP_U_BLACK = 0, ISP_U_WQ, ISP_U_KNEE, ISP_U_SPAN, ISP_U_GQ, ISP_U_VQ = 7, ISP_U_Q = 8, ISP_U_MID_X = 17, ISP_U_MID_Y,
   ISP_U_R2K, ISP_U_SHADE_OFF, ISP_U_R_PLACE, ISP_U_B_PLACE, ISP_U_RAW_W, ISP_U_RAW_H, ISP_U_OUT_W, ISP_U_OUT_H,
   ISP_U_IDENTITY, ISP_U_GAIN = 28, ISP_U_M = 32, ISP_U_K = 41, ISP_U_OY = 50,
};
_Static_assert(sizeof(struct isp_block) == 52 * 4 && offsetof(struct isp_block, m) == ISP_U_M * 4 &&
               offsetof(struct isp_block, k) == ISP_U_K * 4 && offsetof(struct isp_block, identity) == ISP_U_IDENTITY * 4,
               "struct isp_block is the shaders' block, word for word");

struct isp_surface {
   uint32_t id, width, height, offset[2], row[2];
   const void *base;
   struct rwa_texture *luma, *chroma;
   uint64_t used;
};

struct rwa_isp {
   struct rwa_device *device;
   /* the frame's bytes for the GPU: buffers of the device's (0..ISP_SLOTS-1 the caller's, ISP_SLOTS the copy's) and a texture over each */
   struct {
      struct rwa_buffer *buffer;
      size_t header_bytes, frame_bytes, pad;    /* the frame's first byte is at map + pad + header_bytes */
      struct rwa_texture *texture;
      uint32_t stride, height;                  /* what `texture' was made for */
   } slot[ISP_SLOTS + 1];
   struct rwa_texture *raw_own;                 /* softpipe, or a GPU that would not lay a texture over a buffer: an ordinary texture */
   uint32_t raw_own_w, raw_own_h;
   bool no_buffer_textures;
   struct rwa_texture *columns, *rows, *gamma, *rgb;
   /* y2s70 NR2: the planes as made (y0, c0), smoothed (y1, c1, c2), the picture before (by, bc: valid while nr_before) and the next one
    * (by2, bc2), the blocks' difference and moves */
   struct rwa_texture *nr_y0, *nr_y1, *nr_c0, *nr_c1, *nr_c2, *nr_by, *nr_bc, *nr_by2, *nr_bc2, *nr_diff, *nr_moves;
   bool nr_before;
   uint32_t columns_n, rows_n, rgb_w, rgb_h;
   struct { uint32_t fw, fh, ow, oh, flip, zoom_bits, mode; bool valid; int shade_off; } map_key;
   uint8_t gamma_now[4096];
   bool gamma_valid;
   struct isp_surface surface[ISP_SURFACES];
   uint64_t clock;
   struct pipe_fence_handle *pending[ISP_PENDING];
   unsigned pendings;
   /* y2s65: the frame's statistics (isp_stats_shader): the texture, and what the caller asked of this render */
   struct rwa_texture *stats;
   uint32_t stats_tw, stats_th;                 /* the texture's size */
   float *stats_out;
   uint32_t stats_floats, stats_w, stats_h, stats_step;   /* stats_w 0: not made by this render */
};

static struct rwa_device *isp_device;           /* the process's, opened by the first rwa_isp_create and kept */

/* ---- camera_isp.c's arithmetic, for the tables and the block ------------------------------------------------------------------ */

/* crop(): the part of the sensor's cells the picture shows */
static void
isp_crop(uint32_t fw, uint32_t fh, uint32_t width, uint32_t height, uint32_t *x0, uint32_t *y0, uint32_t *cw, uint32_t *ch)
{
   uint32_t w = fw / 2, h = fh / 2;

   if ((uint64_t)width * h > (uint64_t)height * w) {
      *cw = w;
      *ch = (uint32_t)((uint64_t)w * height / width);
   } else {
      *ch = h;
      *cw = (uint32_t)((uint64_t)h * width / height);
   }
   *x0 = (w - *cw) / 2;
   *y0 = (h - *ch) / 2;
}

/* The sensor pixel (top left of the 2x2 read) for every column and every row of the picture: render_line's cells when there is
 * neither zoom nor flip, render_line_window's pixels otherwise; and RWA_ISP_MODE_FULL's, which is the pixel itself. */
static void
isp_positions(const struct rwa_isp_frame *f, const struct rwa_isp_params *p, uint32_t width, uint32_t height, uint16_t *columns,
              uint16_t *rows, int *shade_off)
{
   uint32_t x0, y0, cw, ch;

   if (p->mode == RWA_ISP_MODE_FULL) {
      for (uint32_t x = 0; x < width; x++)
         columns[x] = (uint16_t)x;
      for (uint32_t y = 0; y < height; y++)
         rows[y] = (uint16_t)y;
      *shade_off = 0;
      return;
   }
   isp_crop(f->width, f->height, width, height, &x0, &y0, &cw, &ch);
   if (!(p->zoom > 1.0005f || p->flip != 0)) {
      uint64_t step = ((uint64_t)cw << 16) / width, at = (uint64_t)x0 << 16;

      for (uint32_t x = 0; x < width; x++, at += step)
         columns[x] = (uint16_t)(2 * (uint32_t)(at >> 16));
      for (uint32_t row = 0; row < height; row++)
         rows[row] = (uint16_t)(2 * (y0 + (uint32_t)((uint64_t)row * ch / height)));
      *shade_off = 1;
      return;
   }
   float zoom = p->zoom < 1 ? 1 : p->zoom > 32 ? 32 : p->zoom;
   uint64_t full_w = (uint64_t)cw << 17, full_h = (uint64_t)ch << 17;
   uint64_t zw = (uint64_t)((double)full_w / zoom), zh = (uint64_t)((double)full_h / zoom);
   uint64_t left = ((uint64_t)x0 << 17) + (full_w - zw) / 2, top = ((uint64_t)y0 << 17) + (full_h - zh) / 2;
   int64_t step = (int64_t)(zw / width), at = (int64_t)left;

   for (uint32_t row = 0; row < height; row++) {
      uint32_t from = p->flip & RWA_ISP_FLIP_V ? height - 1 - row : row;
      uint32_t py = (uint32_t)((top + (uint64_t)from * zh / height) >> 16);

      rows[row] = (uint16_t)(py > f->height - 2 ? f->height - 2 : py);
   }
   if (p->flip & RWA_ISP_FLIP_H) {
      at += step * (int64_t)(width - 1);
      step = -step;
   }
   for (uint32_t x = 0; x < width; x++, at += step) {
      uint32_t px = (uint32_t)(at >> 16);

      columns[x] = (uint16_t)(px > f->width - 2 ? f->width - 2 : px);
   }
   *shade_off = 0;
}

/* pipe_init(), cell_places() and nv12_pair()'s constants */
static void
isp_block_fill(struct isp_block *u, const struct rwa_isp_frame *f, const struct rwa_isp_params *p, uint32_t width, uint32_t height,
               int shade_off)
{
   static const int red[4] = { 0, 1, 2, 3 }, blue[4] = { 3, 2, 1, 0 };
   static const int32_t full[9] = { 19595, 38470, 7471, -11058, -21710, 32768, 32768, -27439, -5329 };
   static const int32_t video[9] = { 16829, 33039, 6416, -9714, -19070, 28784, 28784, -24103, -4681 };
   int white = 1023 - (int)f->black;
   float least = p->wb[0] < p->wb[1] ? p->wb[0] : p->wb[1];

   memset(u, 0, sizeof(*u));
   white = white < 64 ? 64 : white;
   least = p->wb[2] < least ? p->wb[2] : least;
   least = least > 0.05f ? least : 0.05f;
   u->black = (int32_t)f->black;
   u->wq = white;
   u->knee = u->wq * 85 / 100;
   u->span = u->wq - u->knee > 0 ? u->wq - u->knee : 1;
   u->mid_x = (int32_t)f->width / 2;
   u->mid_y = (int32_t)f->height / 2;
   u->vq = (int32_t)((p->falloff > 0 ? p->falloff > 2 ? 2 : p->falloff : 0) * 256.0f + 0.5f);
   u->r2k = (uint32_t)(((uint64_t)1 << 32) / ((uint64_t)u->mid_x * (uint64_t)u->mid_x + (uint64_t)u->mid_y * (uint64_t)u->mid_y + 1));
   u->identity = 1;
   for (int c = 0; c < 3; c++) {
      float gain = p->wb[c] / least * p->digital;

      gain = gain > 64 ? 64 : gain;
      u->gain[c] = gain;
      u->gq[c] = (int32_t)(gain * (c == 1 ? 128.0f : 256.0f) + 0.5f);
      for (int j = 0; j < 3; j++) {
         float m = p->ccm[c * 3 + j] * (ISP_LUT_WHITE / (float)white), q = m * 4096.0f;

         u->m[c * 3 + j] = m;
         u->identity = u->identity && (c == j ? p->ccm[c * 3 + j] == 1 : p->ccm[c * 3 + j] == 0);
         u->q[c * 3 + j] = (int32_t)(q >= 0 ? q + 0.5f : q - 0.5f);
      }
   }
   u->shade_off = shade_off;
   u->r_place = red[f->bayer & 3];
   u->b_place = blue[f->bayer & 3];
   u->raw_w = (int32_t)f->width;
   u->raw_h = (int32_t)f->height;
   u->out_w = (int32_t)width;
   u->out_h = (int32_t)height;
   memcpy(u->k, p->video_range ? video : full, sizeof(u->k));
   u->oy = (p->video_range ? 16 << 16 : 0) + (1 << 15);
}

/* ---- the shaders ---------------------------------------------------------------------------------------------------------------- */

static nir_def *
isp_word(nir_builder *b, unsigned word)
{
   return nir_load_ubo(b, 1, 32, nir_imm_int(b, 0), nir_imm_int(b, (int)word * 4), .align_mul = 4, .align_offset = 0,
                       .range_base = word * 4, .range = 4);
}

/* a texel by its whole-number place, level 0 */
static nir_def *
isp_fetch(nir_builder *b, unsigned unit, nir_def *x, nir_def *y, nir_alu_type type)
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

/* the 10-bit pixel (px, py) of the frame, texture 0: group() of camera_isp.c, each pixel out of two bytes */
static nir_def *
isp_raw(nir_builder *b, unsigned format, nir_def *px, nir_def *py)
{
   nir_def *k = nir_iand_imm(b, px, 3), *g5 = nir_imul_imm(b, nir_ushr_imm(b, px, 2), 5), *shift = nir_ishl_imm(b, k, 1);
   nir_def *b0 = nir_channel(b, isp_fetch(b, 0, nir_iadd(b, g5, k), py, nir_type_uint32), 0);

   if (format == 4) {      /* one little-endian 40-bit word: pixel k is bits 10k .. 10k + 9, bytes k and k + 1 */
      nir_def *b1 = nir_channel(b, isp_fetch(b, 0, nir_iadd_imm(b, nir_iadd(b, g5, k), 1), py, nir_type_uint32), 0);

      return nir_iand_imm(b, nir_ushr(b, nir_ior(b, b0, nir_ishl_imm(b, b1, 8)), shift), 0x3ff);
   }
   /* MIPI RAW10: byte k is the pixel's high eight bits, byte 4 holds the four pairs of low bits */
   nir_def *b4 = nir_channel(b, isp_fetch(b, 0, nir_iadd_imm(b, g5, 4), py, nir_type_uint32), 0);

   return nir_ior(b, nir_ishl_imm(b, b0, 2), nir_iand_imm(b, nir_ushr(b, b4, shift), 3));
}

/* the picture's pixel this fragment is: the quad's coordinate times the picture's size, which at a pixel's centre is x + 0.5 */
static void
isp_pixel_xy(nir_builder *b, nir_variable *uv, nir_def **x, nir_def **y)
{
   nir_def *at = nir_load_var(b, uv);

   *x = nir_f2i32(b, nir_fmul(b, nir_channel(b, at, 0), nir_i2f32(b, isp_word(b, ISP_U_OUT_W))));
   *y = nir_f2i32(b, nir_fmul(b, nir_channel(b, at, 1), nir_i2f32(b, isp_word(b, ISP_U_OUT_H))));
}

static nir_builder
isp_fragment_begin(const struct nir_shader_compiler_options *options, const char *name, nir_variable **uv, nir_variable **out)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, options, "%s", name);

   *uv = nir_variable_create(b.shader, nir_var_shader_in, glsl_vec4_type(), "uv");
   (*uv)->data.location = VARYING_SLOT_VAR0;
   (*uv)->data.driver_location = 0;
   (*uv)->data.interpolation = INTERP_MODE_SMOOTH;
   *out = nir_variable_create(b.shader, nir_var_shader_out, glsl_vec4_type(), "color");
   (*out)->data.location = FRAG_RESULT_DATA0;
   (*out)->data.driver_location = 0;
   return b;
}

static nir_shader *
isp_fragment_end(nir_builder *b, unsigned textures)
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

/* highlights_q(): v[] clipped to white, and drawn to the brightest of the three where the sensor itself was near its white */
static void
isp_highlights(nir_builder *b, nir_def *v[3], nir_def *most)
{
   nir_def *wq = isp_word(b, ISP_U_WQ), *knee = isp_word(b, ISP_U_KNEE), *span = isp_word(b, ISP_U_SPAN);

   for (int c = 0; c < 3; c++)
      v[c] = nir_imin(b, v[c], wq);
   nir_def *top = nir_imax(b, nir_imax(b, v[0], v[1]), v[2]);
   nir_def *t = nir_isub(b, nir_imin(b, most, wq), knee), *over = nir_ilt(b, knee, most);

   for (int c = 0; c < 3; c++) {
      /* (top - v) * t is never negative where it is used, so the division rounds as C's does */
      nir_def *more = nir_idiv(b, nir_imul(b, nir_isub(b, top, v[c]), nir_imax(b, t, nir_imm_int(b, 0))), span);

      v[c] = nir_bcsel(b, over, nir_iadd(b, v[c], more), v[c]);
   }
}

/* the tone curve's table (texture `unit', 4096 x 1) at o, as the 0..1 an 8-bit attachment stores back as the same byte */
static nir_def *
isp_curve(nir_builder *b, unsigned unit, nir_def *o)
{
   nir_def *i = nir_imin(b, nir_imax(b, o, nir_imm_int(b, 0)), nir_imm_int(b, 4095));

   return nir_fmul_imm(b, nir_u2f32(b, nir_channel(b, isp_fetch(b, unit, i, nir_imm_int(b, 0), nir_type_uint32), 0)), 1.0 / 255.0);
}

/* render_line / render_line_window -> pixel_q. Textures: 0 the frame, 1 the columns, 2 the rows, 3 the tone curve. */
static nir_shader *
isp_cells_shader(const struct nir_shader_compiler_options *options, unsigned format)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_cells", &uv, &out);
   nir_def *x, *y, *zero = nir_imm_int(&b, 0);

   isp_pixel_xy(&b, uv, &x, &y);
   nir_def *px = nir_channel(&b, isp_fetch(&b, 1, x, zero, nir_type_uint32), 0);
   nir_def *py = nir_channel(&b, isp_fetch(&b, 2, y, zero, nir_type_uint32), 0);
   nir_def *black = isp_word(&b, ISP_U_BLACK), *value[4];

   for (int i = 0; i < 4; i++)
      value[i] = nir_imax(&b, nir_isub(&b, isp_raw(&b, format, nir_iadd_imm(&b, px, i & 1), nir_iadd_imm(&b, py, i >> 1)), black), zero);

   /* value[i] is at place ^ i of a cell (place: where (px, py) is in its cell), so the cell's site s is value[place ^ s] */
   nir_def *place = nir_ior(&b, nir_ishl_imm(&b, nir_iand_imm(&b, py, 1), 1), nir_iand_imm(&b, px, 1));
   nir_def *colour[2];

   for (int i = 0; i < 2; i++) {
      nir_def *s = nir_ixor(&b, place, isp_word(&b, i ? ISP_U_B_PLACE : ISP_U_R_PLACE));
      nir_def *right = nir_ine_imm(&b, nir_iand_imm(&b, s, 1), 0), *below = nir_ine_imm(&b, nir_iand_imm(&b, s, 2), 0);

      colour[i] = nir_bcsel(&b, below, nir_bcsel(&b, right, value[3], value[2]), nir_bcsel(&b, right, value[1], value[0]));
   }
   nir_def *r = colour[0], *bl = colour[1];
   nir_def *g2 = nir_isub(&b, nir_isub(&b, nir_iadd(&b, nir_iadd(&b, value[0], value[1]), nir_iadd(&b, value[2], value[3])), r), bl);

   /* shade_at(): the lens's falloff undone, 256 in the middle */
   nir_def *off = isp_word(&b, ISP_U_SHADE_OFF);
   nir_def *dx = nir_isub(&b, nir_iadd(&b, px, off), isp_word(&b, ISP_U_MID_X));
   nir_def *dy = nir_isub(&b, nir_iadd(&b, py, off), isp_word(&b, ISP_U_MID_Y));
   nir_def *d2 = nir_iadd(&b, nir_imul(&b, dx, dx), nir_imul(&b, dy, dy));
   nir_def *shade = nir_iadd_imm(&b, nir_ushr_imm(&b, nir_imul(&b, nir_ushr_imm(&b, nir_imul(&b, d2, isp_word(&b, ISP_U_R2K)), 16),
                                                                 isp_word(&b, ISP_U_VQ)), 16), 256);

   /* pixel_q() */
   nir_def *in[3] = { r, g2, bl }, *v[3];

   for (int c = 0; c < 3; c++) {
      nir_def *gain = nir_ishr_imm(&b, nir_imul(&b, isp_word(&b, ISP_U_GQ + c), shade), 8);

      v[c] = nir_ishr_imm(&b, nir_iadd_imm(&b, nir_imul(&b, in[c], gain), 128), 8);
   }
   nir_def *most = nir_imax(&b, nir_imax(&b, r, bl), nir_ushr_imm(&b, g2, 1));

   isp_highlights(&b, v, most);
   nir_def *rgb[3];

   for (int c = 0; c < 3; c++) {
      nir_def *o = nir_imul(&b, isp_word(&b, ISP_U_Q + c * 3), v[0]);

      o = nir_iadd(&b, o, nir_imul(&b, isp_word(&b, ISP_U_Q + c * 3 + 1), v[1]));
      o = nir_iadd(&b, o, nir_imul(&b, isp_word(&b, ISP_U_Q + c * 3 + 2), v[2]));
      rgb[c] = isp_curve(&b, 3, nir_ishr_imm(&b, nir_iadd_imm(&b, o, 2048), 12));
   }
   nir_store_var(&b, out, nir_vec4(&b, rgb[0], rgb[1], rgb[2], nir_imm_float(&b, 1.0f)), 0xf);
   return isp_fragment_end(&b, 4);
}

/* full_line -> pixel: every pixel, the two colours a site has not from the same colour's sites in the 3x3 around it.
 * Textures: 0 the frame, 3 the tone curve (1 and 2 are bound and not read: the picture's pixel is the sensor's). */
static nir_shader *
isp_full_shader(const struct nir_shader_compiler_options *options, unsigned format)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_full", &uv, &out);
   nir_def *x, *y, *zero = nir_imm_int(&b, 0), *one = nir_imm_int(&b, 1);

   isp_pixel_xy(&b, uv, &x, &y);
   nir_def *black = isp_word(&b, ISP_U_BLACK), *r_place = isp_word(&b, ISP_U_R_PLACE), *b_place = isp_word(&b, ISP_U_B_PLACE);
   nir_def *last_x = nir_iadd_imm(&b, isp_word(&b, ISP_U_RAW_W), -1), *last_y = nir_iadd_imm(&b, isp_word(&b, ISP_U_RAW_H), -1);
   nir_def *sum[3] = { zero, zero, zero }, *n[3] = { zero, zero, zero }, *own = NULL, *own_is[3] = { NULL, NULL, NULL };

   for (int dy = -1; dy <= 1; dy++) {
      nir_def *yy = nir_imin(&b, nir_imax(&b, nir_iadd_imm(&b, y, dy), zero), last_y);

      for (int dx = -1; dx <= 1; dx++) {
         nir_def *xx = nir_imin(&b, nir_imax(&b, nir_iadd_imm(&b, x, dx), zero), last_x);
         nir_def *value = nir_imax(&b, nir_isub(&b, isp_raw(&b, format, xx, yy), black), zero);
         nir_def *site = nir_ior(&b, nir_ishl_imm(&b, nir_iand_imm(&b, yy, 1), 1), nir_iand_imm(&b, xx, 1));
         nir_def *is_r = nir_ieq(&b, site, r_place), *is_b = nir_ieq(&b, site, b_place);
         nir_def *is[3] = { is_r, nir_inot(&b, nir_ior(&b, is_r, is_b)), is_b };

         if (dx == 0 && dy == 0) {
            own = value;
            for (int c = 0; c < 3; c++)
               own_is[c] = is[c];
            continue;
         }
         for (int c = 0; c < 3; c++) {
            sum[c] = nir_iadd(&b, sum[c], nir_bcsel(&b, is[c], value, zero));
            n[c] = nir_iadd(&b, n[c], nir_bcsel(&b, is[c], one, zero));
         }
      }
   }
   /* (its own value wins over the neighbours'; a colour with no site around is 0 over 1) */
   nir_def *colour[3], *v[3];

   for (int c = 0; c < 3; c++) {
      nir_def *mean = nir_fdiv(&b, nir_i2f32(&b, sum[c]), nir_i2f32(&b, nir_imax(&b, n[c], one)));

      colour[c] = nir_bcsel(&b, own_is[c], nir_i2f32(&b, own), mean);
      /* pixel(): (int32_t)(value * gain + 0.5f) */
      v[c] = nir_f2i32(&b, nir_fadd_imm(&b, nir_fmul(&b, colour[c], isp_word(&b, ISP_U_GAIN + c)), 0.5));
   }
   nir_def *most = nir_f2i32(&b, nir_fmax(&b, nir_fmax(&b, colour[0], colour[1]), colour[2]));

   isp_highlights(&b, v, most);
   nir_def *vf[3] = { nir_i2f32(&b, v[0]), nir_i2f32(&b, v[1]), nir_i2f32(&b, v[2]) }, *rgb[3];
   nir_def *identity = nir_ine_imm(&b, isp_word(&b, ISP_U_IDENTITY), 0);

   for (int c = 0; c < 3; c++) {
      nir_def *all = nir_fmul(&b, isp_word(&b, ISP_U_M + c * 3), vf[0]);

      all = nir_fadd(&b, all, nir_fmul(&b, isp_word(&b, ISP_U_M + c * 3 + 1), vf[1]));
      all = nir_fadd(&b, all, nir_fmul(&b, isp_word(&b, ISP_U_M + c * 3 + 2), vf[2]));
      nir_def *one_of = nir_fmul(&b, vf[c], isp_word(&b, ISP_U_M + c * 4));
      /* curve(): (int)(v + 0.5f), then into the table's range */
      rgb[c] = isp_curve(&b, 3, nir_f2i32(&b, nir_fadd_imm(&b, nir_bcsel(&b, identity, one_of, all), 0.5)));
   }
   nir_store_var(&b, out, nir_vec4(&b, rgb[0], rgb[1], rgb[2], nir_imm_float(&b, 1.0f)), 0xf);
   return isp_fragment_end(&b, 4);
}

/* an 8-bit texel of the intermediate picture (texture 0) as the three whole numbers it holds */
static void
isp_rgb8(nir_builder *b, nir_def *x, nir_def *y, nir_def *rgb[3])
{
   nir_def *texel = isp_fetch(b, 0, x, y, nir_type_float32);

   for (int c = 0; c < 3; c++)
      rgb[c] = nir_f2i32(b, nir_fadd_imm(b, nir_fmul_imm(b, nir_channel(b, texel, c), 255.0), 0.5));
}

static nir_def *
isp_dot_k(nir_builder *b, unsigned first, nir_def *rgb[3])
{
   nir_def *sum = nir_imul(b, isp_word(b, ISP_U_K + first), rgb[0]);

   sum = nir_iadd(b, sum, nir_imul(b, isp_word(b, ISP_U_K + first + 1), rgb[1]));
   return nir_iadd(b, sum, nir_imul(b, isp_word(b, ISP_U_K + first + 2), rgb[2]));
}

/* nv12_pair(): a pixel's Y */
static nir_shader *
isp_luma_shader(const struct nir_shader_compiler_options *options)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_luma", &uv, &out);
   nir_def *x, *y, *rgb[3];

   isp_pixel_xy(&b, uv, &x, &y);
   isp_rgb8(&b, x, y, rgb);
   nir_def *luma = nir_ishr_imm(&b, nir_iadd(&b, isp_dot_k(&b, 0, rgb), isp_word(&b, ISP_U_OY)), 16);
   nir_def *value = nir_fmul_imm(&b, nir_i2f32(&b, luma), 1.0 / 255.0);

   nir_store_var(&b, out, nir_vec4(&b, value, nir_imm_float(&b, 0.0f), nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f)), 0xf);
   return isp_fragment_end(&b, 1);
}

/* nv12_pair(): a 2x2's Cb and Cr from the sum of its four pixels. The block's out_w and out_h are the PICTURE's here; this
 * pass's attachment is half that each way, and the fragment's place in it comes from the attachment's own size. */
static nir_shader *
isp_chroma_shader(const struct nir_shader_compiler_options *options)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_chroma", &uv, &out);
   nir_def *w = isp_word(&b, ISP_U_OUT_W), *h = isp_word(&b, ISP_U_OUT_H), *at = nir_load_var(&b, uv);
   nir_def *cw = nir_ushr_imm(&b, nir_iadd_imm(&b, w, 1), 1), *ch = nir_ushr_imm(&b, nir_iadd_imm(&b, h, 1), 1);
   nir_def *cx = nir_f2i32(&b, nir_fmul(&b, nir_channel(&b, at, 0), nir_i2f32(&b, cw)));
   nir_def *cy = nir_f2i32(&b, nir_fmul(&b, nir_channel(&b, at, 1), nir_i2f32(&b, ch)));
   nir_def *x0 = nir_ishl_imm(&b, cx, 1), *y0 = nir_ishl_imm(&b, cy, 1);
   nir_def *x1 = nir_imin(&b, nir_iadd_imm(&b, x0, 1), nir_iadd_imm(&b, w, -1));
   nir_def *y1 = nir_imin(&b, nir_iadd_imm(&b, y0, 1), nir_iadd_imm(&b, h, -1));
   nir_def *sum[3] = { nir_imm_int(&b, 0), nir_imm_int(&b, 0), nir_imm_int(&b, 0) };

   for (int i = 0; i < 4; i++) {
      nir_def *rgb[3];

      isp_rgb8(&b, i & 1 ? x1 : x0, i & 2 ? y1 : y0, rgb);
      for (int c = 0; c < 3; c++)
         sum[c] = nir_iadd(&b, sum[c], rgb[c]);
   }
   nir_def *value[2];

   for (int i = 0; i < 2; i++) {
      /* + (128 << 18) + (1 << 17) - 1, >> 18, into 0..255 */
      nir_def *c = nir_ishr_imm(&b, nir_iadd_imm(&b, isp_dot_k(&b, 3 + 3 * i, sum), (128 << 18) + (1 << 17) - 1), 18);

      c = nir_imin(&b, nir_imax(&b, c, nir_imm_int(&b, 0)), nir_imm_int(&b, 255));
      value[i] = nir_fmul_imm(&b, nir_i2f32(&b, c), 1.0 / 255.0);
   }
   nir_store_var(&b, out, nir_vec4(&b, value[0], value[1], nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f)), 0xf);
   return isp_fragment_end(&b, 1);
}


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


/* y2s65: camera_isp.c's isp_measure() choice of step for a frame of cw x ch cells (the whole frame) */
static uint32_t
isp_stats_step(uint32_t cw, uint32_t ch)
{
   uint32_t step = cw >= 1024 && ch >= 768 ? 8 : cw >= 256 && ch >= 192 ? 4 : 2;

   return cw < 256 ? 4 : step;
}

static void
isp_state_destroy(struct rwa_device *device)
{
   struct pipe_context *pipe = device->pipe;
   void *fs[15] = { device->isp.cells[0], device->isp.cells[1], device->isp.full[0], device->isp.full[1], device->isp.luma,
                    device->isp.chroma, device->isp.stats[0], device->isp.stats[1], device->isp.nr[0], device->isp.nr[1], device->isp.nr[2],
                    device->isp.nr[3], device->isp.nr[4], device->isp.nr[5], device->isp.nr[6] };
   bool any = device->isp.nearest != NULL;

   for (unsigned i = 0; i < 15; i++)
      any = any || fs[i] != NULL;
   if (any)
      pipe->bind_fs_state(pipe, NULL);
   for (unsigned i = 0; i < 15; i++)
      if (fs[i])
         pipe->delete_fs_state(pipe, fs[i]);
   if (device->isp.nearest)
      pipe->delete_sampler_state(pipe, device->isp.nearest);
   memset(&device->isp, 0, sizeof(device->isp));
}

/* a shader of the device's, compiled when it is first drawn with */
static void *
isp_shader(struct rwa_device *device, void **slot, nir_shader *nir, char *why, size_t why_size)
{
   if (*slot == NULL && nir != NULL)
      *slot = make_stage_cso(device, nir, NULL, 0, false, NULL, why, why_size);
   if (nir != NULL)
      ralloc_free(nir);
   return *slot;
}

/* ---- one pass --------------------------------------------------------------------------------------------------------------------- */

static struct pipe_sampler_view *
isp_view(struct rwa_device *device, struct rwa_texture *t)
{
   if (!t->view) {
      struct pipe_sampler_view tmpl = { 0 };

      u_sampler_view_default_template(&tmpl, t->res, t->format);
      tmpl.target = t->target;
      t->view = device->pipe->create_sampler_view(device->pipe, t->res, &tmpl);
   }
   return t->view;
}

/* The quad over the whole of `dst' with `fs', textures 0..count-1 fetched by whole-number places, the block as constant buffer 0.
 * Queued like a draw; the context is left as rwa_render_draw leaves it. lock held. */
static int
isp_pass(struct rwa_device *device, struct rwa_texture *dst, void *fs, struct rwa_texture *const *textures, unsigned count,
         const struct isp_block *block, const char *what, char *why, size_t why_size)
{
   struct pipe_context *pipe = device->pipe;
   struct pipe_sampler_view *views[4] = { NULL };
   void *samplers[4] = { NULL }, *none[4] = { NULL };
   const unsigned w = dst->res->width0, h = dst->res->height0;

   for (unsigned i = 0; i < count; i++) {
      views[i] = isp_view(device, textures[i]);
      samplers[i] = device->isp.nearest;
      if (!views[i]) {
         say(why, why_size, "%s would not make a sampler view of a texture the camera's %s pass reads", rwa_device_name(device), what);
         return -1;
      }
   }
   struct pipe_surface *surface = texture_surface(device, dst, 0, 0);

   if (!surface) {
      say(why, why_size, "%s would not make a render surface for the camera's %s pass", rwa_device_name(device), what);
      return -1;
   }
   struct pipe_framebuffer_state fb = { .width = w, .height = h, .nr_cbufs = 1, .samples = 1 };

   fb.cbufs[0] = surface;
   pipe->set_framebuffer_state(pipe, &fb);
   pipe->set_viewport_states(pipe, 0, 1, &(struct pipe_viewport_state){
      .scale = { w / 2.0f, h / 2.0f, 1.0f },
      .translate = { w / 2.0f, h / 2.0f, 0.0f } });
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
      /* a user buffer: both back ends copy it when the draw is emitted (bind_stage), so the caller's block need not outlive this */
      struct pipe_constant_buffer cb = { .buffer_size = sizeof(*block), .user_buffer = block };

      pipe->set_constant_buffer(pipe, PIPE_SHADER_FRAGMENT, 0, false, &cb);
   }
   {
      union pipe_color_union zero = { .f = { 0.0f, 0.0f, 0.0f, 1.0f } };
      struct pipe_draw_info info = {
         .mode = MESA_PRIM_TRIANGLE_STRIP, .index_bounds_valid = true,
         .min_index = 0, .max_index = 3, .instance_count = 1,
      };
      struct pipe_draw_start_count_bias sc = { .start = 0, .count = 4 };
      struct pipe_scissor_state scissor = { 0, 0, (uint16_t)MIN2(w, 65535u), (uint16_t)MIN2(h, 65535u) };

      pipe->clear(pipe, PIPE_CLEAR_COLOR0, NULL, &zero, 0.0, 0);        /* every pixel is written: nothing is read back in first */
      pipe->set_scissor_states(pipe, 0, 1, &scissor);
      pipe->draw_vbo(pipe, &info, 0, NULL, &sc, 1);
   }
   pipe->set_constant_buffer(pipe, PIPE_SHADER_FRAGMENT, 0, false, NULL);
   pipe->set_sampler_views(pipe, PIPE_SHADER_FRAGMENT, 0, 0, count, false, NULL);
   pipe->bind_sampler_states(pipe, PIPE_SHADER_FRAGMENT, 0, count, none);
   pipe->set_framebuffer_state(pipe, &(struct pipe_framebuffer_state){ 0 });
   return 0;
}

/* ---- what a context keeps --------------------------------------------------------------------------------------------------------- */

static struct rwa_texture *
isp_texture(struct rwa_device *device, unsigned format, unsigned width, unsigned height, unsigned usage, char *why, size_t why_size)
{
   const struct rwa_texture_shape shape = { .pixel_format = format, .type = RWA_TYPE_2D, .width = width, .height = height, .depth = 1,
                                            .array_length = 1, .mip_levels = 1, .sample_count = 1, .usage = usage };

   return texture_of_shape(device, &shape, NULL, why, why_size);
}

static int
isp_table(struct rwa_device *device, struct rwa_texture **texture, uint32_t *have, unsigned format, uint32_t n, const void *values,
          size_t stride, char *why, size_t why_size)
{
   const unsigned origin[3] = { 0, 0, 0 }, size[3] = { n, 1, 1 };

   if (*texture != NULL && have != NULL && *have != n) {
      rwa_texture_destroy(*texture);
      *texture = NULL;
   }
   if (*texture == NULL) {
      *texture = isp_texture(device, format, n, 1, RWA_USAGE_SHADER_READ, why, why_size);
      if (*texture == NULL)
         return RWA_ISP_E_GPU;
      if (have != NULL)
         *have = n;
   }
   return rwa_texture_write(*texture, 0, 0, origin, size, values, stride, why, why_size) == 0 ? RWA_ISP_OK : RWA_ISP_E_GPU;
}


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

/* the frame as texture 0. *zero_copy: it is read where the caller put it. lock held. */
static struct rwa_texture *
isp_frame_texture(struct rwa_isp *isp, const struct rwa_isp_frame *f, bool *zero_copy, int *error, char *why, size_t why_size)
{
   struct rwa_device *device = isp->device;
   const size_t bytes = (size_t)f->stride * f->height;
   const unsigned origin[3] = { 0, 0, 0 };

   *zero_copy = false;
   *error = RWA_ISP_E_GPU;
   if (!device->software && !isp->no_buffer_textures) {
      int slot = -1;
      size_t offset = 0, row = f->stride;

      /* in memory rwa_isp_frame_memory gave, where the GPU can lay a texture over it as it is? */
      for (int i = 0; i < ISP_SLOTS; i++) {
         if (isp->slot[i].buffer == NULL)
            continue;
         const uint8_t *first = (const uint8_t *)isp->slot[i].buffer->map + isp->slot[i].pad + isp->slot[i].header_bytes;

         if (f->raw == first && bytes <= isp->slot[i].frame_bytes && f->stride % 64 == 0) {
            slot = i;
            offset = isp->slot[i].pad + isp->slot[i].header_bytes;
            *zero_copy = true;
         }
      }
      if (slot < 0) {
         /* copied into the context's own buffer, each line at a row length the GPU takes (a multiple of 64) */
         slot = ISP_SLOTS;
         row = ALIGN_POT(f->stride, 64);
         if (isp->slot[slot].buffer != NULL && isp->slot[slot].frame_bytes < row * f->height) {
            if (isp->slot[slot].texture != NULL)
               rwa_texture_destroy(isp->slot[slot].texture);
            rwa_buffer_destroy(isp->slot[slot].buffer);
            memset(&isp->slot[slot], 0, sizeof(isp->slot[slot]));
         }
         if (isp->slot[slot].buffer == NULL) {
            isp->slot[slot].buffer = rwa_buffer_create(device, row * f->height, why, why_size);
            if (isp->slot[slot].buffer == NULL || isp->slot[slot].buffer->map == NULL) {
               if (isp->slot[slot].buffer != NULL)
                  rwa_buffer_destroy(isp->slot[slot].buffer);
               isp->slot[slot].buffer = NULL;
               *error = RWA_ISP_E_MEMORY;
               return NULL;
            }
            isp->slot[slot].frame_bytes = row * f->height;
         }
         if (row == f->stride) {
            memcpy(isp->slot[slot].buffer->map, f->raw, bytes);
         } else {
            for (uint32_t y = 0; y < f->height; y++)
               memcpy((uint8_t *)isp->slot[slot].buffer->map + (size_t)y * row, f->raw + (size_t)y * f->stride, f->stride);
         }
      }
      if (isp->slot[slot].texture != NULL && (isp->slot[slot].stride != row || isp->slot[slot].height != f->height)) {
         rwa_texture_destroy(isp->slot[slot].texture);
         isp->slot[slot].texture = NULL;
      }
      if (isp->slot[slot].texture == NULL) {
         const struct rwa_texture_shape shape = { .pixel_format = 13 /* R8Uint */, .type = RWA_TYPE_2D, .width = (unsigned)row,
                                                  .height = f->height, .depth = 1, .array_length = 1, .mip_levels = 1,
                                                  .sample_count = 1, .usage = RWA_USAGE_SHADER_READ };

         isp->slot[slot].texture = rwa_texture_from_buffer(isp->slot[slot].buffer, &shape, offset, row, why, why_size);
         isp->slot[slot].stride = (uint32_t)row;
         isp->slot[slot].height = f->height;
      }
      if (isp->slot[slot].texture != NULL)
         return isp->slot[slot].texture;
      /* this GPU lays no texture over a buffer: an ordinary texture from here on (said once, by the caller's log of `why') */
      isp->no_buffer_textures = true;
      *zero_copy = false;
   }
   if (isp->raw_own != NULL && (isp->raw_own_w != f->stride || isp->raw_own_h != f->height)) {
      rwa_texture_destroy(isp->raw_own);
      isp->raw_own = NULL;
   }
   if (isp->raw_own == NULL) {
      isp->raw_own = isp_texture(device, 13 /* R8Uint */, f->stride, f->height, RWA_USAGE_SHADER_READ, why, why_size);
      if (isp->raw_own == NULL)
         return NULL;
      isp->raw_own_w = f->stride;
      isp->raw_own_h = f->height;
   }
   const unsigned size[3] = { f->stride, f->height, 1 };

   if (rwa_texture_write(isp->raw_own, 0, 0, origin, size, f->raw, f->stride, why, why_size) != 0)
      return NULL;
   return isp->raw_own;
}

/* ---- the render, on textures ------------------------------------------------------------------------------------------------------ */

static int
isp_check(const struct rwa_isp_frame *f, const struct rwa_isp_params *p, uint32_t width, uint32_t height, char *why, size_t why_size)
{
   if (f == NULL || p == NULL || f->size < sizeof(*f) || p->size < RWA_ISP_PARAMS_SIZE_1 || f->raw == NULL || p->gamma == NULL) {
      say(why, why_size, "the camera's GPU render was given no frame, no parameters, or a struct smaller than this library's");
      return RWA_ISP_E_ARGUMENT;
   }
   if (f->width < 4 || f->height < 4 || f->width > 8192 || f->height > 8192 || (f->width | f->height) & 1 ||
       f->stride < (f->width + 3) / 4 * 5 || width == 0 || height == 0 || width > 8192 || height > 8192) {
      say(why, why_size, "a %ux%u frame of %u bytes a line into a %ux%u picture: sizes this render does not take (4x4 to 8192x8192, "
                         "even, five bytes for four pixels)", f->width, f->height, f->stride, width, height);
      return RWA_ISP_E_ARGUMENT;
   }
   if (f->format != 1 && f->format != 4) {
      say(why, why_size, "frame format %u is neither MIPI RAW10 (1) nor the 40-bit words (4)", f->format);
      return RWA_ISP_E_UNSUPPORTED;
   }
   if (f->stride > 16384) {
      say(why, why_size, "a frame of %u bytes a line is wider than a texture", f->stride);
      return RWA_ISP_E_UNSUPPORTED;
   }
   if (p->lens_shading) {
      say(why, why_size, "the lens shading table is not done on the GPU");
      return RWA_ISP_E_UNSUPPORTED;
   }
   if (p->mode != RWA_ISP_MODE_CELLS && p->mode != RWA_ISP_MODE_FULL) {
      say(why, why_size, "mode %u is neither RWA_ISP_MODE_CELLS nor RWA_ISP_MODE_FULL", p->mode);
      return RWA_ISP_E_UNSUPPORTED;
   }
   if (p->mode == RWA_ISP_MODE_FULL && (width != f->width || height != f->height)) {
      say(why, why_size, "RWA_ISP_MODE_FULL makes a picture of the frame's own size (%ux%u), not %ux%u", f->width, f->height, width,
          height);
      return RWA_ISP_E_UNSUPPORTED;
   }
   return RWA_ISP_OK;
}

/* Everything but the wait: the three passes queued and submitted, *fence what to wait for. Takes the library's lock. */
static int
isp_render_locked(struct rwa_isp *isp, const struct rwa_isp_frame *f, const struct rwa_isp_params *p, struct rwa_texture *luma,
                  struct rwa_texture *chroma, uint32_t width, uint32_t height, struct pipe_fence_handle **fence,
                  struct rwa_isp_result *result, char *why, size_t why_size)
{
   RWA_GUARD();
   struct rwa_device *device = isp->device;
   uint64_t t0 = os_time_get_nano();
   struct isp_block block;
   bool zero_copy = false;
   int error = isp_check(f, p, width, height, why, why_size);

   *fence = NULL;
   if (error != RWA_ISP_OK)
      return error;
   if (luma == NULL || chroma == NULL || luma->device != device || chroma->device != device || luma->res->width0 != width ||
       luma->res->height0 != height || chroma->res->width0 != (width + 1) / 2 || chroma->res->height0 != (height + 1) / 2) {
      say(why, why_size, "the planes given for a %ux%u picture are not %ux%u and %ux%u textures of the camera's device", width, height,
          width, height, (width + 1) / 2, (height + 1) / 2);
      return RWA_ISP_E_SURFACE;
   }

   /* the fixed state (the YCbCr pass's: the quad, its vertex stage, no blending, no depth), the sampler, the shaders */
   if (!ycbcr_state(device, why, why_size))
      return RWA_ISP_E_GPU;
   if (device->isp.nearest == NULL) {
      device->isp.nearest = device->pipe->create_sampler_state(device->pipe,
         &(struct pipe_sampler_state){
            .wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE, .wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE, .wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
            .min_img_filter = PIPE_TEX_FILTER_NEAREST, .mag_img_filter = PIPE_TEX_FILTER_NEAREST,
            .min_mip_filter = PIPE_TEX_MIPFILTER_NONE });
      if (device->isp.nearest == NULL) {
         say(why, why_size, "%s would not make the camera passes' sampler", rwa_device_name(device));
         return RWA_ISP_E_GPU;
      }
   }
   const unsigned variant = f->format == 4;
   void **slot = p->mode == RWA_ISP_MODE_FULL ? &device->isp.full[variant] : &device->isp.cells[variant];
   void *fs = *slot != NULL ? *slot
            : isp_shader(device, slot, p->mode == RWA_ISP_MODE_FULL ? isp_full_shader(device->nir_options, f->format)
                                                                     : isp_cells_shader(device->nir_options, f->format), why, why_size);
   void *fs_luma = device->isp.luma != NULL ? device->isp.luma
                 : isp_shader(device, &device->isp.luma, isp_luma_shader(device->nir_options), why, why_size);
   void *fs_chroma = device->isp.chroma != NULL ? device->isp.chroma
                   : isp_shader(device, &device->isp.chroma, isp_chroma_shader(device->nir_options), why, why_size);

   if (fs == NULL || fs_luma == NULL || fs_chroma == NULL)
      return RWA_ISP_E_GPU;

   /* the tables: where each column and row of the picture reads the sensor, when anything that decides it has changed */
   uint32_t zoom_bits;

   memcpy(&zoom_bits, &p->zoom, sizeof(zoom_bits));
   if (!isp->map_key.valid || isp->map_key.fw != f->width || isp->map_key.fh != f->height || isp->map_key.ow != width ||
       isp->map_key.oh != height || isp->map_key.flip != p->flip || isp->map_key.zoom_bits != zoom_bits ||
       isp->map_key.mode != p->mode) {
      uint16_t *columns = malloc(sizeof(uint16_t) * ((size_t)width + height)), *rows = columns ? columns + width : NULL;
      int shade_off = 0;

      if (columns == NULL) {
         say(why, why_size, "out of memory for the camera render's tables");
         return RWA_ISP_E_MEMORY;
      }
      isp_positions(f, p, width, height, columns, rows, &shade_off);
      isp->map_key.valid = false;
      error = isp_table(device, &isp->columns, &isp->columns_n, 23 /* R16Uint */, width, columns, width * 2, why, why_size);
      if (error == RWA_ISP_OK)
         error = isp_table(device, &isp->rows, &isp->rows_n, 23, height, rows, height * 2, why, why_size);
      free(columns);
      if (error != RWA_ISP_OK)
         return error;
      isp->map_key = (__typeof__(isp->map_key)){ .fw = f->width, .fh = f->height, .ow = width, .oh = height, .flip = p->flip,
                                             .zoom_bits = zoom_bits, .mode = p->mode, .valid = true, .shade_off = shade_off };
   }
   if (!isp->gamma_valid || memcmp(isp->gamma_now, p->gamma, sizeof(isp->gamma_now)) != 0) {
      isp->gamma_valid = false;
      error = isp_table(device, &isp->gamma, NULL, 13 /* R8Uint */, 4096, p->gamma, 4096, why, why_size);
      if (error != RWA_ISP_OK)
         return error;
      memcpy(isp->gamma_now, p->gamma, sizeof(isp->gamma_now));
      isp->gamma_valid = true;
   }
   /* the intermediate picture */
   if (isp->rgb != NULL && (isp->rgb_w != width || isp->rgb_h != height)) {
      rwa_texture_destroy(isp->rgb);
      isp->rgb = NULL;
   }
   if (isp->rgb == NULL) {
      isp->rgb = isp_texture(device, 70 /* RGBA8Unorm */, width, height, RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET, why, why_size);
      if (isp->rgb == NULL)
         return RWA_ISP_E_MEMORY;
      isp->rgb_w = width;
      isp->rgb_h = height;
   }
   struct rwa_texture *frame = isp_frame_texture(isp, f, &zero_copy, &error, why, why_size);

   if (frame == NULL)
      return error;
   isp_block_fill(&block, f, p, width, height, isp->map_key.shade_off);
   uint64_t t1 = os_time_get_nano();

   struct rwa_texture *const isp_textures[4] = { frame, isp->columns, isp->rows, isp->gamma };
   struct rwa_texture *const rgb_texture[1] = { isp->rgb };

   if (isp_pass(device, isp->rgb, fs, isp_textures, 4, &block, "ISP", why, why_size) != 0 ||
       isp_nr_planes(isp, p, luma, chroma, fs_luma, fs_chroma, rgb_texture, &block, width, height, why, why_size) != 0)
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
   device->pipe->flush(device->pipe, fence, 0);
   if (isp->rgb != NULL && (uint64_t)width * height * 4 > ISP_KEEP_BYTES) {
      rwa_texture_destroy(isp->rgb);          /* a still's: the queued passes hold what they use */
      isp->rgb = NULL;
   }
   if (result != NULL && result->size >= RWA_ISP_RESULT_SIZE_1) {
      result->flags = zero_copy ? RWA_ISP_DID_ZERO_COPY : 0;
      result->prepare_ns = t1 - t0;
      result->queue_ns = os_time_get_nano() - t1;
      result->wait_ns = 0;
   }
   return RWA_ISP_OK;
}

static int
isp_fence_wait(struct rwa_isp *isp, struct pipe_fence_handle *fence, char *why, size_t why_size)
{
   struct pipe_screen *screen = isp->device->screen;

   if (fence == NULL)
      return RWA_ISP_OK;
   bool done = screen->fence_finish(screen, NULL, fence, RWA_FENCE_TIMEOUT_NS);

   screen->fence_reference(screen, &fence, NULL);
   if (!done) {
      say(why, why_size, "the GPU did not finish the camera's passes within two seconds");
      return RWA_ISP_E_GPU;
   }
   return RWA_ISP_OK;
}

/* waits (outside the lock), or keeps the fence for rwa_isp_wait */
static int
isp_finish(struct rwa_isp *isp, struct pipe_fence_handle *fence, bool async, struct rwa_isp_result *result, char *why, size_t why_size)
{
   if (async) {
      struct pipe_fence_handle *oldest = NULL;

      isp->stats_out = NULL;                   /* (y2s65: no statistics without the wait) */
      if (result != NULL && result->size >= sizeof(*result))
         result->stats_w = result->stats_h = result->stats_step = 0;

      pthread_mutex_lock(&rwa_lock);
      if (isp->pendings == ISP_PENDING) {
         oldest = isp->pending[0];
         memmove(isp->pending, isp->pending + 1, sizeof(isp->pending[0]) * (ISP_PENDING - 1));
         isp->pendings--;
      }
      isp->pending[isp->pendings++] = fence;
      pthread_mutex_unlock(&rwa_lock);
      return isp_fence_wait(isp, oldest, why, why_size);
   }
   uint64_t t = os_time_get_nano();
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
}

/* For tests and for a caller with textures of its own (not in rwa_isp_api.h): the picture into `luma' (R8Unorm, width x height)
 * and `chroma' (RG8Unorm, half that each way, rounded up), both made with RWA_USAGE_RENDER_TARGET on the context's device. */
int
rwa_isp_render_textures(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
                        struct rwa_texture *luma, struct rwa_texture *chroma, uint32_t width, uint32_t height, uint32_t flags,
                        struct rwa_isp_result *result, char *why, size_t why_size)
{
   struct pipe_fence_handle *fence = NULL;

   if (isp == NULL) {
      say(why, why_size, "no camera render context");
      return RWA_ISP_E_ARGUMENT;
   }
   int error = isp_render_locked(isp, frame, params, luma, chroma, width, height, &fence, result, why, why_size);

   return error != RWA_ISP_OK ? error : isp_finish(isp, fence, flags & RWA_ISP_OUT_ASYNC, result, why, why_size);
}

/* y2s65, for tests (not in rwa_isp_api.h): the next rwa_isp_render_textures also makes the statistics into `stats' */
void rwa_isp_want_stats(struct rwa_isp *isp, float *stats, uint32_t floats);
void
rwa_isp_want_stats(struct rwa_isp *isp, float *stats, uint32_t floats)
{
   if (isp != NULL) {
      isp->stats_out = stats;
      isp->stats_floats = stats != NULL ? floats : 0;
   }
}

/* the device the contexts share, for a caller of rwa_isp_render_textures to make its textures on */
struct rwa_device *
rwa_isp_device(struct rwa_isp *isp)
{
   return isp ? isp->device : NULL;
}

/* ---- the output surface ----------------------------------------------------------------------------------------------------------- */

static struct {
   pthread_once_t once;
   bool usable;
   uint32_t (*id)(void *), (*format)(void *);
   size_t (*planes)(void *), (*width)(void *), (*height)(void *), (*alloc)(void *);
   size_t (*plane_row)(void *, size_t), (*plane_width)(void *, size_t), (*plane_height)(void *, size_t);
   void *(*base)(void *), *(*plane_base)(void *, size_t);
} isp_ios = { .once = PTHREAD_ONCE_INIT };

static void
isp_ios_load(void)
{
   /* looked up, not linked: this library is Mesa and knows no IOSurface; the process that hands it one has the framework loaded */
   *(void **)&isp_ios.id = dlsym(RTLD_DEFAULT, "IOSurfaceGetID");
   *(void **)&isp_ios.format = dlsym(RTLD_DEFAULT, "IOSurfaceGetPixelFormat");
   *(void **)&isp_ios.planes = dlsym(RTLD_DEFAULT, "IOSurfaceGetPlaneCount");
   *(void **)&isp_ios.width = dlsym(RTLD_DEFAULT, "IOSurfaceGetWidth");
   *(void **)&isp_ios.height = dlsym(RTLD_DEFAULT, "IOSurfaceGetHeight");
   *(void **)&isp_ios.alloc = dlsym(RTLD_DEFAULT, "IOSurfaceGetAllocSize");
   *(void **)&isp_ios.plane_row = dlsym(RTLD_DEFAULT, "IOSurfaceGetBytesPerRowOfPlane");
   *(void **)&isp_ios.plane_width = dlsym(RTLD_DEFAULT, "IOSurfaceGetWidthOfPlane");
   *(void **)&isp_ios.plane_height = dlsym(RTLD_DEFAULT, "IOSurfaceGetHeightOfPlane");
   *(void **)&isp_ios.base = dlsym(RTLD_DEFAULT, "IOSurfaceGetBaseAddress");
   *(void **)&isp_ios.plane_base = dlsym(RTLD_DEFAULT, "IOSurfaceGetBaseAddressOfPlane");
   isp_ios.usable = isp_ios.id && isp_ios.format && isp_ios.planes && isp_ios.width && isp_ios.height && isp_ios.alloc &&
                    isp_ios.plane_row && isp_ios.plane_width && isp_ios.plane_height && isp_ios.base && isp_ios.plane_base;
}

static void
isp_surface_drop(struct isp_surface *s)
{
   if (s->luma != NULL)
      rwa_texture_destroy(s->luma);
   if (s->chroma != NULL)
      rwa_texture_destroy(s->chroma);
   memset(s, 0, sizeof(*s));
}

/* The surface's two planes as textures the GPU renders to in place, kept by its ID. Takes the lock. */
static int
isp_surface_planes(struct rwa_isp *isp, const struct rwa_isp_output *out, uint32_t video_range, struct rwa_texture **luma,
                   struct rwa_texture **chroma, bool *imported, char *why, size_t why_size)
{
   RWA_GUARD();
   void *s = out->iosurface;

   *imported = false;
   pthread_once(&isp_ios.once, isp_ios_load);
   if (!isp_ios.usable) {
      say(why, why_size, "this process has no IOSurface framework loaded, so a surface cannot be described to the GPU");
      return RWA_ISP_E_SURFACE;
   }
   uint32_t id = isp_ios.id(s), fourcc = isp_ios.format(s);
   size_t width = isp_ios.width(s), height = isp_ios.height(s), alloc = isp_ios.alloc(s);
   const uint32_t want = video_range ? 0x34323076u /* '420v' */ : 0x34323066u /* '420f' */;

   if (id == 0 || fourcc != want || isp_ios.planes(s) != 2 || width != out->width || height != out->height) {
      say(why, why_size, "the output surface (ID %u) is %zux%zu '%c%c%c%c' in %zu planes; the render was asked for %ux%u '%s' in two",
          id, width, height, (char)(fourcc >> 24), (char)(fourcc >> 16), (char)(fourcc >> 8), (char)fourcc, isp_ios.planes(s),
          out->width, out->height, video_range ? "420v" : "420f");
      return RWA_ISP_E_SURFACE;
   }
   const uint8_t *base = isp_ios.base(s), *plane[2] = { isp_ios.plane_base(s, 0), isp_ios.plane_base(s, 1) };
   const size_t row[2] = { isp_ios.plane_row(s, 0), isp_ios.plane_row(s, 1) };
   const size_t pw[2] = { width, (width + 1) / 2 }, ph[2] = { height, (height + 1) / 2 }, texel[2] = { 1, 2 };
   size_t offset[2] = { 0, 0 };

   for (int i = 0; i < 2; i++) {
      offset[i] = base != NULL && plane[i] != NULL && plane[i] >= base ? (size_t)(plane[i] - base) : SIZE_MAX;
      if (offset[i] == SIZE_MAX || offset[i] % 64 != 0 || row[i] % 64 != 0 || row[i] < pw[i] * texel[i] || offset[i] > alloc ||
          row[i] * (ph[i] - 1) + pw[i] * texel[i] > alloc - offset[i] || offset[i] > UINT32_MAX || row[i] > UINT32_MAX ||
          (i == 1 && (isp_ios.plane_width(s, 1) < pw[1] || isp_ios.plane_height(s, 1) < ph[1]))) {
         say(why, why_size, "the output surface's %s plane (%zu bytes a row from offset %zu of %zu bytes) cannot be written in place: "
                            "rows and offsets must be multiples of 64 and hold %zux%zu", i ? "chroma" : "luma", row[i],
             offset[i] == SIZE_MAX ? (size_t)0 : offset[i], alloc, pw[i], ph[i]);
         return RWA_ISP_E_SURFACE;
      }
   }
   struct isp_surface *entry = NULL, *oldest = &isp->surface[0];

   for (int i = 0; i < ISP_SURFACES; i++) {
      struct isp_surface *e = &isp->surface[i];

      if (e->luma != NULL && e->id == id) {
         if (e->base == base && e->width == width && e->height == height && e->offset[0] == offset[0] && e->offset[1] == offset[1] &&
             e->row[0] == row[0] && e->row[1] == row[1])
            entry = e;
         else
            isp_surface_drop(e);        /* the ID is another surface's now */
      }
      if (e->used < oldest->used)
         oldest = e;
   }
   if (entry == NULL) {
      const struct rwa_texture_shape shape[2] = {
         { .pixel_format = 10 /* R8Unorm */, .type = RWA_TYPE_2D, .width = (unsigned)pw[0], .height = (unsigned)ph[0], .depth = 1,
           .array_length = 1, .mip_levels = 1, .sample_count = 1, .usage = RWA_USAGE_RENDER_TARGET },
         { .pixel_format = 30 /* RG8Unorm */, .type = RWA_TYPE_2D, .width = (unsigned)pw[1], .height = (unsigned)ph[1], .depth = 1,
           .array_length = 1, .mip_levels = 1, .sample_count = 1, .usage = RWA_USAGE_RENDER_TARGET },
      };

      isp_surface_drop(oldest);
      entry = oldest;
      entry->luma = rwa_texture_from_surface(isp->device, &shape[0], (int)id, (unsigned)offset[0], (unsigned)row[0], why, why_size);
      if (entry->luma != NULL)
         entry->chroma = rwa_texture_from_surface(isp->device, &shape[1], (int)id, (unsigned)offset[1], (unsigned)row[1], why, why_size);
      if (entry->luma == NULL || entry->chroma == NULL) {
         isp_surface_drop(entry);
         return RWA_ISP_E_SURFACE;
      }
      entry->id = id;
      entry->base = base;
      entry->width = (uint32_t)width;
      entry->height = (uint32_t)height;
      for (int i = 0; i < 2; i++) {
         entry->offset[i] = (uint32_t)offset[i];
         entry->row[i] = (uint32_t)row[i];
      }
      *imported = true;
   }
   entry->used = ++isp->clock;
   *luma = entry->luma;
   *chroma = entry->chroma;
   return RWA_ISP_OK;
}

/* ---- the calls of rwa_isp_api.h --------------------------------------------------------------------------------------------------- */

unsigned
rwa_isp_api_version(void)
{
   return RWA_ISP_API_VERSION;
}

struct rwa_isp *
rwa_isp_create(char *why, size_t why_size)
{
   RWA_GUARD();
   if (isp_device == NULL) {
      isp_device = rwa_device_open(why, why_size);
      if (isp_device == NULL)
         return NULL;
   }
   struct rwa_isp *isp = calloc(1, sizeof(*isp));

   if (isp == NULL) {
      say(why, why_size, "out of memory making a camera render context");
      return NULL;
   }
   isp->device = isp_device;
   return isp;
}

int
rwa_isp_wait(struct rwa_isp *isp, char *why, size_t why_size)
{
   struct pipe_fence_handle *pending[ISP_PENDING];
   unsigned n;
   int error = RWA_ISP_OK;

   if (isp == NULL)
      return RWA_ISP_E_ARGUMENT;
   pthread_once(&rwa_lock_once, rwa_lock_init);
   pthread_mutex_lock(&rwa_lock);
   n = isp->pendings;
   memcpy(pending, isp->pending, sizeof(pending[0]) * n);
   isp->pendings = 0;
   pthread_mutex_unlock(&rwa_lock);
   for (unsigned i = 0; i < n; i++) {
      int e = isp_fence_wait(isp, pending[i], why, why_size);

      error = error != RWA_ISP_OK ? error : e;
   }
   return error;
}

void
rwa_isp_forget_surfaces(struct rwa_isp *isp)
{
   RWA_GUARD();
   for (int i = 0; isp != NULL && i < ISP_SURFACES; i++)
      isp_surface_drop(&isp->surface[i]);
}

void
rwa_isp_destroy(struct rwa_isp *isp)
{
   if (isp == NULL)
      return;
   (void)rwa_isp_wait(isp, NULL, 0);
   RWA_GUARD();
   for (int i = 0; i < ISP_SURFACES; i++)
      isp_surface_drop(&isp->surface[i]);
   for (int i = 0; i <= ISP_SLOTS; i++) {
      if (isp->slot[i].texture != NULL)
         rwa_texture_destroy(isp->slot[i].texture);
      if (isp->slot[i].buffer != NULL)
         rwa_buffer_destroy(isp->slot[i].buffer);
   }
   struct rwa_texture *own[17] = { isp->raw_own, isp->columns, isp->rows, isp->gamma, isp->rgb, isp->stats, isp->nr_y0, isp->nr_y1, isp->nr_c0,
                                   isp->nr_c1, isp->nr_c2, isp->nr_by, isp->nr_bc, isp->nr_by2, isp->nr_bc2, isp->nr_diff, isp->nr_moves };

   for (int i = 0; i < 17; i++)
      if (own[i] != NULL)
         rwa_texture_destroy(own[i]);
   free(isp);
}

void *
rwa_isp_frame_memory(struct rwa_isp *isp, unsigned slot, size_t header_bytes, size_t frame_bytes, char *why, size_t why_size)
{
   RWA_GUARD();
   if (isp == NULL || slot >= ISP_SLOTS || frame_bytes == 0 || header_bytes > 65536) {
      say(why, why_size, "frame memory: slot %u of %u, a header of %zu bytes and a frame of %zu", slot, ISP_SLOTS, header_bytes,
          frame_bytes);
      return NULL;
   }
   if (isp->device->software) {
      say(why, why_size, "%s reads no frame in place: frames are uploaded", rwa_device_name(isp->device));
      return NULL;
   }
   /* the frame's first byte at a multiple of 64 in the buffer, which is where the GPU can begin a texture */
   const size_t pad = (64 - header_bytes % 64) % 64;

   if (isp->slot[slot].buffer != NULL && isp->slot[slot].header_bytes == header_bytes && isp->slot[slot].frame_bytes == frame_bytes)
      return (uint8_t *)isp->slot[slot].buffer->map + pad;
   if (isp->slot[slot].texture != NULL)
      rwa_texture_destroy(isp->slot[slot].texture);
   if (isp->slot[slot].buffer != NULL)
      rwa_buffer_destroy(isp->slot[slot].buffer);
   memset(&isp->slot[slot], 0, sizeof(isp->slot[slot]));
   isp->slot[slot].buffer = rwa_buffer_create(isp->device, pad + header_bytes + frame_bytes, why, why_size);
   if (isp->slot[slot].buffer == NULL)
      return NULL;
   if (isp->slot[slot].buffer->map == NULL) {
      say(why, why_size, "%s gave a buffer the processor cannot write", rwa_device_name(isp->device));
      rwa_buffer_destroy(isp->slot[slot].buffer);
      isp->slot[slot].buffer = NULL;
      return NULL;
   }
   isp->slot[slot].header_bytes = header_bytes;
   isp->slot[slot].frame_bytes = frame_bytes;
   isp->slot[slot].pad = pad;
   return (uint8_t *)isp->slot[slot].buffer->map + pad;
}

int
rwa_isp_render(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
               const struct rwa_isp_output *output, struct rwa_isp_result *result, char *why, size_t why_size)
{
   struct rwa_texture *luma = NULL, *chroma = NULL;
   struct pipe_fence_handle *fence = NULL;
   bool imported = false;

   if (isp == NULL || output == NULL || output->size < RWA_ISP_OUTPUT_SIZE_1 || output->iosurface == NULL || params == NULL ||
       params->size < RWA_ISP_PARAMS_SIZE_1) {
      say(why, why_size, "the camera's GPU render was given no context, no output surface, or a struct smaller than this library's");
      return RWA_ISP_E_ARGUMENT;
   }
   int error;

   {
      /* the planes and the passes that write them under ONE hold of the lock: nothing can let the planes go in between */
      RWA_GUARD();
      uint64_t t = os_time_get_nano();

      /* y2s65: the statistics, when the caller's struct has their fields and gave room */
      isp->stats_out = output->size >= sizeof(*output) && output->stats != NULL && !(output->flags & RWA_ISP_OUT_ASYNC) ? output->stats : NULL;
      isp->stats_floats = isp->stats_out != NULL ? output->stats_floats : 0;
      error = isp_surface_planes(isp, output, params->video_range, &luma, &chroma, &imported, why, why_size);
      t = os_time_get_nano() - t;
      if (error == RWA_ISP_OK)
         error = isp_render_locked(isp, frame, params, luma, chroma, output->width, output->height, &fence, result, why, why_size);
      if (error == RWA_ISP_OK && result != NULL && result->size >= RWA_ISP_RESULT_SIZE_1) {
         result->prepare_ns += t;
         result->flags |= imported ? RWA_ISP_DID_IMPORT : 0;
      }
   }
   if (error != RWA_ISP_OK)
      return error;
   return isp_finish(isp, fence, output->flags & RWA_ISP_OUT_ASYNC, result, why, why_size);
}
