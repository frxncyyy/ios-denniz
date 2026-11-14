/* SPDX-License-Identifier: MIT */
/*
 * t_v16.c -- host unit tests for package V16 (n2a32_t32_mem.c, n2a32_t32_misc.c).
 *
 * The class functions are called directly, exactly as the core calls them: c->r[15] holds
 * instruction address + 4, c->next_pc the address of the following instruction, c->in_it is
 * zero unless a test sets it.  Guest memory is one 16 MiB calloc buffer (guest addresses below
 * 16 MiB).  Every encoding comes from llvm-mc via gen_enc.py (enc.h); every expected
 * register/flag/memory value is a literal derived from the ARM ARM pseudocode.
 *
 * Coverage: all load/store single forms (immediate 12, immediate 8 pre/post/writeback,
 * unprivileged, register with LSL 0-3, literal with positive and negative offsets), LDR pc and
 * STR pc, LDRD/STRD (immediate, pre/post, literal), LDM/STM IA/DB with and without writeback,
 * PUSH/POP wide, LDREX/STREX (word and B/H/D), CLREX, TBB/TBH (Rn and PC base), the preload
 * hints, B.W / B<c>.W (all 14 conditions over 8 flag combinations) / BL / BLX over their whole
 * immediate range, MRS/MSR APSR, the hints, the barriers, UDF.W and MRC p15 c13 c0 3, plus a
 * 200000-word random sweep and structured sweeps of every encoding in the class's spaces.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "n2a32.h"
#include "enc.h"

/* The memory layer is plain RAM in this test: the inline accessors still reference the MMIO
 * hooks, so provide them (they are never called: mmio_idx is all zero). */
uint32_t n2a32_mmio_load(struct n2a32_mem *m, uint32_t a, unsigned bytes)
{
	(void)m; (void)a; (void)bytes;
	return 0;
}

void n2a32_mmio_store(struct n2a32_mem *m, uint32_t a, unsigned bytes, uint32_t v)
{
	(void)m; (void)a; (void)bytes; (void)v;
}

#define MEM_SIZE (16u << 20)

static uint8_t *g_mem;
static struct n2a32_mem g_m;
static struct n2a32_cpu C;

static unsigned long g_checks, g_fails;

static void chk(int ok, int line, const char *what, unsigned long long got,
		unsigned long long want)
{
	g_checks++;
	if (!ok) {
		g_fails++;
		if (g_fails <= 60)
			printf("FAIL t_v16.c:%d  %s  got=0x%llx want=0x%llx\n",
			       line, what, got, want);
	}
}

#define CHECK_EQ(got, want) \
	chk((unsigned long long)(got) == (unsigned long long)(want), __LINE__, #got, \
	    (unsigned long long)(got), (unsigned long long)(want))
#define CHECK_NE(got, want) \
	chk((unsigned long long)(got) != (unsigned long long)(want), __LINE__, #got " != " #want, \
	    (unsigned long long)(got), (unsigned long long)(want))

/* ------------------------------------------------------------------ memory helpers */

static void w8(uint32_t a, uint8_t v) { g_mem[a] = v; }
static void w16(uint32_t a, uint16_t v) { g_mem[a] = (uint8_t)v; g_mem[a + 1] = (uint8_t)(v >> 8); }
static void w32(uint32_t a, uint32_t v)
{
	g_mem[a] = (uint8_t)v;
	g_mem[a + 1] = (uint8_t)(v >> 8);
	g_mem[a + 2] = (uint8_t)(v >> 16);
	g_mem[a + 3] = (uint8_t)(v >> 24);
}
static void w64(uint32_t a, uint64_t v) { w32(a, (uint32_t)v); w32(a + 4, (uint32_t)(v >> 32)); }
static uint32_t r32(uint32_t a)
{
	return (uint32_t)g_mem[a] | (uint32_t)g_mem[a + 1] << 8 |
	       (uint32_t)g_mem[a + 2] << 16 | (uint32_t)g_mem[a + 3] << 24;
}
static uint64_t r64(uint32_t a) { return (uint64_t)r32(a) | (uint64_t)r32(a + 4) << 32; }

static void setup(void)
{
	memset(&C, 0, sizeof C);
	C.mem = &g_m;
	C.r[15] = 0x1004;               /* instruction address 0x1000 + 4 */
	C.next_pc = 0x1008;
	C.thumb = 1;
	C.tpidruro = 0xDEADBEEFu;
	/* Fresh scratch: the literal area (0x1000-0x1FFF and 0x800 below it) and the data /
	 * stack area.  Keeps every case independent without a 16 MiB memset per test. */
	memset(g_mem + 0x0000, 0, 0x2100);
	memset(g_mem + 0x7F00, 0, 0x1200);
}

#define MEM_OK(word) do { \
	int _r = n2a32_t32_mem(&C, (word)); \
	CHECK_EQ(_r, N2A32_OK); \
} while (0)
#define MEM_UNDEF(word) do { \
	int _r = n2a32_t32_mem(&C, (word)); \
	CHECK_EQ(_r, N2A32_UNDEF); \
	CHECK_EQ(C.stop, N2A32_STOP_NONE); \
} while (0)
#define MISC_OK(word) do { \
	int _r = n2a32_t32_misc(&C, (word)); \
	CHECK_EQ(_r, N2A32_OK); \
} while (0)
#define MISC_UNDEF(word) do { \
	int _r = n2a32_t32_misc(&C, (word)); \
	CHECK_EQ(_r, N2A32_UNDEF); \
	CHECK_EQ(C.stop, N2A32_STOP_NONE); \
} while (0)

/* ------------------------------------------------------------------ load/store single, imm12 */

static void t_ldst_imm12(void)
{
	/* LDRB */
	setup();
	C.r[1] = 0x8000;
	w8(0x8000, 0x11);
	MEM_OK(E_LDRB_I12_0);
	CHECK_EQ(C.r[0], 0x11);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	w8(0x8004, 0xA5);
	MEM_OK(E_LDRB_I12_4);
	CHECK_EQ(C.r[0], 0xA5);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	w8(0x8ABC, 0x5A);
	MEM_OK(E_LDRB_I12_ABC);
	CHECK_EQ(C.r[0], 0x5A);
	/* no bigger reads: the bytes around stay untouched */
	CHECK_EQ(g_mem[0x8ABD], 0);

	setup();
	C.r[1] = 0x8000;
	w8(0x8FFF, 0x77);
	MEM_OK(E_LDRB_I12_FFF);
	CHECK_EQ(C.r[0], 0x77);

	/* STRB */
	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0x12345600u;
	MEM_OK(E_STRB_I12_4);
	CHECK_EQ(g_mem[0x8004], 0x00);
	CHECK_EQ(g_mem[0x8003], 0x00);   /* byte store, no neighbours written */
	CHECK_EQ(g_mem[0x8005], 0x00);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[3] = 0x8000;
	C.r[2] = 0x9ABCDE78u;
	MEM_OK(E_STRB_I12_ABC);
	CHECK_EQ(g_mem[0x8ABC], 0x78);
	CHECK_EQ(g_mem[0x8ABB], 0x00);
	CHECK_EQ(C.r[3], 0x8000u);
	CHECK_EQ(C.r[2], 0x9ABCDE78u);

	/* LDRH / STRH */
	setup();
	C.r[1] = 0x8000;
	w16(0x8006, 0xBEEF);
	MEM_OK(E_LDRH_I12_6);
	CHECK_EQ(C.r[0], 0xBEEFu);
	CHECK_EQ(g_mem[0x8008], 0);

	setup();
	C.r[5] = 0x8000;
	w16(0x8ABC, 0x1234);
	MEM_OK(E_LDRH_I12_ABC);
	CHECK_EQ(C.r[4], 0x1234u);
	CHECK_EQ(g_mem[0x8ABE], 0);      /* halfword read only */

	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0xDEADBEEFu;
	MEM_OK(E_STRH_I12_6);
	CHECK_EQ(g_mem[0x8006], 0xEF);
	CHECK_EQ(g_mem[0x8007], 0xBE);
	CHECK_EQ(g_mem[0x8008], 0x00);
	CHECK_EQ(g_mem[0x8005], 0x00);

	setup();
	C.r[7] = 0x8000;
	C.r[6] = 0x1234u;
	MEM_OK(E_STRH_I12_ABC);
	CHECK_EQ(g_mem[0x8ABC], 0x34);
	CHECK_EQ(g_mem[0x8ABD], 0x12);

	/* LDR / STR word */
	setup();
	C.r[1] = 0x8000;
	w32(0x8004, 0xCAFEBABEu);
	MEM_OK(E_LDR_I12_4);
	CHECK_EQ(C.r[0], 0xCAFEBABEu);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[3] = 0x8000;
	w32(0x8ABC, 0x11223344u);
	MEM_OK(E_LDR_I12_ABC);
	CHECK_EQ(C.r[2], 0x11223344u);
	CHECK_EQ(g_mem[0x8AC0], 0);

	setup();
	C.r[9] = 0x8000;
	w32(0x8FFC, 0xAABBCCDDu);
	MEM_OK(E_LDR_I12_FFC);
	CHECK_EQ(C.r[8], 0xAABBCCDDu);

	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0x55667788u;
	MEM_OK(E_STR_I12_4);
	CHECK_EQ(r32(0x8004), 0x55667788u);
	CHECK_EQ(r32(0x8000), 0x00000000u);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[3] = 0x8000;
	C.r[2] = 0x99AABBCCu;
	MEM_OK(E_STR_I12_ABC);
	CHECK_EQ(r32(0x8ABC), 0x99AABBCCu);
	CHECK_EQ(r32(0x8AC0), 0x00000000u);

	/* LDRSB */
	setup();
	C.r[1] = 0x8000;
	w8(0x8000, 0x80);
	MEM_OK(E_LDRSB_I12_0);
	CHECK_EQ(C.r[0], 0xFFFFFF80u);

	setup();
	C.r[1] = 0x8000;
	w8(0x8004, 0x7F);
	MEM_OK(E_LDRSB_I12_4);
	CHECK_EQ(C.r[0], 0x0000007Fu);

	setup();
	C.r[3] = 0x8000;
	w8(0x8ABC, 0xFF);
	MEM_OK(E_LDRSB_I12_ABC);
	CHECK_EQ(C.r[2], 0xFFFFFFFFu);

	/* LDRSH */
	setup();
	C.r[1] = 0x8000;
	w16(0x8000, 0x8001);
	MEM_OK(E_LDRSH_I12_0);
	CHECK_EQ(C.r[0], 0xFFFF8001u);

	setup();
	C.r[1] = 0x8000;
	w16(0x8006, 0x7FFF);
	MEM_OK(E_LDRSH_I12_6);
	CHECK_EQ(C.r[0], 0x00007FFFu);

	setup();
	C.r[3] = 0x8000;
	w16(0x8ABC, 0xFFFF);
	MEM_OK(E_LDRSH_I12_ABC);
	CHECK_EQ(C.r[2], 0xFFFFFFFFu);
}

