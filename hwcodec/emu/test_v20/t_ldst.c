/* SPDX-License-Identifier: MIT */
/*
 * t_ldst.c -- host unit tests for n2a32_neon_ldst (package V20).
 *
 * Encodings from ldst.s through llvm-mc. Guest memory is a 16 MiB calloc: the test only uses addresses
 * below 16 MiB, as the package contract requires. Expected bytes are literals derived from the ARM ARM
 * pseudocode for the element/structure layouts.
 */
#include "n2a32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enc_v20.h"

#define MEMSZ (16u << 20)
#define BASE  0x1000u

/* The MMIO hooks live in the core (V02); this test never marks a megabyte as MMIO, so these are never
 * called, but the link needs them because the load/store helpers reference them. */
uint32_t n2a32_mmio_load(struct n2a32_mem *m, uint32_t a, unsigned bytes)
{
	(void)m; (void)a; (void)bytes;
	return 0;
}

void n2a32_mmio_store(struct n2a32_mem *m, uint32_t a, unsigned bytes, uint32_t v)
{
	(void)m; (void)a; (void)bytes; (void)v;
}

static struct n2a32_cpu cpu;
static struct n2a32_mem m;
static uint8_t *mem;
static int checks, fails;

static void rst(void)
{
	memset(&cpu, 0, sizeof cpu);
	memset(&m, 0, sizeof m);
	m.base = mem;
	cpu.mem = &m;
	memset(mem, 0, MEMSZ);
}

static uint32_t enc_word(const char *name)
{
	unsigned i;

	for (i = 0; i < ENC_COUNT; i++)
		if (strcmp(enc_tab[i].name, name) == 0)
			return enc_tab[i].word;
	printf("FATAL: encoding %s not in table\n", name);
	exit(1);
}

static int exec(const char *name)
{
	int r = n2a32_neon_ldst(&cpu, enc_word(name));

	checks++;
	if (r != N2A32_OK) {
		printf("FAIL exec %s -> %d\n", name, r);
		fails++;
		return 0;
	}
	return 1;
}

static void ck64(const char *name, unsigned reg, uint64_t want)
{
	checks++;
	if (cpu.d[reg].u64 != want) {
		fails++;
		printf("FAIL %s d%u = %016llx want %016llx\n", name, reg,
		       (unsigned long long)cpu.d[reg].u64, (unsigned long long)want);
	}
}

static void ck32(const char *name, const char *what, uint32_t got, uint32_t want)
{
	checks++;
	if (got != want) {
		fails++;
		printf("FAIL %s %s = %08x want %08x\n", name, what, got, want);
	}
}

/* fill n bytes at a with 0, 1, 2, ... (mod 256) */
static void fill_seq(uint32_t a, unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++)
		mem[a + i] = (uint8_t)i;
}

static void ck_bytes(const char *name, uint32_t a, unsigned n, const uint8_t *want)
{
	unsigned i;

	for (i = 0; i < n; i++) {
		checks++;
		if (mem[a + i] != want[i]) {
			fails++;
			printf("FAIL %s byte %u at %08x = %02x want %02x\n", name, i, a,
			       mem[a + i], want[i]);
			return;
		}
	}
}

static void ck_undef(const char *name)
{
	struct n2a32_cpu before = cpu;
	int r = n2a32_neon_ldst(&cpu, enc_word(name));

	checks++;
	if (r != N2A32_UNDEF) {
		fails++;
		printf("FAIL %s: expected UNDEF, got %d\n", name, r);
	} else if (memcmp(&before, &cpu, sizeof cpu) != 0) {
		fails++;
		printf("FAIL %s: UNDEF changed the cpu state\n", name);
	}
}

