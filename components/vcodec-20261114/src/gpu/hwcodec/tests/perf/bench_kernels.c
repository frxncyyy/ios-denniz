/* SPDX-License-Identifier: MIT */
/*
 * bench_kernels.c -- guest-side micro benchmarks for the n2a32 interpreter (opus-perf).
 * Compiled for ARM (A32 and T32, -O2) as a self-contained ET_DYN object: no imports, no libc.
 * Every kernel takes an iteration count and returns a checksum (so the work cannot be removed).
 * The shapes imitate what a codec driver library runs in software: integer bookkeeping, struct
 * loads/stores and word copies, branchy control flow, an Exp-Golomb bit writer (header writers),
 * float rate-control arithmetic, and a NEON 8-bit pixel loop.
 */
#include <stdint.h>
#include <stddef.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

static uint32_t buf_a[1024], buf_b[1024];
static uint8_t pix_a[4096], pix_b[4096], pix_c[4096];
static uint8_t bits[8192];

/* integer ALU: shifts, rotates, multiply, compare */
uint32_t b_alu(uint32_t n)
{
	uint32_t h = 0x811c9dc5u, x = n;
	for (uint32_t i = 0; i < n; i++) {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		h = (h ^ x) * 0x01000193u;
		h += (h >> 7) | (h << 25);
		if ((int32_t)h < 0)
			h -= i;
	}
	return h;
}

/* loads/stores: word copy 4 KiB, then a strided struct-like read-modify-write */
struct ent { uint32_t a; uint16_t b; uint8_t c, d; };
uint32_t b_mem(uint32_t n)
{
	uint32_t s = 0;
	for (uint32_t k = 0; k < 1024; k++)
		buf_a[k] = k * 2654435761u;
	for (uint32_t i = 0; i < n; i++) {
		volatile uint32_t *dst = buf_b;
		for (uint32_t k = 0; k < 1024; k += 4) {
			uint32_t w0 = buf_a[k], w1 = buf_a[k + 1], w2 = buf_a[k + 2], w3 = buf_a[k + 3];
			dst[k] = w0; dst[k + 1] = w1; dst[k + 2] = w2; dst[k + 3] = w3;
		}
		struct ent *e = (struct ent *)buf_b;
		for (uint32_t k = 0; k < 512; k++) {
			e[k].b = (uint16_t)(e[k].b + e[k].c);
			s += e[k].a ^ e[k].d;
		}
	}
	return s;
}

/* branches: insertion sort of 64 values + a small switch state machine */
uint32_t b_branch(uint32_t n)
{
	uint32_t v[64], s = 0, st = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t x = i * 747796405u + 2891336453u;
		for (int k = 0; k < 64; k++) {
			x = x * 1103515245u + 12345u;
			v[k] = x >> 8;
		}
		for (int k = 1; k < 64; k++) {
			uint32_t t = v[k];
			int j = k - 1;
			while (j >= 0 && v[j] > t) {
				v[j + 1] = v[j];
				j--;
			}
			v[j + 1] = t;
		}
		for (int k = 0; k < 64; k++) {
			switch ((v[k] ^ st) & 7) {
			case 0: st += 3; break;
			case 1: st ^= v[k]; break;
			case 2: st = (st << 1) | 1; break;
			case 3: st -= v[k] >> 3; break;
			case 4: st += k; break;
			default: st = st * 5 + 1; break;
			}
		}
		s += v[0] + v[63] + st;
	}
	return s;
}

/* bit writer: ue(v)/se(v) Exp-Golomb, the shape of SPS/PPS/slice header writers */
struct bw { uint8_t *p; uint32_t cache; int left; uint32_t bytes; };
static void bw_put(struct bw *w, uint32_t v, int n)
{
	while (n > 0) {
		int take = n < w->left ? n : w->left;
		uint32_t chunk = (v >> (n - take)) & ((1u << take) - 1u);
		w->cache = (w->cache << take) | chunk;
		w->left -= take;
		n -= take;
		if (w->left == 0) {
			w->p[w->bytes & 8191u] = (uint8_t)w->cache;
			w->bytes++;
			w->cache = 0;
			w->left = 8;
		}
	}
}
static void bw_ue(struct bw *w, uint32_t v)
{
	uint32_t x = v + 1;
	int len = 32 - __builtin_clz(x);
	bw_put(w, 0, len - 1);
	bw_put(w, x, len);
}
uint32_t b_bitw(uint32_t n)
{
	struct bw w = { bits, 0, 8, 0 };
	uint32_t s = 0;
	for (uint32_t i = 0; i < n; i++) {
		bw_ue(&w, i & 255u);
		bw_ue(&w, (i * 7u) & 31u);
		int32_t se = (int32_t)(i % 13u) - 6;
		bw_ue(&w, se > 0 ? (uint32_t)(2 * se - 1) : (uint32_t)(-2 * se));
		bw_put(&w, i & 1u, 1);
	}
	for (uint32_t k = 0; k < 64; k++)
		s = s * 31u + bits[k];
	return s + w.bytes;
}

/* VFP: rate-control-like float/double arithmetic */
uint32_t b_vfp(uint32_t n)
{
	float q = 26.0f, fullness = 0.5f, target = 1000.0f;
	double acc = 0.0;
	for (uint32_t i = 0; i < n; i++) {
		float bits_used = (float)((i * 2654435761u) >> 20) + 500.0f;
		float err = (bits_used - target) / target;
		fullness = fullness * 0.95f + err * 0.05f;
		q += fullness * 0.25f;
		if (q < 10.0f) q = 10.0f;
		if (q > 51.0f) q = 51.0f;
		acc += (double)q * 1.0001 - (double)err * 0.5;
	}
	return (uint32_t)(int32_t)q ^ (uint32_t)(int32_t)(acc * (1.0 / 1024.0));
}

/* NEON: 8-bit pixel average + saturating add over 4 KiB (colour-conversion/scaling helper shape) */
uint32_t b_neon(uint32_t n)
{
	uint32_t s = 0;
	for (uint32_t k = 0; k < 4096; k++) {
		pix_a[k] = (uint8_t)(k * 7u);
		pix_b[k] = (uint8_t)(k * 13u + 5u);
	}
	for (uint32_t i = 0; i < n; i++) {
#if defined(__ARM_NEON)
		for (uint32_t k = 0; k < 4096; k += 16) {
			uint8x16_t a = vld1q_u8(pix_a + k), b = vld1q_u8(pix_b + k);
			uint8x16_t r = vqaddq_u8(vrhaddq_u8(a, b), vdupq_n_u8((uint8_t)i));
			vst1q_u8(pix_c + k, r);
		}
#else
		for (uint32_t k = 0; k < 4096; k++) {
			unsigned r = ((pix_a[k] + pix_b[k] + 1u) >> 1) + (i & 255u);
			pix_c[k] = (uint8_t)(r > 255u ? 255u : r);
		}
#endif
		s += pix_c[i & 4095u];
	}
	return s;
}
