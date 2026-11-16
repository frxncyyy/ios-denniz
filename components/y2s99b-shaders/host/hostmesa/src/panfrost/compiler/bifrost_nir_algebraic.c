#include "bifrost_nir.h"

#include "nir.h"
#include "nir_builder.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

/* What follows is NIR algebraic transform code for the following 4
 * transforms:
 *    ('iand', 'a@1', 'b@1') => ('bcsel', 'a', 'b', False)
 *    ('ior', 'a@1', 'b@1') => ('bcsel', 'a', 'a', 'b')
 *    ('iand', 'a@1', ('inot', 'b@1')) => ('bcsel', 'b', 0, 'a')
 *    ('ior', 'a@1', ('inot', 'b@1')) => ('bcsel', 'b', 'a', True)
 */


static const nir_search_value_union bifrost_nir_opt_boolean_bitwise_values[] = {
   /* ('iand', 'a@1', 'b@1') => ('bcsel', 'a', 'b', False) */
   { .variable = {
      { nir_search_value_variable, 1 },
      0, /* a */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 1 },
      1, /* b */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_iand,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace0_0 -> 0 in the cache */
   /* replace0_1 -> 1 in the cache */
   { .constant = {
      { nir_search_value_constant, 1 },
      nir_type_bool, { NIR_FALSE /* False */ },
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_bcsel,
      -1, 0,
      { 0, 1, 3 },
      -1,
   } },

   /* ('ior', 'a@1', 'b@1') => ('bcsel', 'a', 'a', 'b') */
   /* search1_0 -> 0 in the cache */
   /* search1_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_ior,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace1_0 -> 0 in the cache */
   /* replace1_1 -> 0 in the cache */
   /* replace1_2 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_bcsel,
      -1, 0,
      { 0, 0, 1 },
      -1,
   } },

   /* ('iand', 'a@1', ('inot', 'b@1')) => ('bcsel', 'b', 0, 'a') */
   /* search2_0 -> 0 in the cache */
   /* search2_1_0 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_inot,
      -1, 0,
      { 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_iand,
      0, 1,
      { 0, 7 },
      -1,
   } },

   /* replace2_0 -> 1 in the cache */
   { .constant = {
      { nir_search_value_constant, 1 },
      nir_type_int, { 0x0ull /* 0 */ },
   } },
   /* replace2_2 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_bcsel,
      -1, 0,
      { 1, 9, 0 },
      -1,
   } },

   /* ('ior', 'a@1', ('inot', 'b@1')) => ('bcsel', 'b', 'a', True) */
   /* search3_0 -> 0 in the cache */
   /* search3_1_0 -> 1 in the cache */
   /* search3_1 -> 7 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_ior,
      0, 1,
      { 0, 7 },
      -1,
   } },

   /* replace3_0 -> 1 in the cache */
   /* replace3_1 -> 0 in the cache */
   { .constant = {
      { nir_search_value_constant, 1 },
      nir_type_bool, { NIR_TRUE /* True */ },
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_bcsel,
      -1, 0,
      { 1, 0, 12 },
      -1,
   } },

};



static const struct transform bifrost_nir_opt_boolean_bitwise_transforms[] = {
   { ~0, ~0, ~0 }, /* Sentinel */

   { 2, 4, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 5, 6, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 2, 4, 0 },
   { 8, 10, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 5, 6, 0 },
   { 11, 13, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

};

static const struct per_op_table bifrost_nir_opt_boolean_bitwise_pass_op_table[nir_num_search_ops] = {
   [nir_op_iand] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         1,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         2,
         5,
         5,
         5,
      },
   },
   [nir_op_ior] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         1,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         3,
         6,
         6,
         6,
      },
   },
   [nir_op_inot] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         4,
      },
   },
};

/* Mapping from state index to offset in transforms (0 being no transforms) */
static const uint16_t bifrost_nir_opt_boolean_bitwise_transform_offsets[] = {
   0,
   0,
   1,
   3,
   0,
   5,
   8,
};

static const nir_algebraic_table bifrost_nir_opt_boolean_bitwise_table = {
   .transforms = bifrost_nir_opt_boolean_bitwise_transforms,
   .transform_offsets = bifrost_nir_opt_boolean_bitwise_transform_offsets,
   .pass_op_table = bifrost_nir_opt_boolean_bitwise_pass_op_table,
   .values = bifrost_nir_opt_boolean_bitwise_values,
   .expression_cond = NULL,
   .variable_cond = NULL,
};

