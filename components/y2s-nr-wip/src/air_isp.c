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
   /* y2s46, the noise passes: what a block may differ by (quarters of a level), near and keep of the plane being blended, 1 when
    * every block moves, the blocks across and down */
   int32_t nr_limit, nr_near, nr_keep, nr_all, nr_bw, nr_bh, pad2[2];      /* 52 */
};
enum {
   ISP_U_BLACK = 0, ISP_U_WQ, ISP_U_KNEE, ISP_U_SPAN, ISP_U_GQ, ISP_U_VQ = 7, ISP_U_Q = 8, ISP_U_MID_X = 17, ISP_U_MID_Y,
   ISP_U_R2K, ISP_U_SHADE_OFF, ISP_U_R_PLACE, ISP_U_B_PLACE, ISP_U_RAW_W, ISP_U_RAW_H, ISP_U_OUT_W, ISP_U_OUT_H,
   ISP_U_IDENTITY, ISP_U_GAIN = 28, ISP_U_M = 32, ISP_U_K = 41, ISP_U_OY = 50,
   ISP_U_NR_LIMIT = 52, ISP_U_NR_NEAR, ISP_U_NR_KEEP, ISP_U_NR_ALL, ISP_U_NR_BW, ISP_U_NR_BH,
};
_Static_assert(sizeof(struct isp_block) == 60 * 4 && offsetof(struct isp_block, nr_limit) == ISP_U_NR_LIMIT * 4 && offsetof(struct isp_block, m) == ISP_U_M * 4 &&
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
   /* y2s46, noise: the picture as made (nr[0] brightness, nr[1] colours), smoothed (nr[2], nr[3]), each block's difference (nr[4]),
    * and the picture before, two of each to draw from one into the other (nr[5 + 2 * which + plane]) */
   struct rwa_texture *nr[9];
   uint32_t nr_w, nr_h, nr_which;
   bool nr_before;                              /* nr[5 + 2 * nr_which ...] is the picture before of an nr_w x nr_h picture */
   uint32_t columns_n, rows_n, rgb_w, rgb_h;
   struct { uint32_t fw, fh, ow, oh, flip, zoom_bits, mode; bool valid; int shade_off; } map_key;
   uint8_t gamma_now[4096];
   bool gamma_valid;
   struct isp_surface surface[ISP_SURFACES];
   uint64_t clock;
   struct pipe_fence_handle *pending[ISP_PENDING];
   unsigned pendings;
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

static void
isp_state_destroy(struct rwa_device *device)
{
   struct pipe_context *pipe = device->pipe;
   void *fs[12] = { device->isp.cells[0], device->isp.cells[1], device->isp.full[0], device->isp.full[1], device->isp.luma,
                    device->isp.chroma, device->isp.nr_chroma[0], device->isp.nr_chroma[1], device->isp.nr_luma, device->isp.nr_diff,
                    device->isp.nr_blend[0], device->isp.nr_blend[1] };
   bool any = device->isp.nearest != NULL;

   for (unsigned i = 0; i < 12; i++)
      any = any || fs[i] != NULL;
   if (any)
      pipe->bind_fs_state(pipe, NULL);
   for (unsigned i = 0; i < 12; i++)
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

/* ---- noise (y2s46): rwcam_render.c's smooth() and with_before() as passes -------------------------------------------------------- */
/*
 * The camera bundle cleans a picture taken with much gain on the processor, after either back end made it (rwcam_render.c denoise()):
 * 4 to 9 ms a viewfinder picture there. Here the same arithmetic, in whole numbers, as fragment passes, so that the two give the same
 * bytes (isp_gpu_host_test compares them):
 *   smooth()        each sample (v + ^ + < + > + 4 * itself + 4) >> 3, the plane's outermost samples as they are; the colours once at
 *                   level 1 and twice at level 2 (one pass that works the first smoothing out at the five places the second reads);
 *                   the brightness at level 2 only, and only where the smoothed sample is within 16 of the sample;
 *   with_before()   D of each 8 x 8 block: the mean difference of 16 of its brightness samples from the picture before, in quarters
 *                   of a level (a pass into a texture of one byte a block, which is read back: the noise is the mean D of the calmer
 *                   blocks, a block moves when its D is over 1.7 times that, all of them when more than 35 % do -- sums over the whole
 *                   picture, a few thousand bytes for the processor); then a pass for each plane: where the block and its eight
 *                   neighbours are still and the sample is within `near' of the one before, it goes `keep' of 256 of the way to it.
 *                   The result is the picture AND the picture before of the next one: drawn into a texture the context keeps, and
 *                   again into the surface's plane.
 */
#define ISP_NR_BLOCK 8

static nir_def *
nr_int(nir_builder *b, nir_def *texel, unsigned c)
{
   return nir_f2i32(b, nir_fadd_imm(b, nir_fmul_imm(b, nir_channel(b, texel, c), 255.0), 0.5));
}

/* the fragment's place in an attachment of w x h, and the last place each way */
static void
nr_place(nir_builder *b, nir_variable *uv, nir_def *w, nir_def *h, nir_def **x, nir_def **y)
{
   nir_def *at = nir_load_var(b, uv);

   *x = nir_imin(b, nir_f2i32(b, nir_fmul(b, nir_channel(b, at, 0), nir_i2f32(b, w))), nir_iadd_imm(b, w, -1));
   *y = nir_imin(b, nir_f2i32(b, nir_fmul(b, nir_channel(b, at, 1), nir_i2f32(b, h))), nir_iadd_imm(b, h, -1));
}

static void
nr_store(nir_builder *b, nir_variable *out, nir_def *v0, nir_def *v1)
{
   nir_def *zero = nir_imm_float(b, 0.0f);

   nir_store_var(b, out, nir_vec4(b, nir_fmul_imm(b, nir_i2f32(b, v0), 1.0 / 255.0),
                                  v1 ? nir_fmul_imm(b, nir_i2f32(b, v1), 1.0 / 255.0) : zero, zero, nir_imm_float(b, 1.0f)), 0xf);
}

/* smooth() at (x, y) of texture 0, w x h samples of `comps' bytes: out[]; mid[] (if given): the sample itself */
static void
nr_smooth_at(nir_builder *b, nir_def *x, nir_def *y, nir_def *w, nir_def *h, unsigned comps, nir_def *out[2], nir_def *mid[2])
{
   nir_def *xl = nir_imax(b, nir_iadd_imm(b, x, -1), nir_imm_int(b, 0)), *xr = nir_imin(b, nir_iadd_imm(b, x, 1), nir_iadd_imm(b, w, -1));
   nir_def *yu = nir_imax(b, nir_iadd_imm(b, y, -1), nir_imm_int(b, 0)), *yd = nir_imin(b, nir_iadd_imm(b, y, 1), nir_iadd_imm(b, h, -1));
   /* (an outermost sample: one of its neighbours' places was moved back onto it) */
   nir_def *edge = nir_ior(b, nir_ior(b, nir_ieq(b, xl, x), nir_ieq(b, xr, x)), nir_ior(b, nir_ieq(b, yu, y), nir_ieq(b, yd, y)));
   nir_def *c = isp_fetch(b, 0, x, y, nir_type_float32), *u = isp_fetch(b, 0, x, yu, nir_type_float32);
   nir_def *d = isp_fetch(b, 0, x, yd, nir_type_float32), *l = isp_fetch(b, 0, xl, y, nir_type_float32);
   nir_def *r = isp_fetch(b, 0, xr, y, nir_type_float32);

   for (unsigned i = 0; i < comps; i++) {
      nir_def *here = nr_int(b, c, i);
      nir_def *sum = nir_iadd(b, nir_iadd(b, nr_int(b, u, i), nr_int(b, d, i)), nir_iadd(b, nr_int(b, l, i), nr_int(b, r, i)));

      sum = nir_ishr_imm(b, nir_iadd_imm(b, nir_iadd(b, sum, nir_ishl_imm(b, here, 2)), 4), 3);
      out[i] = nir_bcsel(b, edge, here, sum);
      if (mid)
         mid[i] = here;
   }
}

/* the colours smoothed `passes' times (1, 2); the attachment is the colour plane, half the picture each way */
static nir_shader *
isp_nr_chroma_shader(const struct nir_shader_compiler_options *options, unsigned passes)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_nr_chroma", &uv, &out);
   nir_def *w = nir_ushr_imm(&b, nir_iadd_imm(&b, isp_word(&b, ISP_U_OUT_W), 1), 1);
   nir_def *h = nir_ushr_imm(&b, nir_iadd_imm(&b, isp_word(&b, ISP_U_OUT_H), 1), 1), *x, *y, *v[2];

   nr_place(&b, uv, w, h, &x, &y);
   if (passes == 1) {
      nr_smooth_at(&b, x, y, w, h, 2, v, NULL);
   } else {
      /* the second smoothing of the first: the first worked out here and at the four neighbours (their places kept inside) */
      nir_def *xl = nir_imax(&b, nir_iadd_imm(&b, x, -1), nir_imm_int(&b, 0)), *xr = nir_imin(&b, nir_iadd_imm(&b, x, 1), nir_iadd_imm(&b, w, -1));
      nir_def *yu = nir_imax(&b, nir_iadd_imm(&b, y, -1), nir_imm_int(&b, 0)), *yd = nir_imin(&b, nir_iadd_imm(&b, y, 1), nir_iadd_imm(&b, h, -1));
      nir_def *edge = nir_ior(&b, nir_ior(&b, nir_ieq(&b, xl, x), nir_ieq(&b, xr, x)), nir_ior(&b, nir_ieq(&b, yu, y), nir_ieq(&b, yd, y)));
      nir_def *c[2], *u[2], *d[2], *l[2], *r[2];

      nr_smooth_at(&b, x, y, w, h, 2, c, NULL);
      nr_smooth_at(&b, x, yu, w, h, 2, u, NULL);
      nr_smooth_at(&b, x, yd, w, h, 2, d, NULL);
      nr_smooth_at(&b, xl, y, w, h, 2, l, NULL);
      nr_smooth_at(&b, xr, y, w, h, 2, r, NULL);
      for (unsigned i = 0; i < 2; i++) {
         nir_def *sum = nir_iadd(&b, nir_iadd(&b, u[i], d[i]), nir_iadd(&b, l[i], r[i]));

         sum = nir_ishr_imm(&b, nir_iadd_imm(&b, nir_iadd(&b, sum, nir_ishl_imm(&b, c[i], 2)), 4), 3);
         v[i] = nir_bcsel(&b, edge, c[i], sum);
      }
   }
   nr_store(&b, out, v[0], v[1]);
   return isp_fragment_end(&b, 1);
}

/* the brightness smoothed where the smoothed sample is within 16 of it (level 2) */
static nir_shader *
isp_nr_luma_shader(const struct nir_shader_compiler_options *options)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_nr_luma", &uv, &out);
   nir_def *w = isp_word(&b, ISP_U_OUT_W), *h = isp_word(&b, ISP_U_OUT_H), *x, *y, *v[2], *mid[2];

   nr_place(&b, uv, w, h, &x, &y);
   nr_smooth_at(&b, x, y, w, h, 1, v, mid);
   nr_store(&b, out, nir_bcsel(&b, nir_ilt_imm(&b, nir_iabs(&b, nir_isub(&b, v[0], mid[0])), 16), v[0], mid[0]), NULL);
   return isp_fragment_end(&b, 1);
}