int main(void)
{
	static const uint8_t seq16[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	static const uint8_t seq32[32] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	};

	mem = calloc(1, MEMSZ);
	if (!mem) {
		printf("FAIL calloc\n");
		return 1;
	}

	/* ---------------- VLD1 / VST1, multiple structures ---------------- */
	/* 8 bytes 00..07 -> d0 = 0x0706050403020100 */
	rst();
	fill_seq(BASE, 8);
	cpu.r[1] = BASE;
	if (exec("vld1_8_1")) {
		ck64("vld1_8_1", 0, 0x0706050403020100ull);
		ck32("vld1_8_1", "r1", cpu.r[1], BASE);
	}
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld1_8_2")) {
		ck64("vld1_8_2", 0, 0x0706050403020100ull);
		ck64("vld1_8_2", 1, 0x0F0E0D0C0B0A0908ull);
	}
	rst();
	fill_seq(BASE, 24);
	cpu.r[1] = BASE;
	if (exec("vld1_8_3")) {
		ck64("vld1_8_3", 0, 0x0706050403020100ull);
		ck64("vld1_8_3", 1, 0x0F0E0D0C0B0A0908ull);
		ck64("vld1_8_3", 2, 0x1716151413121110ull);
	}
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld1_8_4")) {
		ck64("vld1_8_4", 0, 0x0706050403020100ull);
		ck64("vld1_8_4", 1, 0x0F0E0D0C0B0A0908ull);
		ck64("vld1_8_4", 2, 0x1716151413121110ull);
		ck64("vld1_8_4", 3, 0x1F1E1D1C1B1A1918ull);
	}
	/* 16-bit: 8 bytes -> one register of four halfwords */
	rst();
	fill_seq(BASE, 8);
	cpu.r[1] = BASE;
	if (exec("vld1_16_1"))
		ck64("vld1_16_1", 0, 0x0706050403020100ull);
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld1_16_2")) {
		ck64("vld1_16_2", 0, 0x0706050403020100ull);
		ck64("vld1_16_2", 1, 0x0F0E0D0C0B0A0908ull);
	}
	/* 32-bit: two words */
	rst();
	fill_seq(BASE, 8);
	cpu.r[1] = BASE;
	if (exec("vld1_32_1"))
		ck64("vld1_32_1", 0, 0x0706050403020100ull);
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld1_32_4")) {
		ck64("vld1_32_4", 0, 0x0706050403020100ull);
		ck64("vld1_32_4", 3, 0x1F1E1D1C1B1A1918ull);
	}
	/* 64-bit: one doubleword per register */
	rst();
	fill_seq(BASE, 8);
	cpu.r[1] = BASE;
	if (exec("vld1_64_1"))
		ck64("vld1_64_1", 0, 0x0706050403020100ull);
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld1_64_2")) {
		ck64("vld1_64_2", 0, 0x0706050403020100ull);
		ck64("vld1_64_2", 1, 0x0F0E0D0C0B0A0908ull);
	}
	/* stores */
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	if (exec("vst1_8_1"))
		ck_bytes("vst1_8_1", BASE, 8, seq16);
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[2].u64 = 0x1716151413121110ull;
	cpu.d[3].u64 = 0x1F1E1D1C1B1A1918ull;
	if (exec("vst1_8_4"))
		ck_bytes("vst1_8_4", BASE, 32, seq32);
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x0F0E0D0C0B0A0908ull;
	if (exec("vst1_16_2"))
		ck_bytes("vst1_16_2", BASE, 16, seq16);
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x0F0E0D0C0B0A0908ull;
	if (exec("vst1_32_2"))
		ck_bytes("vst1_32_2", BASE, 16, seq16);
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	if (exec("vst1_64_1"))
		ck_bytes("vst1_64_1", BASE, 8, seq16);
	/* writeback */
	rst();
	fill_seq(BASE, 8);
	cpu.r[1] = BASE;
	if (exec("vld1_8_1_wb")) {
		ck64("vld1_8_1_wb", 0, 0x0706050403020100ull);
		ck32("vld1_8_1_wb", "r1", cpu.r[1], BASE + 8);
	}
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld1_8_2_wb"))
		ck32("vld1_8_2_wb", "r1", cpu.r[1], BASE + 16);
	rst();
	fill_seq(BASE, 8);
	cpu.r[1] = BASE;
	cpu.r[2] = 12;
	if (exec("vld1_32_1_reg")) {
		ck64("vld1_32_1_reg", 0, 0x0706050403020100ull);
		ck32("vld1_32_1_reg", "r1", cpu.r[1], BASE + 12);
	}
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	if (exec("vst1_8_1_wb"))
		ck32("vst1_8_1_wb", "r1", cpu.r[1], BASE + 8);
	rst();
	cpu.r[1] = BASE;
	cpu.r[2] = 5;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x0F0E0D0C0B0A0908ull;
	if (exec("vst1_8_2_reg")) {
		ck_bytes("vst1_8_2_reg", BASE, 16, seq16);
		ck32("vst1_8_2_reg", "r1", cpu.r[1], BASE + 5);
	}
	/* alignment fields change nothing about the bytes transferred */
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld1_8_2_al64")) {
		ck64("vld1_8_2_al64", 0, 0x0706050403020100ull);
		ck64("vld1_8_2_al64", 1, 0x0F0E0D0C0B0A0908ull);
	}
	rst();
	fill_seq(BASE, 8);
	cpu.r[1] = BASE;
	if (exec("vld1_32_1_al64"))
		ck64("vld1_32_1_al64", 0, 0x0706050403020100ull);
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld1_64_2_al128")) {
		ck64("vld1_64_2_al128", 0, 0x0706050403020100ull);
		ck64("vld1_64_2_al128", 1, 0x0F0E0D0C0B0A0908ull);
	}
	/* high registers */
	rst();
	fill_seq(BASE, 8);
	cpu.r[1] = BASE;
	if (exec("vld1_8_hi"))
		ck64("vld1_8_hi", 16, 0x0706050403020100ull);
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld1_8_hi2")) {
		ck64("vld1_8_hi2", 30, 0x0706050403020100ull);
		ck64("vld1_8_hi2", 31, 0x0F0E0D0C0B0A0908ull);
	}

	/* ---------------- VLD2/3/4 multiple ---------------- */
	/* VLD2.8 of 00..0F: d0 = even bytes, d1 = odd bytes */
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld2_8_2")) {
		ck64("vld2_8_2", 0, 0x0E0C0A0806040200ull);
		ck64("vld2_8_2", 1, 0x0F0D0B0907050301ull);
	}
	/* VLD2.16: halfwords 0,2,4,6 and 1,3,5,7 */
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld2_16_2")) {
		/* halfwords 0100 0302 0504 0706 0908 0B0A 0D0C 0F0E:
		 * d0 = 0100 0504 0908 0D0C, d1 = 0302 0706 0B0A 0F0E */
		ck64("vld2_16_2", 0, 0x0D0C090805040100ull);
		ck64("vld2_16_2", 1, 0x0F0E0B0A07060302ull);
	}
	/* VLD2.32: words 0,2 and 1,3 */
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld2_32_2")) {
		ck64("vld2_32_2", 0, 0x0B0A090803020100ull);
		ck64("vld2_32_2", 1, 0x0F0E0D0C07060504ull);
	}
	/* VLD2.8 four registers: two blocks of 16 bytes */
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld2_8_4")) {
		ck64("vld2_8_4", 0, 0x0E0C0A0806040200ull);
		ck64("vld2_8_4", 1, 0x0F0D0B0907050301ull);
		ck64("vld2_8_4", 2, 0x1E1C1A1816141210ull);
		ck64("vld2_8_4", 3, 0x1F1D1B1917151311ull);
	}
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld2_16_4")) {
		ck64("vld2_16_4", 0, 0x0D0C090805040100ull);
		ck64("vld2_16_4", 1, 0x0F0E0B0A07060302ull);
		ck64("vld2_16_4", 2, 0x1D1C191815141110ull);
		ck64("vld2_16_4", 3, 0x1F1E1B1A17161312ull);
	}
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld2_32_4")) {
		ck64("vld2_32_4", 0, 0x0B0A090803020100ull);
		ck64("vld2_32_4", 2, 0x1B1A191813121110ull);
	}
	/* VST2.8 */
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x1716151413121110ull;
	if (exec("vst2_8_2")) {
		static const uint8_t want[16] = { 0, 0x10, 1, 0x11, 2, 0x12, 3, 0x13,
						  4, 0x14, 5, 0x15, 6, 0x16, 7, 0x17 };
		ck_bytes("vst2_8_2", BASE, 16, want);
	}
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x1716151413121110ull;
	if (exec("vst2_32_2")) {
		static const uint8_t want[16] = { 0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13,
						  4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17 };
		ck_bytes("vst2_32_2", BASE, 16, want);
	}
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x1716151413121110ull;
	cpu.d[2].u64 = 0x0706050403020100ull;
	cpu.d[3].u64 = 0x1716151413121110ull;
	if (exec("vst2_8_4")) {
		static const uint8_t want[32] = {
			0, 0x10, 1, 0x11, 2, 0x12, 3, 0x13, 4, 0x14, 5, 0x15, 6, 0x16, 7, 0x17,
			0, 0x10, 1, 0x11, 2, 0x12, 3, 0x13, 4, 0x14, 5, 0x15, 6, 0x16, 7, 0x17
		};
		ck_bytes("vst2_8_4", BASE, 32, want);
	}
	/* VLD3.8 of 00..17 */
	rst();
	fill_seq(BASE, 24);
	cpu.r[1] = BASE;
	if (exec("vld3_8_3")) {
		ck64("vld3_8_3", 0, 0x15120F0C09060300ull);
		ck64("vld3_8_3", 1, 0x1613100D0A070401ull);
		ck64("vld3_8_3", 2, 0x1714110E0B080502ull);
	}
	rst();
	fill_seq(BASE, 24);
	cpu.r[1] = BASE;
	if (exec("vld3_16_3")) {
		/* halfwords 0100 0302 0504 0706 0908 0B0A 0D0C 0F0E 1110 1312 1514 1716:
		 * d0 = 0,3,6,9 ; d1 = 1,4,7,10 ; d2 = 2,5,8,11 */
		ck64("vld3_16_3", 0, 0x13120D0C07060100ull);
		ck64("vld3_16_3", 1, 0x15140F0E09080302ull);
		ck64("vld3_16_3", 2, 0x171611100B0A0504ull);
	}
	rst();
	fill_seq(BASE, 24);
	cpu.r[1] = BASE;
	if (exec("vld3_32_3")) {
		ck64("vld3_32_3", 0, 0x0F0E0D0C03020100ull);
		ck64("vld3_32_3", 1, 0x1312111007060504ull);
		ck64("vld3_32_3", 2, 0x171615140B0A0908ull);
	}
	/* VST3.8 */
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[2].u64 = 0x1716151413121110ull;
	if (exec("vst3_8_3")) {
		static const uint8_t want[24] = { 0, 8, 0x10, 1, 9, 0x11, 2, 0x0A, 0x12,
						  3, 0x0B, 0x13, 4, 0x0C, 0x14, 5, 0x0D, 0x15,
						  6, 0x0E, 0x16, 7, 0x0F, 0x17 };
		ck_bytes("vst3_8_3", BASE, 24, want);
	}
	/* VLD4.8 of 00..1F */
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld4_8_4")) {
		ck64("vld4_8_4", 0, 0x1C1814100C080400ull);
		ck64("vld4_8_4", 1, 0x1D1915110D090501ull);
		ck64("vld4_8_4", 2, 0x1E1A16120E0A0602ull);
		ck64("vld4_8_4", 3, 0x1F1B17130F0B0703ull);
	}
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld4_16_4")) {
		/* halfwords 0100..1F1E: d0 = 0,4,8,12 ; d1 = 1,5,9,13 ; d2 = 2,6,10,14 ; d3 = 3,7,11,15 */
		ck64("vld4_16_4", 0, 0x1918111009080100ull);
		ck64("vld4_16_4", 1, 0x1B1A13120B0A0302ull);
		ck64("vld4_16_4", 2, 0x1D1C15140D0C0504ull);
		ck64("vld4_16_4", 3, 0x1F1E17160F0E0706ull);
	}
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld4_32_4")) {
		ck64("vld4_32_4", 0, 0x1312111003020100ull);
		ck64("vld4_32_4", 3, 0x1F1E1D1C0F0E0D0Cull);
	}
	/* VST4.8 */
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x1716151413121110ull;
	cpu.d[2].u64 = 0x2726252423222120ull;
	cpu.d[3].u64 = 0x3736353433323130ull;
	if (exec("vst4_8_4")) {
		static const uint8_t want[32] = {
			0, 0x10, 0x20, 0x30, 1, 0x11, 0x21, 0x31, 2, 0x12, 0x22, 0x32, 3, 0x13, 0x23, 0x33,
			4, 0x14, 0x24, 0x34, 5, 0x15, 0x25, 0x35, 6, 0x16, 0x26, 0x36, 7, 0x17, 0x27, 0x37
		};
		ck_bytes("vst4_8_4", BASE, 32, want);
	}
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x1716151413121110ull;
	cpu.d[2].u64 = 0x2726252423222120ull;
	cpu.d[3].u64 = 0x3736353433323130ull;
	if (exec("vst4_16_4")) {
		static const uint8_t want[32] = {
			0, 1, 0x10, 0x11, 0x20, 0x21, 0x30, 0x31, 2, 3, 0x12, 0x13, 0x22, 0x23, 0x32, 0x33,
			4, 5, 0x14, 0x15, 0x24, 0x25, 0x34, 0x35, 6, 7, 0x16, 0x17, 0x26, 0x27, 0x36, 0x37
		};
		ck_bytes("vst4_16_4", BASE, 32, want);
	}
	/* writeback */
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld2_8_2_wb"))
		ck32("vld2_8_2_wb", "r1", cpu.r[1], BASE + 16);
	rst();
	fill_seq(BASE, 24);
	cpu.r[1] = BASE;
	cpu.r[2] = 7;
	if (exec("vld3_8_3_reg"))
		ck32("vld3_8_3_reg", "r1", cpu.r[1], BASE + 7);
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	cpu.r[2] = 9;
	if (exec("vld4_32_4_reg"))
		ck32("vld4_32_4_reg", "r1", cpu.r[1], BASE + 9);
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld2_8_2_al64"))
		ck64("vld2_8_2_al64", 0, 0x0E0C0A0806040200ull);
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld4_8_4_al128"))
		ck64("vld4_8_4_al128", 0, 0x1C1814100C080400ull);
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld4_8_hi")) {
		ck64("vld4_8_hi", 28, 0x1C1814100C080400ull);
		ck64("vld4_8_hi", 31, 0x1F1B17130F0B0703ull);
	}

	/* ---------------- single structure to one lane ---------------- */
	/* vld1.8 {d0[3]}, [r1] with memory byte 0x5A -> d0 byte 3 = 0x5A */
	rst();
	mem[BASE] = 0x5A;
	cpu.r[1] = BASE;
	if (exec("vld1_8_l3"))
		ck64("vld1_8_l3", 0, 0x5A000000ull);
	rst();
	mem[BASE] = 0xA5;
	cpu.r[1] = BASE;
	if (exec("vld1_8_l0"))
		ck64("vld1_8_l0", 0, 0xA5ull);
	rst();
	mem[BASE] = 0x11;
	cpu.r[1] = BASE;
	if (exec("vld1_8_l7"))
		ck64("vld1_8_l7", 0, 0x1100000000000000ull);
	/* vld1.16 {d0[1]} with halfword 0x1234 -> d0 = 0x12340000 */
	rst();
	mem[BASE] = 0x34;
	mem[BASE + 1] = 0x12;
	cpu.r[1] = BASE;
	if (exec("vld1_16_l1"))
		ck64("vld1_16_l1", 0, 0x12340000ull);
	rst();
	mem[BASE] = 0x78;
	mem[BASE + 1] = 0x56;
	cpu.r[1] = BASE;
	if (exec("vld1_16_l3"))
		ck64("vld1_16_l3", 0, 0x5678000000000000ull);
	/* vld1.32 {d0[1]} with word 0x12345678 -> d0 = 0x1234567800000000 */
	rst();
	mem[BASE] = 0x78;
	mem[BASE + 1] = 0x56;
	mem[BASE + 2] = 0x34;
	mem[BASE + 3] = 0x12;
	cpu.r[1] = BASE;
	if (exec("vld1_32_l1"))
		ck64("vld1_32_l1", 0, 0x1234567800000000ull);
	rst();
	mem[BASE] = 0xEF;
	mem[BASE + 1] = 0xBE;
	mem[BASE + 2] = 0xAD;
	mem[BASE + 3] = 0xDE;
	cpu.r[1] = BASE;
	if (exec("vld1_32_l0"))
		ck64("vld1_32_l0", 0, 0xDEADBEEFull);
	/* VST1 lane */
	rst();
	cpu.d[0].u64 = 0x1122334455667788ull;
	cpu.r[1] = BASE;
	if (exec("vst1_8_l0"))
		ck32("vst1_8_l0", "byte", mem[BASE], 0x88);
	rst();
	cpu.d[0].u64 = 0x1122334455667788ull;
	cpu.r[1] = BASE;
	if (exec("vst1_32_l1")) {
		checks++;
		if (mem[BASE] != 0x44 || mem[BASE + 1] != 0x33 || mem[BASE + 2] != 0x22 || mem[BASE + 3] != 0x11) {
			fails++;
			printf("FAIL vst1_32_l1 bytes\n");
		}
	}
	/* VLD2 lane */
	rst();
	mem[BASE] = 0x11;
	mem[BASE + 1] = 0x22;
	cpu.r[1] = BASE;
	if (exec("vld2_8_l0")) {
		ck64("vld2_8_l0", 0, 0x11ull);
		ck64("vld2_8_l0", 1, 0x22ull);
	}
	rst();
	mem[BASE] = 0x11;
	mem[BASE + 1] = 0x22;
	cpu.r[1] = BASE;
	if (exec("vld2_8_l3")) {
		ck64("vld2_8_l3", 0, 0x11000000ull);
		ck64("vld2_8_l3", 1, 0x22000000ull);
	}
	/* VLD2.16 lane 1: halfwords 0x1111, 0x2222 */
	rst();
	mem[BASE] = 0x11; mem[BASE + 1] = 0x11;
	mem[BASE + 2] = 0x22; mem[BASE + 3] = 0x22;
	cpu.r[1] = BASE;
	if (exec("vld2_16_l1")) {
		ck64("vld2_16_l1", 0, 0x11110000ull);
		ck64("vld2_16_l1", 1, 0x22220000ull);
	}
	/* VLD2.32 lane 1: words 0x11223344, 0x55667788 */
	rst();
	mem[BASE] = 0x44; mem[BASE + 1] = 0x33; mem[BASE + 2] = 0x22; mem[BASE + 3] = 0x11;
	mem[BASE + 4] = 0x88; mem[BASE + 5] = 0x77; mem[BASE + 6] = 0x66; mem[BASE + 7] = 0x55;
	cpu.r[1] = BASE;
	if (exec("vld2_32_l1")) {
		ck64("vld2_32_l1", 0, 0x1122334400000000ull);
		ck64("vld2_32_l1", 1, 0x5566778800000000ull);
	}
	/* VLD3 lane 2 / VLD4 lane 0 / VLD4.16 lane 1 */
	rst();
	mem[BASE] = 0x01; mem[BASE + 1] = 0x02; mem[BASE + 2] = 0x03;
	cpu.r[1] = BASE;
	if (exec("vld3_8_l2")) {
		ck64("vld3_8_l2", 0, 0x0000000000010000ull);
		ck64("vld3_8_l2", 1, 0x0000000000020000ull);
		ck64("vld3_8_l2", 2, 0x0000000000030000ull);
	}
	rst();
	mem[BASE] = 0x01; mem[BASE + 1] = 0x02;
	mem[BASE + 2] = 0x03; mem[BASE + 3] = 0x04;
	mem[BASE + 4] = 0x05; mem[BASE + 5] = 0x06;
	cpu.r[1] = BASE;
	if (exec("vld3_16_l0")) {
		ck64("vld3_16_l0", 0, 0x0201ull);
		ck64("vld3_16_l0", 1, 0x0403ull);
		ck64("vld3_16_l0", 2, 0x0605ull);
	}
	rst();
	mem[BASE] = 0x01; mem[BASE + 1] = 0x02; mem[BASE + 2] = 0x03; mem[BASE + 3] = 0x04;
	cpu.r[1] = BASE;
	if (exec("vld4_8_l0")) {
		ck64("vld4_8_l0", 0, 0x01ull);
		ck64("vld4_8_l0", 1, 0x02ull);
		ck64("vld4_8_l0", 2, 0x03ull);
		ck64("vld4_8_l0", 3, 0x04ull);
	}
	rst();
	mem[BASE] = 0x11; mem[BASE + 1] = 0x11;
	mem[BASE + 2] = 0x22; mem[BASE + 3] = 0x22;
	mem[BASE + 4] = 0x33; mem[BASE + 5] = 0x33;
	mem[BASE + 6] = 0x44; mem[BASE + 7] = 0x44;
	cpu.r[1] = BASE;
	if (exec("vld4_16_l1")) {
		ck64("vld4_16_l1", 0, 0x11110000ull);
		ck64("vld4_16_l1", 1, 0x22220000ull);
		ck64("vld4_16_l1", 2, 0x33330000ull);
		ck64("vld4_16_l1", 3, 0x44440000ull);
	}
	/* VST2 / VST4 lane */
	rst();
	cpu.d[0].u64 = 0x1122334455667788ull;
	cpu.d[1].u64 = 0x99AABBCCDDEEFF00ull;
	cpu.r[1] = BASE;
	if (exec("vst2_8_l1")) {
		checks++;
		if (mem[BASE] != 0x77 || mem[BASE + 1] != 0xFF) {
			fails++;
			printf("FAIL vst2_8_l1 bytes %02x %02x\n", mem[BASE], mem[BASE + 1]);
		}
	}
	rst();
	cpu.d[0].u64 = 0x0000000011223344ull;
	cpu.d[1].u64 = 0x0000000055667788ull;
	cpu.d[2].u64 = 0x0000000099AABBCCull;
	cpu.d[3].u64 = 0x00000000DDEEFF00ull;
	cpu.r[1] = BASE;
	if (exec("vst4_32_l0")) {
		checks++;
		if (mem[BASE] != 0x44 || mem[BASE + 4] != 0x88 || mem[BASE + 8] != 0xCC || mem[BASE + 12] != 0x00) {
			fails++;
			printf("FAIL vst4_32_l0 bytes\n");
		}
	}
	/* lane writeback: VLD2.8 lane, 2 bytes transferred */
	rst();
	mem[BASE] = 0x11; mem[BASE + 1] = 0x22;
	cpu.r[1] = BASE;
	if (exec("vld2_8_l1_wb"))
		ck32("vld2_8_l1_wb", "r1", cpu.r[1], BASE + 2);
	rst();
	mem[BASE] = 0x5A;
	cpu.r[1] = BASE;
	cpu.r[2] = 3;
	if (exec("vld1_8_l3_reg")) {
		ck64("vld1_8_l3_reg", 0, 0x5A000000ull);
		ck32("vld1_8_l3_reg", "r1", cpu.r[1], BASE + 3);
	}
	/* high register list: vld4.8 {d28-d31} lane 0 */
	rst();
	mem[BASE] = 1; mem[BASE + 1] = 2; mem[BASE + 2] = 3; mem[BASE + 3] = 4;
	cpu.r[1] = BASE;
	if (exec("vld4_8_l0_hi")) {
		ck64("vld4_8_l0_hi", 28, 1);
		ck64("vld4_8_l0_hi", 29, 2);
		ck64("vld4_8_l0_hi", 30, 3);
		ck64("vld4_8_l0_hi", 31, 4);
	}
	/* lane load preserves the other lanes of Dd and the other half of Qd (T01/V18 k_u8_sad
	 * question): vld1.32 {d18[0]} replaces word 0 of d18 only; d18's other words and d19
	 * (the high half of q9) keep their previous contents. */
	rst();
	mem[BASE] = 0xDD; mem[BASE + 1] = 0xCC; mem[BASE + 2] = 0xBB; mem[BASE + 3] = 0xAA;
	cpu.d[18].u64 = 0x1122334455667788ull;
	cpu.d[19].u64 = 0xDEADBEEFCAFEBABEull;
	cpu.r[1] = BASE;
	if (exec("vld1_32_d18_l0")) {
		ck64("vld1_32_d18_l0", 18, 0x11223344AABBCCDDull);
		ck64("vld1_32_d18_l0", 19, 0xDEADBEEFCAFEBABEull);
	}
	rst();
	mem[BASE] = 0xFF;
	cpu.d[18].u64 = 0x0706050403020100ull;
	cpu.d[19].u64 = 0xDEADBEEFCAFEBABEull;
	cpu.r[1] = BASE;
	if (exec("vld1_8_d18_l3")) {
		ck64("vld1_8_d18_l3", 18, 0x07060504FF020100ull);
		ck64("vld1_8_d18_l3", 19, 0xDEADBEEFCAFEBABEull);
	}
	/* ARMv8 register-stride-2 lane forms.  VLD2.16 {d0[1],d2[1]}: one halfword into lane 1 of
	 * d0 and of d2; d1/d3 (the registers in between) must not change. */
	rst();
	mem[BASE] = 0x34; mem[BASE + 1] = 0x12;
	mem[BASE + 2] = 0x78; mem[BASE + 3] = 0x56;
	cpu.d[0].u64 = 0xAAAAAAAAAAAAAAAAull;
	cpu.d[1].u64 = 0xBBBBBBBBBBBBBBBBull;
	cpu.d[2].u64 = 0xCCCCCCCCCCCCCCCCull;
	cpu.d[3].u64 = 0xDDDDDDDDDDDDDDDDull;
	cpu.r[1] = BASE;
	if (exec("vld2_16_l1_s2")) {
		ck64("vld2_16_l1_s2", 0, 0xAAAAAAAA1234AAAAull);
		ck64("vld2_16_l1_s2", 1, 0xBBBBBBBBBBBBBBBBull);
		ck64("vld2_16_l1_s2", 2, 0xCCCCCCCC5678CCCCull);
		ck64("vld2_16_l1_s2", 3, 0xDDDDDDDDDDDDDDDDull);
	}
	/* VLD3.32 {d0[0],d2[0],d4[0]} */
	rst();
	mem[BASE] = 0x44; mem[BASE + 1] = 0x33; mem[BASE + 2] = 0x22; mem[BASE + 3] = 0x11;
	mem[BASE + 4] = 0x88; mem[BASE + 5] = 0x77; mem[BASE + 6] = 0x66; mem[BASE + 7] = 0x55;
	mem[BASE + 8] = 0xCC; mem[BASE + 9] = 0xBB; mem[BASE + 10] = 0xAA; mem[BASE + 11] = 0x99;
	cpu.d[0].u64 = 0xAAAAAAAAAAAAAAAAull;
	cpu.d[1].u64 = 0xBBBBBBBBBBBBBBBBull;
	cpu.d[2].u64 = 0xCCCCCCCCCCCCCCCCull;
	cpu.d[3].u64 = 0xDDDDDDDDDDDDDDDDull;
	cpu.d[4].u64 = 0xEEEEEEEEEEEEEEEEull;
	cpu.d[5].u64 = 0xFFFFFFFFFFFFFFFFull;
	cpu.r[1] = BASE;
	if (exec("vld3_32_l0_s2")) {
		ck64("vld3_32_l0_s2", 0, 0xAAAAAAAA11223344ull);
		ck64("vld3_32_l0_s2", 1, 0xBBBBBBBBBBBBBBBBull);
		ck64("vld3_32_l0_s2", 2, 0xCCCCCCCC55667788ull);
		ck64("vld3_32_l0_s2", 3, 0xDDDDDDDDDDDDDDDDull);
		ck64("vld3_32_l0_s2", 4, 0xEEEEEEEE99AABBCCull);
		ck64("vld3_32_l0_s2", 5, 0xFFFFFFFFFFFFFFFFull);
	}
	/* VLD4.16 {d16[0],d18[0],d20[0],d22[0]} */
	rst();
	mem[BASE] = 0x11; mem[BASE + 1] = 0x11;
	mem[BASE + 2] = 0x22; mem[BASE + 3] = 0x22;
	mem[BASE + 4] = 0x33; mem[BASE + 5] = 0x33;
	mem[BASE + 6] = 0x44; mem[BASE + 7] = 0x44;
	cpu.d[16].u64 = 0xAAAAAAAAAAAAAAAAull;
	cpu.d[17].u64 = 0xBBBBBBBBBBBBBBBBull;
	cpu.d[18].u64 = 0xCCCCCCCCCCCCCCCCull;
	cpu.d[19].u64 = 0xDDDDDDDDDDDDDDDDull;
	cpu.d[20].u64 = 0xEEEEEEEEEEEEEEEEull;
	cpu.d[21].u64 = 0xFFFFFFFFFFFFFFFFull;
	cpu.d[22].u64 = 0x1111111111111111ull;
	cpu.d[23].u64 = 0x2222222222222222ull;
	cpu.r[1] = BASE;
	if (exec("vld4_16_l0_s2")) {
		ck64("vld4_16_l0_s2", 16, 0xAAAAAAAAAAAA1111ull);
		ck64("vld4_16_l0_s2", 17, 0xBBBBBBBBBBBBBBBBull);
		ck64("vld4_16_l0_s2", 18, 0xCCCCCCCCCCCC2222ull);
		ck64("vld4_16_l0_s2", 19, 0xDDDDDDDDDDDDDDDDull);
		ck64("vld4_16_l0_s2", 20, 0xEEEEEEEEEEEE3333ull);
		ck64("vld4_16_l0_s2", 21, 0xFFFFFFFFFFFFFFFFull);
		ck64("vld4_16_l0_s2", 22, 0x1111111111114444ull);
		ck64("vld4_16_l0_s2", 23, 0x2222222222222222ull);
	}
	/* VLD4.32 {d1[1],d3[1],d5[1],d7[1]}: lane 1 (the high word) of each odd register */
	rst();
	mem[BASE] = 0x01; mem[BASE + 1] = 0x00; mem[BASE + 2] = 0x00; mem[BASE + 3] = 0x80;
	mem[BASE + 4] = 0x02; mem[BASE + 5] = 0x00; mem[BASE + 6] = 0x00; mem[BASE + 7] = 0x80;
	mem[BASE + 8] = 0x03; mem[BASE + 9] = 0x00; mem[BASE + 10] = 0x00; mem[BASE + 11] = 0x80;
	mem[BASE + 12] = 0x04; mem[BASE + 13] = 0x00; mem[BASE + 14] = 0x00; mem[BASE + 15] = 0x80;
	cpu.d[0].u64 = 0x1111111111111111ull;
	cpu.d[1].u64 = 0xAAAAAAAAAAAAAAAAull;
	cpu.d[2].u64 = 0x2222222222222222ull;
	cpu.d[3].u64 = 0xBBBBBBBBBBBBBBBBull;
	cpu.d[4].u64 = 0x3333333333333333ull;
	cpu.d[5].u64 = 0xCCCCCCCCCCCCCCCCull;
	cpu.d[6].u64 = 0x4444444444444444ull;
	cpu.d[7].u64 = 0xDDDDDDDDDDDDDDDDull;
	cpu.r[1] = BASE;
	if (exec("vld4_32_l1_s2")) {
		ck64("vld4_32_l1_s2", 0, 0x1111111111111111ull);
		ck64("vld4_32_l1_s2", 1, 0x80000001AAAAAAAAull);
		ck64("vld4_32_l1_s2", 3, 0x80000002BBBBBBBBull);
		ck64("vld4_32_l1_s2", 5, 0x80000003CCCCCCCCull);
		ck64("vld4_32_l1_s2", 7, 0x80000004DDDDDDDDull);
	}
	/* VST2.32 {d0[1],d2[1]} writes two words at [r1] and [r1+4] */
	rst();
	cpu.d[0].u64 = 0x1122334455667788ull;
	cpu.d[2].u64 = 0x99AABBCCDDEEFF00ull;
	cpu.r[1] = BASE;
	if (exec("vst2_32_l1_s2")) {
		checks++;
		if (mem[BASE] != 0x44 || mem[BASE + 1] != 0x33 || mem[BASE + 2] != 0x22 || mem[BASE + 3] != 0x11 ||
		    mem[BASE + 4] != 0xCC || mem[BASE + 5] != 0xBB || mem[BASE + 6] != 0xAA || mem[BASE + 7] != 0x99) {
			fails++;
			printf("FAIL vst2_32_l1_s2 bytes\n");
		}
	}
	/* Unallocated size / alignment / lane fields are UNDEFINED and must not touch any state:
	 * VLD1.8 lane with alignment bit 4 set (only 0 is allocated) and VLD2.8 multiple with
	 * size = 11 (no .64 multi-structure form). */
	{
		struct n2a32_cpu before = cpu;
		int r1 = n2a32_neon_ldst(&cpu, enc_word("vld1_8_l0") | 0x10u);
		int r2;

		checks++;
		if (r1 != N2A32_UNDEF || memcmp(&cpu, &before, sizeof cpu) != 0) {
			fails++;
			printf("FAIL invalid VLD1.8 lane nibble accepted\n");
		}
		rst();
		before = cpu;
		r2 = n2a32_neon_ldst(&cpu, enc_word("vld2_8_2") | 0xC0u);
		checks++;
		if (r2 != N2A32_UNDEF || memcmp(&cpu, &before, sizeof cpu) != 0) {
			fails++;
			printf("FAIL invalid VLD2.64 accepted\n");
		}
	}

	/* ---------------- to all lanes ---------------- */
	rst();
	mem[BASE] = 0x5A;
	cpu.r[1] = BASE;
	if (exec("vld1_8_all"))
		ck64("vld1_8_all", 0, 0x5A5A5A5A5A5A5A5Aull);
	rst();
	mem[BASE] = 0x34; mem[BASE + 1] = 0x12;
	cpu.r[1] = BASE;
	if (exec("vld1_16_all"))
		ck64("vld1_16_all", 0, 0x1234123412341234ull);
	rst();
	mem[BASE] = 0x78; mem[BASE + 1] = 0x56; mem[BASE + 2] = 0x34; mem[BASE + 3] = 0x12;
	cpu.r[1] = BASE;
	if (exec("vld1_32_all"))
		ck64("vld1_32_all", 0, 0x1234567812345678ull);
	rst();
	mem[BASE] = 0x11; mem[BASE + 1] = 0x22;
	cpu.r[1] = BASE;
	if (exec("vld2_8_all")) {
		ck64("vld2_8_all", 0, 0x1111111111111111ull);
		ck64("vld2_8_all", 1, 0x2222222222222222ull);
	}
	rst();
	mem[BASE] = 0x11; mem[BASE + 1] = 0x11;
	mem[BASE + 2] = 0x22; mem[BASE + 3] = 0x22;
	cpu.r[1] = BASE;
	if (exec("vld2_16_all")) {
		ck64("vld2_16_all", 0, 0x1111111111111111ull);
		ck64("vld2_16_all", 1, 0x2222222222222222ull);
	}
	rst();
	for (int i = 0; i < 4; i++) mem[BASE + 4 * i] = 0x10 + i;
	cpu.r[1] = BASE;
	if (exec("vld2_32_all")) {
		ck64("vld2_32_all", 0, 0x0000001000000010ull);
		ck64("vld2_32_all", 1, 0x0000001100000011ull);
	}
	rst();
	mem[BASE] = 1; mem[BASE + 1] = 2; mem[BASE + 2] = 3;
	cpu.r[1] = BASE;
	if (exec("vld3_8_all")) {
		ck64("vld3_8_all", 0, 0x0101010101010101ull);
		ck64("vld3_8_all", 1, 0x0202020202020202ull);
		ck64("vld3_8_all", 2, 0x0303030303030303ull);
	}
	rst();
	mem[BASE] = 0x01; mem[BASE + 1] = 0; mem[BASE + 2] = 0; mem[BASE + 3] = 0;
	mem[BASE + 4] = 0x02; mem[BASE + 5] = 0; mem[BASE + 6] = 0; mem[BASE + 7] = 0;
	mem[BASE + 8] = 0x03; mem[BASE + 9] = 0; mem[BASE + 10] = 0; mem[BASE + 11] = 0;
	cpu.r[1] = BASE;
	if (exec("vld3_32_all")) {
		ck64("vld3_32_all", 0, 0x0000000100000001ull);
		ck64("vld3_32_all", 1, 0x0000000200000002ull);
		ck64("vld3_32_all", 2, 0x0000000300000003ull);
	}
	rst();
	mem[BASE] = 1; mem[BASE + 1] = 2; mem[BASE + 2] = 3; mem[BASE + 3] = 4;
	cpu.r[1] = BASE;
	if (exec("vld4_8_all")) {
		ck64("vld4_8_all", 0, 0x0101010101010101ull);
		ck64("vld4_8_all", 1, 0x0202020202020202ull);
		ck64("vld4_8_all", 2, 0x0303030303030303ull);
		ck64("vld4_8_all", 3, 0x0404040404040404ull);
	}
	rst();
	mem[BASE] = 0x34; mem[BASE + 1] = 0x12;
	mem[BASE + 2] = 0x78; mem[BASE + 3] = 0x56;
	mem[BASE + 4] = 0xBC; mem[BASE + 5] = 0x9A;
	mem[BASE + 6] = 0xF0; mem[BASE + 7] = 0xDE;
	cpu.r[1] = BASE;
	if (exec("vld4_16_all")) {
		ck64("vld4_16_all", 0, 0x1234123412341234ull);
		ck64("vld4_16_all", 1, 0x5678567856785678ull);
		ck64("vld4_16_all", 2, 0x9ABC9ABC9ABC9ABCull);
		ck64("vld4_16_all", 3, 0xDEF0DEF0DEF0DEF0ull);
	}
	rst();
	mem[BASE] = 1; mem[BASE + 1] = 0; mem[BASE + 2] = 0; mem[BASE + 3] = 0;
	mem[BASE + 4] = 2; mem[BASE + 8] = 3; mem[BASE + 12] = 4;
	cpu.r[1] = BASE;
	if (exec("vld4_32_all")) {
		ck64("vld4_32_all", 0, 0x0000000100000001ull);
		ck64("vld4_32_all", 1, 0x0000000200000002ull);
		ck64("vld4_32_all", 2, 0x0000000300000003ull);
		ck64("vld4_32_all", 3, 0x0000000400000004ull);
	}
	rst();
	mem[BASE] = 0x77; mem[BASE + 1] = 0x66; mem[BASE + 2] = 0x55; mem[BASE + 3] = 0x44;
	cpu.r[1] = BASE;
	if (exec("vld1_32_all_wb")) {
		ck64("vld1_32_all_wb", 0, 0x4455667744556677ull);
		ck32("vld1_32_all_wb", "r1", cpu.r[1], BASE + 4);
	}

	/* ---------------- ARMv8 register-stride and all-lane variants ---------------- */
	/* VLD4.8 with stride 2: d0, d2, d4, d6 get bytes 0,4,8... / 1,5,9... / 2,6,... / 3,7,... */
	rst();
	fill_seq(BASE, 32);
	cpu.r[1] = BASE;
	if (exec("vld4_8_stride2")) {
		ck64("vld4_8_stride2", 0, 0x1C1814100C080400ull);
		ck64("vld4_8_stride2", 2, 0x1D1915110D090501ull);
		ck64("vld4_8_stride2", 4, 0x1E1A16120E0A0602ull);
		ck64("vld4_8_stride2", 6, 0x1F1B17130F0B0703ull);
	}
	/* VLD3.16 stride 2 */
	rst();
	fill_seq(BASE, 24);
	cpu.r[1] = BASE;
	if (exec("vld3_16_stride2")) {
		ck64("vld3_16_stride2", 0, 0x13120D0C07060100ull);
		ck64("vld3_16_stride2", 2, 0x15140F0E09080302ull);
		ck64("vld3_16_stride2", 4, 0x171611100B0A0504ull);
	}
	/* VLD2.32 stride 2 */
	rst();
	fill_seq(BASE, 16);
	cpu.r[1] = BASE;
	if (exec("vld2_32_stride2")) {
		ck64("vld2_32_stride2", 0, 0x0B0A090803020100ull);
		ck64("vld2_32_stride2", 2, 0x0F0E0D0C07060504ull);
	}
	/* VST4.8 stride 2 */
	rst();
	cpu.r[1] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x1716151413121110ull;
	cpu.d[4].u64 = 0x2726252423222120ull;
	cpu.d[6].u64 = 0x3736353433323130ull;
	if (exec("vst4_8_stride2")) {
		static const uint8_t want[32] = {
			0, 0x10, 0x20, 0x30, 1, 0x11, 0x21, 0x31, 2, 0x12, 0x22, 0x32, 3, 0x13, 0x23, 0x33,
			4, 0x14, 0x24, 0x34, 5, 0x15, 0x25, 0x35, 6, 0x16, 0x26, 0x36, 7, 0x17, 0x27, 0x37
		};
		ck_bytes("vst4_8_stride2", BASE, 32, want);
	}
	/* VLD1.8 to all lanes of two registers */
	rst();
	mem[BASE] = 0x11;
	mem[BASE + 1] = 0x22;
	cpu.r[1] = BASE;
	if (exec("vld1_8_all2")) {
		ck64("vld1_8_all2", 0, 0x1111111111111111ull);
		ck64("vld1_8_all2", 1, 0x2222222222222222ull);
	}
	/* VLD2.8 to all lanes with stride 2 */
	rst();
	mem[BASE] = 0x11;
	mem[BASE + 1] = 0x22;
	cpu.r[1] = BASE;
	if (exec("vld2_8_all_s2")) {
		ck64("vld2_8_all_s2", 0, 0x1111111111111111ull);
		ck64("vld2_8_all_s2", 2, 0x2222222222222222ull);
	}
	/* VLD4.32 to all lanes, 128-bit alignment encoding */
	rst();
	mem[BASE] = 1; mem[BASE + 4] = 2; mem[BASE + 8] = 3; mem[BASE + 12] = 4;
	cpu.r[1] = BASE;
	if (exec("vld4_32_all_al128")) {
		ck64("vld4_32_all_al128", 0, 0x0000000100000001ull);
		ck64("vld4_32_all_al128", 1, 0x0000000200000002ull);
		ck64("vld4_32_all_al128", 2, 0x0000000300000003ull);
		ck64("vld4_32_all_al128", 3, 0x0000000400000004ull);
	}
	rst();
	mem[BASE] = 5; mem[BASE + 4] = 6;
	cpu.r[1] = BASE;
	if (exec("vld2_32_all_al64")) {
		ck64("vld2_32_all_al64", 0, 0x0000000500000005ull);
		ck64("vld2_32_all_al64", 2, 0x0000000600000006ull);
	}
	/* r13 (SP) as the base register is legal for the structure forms */
	rst();
	fill_seq(BASE, 8);
	cpu.r[13] = BASE;
	if (exec("vld1_8_sp"))
		ck64("vld1_8_sp", 0, 0x0706050403020100ull);
	rst();
	cpu.r[13] = BASE;
	cpu.d[0].u64 = 0x0706050403020100ull;
	if (exec("vst1_8_sp"))
		ck_bytes("vst1_8_sp", BASE, 8, seq16);
	rst();
	fill_seq(BASE, 8);
	cpu.r[13] = BASE;
	if (exec("vld1_8_sp_wb")) {
		ck64("vld1_8_sp_wb", 0, 0x0706050403020100ull);
		ck32("vld1_8_sp_wb", "sp", cpu.r[13], BASE + 8);
	}

	/* ---------------- must be UNDEF ---------------- */
	rst();
	ck_undef("undef_vadd");

	free(mem);
	printf("t_ldst: %d checks, %d failures\n", checks, fails);
	return fails == 0 ? 0 : 1;
}