bool
bifrost_nir_opt_boolean_bitwise(
   nir_shader *shader
) {
   bool progress = false;
   bool condition_flags[1];
   const nir_shader_compiler_options *options = shader->options;
   const shader_info *info = &shader->info;
   (void) options;
   (void) info;

   STATIC_ASSERT(14 == ARRAY_SIZE(bifrost_nir_opt_boolean_bitwise_values));
   condition_flags[0] = true;

   nir_foreach_function_impl(impl, shader) {
     progress |= nir_algebraic_impl(impl, condition_flags, &bifrost_nir_opt_boolean_bitwise_table);
   }

   return progress;
}


#include "nir.h"
#include "nir_builder.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

/* What follows is NIR algebraic transform code for the following 27
 * transforms:
 *    ('pack_32_4x8_split', 'a', 'b', 'c', 'd') => ('pack_32_2x16_split', ('ior', ('u2u16', 'a'), ('ishl', ('u2u16', 'b'), 8)), ('ior', ('u2u16', 'c'), ('ishl', ('u2u16', 'd'), 8)))
 *    ('fmul', 'a', 2.0) => ('fadd', 'a', 'a')
 *    ('fmin', ('fmax', 'a', -1.0), 1.0) => ('fsat_signed', 'a')
 *    ('fmax', ('fmin', 'a', 1.0), -1.0) => ('fsat_signed', 'a')
 *    ('fmax', 'a', 0.0) => ('fclamp_pos', 'a')
 *    ('b32csel', 'b@32', ('iadd', 'a@32', 1), 'a') => ('iadd', 'a', ('b2i32', 'b'))
 *    ('imin', 'a@8', 'b@8') => ('b8csel', ('ilt8', 'a', 'b'), 'a', 'b')
 *    ('imax', 'a@8', 'b@8') => ('b8csel', ('ilt8', 'a', 'b'), 'b', 'a')
 *    ('umin', 'a@8', 'b@8') => ('b8csel', ('ult8', 'a', 'b'), 'a', 'b')
 *    ('umax', 'a@8', 'b@8') => ('b8csel', ('ult8', 'a', 'b'), 'b', 'a')
 *    ('f2i8', 'a') => ('i2i8', ('f2i16', 'a'))
 *    ('f2u8', 'a') => ('u2u8', ('f2u16', 'a'))
 *    ('unpack_64_2x32', 'a') => ('vec2', ('unpack_64_2x32_split_x', 'a'), ('unpack_64_2x32_split_y', 'a'))
 *    ('i2f16', 'a@32') => ('f2f16', ('i2f32', 'a'))
 *    ('u2f16', 'a@32') => ('f2f16', ('u2f32', 'a'))
 *    ('b2f16', ('inot', 'a@8')) => ('b8csel', 'a', 0.0, 1.0)
 *    ('b2f16', 'a@8') => ('b8csel', 'a', 1.0, 0.0)
 *    ('b2f32', ('inot', 'a@8')) => ('b8csel', 'a', 0.0, 1.0)
 *    ('b2f32', 'a@8') => ('b8csel', 'a', 1.0, 0.0)
 *    ('b2f16', ('inot', 'a@16')) => ('b16csel', 'a', 0.0, 1.0)
 *    ('b2f16', 'a@16') => ('b16csel', 'a', 1.0, 0.0)
 *    ('b2f32', ('inot', 'a@16')) => ('b16csel', 'a', 0.0, 1.0)
 *    ('b2f32', 'a@16') => ('b16csel', 'a', 1.0, 0.0)
 *    ('b2f16', ('inot', 'a@32')) => ('b32csel', 'a', 0.0, 1.0)
 *    ('b2f16', 'a@32') => ('b32csel', 'a', 1.0, 0.0)
 *    ('b2f32', ('inot', 'a@32')) => ('b32csel', 'a', 0.0, 1.0)
 *    ('b2f32', 'a@32') => ('b32csel', 'a', 1.0, 0.0)
 */


