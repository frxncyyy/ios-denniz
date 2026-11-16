#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAN_ARCH 10
#include "panfrost/lib/pan_format.h"
#include "genxml/v10_pack.h"

int main(int argc, char **argv)
{
   assert(argc == 3);
   FILE *f = fopen(argv[1], "wb");
   assert(f);
   uint64_t vertex_va = strtoull(argv[2], NULL, 0);
   struct mali_attribute_packed attrs[16] = {0};
   for (unsigned i = 0; i < 16; i++) {
      if (i < 2) {
         enum pipe_format fmt = i == 0 ? PIPE_FORMAT_R32G32B32A32_FLOAT : PIPE_FORMAT_R32G32_FLOAT;
         struct MALI_ATTRIBUTE a = {
            .type = MALI_DESCRIPTOR_TYPE_ATTRIBUTE,
            .attribute_type = MALI_ATTRIBUTE_TYPE_1D,
            .offset_enable = true,
            .format = GENX(pan_pipe_format)[fmt].hw,
            .table = 0,
            .frequency = MALI_ATTRIBUTE_FREQUENCY_VERTEX,
            .offset = i == 0 ? 0 : 16,
            .buffer_index = 17,
            .stride = 32,
         };
         MALI_ATTRIBUTE_pack(&attrs[i], &a);
      } else {
         struct MALI_NULL_DESCRIPTOR n = {.type = MALI_DESCRIPTOR_TYPE_NULL};
         MALI_NULL_DESCRIPTOR_PACKED_T packed;
         MALI_NULL_DESCRIPTOR_pack(&packed, &n);
         memcpy(&attrs[i], &packed, sizeof(packed));
      }
   }
   fwrite(attrs, sizeof(attrs), 1, f);
   struct mali_sampler_packed sampler = {0};
   struct MALI_SAMPLER s = {MALI_SAMPLER_header};
   s.clamp_integer_array_indices = false;
   MALI_SAMPLER_pack(&sampler, &s);
   fwrite(&sampler, sizeof(sampler), 1, f);
   struct mali_buffer_packed buffer = {0};
   struct MALI_BUFFER b = {MALI_BUFFER_header};
   b.size = 96;
   b.address = vertex_va;
   MALI_BUFFER_pack(&buffer, &b);
   fwrite(&buffer, sizeof(buffer), 1, f);
   return fclose(f);
}
