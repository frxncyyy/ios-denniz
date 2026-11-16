/*
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

static uint32_t
field(float x, unsigned bits)          /* Metal's pack: clamp to 0..1, scale, round to nearest */
{
	const float most = (float)((1u << bits) - 1);
	volatile float t = x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f;

	t = t * most;
	t = t + 0.5f;
	return (uint32_t)t;
}

/* 1 when x*most is so near k + 0.5 that the neighbouring integer is as good an answer */
static int
tie(float x, unsigned bits)
{
	double t = (x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f) * (double)((1u << bits) - 1);

	return fabs(t - floor(t) - 0.5) < 1e-3;
}

static uint32_t
pack(const float *v, unsigned n, const unsigned *bits)
{
	uint32_t word = 0;
	unsigned shift = 0;

	for (unsigned i = 0; i < n; i++) {
		word |= field(v[i], bits[i]) << shift;
		shift += bits[i];
	}
	return word;
}

/* equal, or different only in fields whose input was a tie */
static int
same(uint32_t got, uint32_t want, const float *v, unsigned n, const unsigned *bits)
{
	unsigned shift = 0;

	for (unsigned i = 0; i < n; i++) {
		uint32_t mask = (1u << bits[i]) - 1, g = got >> shift & mask, w = want >> shift & mask;

		if (g != w && !(tie(v[i], bits[i]) && (g + 1 == w || w + 1 == g)))
			return 0;
		shift += bits[i];
	}
	return 1;
}

static float
as_half(float f)
{
	return (float)(_Float16)f;
}

int
main(int argc, char **argv)
{
	static const unsigned b4x8[4] = { 8, 8, 8, 8 }, b2x16[2] = { 16, 16 }, b10[4] = { 10, 10, 10, 2 };
	static const char *const what[OUT] = {
		"unpack/pack unorm4x8", "unpack/pack unorm2x16", "unpack/pack rgb10a2", "pack unorm4x8 (float)", "pack unorm2x16 (float)",
		"pack rgb10a2 (float)", "pack unorm4x8 (half)", "pack unorm2x16 (half)", "pack rgb10a2 (half)", "<4 x i16> as <2 x i32> [0]",
		"<4 x i16> as <2 x i32> [1]", "<2 x i32> as <4 x i16>", "<4 x half> as <2 x i32>", "unpack unorm2x16 (half)", "memset of 7 bytes",
		"memcpy of 6 bytes" };
	static const float special[] = { 0.0f, 1.0f, -0.0f, 0.5f, 1e30f, -1e30f, 1e-30f, 0.999999f, 1.000001f, -0.25f, 0.25f, 2.0f,
					 0.00196f, 0.5019608f, 0.49803922f, 0.7529412f };
	unsigned bad[OUT] = { 0 }, ties = 0;

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
	static uint32_t iw[THREADS * IN], ow[THREADS * OUT];
	struct machine m = { .values = calloc(nir_shader_get_entrypoint(nir)->ssa_alloc, sizeof(*m.values)), .in = (const uint8_t *)iw,
			     .out = (uint8_t *)ow };

	srand(42);
	for (unsigned t = 0; t < THREADS; t++) {
		uint32_t *w = iw + t * IN;
		float v[4];

		w[0] = t < 4 ? (uint32_t[]){ 0, 0xffffffffu, 0x80000001u, 0x12345678u }[t] : ((uint32_t)rand() << 16) ^ (uint32_t)rand();
		w[1] = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
		for (unsigned i = 0; i < 4; i++) {
			unsigned kind = (t + i) % 5;

			if (kind == 0)
				v[i] = special[(t / 5 + i) % (sizeof(special) / sizeof(special[0]))];
			else if (kind == 1)
				v[i] = (float)(rand() % 256) / 255.0f;
			else if (kind == 2)
				v[i] = (float)(rand() % 65536) / 65535.0f;
			else
				v[i] = (float)rand() / (float)RAND_MAX * 2.0f - 0.5f;
		}
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
		float v[4], h[4], uh[2];
		uint32_t want[OUT];
		uint16_t hb[2];
		_Float16 h16[2];

		memcpy(v, w + 2, sizeof(v));
		for (unsigned i = 0; i < 4; i++) {
			h[i] = as_half(v[i]);
			ties += tie(v[i], 8);
		}
		h16[0] = (_Float16)v[0];
		h16[1] = (_Float16)v[1];
		memcpy(hb, h16, sizeof(hb));
		for (unsigned i = 0; i < 2; i++) {
			volatile float u = (float)(w0 >> (16 * i) & 0xffff);

			u = u * (float)(1.0 / 65535.0);
			uh[i] = as_half(u);
		}
		want[0] = want[1] = want[2] = w0;
		want[3] = pack(v, 4, b4x8);
		want[4] = pack(v, 2, b2x16);
		want[5] = pack(v, 4, b10);
		want[6] = pack(h, 4, b4x8);
		want[7] = pack(h, 2, b2x16);
		want[8] = pack(h, 4, b10);
		want[9] = w0;
		want[10] = w1;
		want[11] = (w0 & 0xffffu) | (w1 & 0xffff0000u);
		want[12] = (uint32_t)hb[0] | (uint32_t)hb[1] << 16;
		want[13] = pack(uh, 2, b2x16);
		want[14] = (w1 & 0xff000000u) | 0x005a5a5au;
		want[15] = (w1 & 0xffffu) | 0x22220000u;
		for (unsigned k = 0; k < OUT; k++) {
			int ok = o[k] == want[k];

			if (!ok && k >= 3 && k <= 5)
				ok = same(o[k], want[k], v, k == 4 ? 2 : 4, k == 3 ? b4x8 : k == 4 ? b2x16 : b10);
			if (!ok && k >= 6 && k <= 8)
				ok = same(o[k], want[k], h, k == 7 ? 2 : 4, k == 6 ? b4x8 : k == 7 ? b2x16 : b10);
			if (!ok && k == 13)
				ok = same(o[k], want[k], uh, 2, b2x16);
			if (!ok && bad[k]++ < 3)
				printf("  %s, thread %u: got %08x, want %08x (words %08x %08x, floats %g %g %g %g)\n", what[k], t, o[k], want[k],
				       w0, w1, v[0], v[1], v[2], v[3]);
		}
	}
	unsigned total = 0;
	for (unsigned k = 0; k < OUT; k++) {
		printf("%-28s %u of %u differ\n", what[k], bad[k], THREADS);
		total += bad[k];
	}
	printf("translator_numeric_host_test: %u threads evaluated, %u words differ (%u inputs were rounding ties, where either neighbour is "
	       "accepted)\n", THREADS, total, ties);

	/* and the GPU's compiler on the same shader, as the library hands it over */
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
	printf("translator_numeric_host_test: the same kernel is %u bytes of Valhall code (Mali-G77), %u bytes of thread storage\n",
	       (unsigned)binary.size, pinfo.tls_size);
	return total != 0 || binary.size == 0;
}