static const nir_search_value_union bifrost_nir_lower_algebraic_late_values[] = {
   /* ('pack_32_4x8_split', 'a', 'b', 'c', 'd') => ('pack_32_2x16_split', ('ior', ('u2u16', 'a'), ('ishl', ('u2u16', 'b'), 8)), ('ior', ('u2u16', 'c'), ('ishl', ('u2u16', 'd'), 8))) */
   { .variable = {
      { nir_search_value_variable, 8 },
      0, /* a */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 8 },
      1, /* b */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 8 },
      2, /* c */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 8 },
      3, /* d */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_pack_32_4x8_split,
      -1, 0,
      { 0, 1, 2, 3 },
      -1,
   } },

   /* replace4_0_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_u2u16,
      -1, 0,
      { 0 },
      -1,
   } },
   /* replace4_0_1_0_0 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_u2u16,
      -1, 0,
      { 1 },
      -1,
   } },
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_int, { 0x8ull /* 8 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_ishl,
      -1, 0,
      { 6, 7 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_ior,
      0, 1,
      { 5, 8 },
      -1,
   } },
   /* replace4_1_0_0 -> 2 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_u2u16,
      -1, 0,
      { 2 },
      -1,
   } },
   /* replace4_1_1_0_0 -> 3 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_u2u16,
      -1, 0,
      { 3 },
      -1,
   } },
   /* replace4_1_1_1 -> 7 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_ishl,
      -1, 0,
      { 11, 7 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_ior,
      1, 1,
      { 10, 12 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_pack_32_2x16_split,
      -1, 2,
      { 9, 13 },
      -1,
   } },

   /* ('fmul', 'a', 2.0) => ('fadd', 'a', 'a') */
   { .variable = {
      { nir_search_value_variable, -1 },
      0, /* a */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0x4000000000000000ull /* 2.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fmul,
      0, 1,
      { 15, 16 },
      -1,
   } },

   /* replace5_0 -> 15 in the cache */
   /* replace5_1 -> 15 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fadd,
      -1, 0,
      { 15, 15 },
      -1,
   } },

   /* ('fmin', ('fmax', 'a', -1.0), 1.0) => ('fsat_signed', 'a') */
   /* search6_0_0 -> 15 in the cache */
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0xbff0000000000000ull /* -1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fmax,
      1, 1,
      { 15, 19 },
      -1,
   } },
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0x3ff0000000000000ull /* 1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fmin,
      0, 2,
      { 20, 21 },
      -1,
   } },

   /* replace6_0 -> 15 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fsat_signed,
      -1, 0,
      { 15 },
      -1,
   } },

   /* ('fmax', ('fmin', 'a', 1.0), -1.0) => ('fsat_signed', 'a') */
   /* search7_0_0 -> 15 in the cache */
   /* search7_0_1 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fmin,
      1, 1,
      { 15, 21 },
      -1,
   } },
   /* search7_1 -> 19 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fmax,
      0, 2,
      { 24, 19 },
      -1,
   } },

   /* replace7_0 -> 15 in the cache */
   /* replace7 -> 23 in the cache */

   /* ('fmax', 'a', 0.0) => ('fclamp_pos', 'a') */
   /* search8_0 -> 15 in the cache */
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0x0ull /* 0.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fmax,
      0, 1,
      { 15, 26 },
      -1,
   } },

   /* replace8_0 -> 15 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_fclamp_pos,
      -1, 0,
      { 15 },
      -1,
   } },

   /* ('b32csel', 'b@32', ('iadd', 'a@32', 1), 'a') => ('iadd', 'a', ('b2i32', 'b')) */
   { .variable = {
      { nir_search_value_variable, 32 },
      0, /* b */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 32 },
      1, /* a */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_int, { 0x1ull /* 1 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_iadd,
      0, 1,
      { 30, 31 },
      -1,
   } },
   /* search9_2 -> 30 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b32csel,
      -1, 1,
      { 29, 32, 30 },
      -1,
   } },

   /* replace9_0 -> 30 in the cache */
   /* replace9_1_0 -> 29 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2i32,
      -1, 0,
      { 29 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_iadd,
      0, 1,
      { 30, 34 },
      -1,
   } },

   /* ('imin', 'a@8', 'b@8') => ('b8csel', ('ilt8', 'a', 'b'), 'a', 'b') */
   /* search10_0 -> 0 in the cache */
   /* search10_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_imin,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace10_0_0 -> 0 in the cache */
   /* replace10_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_ilt8,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   /* replace10_1 -> 0 in the cache */
   /* replace10_2 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b8csel,
      -1, 0,
      { 37, 0, 1 },
      -1,
   } },

   /* ('imax', 'a@8', 'b@8') => ('b8csel', ('ilt8', 'a', 'b'), 'b', 'a') */
   /* search11_0 -> 0 in the cache */
   /* search11_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_imax,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace11_0_0 -> 0 in the cache */
   /* replace11_0_1 -> 1 in the cache */
   /* replace11_0 -> 37 in the cache */
   /* replace11_1 -> 1 in the cache */
   /* replace11_2 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b8csel,
      -1, 0,
      { 37, 1, 0 },
      -1,
   } },

   /* ('umin', 'a@8', 'b@8') => ('b8csel', ('ult8', 'a', 'b'), 'a', 'b') */
   /* search12_0 -> 0 in the cache */
   /* search12_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_umin,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace12_0_0 -> 0 in the cache */
   /* replace12_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_ult8,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   /* replace12_1 -> 0 in the cache */
   /* replace12_2 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b8csel,
      -1, 0,
      { 42, 0, 1 },
      -1,
   } },

   /* ('umax', 'a@8', 'b@8') => ('b8csel', ('ult8', 'a', 'b'), 'b', 'a') */
   /* search13_0 -> 0 in the cache */
   /* search13_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_umax,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace13_0_0 -> 0 in the cache */
   /* replace13_0_1 -> 1 in the cache */
   /* replace13_0 -> 42 in the cache */
   /* replace13_1 -> 1 in the cache */
   /* replace13_2 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b8csel,
      -1, 0,
      { 42, 1, 0 },
      -1,
   } },

   /* ('f2i8', 'a') => ('i2i8', ('f2i16', 'a')) */
   /* search14_0 -> 15 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_f2i8,
      -1, 0,
      { 15 },
      -1,
   } },

   /* replace14_0_0 -> 15 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_f2i16,
      -1, 0,
      { 15 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_i2i8,
      -1, 0,
      { 47 },
      -1,
   } },

   /* ('f2u8', 'a') => ('u2u8', ('f2u16', 'a')) */
   /* search15_0 -> 15 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_f2u8,
      -1, 0,
      { 15 },
      -1,
   } },

   /* replace15_0_0 -> 15 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_f2u16,
      -1, 0,
      { 15 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_u2u8,
      -1, 0,
      { 50 },
      -1,
   } },

   /* ('unpack_64_2x32', 'a') => ('vec2', ('unpack_64_2x32_split_x', 'a'), ('unpack_64_2x32_split_y', 'a')) */
   { .variable = {
      { nir_search_value_variable, 64 },
      0, /* a */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_unpack_64_2x32,
      -1, 0,
      { 52 },
      -1,
   } },

   /* replace16_0_0 -> 52 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_unpack_64_2x32_split_x,
      -1, 0,
      { 52 },
      -1,
   } },
   /* replace16_1_0 -> 52 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_unpack_64_2x32_split_y,
      -1, 0,
      { 52 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_vec2,
      -1, 0,
      { 54, 55 },
      -1,
   } },

   /* ('i2f16', 'a@32') => ('f2f16', ('i2f32', 'a')) */
   { .variable = {
      { nir_search_value_variable, 32 },
      0, /* a */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_i2f16,
      -1, 0,
      { 57 },
      -1,
   } },

   /* replace17_0_0 -> 57 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_i2f32,
      -1, 0,
      { 57 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_f2f16,
      -1, 0,
      { 59 },
      -1,
   } },

   /* ('u2f16', 'a@32') => ('f2f16', ('u2f32', 'a')) */
   /* search18_0 -> 57 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_u2f16,
      -1, 0,
      { 57 },
      -1,
   } },

   /* replace18_0_0 -> 57 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_u2f32,
      -1, 0,
      { 57 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_f2f16,
      -1, 0,
      { 62 },
      -1,
   } },

   /* ('b2f16', ('inot', 'a@8')) => ('b8csel', 'a', 0.0, 1.0) */
   /* search19_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_inot,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f16,
      -1, 0,
      { 64 },
      -1,
   } },

   /* replace19_0 -> 0 in the cache */
   { .constant = {
      { nir_search_value_constant, 16 },
      nir_type_float, { 0x0ull /* 0.0 */ },
   } },
   { .constant = {
      { nir_search_value_constant, 16 },
      nir_type_float, { 0x3ff0000000000000ull /* 1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b8csel,
      -1, 0,
      { 0, 66, 67 },
      -1,
   } },

   /* ('b2f16', 'a@8') => ('b8csel', 'a', 1.0, 0.0) */
   /* search20_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f16,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace20_0 -> 0 in the cache */
   /* replace20_1 -> 67 in the cache */
   /* replace20_2 -> 66 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b8csel,
      -1, 0,
      { 0, 67, 66 },
      -1,
   } },

   /* ('b2f32', ('inot', 'a@8')) => ('b8csel', 'a', 0.0, 1.0) */
   /* search21_0_0 -> 0 in the cache */
   /* search21_0 -> 64 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f32,
      -1, 0,
      { 64 },
      -1,
   } },

   /* replace21_0 -> 0 in the cache */
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_float, { 0x0ull /* 0.0 */ },
   } },
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_float, { 0x3ff0000000000000ull /* 1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b8csel,
      -1, 0,
      { 0, 72, 73 },
      -1,
   } },

   /* ('b2f32', 'a@8') => ('b8csel', 'a', 1.0, 0.0) */
   /* search22_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f32,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace22_0 -> 0 in the cache */
   /* replace22_1 -> 73 in the cache */
   /* replace22_2 -> 72 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b8csel,
      -1, 0,
      { 0, 73, 72 },
      -1,
   } },

   /* ('b2f16', ('inot', 'a@16')) => ('b16csel', 'a', 0.0, 1.0) */
   { .variable = {
      { nir_search_value_variable, 16 },
      0, /* a */
      false,
      nir_type_invalid,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_inot,
      -1, 0,
      { 77 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f16,
      -1, 0,
      { 78 },
      -1,
   } },

   /* replace23_0 -> 77 in the cache */
   /* replace23_1 -> 66 in the cache */
   /* replace23_2 -> 67 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b16csel,
      -1, 0,
      { 77, 66, 67 },
      -1,
   } },

   /* ('b2f16', 'a@16') => ('b16csel', 'a', 1.0, 0.0) */
   /* search24_0 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f16,
      -1, 0,
      { 77 },
      -1,
   } },

   /* replace24_0 -> 77 in the cache */
   /* replace24_1 -> 67 in the cache */
   /* replace24_2 -> 66 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b16csel,
      -1, 0,
      { 77, 67, 66 },
      -1,
   } },

   /* ('b2f32', ('inot', 'a@16')) => ('b16csel', 'a', 0.0, 1.0) */
   /* search25_0_0 -> 77 in the cache */
   /* search25_0 -> 78 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f32,
      -1, 0,
      { 78 },
      -1,
   } },

   /* replace25_0 -> 77 in the cache */
   /* replace25_1 -> 72 in the cache */
   /* replace25_2 -> 73 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b16csel,
      -1, 0,
      { 77, 72, 73 },
      -1,
   } },

   /* ('b2f32', 'a@16') => ('b16csel', 'a', 1.0, 0.0) */
   /* search26_0 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f32,
      -1, 0,
      { 77 },
      -1,
   } },

   /* replace26_0 -> 77 in the cache */
   /* replace26_1 -> 73 in the cache */
   /* replace26_2 -> 72 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b16csel,
      -1, 0,
      { 77, 73, 72 },
      -1,
   } },

   /* ('b2f16', ('inot', 'a@32')) => ('b32csel', 'a', 0.0, 1.0) */
   /* search27_0_0 -> 57 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_inot,
      -1, 0,
      { 57 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f16,
      -1, 0,
      { 87 },
      -1,
   } },

   /* replace27_0 -> 57 in the cache */
   /* replace27_1 -> 66 in the cache */
   /* replace27_2 -> 67 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b32csel,
      -1, 0,
      { 57, 66, 67 },
      -1,
   } },

   /* ('b2f16', 'a@32') => ('b32csel', 'a', 1.0, 0.0) */
   /* search28_0 -> 57 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f16,
      -1, 0,
      { 57 },
      -1,
   } },

   /* replace28_0 -> 57 in the cache */
   /* replace28_1 -> 67 in the cache */
   /* replace28_2 -> 66 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b32csel,
      -1, 0,
      { 57, 67, 66 },
      -1,
   } },

   /* ('b2f32', ('inot', 'a@32')) => ('b32csel', 'a', 0.0, 1.0) */
   /* search29_0_0 -> 57 in the cache */
   /* search29_0 -> 87 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f32,
      -1, 0,
      { 87 },
      -1,
   } },

   /* replace29_0 -> 57 in the cache */
   /* replace29_1 -> 72 in the cache */
   /* replace29_2 -> 73 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b32csel,
      -1, 0,
      { 57, 72, 73 },
      -1,
   } },

   /* ('b2f32', 'a@32') => ('b32csel', 'a', 1.0, 0.0) */
   /* search30_0 -> 57 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b2f32,
      -1, 0,
      { 57 },
      -1,
   } },

   /* replace30_0 -> 57 in the cache */
   /* replace30_1 -> 73 in the cache */
   /* replace30_2 -> 72 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      nir_op_b32csel,
      -1, 0,
      { 57, 73, 72 },
      -1,
   } },

};



static const struct transform bifrost_nir_lower_algebraic_late_transforms[] = {
   { ~0, ~0, ~0 }, /* Sentinel */

   { 4, 14, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 17, 18, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 27, 28, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 36, 38, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 39, 40, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 41, 43, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 44, 45, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 46, 48, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 49, 51, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 53, 56, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 58, 60, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 61, 63, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 69, 70, 0 },
   { 75, 76, 0 },
   { 81, 82, 0 },
   { 85, 86, 0 },
   { 90, 91, 0 },
   { 94, 95, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 25, 23, 0 },
   { 27, 28, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 22, 23, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 33, 35, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 65, 68, 0 },
   { 69, 70, 0 },
   { 71, 74, 0 },
   { 75, 76, 0 },
   { 79, 80, 0 },
   { 81, 82, 0 },
   { 83, 84, 0 },
   { 85, 86, 0 },
   { 88, 89, 0 },
   { 90, 91, 0 },
   { 92, 93, 0 },
   { 94, 95, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

};

static const struct per_op_table bifrost_nir_lower_algebraic_late_pass_op_table[nir_num_search_ops] = {
   [nir_op_pack_32_4x8_split] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         2,
      },
   },
   [nir_op_fmul] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         3,
         3,
         3,
      },
   },
   [nir_op_fmin] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 3,
      .table = (const uint16_t []) {
      
         0,
         4,
         0,
         4,
         4,
         19,
         0,
         19,
         0,
      },
   },
   [nir_op_fmax] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
      },
      
      .num_filtered_states = 3,
      .table = (const uint16_t []) {
      
         0,
         5,
         0,
         5,
         5,
         18,
         0,
         18,
         0,
      },
   },
   [nir_op_b32csel] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         0,
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         0,
         20,
         20,
         0,
         0,
         20,
         20,
      },
   },
   [nir_op_iadd] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         6,
         6,
         6,
      },
   },
   [nir_op_imin] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         7,
      },
   },
   [nir_op_imax] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         8,
      },
   },
   [nir_op_umin] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         9,
      },
   },
   [nir_op_umax] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         10,
      },
   },
   [nir_search_op_f2i] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         11,
      },
   },
   [nir_search_op_f2u] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         12,
      },
   },
   [nir_op_unpack_64_2x32] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         13,
      },
   },
   [nir_search_op_i2f] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         14,
      },
   },
   [nir_search_op_u2f] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         15,
      },
   },
   [nir_search_op_b2f] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         1,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         16,
         21,
      },
   },
   [nir_op_inot] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         17,
      },
   },
};