/* ------------------------------------------------------------------ load/store single, imm8 */

static void t_ldst_imm8(void)
{
	/* LDRB */
	setup();
	C.r[1] = 0x8004;
	w8(0x8000, 0x42);
	MEM_OK(E_LDRB_I8_NEG);
	CHECK_EQ(C.r[0], 0x42);
	CHECK_EQ(C.r[1], 0x8004u);       /* no writeback */

	setup();
	C.r[1] = 0x8000;
	w8(0x8000, 0x33);
	MEM_OK(E_LDRB_I8_POST);
	CHECK_EQ(C.r[0], 0x33);
	CHECK_EQ(C.r[1], 0x8004u);

	setup();
	C.r[1] = 0x8004;
	w8(0x8004, 0x44);
	MEM_OK(E_LDRB_I8_POST_NEG);
	CHECK_EQ(C.r[0], 0x44);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	w8(0x8004, 0x55);
	MEM_OK(E_LDRB_I8_PRE);
	CHECK_EQ(C.r[0], 0x55);
	CHECK_EQ(C.r[1], 0x8004u);

	setup();
	C.r[1] = 0x8004;
	w8(0x8000, 0x66);
	MEM_OK(E_LDRB_I8_PRE_NEG);
	CHECK_EQ(C.r[0], 0x66);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8100;
	w8(0x8001, 0x77);
	MEM_OK(E_LDRB_I8_FF);
	CHECK_EQ(C.r[0], 0x77);
	CHECK_EQ(C.r[1], 0x8001u);

	/* STRB */
	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0xABu;
	MEM_OK(E_STRB_I8_POST);
	CHECK_EQ(g_mem[0x8000], 0xAB);
	CHECK_EQ(C.r[1], 0x8004u);
	CHECK_EQ(g_mem[0x8004], 0x00);

	setup();
	C.r[1] = 0x8004;
	C.r[0] = 0xCDu;
	MEM_OK(E_STRB_I8_PRE_NEG);
	CHECK_EQ(g_mem[0x8000], 0xCD);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8004;
	C.r[0] = 0xEFu;
	MEM_OK(E_STRB_I8_NEG);
	CHECK_EQ(g_mem[0x8001], 0xEF);
	CHECK_EQ(g_mem[0x8004], 0x00);
	CHECK_EQ(C.r[1], 0x8004u);

	/* LDRH */
	setup();
	C.r[1] = 0x8008;
	w16(0x8002, 0x1234);
	MEM_OK(E_LDRH_I8_NEG);
	CHECK_EQ(C.r[0], 0x1234u);
	CHECK_EQ(C.r[1], 0x8008u);

	setup();
	C.r[1] = 0x8000;
	w16(0x8000, 0x5678);
	MEM_OK(E_LDRH_I8_POST);
	CHECK_EQ(C.r[0], 0x5678u);
	CHECK_EQ(C.r[1], 0x8006u);

	setup();
	C.r[1] = 0x8000;
	w16(0x8006, 0x9ABC);
	MEM_OK(E_LDRH_I8_PRE);
	CHECK_EQ(C.r[0], 0x9ABCu);
	CHECK_EQ(C.r[1], 0x8006u);

	setup();
	C.r[1] = 0x8008;
	C.r[0] = 0x1111u;
	MEM_OK(E_STRH_I8_POST);
	CHECK_EQ(g_mem[0x8008], 0x11);
	CHECK_EQ(g_mem[0x8009], 0x11);
	CHECK_EQ(C.r[1], 0x8002u);
	CHECK_EQ(g_mem[0x8002], 0x00);

	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0x2222u;
	MEM_OK(E_STRH_I8_PRE);
	CHECK_EQ(g_mem[0x8006], 0x22);
	CHECK_EQ(g_mem[0x8007], 0x22);
	CHECK_EQ(C.r[1], 0x8006u);

	/* LDR / STR */
	setup();
	C.r[1] = 0x8004;
	w32(0x8000, 0x01020304u);
	MEM_OK(E_LDR_I8_NEG);
	CHECK_EQ(C.r[0], 0x01020304u);
	CHECK_EQ(C.r[1], 0x8004u);

	setup();
	C.r[1] = 0x8000;
	w32(0x8000, 0xA1B2C3D4u);
	MEM_OK(E_LDR_I8_POST);
	CHECK_EQ(C.r[0], 0xA1B2C3D4u);
	CHECK_EQ(C.r[1], 0x8004u);

	setup();
	C.r[1] = 0x8004;
	w32(0x8004, 0x11223344u);
	MEM_OK(E_LDR_I8_POST_NEG);
	CHECK_EQ(C.r[0], 0x11223344u);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	w32(0x8004, 0x55667788u);
	MEM_OK(E_LDR_I8_PRE);
	CHECK_EQ(C.r[0], 0x55667788u);
	CHECK_EQ(C.r[1], 0x8004u);

	setup();
	C.r[1] = 0x8004;
	w32(0x8000, 0x99AABBCCu);
	MEM_OK(E_LDR_I8_PRE_NEG);
	CHECK_EQ(C.r[0], 0x99AABBCCu);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0xDEADBEEFu;
	MEM_OK(E_STR_I8_POST);
	CHECK_EQ(r32(0x8000), 0xDEADBEEFu);
	CHECK_EQ(C.r[1], 0x8004u);
	CHECK_EQ(r32(0x8004), 0u);

	setup();
	C.r[1] = 0x8004;
	C.r[0] = 0xFEEDFACEu;
	MEM_OK(E_STR_I8_PRE_NEG);
	CHECK_EQ(r32(0x8000), 0xFEEDFACEu);
	CHECK_EQ(C.r[1], 0x8000u);

	/* LDRSB / LDRSH */
	setup();
	C.r[1] = 0x8004;
	w8(0x8000, 0x80);
	MEM_OK(E_LDRSB_I8_NEG);
	CHECK_EQ(C.r[0], 0xFFFFFF80u);

	setup();
	C.r[1] = 0x8000;
	w8(0x8000, 0x7F);
	MEM_OK(E_LDRSB_I8_POST);
	CHECK_EQ(C.r[0], 0x0000007Fu);
	CHECK_EQ(C.r[1], 0x8004u);

	setup();
	C.r[1] = 0x8008;
	w16(0x8002, 0x8000);
	MEM_OK(E_LDRSH_I8_NEG);
	CHECK_EQ(C.r[0], 0xFFFF8000u);

	setup();
	C.r[1] = 0x8000;
	w16(0x8006, 0xFFFF);
	MEM_OK(E_LDRSH_I8_PRE);
	CHECK_EQ(C.r[0], 0xFFFFFFFFu);
	CHECK_EQ(C.r[1], 0x8006u);
}

/* ------------------------------------------------------------------ unprivileged */

