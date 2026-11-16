/*
 * y2s99b: shader99b_numeric.ll (sincos/modf/frexp through a pointer, fmin3/fmax3/fmedian3, snorm packing, a callee returning a
 * struct with an array member built from undef, nested insertvalue into undef, a buffer without air.address_space) through the
 * translator, evaluated with NIR's constant folder, against plain C; then Panfrost's compiler.  The evaluator is y2s42's:
 * y2s42 host test (no GPU, no phone): translator_numeric.ll through the translator (air_to_nir, the files this library is built from), the
 * NIR it gives EVALUATED instruction by instruction with NIR's own constant folder (nir_eval_const_opcode: the definition of every opcode,
 * which every back end implements), every stored word against plain C; then the same NIR through finalize_for_gallium and Panfrost's
 * Valhall compiler for the Mali-G77.  What it shows: the translator's air.pack.* / air.unpack.* (float and half), its vector bitcasts and
 * its memset/memcpy of lengths that are not multiples of four mean what Metal defines, and the GPU's compiler takes the result.  What it
 * cannot show: the execution of Panfrost's code (only the phone has a Mali), and VideoToolbox's own kernel, whose specialised module exists
 * only on the phone.  (softpipe is not used: it has no 16-bit types, and the kernel is about them.)
 *   translator_numeric_host_test numeric.bc      (llvm-as translator_numeric.ll; see host-tests.sh for the reader's one host-only line)
 */
#include "air_metal.c"
#include "panfrost/compiler/bifrost_compile.h"
#include "panfrost/util/pan_ir.h"
#include "util/u_dynarray.h"
#include "nir_constant_expressions.h"
#include <math.h>

enum { THREADS = 8192, IN = 8, OUT = 16 };

/* -- the evaluator: one block, constants, ALU, and the five intrinsics the kernel is made of -- */
struct machine {
	nir_const_value (*values)[NIR_MAX_VEC_COMPONENTS];
	const uint8_t *in;
	uint8_t *out, scratch[256];
	unsigned tid;
};

static void
bytes_of(const nir_const_value *v, unsigned comps, unsigned bits, uint8_t *to)
{
	for (unsigned i = 0; i < comps; i++) {
		uint64_t u = nir_const_value_as_uint(v[i], bits);
		memcpy(to + i * bits / 8, &u, bits / 8);
	}
}

static void
values_of(const uint8_t *from, unsigned comps, unsigned bits, nir_const_value *v)
{
	for (unsigned i = 0; i < comps; i++) {
		uint64_t u = 0;
		memcpy(&u, from + i * bits / 8, bits / 8);
		v[i] = nir_const_value_for_raw_uint(u, bits);
	}
}

