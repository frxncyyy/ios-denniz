/* SPDX-License-Identifier: MIT */
/*
 * k_v11_media.c -- differential kernels for the V11 instruction class
 * (A32 extend / bit field / reverse / saturate / pack / select / parallel
 * add-subtract / QADD / 16x16 multiplies / USAD8 / divide).
 *
 * These are written for T01's differential harness (gpu/hwcodec/tests/emu):
 * drop this file into tests/emu/kernels/ and every k_v11_* function is picked
 * up automatically, compiled natively (reference) and for ARM in each
 * configuration.  They follow the kernel rules in tests/emu/README.md: only
 * exact-width types, unsigned arithmetic where overflow can occur, no signed
 * overflow, no division by zero, no shift count outside 0..width-1.
 *
 * The value of each kernel is that an ARMv7 compiler lowers these C idioms to
 * the V11 instructions; the ARM object's instruction mix was checked with
 * llvm-objdump (see gpu/hwcodec/emu/test_v11/STATUS.md).  Whatever the
 * compiler emits, the interpreter must produce the same result as the host.
 */
#include <stdint.h>

/* local helpers (unique names so this file can be linked with the other kernels) */
static uint32_t kv11_rotl(uint32_t v, unsigned n)
{
	return (v << n) | (v >> (32u - n));
}

static uint32_t kv11_mix(uint32_t a, uint32_t b)
{
	return (a ^ kv11_rotl(b, 13)) + 0x9e3779b9u;
}

/* SXTB SXTH UXTB UXTH (one rotated source makes the compiler use ROR forms) */
uint32_t k_v11_extend(uint32_t seed)
{
	uint32_t a = seed ^ 0x89abcdefu;
	uint32_t rot = kv11_rotl(a, 8);
	int32_t s8 = (int32_t)(int8_t)(uint8_t)a;
	int32_t s16 = (int32_t)(int16_t)(uint16_t)(a >> 8);
	uint32_t u8 = (uint32_t)(uint8_t)(a >> 16);
	uint32_t u16 = (uint32_t)(uint16_t)(a >> 4);
	uint32_t r = (uint32_t)s8 * 3u + (uint32_t)s16 + (u8 << 8) + u16;

	return kv11_mix(r, rot);
}

/* UBFX SBFX BFI BFC (byte/bit insertion and extraction) */
uint32_t k_v11_bitfield(uint32_t seed)
{
	uint32_t a = seed ^ 0x5a5a5a5au;
	uint32_t b = kv11_rotl(seed, 11);
	uint32_t f = (a >> 5) & 0xffu;                 /* UBFX */
	uint32_t g = ((b >> 12) & 0x1ffu) << 7;        /* UBFX + shift */
	int32_t sf = (int32_t)(a << 20) >> 24;         /* SBFX */
	uint32_t ins = (a & ~(0xffu << 8)) | ((b & 0xffu) << 8);  /* BFI */
	uint32_t clr = b & ~(0x7fu << 19);             /* BFC */

	return kv11_mix(kv11_mix(f, g), kv11_mix((uint32_t)sf, kv11_mix(ins, clr)));
}

/* REV REV16 REVSH RBIT */
uint32_t k_v11_reverse(uint32_t seed)
{
	uint32_t a = seed ^ 0x12345678u;
	uint32_t rev = __builtin_bswap32(a);
	uint32_t rev16 = ((a & 0x00ff00ffu) << 8) | ((a & 0xff00ff00u) >> 8);
	uint32_t swapped_lo = ((a & 0xffu) << 8) | ((a >> 8) & 0xffu);
	int32_t revsh = (int32_t)(int16_t)(uint16_t)swapped_lo;
	uint32_t rb = __builtin_bitreverse32(a);

	return kv11_mix(kv11_mix(rev, rev16), kv11_mix((uint32_t)revsh, rb));
}

/* SSAT USAT: clamped narrowing idioms */
uint32_t k_v11_sat(uint32_t seed)
{
	int32_t a = (int32_t)seed;
	int32_t b = (int32_t)kv11_rotl(seed, 9);
	int32_t c = (int32_t)(seed >> 1);
	int32_t s16 = a > 32767 ? 32767 : (a < -32768 ? -32768 : a);
	int32_t s8 = b > 127 ? 127 : (b < -128 ? -128 : b);
	uint32_t u8 = c < 0 ? 0u : (c > 255 ? 255u : (uint32_t)c);
	uint32_t u16 = c < 0 ? 0u : (c > 65535 ? 65535u : (uint32_t)c);
	uint32_t r = ((uint32_t)s16 ^ (uint32_t)s8) + (u8 << 9) + (u16 >> 3);

	return kv11_mix(r, kv11_mix((uint32_t)s8, u8));
}