static void t_ldst_unpriv(void)
{
	setup();
	C.r[1] = 0x8000;
	w32(0x8000, 0x13572468u);
	MEM_OK(E_LDRT_0);
	CHECK_EQ(C.r[0], 0x13572468u);
	CHECK_EQ(C.r[1], 0x8000u);        /* no writeback */

	setup();
	C.r[1] = 0x8000;
	w32(0x8004, 0x24681357u);
	MEM_OK(E_LDRT_4);
	CHECK_EQ(C.r[0], 0x24681357u);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0x24681357u;
	MEM_OK(E_STRT_4);
	CHECK_EQ(r32(0x8004), 0x24681357u);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	w8(0x8004, 0x99);
	MEM_OK(E_LDRBT_4);
	CHECK_EQ(C.r[0], 0x99);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0x123456AAu;
	MEM_OK(E_STRBT_4);
	CHECK_EQ(g_mem[0x8004], 0xAA);
	CHECK_EQ(g_mem[0x8005], 0x00);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	w16(0x8004, 0xABCD);
	MEM_OK(E_LDRHT_4);
	CHECK_EQ(C.r[0], 0xABCDu);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	C.r[0] = 0x11223344u;
	MEM_OK(E_STRHT_4);
	CHECK_EQ(g_mem[0x8004], 0x44);
	CHECK_EQ(g_mem[0x8005], 0x33);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	w8(0x8004, 0xFC);
	MEM_OK(E_LDRSBT_4);
	CHECK_EQ(C.r[0], 0xFFFFFFFCu);

	setup();
	C.r[1] = 0x8000;
	w16(0x8004, 0x8001);
	MEM_OK(E_LDRSHT_4);
	CHECK_EQ(C.r[0], 0xFFFF8001u);
}

/* ------------------------------------------------------------------ register offset */