static int
evaluate(nir_shader *nir, struct machine *m)
{
	nir_function_impl *impl = nir_shader_get_entrypoint(nir);

	if (nir_start_block(impl) != nir_impl_last_block(impl)) {
		fprintf(stderr, "the kernel is not one block\n");
		return -1;
	}
	nir_foreach_block(block, impl) {
		nir_foreach_instr(instr, block) {
			if (instr->type == nir_instr_type_load_const) {
				nir_load_const_instr *k = nir_instr_as_load_const(instr);
				memcpy(m->values[k->def.index], k->value, sizeof(nir_const_value) * k->def.num_components);
			} else if (instr->type == nir_instr_type_alu) {
				nir_alu_instr *alu = nir_instr_as_alu(instr);
				nir_const_value src[NIR_MAX_VEC_COMPONENTS][NIR_MAX_VEC_COMPONENTS], *srcs[NIR_MAX_VEC_COMPONENTS];
				unsigned bit_size = alu->def.bit_size;

				for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
					for (unsigned c = 0; c < nir_ssa_alu_instr_src_components(alu, i); c++)
						src[i][c] = m->values[alu->src[i].src.ssa->index][alu->src[i].swizzle[c]];
					srcs[i] = src[i];
					if (nir_alu_type_get_type_size(nir_op_infos[alu->op].input_types[i]) == 0)
						bit_size = alu->src[i].src.ssa->bit_size;
				}
				if (nir_alu_type_get_type_size(nir_op_infos[alu->op].output_type) == 0)
					bit_size = alu->def.bit_size;
				nir_eval_const_opcode(alu->op, m->values[alu->def.index], alu->def.num_components, bit_size, srcs,
						      nir->info.float_controls_execution_mode);
			} else if (instr->type == nir_instr_type_intrinsic) {
				nir_intrinsic_instr *x = nir_instr_as_intrinsic(instr);
#define U32(n) ((unsigned)nir_const_value_as_uint(m->values[x->src[n].ssa->index][0], 32))
				switch (x->intrinsic) {
				case nir_intrinsic_load_global_invocation_id:
					m->values[x->def.index][0] = nir_const_value_for_uint(m->tid, 32);
					m->values[x->def.index][1] = m->values[x->def.index][2] = nir_const_value_for_uint(0, 32);
					break;
				case nir_intrinsic_load_ssbo:
					if (U32(0) != 0 || U32(1) + x->def.num_components * x->def.bit_size / 8 > THREADS * IN * 4)
						return fprintf(stderr, "a load outside the input buffer\n"), -1;
					values_of(m->in + U32(1), x->def.num_components, x->def.bit_size, m->values[x->def.index]);
					break;
				case nir_intrinsic_store_ssbo:
					if (U32(1) != 1 || U32(2) + x->src[0].ssa->num_components * x->src[0].ssa->bit_size / 8 > THREADS * OUT * 4)
						return fprintf(stderr, "a store outside the output buffer\n"), -1;
					bytes_of(m->values[x->src[0].ssa->index], x->src[0].ssa->num_components, x->src[0].ssa->bit_size, m->out + U32(2));
					break;
				case nir_intrinsic_load_scratch:
					if (U32(0) + x->def.num_components * x->def.bit_size / 8 > sizeof(m->scratch))
						return fprintf(stderr, "a load outside the scratch\n"), -1;
					values_of(m->scratch + U32(0), x->def.num_components, x->def.bit_size, m->values[x->def.index]);
					break;
				case nir_intrinsic_store_scratch:
					if (U32(1) + x->src[0].ssa->num_components * x->src[0].ssa->bit_size / 8 > sizeof(m->scratch))
						return fprintf(stderr, "a store outside the scratch\n"), -1;
					bytes_of(m->values[x->src[0].ssa->index], x->src[0].ssa->num_components, x->src[0].ssa->bit_size, m->scratch + U32(1));
					break;
				case nir_intrinsic_nop:
					break;
				default:
					return fprintf(stderr, "an intrinsic this evaluator does not know: %s\n", nir_intrinsic_infos[x->intrinsic].name), -1;
				}
#undef U32
			} else if (instr->type == nir_instr_type_undef) {
				nir_undef_instr *u = nir_instr_as_undef(instr);       /* (an insertelement's first operand: any value will do) */
				memset(m->values[u->def.index], 0xa5, sizeof(nir_const_value) * u->def.num_components);
			} else {
				return fprintf(stderr, "an instruction this evaluator does not know (type %d)\n", instr->type), -1;
			}
		}
	}
	return 0;
}


static int
near(float got, float want, float tol)
{
	if (isnan(want))
		return isnan(got);
	return fabsf(got - want) <= tol * (1.0f + fabsf(want));
}

static float
f_of(uint32_t u)
{
	float f;
	memcpy(&f, &u, 4);
	return f;
}

static uint32_t
u_of(float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}

static uint32_t
snorm(float x, unsigned bits)
{
	float most = (float)((1u << (bits - 1)) - 1), c = x < -1.0f ? -1.0f : x > 1.0f ? 1.0f : x;
	return (uint32_t)(int32_t)nearbyintf(c * most) & ((1u << bits) - 1);
}

