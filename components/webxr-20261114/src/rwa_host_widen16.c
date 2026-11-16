/*
 * y2s99b, HOST TESTS ONLY (linked into blur_chain_host_test; air_metal.c calls it only when built with -DRWA_HOST_WIDEN16):
 * softpipe (nir_to_tgsi) has no 16-bit values, and CoreAnimation's blur functions are all `_lph` (half precision). This turns a
 * translated shader's 16-bit values into 32-bit ones with the same numbers: every 16-bit def becomes 32-bit, a 16-bit float
 * constant is widened as a float (an integer one as an integer, decided by its uses), conversions to or from 16 bits become
 * moves or the 32-bit conversion, a 16-bit texture result a 32-bit one, a 16-bit store_output a 32-bit one, and a 16-bit
 * load_ubo reads the 32-bit words that hold the halves and unpacks them (unpack_half_2x16_split_x/y).
 * What this changes against the phone: intermediate values are not rounded to half after each operation (the CPU model rounds
 * the taps and weights, where the shader's own conversions are); the test's tolerance carries that.
 */
#include "nir.h"
#include "nir_builder.h"
#include <stdlib.h>

static bool
used_as_float(nir_def *def)
{
	nir_foreach_use(src, def) {
		nir_instr *u = nir_src_parent_instr(src);
		if (u->type == nir_instr_type_alu) {
			nir_alu_instr *a = nir_instr_as_alu(u);
			unsigned i = (unsigned)((nir_alu_src *)src - a->src);
			if (i < nir_op_infos[a->op].num_inputs &&
			    nir_alu_type_get_base_type(nir_op_infos[a->op].input_types[i]) == nir_type_float)
				return true;
		}
		if (u->type == nir_instr_type_intrinsic) {
			nir_intrinsic_instr *x = nir_instr_as_intrinsic(u);
			if (x->intrinsic == nir_intrinsic_store_output &&
			    nir_alu_type_get_base_type(nir_intrinsic_src_type(x)) == nir_type_float)
				return true;
		}
	}
	return false;
}

static nir_def *
unpack_half_at(nir_builder *b, unsigned index, nir_def *byte_off, const nir_intrinsic_instr *orig)
{
	nir_def *aligned = nir_iand_imm(b, byte_off, ~3u);
	nir_def *word = nir_load_ubo(b, 1, 32, nir_imm_int(b, index), aligned, .align_mul = 4, .align_offset = 0,
				     .range_base = nir_intrinsic_range_base(orig), .range = nir_intrinsic_range(orig));
	nir_def *hi = nir_ine_imm(b, nir_iand_imm(b, byte_off, 2), 0);
	nir_def *both = nir_unpack_half_2x16(b, word);
	return nir_bcsel(b, hi, nir_channel(b, both, 1), nir_channel(b, both, 0));
}