static void t_ldst_reg(void)
{
	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w8(0x8010, 0x5A);
	MEM_OK(E_LDRB_R0);
	CHECK_EQ(C.r[0], 0x5A);
	CHECK_EQ(C.r[2], 0x10u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w8(0x8020, 0x11);
	MEM_OK(E_LDRB_R1);
	CHECK_EQ(C.r[0], 0x11);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w8(0x8040, 0x22);
	MEM_OK(E_LDRB_R2);
	CHECK_EQ(C.r[0], 0x22);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w8(0x8080, 0x33);
	MEM_OK(E_LDRB_R3);
	CHECK_EQ(C.r[0], 0x33);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	C.r[0] = 0xABu;
	MEM_OK(E_STRB_R3);
	CHECK_EQ(g_mem[0x8080], 0xAB);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w16(0x8080, 0xCAFE);
	MEM_OK(E_LDRH_R3);
	CHECK_EQ(C.r[0], 0xCAFEu);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	C.r[0] = 0x1234u;
	MEM_OK(E_STRH_R1);
	CHECK_EQ(g_mem[0x8020], 0x34);
	CHECK_EQ(g_mem[0x8021], 0x12);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w32(0x8010, 0x01020304u);
	MEM_OK(E_LDR_R0);
	CHECK_EQ(C.r[0], 0x01020304u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w32(0x8040, 0x0A0B0C0Du);
	MEM_OK(E_LDR_R2);
	CHECK_EQ(C.r[0], 0x0A0B0C0Du);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x04;
	w32(0x8020, 0xE0E1E2E3u);
	MEM_OK(E_LDR_R3);
	CHECK_EQ(C.r[0], 0xE0E1E2E3u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	C.r[0] = 0xCAFEF00Du;
	MEM_OK(E_STR_R2);
	CHECK_EQ(r32(0x8040), 0xCAFEF00Du);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w8(0x8020, 0x80);
	MEM_OK(E_LDRSB_R1);
	CHECK_EQ(C.r[0], 0xFFFFFF80u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x04;
	w16(0x8020, 0x7FFF);
	MEM_OK(E_LDRSH_R3);
	CHECK_EQ(C.r[0], 0x00007FFFu);

	/* The remaining shift variants. */
	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w32(0x8020, 0x13579BDFu);
	MEM_OK(E_LDR_R1);
	CHECK_EQ(C.r[0], 0x13579BDFu);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x100;
	C.r[0] = 0x2468ACE0u;
	MEM_OK(E_STR_R0);
	CHECK_EQ(r32(0x8100), 0x2468ACE0u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x10;
	w16(0x8020, 0xFEDC);
	MEM_OK(E_LDRH_R1);
	CHECK_EQ(C.r[0], 0xFEDCu);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x8;
	w16(0x8020, 0x1357);
	MEM_OK(E_LDRH_R2);
	CHECK_EQ(C.r[0], 0x1357u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x20;
	w8(0x8020, 0x80);
	MEM_OK(E_LDRSB_R0);
	CHECK_EQ(C.r[0], 0xFFFFFF80u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x20;
	w16(0x8020, 0x8001);
	MEM_OK(E_LDRSH_R0);
	CHECK_EQ(C.r[0], 0xFFFF8001u);

	/* The shifted index wraps modulo 2^32: 0x40000000 << 2 == 0. */
	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x40000000u;
	w32(0x8000, 0x5A5A5A5Au);
	MEM_OK(E_LDR_R2);
	CHECK_EQ(C.r[0], 0x5A5A5A5Au);

	/* 0x40000001 << 2 == 4 */
	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x40000001u;
	w32(0x8004, 0xA5A5A5A5u);
	MEM_OK(E_LDR_R2);
	CHECK_EQ(C.r[0], 0xA5A5A5A5u);
}

/* ------------------------------------------------------------------ literal */

static void t_ldst_literal(void)
{
	/* r[15] = 0x1004, so Align(PC,4) = 0x1004. */
	setup();
	w32(0x1008, 0x12345678u);
	MEM_OK(E_LDR_LIT);
	CHECK_EQ(C.r[0], 0x12345678u);

	setup();
	w32(0x1000, 0x87654321u);
	MEM_OK(E_LDR_LIT_NEG);
	CHECK_EQ(C.r[0], 0x87654321u);

	setup();
	w32(0x1004 + 0xABC, 0x11223344u);
	MEM_OK(E_LDR_LIT_ABC);
	CHECK_EQ(C.r[2], 0x11223344u);

	setup();
	w32(0x1004 - 0x800, 0x55667788u);
	MEM_OK(E_LDR_LIT_NEG_800);
	CHECK_EQ(C.r[2], 0x55667788u);

	setup();
	w8(0x1008, 0x9A);
	MEM_OK(E_LDRB_LIT);
	CHECK_EQ(C.r[0], 0x9A);

	setup();
	w8(0x1000, 0x5B);
	MEM_OK(E_LDRB_LIT_NEG);
	CHECK_EQ(C.r[0], 0x5B);

	setup();
	w16(0x1004 + 6, 0xBEEF);
	MEM_OK(E_LDRH_LIT);
	CHECK_EQ(C.r[0], 0xBEEFu);

	setup();
	w16(0x1004 - 0x800, 0x1234);
	MEM_OK(E_LDRH_LIT_NEG_800);
	CHECK_EQ(C.r[0], 0x1234u);

	setup();
	w8(0x1008, 0x80);
	MEM_OK(E_LDRSB_LIT);
	CHECK_EQ(C.r[0], 0xFFFFFF80u);

	setup();
	w8(0x1004 - 0x800, 0x7F);
	MEM_OK(E_LDRSB_LIT_NEG);
	CHECK_EQ(C.r[0], 0x0000007Fu);

	setup();
	w16(0x1004 + 6, 0x8001);
	MEM_OK(E_LDRSH_LIT);
	CHECK_EQ(C.r[0], 0xFFFF8001u);

	setup();
	w16(0x1004 - 0x800, 0x7FFF);
	MEM_OK(E_LDRSH_LIT_NEG);
	CHECK_EQ(C.r[0], 0x00007FFFu);

	/* Align(PC,4): with r15 = 0x1006 the same encoding reads 0x1004 + offset. */
	setup();
	C.r[15] = 0x1006;
	C.next_pc = 0x1008;
	w8(0x1008, 0x66);                 /* Align(0x1006,4) + 4 */
	w8(0x100A, 0x99);                 /* what an unaligned base would read */
	MEM_OK(E_LDRB_LIT);
	CHECK_EQ(C.r[0], 0x66);
	CHECK_EQ(C.r[15], 0x1006u);
}

/* ------------------------------------------------------------------ pc as data / destination */

static void t_ldst_pc(void)
{
	setup();
	C.r[1] = 0x8000;
	w32(0x8000, 0x2001u);
	MEM_OK(E_LDR_PC);
	CHECK_EQ(C.next_pc, 0x2000u);
	CHECK_EQ(C.thumb, 1);
	CHECK_EQ(C.r[1], 0x8000u);

	setup();
	C.r[1] = 0x8000;
	w32(0x8004, 0x2000u);
	MEM_OK(E_LDR_PC_I12);
	CHECK_EQ(C.next_pc, 0x2000u);
	CHECK_EQ(C.thumb, 0);

	setup();
	C.r[1] = 0x8000;
	w32(0x8000, 0x3001u);
	MEM_OK(E_LDR_PC_POST);
	CHECK_EQ(C.next_pc, 0x3000u);
	CHECK_EQ(C.thumb, 1);
	CHECK_EQ(C.r[1], 0x8004u);

	setup();
	w32(0x1008, 0x4001u);
	MEM_OK(E_LDR_PC_LIT);
	CHECK_EQ(C.next_pc, 0x4000u);
	CHECK_EQ(C.thumb, 1);

	/* STR pc stores the biased PC value, c->r[15]. */
	setup();
	C.r[1] = 0x8000;
	MEM_OK(E_STR_PC);
	CHECK_EQ(r32(0x8000), 0x1004u);

	setup();
	C.r[1] = 0x8000;
	MEM_OK(E_STR_PC_I12);
	CHECK_EQ(r32(0x8004), 0x1004u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x40;
	MEM_OK(E_STR_PC_REG);
	CHECK_EQ(r32(0x8040), 0x1004u);

	setup();
	C.r[1] = 0x8000;
	C.r[2] = 0x40;
	w32(0x8040, 0x5001u);
	MEM_OK(E_LDR_PC_REG);
	CHECK_EQ(C.next_pc, 0x5000u);
	CHECK_EQ(C.thumb, 1);
}

/* ------------------------------------------------------------------ preloads */

static void t_preloads(void)
{
	static const uint32_t preloads[] = {
		E_PLD_I12_0, E_PLD_I12_4, E_PLD_I12_ABC, E_PLD_I8_NEG, E_PLD_I8_FF,
		E_PLD_REG, E_PLD_REG_LSL2, E_PLD_LIT, E_PLD_LIT_NEG,
		E_PLI_I12_4, E_PLI_I8_NEG, E_PLI_REG_LSL3, E_PLI_LIT, E_PLI_LIT_NEG,
		E_PLDW_0, E_PLDW_4, E_PLDW_ABC, E_PLDW_I8_NEG, E_PLDW_REG,
		E_PLDW_REG_LSL2, E_PLDW_LIT,
	};
	uint32_t saved[16];
	unsigned i;

	for (i = 0; i < sizeof preloads / sizeof preloads[0]; i++) {
		int rc;

		setup();
		C.r[0] = 0x11111111u;
		C.r[1] = 0x8000;
		C.r[2] = 0x1000;
		C.r[3] = 0x22222222u;
		memcpy(saved, C.r, sizeof saved);
		rc = n2a32_t32_mem(&C, preloads[i]);
		CHECK_EQ(rc, N2A32_OK);
		CHECK_EQ(memcmp(saved, C.r, sizeof saved), 0);
		CHECK_EQ(C.next_pc, 0x1008u);
	}
}

/* ------------------------------------------------------------------ LDRD / STRD */

static void t_ldrd_strd(void)
{
	setup();
	C.r[5] = 0x8000;
	w64(0x8008, 0x1122334455667788ull);
	MEM_OK(E_LDRD_8);
	CHECK_EQ(C.r[0], 0x55667788u);
	CHECK_EQ(C.r[1], 0x11223344u);
	CHECK_EQ(C.r[5], 0x8000u);

	setup();
	C.r[5] = 0x8008;
	w64(0x8000, 0xAABBCCDDEEFF0011ull);
	MEM_OK(E_LDRD_NEG8);
	CHECK_EQ(C.r[0], 0xEEFF0011u);
	CHECK_EQ(C.r[1], 0xAABBCCDDu);
	CHECK_EQ(C.r[5], 0x8008u);

	setup();
	C.r[5] = 0x8000;
	w64(0x8000, 0x0102030405060708ull);
	MEM_OK(E_LDRD_0);
	CHECK_EQ(C.r[2], 0x05060708u);
	CHECK_EQ(C.r[3], 0x01020304u);

	setup();
	C.r[5] = 0x8000;
	w64(0x8000, 0x0F1E2D3C4B5A6978ull);
	MEM_OK(E_LDRD_POST8);
	CHECK_EQ(C.r[0], 0x4B5A6978u);
	CHECK_EQ(C.r[1], 0x0F1E2D3Cu);
	CHECK_EQ(C.r[5], 0x8008u);

	setup();
	C.r[5] = 0x8008;
	w64(0x8008, 0x1112131415161718ull);
	MEM_OK(E_LDRD_POST_NEG8);
	CHECK_EQ(C.r[0], 0x15161718u);
	CHECK_EQ(C.r[1], 0x11121314u);
	CHECK_EQ(C.r[5], 0x8000u);

	setup();
	C.r[5] = 0x8000;
	w64(0x8008, 0x2122232425262728ull);
	MEM_OK(E_LDRD_PRE_WB);
	CHECK_EQ(C.r[0], 0x25262728u);
	CHECK_EQ(C.r[1], 0x21222324u);
	CHECK_EQ(C.r[5], 0x8008u);

	setup();
	C.r[5] = 0x8008;
	w64(0x8000, 0x3132333435363738ull);
	MEM_OK(E_LDRD_PRE_NEG_WB);
	CHECK_EQ(C.r[0], 0x35363738u);
	CHECK_EQ(C.r[1], 0x31323334u);
	CHECK_EQ(C.r[5], 0x8000u);

	setup();
	C.r[6] = 0x8000;
	w64(0x8004, 0x4142434445464748ull);
	MEM_OK(E_LDRD_4);
	CHECK_EQ(C.r[4], 0x45464748u);
	CHECK_EQ(C.r[5], 0x41424344u);
	CHECK_EQ(C.r[6], 0x8000u);

	setup();
	C.r[10] = 0x8000;
	w64(0x83FC, 0x5152535455565758ull);
	MEM_OK(E_LDRD_3FC);
	CHECK_EQ(C.r[8], 0x55565758u);
	CHECK_EQ(C.r[9], 0x51525354u);

	setup();
	C.r[5] = 0x8000;
	C.r[0] = 0x11223344u;
	C.r[1] = 0x55667788u;
	MEM_OK(E_STRD_8);
	CHECK_EQ(r64(0x8008), 0x5566778811223344ull);
	CHECK_EQ(r64(0x8000), 0ull);
	CHECK_EQ(C.r[5], 0x8000u);

	setup();
	C.r[5] = 0x8008;
	C.r[0] = 0xAABBCCDDu;
	C.r[1] = 0xEEFF0011u;
	MEM_OK(E_STRD_NEG8);
	CHECK_EQ(r64(0x8000), 0xEEFF0011AABBCCDDull);

	setup();
	C.r[5] = 0x8000;
	C.r[2] = 0x01020304u;
	C.r[3] = 0x05060708u;
	MEM_OK(E_STRD_0);
	CHECK_EQ(r64(0x8000), 0x0506070801020304ull);

	setup();
	C.r[5] = 0x8000;
	C.r[0] = 0x11111111u;
	C.r[1] = 0x22222222u;
	MEM_OK(E_STRD_POST8);
	CHECK_EQ(r64(0x8000), 0x2222222211111111ull);
	CHECK_EQ(C.r[5], 0x8008u);

	setup();
	C.r[5] = 0x8008;
	C.r[2] = 0x33333333u;
	C.r[3] = 0x44444444u;
	MEM_OK(E_STRD_POST_NEG8);
	CHECK_EQ(r64(0x8008), 0x4444444433333333ull);
	CHECK_EQ(C.r[5], 0x8000u);

	setup();
	C.r[5] = 0x8000;
	C.r[0] = 0x55555555u;
	C.r[1] = 0x66666666u;
	MEM_OK(E_STRD_PRE_WB);
	CHECK_EQ(r64(0x8008), 0x6666666655555555ull);
	CHECK_EQ(C.r[5], 0x8008u);

	setup();
	C.r[5] = 0x8008;
	C.r[2] = 0x77777777u;
	C.r[3] = 0x88888888u;
	MEM_OK(E_STRD_PRE_NEG_WB);
	CHECK_EQ(r64(0x8000), 0x8888888877777777ull);
	CHECK_EQ(C.r[5], 0x8000u);

	setup();
	C.r[12] = 0x8000;
	C.r[10] = 0x99999999u;
	C.r[11] = 0xAAAAAAAAu;
	MEM_OK(E_STRD_3FC);
	CHECK_EQ(r64(0x83FC), 0xAAAAAAAA99999999ull);

	/* Arbitrary register pairs (used by the vendor libraries). */
	setup();
	C.r[0] = 0x8000;
	w64(0x8000 + 64, 0x1122334455667788ull);
	MEM_OK(E_LDRD_ODD_RT);
	CHECK_EQ(C.r[3], 0x55667788u);
	CHECK_EQ(C.r[2], 0x11223344u);

	setup();
	C.r[13] = 0x8000;
	w64(0x8000, 0xA1A2A3A4B1B2B3B4ull);
	MEM_OK(E_LDRD_NONCONS);
	CHECK_EQ(C.r[2], 0xB1B2B3B4u);
	CHECK_EQ(C.r[0], 0xA1A2A3A4u);

	setup();
	C.r[1] = 0x8000;
	w64(0x8000, 0x0102030405060708ull);
	MEM_OK(E_LDRD_SAME);
	CHECK_EQ(C.r[4], 0x05060708u);
	CHECK_EQ(C.r[6], 0x01020304u);

	setup();
	C.r[13] = 0x8000;
	C.r[3] = 0xCAFEF00Du;
	C.r[1] = 0x0BADF00Du;
	MEM_OK(E_STRD_ODD_RT);
	CHECK_EQ(r64(0x8008), 0x0BADF00DCAFEF00Dull);
	CHECK_EQ(C.r[13], 0x8000u);

	setup();
	C.r[13] = 0x8000;
	C.r[5] = 0x11111111u;
	C.r[8] = 0x22222222u;
	MEM_OK(E_STRD_NONCONS);
	CHECK_EQ(r64(0x8000), 0x2222222211111111ull);

	setup();
	C.r[7] = 0x8000;
	C.r[6] = 0x33333333u;
	C.r[2] = 0x22222222u;
	MEM_OK(E_STRD_SAME);
	CHECK_EQ(r32(0x8000), 0x33333333u);
	CHECK_EQ(r32(0x8004), 0x22222222u);

	/* Literal forms: base = Align(PC,4) = 0x1004. */
	setup();
	w64(0x1004 + 8, 0xBBBBBBBBCCCCCCCCull);
	MEM_OK(E_LDRD_LIT);
	CHECK_EQ(C.r[0], 0xCCCCCCCCu);
	CHECK_EQ(C.r[1], 0xBBBBBBBBu);

	setup();
	w64(0x1004 - 8, 0xDDDDDDDDEEEEEEEEull);
	MEM_OK(E_LDRD_LIT_NEG);
	CHECK_EQ(C.r[0], 0xEEEEEEEEu);
	CHECK_EQ(C.r[1], 0xDDDDDDDDu);

	setup();
	C.r[0] = 0x12345678u;
	C.r[1] = 0x9ABCDEF0u;
	MEM_OK(E_STRD_LIT);
	CHECK_EQ(r64(0x1004 + 8), 0x9ABCDEF012345678ull);

	setup();
	C.r[0] = 0x0F1E2D3Cu;
	C.r[1] = 0x4B5A6978u;
	MEM_OK(E_STRD_LIT_NEG);
	CHECK_EQ(r64(0x1004 - 8), 0x4B5A69780F1E2D3Cull);
}

/* ------------------------------------------------------------------ LDM / STM */

static void t_ldm_stm(void)
{
	setup();
	C.r[0] = 0x8000;
	w32(0x8000, 0x11);
	w32(0x8004, 0x22);
	MEM_OK(E_LDMIA);
	CHECK_EQ(C.r[1], 0x11u);
	CHECK_EQ(C.r[2], 0x22u);
	CHECK_EQ(C.r[0], 0x8000u);

	setup();
	C.r[0] = 0x8000;
	w32(0x8000, 0x33);
	w32(0x8004, 0x44);
	MEM_OK(E_LDMIA_WB);
	CHECK_EQ(C.r[1], 0x33u);
	CHECK_EQ(C.r[2], 0x44u);
	CHECK_EQ(C.r[0], 0x8008u);

	setup();
	C.r[0] = 0x8000;
	{
		unsigned i;
		for (i = 0; i < 9; i++)
			w32(0x8000 + 4 * i, 0x100 + i);
	}
	MEM_OK(E_LDMIA_MANY);
	CHECK_EQ(C.r[1], 0x100u);
	CHECK_EQ(C.r[2], 0x101u);
	CHECK_EQ(C.r[3], 0x102u);
	CHECK_EQ(C.r[5], 0x103u);
	CHECK_EQ(C.r[8], 0x104u);
	CHECK_EQ(C.r[9], 0x105u);
	CHECK_EQ(C.r[10], 0x106u);
	CHECK_EQ(C.r[11], 0x107u);
	CHECK_EQ(C.r[4], 0u);            /* not in the list */
	CHECK_EQ(C.r[0], 0x8000u);

	setup();
	C.r[3] = 0x8000;
	{
		unsigned i;
		for (i = 0; i < 9; i++)
			w32(0x8000 + 4 * i, 0x200 + i);
	}
	MEM_OK(E_LDMIA_WB_MANY);
	CHECK_EQ(C.r[0], 0x200u);
	CHECK_EQ(C.r[1], 0x201u);
	CHECK_EQ(C.r[2], 0x202u);
	CHECK_EQ(C.r[4], 0x203u);
	CHECK_EQ(C.r[5], 0x204u);
	CHECK_EQ(C.r[6], 0x205u);
	CHECK_EQ(C.r[7], 0x206u);
	CHECK_EQ(C.r[8], 0x207u);
	CHECK_EQ(C.r[9], 0x208u);
	CHECK_EQ(C.r[3], 0x8000u + 36u);

	setup();
	C.r[0] = 0x8000;
	C.r[1] = 0xAAAAu;
	C.r[2] = 0xBBBBu;
	MEM_OK(E_STMIA);
	CHECK_EQ(r32(0x8000), 0xAAAAu);
	CHECK_EQ(r32(0x8004), 0xBBBBu);
	CHECK_EQ(C.r[0], 0x8000u);

	setup();
	C.r[0] = 0x8000;
	C.r[1] = 0xCCCCu;
	C.r[2] = 0xDDDDu;
	MEM_OK(E_STMIA_WB);
	CHECK_EQ(r32(0x8000), 0xCCCCu);
	CHECK_EQ(r32(0x8004), 0xDDDDu);
	CHECK_EQ(C.r[0], 0x8008u);

	setup();
	C.r[5] = 0x8000;
	C.r[0] = 0xF0u;
	C.r[1] = 0xF1u;
	C.r[2] = 0xF2u;
	C.r[3] = 0xF3u;
	C.r[4] = 0xF4u;
	C.r[6] = 0xF6u;
	C.r[7] = 0xF7u;
	MEM_OK(E_STMIA_MANY);
	CHECK_EQ(r32(0x8000), 0xF0u);
	CHECK_EQ(r32(0x8004), 0xF1u);
	CHECK_EQ(r32(0x8008), 0xF2u);
	CHECK_EQ(r32(0x800C), 0xF3u);
	CHECK_EQ(r32(0x8010), 0xF4u);
	CHECK_EQ(r32(0x8014), 0xF6u);
	CHECK_EQ(r32(0x8018), 0xF7u);
	CHECK_EQ(C.r[5], 0x8000u + 28u);

	setup();
	C.r[0] = 0x8010;
	w32(0x8008, 0x1234);
	w32(0x800C, 0x5678);
	MEM_OK(E_LDMDB);
	CHECK_EQ(C.r[1], 0x1234u);
	CHECK_EQ(C.r[2], 0x5678u);
	CHECK_EQ(C.r[0], 0x8010u);

	setup();
	C.r[0] = 0x8010;
	w32(0x8000, 0x10);
	w32(0x8004, 0x11);
	w32(0x8008, 0x12);
	w32(0x800C, 0x13);
	MEM_OK(E_LDMDB_WB);
	CHECK_EQ(C.r[1], 0x10u);
	CHECK_EQ(C.r[2], 0x11u);
	CHECK_EQ(C.r[3], 0x12u);
	CHECK_EQ(C.r[4], 0x13u);
	CHECK_EQ(C.r[0], 0x8000u);

	setup();
	C.r[0] = 0x8010;
	C.r[1] = 0xAA01u;
	C.r[2] = 0xAA02u;
	MEM_OK(E_STMDB);
	CHECK_EQ(r32(0x8008), 0xAA01u);
	CHECK_EQ(r32(0x800C), 0xAA02u);
	CHECK_EQ(C.r[0], 0x8010u);

	setup();
	C.r[0] = 0x8010;
	C.r[1] = 0xBB01u;
	C.r[2] = 0xBB02u;
	C.r[3] = 0xBB03u;
	C.r[4] = 0xBB04u;
	MEM_OK(E_STMDB_WB);
	CHECK_EQ(r32(0x8000), 0xBB01u);
	CHECK_EQ(r32(0x8004), 0xBB02u);
	CHECK_EQ(r32(0x8008), 0xBB03u);
	CHECK_EQ(r32(0x800C), 0xBB04u);
	CHECK_EQ(C.r[0], 0x8000u);

	/* PUSH/POP wide: SP = 0x9000. */
	setup();
	C.r[13] = 0x9000;
	C.r[0] = 0x01u;
	C.r[1] = 0x02u;
	C.r[2] = 0x03u;
	C.r[14] = 0x04u;
	MEM_OK(E_PUSH_W);
	CHECK_EQ(r32(0x8FF0), 0x01u);
	CHECK_EQ(r32(0x8FF4), 0x02u);
	CHECK_EQ(r32(0x8FF8), 0x03u);
	CHECK_EQ(r32(0x8FFC), 0x04u);
	CHECK_EQ(C.r[13], 0x8FF0u);

	setup();
	C.r[13] = 0x9000;
	C.r[4] = 0x14u;
	C.r[14] = 0x15u;
	MEM_OK(E_PUSH_W_ONE);
	CHECK_EQ(r32(0x8FF8), 0x14u);
	CHECK_EQ(r32(0x8FFC), 0x15u);
	CHECK_EQ(C.r[13], 0x8FF8u);

	setup();
	C.r[13] = 0x9000;
	{
		unsigned i;
		for (i = 4; i <= 11; i++)
			C.r[i] = 0x40u + i;
	}
	C.r[14] = 0x5Au;
	MEM_OK(E_PUSH_W_MANY);
	{
		unsigned i;
		for (i = 0; i < 8; i++)
			CHECK_EQ(r32(0x9000 - 36 + 4 * i), 0x40u + 4u + i);
		CHECK_EQ(r32(0x9000 - 4), 0x5Au);
	}
	CHECK_EQ(C.r[13], 0x9000u - 36u);

	setup();
	C.r[13] = 0x8000;
	w32(0x8000, 0xA0u);
	w32(0x8004, 0xA2u);
	MEM_OK(E_POP_W);
	CHECK_EQ(C.r[0], 0xA0u);
	CHECK_EQ(C.r[2], 0xA2u);
	CHECK_EQ(C.r[13], 0x8008u);

	setup();
	C.r[13] = 0x8000;
	w32(0x8000, 0xB0u);
	w32(0x8004, 0x2021u);
	MEM_OK(E_POP_W_PC);
	CHECK_EQ(C.r[0], 0xB0u);
	CHECK_EQ(C.next_pc, 0x2020u);
	CHECK_EQ(C.thumb, 1);
	CHECK_EQ(C.r[13], 0x8008u);

	setup();
	C.r[13] = 0x8000;
	{
		unsigned i;
		for (i = 0; i < 8; i++)
			w32(0x8000 + 4 * i, 0xC0u + i);
		w32(0x8000 + 32, 0x2031u);
	}
	MEM_OK(E_POP_W_PC_MANY);
	CHECK_EQ(C.r[4], 0xC0u);
	CHECK_EQ(C.r[11], 0xC7u);
	CHECK_EQ(C.next_pc, 0x2030u);
	CHECK_EQ(C.thumb, 1);
	CHECK_EQ(C.r[13], 0x8000u + 36u);

	setup();
	C.r[13] = 0x8000;
	w32(0x8000, 0xD4u);
	w32(0x8004, 0xD5u);
	MEM_OK(E_POP_W_LR);
	CHECK_EQ(C.r[4], 0xD4u);
	CHECK_EQ(C.r[14], 0xD5u);
	CHECK_EQ(C.r[13], 0x8008u);

	setup();
	C.r[0] = 0x8000;
	w32(0x8000, 0xE1u);
	w32(0x8004, 0xE2u);
	MEM_OK(E_LDM_LR);
	CHECK_EQ(C.r[1], 0xE1u);
	CHECK_EQ(C.r[14], 0xE2u);
	CHECK_EQ(C.r[0], 0x8000u);

	setup();
	C.r[0] = 0x8010;
	C.r[1] = 0xF1u;
	C.r[14] = 0xF2u;
	MEM_OK(E_STMDB_LR);
	CHECK_EQ(r32(0x8008), 0xF1u);
	CHECK_EQ(r32(0x800C), 0xF2u);
	CHECK_EQ(C.r[0], 0x8008u);

	setup();
	C.r[0] = 0x8000;
	w32(0x8000, 0x71u);
	w32(0x8004, 0x72u);
	w32(0x8008, 0x73u);
	w32(0x800C, 0x2071u);
	MEM_OK(E_LDMIA_PC);
	CHECK_EQ(C.r[1], 0x71u);
	CHECK_EQ(C.r[2], 0x72u);
	CHECK_EQ(C.r[3], 0x73u);
	CHECK_EQ(C.next_pc, 0x2070u);
	CHECK_EQ(C.thumb, 1);
	CHECK_EQ(C.r[0], 0x8000u);

	setup();
	C.r[0] = 0x8000;
	w32(0x8000, 0x81u);
	w32(0x8004, 0x82u);
	w32(0x8008, 0x83u);
	w32(0x800C, 0x2081u);
	MEM_OK(E_LDMIA_PC_WB);
	CHECK_EQ(C.next_pc, 0x2080u);
	CHECK_EQ(C.r[0], 0x8010u);

	setup();
	C.r[0] = 0x8010;
	w32(0x8008, 0x91u);
	w32(0x800C, 0x2091u);
	MEM_OK(E_LDMDB_PC);
	CHECK_EQ(C.r[1], 0x91u);
	CHECK_EQ(C.next_pc, 0x2090u);
	CHECK_EQ(C.r[0], 0x8008u);
}

/* ------------------------------------------------------------------ exclusives */

static void t_exclusive(void)
{
	setup();
	w32(0x8000, 0x11223344u);
	C.r[1] = 0x8000;
	MEM_OK(E_LDREX_0);
	CHECK_EQ(C.r[0], 0x11223344u);
	CHECK_EQ(C.excl_valid, 4u);
	C.r[0] = 0x55667788u;
	MEM_OK(E_STREX_0);
	CHECK_EQ(C.r[2], 0u);
	CHECK_EQ(r32(0x8000), 0x55667788u);

	/* Monitor broken by an intervening write: STREX fails. */
	setup();
	w32(0x8000, 0x11223344u);
	C.r[1] = 0x8000;
	MEM_OK(E_LDREX_0);
	w32(0x8000, 0x99AABBCCu);
	C.r[0] = 0x55667788u;
	MEM_OK(E_STREX_0);
	CHECK_EQ(C.r[2], 1u);
	CHECK_EQ(r32(0x8000), 0x99AABBCCu);

	/* An unaligned address always fails the exclusive store (n2a32_excl_store). */
	setup();
	w32(0x8001, 0x11223344u);
	C.r[1] = 0x8001;
	MEM_OK(E_LDREX_0);
	C.r[0] = 0x55667788u;
	MEM_OK(E_STREX_0);
	CHECK_EQ(C.r[2], 1u);

	/* A plain load between LDREX and STREX does not break the monitor (value-based). */
	setup();
	w32(0x8000, 0x11223344u);
	w32(0x8010, 0x99AABBCCu);
	C.r[1] = 0x8000;
	MEM_OK(E_LDREX_0);
	{
		uint32_t v = n2a32_ld32(&C, 0x8010);

		CHECK_EQ(v, 0x99AABBCCu);
	}
	C.r[0] = 0x55667788u;
	MEM_OK(E_STREX_0);
	CHECK_EQ(C.r[2], 0u);
	CHECK_EQ(r32(0x8000), 0x55667788u);

	/* A store with no monitor at all fails. */
	setup();
	w32(0x8000, 0x0u);
	C.r[1] = 0x8000;
	C.r[0] = 0x1u;
	MEM_OK(E_STREX_0);
	CHECK_EQ(C.r[2], 1u);

	/* CLREX opens the monitor. */
	setup();
	w32(0x8000, 0x1u);
	C.r[1] = 0x8000;
	MEM_OK(E_LDREX_0);
	MISC_OK(E_CLREX);
	C.r[0] = 0x2u;
	MEM_OK(E_STREX_0);
	CHECK_EQ(C.r[2], 1u);
	CHECK_EQ(r32(0x8000), 1u);

	/* Offset forms: the imm8 field is scaled by 4, so #4 is the byte offset 4. */
	setup();
	w32(0x8004, 0xCAFEF00Du);
	C.r[1] = 0x8000;
	MEM_OK(E_LDREX_4);
	CHECK_EQ(C.r[0], 0xCAFEF00Du);
	CHECK_EQ(C.excl_addr, 0x8004u);

	setup();
	w32(0x83FC, 0x0BADF00Du);
	C.r[7] = 0x8000;
	MEM_OK(E_LDREX_3FC);
	CHECK_EQ(C.r[6], 0x0BADF00Du);

	setup();
	w32(0x8004, 0x11111111u);
	C.r[1] = 0x8000;
	MEM_OK(E_LDREX_4);
	C.r[0] = 0x22222222u;
	MEM_OK(E_STREX_4);
	CHECK_EQ(C.r[2], 0u);
	CHECK_EQ(r32(0x8004), 0x22222222u);

	setup();
	w32(0x83FC, 0x33333333u);
	C.r[7] = 0x8000;                  /* LDREX_3FC is ldrex r6, [r7, #0x3fc] */
	MEM_OK(E_LDREX_3FC);
	CHECK_EQ(C.r[6], 0x33333333u);
	C.r[11] = 0x8000;                 /* STREX_3FC is strex r9, r10, [r11, #0x3fc] */
	C.r[10] = 0x44444444u;
	MEM_OK(E_STREX_3FC);
	CHECK_EQ(C.r[9], 0u);
	CHECK_EQ(r32(0x83FC), 0x44444444u);

	/* Byte / halfword exclusives. */
	setup();
	w8(0x8000, 0x5A);
	C.r[1] = 0x8000;
	MEM_OK(E_LDREXB);
	CHECK_EQ(C.r[0], 0x5Au);
	CHECK_EQ(C.excl_valid, 1u);
	C.r[0] = 0xA5u;
	MEM_OK(E_STREXB);
	CHECK_EQ(C.r[2], 0u);
	CHECK_EQ(g_mem[0x8000], 0xA5);

	setup();
	w8(0x8000, 0x5A);
	C.r[1] = 0x8000;
	MEM_OK(E_LDREXB);
	w8(0x8000, 0x00);
	C.r[0] = 0xA5u;
	MEM_OK(E_STREXB);
	CHECK_EQ(C.r[2], 1u);
	CHECK_EQ(g_mem[0x8000], 0x00);

	setup();
	w16(0x8000, 0x1234);
	C.r[4] = 0x8000;
	MEM_OK(E_LDREXH);
	CHECK_EQ(C.r[3], 0x1234u);
	CHECK_EQ(C.excl_valid, 2u);
	C.r[7] = 0x8000;
	C.r[6] = 0xABCDu;
	MEM_OK(E_STREXH);
	CHECK_EQ(C.r[5], 0u);
	CHECK_EQ(g_mem[0x8000], 0xCD);
	CHECK_EQ(g_mem[0x8001], 0xAB);

	/* Doubleword exclusive. */
	setup();
	w32(0x8000, 0x11223344u);
	w32(0x8004, 0x55667788u);
	C.r[2] = 0x8000;
	MEM_OK(E_LDREXD);
	CHECK_EQ(C.r[0], 0x11223344u);
	CHECK_EQ(C.r[1], 0x55667788u);
	CHECK_EQ(C.excl_valid, 8u);
	C.r[0] = 0x99AABBCCu;
	C.r[1] = 0xDDEEFF00u;
	MEM_OK(E_STREXD);
	CHECK_EQ(C.r[3], 0u);
	CHECK_EQ(r32(0x8000), 0x99AABBCCu);
	CHECK_EQ(r32(0x8004), 0xDDEEFF00u);

	setup();
	w32(0x8010, 0x0F0F0F0Fu);
	w32(0x8014, 0xF0F0F0F0u);
	C.r[6] = 0x8010;
	MEM_OK(E_LDREXD_46);
	CHECK_EQ(C.r[4], 0x0F0F0F0Fu);
	CHECK_EQ(C.r[5], 0xF0F0F0F0u);
}

/* ------------------------------------------------------------------ TBB / TBH */

static void t_tbb_tbh(void)
{
	setup();
	C.r[0] = 0x8000;
	C.r[1] = 2;
	w8(0x8002, 0x10);
	MEM_OK(E_TBB);
	CHECK_EQ(C.next_pc, 0x1024u);
	CHECK_EQ(C.thumb, 1);
	CHECK_EQ(C.r[0], 0x8000u);

	setup();
	C.r[1] = 4;
	w8(0x1004 + 4, 0x20);
	MEM_OK(E_TBB_PC);
	CHECK_EQ(C.next_pc, 0x1044u);

	setup();
	C.r[4] = 0x8000;
	C.r[3] = 0;
	w8(0x8000, 0x7F);
	MEM_OK(E_TBB_R3);
	CHECK_EQ(C.next_pc, 0x1004u + 0xFEu);

	setup();
	C.r[0] = 0x8000;
	C.r[1] = 0xFFFFFFFEu;            /* index -2 */
	w8(0x7FFE, 0x08);
	MEM_OK(E_TBB);
	CHECK_EQ(C.next_pc, 0x1014u);

	setup();
	C.r[0] = 0x8000;
	C.r[1] = 8;
	w16(0x8010, 0x1234);
	MEM_OK(E_TBH);
	CHECK_EQ(C.next_pc, 0x1004u + 0x2468u);

	setup();
	C.r[15] = 0x1006;
	C.r[2] = 2;
	w16(0x1006 + 4, 0x0100);
	w16(0x1004 + 4, 0x7FFF);         /* what an Align(PC,4) base would read */
	MEM_OK(E_TBH_PC);
	CHECK_EQ(C.next_pc, 0x1006u + 0x200u);

	setup();
	C.r[6] = 0x8000;
	C.r[7] = 1;
	w16(0x8002, 0x00FF);
	MEM_OK(E_TBH_R7);
	CHECK_EQ(C.next_pc, 0x1004u + 0x1FEu);
}

/* ------------------------------------------------------------------ branches */

static void t_branches(void)
{
	unsigned i;

	/* B.W over its whole 25-bit immediate range. */
	for (i = 0; i < sizeof BR_BW / sizeof BR_BW[0]; i++) {
		setup();
		C.r[14] = 0x7777u;
		MISC_OK(BR_BW[i].word);
		CHECK_EQ(C.next_pc, 0x1004u + (uint32_t)BR_BW[i].off);
		CHECK_EQ(C.thumb, 1);
		CHECK_EQ(C.r[14], 0x7777u);      /* B does not link */
	}

	/* BL. */
	for (i = 0; i < sizeof BR_BL / sizeof BR_BL[0]; i++) {
		setup();
		MISC_OK(BR_BL[i].word);
		CHECK_EQ(C.next_pc, 0x1004u + (uint32_t)BR_BL[i].off);
		CHECK_EQ(C.thumb, 1);
		CHECK_EQ(C.r[14], 0x1005u);      /* LR = PC | 1 */
	}

	/* BLX immediate: ARM state, target = Align(PC,4) + imm. */
	for (i = 0; i < sizeof BR_BLX / sizeof BR_BLX[0]; i++) {
		setup();
		MISC_OK(BR_BLX[i].word);
		CHECK_EQ(C.next_pc, (0x1004u & ~3u) + (uint32_t)BR_BLX[i].off);
		CHECK_EQ(C.thumb, 0);
		CHECK_EQ(C.r[14], 0x1005u);
	}

	/* B<c>.W over the 21-bit immediate range: taken with Z=0 for BNE. */
	for (i = 0; i < sizeof BR_BC / sizeof BR_BC[0]; i++) {
		setup();
		C.z = 0;
		MISC_OK(BR_BC[i].word);
		CHECK_EQ(C.next_pc, 0x1004u + (uint32_t)BR_BC[i].off);

		setup();
		C.z = 1;                        /* BNE not taken: PC stays at next_pc */
		MISC_OK(BR_BC[i].word);
		CHECK_EQ(C.next_pc, 0x1008u);
	}

	/* The T3 space allocates conditions 0000-1101 only.  cond == 1110 is unallocated
	 * (B.W is the 25-bit T4 form, whose second halfword has bit 12 set); cond == 1111
	 * is the UDF space.  Derive both words from a generated BNE.W by replacing the
	 * condition nibble, so no encoding here is hand-typed. */
	{
		uint32_t base = BR_BC_AL[1].word;      /* bne.w #0x100, hw2 bit 12 == 0 */
		uint32_t undef14 = (base & ~(0xFu << 22)) | (0xEu << 22);
		uint32_t undef15 = (base & ~(0xFu << 22)) | (0xFu << 22);

		setup();
		MISC_UNDEF(undef14);
		CHECK_EQ(C.next_pc, 0x1008u);          /* not branched */
		setup();
		MISC_UNDEF(undef15);
		CHECK_EQ(C.next_pc, 0x1008u);
	}

	/* Every condition against eight flag combinations. */
	{
		static const uint8_t flags[8][4] = {   /* n, z, c, v */
			{ 0, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 },
			{ 1, 0, 0, 0 }, { 1, 1, 1, 0 }, { 0, 1, 1, 1 }, { 1, 0, 1, 1 },
		};
		unsigned cond, f;

		for (cond = 0; cond < 14; cond++) {
			for (f = 0; f < 8; f++) {
				unsigned n = flags[f][0], z = flags[f][1];
				unsigned c = flags[f][2], v = flags[f][3];
				int want;

				switch (cond) {
				case 0: want = (int)z; break;                    /* EQ */
				case 1: want = !z; break;                        /* NE */
				case 2: want = (int)c; break;                    /* CS */
				case 3: want = !c; break;                        /* CC */
				case 4: want = (int)n; break;                    /* MI */
				case 5: want = !n; break;                        /* PL */
				case 6: want = (int)v; break;                    /* VS */
				case 7: want = !v; break;                        /* VC */
				case 8: want = c && !z; break;                   /* HI */
				case 9: want = !(c && !z); break;                /* LS */
				case 10: want = (n == v); break;                 /* GE */
				case 11: want = (n != v); break;                 /* LT */
				case 12: want = !z && (n == v); break;           /* GT */
				default: want = !(!z && (n == v)); break;        /* LE */
				}
				setup();
				C.n = (uint8_t)n;
				C.z = (uint8_t)z;
				C.cf = (uint8_t)c;
				C.v = (uint8_t)v;
				MISC_OK(BR_BC_AL[cond].word);
				CHECK_EQ(C.next_pc, want ? 0x1004u + 0x100u : 0x1008u);
			}
		}
	}
}

/* ------------------------------------------------------------------ system */

static void t_system(void)
{
	/* MRS Rd, APSR. */
	setup();
	C.n = 1; C.z = 0; C.cf = 1; C.v = 0; C.q = 1; C.ge = 5;
	MISC_OK(E_MRS_R0);
	CHECK_EQ(C.r[0], 0xA8050000u);

	setup();
	C.n = 0; C.z = 1; C.cf = 0; C.v = 1; C.q = 0; C.ge = 0xF;
	MISC_OK(E_MRS_R7);
	CHECK_EQ(C.r[7], 0x500F0000u);

	setup();
	MISC_OK(E_MRS_R12);
	CHECK_EQ(C.r[12], 0u);

	/* MSR APSR_nzcvq. */
	setup();
	C.r[0] = 0xF8000000u;             /* N Z C V Q */
	C.ge = 0x3;
	MISC_OK(E_MSR_NZCVQ);
	CHECK_EQ(C.n, 1);
	CHECK_EQ(C.z, 1);
	CHECK_EQ(C.cf, 1);
	CHECK_EQ(C.v, 1);
	CHECK_EQ(C.q, 1);
	CHECK_EQ(C.ge, 0x3u);             /* GE not written by the nzcvq mask */

	setup();
	C.r[0] = 0x08000000u;             /* only Q */
	MISC_OK(E_MSR_NZCVQ);
	CHECK_EQ(C.n, 0);
	CHECK_EQ(C.z, 0);
	CHECK_EQ(C.cf, 0);
	CHECK_EQ(C.v, 0);
	CHECK_EQ(C.q, 1);

	/* MSR APSR_g leaves the flags alone. */
	setup();
	C.n = 1; C.z = 1; C.cf = 1; C.v = 1; C.q = 1;
	C.r[1] = 0x000A0000u;
	MISC_OK(E_MSR_G);
	CHECK_EQ(C.ge, 0xAu);
	CHECK_EQ(C.n, 1);
	CHECK_EQ(C.q, 1);

	/* MSR APSR_nzcvqg. */
	setup();
	C.r[2] = 0x00050000u;
	MISC_OK(E_MSR_NZCVQG);
	CHECK_EQ(C.ge, 0x5u);
	CHECK_EQ(C.n, 0);
	CHECK_EQ(C.z, 0);
	CHECK_EQ(C.cf, 0);
	CHECK_EQ(C.v, 0);
	CHECK_EQ(C.q, 0);

	/* Hints: no effect at all. */
	{
		static const uint32_t hints[] = {
			E_NOP_W, E_YIELD_W, E_WFE_W, E_WFI_W, E_SEV_W, E_DBG0_W, E_DBG15_W,
		};
		unsigned i;

		for (i = 0; i < sizeof hints / sizeof hints[0]; i++) {
			setup();
			C.n = 1; C.z = 1; C.cf = 1; C.v = 1; C.q = 1; C.ge = 0xFu;
			C.r[0] = 0x1234u;
			MISC_OK(hints[i]);
			CHECK_EQ(C.r[0], 0x1234u);
			CHECK_EQ(C.n, 1);
			CHECK_EQ(C.next_pc, 0x1008u);
		}
	}

	/* Barriers: no effect; CLREX drops the monitor. */
	{
		static const uint32_t barriers[] = {
			E_DMB_SY, E_DMB_ISH, E_DMB_ISHST, E_DSB_SY, E_DSB_ISHST, E_ISB_SY,
		};
		unsigned i;

		for (i = 0; i < sizeof barriers / sizeof barriers[0]; i++) {
			setup();
			C.r[0] = 0x99u;
			MISC_OK(barriers[i]);
			CHECK_EQ(C.r[0], 0x99u);
			CHECK_EQ(C.next_pc, 0x1008u);
		}
	}

	/* MRC p15, 0, Rt, c13, c0, 3 reads tpidruro. */
	setup();
	MISC_OK(E_MRC_TPID0);
	CHECK_EQ(C.r[0], 0xDEADBEEFu);
	setup();
	MISC_OK(E_MRC_TPID9);
	CHECK_EQ(C.r[9], 0xDEADBEEFu);
	setup();
	C.tpidruro = 0x12345678u;
	MISC_OK(E_MRC_TPID12);
	CHECK_EQ(C.r[12], 0x12345678u);

	/* UDF.W stops the run with STOP_UNDEF. */
	setup();
	CHECK_EQ(n2a32_t32_misc(&C, E_UDF0_W), N2A32_UNDEF);
	CHECK_EQ(C.stop, N2A32_STOP_UNDEF);
	setup();
	CHECK_EQ(n2a32_t32_misc(&C, E_UDF_W_123), N2A32_UNDEF);
	CHECK_EQ(C.stop, N2A32_STOP_UNDEF);

	/* Everything else in the space must be UNDEF and leave the state alone. */
	{
		static const uint32_t undef_misc[] = {
			E_X_MRC_OPC2_2, E_X_MRC_CRN_12, E_X_MCR_SAME, E_X_MCR_OPC1,
			E_X_MRC_CP14, E_X_MCRR, E_X_MRRC, E_X_LDC, E_X_STC, E_X_CDP,
			E_X_SVC, E_X_BKPT, E_X_T16_LDR, E_X_T16_PUSH, E_X_T16_UDf,
			E_X_T16_BLX_REG,
		};
		unsigned i;

		for (i = 0; i < sizeof undef_misc / sizeof undef_misc[0]; i++) {
			setup();
			MISC_UNDEF(undef_misc[i]);
			CHECK_EQ(C.next_pc, 0x1008u);
		}
	}
}

/* ------------------------------------------------------------------ sweeps */

static uint32_t xs = 0x12345678u;

static uint32_t rnd(void)
{
	xs ^= xs << 13;
	xs ^= xs >> 17;
	xs ^= xs << 5;
	return xs;
}

/* Small register values keep every generated address inside the 16 MiB buffer. */
static void sweep_setup(void)
{
	unsigned i;

	memset(&C, 0, sizeof C);
	C.mem = &g_m;
	C.thumb = 1;
	C.r[15] = 0x1004;
	C.next_pc = 0x1008;
	for (i = 0; i < 13; i++)
		C.r[i] = 0x400u + 4u * i;
	C.r[13] = 0x2000;
	C.r[14] = 0x3000;
}

static void t_sweeps(void)
{
	unsigned long n;

	/* 200000 random words: half drawn from the two encoding regions of the class,
	 * half fully random.  Every word must return OK or UNDEF and must not crash. */
	for (n = 0; n < 200000u; n++) {
		uint32_t w = rnd();
		int r;

		if (n & 1u) {
			unsigned hw1 = (w & 1u) ? (0xE800u | (w & 0x1FFu))
						: (0xF800u | (w & 0x1FFu));
			w = ((uint32_t)hw1 << 16) | (w & 0xFFFFu);
		}
		sweep_setup();
		r = n2a32_t32_mem(&C, w);
		g_checks++;
		if (r != N2A32_OK && r != N2A32_UNDEF) {
			g_fails++;
			printf("FAIL sweep mem word=0x%08x r=%d\n", w, r);
		}
		sweep_setup();
		r = n2a32_t32_misc(&C, w);
		g_checks++;
		if (r != N2A32_OK && r != N2A32_UNDEF) {
			g_fails++;
			printf("FAIL sweep misc word=0x%08x r=%d\n", w, r);
		}
	}

	/* Structured: every second halfword for representative first halfwords of every
	 * subspace, plus every first halfword of the class with a few second halfwords. */
	{
		static const unsigned hw1_list[] = {
			0xE880, 0xE890, 0xE8A0, 0xE8B0, 0xE900, 0xE910, 0xE920, 0xE930,
			0xE840, 0xE850, 0xE8C0, 0xE8D0, 0xE8E0, 0xE8F0, 0xE9C0, 0xE9D0,
			0xE9E0, 0xE9F0, 0xE800, 0xE980,
			0xF800, 0xF810, 0xF820, 0xF830, 0xF840, 0xF850, 0xF880, 0xF890,
			0xF8A0, 0xF8B0, 0xF8C0, 0xF8D0, 0xF900, 0xF910, 0xF920, 0xF930,
			0xF940, 0xF950, 0xF980, 0xF990, 0xF9A0, 0xF9B0, 0xF9C0, 0xF9D0,
		};
		unsigned i;
		uint32_t hw2;

		for (i = 0; i < sizeof hw1_list / sizeof hw1_list[0]; i++) {
			for (hw2 = 0; hw2 < 0x10000u; hw2++) {
				uint32_t w = ((uint32_t)hw1_list[i] << 16) | hw2;
				int r;

				sweep_setup();
				r = n2a32_t32_mem(&C, w);
				g_checks++;
				if (r != N2A32_OK && r != N2A32_UNDEF) {
					g_fails++;
					printf("FAIL structured word=0x%08x\n", w);
				}
			}
		}
		/* LDM/STM must reject an empty list and any list containing SP. */
		for (i = 0; i < 8; i++) {
			static const unsigned ldm_hw1[8] = {
				0xE880, 0xE890, 0xE8A0, 0xE8B0, 0xE900, 0xE910, 0xE920, 0xE930,
			};
			uint32_t w0 = ((uint32_t)ldm_hw1[i] << 16) | 0x0000u;
			uint32_t wsp = ((uint32_t)ldm_hw1[i] << 16) | 0x2001u;

			sweep_setup();
			g_checks++;
			if (n2a32_t32_mem(&C, w0) != N2A32_UNDEF) {
				g_fails++;
				printf("FAIL empty list accepted hw1=0x%04x\n", ldm_hw1[i]);
			}
			sweep_setup();
			g_checks++;
			if (n2a32_t32_mem(&C, wsp) != N2A32_UNDEF) {
				g_fails++;
				printf("FAIL SP in list accepted hw1=0x%04x\n", ldm_hw1[i]);
			}
		}
	}
}

/* ------------------------------------------------------------------ main */

int main(void)
{
	g_mem = (uint8_t *)calloc(1, MEM_SIZE);
	if (!g_mem) {
		printf("FAIL cannot allocate guest memory\n");
		return 1;
	}
	memset(&g_m, 0, sizeof g_m);
	g_m.base = g_mem;

	t_ldst_imm12();
	t_ldst_imm8();
	t_ldst_unpriv();
	t_ldst_reg();
	t_ldst_literal();
	t_ldst_pc();
	t_preloads();
	t_ldrd_strd();
	t_ldm_stm();
	t_exclusive();
	t_tbb_tbh();
	t_branches();
	t_system();
	t_sweeps();

	if (g_fails != 0) {
		printf("N2TEST V16 FAIL %lu/%lu checks failed\n", g_fails, g_checks);
		return 1;
	}
	printf("V16 checks %lu\n", g_checks);
	printf("N2TEST V16 PASS %lu/%lu\n", g_checks, g_checks);
	free(g_mem);
	return 0;
}
