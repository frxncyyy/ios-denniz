/*
 * y2s52 host test (no GPU, no phone): this directory's pan_layout.c -- Mesa's with one change -- asked for the linear images the
 * still-image noise reduction lays over its scratch buffer, at the offsets run q10ig-vt11 had. With rwa_pan_buffer_offset_align
 * set (as rwa_texture_from_buffer sets it) an offset 32 past a multiple of 64 is taken; without it, and for an offset that is
 * not a multiple of 16 or a row that is not a multiple of 64, the answer is Mesa's own: refused.
 */
#include <stdio.h>
#include "pan_texture.h"

extern int rwa_pan_buffer_offset_align;

static bool
lay(enum pipe_format format, unsigned w, unsigned h, unsigned offset, unsigned stride)
{
   struct pan_image_layout layout = { .modifier = 0 /* DRM_FORMAT_MOD_LINEAR */, .format = format, .width = w, .height = h, .depth = 1,
                                      .nr_samples = 1, .dim = MALI_TEXTURE_DIMENSION_2D, .nr_slices = 1, .array_size = 1 };
   struct pan_image_explicit_layout explicit = { .offset = offset, .row_stride = stride };
   bool ok = pan_image_layout_init(9, &layout, &explicit);
   return ok && layout.slices[0].offset == offset && layout.slices[0].row_stride == stride;
}

int
main(void)
{
   static const struct { enum pipe_format f; unsigned w, h, offset, stride; int loose, strict; const char *what; } c[] = {
      { PIPE_FORMAT_R16G16_FLOAT, 2016, 1512, 0, 8064, 1, 1, "level 0, RG16Float at offset 0" },
      { PIPE_FORMAT_R16_FLOAT, 1008, 756, 9146592, 2048, 1, 0, "the level run q10ig-vt11 lost: R16Float at 64 x 142915 + 32" },
      { PIPE_FORMAT_R8_UNORM, 1008, 756, 9146592, 1024, 1, 0, "the same memory as R8Unorm" },
      { PIPE_FORMAT_R16G16_FLOAT, 504, 378, 9146592 + 2048 * 756, 2048, 1, 0, "its chroma, RG16Float" },
      { PIPE_FORMAT_R16_FLOAT, 504, 378, 9146608, 1024, 1, 0, "16 past a multiple of 64" },
      { PIPE_FORMAT_R16_FLOAT, 504, 378, 9146600, 1024, 0, 0, "8 past: not a multiple of 16" },
      { PIPE_FORMAT_R16_FLOAT, 504, 378, 9146592, 1040, 0, 0, "a row that is not a multiple of 64" },
      { PIPE_FORMAT_R16_FLOAT, 504, 378, 9146592, 512, 0, 0, "a row shorter than the picture" },
   };
   int bad = 0;

   for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
      rwa_pan_buffer_offset_align = 16;
      bool loose = lay(c[i].f, c[i].w, c[i].h, c[i].offset, c[i].stride);
      rwa_pan_buffer_offset_align = 0;
      bool strict = lay(c[i].f, c[i].w, c[i].h, c[i].offset, c[i].stride);
      if (loose != (bool)c[i].loose || strict != (bool)c[i].strict) {
         bad++;
         printf("FAILED: %s: over a buffer %d, otherwise %d\n", c[i].what, loose, strict);
      }
   }
   printf("pan_layout_host_test: %u cases, %d failed\n", (unsigned)(sizeof(c) / sizeof(c[0])), bad);
   return bad != 0;
}
