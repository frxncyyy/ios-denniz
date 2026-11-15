/* Host test of 0670 nord2_mdp.inc: register model of SPM / topckgen / infracfg / mdpsys / GCE.
 * Fails on: any mdpsys read while the domain is off, bus-protected or mdp_sel gated; any engine/mutex read with its own gate
 * closed or the APB gate closed; any larb/M4U read with SMI clocks closed; any GCE read with its infracfg gate closed;
 * any write at level 1; any write at level 2 other than mdpsys CG SET/CLR; any gate left different from found. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef uintptr_t vm_offset_t;
#define N2MDP_HOST 1

enum { R_SPM, R_TOP, R_INFRA, R_SYS, R_GCED, R_GCEM, R_N };
static const uint32_t base_pa[R_N] = { 0x10006000u, 0x10000000u, 0x10001000u, 0x1f000000u, 0x10228000u, 0x10318000u };
static const uint32_t size_b[R_N] = { 0x1000, 0x1000, 0x1000, 0x2e000, 0x1000, 0x1000 };
static uint32_t *mem[R_N];
static int fails, level_now, writes;

vm_offset_t n2mdp_host_map(uint32_t pa, uint32_t bytes)
{
	for (int i = 0; i < R_N; i++) {
		if (pa == base_pa[i] && bytes <= size_b[i]) { return (vm_offset_t)mem[i]; }
	}
	printf("FAIL map %#x\n", pa); fails++; return 0;
}
static int region(vm_offset_t b) { for (int i = 0; i < R_N; i++) { if (b == (vm_offset_t)mem[i]) { return i; } } return -1; }
#define REG(r, off) mem[r][(off) / 4]

static bool mdp_ok(void)
{
	uint32_t s = REG(R_SPM, 0x16c) & REG(R_SPM, 0x170);
	bool prot = (REG(R_INFRA, 0x228) & ((1u << 10) | (1u << 23))) || (REG(R_INFRA, 0x2ec) & 0xf0cc03fcu) ||
	    (REG(R_INFRA, 0xdd8) & 0xbd7u);
	return (s & (1u << 19)) && !prot && !(REG(R_TOP, 0x20) & (1u << 15));
}
static void check_sys_read(unsigned off)
{
	if (!mdp_ok()) { printf("FAIL mdpsys read %#x with the domain unusable\n", off); fails++; return; }
	uint32_t cg0 = REG(R_SYS, 0x100), cg1 = REG(R_SYS, 0x110);
	if (off < 0x1000) { return; }                                 /* config: needs only the domain */
	if (cg0 & (1u << 31)) { printf("FAIL read %#x with APB gated\n", off); fails++; return; }
	static const struct { uint32_t off; int bit; } e[] = { {0x1000,15}, {0x6000,0},{0x7000,16},{0x8000,8},{0x9000,24},
		{0x12000,4},{0x13000,20},{0x14000,10},{0x15000,26},{0x16000,7},{0x17000,23},{0x18000,14},{0x19000,30} };
	for (unsigned i = 0; i < sizeof e / sizeof e[0]; i++) {
		if ((off & ~0xfffu) == e[i].off && (cg0 & (1u << e[i].bit))) { printf("FAIL engine read %#x gated\n", off); fails++; }
	}
	if (((off & ~0xfffu) == 0x2000 || (off & ~0xfffu) == 0x3000 || (off & ~0xfffu) == 0x4000 || (off & ~0xfffu) == 0x27000) &&
	    (cg1 & 0x1112u)) { printf("FAIL smi/larb/m4u read %#x with SMI clocks gated\n", off); fails++; }
}
static uint32_t ni_rd(vm_offset_t b, unsigned off)
{
	int r = region(b);
	if (r < 0) { printf("FAIL read unmapped\n"); fails++; return 0; }
	if (r == R_SYS) { check_sys_read(off); }
	if (r == R_GCED && (REG(R_INFRA, 0x90) & (1u << 8))) { printf("FAIL GCE-D read gated\n"); fails++; }
	if (r == R_GCEM && (REG(R_INFRA, 0x90) & (1u << 9))) { printf("FAIL GCE-M read gated\n"); fails++; }
	return REG(r, off);
}
static void ni_wr(vm_offset_t b, unsigned off, uint32_t v)
{
	int r = region(b);
	writes++;
	if (level_now < 2) { printf("FAIL write at level %d\n", level_now); fails++; return; }
	if (r != R_SYS || !(off == 0x104 || off == 0x108 || off == 0x114 || off == 0x118)) { printf("FAIL write r%d %#x\n", r, off); fails++; return; }
	if (!mdp_ok()) { printf("FAIL CG write domain unusable\n"); fails++; }
	unsigned con = off & ~0xfu;
	if ((off & 0xf) == 4) { REG(R_SYS, con) |= v; } else { REG(R_SYS, con) &= ~v; }
}
#include "../piece-0670/patched-sources-0670/bsd/dev/nord2_mdp.inc"

