/* SPDX-License-Identifier: MIT */
/*
 * R1 independent spot tests for track V instruction classes.
 *
 * Every encoding below was produced by llvm-mc from the assembly text in the comment
 * (clang/LLVM 23.1.4, -triple=armv7a-none-eabi / thumbv7a-none-eabi), never by the
 * implementation under test.  Every expected value is a literal worked out from the
 * Arm ARM pseudocode (A32/T32 instruction set reference) and is written in the comment.
 * The tests do not call any package's test code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include "n2a32.h"

int n2a32_arm_dp(struct n2a32_cpu *c, uint32_t insn);
int n2a32_arm_media(struct n2a32_cpu *c, uint32_t insn);
int n2a32_arm_mem(struct n2a32_cpu *c, uint32_t insn);
int n2a32_arm_misc(struct n2a32_cpu *c, uint32_t insn);
int n2a32_t16(struct n2a32_cpu *c, uint32_t insn);
int n2a32_t32_dp(struct n2a32_cpu *c, uint32_t insn);
int n2a32_neon_shift(struct n2a32_cpu *c, uint32_t insn);
int n2a32_vfp(struct n2a32_cpu *c, uint32_t insn);
int n2a32_neon_3same(struct n2a32_cpu *c, uint32_t insn);
int n2a32_t32_mem(struct n2a32_cpu *c, uint32_t insn);
int n2a32_t32_misc(struct n2a32_cpu *c, uint32_t insn);
int n2a32_neon_misc(struct n2a32_cpu *c, uint32_t insn);
int n2a32_neon_ldst(struct n2a32_cpu *c, uint32_t insn);

static uint8_t ram[1u << 20];
static struct n2a32_mem mem;
static int checks, fails;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { fails++; printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void cpu_reset(struct n2a32_cpu *c)
{
	memset(c, 0, sizeof *c);
	memset(ram, 0, sizeof ram);
	mem.base = ram;
	mem.priv = NULL;
	c->mem = &mem;
	c->r[15] = 0x1008;		/* A32: instruction address + 8 */
	c->next_pc = 0x1004;
	c->thumb = 0;
}

static void cpu_reset_t16(struct n2a32_cpu *c)
{
	cpu_reset(c);
	c->r[15] = 0x1004;		/* T32: instruction address + 4 */
	c->next_pc = 0x1002;
	c->thumb = 1;
}

static void put32(uint32_t addr, uint32_t v) { memcpy(ram + addr, &v, 4); }
static void put16(uint32_t addr, uint16_t v) { memcpy(ram + addr, &v, 2); }
static void put8(uint32_t addr, uint8_t v) { ram[addr] = v; }
static uint32_t get32(uint32_t addr) { uint32_t v; memcpy(&v, ram + addr, 4); return v; }

/* ------------------------------------------------------------------ V10 A32 DP */
static void v10(void)
{
	struct n2a32_cpu c;
	int rc;

	/* adds r0, r1, r2 = E0910002; 0x7fffffff + 1 -> 80000000, N=1 Z=0 C=0 V=1 */
	cpu_reset(&c); c.r[1] = 0x7fffffffu; c.r[2] = 1;
	rc = n2a32_arm_dp(&c, 0xE0910002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u && c.n == 1 && c.z == 0 &&
	      c.cf == 0 && c.v == 1, "adds rc=%d r0=%08x n%u z%u c%u v%u", rc, c.r[0], c.n, c.z, c.cf, c.v);

	/* adcs r0, r1, r2 = E0B10002; ffffffff + 0 + C(1) -> 0, C=1 Z=1 V=0 */
	cpu_reset(&c); c.r[1] = 0xffffffffu; c.r[2] = 0; c.cf = 1;
	rc = n2a32_arm_dp(&c, 0xE0B10002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1 && c.v == 0,
	      "adcs rc=%d r0=%08x c%u z%u v%u", rc, c.r[0], c.cf, c.z, c.v);

	/* sbcs r0, r1, r2 = E0D10002; 5 - 3 - !C(1) = 1, C=1 (no borrow) */
	cpu_reset(&c); c.r[1] = 5; c.r[2] = 3; c.cf = 0;
	rc = n2a32_arm_dp(&c, 0xE0D10002u);
	CHECK(rc == N2A32_OK && c.r[0] == 1 && c.cf == 1 && c.v == 0,
	      "sbcs rc=%d r0=%08x c%u v%u", rc, c.r[0], c.cf, c.v);

	/* rsbs r0, r1, #0 = E2710000; 0 - 80000000 -> 80000000, C=0 V=1 N=1 */
	cpu_reset(&c); c.r[1] = 0x80000000u;
	rc = n2a32_arm_dp(&c, 0xE2710000u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u && c.cf == 0 && c.v == 1 && c.n == 1,
	      "rsbs rc=%d r0=%08x c%u v%u n%u", rc, c.r[0], c.cf, c.v, c.n);

	/* lsrs r0, r1, #32 = E1B00021; 80000001 -> 0, C=bit31=1, Z=1 */
	cpu_reset(&c); c.r[1] = 0x80000001u;
	rc = n2a32_arm_dp(&c, 0xE1B00021u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1,
	      "lsr#32 rc=%d r0=%08x c%u z%u", rc, c.r[0], c.cf, c.z);

	/* rrxs r0, r1 = E1B00061; r1=2, C=1 -> 80000001, C=old bit0=0 */
	cpu_reset(&c); c.r[1] = 2; c.cf = 1;
	rc = n2a32_arm_dp(&c, 0xE1B00061u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000001u && c.cf == 0,
	      "rrx rc=%d r0=%08x c%u", rc, c.r[0], c.cf);

	/* asrs r0, r1, #32 = E1B00041; 80000001 -> ffffffff, C=1 */
	cpu_reset(&c); c.r[1] = 0x80000001u;
	rc = n2a32_arm_dp(&c, 0xE1B00041u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffffffu && c.cf == 1,
	      "asr#32 rc=%d r0=%08x c%u", rc, c.r[0], c.cf);

	/* lsls r0, r1, r2 = E1B00211; r1=1, r2=32 -> 0, C=bit0=1, Z=1 */
	cpu_reset(&c); c.r[1] = 1; c.r[2] = 32;
	rc = n2a32_arm_dp(&c, 0xE1B00211u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1,
	      "lsl reg32 rc=%d r0=%08x c%u z%u", rc, c.r[0], c.cf, c.z);

	/* rors r0, r1, r2 = E1B00271; r1=12345678, r2=32 -> unchanged, C=bit31=0 */
	cpu_reset(&c); c.r[1] = 0x12345678u; c.r[2] = 32;
	rc = n2a32_arm_dp(&c, 0xE1B00271u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x12345678u && c.cf == 0,
	      "ror reg32 rc=%d r0=%08x c%u", rc, c.r[0], c.cf);

	/* muls r0, r1, r2 = E0100291; 10000*10000 -> 0, Z=1, C untouched (preset 1) */
	cpu_reset(&c); c.r[1] = 0x10000u; c.r[2] = 0x10000u; c.cf = 1; c.v = 1;
	rc = n2a32_arm_dp(&c, 0xE0100291u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.z == 1 && c.cf == 1 && c.v == 1,
	      "muls rc=%d r0=%08x z%u c%u v%u", rc, c.r[0], c.z, c.cf, c.v);

	/* umull r0, r1, r2, r3 = E0810392; ffffffff*2 = 1fffffffe -> lo fffffffe hi 1 */
	cpu_reset(&c); c.r[2] = 0xffffffffu; c.r[3] = 2;
	rc = n2a32_arm_dp(&c, 0xE0810392u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffffeu && c.r[1] == 1,
	      "umull rc=%d lo=%08x hi=%08x", rc, c.r[0], c.r[1]);

	/* smull r0, r1, r2, r3 = E0C10392; (-2)*3 = -6 -> fffffffa:ffffffff */
	cpu_reset(&c); c.r[2] = 0xfffffffeu; c.r[3] = 3;
	rc = n2a32_arm_dp(&c, 0xE0C10392u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffffau && c.r[1] == 0xffffffffu,
	      "smull rc=%d lo=%08x hi=%08x", rc, c.r[0], c.r[1]);

	/* mls r0, r1, r2, r3 = E0603291; r3 - r1*r2 = 100 - 35 = 65 */
	cpu_reset(&c); c.r[1] = 5; c.r[2] = 7; c.r[3] = 100;
	rc = n2a32_arm_dp(&c, 0xE0603291u);
	CHECK(rc == N2A32_OK && c.r[0] == 65, "mls rc=%d r0=%u", rc, c.r[0]);

	/* movw r0, #0x1234 = E3010234; r0 preset ffffffff -> 00001234 */
	cpu_reset(&c); c.r[0] = 0xffffffffu;
	rc = n2a32_arm_dp(&c, 0xE3010234u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x1234u, "movw rc=%d r0=%08x", rc, c.r[0]);

	/* movt r0, #0x5678 = E3450678; r0 preset 1234 -> 56781234 */
	cpu_reset(&c); c.r[0] = 0x1234u;
	rc = n2a32_arm_dp(&c, 0xE3450678u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x56781234u, "movt rc=%d r0=%08x", rc, c.r[0]);

	/* clz r0, r1 = E16F0F11; 00010000 -> 15 */
	cpu_reset(&c); c.r[1] = 0x00010000u;
	rc = n2a32_arm_dp(&c, 0xE16F0F11u);
	CHECK(rc == N2A32_OK && c.r[0] == 15, "clz rc=%d r0=%u", rc, c.r[0]);

	/* tst r0, r1, lsl #1 = E1100081; r0=80000000, r1=40000000 -> N=1, C=bit31(r1)=0 */
	cpu_reset(&c); c.r[0] = 0x80000000u; c.r[1] = 0x40000000u;
	rc = n2a32_arm_dp(&c, 0xE1100081u);
	CHECK(rc == N2A32_OK && c.n == 1 && c.z == 0 && c.cf == 0 && c.r[0] == 0x80000000u,
	      "tst rc=%d n%u z%u c%u", rc, c.n, c.z, c.cf);

	/* cmp r0, #0xff000000 = E35004FF; equal -> Z=1 C=1 V=0 */
	cpu_reset(&c); c.r[0] = 0xff000000u;
	rc = n2a32_arm_dp(&c, 0xE35004FFu);
	CHECK(rc == N2A32_OK && c.z == 1 && c.cf == 1 && c.v == 0,
	      "cmp imm rc=%d z%u c%u v%u", rc, c.z, c.cf, c.v);

	/* asr r0, r1, #4 = E1A00241; 80000000 -> f8000000 */
	cpu_reset(&c); c.r[1] = 0x80000000u;
	rc = n2a32_arm_dp(&c, 0xE1A00241u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xf8000000u, "asr#4 rc=%d r0=%08x", rc, c.r[0]);

	/* add r0, r1, r2, lsl #3 = E0810182; 1 + 2<<3 = 17 */
	cpu_reset(&c); c.r[1] = 1; c.r[2] = 2;
	rc = n2a32_arm_dp(&c, 0xE0810182u);
	CHECK(rc == N2A32_OK && c.r[0] == 17, "add shifted rc=%d r0=%u", rc, c.r[0]);

	/* eor r0, r1, #0x0f000000 = E221040F; 0f000000 ^ 0f000000 = 0 */
	cpu_reset(&c); c.r[1] = 0x0f000000u;
	rc = n2a32_arm_dp(&c, 0xE221040Fu);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.z == 0, "eor imm rc=%d r0=%08x", rc, c.r[0]);

	/* mvn r0, r1, ror #8 = E1E00461; ror8(00000001)=01000000 -> feffffff */
	cpu_reset(&c); c.r[1] = 1;
	rc = n2a32_arm_dp(&c, 0xE1E00461u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfeffffffu, "mvn ror rc=%d r0=%08x", rc, c.r[0]);
}