/* QADD QSUB: saturating 32-bit add/subtract idioms */
uint32_t k_v11_qadd(uint32_t seed)
{
	int32_t a = (int32_t)seed;
	int32_t b = (int32_t)kv11_rotl(seed, 9);
	int64_t s = (int64_t)a + (int64_t)b;
	int64_t d = (int64_t)a - (int64_t)b;
	int32_t sa = s > 2147483647LL ? 2147483647 : (s < -2147483648LL ? (int32_t)-2147483648 : (int32_t)s);
	int32_t sd = d > 2147483647LL ? 2147483647 : (d < -2147483648LL ? (int32_t)-2147483648 : (int32_t)d);

	return kv11_mix((uint32_t)sa, (uint32_t)sd);
}

/* PKHBT PKHTB SEL (16-bit lane packing and byte selection) */
uint32_t k_v11_pack(uint32_t seed)
{
	uint32_t a = seed ^ 0xa5a5a5a5u;
	uint32_t b = kv11_rotl(seed, 17);
	uint32_t hi = (a & 0xffff0000u) | (b & 0x0000ffffu);   /* PKHBT-like */
	uint32_t lo = (b & 0xffff0000u) | (a & 0x0000ffffu);   /* PKHTB-like */
	uint32_t sel = 0;
	unsigned i;

	for (i = 0; i < 4; i++) {
		uint32_t m = 0xffu << (8u * i);
		sel |= ((a >> (8u * i)) & 0x80u) ? (a & m) : (b & m);
	}
	return kv11_mix(kv11_mix(hi, lo), sel);
}

/* SMULxy SMLAxy SMULWy SMULWy: 16-bit signed products and accumulations */
uint32_t k_v11_mul16(uint32_t seed)
{
	int32_t a = (int32_t)(seed & 0xffffu) - (seed & 0x8000u ? 65536 : 0);
	int32_t b = (int32_t)((seed >> 16) & 0xffffu) - (seed & 0x80000000u ? 65536 : 0);
	int32_t c = (int32_t)(kv11_rotl(seed, 7) & 0xffffu) - (kv11_rotl(seed, 7) & 0x8000u ? 65536 : 0);
	int32_t d = (int32_t)((seed >> 3) & 0xffffu) - (seed & 0x00080000u ? 65536 : 0);
	uint32_t p1 = (uint32_t)(a * b);
	uint32_t p2 = (uint32_t)(a * b + c);            /* SMLAxy */
	uint32_t p3 = (uint32_t)((int64_t)a * d >> 16); /* SMULWy */
	uint32_t p4 = (uint32_t)(((int64_t)c * d >> 16) + b);

	return kv11_mix(kv11_mix(p1, p2), kv11_mix(p3, p4));
}

/* SMUAD SMUSD SMLAD SMLSD SMMUL SMMLA (dual 16x16 multiply / most significant word) */
uint32_t k_v11_dual(uint32_t seed)
{
	uint32_t a = seed ^ 0x3c3c3c3cu;
	uint32_t b = kv11_rotl(seed, 5) ^ 0xc3c3c3c3u;
	int32_t al = (int32_t)(a & 0xffffu) - (a & 0x8000u ? 65536 : 0);
	int32_t ah = (int32_t)(a >> 16) - (int32_t)(a & 0x80000000u ? 32768 : 0);
	int32_t bl = (int32_t)(b & 0xffffu) - (b & 0x8000u ? 65536 : 0);
	int32_t bh = (int32_t)(b >> 16) - (int32_t)(b & 0x80000000u ? 32768 : 0);
	uint32_t add = (uint32_t)(al * bl + ah * bh);          /* SMUAD */
	uint32_t sub = (uint32_t)(al * bl - ah * bh);          /* SMUSD */
	uint32_t msw = (uint32_t)(((int64_t)al * bh) >> 32);   /* SMMUL-ish */

	return kv11_mix(kv11_mix(add, sub), kv11_mix(msw, (uint32_t)(al * bh)));
}

/* USAD8 USADA8: sum of absolute byte differences */
uint32_t k_v11_usad(uint32_t seed)
{
	uint32_t a = seed ^ 0x0f0f0f0fu;
	uint32_t b = kv11_rotl(seed, 21);
	uint32_t sum = 0;
	unsigned i;

	for (i = 0; i < 4; i++) {
		uint32_t x = (a >> (8u * i)) & 0xffu;
		uint32_t y = (b >> (8u * i)) & 0xffu;

		sum += x > y ? x - y : y - x;
	}
	return kv11_mix(sum + seed, a ^ b);
}

/* SDIV UDIV with zero and INT_MIN/-1 guards (division by zero never occurs) */
uint32_t k_v11_div(uint32_t seed)
{
	int32_t a = (int32_t)(seed | 1u);
	int32_t b = (int32_t)(kv11_rotl(seed, 3) | 1u);
	uint32_t ua = seed | 1u;
	uint32_t ub = (kv11_rotl(seed, 11) | 1u);
	int32_t q = (a == (int32_t)0x80000000u && b == -1) ? a : a / b;
	uint32_t uq = ua / ub;

	return kv11_mix((uint32_t)q, uq);
}