/* D of a block: texture 0 the picture's brightness, texture 1 the one before; the attachment is one texel a block */
static nir_shader *
isp_nr_diff_shader(const struct nir_shader_compiler_options *options)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_nr_diff", &uv, &out);
   nir_def *w = isp_word(&b, ISP_U_OUT_W), *h = isp_word(&b, ISP_U_OUT_H), *bx, *by;
   nir_def *sum = nir_imm_int(&b, 0), *count = nir_imm_int(&b, 0);

   nr_place(&b, uv, isp_word(&b, ISP_U_NR_BW), isp_word(&b, ISP_U_NR_BH), &bx, &by);
   for (int j = 0; j < ISP_NR_BLOCK; j += 2) {
      for (int i = 0; i < ISP_NR_BLOCK; i += 2) {
         nir_def *x = nir_iadd_imm(&b, nir_ishl_imm(&b, bx, 3), i), *y = nir_iadd_imm(&b, nir_ishl_imm(&b, by, 3), j);
         nir_def *in = nir_iand(&b, nir_ilt(&b, x, w), nir_ilt(&b, y, h));
         nir_def *xc = nir_imin(&b, x, nir_iadd_imm(&b, w, -1)), *yc = nir_imin(&b, y, nir_iadd_imm(&b, h, -1));
         nir_def *d = nir_iabs(&b, nir_isub(&b, nr_int(&b, isp_fetch(&b, 0, xc, yc, nir_type_float32), 0),
                                            nr_int(&b, isp_fetch(&b, 1, xc, yc, nir_type_float32), 0)));

         sum = nir_iadd(&b, sum, nir_bcsel(&b, in, d, nir_imm_int(&b, 0)));
         count = nir_iadd(&b, count, nir_b2i32(&b, in));
      }
   }
   /* (a block has its first sample at least: count is 1 or more) */
   nr_store(&b, out, nir_umin(&b, nir_udiv(&b, nir_ishl_imm(&b, sum, 2), nir_imax(&b, count, nir_imm_int(&b, 1))), nir_imm_int(&b, 255)), NULL);
   return isp_fragment_end(&b, 2);
}