int
main(int argc, char **argv)
{
	static const char *const what[OUT] = {
		"sincos: sin", "sincos: cos (pointer)", "modf: fraction", "modf: integer (pointer)", "frexp: mantissa",
		"frexp: exponent (pointer)", "fmin3", "fmax3", "fmedian3", "pack snorm4x8", "unpack snorm4x8 y+w", "pack snorm2x16",
		"struct callee: array[0]", "struct callee: array[1] ^ i32 field", "struct callee: array[2]", "nested undef insert + half sincos" };
	static const float special[] = { 0.0f, 1.0f, -0.0f, 0.5f, 1e30f, -1e30f, 1e-30f, 0.999999f, 1.000001f, -0.25f, 0.25f, 2.0f,
					 -1.0f, -1.5f, 3.75f, -7.25f, INFINITY, -INFINITY, 100.5f, -0.75f };
	unsigned bad[OUT] = { 0 };

	if (argc != 2) {
		fprintf(stderr, "usage: %s numeric.bc\n", argv[0]);
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");
	static unsigned char bitcode[1 << 20];
	size_t size = f ? fread(bitcode, 1, sizeof(bitcode), f) : 0;
	if (size <= 4) {
		fprintf(stderr, "FAILED: %s is not a module\n", argv[1]);
		return 1;
	}
	fclose(f);

	glsl_type_singleton_init_or_ref();      /* (as rwa_device_open does in the library before any translation) */
	void *mem_ctx = ralloc_context(NULL);
	struct air_shader_info info = { 0 };
	const struct air_to_nir_options options = { 0 };
	const char *error = NULL;
	nir_shader *nir = air_to_nir(mem_ctx, bitcode, size, "nord2_translator_numeric", &options, &bifrost_nir_options_v9, &info, &error);
	if (!nir) {
		fprintf(stderr, "FAILED: not translated: %s\n", error ? error : "?");
		return 1;
	}
	nir_validate_shader(nir, "after the translator");
	nir_index_ssa_defs(nir_shader_get_entrypoint(nir));
	if (info.num_resources < 2 || info.resources[0].kind != AIR_RESOURCE_DEVICE_BUFFER) {
		fprintf(stderr, "FAILED: the buffer without air.address_space is not a device buffer\n");
		return 1;
	}
	static uint32_t iw[THREADS * IN], ow[THREADS * OUT];
	struct machine m = { .values = calloc(nir_shader_get_entrypoint(nir)->ssa_alloc, sizeof(*m.values)), .in = (const uint8_t *)iw,
			     .out = (uint8_t *)ow };

	srand(99);
	for (unsigned t = 0; t < THREADS; t++) {
		uint32_t *w = iw + t * IN;
		float v[4];

		w[0] = t < 4 ? (uint32_t[]){ 0, 0xffffffffu, 0x80808080u, 0x7f817f01u }[t] : ((uint32_t)rand() << 16) ^ (uint32_t)rand();
		w[1] = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
		for (unsigned i = 0; i < 4; i++) {
			unsigned kind = (t + i) % 4;

			if (kind == 0)
				v[i] = special[(t / 4 + i) % (sizeof(special) / sizeof(special[0]))];
			else if (kind == 1)
				v[i] = (float)rand() / (float)RAND_MAX * 4.0f - 2.0f;
			else if (kind == 2)
				v[i] = (float)(rand() % 2001 - 1000) / 8.0f;
			else
				v[i] = (float)rand() / (float)RAND_MAX * 20.0f - 10.0f;
		}
		if (t % 7 == 3)
			v[3] = 1.0f;      /* keep some half inputs ordinary */
		memcpy(w + 2, v, sizeof(v));
		w[6] = w[7] = 0;
	}
	memset(ow, 0xee, sizeof(ow));
	for (m.tid = 0; m.tid < THREADS; m.tid++) {
		memset(m.scratch, 0xcc, sizeof(m.scratch));
		if (evaluate(nir, &m) != 0)
			return 1;
	}
	for (unsigned t = 0; t < THREADS; t++) {
		const uint32_t *w = iw + t * IN, *o = ow + t * OUT, w0 = w[0], w1 = w[1];
		float v[4];
		int ok[OUT];

		memcpy(v, w + 2, sizeof(v));
		/* sin/cos: the evaluator's fsin is libm's; only finite inputs of a sane size are compared (the phone's would differ anyway) */
		int sane = isfinite(v[0]) && fabsf(v[0]) < 1e6f;
		ok[0] = !sane || near(f_of(o[0]), sinf(v[0]), 1e-6f);
		ok[1] = !sane || near(f_of(o[1]), cosf(v[0]), 1e-6f);
		float ip = truncf(v[1]), fr = isinf(v[1]) ? copysignf(0.0f, v[1]) : v[1] - ip;
		ok[2] = u_of(f_of(o[2])) == u_of(fr) || (fr == 0.0f && f_of(o[2]) == 0.0f);
		ok[3] = f_of(o[3]) == ip || (isnan(ip) && isnan(f_of(o[3])));
		int e = 0;
		float mant = frexpf(v[2], &e);
		ok[4] = !isfinite(v[2]) || v[2] == 0.0f || f_of(o[4]) == mant;
		ok[5] = !isfinite(v[2]) || v[2] == 0.0f || (int)o[5] == e;
		ok[6] = f_of(o[6]) == fminf(fminf(v[0], v[1]), v[2]);
		ok[7] = f_of(o[7]) == fmaxf(fmaxf(v[0], v[1]), v[2]);
		float lo = fminf(v[0], v[1]), hi = fmaxf(v[0], v[1]);
		ok[8] = f_of(o[8]) == fmaxf(lo, fminf(hi, v[2]));
		uint32_t p4 = 0;
		for (unsigned i = 0; i < 4; i++)
			p4 |= snorm(v[i], 8) << (8 * i);
		ok[9] = o[9] == p4;
		float y = fmaxf((float)(int8_t)(w0 >> 8) / 127.0f, -1.0f), ww = fmaxf((float)(int8_t)(w0 >> 24) / 127.0f, -1.0f);
		ok[10] = near(f_of(o[10]), y + ww, 1e-6f);
		ok[11] = o[11] == (snorm(v[0], 16) | snorm(v[1], 16) << 16);
		ok[12] = u_of(f_of(o[12])) == u_of(v[0] * 2.0f) || (isnan(v[0] * 2.0f) && isnan(f_of(o[12])));
		float e1 = v[1] + 1.0f, e2 = v[2] - v[0];
		ok[13] = (o[13] ^ (w1 + 7u)) == u_of(e1) || (isnan(e1) && isnan(f_of(o[13] ^ (w1 + 7u))));
		ok[14] = o[14] == u_of(e2) || (isnan(e2) && isnan(f_of(o[14])));
		uint32_t hh = o[15] ^ w1;
		_Float16 hs, hc, h0 = (_Float16)v[3];
		uint16_t a = (uint16_t)hh, b = (uint16_t)(hh >> 16);
		memcpy(&hs, &a, 2);
		memcpy(&hc, &b, 2);
		int hsane = isfinite((float)h0) && fabsf((float)h0) < 100.0f;
		ok[15] = !hsane || (near((float)hs, sinf((float)h0), 2e-3f) && near((float)hc, cosf((float)h0), 2e-3f));
		for (unsigned k = 0; k < OUT; k++)
			if (!ok[k] && bad[k]++ < 3)
				printf("  %s, thread %u: got %08x (words %08x %08x, floats %g %g %g %g)\n", what[k], t, o[k], w0, w1, v[0], v[1],
				       v[2], v[3]);
	}
	unsigned total = 0;
	for (unsigned k = 0; k < OUT; k++) {
		printf("%-40s %u of %u differ\n", what[k], bad[k], THREADS);
		total += bad[k];
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
	printf("shader99b_numeric_host_test: %u threads evaluated, %u results differ; Valhall code %u bytes, thread storage %u bytes\n",
	       THREADS, total, (unsigned)binary.size, pinfo.tls_size);
	if (total == 0 && binary.size != 0)
		printf("shader99b_numeric: ok\n");
	return total != 0 || binary.size == 0;
}