/* Mapping from state index to offset in transforms (0 being no transforms) */
static const uint16_t bifrost_nir_lower_algebraic_late_transform_offsets[] = {
   0,
   0,
   1,
   3,
   0,
   5,
   0,
   7,
   9,
   11,
   13,
   15,
   17,
   19,
   21,
   23,
   25,
   0,
   32,
   35,
   37,
   39,
};

static const nir_algebraic_table bifrost_nir_lower_algebraic_late_table = {
   .transforms = bifrost_nir_lower_algebraic_late_transforms,
   .transform_offsets = bifrost_nir_lower_algebraic_late_transform_offsets,
   .pass_op_table = bifrost_nir_lower_algebraic_late_pass_op_table,
   .values = bifrost_nir_lower_algebraic_late_values,
   .expression_cond = NULL,
   .variable_cond = NULL,
};

bool
bifrost_nir_lower_algebraic_late(
   nir_shader *shader
) {
   bool progress = false;
   bool condition_flags[1];
   const nir_shader_compiler_options *options = shader->options;
   const shader_info *info = &shader->info;
   (void) options;
   (void) info;

   STATIC_ASSERT(96 == ARRAY_SIZE(bifrost_nir_lower_algebraic_late_values));
   condition_flags[0] = true;

   nir_foreach_function_impl(impl, shader) {
     progress |= nir_algebraic_impl(impl, condition_flags, &bifrost_nir_lower_algebraic_late_table);
   }

   return progress;
}