/* with_before()'s last step for a plane: texture 0 the picture's plane, 1 the one before, 2 the blocks' D. comps 1: the brightness
 * (a block is 8 samples each way); 2: the colours (4 pairs each way). */
static nir_shader *
isp_nr_blend_shader(const struct nir_shader_compiler_options *options, unsigned comps)
{
   nir_variable *uv, *out;
   nir_builder b = isp_fragment_begin(options, "isp_nr_blend", &uv, &out);
   nir_def *w = isp_word(&b, ISP_U_OUT_W), *h = isp_word(&b, ISP_U_OUT_H), *x, *y, *v[2] = { NULL, NULL };
   nir_def *bw = isp_word(&b, ISP_U_NR_BW), *bh = isp_word(&b, ISP_U_NR_BH), *limit = isp_word(&b, ISP_U_NR_LIMIT);
   nir_def *near = isp_word(&b, ISP_U_NR_NEAR), *keep = isp_word(&b, ISP_U_NR_KEEP);

   if (comps == 2) {
      w = nir_ushr_imm(&b, nir_iadd_imm(&b, w, 1), 1);
      h = nir_ushr_imm(&b, nir_iadd_imm(&b, h, 1), 1);
   }
   nr_place(&b, uv, w, h, &x, &y);
   nir_def *bx = nir_ushr_imm(&b, x, comps == 2 ? 2 : 3), *by = nir_ushr_imm(&b, y, comps == 2 ? 2 : 3);
   nir_def *still = nir_ieq_imm(&b, isp_word(&b, ISP_U_NR_ALL), 0);

   for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
         nir_def *nx = nir_iadd_imm(&b, bx, dx), *ny = nir_iadd_imm(&b, by, dy);
         nir_def *in = nir_iand(&b, nir_iand(&b, nir_ige_imm(&b, nx, 0), nir_ige_imm(&b, ny, 0)), nir_iand(&b, nir_ilt(&b, nx, bw), nir_ilt(&b, ny, bh)));
         nir_def *d = nr_int(&b, isp_fetch(&b, 2, nir_imin(&b, nir_imax(&b, nx, nir_imm_int(&b, 0)), nir_iadd_imm(&b, bw, -1)),
                                           nir_imin(&b, nir_imax(&b, ny, nir_imm_int(&b, 0)), nir_iadd_imm(&b, bh, -1)), nir_type_float32), 0);

         still = nir_iand(&b, still, nir_inot(&b, nir_iand(&b, in, nir_ilt(&b, limit, d))));
      }
   }
   nir_def *now = isp_fetch(&b, 0, x, y, nir_type_float32), *before = isp_fetch(&b, 1, x, y, nir_type_float32);

   for (unsigned i = 0; i < comps; i++) {
      nir_def *s = nr_int(&b, now, i), *d = nir_isub(&b, s, nr_int(&b, before, i));

      v[i] = nir_bcsel(&b, nir_iand(&b, still, nir_ilt(&b, nir_iabs(&b, d), near)),
                       nir_isub(&b, s, nir_ishr_imm(&b, nir_imul(&b, d, keep), 8)), s);
   }
   nr_store(&b, out, v[0], v[1]);
   return isp_fragment_end(&b, 3);
}