static void setup(bool power, bool prot, bool mux, uint32_t cg0, uint32_t cg1, uint32_t ifr)
{
	for (int i = 0; i < R_N; i++) { memset(mem[i], 0, size_b[i]); }
	memset(&n2mdp, 0, sizeof n2mdp);
	REG(R_SPM, 0x16c) = REG(R_SPM, 0x170) = 0x10500e03u | (power ? (1u << 19) : 0);
	REG(R_INFRA, 0x228) = prot ? (1u << 10) : 0;
	REG(R_TOP, 0x20) = 0x00000701u | (mux ? (1u << 15) : 0);
	REG(R_SYS, 0x100) = cg0; REG(R_SYS, 0x110) = cg1; REG(R_SYS, 0x120) = 0xf;
	REG(R_INFRA, 0x90) = ifr;
	REG(R_SYS, 0x6000) = 0x1; REG(R_SYS, 0x16000) = 0x0;
}
static uint32_t run(int level) { level_now = level; writes = 0; return n2mdp_probe(level); }

int main(void)
{
	for (int i = 0; i < R_N; i++) { mem[i] = calloc(1, size_b[i]); }
	uint32_t st;
	/* 1: domain off, both levels: no mdpsys access, no write */
	setup(false, false, false, 0, 0, 0x300); st = run(1); if (st & N2MDP_S_POWER) { printf("FAIL power bit\n"); fails++; }
	st = run(2); if (writes) { printf("FAIL writes with domain off\n"); fails++; }
	/* 2: protection set / mux gated */
	setup(true, true, false, 0, 0, 0x300); st = run(2); if (writes || !(st & N2MDP_S_PROT)) { printf("FAIL prot\n"); fails++; }
	setup(true, false, true, 0, 0, 0x300); st = run(2); if (writes || !(st & N2MDP_S_MUXGATE)) { printf("FAIL mux\n"); fails++; }
	/* 3: all gated (reset state) level 1 */
	setup(true, false, false, 0xffffffffu, 0xffffu, 0x300); st = run(1); if (writes || (st & N2MDP_S_ENGREAD)) { printf("FAIL l1 gated\n"); fails++; }
	/* 4: all gated, level 2: opens 5+SMI, reads, restores */
	setup(true, false, false, 0xffffffffu, 0xffffu, 0x300); st = run(2);
	if (!(st & N2MDP_S_LVL2) || !(st & N2MDP_S_RESTORED) || !(st & N2MDP_S_ENGREAD)) { printf("FAIL l2 st %#x\n", st); fails++; }
	if (REG(R_SYS, 0x100) != 0xffffffffu || REG(R_SYS, 0x110) != 0xffffu) { printf("FAIL l2 not restored\n"); fails++; }
	/* 5: partially open as found (bootloader left some), GCE open */
	setup(true, false, false, 0x7fff7f7eu, 0x0u, 0x0); st = run(1);
	if (!(st & N2MDP_S_APB) || !(st & N2MDP_S_SMI) || !(st & N2MDP_S_GCEM_ON)) { printf("FAIL partial st %#x\n", st); fails++; }
	st = run(2); if (REG(R_SYS, 0x100) != 0x7fff7f7eu || !(st & N2MDP_S_RESTORED)) { printf("FAIL partial restore\n"); fails++; }
	/* 6: everything open: level 2 writes nothing */
	setup(true, false, false, 0, 0, 0); st = run(2); if (writes) { printf("FAIL wrote with all open (%d)\n", writes); fails++; }
	/* 7: random sweep of gate states */
	srand(7);
	for (int k = 0; k < 20000; k++) {
		uint32_t c0 = (uint32_t)rand() ^ ((uint32_t)rand() << 16), c1 = (uint32_t)rand() & 0xffffu;
		setup(rand() & 1, (rand() % 4) == 0, (rand() % 4) == 0, c0, c1, (uint32_t)(rand() & 0x300));
		run(1 + (rand() & 1));
		if (REG(R_SYS, 0x100) != c0 || REG(R_SYS, 0x110) != c1) { printf("FAIL sweep restore\n"); fails++; break; }
		if (fails) { break; }
	}
	printf(fails ? "T_MDP_PROBE FAILED %d\n" : "T_MDP_PROBE ALL OK\n", fails);
	return fails != 0;
}