/* ------------------------------------------------------------------ V11 A32 media */
static void v11(void)
{
	struct n2a32_cpu c;
	int rc;

	/* sxtb r0, r1 = E6AF0071; 0x80 -> ffffff80 */
	cpu_reset(&c); c.r[1] = 0x80;
	rc = n2a32_arm_media(&c, 0xE6AF0071u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffff80u, "sxtb rc=%d r0=%08x", rc, c.r[0]);

	/* uxth r0, r1 = E6FF0071; 12345678 -> 00005678 */
	cpu_reset(&c); c.r[1] = 0x12345678u;
	rc = n2a32_arm_media(&c, 0xE6FF0071u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x5678u, "uxth rc=%d r0=%08x", rc, c.r[0]);

	/* sxtb16 r0, r1 = E68F0071; 00800080 -> ff80ff80 */
	cpu_reset(&c); c.r[1] = 0x00800080u;
	rc = n2a32_arm_media(&c, 0xE68F0071u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xff80ff80u, "sxtb16 rc=%d r0=%08x", rc, c.r[0]);

	/* sxtab r0, r2, r1 = E6A20071; r2=1000, r1=0x80 -> 1000-128 = ffffff80? 0x1000+(-128)=f80 */
	cpu_reset(&c); c.r[1] = 0x80; c.r[2] = 0x1000;
	rc = n2a32_arm_media(&c, 0xE6A20071u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x0f80u, "sxtab rc=%d r0=%08x", rc, c.r[0]);

	/* rev r0, r1 = E6BF0F31; 12345678 -> 78563412 */
	cpu_reset(&c); c.r[1] = 0x12345678u;
	rc = n2a32_arm_media(&c, 0xE6BF0F31u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x78563412u, "rev rc=%d r0=%08x", rc, c.r[0]);

	/* rev16 r0, r1 = E6BF0FB1; 12345678 -> 34127856 */
	cpu_reset(&c); c.r[1] = 0x12345678u;
	rc = n2a32_arm_media(&c, 0xE6BF0FB1u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x34127856u, "rev16 rc=%d r0=%08x", rc, c.r[0]);

	/* revsh r0, r1 = E6FF0FB1; 00008001 -> 00000180 */
	cpu_reset(&c); c.r[1] = 0x8001u;
	rc = n2a32_arm_media(&c, 0xE6FF0FB1u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x0180u, "revsh rc=%d r0=%08x", rc, c.r[0]);

	/* rbit r0, r1 = E6FF0F31; 1 -> 80000000 */
	cpu_reset(&c); c.r[1] = 1;
	rc = n2a32_arm_media(&c, 0xE6FF0F31u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u, "rbit rc=%d r0=%08x", rc, c.r[0]);

	/* ubfx r0, r1, #4, #8 = E7E70251; 12345678 bits 11:4 = 67 */
	cpu_reset(&c); c.r[1] = 0x12345678u;
	rc = n2a32_arm_media(&c, 0xE7E70251u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x67u, "ubfx rc=%d r0=%08x", rc, c.r[0]);

	/* sbfx r0, r1, #4, #8 = E7A70251; 00000F80 -> fffffff8 */
	cpu_reset(&c); c.r[1] = 0x00000F80u;
	rc = n2a32_arm_media(&c, 0xE7A70251u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffff8u, "sbfx rc=%d r0=%08x", rc, c.r[0]);

	/* bfi r0, r1, #8, #8 = E7CF0411; ffffffff + 1234 -> ffff34ff */
	cpu_reset(&c); c.r[0] = 0xffffffffu; c.r[1] = 0x1234u;
	rc = n2a32_arm_media(&c, 0xE7CF0411u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffff34ffu, "bfi rc=%d r0=%08x", rc, c.r[0]);

	/* bfc r0, #8, #8 = E7CF041F; ffffffff -> ffff00ff */
	cpu_reset(&c); c.r[0] = 0xffffffffu;
	rc = n2a32_arm_media(&c, 0xE7CF041Fu);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffff00ffu, "bfc rc=%d r0=%08x", rc, c.r[0]);

	/* ssat r0, #8, r1, lsl #4 = E6A70211; 08000000<<4 = 80000000 = -2^31 -> sat -128, Q=1 */
	cpu_reset(&c); c.r[1] = 0x08000000u;
	rc = n2a32_arm_media(&c, 0xE6A70211u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffff80u && c.q == 1, "ssat rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* usat r0, #8, r1 = E6E80011; 0x1234 -> sat 255, Q=1 */
	cpu_reset(&c); c.r[1] = 0x1234u;
	rc = n2a32_arm_media(&c, 0xE6E80011u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffu && c.q == 1, "usat rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* ssat16 r0, #8, r1 = E6A70F31; lo 8000=-32768 -> ff80, hi 7fff -> 7f, Q=1 */
	cpu_reset(&c); c.r[1] = 0x7fff8000u;
	rc = n2a32_arm_media(&c, 0xE6A70F31u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x007fff80u && c.q == 1, "ssat16 rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* usat16 r0, #8, r1 = E6E80F31; lo 0000 -> 00, hi 1234 -> ff, Q=1 */
	cpu_reset(&c); c.r[1] = 0x12340000u;
	rc = n2a32_arm_media(&c, 0xE6E80F31u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x00ff0000u && c.q == 1, "usat16 rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* qadd r0, r1, r2 = E1020051; 7fffffff + 1 -> 7fffffff, Q=1 */
	cpu_reset(&c); c.r[1] = 0x7fffffffu; c.r[2] = 1;
	rc = n2a32_arm_media(&c, 0xE1020051u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x7fffffffu && c.q == 1, "qadd rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* qsub r0, r1, r2 = E1220051; 80000000 - 1 -> 80000000, Q=1 */
	cpu_reset(&c); c.r[1] = 0x80000000u; c.r[2] = 1;
	rc = n2a32_arm_media(&c, 0xE1220051u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u && c.q == 1, "qsub rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* qdadd r0, r1, r2 = E1420051; r1=0, r2=40000000 -> 2*r2 sat 7fffffff, Q=1 */
	cpu_reset(&c); c.r[1] = 0; c.r[2] = 0x40000000u;
	rc = n2a32_arm_media(&c, 0xE1420051u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x7fffffffu && c.q == 1, "qdadd rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* qdsub r0, r1, r2 = E1620051; r1=0, r2=40000000 -> 0 - sat(80000000) = -7fffffff, Q=1 */
	cpu_reset(&c); c.r[1] = 0; c.r[2] = 0x40000000u;
	rc = n2a32_arm_media(&c, 0xE1620051u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000001u && c.q == 1, "qdsub rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* sadd16 r0, r1, r2 = E6110F12; 0001ffff + 00010001 -> 00020000, GE=1111 */
	cpu_reset(&c); c.r[1] = 0x0001ffffu; c.r[2] = 0x00010001u;
	rc = n2a32_arm_media(&c, 0xE6110F12u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x00020000u && c.ge == 0xfu,
	      "sadd16 rc=%d r0=%08x ge=%x", rc, c.r[0], c.ge);

	/* uadd8 r0, r1, r2 = E6510F92; r1=ff, r2=1 -> 00, GE bit0=1 */
	cpu_reset(&c); c.r[1] = 0xff; c.r[2] = 1;
	rc = n2a32_arm_media(&c, 0xE6510F92u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.ge == 1, "uadd8 rc=%d r0=%08x ge=%x", rc, c.r[0], c.ge);

	/* sasx r0, r1, r2 = E6110F32; lo=1+4=5, hi=3-2=1 -> 00010005; GE=1111 */
	cpu_reset(&c); c.r[1] = 0x00030001u; c.r[2] = 0x00040002u;
	rc = n2a32_arm_media(&c, 0xE6110F32u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x00010005u && c.ge == 0xf,
	      "sasx rc=%d r0=%08x ge=%x", rc, c.r[0], c.ge);

	/* ssax r0, r1, r2 = E6110F52; lo=1-4=-3, hi=3+2=5 -> 0005fffd; GE[1:0]=00, GE[3:2]=11 */
	cpu_reset(&c); c.r[1] = 0x00030001u; c.r[2] = 0x00040002u;
	rc = n2a32_arm_media(&c, 0xE6110F52u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x0005fffdu && c.ge == 0xc,
	      "ssax rc=%d r0=%08x ge=%x", rc, c.r[0], c.ge);

	/* shadd16 r0, r1, r2 = E6310F12; (1 + 3)>>1 = 2, (5 + 7)>>1 = 6 -> 00060002 */
	cpu_reset(&c); c.r[1] = 0x00050001u; c.r[2] = 0x00070003u;
	rc = n2a32_arm_media(&c, 0xE6310F12u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x00060002u && c.ge == 0,
	      "shadd16 rc=%d r0=%08x ge=%x", rc, c.r[0], c.ge);

	/* uqadd16 r0, r1, r2 = E6610F12; ffff + 1 -> ffff each, no flags */
	cpu_reset(&c); c.r[1] = 0xffffffffu; c.r[2] = 0x00010001u; c.ge = 0x5; c.q = 1;
	rc = n2a32_arm_media(&c, 0xE6610F12u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffffffu && c.ge == 0x5 && c.q == 1,
	      "uqadd16 rc=%d r0=%08x ge=%x q%u", rc, c.r[0], c.ge, c.q);

	/* sel r0, r1, r2 = E6810FB2; GE=0101 -> bytes 0,2 from r1 (44,22), 1,3 from r2 (cc,aa) */
	cpu_reset(&c); c.r[1] = 0x11223344u; c.r[2] = 0xaabbccddu; c.ge = 0x5;
	rc = n2a32_arm_media(&c, 0xE6810FB2u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xaa22cc44u, "sel rc=%d r0=%08x", rc, c.r[0]);

	/* pkhbt r0, r1, r2, lsl #8 = E6810412; r1=11223344, r2=55667788 -> 66773344 */
	cpu_reset(&c); c.r[1] = 0x11223344u; c.r[2] = 0x55667788u;
	rc = n2a32_arm_media(&c, 0xE6810412u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x66773344u, "pkhbt rc=%d r0=%08x", rc, c.r[0]);

	/* pkhtb r0, r1, r2, asr #8 = E6810452; r1=11223344, r2=55667788 -> 11226677 */
	cpu_reset(&c); c.r[1] = 0x11223344u; c.r[2] = 0x55667788u;
	rc = n2a32_arm_media(&c, 0xE6810452u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x11226677u, "pkhtb rc=%d r0=%08x", rc, c.r[0]);

	/* smulbb r0, r1, r2 = E1600281; r1=fffffffE(-2), r2=3 -> fffffffa */
	cpu_reset(&c); c.r[1] = 0xfffffffeu; c.r[2] = 3;
	rc = n2a32_arm_media(&c, 0xE1600281u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffffau, "smulbb rc=%d r0=%08x", rc, c.r[0]);

	/* smlabb r0, r1, r2, r3 = E1003281; (-2)*3 + 10 = 4 */
	cpu_reset(&c); c.r[1] = 0xfffffffeu; c.r[2] = 3; c.r[3] = 10;
	rc = n2a32_arm_media(&c, 0xE1003281u);
	CHECK(rc == N2A32_OK && c.r[0] == 4, "smlabb rc=%d r0=%u", rc, c.r[0]);

	/* smulwb r0, r1, r2 = E12002A1; r1=10000, r2=00030000 -> (10000*3)>>16 = 0 */
	cpu_reset(&c); c.r[1] = 0x10000u; c.r[2] = 0x00030000u;
	rc = n2a32_arm_media(&c, 0xE12002A1u);
	CHECK(rc == N2A32_OK && c.r[0] == 0, "smulwb rc=%d r0=%u", rc, c.r[0]);

	/* smlawb r0, r1, r2, r3 = E1203281; r1=10000, r2=00030000, r3=7 -> 0 + 7 */
	cpu_reset(&c); c.r[1] = 0x10000u; c.r[2] = 0x00030000u; c.r[3] = 7;
	rc = n2a32_arm_media(&c, 0xE1203281u);
	CHECK(rc == N2A32_OK && c.r[0] == 7, "smlawb rc=%d r0=%u", rc, c.r[0]);

	/* smlalbb r0, r1, r2, r3 = E1410382; r1:r0 = 0, (-2)*3 -> lo fffffffa hi ffffffff */
	cpu_reset(&c); c.r[0] = 0; c.r[1] = 0; c.r[2] = 0xfffffffeu; c.r[3] = 3;
	rc = n2a32_arm_media(&c, 0xE1410382u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffffau && c.r[1] == 0xffffffffu,
	      "smlalbb rc=%d lo=%08x hi=%08x", rc, c.r[0], c.r[1]);

	/* smuad r0, r1, r2 = E700F211; r1=00020003, r2=00040005 -> 3*5 + 2*4 = 23 */
	cpu_reset(&c); c.r[1] = 0x00020003u; c.r[2] = 0x00040005u;
	rc = n2a32_arm_media(&c, 0xE700F211u);
	CHECK(rc == N2A32_OK && c.r[0] == 23, "smuad rc=%d r0=%u", rc, c.r[0]);

	/* smuadx r0, r1, r2 = E700F231; 2*5 + 3*4 = 22 */
	cpu_reset(&c); c.r[1] = 0x00020003u; c.r[2] = 0x00040005u;
	rc = n2a32_arm_media(&c, 0xE700F231u);
	CHECK(rc == N2A32_OK && c.r[0] == 22, "smuadx rc=%d r0=%u", rc, c.r[0]);

	/* smusd r0, r1, r2 = E700F251; 3*5 - 2*4 = 7 */
	cpu_reset(&c); c.r[1] = 0x00020003u; c.r[2] = 0x00040005u;
	rc = n2a32_arm_media(&c, 0xE700F251u);
	CHECK(rc == N2A32_OK && c.r[0] == 7, "smusd rc=%d r0=%u", rc, c.r[0]);

	/* smlad r0, r1, r2, r3 = E7003211; 3*5 + 2*4 + 100 = 123 */
	cpu_reset(&c); c.r[1] = 0x00020003u; c.r[2] = 0x00040005u; c.r[3] = 100;
	rc = n2a32_arm_media(&c, 0xE7003211u);
	CHECK(rc == N2A32_OK && c.r[0] == 123, "smlad rc=%d r0=%u", rc, c.r[0]);

	/* smlald r0, r1, r2, r3 = E7410312; r1:r0 = 0, 3*5 + 2*4 = 23 */
	cpu_reset(&c); c.r[0] = 0; c.r[1] = 0; c.r[2] = 0x00020003u; c.r[3] = 0x00040005u;
	rc = n2a32_arm_media(&c, 0xE7410312u);
	CHECK(rc == N2A32_OK && c.r[0] == 23 && c.r[1] == 0, "smlald rc=%d lo=%u hi=%u", rc, c.r[0], c.r[1]);

	/* smmul r0, r1, r2 = E750F211; (-1)*3 top word -> ffffffff */
	cpu_reset(&c); c.r[1] = 0xffffffffu; c.r[2] = 3;
	rc = n2a32_arm_media(&c, 0xE750F211u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffffffu, "smmul rc=%d r0=%08x", rc, c.r[0]);

	/* smmla r0, r1, r2, r3 = E7503211; (r3<<32 + (-1)*3)>>32 = 2 */
	cpu_reset(&c); c.r[1] = 0xffffffffu; c.r[2] = 3; c.r[3] = 3;
	rc = n2a32_arm_media(&c, 0xE7503211u);
	CHECK(rc == N2A32_OK && c.r[0] == 2, "smmla rc=%d r0=%08x", rc, c.r[0]);

	/* smmls r0, r1, r2, r3 = E75032D1; (r3<<32 - (-1)*3)>>32 = 3 */
	cpu_reset(&c); c.r[1] = 0xffffffffu; c.r[2] = 3; c.r[3] = 3;
	rc = n2a32_arm_media(&c, 0xE75032D1u);
	CHECK(rc == N2A32_OK && c.r[0] == 3, "smmls rc=%d r0=%08x", rc, c.r[0]);

	/* usad8 r0, r1, r2 = E780F211; bytes 01,02,03,04 vs 05,04,03,02 -> 4+2+0+2 = 8 */
	cpu_reset(&c); c.r[1] = 0x04030201u; c.r[2] = 0x02030405u;
	rc = n2a32_arm_media(&c, 0xE780F211u);
	CHECK(rc == N2A32_OK && c.r[0] == 8, "usad8 rc=%d r0=%u", rc, c.r[0]);

	/* usada8 r0, r1, r2, r3 = E7803211; 8 + 100 = 108 */
	cpu_reset(&c); c.r[1] = 0x04030201u; c.r[2] = 0x02030405u; c.r[3] = 100;
	rc = n2a32_arm_media(&c, 0xE7803211u);
	CHECK(rc == N2A32_OK && c.r[0] == 108, "usada8 rc=%d r0=%u", rc, c.r[0]);

	/* udiv r0, r1, r2 = E730F211; 100/7 = 14 */
	cpu_reset(&c); c.r[1] = 100; c.r[2] = 7;
	rc = n2a32_arm_media(&c, 0xE730F211u);
	CHECK(rc == N2A32_OK && c.r[0] == 14, "udiv rc=%d r0=%u", rc, c.r[0]);

	/* udiv by zero -> 0 */
	cpu_reset(&c); c.r[1] = 100; c.r[2] = 0;
	rc = n2a32_arm_media(&c, 0xE730F211u);
	CHECK(rc == N2A32_OK && c.r[0] == 0, "udiv0 rc=%d r0=%u", rc, c.r[0]);
}

/* sdiv needs +hwdiv: sdiv r0, r1, r2 = E710F211; -7/2 = -3 (truncating) */
static void v11_sdiv(void)
{
	struct n2a32_cpu c;
	int rc;

	cpu_reset(&c); c.r[1] = 0xfffffff9u; c.r[2] = 2;
	rc = n2a32_arm_media(&c, 0xE710F211u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffffdu, "sdiv rc=%d r0=%08x", rc, c.r[0]);

	/* INT_MIN / -1 -> INT_MIN (Arm ARM SDIV: no exception) */
	cpu_reset(&c); c.r[1] = 0x80000000u; c.r[2] = 0xffffffffu;
	rc = n2a32_arm_media(&c, 0xE710F211u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u, "sdiv ovf rc=%d r0=%08x", rc, c.r[0]);
}

/* ------------------------------------------------------------------ V12 A32 memory */
static void v12(void)
{
	struct n2a32_cpu c;
	int rc;

	/* ldr r0, [r1, #4] = E5910004 */
	cpu_reset(&c); c.r[1] = 0x2000; put32(0x2004, 0xdeadbeefu);
	rc = n2a32_arm_mem(&c, 0xE5910004u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xdeadbeefu && c.r[1] == 0x2000, "ldr imm rc=%d r0=%08x r1=%x", rc, c.r[0], c.r[1]);

	/* ldr r0, [r1], #4 = E4910004; value at 0x2000, writeback */
	cpu_reset(&c); c.r[1] = 0x2000; put32(0x2000, 0x11223344u);
	rc = n2a32_arm_mem(&c, 0xE4910004u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x11223344u && c.r[1] == 0x2004, "ldr post rc=%d r0=%08x r1=%x", rc, c.r[0], c.r[1]);

	/* ldr r0, [r1, #-4]! = E5310004; pre-indexed negative */
	cpu_reset(&c); c.r[1] = 0x2004; put32(0x2000, 0x55667788u);
	rc = n2a32_arm_mem(&c, 0xE5310004u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x55667788u && c.r[1] == 0x2000, "ldr pre rc=%d r0=%08x r1=%x", rc, c.r[0], c.r[1]);

	/* ldr r0, [r1, r2, lsl #2] = E7910102 */
	cpu_reset(&c); c.r[1] = 0x2000; c.r[2] = 2; put32(0x2008, 0xa5a5a5a5u);
	rc = n2a32_arm_mem(&c, 0xE7910102u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xa5a5a5a5u, "ldr shifted rc=%d r0=%08x", rc, c.r[0]);

	/* str r0, [r1], #4 = E4810004 */
	cpu_reset(&c); c.r[0] = 0x11223344u; c.r[1] = 0x2000;
	rc = n2a32_arm_mem(&c, 0xE4810004u);
	CHECK(rc == N2A32_OK && get32(0x2000) == 0x11223344u && c.r[1] == 0x2004, "str post rc=%d m=%08x r1=%x", rc, get32(0x2000), c.r[1]);

	/* ldrb r0, [r1, #3] = E5D10003 */
	cpu_reset(&c); c.r[1] = 0x2000; put8(0x2003, 0xabu);
	rc = n2a32_arm_mem(&c, 0xE5D10003u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xabu, "ldrb rc=%d r0=%08x", rc, c.r[0]);

	/* strb r0, [r1, #-1] = E5410001 */
	cpu_reset(&c); c.r[0] = 0x1ff; c.r[1] = 0x2000;
	rc = n2a32_arm_mem(&c, 0xE5410001u);
	CHECK(rc == N2A32_OK && ram[0x1fff] == 0xffu, "strb rc=%d m=%02x", rc, ram[0x1fff]);

	/* ldrh r0, [r1, #6] = E1D100B6 */
	cpu_reset(&c); c.r[1] = 0x2000; put16(0x2006, 0x1234);
	rc = n2a32_arm_mem(&c, 0xE1D100B6u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x1234u, "ldrh rc=%d r0=%08x", rc, c.r[0]);

	/* strh r0, [r1, #-2] = E14100B2 */
	cpu_reset(&c); c.r[0] = 0xabcdu; c.r[1] = 0x2000;
	rc = n2a32_arm_mem(&c, 0xE14100B2u);
	CHECK(rc == N2A32_OK && ram[0x1ffe] == 0xcdu && ram[0x1fff] == 0xabu, "strh rc=%d m=%02x%02x", rc, ram[0x1fff], ram[0x1ffe]);

	/* ldrsb r0, [r1, #1] = E1D100D1 */
	cpu_reset(&c); c.r[1] = 0x2000; put8(0x2001, 0x80);
	rc = n2a32_arm_mem(&c, 0xE1D100D1u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffff80u, "ldrsb rc=%d r0=%08x", rc, c.r[0]);

	/* ldrsh r0, [r1, #-2] = E15100F2 */
	cpu_reset(&c); c.r[1] = 0x2002; put16(0x2000, 0x8001);
	rc = n2a32_arm_mem(&c, 0xE15100F2u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffff8001u, "ldrsh rc=%d r0=%08x", rc, c.r[0]);

	/* ldrd r2, r3, [r1, #8] = E1C120D8 */
	cpu_reset(&c); c.r[1] = 0x2000; put32(0x2008, 0x11111111u); put32(0x200c, 0x22222222u);
	rc = n2a32_arm_mem(&c, 0xE1C120D8u);
	CHECK(rc == N2A32_OK && c.r[2] == 0x11111111u && c.r[3] == 0x22222222u, "ldrd rc=%d r2=%08x r3=%08x", rc, c.r[2], c.r[3]);

	/* strd r2, r3, [r1], #8 = E0C120F8 */
	cpu_reset(&c); c.r[1] = 0x2000; c.r[2] = 0x33333333u; c.r[3] = 0x44444444u;
	rc = n2a32_arm_mem(&c, 0xE0C120F8u);
	CHECK(rc == N2A32_OK && get32(0x2000) == 0x33333333u && get32(0x2004) == 0x44444444u && c.r[1] == 0x2008,
	      "strd rc=%d m=%08x/%08x r1=%x", rc, get32(0x2000), get32(0x2004), c.r[1]);

	/* ldm r1!, {r0, r2, r4} = E8B10015 */
	cpu_reset(&c); c.r[1] = 0x2000;
	put32(0x2000, 1); put32(0x2004, 2); put32(0x2008, 3);
	rc = n2a32_arm_mem(&c, 0xE8B10015u);
	CHECK(rc == N2A32_OK && c.r[0] == 1 && c.r[2] == 2 && c.r[4] == 3 && c.r[1] == 0x200c,
	      "ldm rc=%d r0=%u r2=%u r4=%u r1=%x", rc, c.r[0], c.r[2], c.r[4], c.r[1]);

	/* stmdb r1!, {r0, r2, r3, r4} = E921001D */
	cpu_reset(&c); c.r[1] = 0x2010; c.r[0] = 1; c.r[2] = 2; c.r[3] = 3; c.r[4] = 4;
	rc = n2a32_arm_mem(&c, 0xE921001Du);
	CHECK(rc == N2A32_OK && get32(0x2000) == 1 && get32(0x2004) == 2 && get32(0x2008) == 3 &&
	      get32(0x200c) == 4 && c.r[1] == 0x2000, "stmdb rc=%d m=%u/%u/%u/%u r1=%x", rc,
	      get32(0x2000), get32(0x2004), get32(0x2008), get32(0x200c), c.r[1]);

	/* ldm r4, {r0-r3, pc} = E894800F; pc=0x101 -> thumb, next_pc 0x100, no writeback */
	cpu_reset(&c); c.r[4] = 0x2000;
	put32(0x2000, 10); put32(0x2004, 11); put32(0x2008, 12); put32(0x200c, 13); put32(0x2010, 0x101);
	rc = n2a32_arm_mem(&c, 0xE894800Fu);
	CHECK(rc == N2A32_OK && c.r[0] == 10 && c.r[3] == 13 && c.thumb == 1 && c.next_pc == 0x100 && c.r[4] == 0x2000,
	      "ldm pc rc=%d r0=%u r3=%u thumb=%u next=%x r4=%x", rc, c.r[0], c.r[3], c.thumb, c.next_pc, c.r[4]);

	/* ldrex r0, [r1] = E1910F9F; then strex r0, r2, [r1] = E1810F92 -> success 0 */
	cpu_reset(&c); c.r[1] = 0x2000; put32(0x2000, 0x55667788u);
	rc = n2a32_arm_mem(&c, 0xE1910F9Fu);
	CHECK(rc == N2A32_OK && c.r[0] == 0x55667788u && c.excl_valid != 0, "ldrex rc=%d r0=%08x v=%u", rc, c.r[0], c.excl_valid);
	c.r[2] = 0x99aabbccu;
	rc = n2a32_arm_mem(&c, 0xE1810F92u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && get32(0x2000) == 0x99aabbccu, "strex rc=%d r0=%u m=%08x", rc, c.r[0], get32(0x2000));

	/* clrex = F57FF01F clears the monitor */
	cpu_reset(&c); c.excl_valid = 1;
	rc = n2a32_arm_mem(&c, 0xF57FF01Fu);
	CHECK(rc == N2A32_OK && c.excl_valid == 0, "clrex rc=%d v=%u", rc, c.excl_valid);

	/* pld [r1, #4] = F5D1F004: no register or memory effect */
	cpu_reset(&c); c.r[1] = 0x2000; c.r[0] = 0x1234;
	rc = n2a32_arm_mem(&c, 0xF5D1F004u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x1234 && c.r[1] == 0x2000, "pld rc=%d r0=%08x r1=%x", rc, c.r[0], c.r[1]);

	/* ldrt r0, [r1], #4 = E4B10004: load from r1+4, no writeback */
	cpu_reset(&c); c.r[1] = 0x2000; put32(0x2004, 0x01020304u);
	rc = n2a32_arm_mem(&c, 0xE4B10004u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x01020304u && c.r[1] == 0x2000, "ldrt rc=%d r0=%08x r1=%x", rc, c.r[0], c.r[1]);

	/* strt r0, [r1], #4 = E4A10004: store at r1+4, no writeback */
	cpu_reset(&c); c.r[0] = 0x0a0b0c0du; c.r[1] = 0x2000;
	rc = n2a32_arm_mem(&c, 0xE4A10004u);
	CHECK(rc == N2A32_OK && get32(0x2004) == 0x0a0b0c0du && c.r[1] == 0x2000, "strt rc=%d m=%08x r1=%x", rc, get32(0x2004), c.r[1]);

	/* b +8: EA000002 at r15=0x1008 -> 0x1010 */
	cpu_reset(&c);
	rc = n2a32_arm_misc(&c, 0xEA000002u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1010u && c.thumb == 0, "b rc=%d next=%x", rc, c.next_pc);

	/* bl +8: EB000002 -> LR=0x1004, target 0x1010 */
	cpu_reset(&c);
	rc = n2a32_arm_misc(&c, 0xEB000002u);
	CHECK(rc == N2A32_OK && c.r[14] == 0x1004u && c.next_pc == 0x1010u, "bl rc=%d lr=%x next=%x", rc, c.r[14], c.next_pc);

	/* blx +0 (H=0): FA000000 -> thumb, LR=0x1004, target 0x1008 */
	cpu_reset(&c);
	rc = n2a32_arm_misc(&c, 0xFA000000u);
	CHECK(rc == N2A32_OK && c.thumb == 1 && c.r[14] == 0x1004u && c.next_pc == 0x1008u,
	      "blx rc=%d thumb=%u lr=%x next=%x", rc, c.thumb, c.r[14], c.next_pc);

	/* blx +2 (H=1): FB000000 -> target 0x100a */
	cpu_reset(&c);
	rc = n2a32_arm_misc(&c, 0xFB000000u);
	CHECK(rc == N2A32_OK && c.thumb == 1 && c.next_pc == 0x100au, "blx H rc=%d thumb=%u next=%x", rc, c.thumb, c.next_pc);

	/* bx r3 = E12FFF13; r3=0x101 -> next 0x100 thumb */
	cpu_reset(&c); c.r[3] = 0x101;
	rc = n2a32_arm_misc(&c, 0xE12FFF13u);
	CHECK(rc == N2A32_OK && c.thumb == 1 && c.next_pc == 0x100u, "bx rc=%d thumb=%u next=%x", rc, c.thumb, c.next_pc);

	/* mrc p15, 0, r0, c13, c0, 3 = EE1D0F70 -> r0 = tpidruro */
	cpu_reset(&c); c.tpidruro = 0x12345678u;
	rc = n2a32_arm_misc(&c, 0xEE1D0F70u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x12345678u, "mrc rc=%d r0=%08x", rc, c.r[0]);
}

/* ------------------------------------------------------------------ V14 T32 16-bit */
static void v14(void)
{
	struct n2a32_cpu c;
	int rc;

	/* lsls r0, r1, #3 = 00C8; 20000001<<3 = 00000008, C=bit28 of x? carry=x>>29=1 */
	cpu_reset_t16(&c); c.r[1] = 0x20000001u;
	rc = n2a32_t16(&c, 0x00C8u);
	CHECK(rc == N2A32_OK && c.r[0] == 8 && c.cf == 1 && c.n == 0 && c.z == 0,
	      "lsls rc=%d r0=%08x c%u n%u", rc, c.r[0], c.cf, c.n);

	/* lsrs r0, r1, #3 = 08C8; 80000000>>3 = 10000000, C=0 */
	cpu_reset_t16(&c); c.r[1] = 0x80000000u;
	rc = n2a32_t16(&c, 0x08C8u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x10000000u && c.cf == 0 && c.n == 0,
	      "lsrs rc=%d r0=%08x c%u", rc, c.r[0], c.cf);

	/* asrs r0, r1, #3 = 10C8; 80000000 -> f0000000, C=bit2=0, N=1 */
	cpu_reset_t16(&c); c.r[1] = 0x80000000u;
	rc = n2a32_t16(&c, 0x10C8u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xf0000000u && c.cf == 0 && c.n == 1,
	      "asrs rc=%d r0=%08x c%u n%u", rc, c.r[0], c.cf, c.n);

	/* adds r0, r1, r2 = 1888; ffffffff+1 -> 0, C=1 Z=1 V=0 */
	cpu_reset_t16(&c); c.r[1] = 0xffffffffu; c.r[2] = 1;
	rc = n2a32_t16(&c, 0x1888u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1 && c.v == 0,
	      "adds rc=%d r0=%08x c%u z%u v%u", rc, c.r[0], c.cf, c.z, c.v);

	/* subs r0, r1, #5 = 1F48; 5-5 = 0, C=1 Z=1 */
	cpu_reset_t16(&c); c.r[1] = 5;
	rc = n2a32_t16(&c, 0x1F48u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1, "subs rc=%d r0=%u c%u z%u", rc, c.r[0], c.cf, c.z);

	/* movs r0, #0x42 = 2042 */
	cpu_reset_t16(&c);
	rc = n2a32_t16(&c, 0x2042u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x42u && c.n == 0 && c.z == 0, "movs rc=%d r0=%x", rc, c.r[0]);

	/* cmp r0, #0x42 = 2842; equal -> Z=1 C=1 */
	cpu_reset_t16(&c); c.r[0] = 0x42;
	rc = n2a32_t16(&c, 0x2842u);
	CHECK(rc == N2A32_OK && c.z == 1 && c.cf == 1, "cmp rc=%d z%u c%u", rc, c.z, c.cf);

	/* adds r0, #7 = 3007; fffffff9 + 7 -> 0, C=1 Z=1 */
	cpu_reset_t16(&c); c.r[0] = 0xfffffff9u;
	rc = n2a32_t16(&c, 0x3007u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1, "adds imm rc=%d r0=%08x c%u z%u", rc, c.r[0], c.cf, c.z);

	/* subs r0, #7 = 3807; 5-7 = fffffffe, C=0 N=1 */
	cpu_reset_t16(&c); c.r[0] = 5;
	rc = n2a32_t16(&c, 0x3807u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffffeu && c.cf == 0 && c.n == 1,
	      "subs imm rc=%d r0=%08x c%u n%u", rc, c.r[0], c.cf, c.n);

	/* ands r0, r1 = 4008; f0f0f0f0 & ff00ff00 = f000f000 */
	cpu_reset_t16(&c); c.r[0] = 0xf0f0f0f0u; c.r[1] = 0xff00ff00u;
	rc = n2a32_t16(&c, 0x4008u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xf000f000u && c.n == 1 && c.z == 0,
	      "ands rc=%d r0=%08x n%u", rc, c.r[0], c.n);

	/* eors r0, r1 = 4048 */
	cpu_reset_t16(&c); c.r[0] = 0xf0f0f0f0u; c.r[1] = 0xff00ff00u;
	rc = n2a32_t16(&c, 0x4048u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x0ff00ff0u, "eors rc=%d r0=%08x", rc, c.r[0]);

	/* lsls r0, r1 = 4088; 1<<31 = 80000000, C=bit1 of 1 = 0 */
	cpu_reset_t16(&c); c.r[0] = 1; c.r[1] = 31;
	rc = n2a32_t16(&c, 0x4088u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u && c.cf == 0 && c.n == 1,
	      "lsls reg rc=%d r0=%08x c%u", rc, c.r[0], c.cf);

	/* lsrs r0, r1 = 40C8; 80000000 >> 32 = 0, C=1 */
	cpu_reset_t16(&c); c.r[0] = 0x80000000u; c.r[1] = 32;
	rc = n2a32_t16(&c, 0x40C8u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1, "lsrs reg rc=%d r0=%u c%u", rc, c.r[0], c.cf);

	/* asrs r0, r1 = 4108; 80000000 >> 32 arithmetic = ffffffff, C=1 */
	cpu_reset_t16(&c); c.r[0] = 0x80000000u; c.r[1] = 32;
	rc = n2a32_t16(&c, 0x4108u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffffffu && c.cf == 1, "asrs reg rc=%d r0=%08x c%u", rc, c.r[0], c.cf);

	/* adcs r0, r1 = 4148; ffffffff+0+C(1) = 0, C=1 Z=1 */
	cpu_reset_t16(&c); c.r[0] = 0xffffffffu; c.r[1] = 0; c.cf = 1;
	rc = n2a32_t16(&c, 0x4148u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1, "adcs rc=%d r0=%u c%u", rc, c.r[0], c.cf);

	/* sbcs r0, r1 = 4188; 5-3-!C(0) = 1, C=1 */
	cpu_reset_t16(&c); c.r[0] = 5; c.r[1] = 3; c.cf = 0;
	rc = n2a32_t16(&c, 0x4188u);
	CHECK(rc == N2A32_OK && c.r[0] == 1 && c.cf == 1, "sbcs rc=%d r0=%u c%u", rc, c.r[0], c.cf);

	/* rors r0, r1 = 41C8; 1 ror 1 = 80000000, C=1 */
	cpu_reset_t16(&c); c.r[0] = 1; c.r[1] = 1;
	rc = n2a32_t16(&c, 0x41C8u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u && c.cf == 1, "rors rc=%d r0=%08x c%u", rc, c.r[0], c.cf);

	/* tst r0, r1 = 4208; 80000000 & 80000000 -> N=1 Z=0, C unchanged (preset 1) */
	cpu_reset_t16(&c); c.r[0] = 0x80000000u; c.r[1] = 0x80000000u; c.cf = 1;
	rc = n2a32_t16(&c, 0x4208u);
	CHECK(rc == N2A32_OK && c.n == 1 && c.z == 0 && c.cf == 1, "tst rc=%d n%u z%u c%u", rc, c.n, c.z, c.cf);

	/* rsbs r0, r1, #0 = 4248; r1=1 -> ffffffff, C=0 N=1 */
	cpu_reset_t16(&c); c.r[1] = 1;
	rc = n2a32_t16(&c, 0x4248u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffffffu && c.cf == 0 && c.n == 1,
	      "rsbs rc=%d r0=%08x c%u n%u", rc, c.r[0], c.cf, c.n);

	/* cmp r0, r1 = 4288; equal */
	cpu_reset_t16(&c); c.r[0] = 5; c.r[1] = 5;
	rc = n2a32_t16(&c, 0x4288u);
	CHECK(rc == N2A32_OK && c.z == 1 && c.cf == 1, "cmp reg rc=%d z%u c%u", rc, c.z, c.cf);

	/* cmn r0, r1 = 42C8; ffffffff + 1 -> 0, C=1 Z=1 V=0 */
	cpu_reset_t16(&c); c.r[0] = 0xffffffffu; c.r[1] = 1;
	rc = n2a32_t16(&c, 0x42C8u);
	CHECK(rc == N2A32_OK && c.cf == 1 && c.z == 1 && c.v == 0, "cmn rc=%d c%u z%u v%u", rc, c.cf, c.z, c.v);

	/* muls r0, r1 = 4348; 10000*10000 -> 0, Z=1, C unchanged (preset 1) */
	cpu_reset_t16(&c); c.r[0] = 0x10000u; c.r[1] = 0x10000u; c.cf = 1;
	rc = n2a32_t16(&c, 0x4348u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.z == 1 && c.cf == 1, "muls rc=%d r0=%u z%u c%u", rc, c.r[0], c.z, c.cf);

	/* add r8, r1 = 4488; no flags */
	cpu_reset_t16(&c); c.r[8] = 0x10; c.r[1] = 0x20; c.n = 1; c.z = 1; c.cf = 1; c.v = 1;
	rc = n2a32_t16(&c, 0x4488u);
	CHECK(rc == N2A32_OK && c.r[8] == 0x30 && c.n == 1 && c.z == 1 && c.cf == 1 && c.v == 1,
	      "add hi rc=%d r8=%x", rc, c.r[8]);

	/* mov r8, r1 = 4688; no flags */
	cpu_reset_t16(&c); c.r[8] = 0x10; c.r[1] = 0x20; c.n = 1;
	rc = n2a32_t16(&c, 0x4688u);
	CHECK(rc == N2A32_OK && c.r[8] == 0x20 && c.n == 1, "mov hi rc=%d r8=%x", rc, c.r[8]);

	/* mov pc, lr = 46F7; LR=0x101 (Thumb) -> next 0x100, thumb 1 */
	cpu_reset_t16(&c); c.r[14] = 0x101;
	rc = n2a32_t16(&c, 0x46F7u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x100u && c.thumb == 1, "mov pc thumb rc=%d next=%x thumb=%u", rc, c.next_pc, c.thumb);

	/* mov pc, lr with LR=0x100 (ARM): BXWritePC must clear Thumb (Arm ARM MOV (register) T1) */
	cpu_reset_t16(&c); c.r[14] = 0x100;
	rc = n2a32_t16(&c, 0x46F7u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x100u && c.thumb == 0, "mov pc arm rc=%d next=%x thumb=%u (want thumb=0)", rc, c.next_pc, c.thumb);

	/* add pc, r1 = 448F; PC=0x1004 + 0x200 = 0x1204, stays Thumb */
	cpu_reset_t16(&c); c.r[1] = 0x200;
	rc = n2a32_t16(&c, 0x448Fu);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1204u && c.thumb == 1, "add pc rc=%d next=%x", rc, c.next_pc);

	/* bx r3 = 4718; r3=0x101 */
	cpu_reset_t16(&c); c.r[3] = 0x101;
	rc = n2a32_t16(&c, 0x4718u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x100u && c.thumb == 1, "bx rc=%d next=%x", rc, c.next_pc);

	/* blx r3 = 4798; LR=(0x1004-2)|1 = 0x1003, next 0x100 */
	cpu_reset_t16(&c); c.r[3] = 0x101;
	rc = n2a32_t16(&c, 0x4798u);
	CHECK(rc == N2A32_OK && c.r[14] == 0x1003u && c.next_pc == 0x100u && c.thumb == 1,
	      "blx rc=%d lr=%x next=%x", rc, c.r[14], c.next_pc);

	/* ldr r0, [r1, r2] = 5888 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; c.r[2] = 4; put32(0x2004, 0x13579bdfu);
	rc = n2a32_t16(&c, 0x5888u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x13579bdfu, "ldr reg rc=%d r0=%08x", rc, c.r[0]);

	/* strh r0, [r1, r2] = 5288 */
	cpu_reset_t16(&c); c.r[0] = 0xbeefu; c.r[1] = 0x2000; c.r[2] = 2;
	rc = n2a32_t16(&c, 0x5288u);
	CHECK(rc == N2A32_OK && ram[0x2002] == 0xefu && ram[0x2003] == 0xbeu, "strh reg rc=%d", rc);

	/* ldrsb r0, [r1, r2] = 5688; byte 0x80 -> ffffff80 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; c.r[2] = 1; put8(0x2001, 0x80);
	rc = n2a32_t16(&c, 0x5688u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffff80u, "ldrsb rc=%d r0=%08x", rc, c.r[0]);

	/* ldrsh r0, [r1, r2] = 5E88; halfword 0x8001 -> ffff8001 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; c.r[2] = 2; put16(0x2002, 0x8001);
	rc = n2a32_t16(&c, 0x5E88u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffff8001u, "ldrsh rc=%d r0=%08x", rc, c.r[0]);

	/* ldr r0, [r1, #8] = 6888 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put32(0x2008, 0xcafebabeu);
	rc = n2a32_t16(&c, 0x6888u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xcafebabeu, "ldr imm rc=%d r0=%08x", rc, c.r[0]);

	/* strb r0, [r1, #3] = 70C8 */
	cpu_reset_t16(&c); c.r[0] = 0x1a2b; c.r[1] = 0x2000;
	rc = n2a32_t16(&c, 0x70C8u);
	CHECK(rc == N2A32_OK && ram[0x2003] == 0x2bu, "strb imm rc=%d m=%02x", rc, ram[0x2003]);

	/* ldrh r0, [r1, #6] = 88C8 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put16(0x2006, 0x7788);
	rc = n2a32_t16(&c, 0x88C8u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x7788u, "ldrh imm rc=%d r0=%04x", rc, c.r[0]);

	/* ldr r0, [pc, #8] = 4802; Align(0x1004,4)+8 = 0x100c */
	cpu_reset_t16(&c); put32(0x100c, 0x0badf00du);
	rc = n2a32_t16(&c, 0x4802u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x0badf00du, "ldr lit rc=%d r0=%08x", rc, c.r[0]);

	/* adr r0, +8 = A002; (0x1004&~3)+8 = 0x100c */
	cpu_reset_t16(&c);
	rc = n2a32_t16(&c, 0xA002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x100cu, "adr rc=%d r0=%x", rc, c.r[0]);

	/* add sp, #8 = B002 */
	cpu_reset_t16(&c); c.r[13] = 0x3000;
	rc = n2a32_t16(&c, 0xB002u);
	CHECK(rc == N2A32_OK && c.r[13] == 0x3008u, "add sp rc=%d sp=%x", rc, c.r[13]);

	/* sub sp, #8 = B082 */
	cpu_reset_t16(&c); c.r[13] = 0x3000;
	rc = n2a32_t16(&c, 0xB082u);
	CHECK(rc == N2A32_OK && c.r[13] == 0x2ff8u, "sub sp rc=%d sp=%x", rc, c.r[13]);

	/* cbz r0, +0 = B100 with r0=0 -> branch to r15=0x1004 */
	cpu_reset_t16(&c); c.r[0] = 0;
	rc = n2a32_t16(&c, 0xB100u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1004u, "cbz i=0 rc=%d next=%x", rc, c.next_pc);

	/* cbz r0, +0x44 (i=1) = B310: target = 0x1004 + 0x44 = 0x1048 (finding F1) */
	cpu_reset_t16(&c); c.r[0] = 0;
	rc = n2a32_t16(&c, 0xB310u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1048u, "cbz i=1 rc=%d next=%x (want 1048)", rc, c.next_pc);

	/* cbnz r1, +0x44 (i=1) = BB10 with r1!=0 -> 0x1048 (finding F1) */
	cpu_reset_t16(&c); c.r[1] = 1;
	rc = n2a32_t16(&c, 0xBB11u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1048u, "cbnz i=1 rc=%d next=%x (want 1048)", rc, c.next_pc);

	/* push {r0-r3, lr} = B50F; sp=3000 -> 2fec, stores r0..r3, lr */
	cpu_reset_t16(&c); c.r[13] = 0x3000; c.r[0] = 1; c.r[1] = 2; c.r[2] = 3; c.r[3] = 4; c.r[14] = 5;
	rc = n2a32_t16(&c, 0xB50Fu);
	CHECK(rc == N2A32_OK && c.r[13] == 0x2fecu && get32(0x2fec) == 1 && get32(0x2ff0) == 2 &&
	      get32(0x2ff4) == 3 && get32(0x2ff8) == 4 && get32(0x2ffc) == 5,
	      "push rc=%d sp=%x", rc, c.r[13]);

	/* pop {r0-r3, pc} = BD0F; sp=2fec -> regs, sp=3000, pc=0x101 -> thumb next 0x100 */
	cpu_reset_t16(&c); c.r[13] = 0x2fec;
	put32(0x2fec, 11); put32(0x2ff0, 12); put32(0x2ff4, 13); put32(0x2ff8, 14); put32(0x2ffc, 0x101);
	rc = n2a32_t16(&c, 0xBD0Fu);
	CHECK(rc == N2A32_OK && c.r[0] == 11 && c.r[3] == 14 && c.r[13] == 0x3000u &&
	      c.next_pc == 0x100u && c.thumb == 1, "pop rc=%d r0=%u r13=%x next=%x", rc, c.r[0], c.r[13], c.next_pc);

	/* rev r0, r1 = BA08 */
	cpu_reset_t16(&c); c.r[1] = 0x12345678u;
	rc = n2a32_t16(&c, 0xBA08u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x78563412u, "rev rc=%d r0=%08x", rc, c.r[0]);

	/* rev16 r0, r1 = BA48 */
	cpu_reset_t16(&c); c.r[1] = 0x12345678u;
	rc = n2a32_t16(&c, 0xBA48u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x34127856u, "rev16 rc=%d r0=%08x", rc, c.r[0]);

	/* revsh r0, r1 = BAC8; 0x8001 -> 0x0180 */
	cpu_reset_t16(&c); c.r[1] = 0x8001u;
	rc = n2a32_t16(&c, 0xBAC8u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x0180u, "revsh rc=%d r0=%08x", rc, c.r[0]);

	/* sxth r0, r1 = B208; 0x8000 -> ffff8000 */
	cpu_reset_t16(&c); c.r[1] = 0x8000u;
	rc = n2a32_t16(&c, 0xB208u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffff8000u, "sxth rc=%d r0=%08x", rc, c.r[0]);

	/* uxtb r0, r1 = B2C8; 0x1234 -> 0x34 */
	cpu_reset_t16(&c); c.r[1] = 0x1234u;
	rc = n2a32_t16(&c, 0xB2C8u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x34u, "uxtb rc=%d r0=%x", rc, c.r[0]);

	/* it eq = BF08 -> itstate 0x08 */
	cpu_reset_t16(&c);
	rc = n2a32_t16(&c, 0xBF08u);
	CHECK(rc == N2A32_OK && c.itstate == 0x08u, "it rc=%d itstate=%02x", rc, c.itstate);

	/* beq +2 = D001; Z=1 -> next 0x1006 */
	cpu_reset_t16(&c); c.z = 1;
	rc = n2a32_t16(&c, 0xD001u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1006u, "beq rc=%d next=%x", rc, c.next_pc);

	/* beq not taken; next_pc stays the core's next instruction */
	cpu_reset_t16(&c); c.z = 0;
	rc = n2a32_t16(&c, 0xD001u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1002u, "beq nt rc=%d next=%x", rc, c.next_pc);

	/* b +2 = E001 */
	cpu_reset_t16(&c);
	rc = n2a32_t16(&c, 0xE001u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1006u, "b rc=%d next=%x", rc, c.next_pc);

	/* ldmia r4!, {r0-r3} = CC0F */
	cpu_reset_t16(&c); c.r[4] = 0x2000;
	put32(0x2000, 21); put32(0x2004, 22); put32(0x2008, 23); put32(0x200c, 24);
	rc = n2a32_t16(&c, 0xCC0Fu);
	CHECK(rc == N2A32_OK && c.r[0] == 21 && c.r[3] == 24 && c.r[4] == 0x2010u,
	      "ldmia rc=%d r0=%u r3=%u r4=%x", rc, c.r[0], c.r[3], c.r[4]);

	/* stmia r4!, {r0-r3} = C40F */
	cpu_reset_t16(&c); c.r[4] = 0x2000; c.r[0] = 31; c.r[1] = 32; c.r[2] = 33; c.r[3] = 34;
	rc = n2a32_t16(&c, 0xC40Fu);
	CHECK(rc == N2A32_OK && get32(0x2000) == 31 && get32(0x200c) == 34 && c.r[4] == 0x2010u,
	      "stmia rc=%d m=%u/%u r4=%x", rc, get32(0x2000), get32(0x200c), c.r[4]);

	/* svc #1 = DF01: stop SVC, UNDEF */
	cpu_reset_t16(&c);
	rc = n2a32_t16(&c, 0xDF01u);
	CHECK(rc == N2A32_UNDEF && c.stop == N2A32_STOP_SVC && c.stop_insn == 0xDF01u,
	      "svc rc=%d stop=%d", rc, c.stop);

	/* bkpt #0 = BE00 */
	cpu_reset_t16(&c);
	rc = n2a32_t16(&c, 0xBE00u);
	CHECK(rc == N2A32_UNDEF && c.stop == N2A32_STOP_BKPT, "bkpt rc=%d stop=%d", rc, c.stop);

	/* udf #0 = DE00 */
	cpu_reset_t16(&c);
	rc = n2a32_t16(&c, 0xDE00u);
	CHECK(rc == N2A32_UNDEF && c.stop == N2A32_STOP_UNDEF, "udf rc=%d stop=%d", rc, c.stop);
}


/* ------------------------------------------------------------------ V15 T32 32-bit data processing */
static void v15(void)
{
	struct n2a32_cpu c;
	int rc;

	/* add.w r0, r1, r2 = EB010002; 7fffffff+1 = 80000000, no flags (S=0) */
	cpu_reset_t16(&c); c.r[1] = 0x7fffffffu; c.r[2] = 1; c.n = 1; c.z = 1;
	rc = n2a32_t32_dp(&c, 0xEB010002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u && c.n == 1 && c.z == 1,
	      "t32 add.w rc=%d r0=%08x n%u z%u", rc, c.r[0], c.n, c.z);

	/* adds.w r0, r1, r2 = EB110002; 7fffffff+1 -> 80000000 N=1 C=0 V=1 */
	cpu_reset_t16(&c); c.r[1] = 0x7fffffffu; c.r[2] = 1;
	rc = n2a32_t32_dp(&c, 0xEB110002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x80000000u && c.n == 1 && c.z == 0 && c.cf == 0 && c.v == 1,
	      "t32 adds.w rc=%d r0=%08x n%u c%u v%u", rc, c.r[0], c.n, c.cf, c.v);

	/* adcs.w r0, r1, r2 = EB510002; ffffffff+0+C(1) -> 0, C=1 Z=1 V=0 */
	cpu_reset_t16(&c); c.r[1] = 0xffffffffu; c.r[2] = 0; c.cf = 1;
	rc = n2a32_t32_dp(&c, 0xEB510002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.cf == 1 && c.z == 1 && c.v == 0,
	      "t32 adcs rc=%d r0=%08x c%u z%u v%u", rc, c.r[0], c.cf, c.z, c.v);

	/* lsl.w r0, r1, #3 = EA4F00C1; 20000001<<3 = 8, S=0 so C untouched */
	cpu_reset_t16(&c); c.r[1] = 0x20000001u; c.cf = 1;
	rc = n2a32_t32_dp(&c, 0xEA4F00C1u);
	CHECK(rc == N2A32_OK && c.r[0] == 8 && c.cf == 1, "t32 lsl.w rc=%d r0=%08x c%u", rc, c.r[0], c.cf);

	/* movw r0, #0x1234 = F2412034 */
	cpu_reset_t16(&c); c.r[0] = 0xffffffffu;
	rc = n2a32_t32_dp(&c, 0xF2412034u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x1234u, "t32 movw rc=%d r0=%08x", rc, c.r[0]);

	/* movt r0, #0x5678 = F2C56078; r0=1234 -> 56781234 */
	cpu_reset_t16(&c); c.r[0] = 0x1234u;
	rc = n2a32_t32_dp(&c, 0xF2C56078u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x56781234u, "t32 movt rc=%d r0=%08x", rc, c.r[0]);

	/* ubfx r0, r1, #4, #8 = F3C11007; 12345678 -> 67 */
	cpu_reset_t16(&c); c.r[1] = 0x12345678u;
	rc = n2a32_t32_dp(&c, 0xF3C11007u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x67u, "t32 ubfx rc=%d r0=%08x", rc, c.r[0]);

	/* sbfx r0, r1, #4, #8 = F3411007; 00000f80 -> fffffff8 */
	cpu_reset_t16(&c); c.r[1] = 0x00000F80u;
	rc = n2a32_t32_dp(&c, 0xF3411007u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffff8u, "t32 sbfx rc=%d r0=%08x", rc, c.r[0]);

	/* bfi r0, r1, #8, #8 = F361200F; ffffffff + 1234 -> ffff34ff */
	cpu_reset_t16(&c); c.r[0] = 0xffffffffu; c.r[1] = 0x1234u;
	rc = n2a32_t32_dp(&c, 0xF361200Fu);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffff34ffu, "t32 bfi rc=%d r0=%08x", rc, c.r[0]);

	/* ssat r0, #8, r1, lsl #4 = F3011007; 08000000<<4 = 80000000 -> -128, Q=1 */
	cpu_reset_t16(&c); c.r[1] = 0x08000000u;
	rc = n2a32_t32_dp(&c, 0xF3011007u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffff80u && c.q == 1, "t32 ssat rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* usat r0, #8, r1 = F3810008; 1234 -> ff, Q=1 */
	cpu_reset_t16(&c); c.r[1] = 0x1234u;
	rc = n2a32_t32_dp(&c, 0xF3810008u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffu && c.q == 1, "t32 usat rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* sdiv r0, r1, r2 = FB91F0F2; -7/2 = -3 */
	cpu_reset_t16(&c); c.r[1] = 0xfffffff9u; c.r[2] = 2;
	rc = n2a32_t32_dp(&c, 0xFB91F0F2u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xfffffffdu, "t32 sdiv rc=%d r0=%08x", rc, c.r[0]);

	/* udiv r0, r1, r2 = FBB1F0F2; 100/7 = 14 */
	cpu_reset_t16(&c); c.r[1] = 100; c.r[2] = 7;
	rc = n2a32_t32_dp(&c, 0xFBB1F0F2u);
	CHECK(rc == N2A32_OK && c.r[0] == 14, "t32 udiv rc=%d r0=%u", rc, c.r[0]);

	/* mul r0, r1, r2 = FB01F002; 10000*10000 = 0, no flags */
	cpu_reset_t16(&c); c.r[1] = 0x10000u; c.r[2] = 0x10000u; c.z = 1;
	rc = n2a32_t32_dp(&c, 0xFB01F002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.z == 1, "t32 mul rc=%d r0=%u z%u", rc, c.r[0], c.z);

	/* mla r0, r1, r2, r3 = FB013002; 5*7+100 = 135 */
	cpu_reset_t16(&c); c.r[1] = 5; c.r[2] = 7; c.r[3] = 100;
	rc = n2a32_t32_dp(&c, 0xFB013002u);
	CHECK(rc == N2A32_OK && c.r[0] == 135, "t32 mla rc=%d r0=%u", rc, c.r[0]);

	/* smmul r0, r1, r2 = FB51F002; -1*3 -> ffffffff */
	cpu_reset_t16(&c); c.r[1] = 0xffffffffu; c.r[2] = 3;
	rc = n2a32_t32_dp(&c, 0xFB51F002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffffffu, "t32 smmul rc=%d r0=%08x", rc, c.r[0]);

	/* clz r0, r1 = FAB1F081; 00010000 -> 15 */
	cpu_reset_t16(&c); c.r[1] = 0x00010000u;
	rc = n2a32_t32_dp(&c, 0xFAB1F081u);
	CHECK(rc == N2A32_OK && c.r[0] == 15, "t32 clz rc=%d r0=%u", rc, c.r[0]);

	/* rev.w r0, r1 = FA91F081; 12345678 -> 78563412 */
	cpu_reset_t16(&c); c.r[1] = 0x12345678u;
	rc = n2a32_t32_dp(&c, 0xFA91F081u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x78563412u, "t32 rev rc=%d r0=%08x", rc, c.r[0]);

	/* addw r0, r1, #0x123 = F2011023; 1000+123 = 1123 */
	cpu_reset_t16(&c); c.r[1] = 0x1000u;
	rc = n2a32_t32_dp(&c, 0xF2011023u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x1123u, "t32 addw rc=%d r0=%08x", rc, c.r[0]);

	/* subw r0, r1, #0x123 = F2A11023; 1000-123 = edd */
	cpu_reset_t16(&c); c.r[1] = 0x1000u;
	rc = n2a32_t32_dp(&c, 0xF2A11023u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xeddu, "t32 subw rc=%d r0=%08x", rc, c.r[0]);

	/* pkhbt r0, r1, r2, lsl #8 = EAC12002; 11223344,55667788 -> 66773344 */
	cpu_reset_t16(&c); c.r[1] = 0x11223344u; c.r[2] = 0x55667788u;
	rc = n2a32_t32_dp(&c, 0xEAC12002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x66773344u, "t32 pkhbt rc=%d r0=%08x", rc, c.r[0]);

	/* pkhtb r0, r1, r2, asr #8 = EAC12022; -> 11226677 */
	cpu_reset_t16(&c); c.r[1] = 0x11223344u; c.r[2] = 0x55667788u;
	rc = n2a32_t32_dp(&c, 0xEAC12022u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x11226677u, "t32 pkhtb rc=%d r0=%08x", rc, c.r[0]);

	/* sadd16 r0, r1, r2 = FA91F002; 0001ffff + 00010001 -> 00020000, GE=f */
	cpu_reset_t16(&c); c.r[1] = 0x0001ffffu; c.r[2] = 0x00010001u;
	rc = n2a32_t32_dp(&c, 0xFA91F002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x00020000u && c.ge == 0xfu, "t32 sadd16 rc=%d r0=%08x ge=%x", rc, c.r[0], c.ge);

	/* uadd8 r0, r1, r2 = FA81F042; ff+1 -> 0, GE bit0=1 */
	cpu_reset_t16(&c); c.r[1] = 0xff; c.r[2] = 1;
	rc = n2a32_t32_dp(&c, 0xFA81F042u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && c.ge == 1, "t32 uadd8 rc=%d r0=%08x ge=%x", rc, c.r[0], c.ge);

	/* qadd r0, r1, r2 = FA82F081; 7fffffff+1 -> 7fffffff, Q=1 */
	cpu_reset_t16(&c); c.r[1] = 0x7fffffffu; c.r[2] = 1;
	rc = n2a32_t32_dp(&c, 0xFA82F081u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x7fffffffu && c.q == 1, "t32 qadd rc=%d r0=%08x q%u", rc, c.r[0], c.q);

	/* usad8 r0, r1, r2 = FB71F002; 04030201 vs 02030405 -> 8 */
	cpu_reset_t16(&c); c.r[1] = 0x04030201u; c.r[2] = 0x02030405u;
	rc = n2a32_t32_dp(&c, 0xFB71F002u);
	CHECK(rc == N2A32_OK && c.r[0] == 8, "t32 usad8 rc=%d r0=%u", rc, c.r[0]);
}


/* ------------------------------------------------------------------ V02 core: IT, PC bias, faults, host calls */
static int hc_called;
static uint32_t hc_result;
static void hc_simple(struct n2a32_cpu *c)
{
	hc_called++;
	c->r[0] = 0x55;
}
static void hc_nested(struct n2a32_cpu *c)
{
	uint32_t args[1] = { 0 };
	hc_called++;
	hc_result = n2a32_call(c, 0x10100u, args, 0, NULL);
	c->r[0] = hc_result;
}

static void v02(void)
{
	struct n2a32_mem *m = n2a32_mem_create();
	struct n2a32_cpu c;
	enum n2a32_stop st;
	uint32_t slot;

	CHECK(m != NULL, "n2a32_mem_create failed");
	if (!m)
		return;
	CHECK(n2a32_mem_map(m, 0x10000u, 0x10000u, 1) == 0, "map 0x10000 failed");

	/* A: mov r0,#1; add r0,r0,#2; bx lr -> RETURN with r0=3, icount=3 */
	memcpy(m->base + 0x10000u, "\x01\x00\xa0\xe3\x02\x00\x80\xe2\x1e\xff\x2f\xe1", 12);
	n2a32_cpu_init(&c, m);
	c.r[15] = 0x10000u; c.r[14] = N2A32_RETURN_ADDR;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_RETURN && c.r[0] == 3 && c.icount == 3,
	      "core A st=%d r0=%u icount=%llu", st, c.r[0], (unsigned long long)c.icount);

	/* B: T32 ITE EQ; MOVEQ r0,r1; MOVNE r0,r2; BX lr, Z=1 -> r0=r1, itstate cleared */
	memcpy(m->base + 0x10000u, "\x0c\xbf\x08\x46\x10\x46\x70\x47", 8);
	n2a32_cpu_init(&c, m);
	c.r[15] = 0x10000u; c.r[14] = N2A32_RETURN_ADDR; c.thumb = 1;
	c.r[1] = 0x11; c.r[2] = 0x22; c.z = 1;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_RETURN && c.r[0] == 0x11 && c.itstate == 0,
	      "core IT taken st=%d r0=%x it=%02x", st, c.r[0], c.itstate);
	n2a32_cpu_init(&c, m);
	c.r[15] = 0x10000u; c.r[14] = N2A32_RETURN_ADDR; c.thumb = 1;
	c.r[1] = 0x11; c.r[2] = 0x22; c.z = 0;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_RETURN && c.r[0] == 0x22 && c.itstate == 0,
	      "core IT not-taken st=%d r0=%x it=%02x", st, c.r[0], c.itstate);

	/* C: A32 mov r0,pc at 0x10000 -> 0x10008 (address + 8) */
	memcpy(m->base + 0x10000u, "\x0f\x00\xa0\xe1\x1e\xff\x2f\xe1", 8);
	n2a32_cpu_init(&c, m);
	c.r[15] = 0x10000u; c.r[14] = N2A32_RETURN_ADDR;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_RETURN && c.r[0] == 0x10008u, "core PC bias st=%d r0=%x", st, c.r[0]);

	/* D: skipped conditional instruction still advances icount/PC: Z=0, moveq then mov #2 -> r0=2 */
	memcpy(m->base + 0x10000u, "\x01\x00\xa0\x03\x02\x00\xa0\xe3\x1e\xff\x2f\xe1", 12);
	n2a32_cpu_init(&c, m);
	c.r[15] = 0x10000u; c.r[14] = N2A32_RETURN_ADDR; c.z = 0;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_RETURN && c.r[0] == 2 && c.icount == 3,
	      "core skip st=%d r0=%u icount=%llu", st, c.r[0], (unsigned long long)c.icount);

	/* E: fault on an unmapped load -> STOP_FAULT with stop_addr */
	memcpy(m->base + 0x10000u, "\x00\x00\x90\xe5\x1e\xff\x2f\xe1", 8);
	n2a32_cpu_init(&c, m);
	c.r[15] = 0x10000u; c.r[14] = N2A32_RETURN_ADDR; c.r[0] = 0x50000000u;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_FAULT && c.stop_addr == 0x50000000u,
	      "core fault st=%d addr=%x", st, c.stop_addr);

	/* F: host-call slot: a host function runs and returns to LR */
	hc_called = 0;
	slot = n2a32_hostcall_register(hc_simple, "r1_spot_hc");
	CHECK(slot != 0, "hostcall register failed");
	n2a32_cpu_init(&c, m);
	c.r[15] = slot; c.r[14] = N2A32_RETURN_ADDR;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_RETURN && hc_called == 1 && c.r[0] == 0x55,
	      "core hostcall st=%d called=%d r0=%x", st, hc_called, c.r[0]);

	/* G: ilimit stops after 2 instructions of program A */
	memcpy(m->base + 0x10000u, "\x01\x00\xa0\xe3\x02\x00\x80\xe2\x1e\xff\x2f\xe1", 12);
	n2a32_cpu_init(&c, m);
	c.r[15] = 0x10000u; c.r[14] = N2A32_RETURN_ADDR; c.ilimit = 2;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_LIMIT && c.icount == 2, "core limit st=%d icount=%llu", st,
	      (unsigned long long)c.icount);

	/* H: T32 32-bit fetch: movw r0,#0x1234; bx lr */
	memcpy(m->base + 0x10000u, "\x41\xf2\x34\x20\x70\x47", 6);
	n2a32_cpu_init(&c, m);
	c.r[15] = 0x10000u; c.r[14] = N2A32_RETURN_ADDR; c.thumb = 1;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_RETURN && c.r[0] == 0x1234u, "core T32 fetch st=%d r0=%x", st, c.r[0]);

	/* I: a shim calls back into the guest (nested n2a32_call) */
	memcpy(m->base + 0x10100u, "\x07\x00\xa0\xe3\x1e\xff\x2f\xe1", 8);  /* mov r0,#7; bx lr */
	hc_called = 0; hc_result = 0;
	slot = n2a32_hostcall_register(hc_nested, "r1_spot_nested");
	CHECK(slot != 0, "nested hostcall register failed");
	n2a32_cpu_init(&c, m);
	c.r[15] = slot; c.r[14] = N2A32_RETURN_ADDR; c.r[13] = 0x18000u;
	st = n2a32_run(&c);
	CHECK(st == N2A32_STOP_RETURN && hc_called == 1 && c.r[0] == 7,
	      "core nested st=%d called=%d r0=%x", st, hc_called, c.r[0]);

	n2a32_mem_destroy(m);
}