/* what with_before() works out of the blocks' D (rwcam_render.c, line for line): the limit, near for both planes, whether all move */
static void
isp_nr_measure(const uint8_t *diff, uint32_t n, unsigned level, int *limit, int near[2], int keep[2], int *all_move)
{
   uint64_t all = 0, calm = 0, calm_n = 0;
   uint32_t moving = 0;

   near[0] = level == 2 ? 18 : 10;
   keep[0] = level == 2 ? 128 : 90;
   near[1] = level == 2 ? 14 : 8;
   keep[1] = level == 2 ? 140 : 102;
   for (uint32_t i = 0; i < n; i++)
      all += diff[i];
   for (uint32_t i = 0; i < n; i++) {
      if ((uint64_t)diff[i] * n <= 2 * all) {
         calm += diff[i];
         calm_n++;
      }
   }
   *limit = (int)(1.7 * (calm_n ? (double)calm / (double)calm_n : 0) + 0.5);
   *limit = *limit < 10 ? 10 : *limit > 48 ? 48 : *limit;
   int noise3 = (int)(3.0 * (calm_n ? (double)calm / (double)calm_n : 0) / 4 + 0.5);

   noise3 = noise3 > 40 ? 40 : noise3;
   near[0] = noise3 > near[0] ? noise3 : near[0];
   near[1] = noise3 * 3 / 4 > near[1] ? noise3 * 3 / 4 : near[1];
   for (uint32_t i = 0; i < n; i++)
      moving += diff[i] > *limit;
   *all_move = !((double)moving <= 0.35 * n);
}

