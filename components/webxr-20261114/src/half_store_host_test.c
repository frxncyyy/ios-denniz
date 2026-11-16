/*
 * y2s47 host test (no GPU, no phone): a kernel that writes `half` values and 16-bit unsigned values to textures (half_store.ll), as every
 * write of GNR.videoprocessor's pyramid does, through the translator (air_to_nir, the files this library is built from). Checked in the NIR:
 * each image_store's value is four 32-bit components made from the 16-bit ones by the conversion that keeps the value (f2f32, u2u32) and
 * its type says so (float32, uint32) -- which is what Panfrost's compiler takes (bi_emit_image_store: four 32-bit registers, converted by
 * the image's own format). Then the kernel through that compiler, with its assertions on. (softpipe has no 16-bit arithmetic at all --
 * nir_to_tgsi stops at u2f16 -- so it cannot run this kernel; the conversions themselves are NIR's and exact.)
 *   half_store_host_test half_store.bc
 */
#include "air_metal.c"
#include "panfrost/compiler/bifrost_compile.h"
#include "panfrost/util/pan_ir.h"
#include "util/u_dynarray.h"

int
main(int argc, char **argv)
{
	static uint8_t bitcode[1 << 16];
	FILE *f = argc == 2 ? fopen(argv[1], "rb") : NULL;
	size_t size = f ? fread(bitcode, 1, sizeof(bitcode), f) : 0;
	void *mem_ctx = ralloc_context(NULL);
	const struct air_to_nir_options options = { 0 };
	struct air_shader_info info = { 0 };
	const char *error = NULL;
	unsigned stores = 0, good = 0;

	glsl_type_singleton_init_or_ref();
	nir_shader *nir = size > 4 ? air_to_nir(mem_ctx, bitcode, size, "nord2_half_store", &options, &bifrost_nir_options_v9, &info, &error) : NULL;
	if (!nir) {
		fprintf(stderr, "FAILED: not translated: %s\n", error ? error : "no module");
		return 1;
	}
	nir_foreach_function_impl(impl, nir) {
		nir_foreach_block(block, impl) {
			nir_foreach_instr(instr, block) {
				if (instr->type != nir_instr_type_intrinsic || nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_image_store)
					continue;
				nir_intrinsic_instr *store = nir_instr_as_intrinsic(instr);
				nir_alu_type type = nir_intrinsic_src_type(store);
				bool from_16 = false, right_op = false;

				stores++;
				/* the value is a vec4 of the converted components: follow one to its conversion */
				nir_scalar s = nir_scalar_resolved(store->src[3].ssa, 0);
				if (nir_scalar_is_alu(s)) {
					nir_op op = nir_scalar_alu_op(s);
					from_16 = nir_scalar_chase_alu_src(s, 0).def->bit_size == 16;
					right_op = type == nir_type_float32 ? op == nir_op_f2f32 : type == nir_type_uint32 ? op == nir_op_u2u32 : false;
				}
				printf("  image_store %u: %u bits a component, type %s, made by %s of %s-bit values\n", stores, store->src[3].ssa->bit_size,
				    type == nir_type_float32 ? "float32" : type == nir_type_uint32 ? "uint32" : type == nir_type_int32 ? "int32" : "SOMETHING ELSE",
				    right_op ? "the value-keeping conversion" : "SOMETHING ELSE", from_16 ? "16" : "other");
				good += store->src[3].ssa->bit_size == 32 && from_16 && right_op;
			}
		}
	}
	nir_shader *clone = nir_shader_clone(NULL, nir);
	setenv("RWA_SOFTWARE", "1", 1);
	char why[512] = "";
	struct rwa_device *device = rwa_device_open(why, sizeof(why));
	if (!device) {
		fprintf(stderr, "FAILED: no device for finalize_for_gallium: %s\n", why);
		return 1;
	}
	finalize_for_gallium(clone, device->screen);
	nir_shader_gather_info(clone, nir_shader_get_entrypoint(clone));
	bifrost_preprocess_nir(clone, 0x9091);
	struct panfrost_compile_inputs inputs = { .gpu_id = 0x9091 };
	struct util_dynarray binary;
	struct pan_shader_info pinfo = { 0 };
	util_dynarray_init(&binary, NULL);
	bifrost_compile_shader_nir(clone, &inputs, &binary, &pinfo);
	printf("half_store_host_test: %u of %u stores are 32-bit values that say what they are; the kernel is %u bytes of Valhall code (Mali-G77)\n",
	    good, stores, (unsigned)binary.size);
	return !(stores == 2 && good == 2 && binary.size > 0);
}