void rwa_host_widen16(nir_shader *nir);
void
rwa_host_widen16(nir_shader *nir)
{
	nir_foreach_variable_with_modes(var, nir, nir_var_shader_in | nir_var_shader_out) {
		const struct glsl_type *t = glsl_without_array(var->type);
		if (glsl_type_is_vector_or_scalar(t) && glsl_get_bit_size(t) == 16 && !glsl_type_is_array(var->type))
			var->type = glsl_vector_type(glsl_get_base_type(t) == GLSL_TYPE_FLOAT16 ? GLSL_TYPE_FLOAT :
						     glsl_get_base_type(t) == GLSL_TYPE_INT16 ? GLSL_TYPE_INT : GLSL_TYPE_UINT,
						     glsl_get_vector_elements(t));
	}
	nir_foreach_function_impl(impl, nir) {
		nir_builder b = nir_builder_create(impl);
		nir_foreach_block(block, impl) {
			nir_foreach_instr_safe(instr, block) {
				switch (instr->type) {
				case nir_instr_type_load_const: {
					nir_load_const_instr *k = nir_instr_as_load_const(instr);
					if (k->def.bit_size != 16)
						break;
					bool f = used_as_float(&k->def);
					for (unsigned i = 0; i < k->def.num_components; i++) {
						uint16_t raw = k->value[i].u16;
						if (f)
							k->value[i] = nir_const_value_for_float(_mesa_half_to_float(raw), 32);
						else
							k->value[i] = nir_const_value_for_int((int16_t)raw, 32);
					}
					k->def.bit_size = 32;
					break;
				}
				case nir_instr_type_undef: {
					nir_undef_instr *u = nir_instr_as_undef(instr);
					if (u->def.bit_size == 16)
						u->def.bit_size = 32;
					break;
				}
				case nir_instr_type_phi: {
					nir_phi_instr *p = nir_instr_as_phi(instr);
					if (p->def.bit_size == 16)
						p->def.bit_size = 32;
					break;
				}
				case nir_instr_type_alu: {
					nir_alu_instr *a = nir_instr_as_alu(instr);
					const nir_op_info *info = &nir_op_infos[a->op];
					unsigned out_size = nir_alu_type_get_type_size(info->output_type);
					unsigned in_size = info->num_inputs ? nir_alu_type_get_type_size(info->input_types[0]) : 0;
					bool conv = info->num_inputs == 1 && out_size != 0 && (in_size == 0 || in_size != out_size) &&
						    nir_op_is_vec(a->op) == false && (out_size == 16 || nir_src_bit_size(a->src[0].src) == 16 ||
						    out_size == 32);
					if (a->def.bit_size == 16)
						a->def.bit_size = 32;
					if (conv && (out_size == 16 || out_size == 32) && info->num_inputs == 1) {
						nir_alu_type st = info->input_types[0], dt = info->output_type;
						unsigned sbits = nir_src_bit_size(a->src[0].src);
						if (sbits == 16)
							sbits = 32;          /* (the source is widened too, or will be) */
						if (nir_alu_type_get_base_type(dt) == nir_type_bool)
							break;
						if (nir_alu_type_get_base_type(st) == nir_type_bool) {
							a->op = nir_type_conversion_op(nir_type_bool1, nir_alu_type_get_base_type(dt) | 32,
										       nir_rounding_mode_undef);
							break;
						}
						nir_alu_type s2 = nir_alu_type_get_base_type(st) | sbits;
						nir_alu_type d2 = nir_alu_type_get_base_type(dt) | 32;
						nir_op op = s2 == d2 || (nir_alu_type_get_base_type(st) != nir_type_float &&
									 nir_alu_type_get_base_type(dt) != nir_type_float && sbits == 32)
							    ? nir_op_mov : nir_type_conversion_op(s2, d2, nir_rounding_mode_undef);
						a->op = op;
					}
					break;
				}
				case nir_instr_type_tex: {
					nir_tex_instr *t = nir_instr_as_tex(instr);
					/* TGSI (softpipe) names the unit by ONE index: the texture's, with the sampler bound there too by the
					 * test (Panfrost and Metal keep the two apart; this is the host's limitation, as y2s46's txf note says) */
					t->sampler_index = t->texture_index;
					/* (diagnosis switch: an explicit level 0 sample as an implicit one -- the same texel on a one-level texture) */
					if (getenv("WIDEN_TXL_AS_TEX") && t->op == nir_texop_txl) {
						int li = nir_tex_instr_src_index(t, nir_tex_src_lod);
						if (li >= 0 && nir_src_is_const(t->src[li].src) && nir_src_as_float(t->src[li].src) == 0.0) {
							nir_tex_instr_remove_src(t, li);
							t->op = nir_texop_tex;
						}
					}
					if (t->def.bit_size == 16) {
						t->def.bit_size = 32;
						t->dest_type = nir_alu_type_get_base_type(t->dest_type) | 32;
					}
					break;
				}
				case nir_instr_type_intrinsic: {
					nir_intrinsic_instr *x = nir_instr_as_intrinsic(instr);
					if (x->intrinsic == nir_intrinsic_store_output) {
						nir_alu_type st = nir_intrinsic_src_type(x);
						if (nir_alu_type_get_type_size(st) == 16)
							nir_intrinsic_set_src_type(x, nir_alu_type_get_base_type(st) | 32);
						break;
					}
					if (x->intrinsic == nir_intrinsic_load_ubo && x->def.bit_size == 16 && nir_src_is_const(x->src[0])) {
						b.cursor = nir_before_instr(instr);
						nir_def *comps[4];
						for (unsigned i = 0; i < x->def.num_components; i++)
							comps[i] = unpack_half_at(&b, nir_src_as_uint(x->src[0]),
										  nir_iadd_imm(&b, x->src[1].ssa, 2 * i), x);
						nir_def *v = nir_vec(&b, comps, x->def.num_components);
						nir_def_rewrite_uses(&x->def, v);
						nir_instr_remove(instr);
						break;
					}
					if (nir_intrinsic_infos[x->intrinsic].has_dest && x->def.bit_size == 16)
						x->def.bit_size = 32;    /* (anything else: let validation say it) */
					break;
				}
				case nir_instr_type_deref: {
					nir_deref_instr *dr = nir_instr_as_deref(instr);
					if (dr->deref_type == nir_deref_type_var && (dr->var->data.mode & (nir_var_shader_in | nir_var_shader_out)))
						dr->type = dr->var->type;
					break;
				}
				default:
					break;
				}
			}
		}
		nir_metadata_preserve(impl, nir_metadata_none);
	}
	nir_validate_shader(nir, "after rwa_host_widen16");
}