/* ------------------------------------------------------------------ V19 NEON shifts / widening / scalar */
static void v19(void)
{
	struct n2a32_cpu c;
	int rc;

	/* vshr.u32 q0,q1,#4 = F3BC0052; d2=80000000, d3=10 -> d0=08000000, d1=1 */
	cpu_reset(&c); c.d[2].u32[0] = 0x80000000u; c.d[2].u32[1] = 0; c.d[3].u32[0] = 0x10; c.d[3].u32[1] = 0;
	rc = n2a32_neon_shift(&c, 0xF3BC0052u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0x08000000u && c.d[1].u32[0] == 1,
	      "vshr.u32 rc=%d d0=%08x d1=%08x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vshr.s16 d0,d1,#3 = F29D0011; lanes 8000,0010 -> f000,0002 */
	cpu_reset(&c); c.d[1].u16[0] = 0x8000; c.d[1].u16[1] = 0x10;
	rc = n2a32_neon_shift(&c, 0xF29D0011u);
	CHECK(rc == N2A32_OK && c.d[0].u16[0] == 0xf000u && c.d[0].u16[1] == 2 && c.d[0].u16[2] == 0,
	      "vshr.s16 rc=%d d0=%016llx", rc, (unsigned long long)c.d[0].u64);

	/* vshl.i32 q0,q1,#3 = F2A30552; d2=1,d3=80000000 -> d0=8,d1=0 */
	cpu_reset(&c); c.d[2].u32[0] = 1; c.d[3].u32[0] = 0x80000000u;
	rc = n2a32_neon_shift(&c, 0xF2A30552u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 8 && c.d[1].u32[0] == 0,
	      "vshl.i32 rc=%d d0=%x d1=%x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vshr.u64 d0,d1,#8 = F3B80091 */
	cpu_reset(&c); c.d[1].u64 = 0x0102030405060708ull;
	rc = n2a32_neon_shift(&c, 0xF3B80091u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x0001020304050607ull,
	      "vshr.u64 rc=%d d0=%016llx", rc, (unsigned long long)c.d[0].u64);

	/* vsra.u32 q0,q1,#4 = F3BC0152; q0=10,20; q1=80,100 -> 18,30 */
	cpu_reset(&c); c.d[0].u32[0] = 0x10; c.d[1].u32[0] = 0x20;
	c.d[2].u32[0] = 0x80; c.d[3].u32[0] = 0x100;
	rc = n2a32_neon_shift(&c, 0xF3BC0152u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0x18 && c.d[1].u32[0] == 0x30,
	      "vsra rc=%d d0=%x d1=%x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vrshr.u32 q0,q1,#4 = F3BC0252; q1=1f,2f -> 2,3 */
	cpu_reset(&c); c.d[2].u32[0] = 0x1f; c.d[3].u32[0] = 0x2f;
	rc = n2a32_neon_shift(&c, 0xF3BC0252u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 2 && c.d[1].u32[0] == 3,
	      "vrshr rc=%d d0=%x d1=%x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vrsra.u32 q0,q1,#4 = F3BC0352; q0=1,2; q1=1f,2f -> 3,5 */
	cpu_reset(&c); c.d[0].u32[0] = 1; c.d[1].u32[0] = 2;
	c.d[2].u32[0] = 0x1f; c.d[3].u32[0] = 0x2f;
	rc = n2a32_neon_shift(&c, 0xF3BC0352u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 3 && c.d[1].u32[0] == 5,
	      "vrsra rc=%d d0=%x d1=%x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vsri.32 q0,q1,#8 = F3B80452; q0=ffffffff,0; q1=11223344,0
	 * Arm: the top 8 bits of Vd are unchanged -> ff112233 (F10) */
	cpu_reset(&c); c.d[0].u32[0] = 0xffffffffu;
	c.d[2].u32[0] = 0x11223344u;
	rc = n2a32_neon_shift(&c, 0xF3B80452u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0xff112233u && c.d[1].u32[0] == 0,
	      "vsri rc=%d d0=%08x d1=%08x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vsli.32 q0,q1,#8 = F3A80552 (U=1); q0=ffffffff,0; q1=11223344,0
	 * Arm: the low 8 bits of Vd are unchanged -> 223344ff (F10) */
	cpu_reset(&c); c.d[0].u32[0] = 0xffffffffu;
	c.d[2].u32[0] = 0x11223344u;
	rc = n2a32_neon_shift(&c, 0xF3A80552u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0x223344ffu && c.d[1].u32[0] == 0,
	      "vsli rc=%d d0=%08x d1=%08x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vqshl.u32 q0,q1,#3 = F3A30752; q1=20000000,1 -> ffffffff,8, QC */
	cpu_reset(&c); c.d[2].u32[0] = 0x20000000u; c.d[3].u32[0] = 1;
	rc = n2a32_neon_shift(&c, 0xF3A30752u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0xffffffffu && c.d[1].u32[0] == 8 &&
	      (c.fpscr & (1u << 27)) != 0, "vqshl.u32 rc=%d d0=%08x d1=%x fpscr=%08x", rc,
	      c.d[0].u32[0], c.d[1].u32[0], c.fpscr);

	/* vqshlu.s32 q0,q1,#3 = F3A30652; q1=80000000,10000000 -> 0,80000000, QC */
	cpu_reset(&c); c.d[2].u32[0] = 0x80000000u; c.d[3].u32[0] = 0x10000000u;
	rc = n2a32_neon_shift(&c, 0xF3A30652u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0 && c.d[1].u32[0] == 0x80000000u &&
	      (c.fpscr & (1u << 27)) != 0, "vqshlu rc=%d d0=%08x d1=%08x fpscr=%08x", rc,
	      c.d[0].u32[0], c.d[1].u32[0], c.fpscr);

	/* vqshrn.u64 d0,q1,#8 = F3B80912; q1=1ff,100 -> 1,1 */
	cpu_reset(&c); c.d[2].u64 = 0x1ff; c.d[3].u64 = 0x100;
	rc = n2a32_neon_shift(&c, 0xF3B80912u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 1 && c.d[0].u32[1] == 1,
	      "vqshrn rc=%d d0=%016llx", rc, (unsigned long long)c.d[0].u64);

	/* vqrshrun.s64 d0,q1,#8 = F3B80852; q1=180,-1 -> 2,0; -1 rounds to 0, no saturation */
	cpu_reset(&c); c.d[2].u64 = 0x180; c.d[3].u64 = ~0ull;
	rc = n2a32_neon_shift(&c, 0xF3B80852u);
	CHECK(rc == N2A32_OK && c.d[0].u16[0] == 2 && c.d[0].u16[1] == 0 &&
	      (c.fpscr & (1u << 27)) == 0, "vqrshrun rc=%d d0=%016llx fpscr=%08x", rc,
	      (unsigned long long)c.d[0].u64, c.fpscr);

	/* vmovl.u16 q0,d1 = F3900A11; d1 halfword0=8001 -> q0=8001,0 */
	cpu_reset(&c); c.d[1].u16[0] = 0x8001;
	rc = n2a32_neon_shift(&c, 0xF3900A11u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0x8001 && c.d[1].u32[0] == 0,
	      "vmovl rc=%d d0=%08x d1=%08x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vaddl.u32 q0,d1,d2 = F3A10002; lane0 ffffffff + 1 -> 1_00000000 in d0, lane1 0 in d1 */
	cpu_reset(&c); c.d[1].u32[0] = 0xffffffffu; c.d[2].u32[0] = 1;
	rc = n2a32_neon_shift(&c, 0xF3A10002u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x100000000ull && c.d[1].u64 == 0,
	      "vaddl rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vmull.u32 q0,d1,d2 = F3A10C02; lane0 ffffffff * 2 -> 1fffffffe in d0, lane1 0 in d1 */
	cpu_reset(&c); c.d[1].u32[0] = 0xffffffffu; c.d[2].u32[0] = 2;
	rc = n2a32_neon_shift(&c, 0xF3A10C02u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x1fffffffeull && c.d[1].u64 == 0,
	      "vmull rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vmlal.u32 q2,d1,d2 = F3A14802; q2(d4:d5)=1,0; d1=2,d2=3 -> lane0 7 in d4, lane1 0 in d5 */
	cpu_reset(&c); c.d[4].u32[0] = 1; c.d[5].u32[0] = 0;
	c.d[1].u32[0] = 2; c.d[2].u32[0] = 3;
	rc = n2a32_neon_shift(&c, 0xF3A14802u);
	CHECK(rc == N2A32_OK && c.d[4].u64 == 7 && c.d[5].u64 == 0,
	      "vmlal rc=%d d4=%016llx d5=%016llx", rc,
	      (unsigned long long)c.d[4].u64, (unsigned long long)c.d[5].u64);

	/* vmla.i32 q0,q1,d2[1] = F3A20062; q0=1,2; q1 lane0=d2=0, lane1=d3=4; scalar d2[1]=5 -> 1,22 */
	cpu_reset(&c); c.d[0].u32[0] = 1; c.d[1].u32[0] = 2;
	c.d[2].u32[0] = 0; c.d[2].u32[1] = 5; c.d[3].u32[0] = 4;
	rc = n2a32_neon_shift(&c, 0xF3A20062u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 1 && c.d[1].u32[0] == 22,
	      "vmla scalar rc=%d d0=%08x d1=%08x", rc, c.d[0].u32[0], c.d[1].u32[0]);

	/* vqdmulh.s32 q0,q1,d2[0] = F3A20C42; q1=d2=40000000,d3=20000000; scalar d2[0]=40000000
	 * -> (2*2^60)>>32 = 20000000, (2*2^59)>>32 = 10000000 */
	cpu_reset(&c); c.d[2].u32[0] = 0x40000000u; c.d[2].u32[1] = 0; c.d[3].u32[0] = 0x20000000u;
	rc = n2a32_neon_shift(&c, 0xF3A20C42u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0x20000000u && c.d[1].u32[0] == 0x10000000u,
	      "vqdmulh scalar rc=%d d0=%08x d1=%08x", rc, c.d[0].u32[0], c.d[1].u32[0]);
}


/* ------------------------------------------------------------------ V17 VFP */
static uint32_t bits_of(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static void v17(void)
{
	struct n2a32_cpu c;
	int rc;

	/* vadd.f32 s0,s1,s2 = EE300A81; 1.5 + 2.25 = 3.75 */
	cpu_reset(&c); c.d[0].f32[1] = 1.5f; c.d[1].f32[0] = 2.25f;
	rc = n2a32_vfp(&c, 0xEE300A81u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 3.75f, "vadd.f32 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vadd.f64 d0,d1,d2 = EE310B02; 1.5 + 2.25 = 3.75 */
	cpu_reset(&c); c.d[1].f64 = 1.5; c.d[2].f64 = 2.25;
	rc = n2a32_vfp(&c, 0xEE310B02u);
	CHECK(rc == N2A32_OK && c.d[0].f64 == 3.75, "vadd.f64 rc=%d d0=%g", rc, c.d[0].f64);

	/* vsub.f32 s0,s1,s2 = EE300AC1; 5.5 - 2.25 = 3.25 */
	cpu_reset(&c); c.d[0].f32[1] = 5.5f; c.d[1].f32[0] = 2.25f;
	rc = n2a32_vfp(&c, 0xEE300AC1u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 3.25f, "vsub.f32 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vmul.f32 s0,s1,s2 = EE200A81; 1.5 * 2.0 = 3.0 */
	cpu_reset(&c); c.d[0].f32[1] = 1.5f; c.d[1].f32[0] = 2.0f;
	rc = n2a32_vfp(&c, 0xEE200A81u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 3.0f, "vmul.f32 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vdiv.f32 s0,s1,s2 = EE800A81; 3.0 / 2.0 = 1.5 */
	cpu_reset(&c); c.d[0].f32[1] = 3.0f; c.d[1].f32[0] = 2.0f;
	rc = n2a32_vfp(&c, 0xEE800A81u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 1.5f, "vdiv.f32 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vsqrt.f32 s0,s1 = EEB10AE0; sqrt(4.0) = 2.0 */
	cpu_reset(&c); c.d[0].f32[1] = 4.0f;
	rc = n2a32_vfp(&c, 0xEEB10AE0u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 2.0f, "vsqrt rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vabs.f32 s0,s1 = EEB00AE0; |-3.5| = 3.5 */
	cpu_reset(&c); c.d[0].f32[1] = -3.5f;
	rc = n2a32_vfp(&c, 0xEEB00AE0u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 3.5f, "vabs rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vneg.f32 s0,s1 = EEB10A60; -3.5 -> 3.5 */
	cpu_reset(&c); c.d[0].f32[1] = 3.5f;
	rc = n2a32_vfp(&c, 0xEEB10A60u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == -3.5f, "vneg rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vcmp.f32 s0,s1 = EEB40A60; 1.5 < 2.5 -> N=1, Z=0, C=0, V=0 */
	cpu_reset(&c); c.d[0].f32[0] = 1.5f; c.d[0].f32[1] = 2.5f;
	rc = n2a32_vfp(&c, 0xEEB40A60u);
	CHECK(rc == N2A32_OK && ((c.fpscr >> 31) & 1u) == 1 && ((c.fpscr >> 30) & 1u) == 0 &&
	      ((c.fpscr >> 29) & 1u) == 0 && ((c.fpscr >> 28) & 1u) == 0,
	      "vcmp rc=%d fpscr=%08x", rc, c.fpscr);

	/* vcvt.f64.s32 d0,s1 = EEB80BE0; s1 int32 -7 -> d0 -7.0 */
	cpu_reset(&c); c.d[0].s32[1] = -7;
	rc = n2a32_vfp(&c, 0xEEB80BE0u);
	CHECK(rc == N2A32_OK && c.d[0].f64 == -7.0, "vcvt.f64.s32 rc=%d d0=%g", rc, c.d[0].f64);

	/* vcvt.s32.f32 s0,s1 = EEBD0AE0; 3.0 -> int32 3 */
	cpu_reset(&c); c.d[0].f32[1] = 3.0f;
	rc = n2a32_vfp(&c, 0xEEBD0AE0u);
	CHECK(rc == N2A32_OK && c.d[0].s32[0] == 3, "vcvt.s32.f32 rc=%d s0=%d", rc, c.d[0].s32[0]);

	/* vcvt.f32.u32 s0,s1 = EEB80A60; 7 -> 7.0f */
	cpu_reset(&c); c.d[0].u32[1] = 7;
	rc = n2a32_vfp(&c, 0xEEB80A60u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 7.0f, "vcvt.f32.u32 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vmov.f32 s0,s1 = EEB00A60; copy 1.25 */
	cpu_reset(&c); c.d[0].f32[1] = 1.25f;
	rc = n2a32_vfp(&c, 0xEEB00A60u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 1.25f, "vmov.f32 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vmov r0,s0 = EE100A10; s0 = 1.25 -> r0 = 3fa00000 */
	cpu_reset(&c); c.d[0].f32[0] = 1.25f;
	rc = n2a32_vfp(&c, 0xEE100A10u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x3fa00000u, "vmov r0,s0 rc=%d r0=%08x", rc, c.r[0]);

	/* vmov s0,r0 = EE000A10; r0 = 40400000 -> s0 = 3.0 */
	cpu_reset(&c); c.r[0] = 0x40400000u;
	rc = n2a32_vfp(&c, 0xEE000A10u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 3.0f, "vmov s0,r0 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vmrs r0,fpscr = EEF10A10 */
	cpu_reset(&c); c.fpscr = 0x01234567u;
	rc = n2a32_vfp(&c, 0xEEF10A10u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x01234567u, "vmrs rc=%d r0=%08x", rc, c.r[0]);

	/* vmsr fpscr,r0 = EEE10A10 */
	cpu_reset(&c); c.r[0] = 0x00400000u;
	rc = n2a32_vfp(&c, 0xEEE10A10u);
	CHECK(rc == N2A32_OK && c.fpscr == 0x00400000u, "vmsr rc=%d fpscr=%08x", rc, c.fpscr);

	/* vldr s0,[r1,#4] = ED910A01; mem[0x2004]=3.0 */
	cpu_reset(&c); c.r[1] = 0x2000; put32(0x2004, 0x40400000u);
	rc = n2a32_vfp(&c, 0xED910A01u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 3.0f, "vldr rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vstr s0,[r1,#4] = ED810A01 */
	cpu_reset(&c); c.r[1] = 0x2000; c.d[0].f32[0] = 3.0f;
	rc = n2a32_vfp(&c, 0xED810A01u);
	CHECK(rc == N2A32_OK && get32(0x2004) == 0x40400000u, "vstr rc=%d m=%08x", rc, get32(0x2004));

	/* vpush {s0-s3} = ED2D0A04; sp=3000 -> 2ff0, s0..s3 stored */
	cpu_reset(&c); c.r[13] = 0x3000;
	c.d[0].f32[0] = 1.0f; c.d[0].f32[1] = 2.0f; c.d[1].f32[0] = 3.0f; c.d[1].f32[1] = 4.0f;
	rc = n2a32_vfp(&c, 0xED2D0A04u);
	CHECK(rc == N2A32_OK && c.r[13] == 0x2ff0u && get32(0x2ff0) == bits_of(1.0f) &&
	      get32(0x2ffc) == bits_of(4.0f), "vpush rc=%d sp=%x", rc, c.r[13]);

	/* vpop {s0-s3} = ECBD0A04; sp=2ff0 -> 3000, values loaded */
	cpu_reset(&c); c.r[13] = 0x2ff0;
	put32(0x2ff0, bits_of(1.0f)); put32(0x2ff4, bits_of(2.0f));
	put32(0x2ff8, bits_of(3.0f)); put32(0x2ffc, bits_of(4.0f));
	rc = n2a32_vfp(&c, 0xECBD0A04u);
	CHECK(rc == N2A32_OK && c.r[13] == 0x3000u && c.d[0].f32[0] == 1.0f && c.d[1].f32[1] == 4.0f,
	      "vpop rc=%d sp=%x s0=%g s3=%g", rc, c.r[13], (double)c.d[0].f32[0], (double)c.d[1].f32[1]);

	/* vmla.f32 s0,s1,s2 = EE000A81; 1.0 + 2.0*3.0 = 7.0 */
	cpu_reset(&c); c.d[0].f32[0] = 1.0f; c.d[0].f32[1] = 2.0f; c.d[1].f32[0] = 3.0f;
	rc = n2a32_vfp(&c, 0xEE000A81u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 7.0f, "vmla rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vmaxnm.f32 s0,s1,s2 = FE800A81; max(1.5,2.5)=2.5 */
	cpu_reset(&c); c.d[0].f32[1] = 1.5f; c.d[1].f32[0] = 2.5f;
	rc = n2a32_vfp(&c, 0xFE800A81u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 2.5f, "vmaxnm rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vminnm.f32 s0,s1,s2 = FE800AC1 */
	cpu_reset(&c); c.d[0].f32[1] = 1.5f; c.d[1].f32[0] = 2.5f;
	rc = n2a32_vfp(&c, 0xFE800AC1u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 1.5f, "vminnm rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vrintz.f32 s0,s1 = EEB60AE0; 2.75 -> 2.0 */
	cpu_reset(&c); c.d[0].f32[1] = 2.75f;
	rc = n2a32_vfp(&c, 0xEEB60AE0u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 2.0f, "vrintz rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vrinta.f32 s0,s1 = FEB80A60; 2.75 -> 3.0 */
	cpu_reset(&c); c.d[0].f32[1] = 2.75f;
	rc = n2a32_vfp(&c, 0xFEB80A60u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 3.0f, "vrinta rc=%d s0=%g", rc, (double)c.d[0].f32[0]);

	/* vseleq.f32 s0,s1,s2 = FE000A81; VSEL uses the APSR condition flags (ConditionPassed),
	 * not FPSCR.NZCV: set APSR.Z=1 while FPSCR.Z=0 -> s1=1.5; then APSR.Z=0, FPSCR.Z=1 -> s2=2.5 */
	cpu_reset(&c); c.d[0].f32[1] = 1.5f; c.d[1].f32[0] = 2.5f; c.z = 1; c.fpscr = 0;
	rc = n2a32_vfp(&c, 0xFE000A81u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 1.5f, "vseleq APSR.Z=1 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);
	cpu_reset(&c); c.d[0].f32[1] = 1.5f; c.d[1].f32[0] = 2.5f; c.z = 0; c.fpscr = 0x40000000u;
	rc = n2a32_vfp(&c, 0xFE000A81u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 2.5f, "vseleq APSR.Z=0 rc=%d s0=%g", rc, (double)c.d[0].f32[0]);
}


/* ------------------------------------------------------------------ V18 NEON three registers same length */
static void v18(void)
{
	struct n2a32_cpu c;
	int rc;

	/* vadd.i32 q0,q1,q2 = F2220844; lanes 1,2 + 3,4 -> 4,6 */
	cpu_reset(&c); c.d[2].u32[0] = 1; c.d[2].u32[1] = 2; c.d[4].u32[0] = 3; c.d[4].u32[1] = 4;
	rc = n2a32_neon_3same(&c, 0xF2220844u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 4 && c.d[0].u32[1] == 6 && c.d[1].u64 == 0,
	      "vadd.i32 rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vadd.i8 d0,d1,d2 = F2010802; 01..08 + 01 each -> 02..09 */
	cpu_reset(&c); c.d[1].u64 = 0x0807060504030201ull; c.d[2].u64 = 0x0101010101010101ull;
	rc = n2a32_neon_3same(&c, 0xF2010802u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x0908070605040302ull, "vadd.i8 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vsub.i16 q0,q1,q2 = F3120844; 5,6 - 1,2 -> 4,4 */
	cpu_reset(&c); c.d[2].u16[0] = 5; c.d[2].u16[1] = 6; c.d[4].u16[0] = 1; c.d[4].u16[1] = 2;
	rc = n2a32_neon_3same(&c, 0xF3120844u);
	CHECK(rc == N2A32_OK && c.d[0].u16[0] == 4 && c.d[0].u16[1] == 4, "vsub.i16 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vand q0,q1,q2 = F2020154 */
	cpu_reset(&c); c.d[2].u64 = 0xff00ff00ff00ff00ull; c.d[3].u64 = 0x0f0f0f0f0f0f0f0full;
	c.d[4].u64 = 0xffff0000ffff0000ull; c.d[5].u64 = 0x00ff00ff00ff00ffull;
	rc = n2a32_neon_3same(&c, 0xF2020154u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0xff000000ff000000ull && c.d[1].u64 == 0x000f000f000f000full,
	      "vand rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* veor d0,d1,d2 = F3010112 */
	cpu_reset(&c); c.d[1].u64 = 0xffffffffffffffffull; c.d[2].u64 = 0x0f0f0f0f0f0f0f0full;
	rc = n2a32_neon_3same(&c, 0xF3010112u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0xf0f0f0f0f0f0f0f0ull, "veor rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vbic q0,q1,q2 = F2120154; q1 & ~q2 */
	cpu_reset(&c); c.d[2].u64 = 0xffffffffffffffffull; c.d[3].u64 = 0;
	c.d[4].u64 = 0x00ff00ff00ff00ffull; c.d[5].u64 = 0xffffffffffffffffull;
	rc = n2a32_neon_3same(&c, 0xF2120154u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0xff00ff00ff00ff00ull && c.d[1].u64 == 0,
	      "vbic rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vbsl q0,q1,q2 = F3120154; q0 mask: all ones -> q1, zero -> q2 */
	cpu_reset(&c); c.d[0].u64 = 0xffffffffffffffffull; c.d[1].u64 = 0;
	c.d[2].u64 = 0x1111111111111111ull; c.d[3].u64 = 0x2222222222222222ull;
	c.d[4].u64 = 0xaaaaaaaaaaaaaaaaull; c.d[5].u64 = 0xbbbbbbbbbbbbbbbbull;
	rc = n2a32_neon_3same(&c, 0xF3120154u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x1111111111111111ull && c.d[1].u64 == 0xbbbbbbbbbbbbbbbbull,
	      "vbsl rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vceq.i32 q0,q1,q2 = F3220854; lanes 1,2,0,0 vs 1,3,0,0 -> ffffffff,0,ffffffff,ffffffff */
	cpu_reset(&c); c.d[2].u32[0] = 1; c.d[2].u32[1] = 2; c.d[4].u32[0] = 1; c.d[4].u32[1] = 3;
	rc = n2a32_neon_3same(&c, 0xF3220854u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0xffffffffu && c.d[0].u32[1] == 0 &&
	      c.d[1].u64 == 0xffffffffffffffffull,
	      "vceq.i32 rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vcgt.s16 q0,q1,q2 = F2120344; 1,2,3,4 vs 0,3,2,5 -> ffff,0,ffff,0 */
	cpu_reset(&c); c.d[2].u16[0] = 1; c.d[2].u16[1] = 2; c.d[2].u16[2] = 3; c.d[2].u16[3] = 4;
	c.d[4].u16[0] = 0; c.d[4].u16[1] = 3; c.d[4].u16[2] = 2; c.d[4].u16[3] = 5;
	rc = n2a32_neon_3same(&c, 0xF2120344u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x0000ffff0000ffffull, "vcgt.s16 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vmax.u8 d0,d1,d2 = F3010602; bytes 01..08 vs 08..01 -> 08,07,06,05,05,06,07,08 */
	cpu_reset(&c); c.d[1].u64 = 0x0807060504030201ull; c.d[2].u64 = 0x0102030405060708ull;
	rc = n2a32_neon_3same(&c, 0xF3010602u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x0807060505060708ull, "vmax.u8 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vmin.s32 q0,q1,q2 = F2220654; -1,2 vs 0,3 -> -1,2 */
	cpu_reset(&c); c.d[2].s32[0] = -1; c.d[2].s32[1] = 2; c.d[4].s32[0] = 0; c.d[4].s32[1] = 3;
	rc = n2a32_neon_3same(&c, 0xF2220654u);
	CHECK(rc == N2A32_OK && c.d[0].s32[0] == -1 && c.d[0].s32[1] == 2, "vmin.s32 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vhadd.u16 q0,q1,q2 = F3120044; (1+2)>>1=1, (2+3)>>1=2 */
	cpu_reset(&c); c.d[2].u16[0] = 1; c.d[2].u16[1] = 2; c.d[4].u16[0] = 2; c.d[4].u16[1] = 3;
	rc = n2a32_neon_3same(&c, 0xF3120044u);
	CHECK(rc == N2A32_OK && c.d[0].u16[0] == 1 && c.d[0].u16[1] == 2, "vhadd.u16 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vqadd.s8 d0,d1,d2 = F2010012; -128 + -1 -> -128, 127 + 1 -> 127, QC */
	cpu_reset(&c); c.d[1].u8[0] = 0x80; c.d[1].u8[1] = 0x7f; c.d[2].u8[0] = 0xff; c.d[2].u8[1] = 1;
	rc = n2a32_neon_3same(&c, 0xF2010012u);
	CHECK(rc == N2A32_OK && c.d[0].u8[0] == 0x80 && c.d[0].u8[1] == 0x7f &&
	      (c.fpscr & (1u << 27)) != 0, "vqadd.s8 rc=%d d0=%016llx fpscr=%08x", rc,
	      (unsigned long long)c.d[0].u64, c.fpscr);

	/* vmul.i32 q0,q1,q2 = F2220954; 2,3 * 4,5 -> 8,15 */
	cpu_reset(&c); c.d[2].u32[0] = 2; c.d[2].u32[1] = 3; c.d[4].u32[0] = 4; c.d[4].u32[1] = 5;
	rc = n2a32_neon_3same(&c, 0xF2220954u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 8 && c.d[0].u32[1] == 15, "vmul.i32 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vmla.i32 q0,q1,q2 = F2220944; 1,2 + 2,3*4,5 -> 9,17 */
	cpu_reset(&c); c.d[0].u32[0] = 1; c.d[0].u32[1] = 2;
	c.d[2].u32[0] = 2; c.d[2].u32[1] = 3; c.d[4].u32[0] = 4; c.d[4].u32[1] = 5;
	rc = n2a32_neon_3same(&c, 0xF2220944u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 9 && c.d[0].u32[1] == 17, "vmla.i32 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vabd.s8 d0,d1,d2 = F2010702; |01-08| etc -> 07,05,03,01,01,03,05,07 */
	cpu_reset(&c); c.d[1].u64 = 0x0807060504030201ull; c.d[2].u64 = 0x0102030405060708ull;
	rc = n2a32_neon_3same(&c, 0xF2010702u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x0705030101030507ull, "vabd.s8 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vrhadd.u16 q0,q1,q2 = F3120144; (1+2+1)>>1=2, (2+3+1)>>1=3 */
	cpu_reset(&c); c.d[2].u16[0] = 1; c.d[2].u16[1] = 2; c.d[4].u16[0] = 2; c.d[4].u16[1] = 3;
	rc = n2a32_neon_3same(&c, 0xF3120144u);
	CHECK(rc == N2A32_OK && c.d[0].u16[0] == 2 && c.d[0].u16[1] == 3, "vrhadd.u16 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vtst.32 q0,q1,q2 = F2220854; 1&1 != 0 -> all ones, 2&4 == 0 -> 0 */
	cpu_reset(&c); c.d[2].u32[0] = 1; c.d[2].u32[1] = 2; c.d[4].u32[0] = 1; c.d[4].u32[1] = 4;
	rc = n2a32_neon_3same(&c, 0xF2220854u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0xffffffffu && c.d[0].u32[1] == 0, "vtst.32 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vadd.f32 q0,q1,q2 = F2020D44; 1.5+2.25 = 3.75 per lane */
	cpu_reset(&c); c.d[2].f32[0] = 1.5f; c.d[2].f32[1] = 2.5f; c.d[4].f32[0] = 2.25f; c.d[4].f32[1] = 3.5f;
	rc = n2a32_neon_3same(&c, 0xF2020D44u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 3.75f && c.d[0].f32[1] == 6.0f,
	      "vadd.f32 rc=%d s0=%g s1=%g", rc, (double)c.d[0].f32[0], (double)c.d[0].f32[1]);

	/* vceq.f32 q0,q1,q2 = F2020E44; 1.5,2.5 vs 1.5,3.5 -> all ones, 0 */
	cpu_reset(&c); c.d[2].f32[0] = 1.5f; c.d[2].f32[1] = 2.5f; c.d[4].f32[0] = 1.5f; c.d[4].f32[1] = 3.5f;
	rc = n2a32_neon_3same(&c, 0xF2020E44u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0xffffffffu && c.d[0].u32[1] == 0,
	      "vceq.f32 rc=%d d0=%016llx", rc, (unsigned long long)c.d[0].u64);

	/* vmax.f32 q0,q1,q2 = F2020F44; 1.5 vs 2.5 -> 2.5 */
	cpu_reset(&c); c.d[2].f32[0] = 1.5f; c.d[2].f32[1] = 2.5f; c.d[4].f32[0] = 2.5f; c.d[4].f32[1] = 1.5f;
	rc = n2a32_neon_3same(&c, 0xF2020F44u);
	CHECK(rc == N2A32_OK && c.d[0].f32[0] == 2.5f && c.d[0].f32[1] == 2.5f,
	      "vmax.f32 rc=%d s0=%g s1=%g", rc, (double)c.d[0].f32[0], (double)c.d[0].f32[1]);

	/* vqdmulh.s32 q0,q1,q2 = F2220B44; (2*2^30*2)>>32 = 1, (2*2^29*2)>>32 = 0 */
	cpu_reset(&c); c.d[2].u32[0] = 0x40000000u; c.d[2].u32[1] = 0x20000000u;
	c.d[4].u32[0] = 2; c.d[4].u32[1] = 2;
	rc = n2a32_neon_3same(&c, 0xF2220B44u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 1 && c.d[0].u32[1] == 0, "vqdmulh.s32 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);
}


/* ------------------------------------------------------------------ V16 T32 32-bit memory/branches */
static void v16(void)
{
	struct n2a32_cpu c;
	int rc;

	/* ldr.w r0,[r1,#4] = F8D10004 (imm12 always adds) */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put32(0x2004, 0xdeadbeefu);
	rc = n2a32_t32_mem(&c, 0xF8D10004u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xdeadbeefu, "t32 ldr.w rc=%d r0=%08x", rc, c.r[0]);

	/* str.w r0,[r1,#4] = F8C10004 */
	cpu_reset_t16(&c); c.r[0] = 0x11223344u; c.r[1] = 0x2000;
	rc = n2a32_t32_mem(&c, 0xF8C10004u);
	CHECK(rc == N2A32_OK && get32(0x2004) == 0x11223344u, "t32 str.w rc=%d m=%08x", rc, get32(0x2004));

	/* ldrb.w r0,[r1,#1] = F8910001 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put8(0x2001, 0xab);
	rc = n2a32_t32_mem(&c, 0xF8910001u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xabu, "t32 ldrb.w rc=%d r0=%08x", rc, c.r[0]);

	/* strh.w r0,[r1,#2] = F8A10002 */
	cpu_reset_t16(&c); c.r[0] = 0xbeefu; c.r[1] = 0x2000;
	rc = n2a32_t32_mem(&c, 0xF8A10002u);
	CHECK(rc == N2A32_OK && ram[0x2002] == 0xefu && ram[0x2003] == 0xbeu, "t32 strh.w rc=%d", rc);

	/* ldrsb.w r0,[r1,#1] = F9910001; 0x80 -> ffffff80 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put8(0x2001, 0x80);
	rc = n2a32_t32_mem(&c, 0xF9910001u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffffff80u, "t32 ldrsb.w rc=%d r0=%08x", rc, c.r[0]);

	/* ldrsh.w r0,[r1,#2] = F9B10002; 0x8001 -> ffff8001 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put16(0x2002, 0x8001);
	rc = n2a32_t32_mem(&c, 0xF9B10002u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xffff8001u, "t32 ldrsh.w rc=%d r0=%08x", rc, c.r[0]);

	/* ldr.w r0,[pc,#4] = F8DF0004; base Align(PC,4)+4 = 0x1008 */
	cpu_reset_t16(&c); put32(0x1008, 0xcafebabeu);
	rc = n2a32_t32_mem(&c, 0xF8DF0004u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xcafebabeu, "t32 ldr literal rc=%d r0=%08x", rc, c.r[0]);

	/* ldr.w r0,[r1,r2,lsl #2] = F8510022 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; c.r[2] = 2; put32(0x2008, 0xa5a5a5a5u);
	rc = n2a32_t32_mem(&c, 0xF8510022u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xa5a5a5a5u, "t32 ldr reg rc=%d r0=%08x", rc, c.r[0]);

	/* ldr.w r0,[r1],#4 = F8510B04 (post-index, writeback) */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put32(0x2000, 0x55667788u);
	rc = n2a32_t32_mem(&c, 0xF8510B04u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x55667788u && c.r[1] == 0x2004u,
	      "t32 ldr post rc=%d r0=%08x r1=%x", rc, c.r[0], c.r[1]);

	/* ldr.w r0,[r1,#4]! = F8510F04 (pre-index, writeback) */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put32(0x2004, 0x99aabbccu);
	rc = n2a32_t32_mem(&c, 0xF8510F04u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x99aabbccu && c.r[1] == 0x2004u,
	      "t32 ldr pre rc=%d r0=%08x r1=%x", rc, c.r[0], c.r[1]);

	/* ldr.w r0,[r1,#-4] = F8510C04 (imm8, U=0, P=1,W=0) */
	cpu_reset_t16(&c); c.r[1] = 0x2004; put32(0x2000, 0x0badf00du);
	rc = n2a32_t32_mem(&c, 0xF8510C04u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x0badf00du && c.r[1] == 0x2004u,
	      "t32 ldr neg rc=%d r0=%08x r1=%x", rc, c.r[0], c.r[1]);

	/* ldm.w r1!,{r0,r2,r4} = E8B10015 */
	cpu_reset_t16(&c); c.r[1] = 0x2000;
	put32(0x2000, 1); put32(0x2004, 2); put32(0x2008, 3);
	rc = n2a32_t32_mem(&c, 0xE8B10015u);
	CHECK(rc == N2A32_OK && c.r[0] == 1 && c.r[2] == 2 && c.r[4] == 3 && c.r[1] == 0x200cu,
	      "t32 ldm rc=%d r0=%u r2=%u r4=%u r1=%x", rc, c.r[0], c.r[2], c.r[4], c.r[1]);

	/* stmdb.w r1!,{r0,r2-r4} = E921001D */
	cpu_reset_t16(&c); c.r[1] = 0x2010; c.r[0] = 1; c.r[2] = 2; c.r[3] = 3; c.r[4] = 4;
	rc = n2a32_t32_mem(&c, 0xE921001Du);
	CHECK(rc == N2A32_OK && get32(0x2000) == 1 && get32(0x2004) == 2 && get32(0x2008) == 3 &&
	      get32(0x200c) == 4 && c.r[1] == 0x2000u, "t32 stmdb rc=%d r1=%x", rc, c.r[1]);

	/* ldrd r2,r3,[r1,#8] = E9D12302 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put32(0x2008, 0x11111111u); put32(0x200c, 0x22222222u);
	rc = n2a32_t32_mem(&c, 0xE9D12302u);
	CHECK(rc == N2A32_OK && c.r[2] == 0x11111111u && c.r[3] == 0x22222222u,
	      "t32 ldrd rc=%d r2=%08x r3=%08x", rc, c.r[2], c.r[3]);

	/* strd r2,r3,[r1],#8 = E8E12302 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; c.r[2] = 0x33333333u; c.r[3] = 0x44444444u;
	rc = n2a32_t32_mem(&c, 0xE8E12302u);
	CHECK(rc == N2A32_OK && get32(0x2000) == 0x33333333u && get32(0x2004) == 0x44444444u &&
	      c.r[1] == 0x2008u, "t32 strd rc=%d r1=%x", rc, c.r[1]);

	/* ldrex r0,[r1] = E8510F00; then strex r0,r2,[r1] = E8412000 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; put32(0x2000, 0x55667788u);
	rc = n2a32_t32_mem(&c, 0xE8510F00u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x55667788u && c.excl_valid != 0,
	      "t32 ldrex rc=%d r0=%08x v=%u", rc, c.r[0], c.excl_valid);
	c.r[2] = 0x99aabbccu;
	rc = n2a32_t32_mem(&c, 0xE8412000u);
	CHECK(rc == N2A32_OK && c.r[0] == 0 && get32(0x2000) == 0x99aabbccu,
	      "t32 strex rc=%d r0=%u m=%08x", rc, c.r[0], get32(0x2000));

	/* tbb [pc,r1] = E8DFF001; PC=0x1004, r1=0, table byte 2 -> target 0x1008 */
	cpu_reset_t16(&c); c.r[1] = 0; put8(0x1004, 2);
	rc = n2a32_t32_mem(&c, 0xE8DFF001u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x1008u, "t32 tbb rc=%d next=%x", rc, c.next_pc);

	/* tbh [r1,r2,lsl #1] = E8D1F012; r1=0x2000, r2=1 -> halfword at 0x2002 = 3 -> 0x1004+6 */
	cpu_reset_t16(&c); c.r[1] = 0x2000; c.r[2] = 1; put16(0x2002, 3);
	rc = n2a32_t32_mem(&c, 0xE8D1F012u);
	CHECK(rc == N2A32_OK && c.next_pc == 0x100au, "t32 tbh rc=%d next=%x", rc, c.next_pc);

	/* mrs r0,apsr = F3EF8000; msr apsr_nzcvq,r0 = F3808800 */
	cpu_reset_t16(&c); c.n = 1; c.z = 0; c.cf = 1; c.v = 0; c.q = 1;
	rc = n2a32_t32_misc(&c, 0xF3EF8000u);
	CHECK(rc == N2A32_OK && c.r[0] == 0xa8000000u, "t32 mrs rc=%d r0=%08x", rc, c.r[0]);
	c.r[0] = 0x50000000u;
	rc = n2a32_t32_misc(&c, 0xF3808800u);
	CHECK(rc == N2A32_OK && c.n == 0 && c.z == 1 && c.cf == 0 && c.v == 1 && c.q == 0,
	      "t32 msr rc=%d n%u z%u c%u v%u q%u", rc, c.n, c.z, c.cf, c.v, c.q);

	/* nop.w = F3AF8000, dmb = F3BF8F5F, dsb = F3BF8F4F, isb = F3BF8F6F: all no-ops */
	cpu_reset_t16(&c); c.r[0] = 0x1234;
	rc = n2a32_t32_misc(&c, 0xF3AF8000u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x1234, "t32 nop.w rc=%d", rc);
	rc = n2a32_t32_misc(&c, 0xF3BF8F5Fu);
	CHECK(rc == N2A32_OK, "t32 dmb rc=%d", rc);
	rc = n2a32_t32_misc(&c, 0xF3BF8F4Fu);
	CHECK(rc == N2A32_OK, "t32 dsb rc=%d", rc);
	rc = n2a32_t32_misc(&c, 0xF3BF8F6Fu);
	CHECK(rc == N2A32_OK, "t32 isb rc=%d", rc);

	/* mrc p15,0,r0,c13,c0,3 = EE1D0F70 -> tpidruro */
	cpu_reset_t16(&c); c.tpidruro = 0x12345678u;
	rc = n2a32_t32_misc(&c, 0xEE1D0F70u);
	CHECK(rc == N2A32_OK && c.r[0] == 0x12345678u, "t32 mrc rc=%d r0=%08x", rc, c.r[0]);
}


/* ------------------------------------------------------------------ V20 NEON misc / structure load-store */
static void v20(void)
{
	struct n2a32_cpu c;
	int rc;

	/* vdup.32 q0,r0 = EEA00B10; r0=12345678 -> all lanes (VDUP from core is V17) */
	cpu_reset(&c); c.r[0] = 0x12345678u;
	rc = n2a32_vfp(&c, 0xEEA00B10u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x1234567812345678ull && c.d[1].u64 == 0x1234567812345678ull,
	      "vdup.32 rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vmov.32 d0[1],r0 = EE200B10; r0=12345678 -> d0 lane1 (VMOV lane is V17) */
	cpu_reset(&c); c.r[0] = 0x12345678u; c.d[0].u64 = 0xaaaaaaaaaaaaaaaaull;
	rc = n2a32_vfp(&c, 0xEE200B10u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x12345678aaaaaaaaull, "vmov lane rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vswp q0,q1 = F3B20042 */
	cpu_reset(&c); c.d[0].u64 = 1; c.d[1].u64 = 2; c.d[2].u64 = 3; c.d[3].u64 = 4;
	rc = n2a32_neon_misc(&c, 0xF3B20042u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 3 && c.d[1].u64 == 4 && c.d[2].u64 == 1 && c.d[3].u64 == 2,
	      "vswp rc=%d d0=%llu d1=%llu d2=%llu d3=%llu", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64,
	      (unsigned long long)c.d[2].u64, (unsigned long long)c.d[3].u64);

	/* vtrn.32 q0,q1 = F3BA00C2; q0=11111111,22222222; q1=33333333,44444444 */
	cpu_reset(&c); c.d[0].u32[0] = 0x11111111u; c.d[0].u32[1] = 0x22222222u;
	c.d[1].u32[0] = 0x22222222u; c.d[1].u32[1] = 0x22222222u;
	c.d[2].u32[0] = 0x33333333u; c.d[2].u32[1] = 0x33333333u;
	c.d[3].u32[0] = 0x44444444u; c.d[3].u32[1] = 0x44444444u;
	rc = n2a32_neon_misc(&c, 0xF3BA00C2u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0x11111111u && c.d[0].u32[1] == 0x33333333u &&
	      c.d[1].u32[0] == 0x22222222u && c.d[1].u32[1] == 0x44444444u,
	      "vtrn.32 rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vuzp.8 d0,d1 = F3B20101 */
	cpu_reset(&c); c.d[0].u64 = 0x0706050403020100ull; c.d[1].u64 = 0x0f0e0d0c0b0a0908ull;
	rc = n2a32_neon_misc(&c, 0xF3B20101u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x0e0c0a0806040200ull && c.d[1].u64 == 0x0f0d0b0907050301ull,
	      "vuzp.8 rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vzip.16 d0,d1 = F3B60181 */
	cpu_reset(&c); c.d[0].u64 = 0x4444333322221111ull; c.d[1].u64 = 0x8888777766665555ull;
	rc = n2a32_neon_misc(&c, 0xF3B60181u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x6666222255551111ull && c.d[1].u64 == 0x8888444477773333ull,
	      "vzip.16 rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vclz.i32 q0,q1 = F3B804C2; lanes 1,80000000 -> 31,0 */
	cpu_reset(&c); c.d[2].u32[0] = 1; c.d[2].u32[1] = 0x80000000u;
	rc = n2a32_neon_misc(&c, 0xF3B804C2u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 31 && c.d[0].u32[1] == 0,
	      "vclz rc=%d d0=%016llx", rc, (unsigned long long)c.d[0].u64);

	/* vcnt.8 d0,d1 = F3B00501; d1 low byte ff -> 8 */
	cpu_reset(&c); c.d[1].u64 = 0xff;
	rc = n2a32_neon_misc(&c, 0xF3B00501u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 8, "vcnt rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vrev64.8 q0,q1 = F3B00042 */
	cpu_reset(&c); c.d[2].u64 = 0x0706050403020100ull; c.d[3].u64 = 0x0f0e0d0c0b0a0908ull;
	rc = n2a32_neon_misc(&c, 0xF3B00042u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x0001020304050607ull && c.d[1].u64 == 0x08090a0b0c0d0e0full,
	      "vrev64 rc=%d d0=%016llx d1=%016llx", rc,
	      (unsigned long long)c.d[0].u64, (unsigned long long)c.d[1].u64);

	/* vpadal.u8 d0,d1 = F3B00681; bytes 01..08 -> 3,7,11,15 */
	cpu_reset(&c); c.d[1].u64 = 0x0807060504030201ull;
	rc = n2a32_neon_misc(&c, 0xF3B00681u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x000f000b00070003ull, "vpadal rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vld1.32 {d0},[r0] = F420078F; load 8 bytes */
	cpu_reset(&c); c.r[0] = 0x2000; put32(0x2000, 0x11223344u); put32(0x2004, 0x55667788u);
	rc = n2a32_neon_ldst(&c, 0xF420078Fu);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x5566778811223344ull, "vld1.32 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vst1.32 {d0},[r0] = F400078F */
	cpu_reset(&c); c.r[0] = 0x2000; c.d[0].u64 = 0x5566778811223344ull;
	rc = n2a32_neon_ldst(&c, 0xF400078Fu);
	CHECK(rc == N2A32_OK && get32(0x2000) == 0x11223344u && get32(0x2004) == 0x55667788u,
	      "vst1.32 rc=%d m=%08x/%08x", rc, get32(0x2000), get32(0x2004));

	/* vld1.32 {d0[1]},[r0] = F4A0088F; load into lane 1 */
	cpu_reset(&c); c.r[0] = 0x2000; put32(0x2000, 0xdeadbeefu); c.d[0].u64 = 0xaaaaaaaaaaaaaaaaull;
	rc = n2a32_neon_ldst(&c, 0xF4A0088Fu);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0xdeadbeefaaaaaaaaull, "vld1 lane rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vst1.16 {d0[0]},[r0] = F480040F; store low 2 bytes */
	cpu_reset(&c); c.r[0] = 0x2000; c.d[0].u64 = 0x000000000000abcdull;
	rc = n2a32_neon_ldst(&c, 0xF480040Fu);
	CHECK(rc == N2A32_OK && ram[0x2000] == 0xcdu && ram[0x2001] == 0xabu, "vst1 lane rc=%d m=%02x%02x", rc,
	      ram[0x2001], ram[0x2000]);

	/* vmovn.i32 d0,q1 = F3B60202; q1 32-bit lanes 11223344,0,55667788,0 -> 3344,0,7788,0 */
	cpu_reset(&c); c.d[2].u32[0] = 0x11223344u; c.d[3].u32[0] = 0x55667788u;
	rc = n2a32_neon_misc(&c, 0xF3B60202u);
	CHECK(rc == N2A32_OK && c.d[0].u64 == 0x0000778800003344ull, "vmovn.i32 rc=%d d0=%016llx", rc,
	      (unsigned long long)c.d[0].u64);

	/* vqmovn.s32 d0,q1 = F3B60282; -1,0x7fffffff -> ffff,7fff */
	cpu_reset(&c); c.d[2].s32[0] = -1; c.d[3].s32[0] = 0x7fffffff;
	rc = n2a32_neon_misc(&c, 0xF3B60282u);
	CHECK(rc == N2A32_OK && c.d[0].u32[0] == 0xffffu && c.d[0].u32[1] == 0x7fffu,
	      "vqmovn.s32 rc=%d d0=%016llx", rc, (unsigned long long)c.d[0].u64);
}

int main(void)
{
	v10();
	v11();
	v11_sdiv();
	v12();
	v14();
	v15();
	v02();
	v19();
	v17();
	v18();
	v16();
	v20();
	printf("R1-SPOT %s %d/%d checks\n", fails ? "FAIL" : "PASS", checks - fails, checks);
	return fails ? 1 : 0;
}