/* The picture's two planes out of isp->rgb, cleaned. lock held; the caller flushes. */
static int
isp_denoise(struct rwa_isp *isp, const struct isp_block *block, struct rwa_texture *luma, struct rwa_texture *chroma, void *fs_luma,
            void *fs_chroma, uint32_t width, uint32_t height, unsigned level, bool before, char *why, size_t why_size)
{
   struct rwa_device *device = isp->device;
   const uint32_t cw = (width + 1) / 2, ch = (height + 1) / 2, bw = (width + ISP_NR_BLOCK - 1) / ISP_NR_BLOCK;
   const uint32_t bh = (height + ISP_NR_BLOCK - 1) / ISP_NR_BLOCK;
   const unsigned target = RWA_USAGE_SHADER_READ | RWA_USAGE_RENDER_TARGET;
   /* nr[]: which are needed, and what each is */
   const struct { unsigned format; uint32_t w, h; bool want; } make[9] = {
      { 10, width, height, level == 2 || before }, { 30, cw, ch, true }, { 10, width, height, level == 2 && before }, { 30, cw, ch, before },
      { 10, bw, bh, before }, { 10, width, height, before }, { 30, cw, ch, before }, { 10, width, height, before }, { 30, cw, ch, before },
   };
   void *fs_c = device->isp.nr_chroma[level - 1] != NULL ? device->isp.nr_chroma[level - 1]
              : isp_shader(device, &device->isp.nr_chroma[level - 1], isp_nr_chroma_shader(device->nir_options, level), why, why_size);
   void *fs_y = level < 2 ? NULL : device->isp.nr_luma != NULL ? device->isp.nr_luma
              : isp_shader(device, &device->isp.nr_luma, isp_nr_luma_shader(device->nir_options), why, why_size);

   if (fs_c == NULL || (level == 2 && fs_y == NULL))
      return RWA_ISP_E_GPU;
   if (isp->nr_w != width || isp->nr_h != height) {
      for (int i = 0; i < 9; i++) {
         if (isp->nr[i] != NULL)
            rwa_texture_destroy(isp->nr[i]);
         isp->nr[i] = NULL;
      }
      isp->nr_w = width;
      isp->nr_h = height;
      isp->nr_before = false;
   }
   for (int i = 0; i < 9; i++) {
      if (make[i].want && isp->nr[i] == NULL &&
          (isp->nr[i] = isp_texture(device, make[i].format, make[i].w, make[i].h, target, why, why_size)) == NULL)
         return RWA_ISP_E_MEMORY;
   }
   struct rwa_texture *const rgb[1] = { isp->rgb }, *const made_y[1] = { isp->nr[0] }, *const made_c[1] = { isp->nr[1] };
   struct rwa_texture *y_made = isp->nr[0] != NULL && (level == 2 || before) ? isp->nr[0] : luma;
   struct rwa_texture *y_clean = level < 2 ? y_made : before ? isp->nr[2] : luma, *c_clean = before ? isp->nr[3] : chroma;

   if (isp_pass(device, y_made, fs_luma, rgb, 1, block, "luma", why, why_size) != 0 ||
       isp_pass(device, isp->nr[1], fs_chroma, rgb, 1, block, "chroma", why, why_size) != 0 ||
       isp_pass(device, c_clean, fs_c, made_c, 1, block, "colour smoothing", why, why_size) != 0 ||
       (level == 2 && isp_pass(device, y_clean, fs_y, made_y, 1, block, "brightness smoothing", why, why_size) != 0))
      return RWA_ISP_E_GPU;
   if (before) {
      /* the picture before: the context's, or (the first of its size) this picture as it was made, as on the processor */
      struct rwa_texture *by = isp->nr_before ? isp->nr[5 + 2 * isp->nr_which] : isp->nr[0];
      struct rwa_texture *bc = isp->nr_before ? isp->nr[6 + 2 * isp->nr_which] : isp->nr[1];
      struct rwa_texture *ny = isp->nr[5 + 2 * !isp->nr_which], *nc = isp->nr[6 + 2 * !isp->nr_which];
      void *fs_d = device->isp.nr_diff != NULL ? device->isp.nr_diff
                 : isp_shader(device, &device->isp.nr_diff, isp_nr_diff_shader(device->nir_options), why, why_size);
      void *fs_b[2];

      for (unsigned i = 0; i < 2; i++)
         fs_b[i] = device->isp.nr_blend[i] != NULL ? device->isp.nr_blend[i]
                 : isp_shader(device, &device->isp.nr_blend[i], isp_nr_blend_shader(device->nir_options, i + 1), why, why_size);
      if (fs_d == NULL || fs_b[0] == NULL || fs_b[1] == NULL)
         return RWA_ISP_E_GPU;
      struct isp_block for_y = *block, for_c;
      struct rwa_texture *const pair[2] = { y_clean, by }, *const blend_y[3] = { y_clean, by, isp->nr[4] };
      struct rwa_texture *const blend_c[3] = { c_clean, bc, isp->nr[4] };
      const unsigned origin[3] = { 0, 0, 0 }, size[3] = { bw, bh, 1 };
      uint8_t *diff = malloc((size_t)bw * bh);
      int limit, near[2], keep[2], all_move;

      if (diff == NULL)
         return RWA_ISP_E_MEMORY;
      for_y.nr_bw = (int32_t)bw;
      for_y.nr_bh = (int32_t)bh;
      if (isp_pass(device, isp->nr[4], fs_d, pair, 2, &for_y, "block difference", why, why_size) != 0 ||
          rwa_texture_read(isp->nr[4], 0, 0, origin, size, diff, bw, why, why_size) != 0) {
         free(diff);
         return RWA_ISP_E_GPU;
      }
      isp_nr_measure(diff, bw * bh, level, &limit, near, keep, &all_move);
      free(diff);
      for_y.nr_limit = limit;
      for_y.nr_all = all_move;
      for_c = for_y;
      for_y.nr_near = near[0];
      for_y.nr_keep = keep[0];
      for_c.nr_near = near[1];
      for_c.nr_keep = keep[1];
      if (isp_pass(device, ny, fs_b[0], blend_y, 3, &for_y, "brightness with the picture before", why, why_size) != 0 ||
          isp_pass(device, luma, fs_b[0], blend_y, 3, &for_y, "brightness with the picture before", why, why_size) != 0 ||
          isp_pass(device, nc, fs_b[1], blend_c, 3, &for_c, "colours with the picture before", why, why_size) != 0 ||
          isp_pass(device, chroma, fs_b[1], blend_c, 3, &for_c, "colours with the picture before", why, why_size) != 0)
         return RWA_ISP_E_GPU;
      /* (the blocks are user buffers the passes copy at the draw; flushed here so that for_y / for_c need not outlive this) */
      device->pipe->flush(device->pipe, NULL, 0);
      isp->nr_which = !isp->nr_which;
      isp->nr_before = true;
   } else {
      isp->nr_before = false;
   }
   if ((uint64_t)width * height > ISP_KEEP_BYTES / 4) {
      /* a still's: not kept (the queued passes hold what they use) */
      for (int i = 0; i < 9; i++) {
         if (isp->nr[i] != NULL)
            rwa_texture_destroy(isp->nr[i]);
         isp->nr[i] = NULL;
      }
      isp->nr_w = isp->nr_h = 0;
      isp->nr_before = false;
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

   const unsigned level = p->size >= sizeof(*p) ? MIN2(p->denoise, 2u) : 0;

   if (isp_pass(device, isp->rgb, fs, isp_textures, 4, &block, "ISP", why, why_size) != 0)
      return RWA_ISP_E_GPU;
   if (level == 0) {
      isp->nr_before = false;
      if (isp_pass(device, luma, fs_luma, rgb_texture, 1, &block, "luma", why, why_size) != 0 ||
          isp_pass(device, chroma, fs_chroma, rgb_texture, 1, &block, "chroma", why, why_size) != 0)
         return RWA_ISP_E_GPU;
   } else {
      error = isp_denoise(isp, &block, luma, chroma, fs_luma, fs_chroma, width, height, level, p->denoise_before != 0, why, why_size);
      if (error != RWA_ISP_OK) {
         isp->nr_before = false;
         return error;
      }
   }
   device->pipe->flush(device->pipe, fence, 0);
   if (isp->rgb != NULL && (uint64_t)width * height * 4 > ISP_KEEP_BYTES) {
      rwa_texture_destroy(isp->rgb);          /* a still's: the queued passes hold what they use */
      isp->rgb = NULL;
   }
   if (result != NULL && result->size >= sizeof(*result)) {
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

   if (result != NULL && result->size >= sizeof(*result))
      result->wait_ns = os_time_get_nano() - t;
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
   struct rwa_texture *own[5] = { isp->raw_own, isp->columns, isp->rows, isp->gamma, isp->rgb };

   for (int i = 0; i < 5; i++)
      if (own[i] != NULL)
         rwa_texture_destroy(own[i]);
   for (int i = 0; i < 9; i++)
      if (isp->nr[i] != NULL)
         rwa_texture_destroy(isp->nr[i]);
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

   if (isp == NULL || output == NULL || output->size < sizeof(*output) || output->iosurface == NULL || params == NULL ||
       params->size < RWA_ISP_PARAMS_SIZE_1) {
      say(why, why_size, "the camera's GPU render was given no context, no output surface, or a struct smaller than this library's");
      return RWA_ISP_E_ARGUMENT;
   }
   int error;

   {
      /* the planes and the passes that write them under ONE hold of the lock: nothing can let the planes go in between */
      RWA_GUARD();
      uint64_t t = os_time_get_nano();

      error = isp_surface_planes(isp, output, params->video_range, &luma, &chroma, &imported, why, why_size);
      t = os_time_get_nano() - t;
      if (error == RWA_ISP_OK)
         error = isp_render_locked(isp, frame, params, luma, chroma, output->width, output->height, &fence, result, why, why_size);
      if (error == RWA_ISP_OK && result != NULL && result->size >= sizeof(*result)) {
         result->prepare_ns += t;
         result->flags |= imported ? RWA_ISP_DID_IMPORT : 0;
      }
   }
   if (error != RWA_ISP_OK)
      return error;
   return isp_finish(isp, fence, output->flags & RWA_ISP_OUT_ASYNC, result, why, why_size);
}
