/* Host test of 0673 (MDP bring-up stage P in nord2_mdp.inc) against a strict register state machine.
 * Model, written independently from the driver's tables (literal masks from clk-mt6885-pg.c):
 *   - infracfg protection registers PROTECTEN / _MM / _MM_2 / _SUB_INFRA_VDNR: EN changed by writes to _SET/_CLR, STA1 follows
 *     EN after `ack_delay` STA1 reads; bits in stuck1 never clear in STA1, bits in stuck0 never set in STA1.
 *   - hardware truth "mdpsys reachable" = MDP domain on (PWR_STATUS and _2ND bit 19), mdp_sel ungated, and no STA1 bit of the
 *     MDP masks set unless a powered-off child domain owns it.
 * Fails on: any mdpsys read while not reachable (engines/SMI with their gates closed as in t_mdp_probe); any SPM / topckgen
 * write; a protection write that touches a bit shared with another domain or a display bit; a CLR of a later vendor step
 * before the earlier released bits were acked clear; release steps out of vendor order (2:0 2:1 2:2 2:3 1:0 1:1 1:2);
 * restore (SET) out of the vendor power-down order (step1 before step2) or of bits the driver did not release; CG writes while
 * not reachable; CG_CON1 CLR value other than 0x1112 or CG_CON0 opened before CG_CON1; GCE (display) gate bit 8 written;
 * unbounded polling (> 20000 register reads or > 20 ms of delay in one bring-up); end state != start state after a failure
 * code that wrote something; success without a reachable mdpsys or without the re-probe reading it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
typedef uintptr_t vm_offset_t;
#define N2MDP_HOST 1
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

enum { R_SPM, R_TOP, R_INFRA, R_SYS, R_GCED, R_GCEM, R_N };
static const uint32_t base_pa[R_N] = { 0x10006000u, 0x10000000u, 0x10001000u, 0x1f000000u, 0x10228000u, 0x10318000u };
static const uint32_t size_b[R_N] = { 0x1000, 0x1000, 0x1000, 0x2e000, 0x1000, 0x1000 };
static uint32_t *mem[R_N];
#define REG(r, off) mem[r][(off) / 4]

/* ---- protection model ---------------------------------------------------------------------------------------------- */
static const uint16_t P_EN[4] = { 0x220, 0x2d0, 0xdc8, 0xbb4 }, P_SET[4] = { 0x2a0, 0x2d4, 0xdcc, 0xbb8 },
	P_CLR[4] = { 0x2a4, 0x2d8, 0xdd0, 0xbbc }, P_STA[4] = { 0x228, 0x2ec, 0xdd8, 0xbc4 };
/* MDP steps, vendor power-on order */
static const struct { int reg; uint32_t m; } STEP[7] = { {0, 0x00800000u}, {1, 0xa08802a8u}, {2, 0x00000a82u}, {3, 0x00100000u},
	{0, 0x00000400u}, {1, 0x50440154u}, {2, 0x00000155u} };
static const uint32_t MDPM[4] = { 0x00800400u, 0xf0cc03fcu, 0x00000bd7u, 0x00100000u };
/* children / others: register, PWR_STATUS bit, mask (pg.c, step1|step2) */
static const struct { int reg, bit; uint32_t m; } OTH[] = {
	{2, 12, 0x300}, {2, 16, 0xc0}, {2, 17, 0x3}, {1, 13, 0xc000}, {1, 14, 0x30000}, {1, 15, 0x03000000}, {1, 17, 0x0c000000},
	{1, 18, 0xf0000000u}, {1, 23, 0x3f}, {1, 24, 0xc0000}, {1, 25, 0x300000}, {1, 26, 0xc00000} };
static const uint32_t DISM[4] = { 0x40u, 0x0f33d7c3u, 0, 0x00200000u };
static const uint32_t EXCL[4] = { 0x00800400u, 0x0u, 0x00000814u, 0x00100000u };   /* computed by hand from the lists above */

static uint32_t en[4], sta[4], stuck1[4], stuck0[4];
static int pend[4], ack_delay;
static uint32_t released_by_drv[4];
static int last_clr_step, last_set_group, cg1_opened_seen, reads, writes, prot_writes, sys_reads_bad;
static uint64_t delay_us;
static bool cg0_stuck;
static bool killfile;
static int inj_reg = -1; static uint32_t inj_bit;     /* another agent sets this bit right after the driver's first CLR */

static uint32_t pwr(void) { return REG(R_SPM, 0x16c) & REG(R_SPM, 0x170); }
static uint32_t sta_target(int r) { return (en[r] | stuck1[r]) & ~stuck0[r]; }
static uint32_t child_off(int r)
{
	uint32_t m = 0;
	for (unsigned k = 0; k < sizeof OTH / sizeof OTH[0]; k++) { if (OTH[k].reg == r && !(pwr() & (1u << OTH[k].bit))) { m |= OTH[k].m; } }
	return m;
}
static bool truth_reachable(void)
{
	if (!(pwr() & (1u << 19)) || (REG(R_TOP, 0x20) & (1u << 15))) { return false; }
	for (int r = 0; r < 4; r++) { if (sta[r] & MDPM[r] & ~child_off(r)) { return false; } }
	return true;
}
static int step_of_bit(int r, uint32_t bit)
{
	for (int k = 0; k < 7; k++) { if (STEP[k].reg == r && (STEP[k].m & bit)) { return k; } }
	return -1;
}

static uint32_t apm_page[1024];
vm_offset_t n2mdp_host_map(uint32_t pa, uint32_t bytes)
{
	for (int i = 0; i < R_N; i++) { if (pa == base_pa[i] && bytes <= size_b[i]) { return (vm_offset_t)mem[i]; } }
	if (pa == 0x1000c000u) { return (vm_offset_t)apm_page; }   /* 0688: clock log (read only) */
	printf("FAIL map %#x\n", pa); fails++; return 0;
}
bool n2mdp_host_pool(uint32_t bytes, uint32_t *pa, vm_offset_t *va) { (void)bytes; (void)pa; (void)va; return false; }
void n2mdp_host_delay(uint32_t us) { delay_us += us; }
static uint64_t fake_now;
uint64_t n2mdp_host_now(void) { return fake_now += 7; }
bool n2mdp_host_killfile(void) { return killfile; }
bool n2mdp_host_alloc_hi(uint32_t bytes, vm_offset_t *va, uint64_t *pages) { (void)bytes; (void)va; (void)pages; return false; }
static int region(vm_offset_t b) { for (int i = 0; i < R_N; i++) { if (b == (vm_offset_t)mem[i]) { return i; } } return -1; }

static void check_sys_read(unsigned off)
{
	if (!truth_reachable()) { sys_reads_bad++; printf("FAIL mdpsys read %#x while not reachable\n", off); fails++; return; }
	uint32_t cg0 = REG(R_SYS, 0x100), cg1 = REG(R_SYS, 0x110);
	if (off < 0x1000) { return; }
	if (cg0 & (1u << 31)) { printf("FAIL read %#x with APB gated\n", off); fails++; return; }
	static const struct { uint32_t off; int bit; } e[] = { {0x1000,15}, {0x6000,0},{0x7000,16},{0x8000,8},{0x9000,24},
		{0x12000,4},{0x13000,20},{0x14000,10},{0x15000,26},{0x16000,7},{0x17000,23},{0x18000,14},{0x19000,30} };
	for (unsigned i = 0; i < sizeof e / sizeof e[0]; i++) {
		if ((off & ~0xfffu) == e[i].off && (cg0 & (1u << e[i].bit))) { printf("FAIL engine read %#x gated\n", off); fails++; }
	}
	unsigned blk = off & ~0xfffu;
	if ((blk == 0x2000 || blk == 0x3000 || blk == 0x4000 || blk == 0x27000) && (cg1 & 0x1112u)) { printf("FAIL smi read %#x gated\n", off); fails++; }
}
static uint32_t ni_rd(vm_offset_t b, unsigned off)
{
	if (b == (vm_offset_t)apm_page) { reads++; return apm_page[off / 4]; }
	int r = region(b);
	reads++;
	if (r < 0) { printf("FAIL rd unmapped\n"); fails++; return 0; }
	if (r == R_SYS) { check_sys_read(off); }
	if (r == R_GCED && (REG(R_INFRA, 0x90) & (1u << 8))) { printf("FAIL GCE-D read gated\n"); fails++; }
	if (r == R_GCEM && (REG(R_INFRA, 0x90) & (1u << 9))) { printf("FAIL GCE-M read gated\n"); fails++; }
	if (r == R_INFRA) {
		for (int p = 0; p < 4; p++) {
			if (off == P_STA[p]) {
				if (pend[p] > 0) { pend[p]--; } else { sta[p] = sta_target(p); }
				return sta[p];
			}
			if (off == P_EN[p]) { return en[p]; }
		}
	}
	return REG(r, off);
}
static void ni_wr(vm_offset_t b, unsigned off, uint32_t v)
{
	int r = region(b);
	writes++;
	if (r == R_SPM || r == R_TOP || r < 0) { printf("FAIL write to SPM/TOP/unmapped %#x\n", off); fails++; return; }
	if (r == R_INFRA) {
		if (off == 0x84 || off == 0x80) {
			if (v & ~(1u << 9)) { printf("FAIL IFRAO0 write %#x touches more than GCE2\n", v); fails++; }
			if (off == 0x84) { REG(R_INFRA, 0x90) &= ~v; } else { REG(R_INFRA, 0x90) |= v; }
			return;
		}
		for (int p = 0; p < 4; p++) {
			if (off == P_CLR[p] || off == P_SET[p]) {
				prot_writes++;
				if (v & ~EXCL[p]) { printf("FAIL protection write reg %d value %#x touches shared/display bits %#x\n", p, v, v & ~EXCL[p]); fails++; }
				if (v & DISM[p]) { printf("FAIL display bit written\n"); fails++; }
				if (off == P_CLR[p]) {
					for (uint32_t bit = 1; bit; bit <<= 1) {
						if (!(v & bit)) { continue; }
						int k = step_of_bit(p, bit);
						if (k < last_clr_step) { printf("FAIL release order: step %d after step %d\n", k, last_clr_step); fails++; }
						last_clr_step = k;
					}
					/* earlier released bits must be acked clear (by the model state) before the next step */
					for (int q = 0; q < 4; q++) {
						if (sta[q] & released_by_drv[q]) { printf("FAIL CLR reg %d before reg %d bits %#x were acked clear\n", p, q, sta[q] & released_by_drv[q]); fails++; }
					}
					released_by_drv[p] |= v;
					en[p] &= ~v;
					if (inj_reg >= 0) { en[inj_reg] |= inj_bit; sta[inj_reg] |= inj_bit; stuck1[inj_reg] |= inj_bit; inj_reg = -1; }
				} else {
					if (v & ~released_by_drv[p]) { printf("FAIL SET of bits %#x the driver did not release\n", v & ~released_by_drv[p]); fails++; }
					for (uint32_t bit = 1; bit; bit <<= 1) {
						if (!(v & bit)) { continue; }
						int g = step_of_bit(p, bit) >= 4 ? 1 : 2;            /* step1 group = 1, step2 group = 2 */
						if (g < last_set_group) { printf("FAIL restore order: step%d after step%d\n", g, last_set_group); fails++; }
						last_set_group = g;
					}
					released_by_drv[p] &= ~v;
					en[p] |= v;
				}
				pend[p] = ack_delay;
				return;
			}
		}
		printf("FAIL infra write %#x\n", off); fails++; return;
	}
	if (r == R_SYS) {
		if (!truth_reachable()) { printf("FAIL mdpsys write %#x while not reachable\n", off); fails++; }
		if (off == 0x118) { if (v != 0x1112u) { printf("FAIL CG_CON1 CLR value %#x\n", v); fails++; } cg1_opened_seen = 1; REG(R_SYS, 0x110) &= ~v; return; }
		if (off == 0x108) {
			if ((REG(R_SYS, 0x110) & 0x1112u) && !cg1_opened_seen) { printf("FAIL CG_CON0 opened before CG_CON1 (SMI)\n"); fails++; }
			REG(R_SYS, 0x100) &= ~(cg0_stuck ? (v & ~(1u << 31)) : v); return;
		}
		if (off == 0x104) { REG(R_SYS, 0x100) |= v; return; }
		if (off == 0x114) { REG(R_SYS, 0x110) |= v; return; }
		printf("FAIL mdpsys write %#x (only CG SET/CLR allowed)\n", off); fails++; return;
	}
	printf("FAIL write region %d %#x\n", r, off); fails++;
}

#include NORD2_MDP_INC

/* ---- scenarios ------------------------------------------------------------------------------------------------------- */
struct world { uint32_t pwr, con, cfg1, ifr, cg0, cg1, p[4], s1[4], s0[4]; int delay; bool cg0_stuck; int stages; };
static const struct world PHONE = { 0x10580fffu, 0xdu, 0x1010808u, 0x0u, 0xffffffffu, 0xffffu,
	{ 0xe70c508eu, 0x55554815u, 0x141u, 0x0u }, { 0 }, { 0 }, 0, false, 0x100 };

static void setw(const struct world *w)
{
	for (int i = 0; i < R_N; i++) { memset(mem[i], 0, size_b[i]); }
	memset(&n2mdp, 0, sizeof n2mdp); memset(&n2mx, 0, sizeof n2mx); memset(&n2pw, 0, sizeof n2pw);
	REG(R_SPM, 0x16c) = REG(R_SPM, 0x170) = w->pwr; REG(R_SPM, 0x34c) = w->con; REG(R_SPM, 0x350) = 0xd;
	REG(R_TOP, 0x20) = w->cfg1; REG(R_INFRA, 0x90) = w->ifr;
	REG(R_SYS, 0x100) = w->cg0; REG(R_SYS, 0x110) = w->cg1; REG(R_SYS, 0x120) = 0xf;
	for (int r = 0; r < 4; r++) { en[r] = sta[r] = w->p[r]; stuck1[r] = w->s1[r]; stuck0[r] = w->s0[r]; pend[r] = 0; released_by_drv[r] = 0; }
	ack_delay = w->delay; cg0_stuck = w->cg0_stuck; n2mdp_max_stage = w->stages; killfile = false; inj_reg = -1;
	last_clr_step = -1; last_set_group = 0; cg1_opened_seen = 0; reads = writes = prot_writes = sys_reads_bad = 0; delay_us = 0;
}
/* the boot sequence of n2mdp_thread: level-1 probe, then the stage machine */
static void boot(void)
{
	n2mdp.state = n2mdp_probe(1);
	int r0 = reads; uint64_t d0 = delay_us;
	n2mdp_stages(n2mdp.state);
	CHECK(reads - r0 < 20000, "unbounded polling: %d reads", reads - r0);
	CHECK(delay_us - d0 <= 20000, "too much delay: %llu us", (unsigned long long)(delay_us - d0));
}
static bool prot_equal(const struct world *w) { for (int r = 0; r < 4; r++) { if (en[r] != w->p[r]) { return false; } } return true; }
static bool gates_equal(const struct world *w) { return REG(R_SYS, 0x100) == w->cg0 && REG(R_SYS, 0x110) == w->cg1 && REG(R_INFRA, 0x90) == w->ifr; }

static void expect_untouched(const struct world *w, uint32_t code, const char *what)
{
	CHECK(n2pw.code == code, "%s: code %u want %u", what, n2pw.code, code);
	CHECK(prot_writes == 0, "%s: %d protection writes", what, prot_writes);
	CHECK(prot_equal(w) && gates_equal(w), "%s: state changed", what);
	CHECK(!n2mdp.prot_owned_rule, "%s: owned rule enabled", what);
}

int main(void)
{
	for (int i = 0; i < R_N; i++) { mem[i] = calloc(1, size_b[i]); }
	struct world w;

	/* 0: the driver's derived tables equal the hand-computed ones */
	for (unsigned r = 0; r < 4; r++) {
		CHECK(n2mdp_prot_mdp(r) == MDPM[r], "mdp mask reg %u %#x", r, n2mdp_prot_mdp(r));
		CHECK(n2mdp_prot_exclusive(r) == EXCL[r], "exclusive reg %u %#x want %#x", r, n2mdp_prot_exclusive(r), EXCL[r]);
	}

	/* 1: the phone state of 2026-11-15 (q10jmdp0/1/2a logs): nothing to release, gates opened, re-probe reads mdpsys */
	w = PHONE; setw(&w);
	n2mdp.state = n2mdp_probe(1);
	CHECK(n2mdp.state & N2MDP_S_PROT, "phone: legacy probe should still report prot (0670 rule), state %#x", n2mdp.state);
	n2mdp_stages(n2mdp.state);
	CHECK(n2pw.code == 1, "phone: code %u", n2pw.code);
	CHECK(prot_writes == 0, "phone: %d protection writes (nothing should be released)", prot_writes);
	CHECK(prot_equal(&w), "phone: protection changed");
	CHECK(n2mdp.prot_owned_rule, "phone: owned rule not set");
	CHECK(!(n2mdp.state & N2MDP_S_PROT) && (n2mdp.state & N2MDP_S_APB) && (n2mdp.state & N2MDP_S_SMI) && (n2mdp.state & N2MDP_S_ENGREAD),
	    "phone: re-probe state %#x", n2mdp.state);
	CHECK(REG(R_SYS, 0x110) == (0xffffu & ~0x1112u) && REG(R_SYS, 0x100) == (0xffffffffu & ~N2MDP_CG0_PWRON), "phone: gates %#x %#x",
	    REG(R_SYS, 0x100), REG(R_SYS, 0x110));
	CHECK(sys_reads_bad == 0, "phone: bad reads");

	/* 2: every exclusive bit set, acks delayed 3 reads: all released in vendor order, success */
	w = PHONE; w.p[0] |= 0x00800400u; w.p[2] |= 0x814u; w.p[3] = 0x00100000u | 0x00200000u; w.delay = 3; setw(&w);
	boot();
	CHECK(n2pw.code == 1, "excl: code %u", n2pw.code);
	CHECK(prot_writes == 5, "excl: %d protection writes (want 5: 2:0 2:2 2:3 1:0 1:2)", prot_writes);
	CHECK(en[0] == (w.p[0] & ~0x00800400u) && en[2] == (w.p[2] & ~0x814u) && en[3] == 0x00200000u, "excl: end EN %#x %#x %#x", en[0], en[2], en[3]);
	CHECK(en[3] & 0x00200000u, "excl: display bit 21 of SUB_INFRA_VDNR changed");
	CHECK((n2mdp.state & N2MDP_S_APB) && !(n2mdp.state & N2MDP_S_PROT), "excl: re-probe %#x", n2mdp.state);

	/* 3: each exclusive bit stuck at 1 in turn: code 6, everything released is put back, gates untouched */
	static const struct { int r; uint32_t b; } EB[6] = { {0, 1u << 23}, {2, 1u << 11}, {3, 1u << 20}, {0, 1u << 10}, {2, 1u << 2}, {2, 1u << 4} };
	for (int i = 0; i < 6; i++) {
		w = PHONE; w.p[0] |= 0x00800400u; w.p[2] |= 0x814u; w.p[3] = 0x00100000u; w.delay = 2; w.s1[EB[i].r] = EB[i].b; setw(&w);
		boot();
		CHECK(n2pw.code == 6, "stuck %d: code %u", i, n2pw.code);
		CHECK(prot_equal(&w), "stuck %d: protection not restored (%#x %#x %#x %#x)", i, en[0], en[1], en[2], en[3]);
		CHECK(gates_equal(&w), "stuck %d: gates changed", i);
		CHECK(!n2mdp.prot_owned_rule && (n2mdp.state & N2MDP_S_PROT), "stuck %d: owned rule / probe state", i);
		CHECK(sys_reads_bad == 0, "stuck %d: mdpsys read", i);
	}
	/* 4: stuck at release AND a released bit never re-acks on restore: code 7 */
	w = PHONE; w.p[0] |= 0x00800400u; w.p[2] |= 0x814u; w.s1[2] = 1u << 2; w.s0[0] = 1u << 23; setw(&w);
	boot();
	CHECK(n2pw.code == 7, "restore stuck: code %u", n2pw.code);
	CHECK(sys_reads_bad == 0 && gates_equal(&w), "restore stuck: reads/gates");

	/* 5: shared bit set while its child is ON (CAM on, MM bit 2), shared display bit (MM bit 6): code 5, nothing written */
	w = PHONE; w.pwr |= 1u << 23; setw(&w); boot(); expect_untouched(&w, 5, "cam on");
	w = PHONE; w.p[1] |= 1u << 6; setw(&w); boot(); expect_untouched(&w, 5, "display-shared bit");
	w = PHONE; w.pwr |= 1u << 16; w.p[2] |= 1u << 7; setw(&w); boot(); expect_untouched(&w, 5, "MM_2 bit 7 set with VDE2 on");
	w = PHONE; w.p[2] |= 1u << 7; setw(&w); boot();      /* same bit with VDE2 off: explained by the child, success, no write */
	CHECK(n2pw.code == 1 && prot_writes == 0, "MM_2 bit 7 VDE2 off: code %u writes %d", n2pw.code, prot_writes);
	/* 6: preconditions */
	w = PHONE; w.pwr &= ~(1u << 19); setw(&w); boot(); expect_untouched(&w, 2, "domain off");
	w = PHONE; w.con = 0xf; setw(&w); boot(); expect_untouched(&w, 3, "isolated");
	w = PHONE; w.con = 0x10d; setw(&w); boot(); expect_untouched(&w, 3, "sram pdn");
	w = PHONE; w.cfg1 |= 1u << 15; setw(&w); boot(); expect_untouched(&w, 4, "mdp_sel gated");
	/* 7: gate does not open (APB stuck): code 8, gates and protection restored */
	w = PHONE; w.p[0] |= 0x400u; w.cg0_stuck = true; w.ifr = 0x300u; setw(&w); boot();
	CHECK(n2pw.code == 8, "cg stuck: code %u", n2pw.code);
	CHECK(prot_equal(&w) && gates_equal(&w), "cg stuck: not restored (en0 %#x cg0 %#x cg1 %#x ifr %#x)", en[0], REG(R_SYS, 0x100), REG(R_SYS, 0x110), REG(R_INFRA, 0x90));
	/* 8: kill switches: kill file, stages 0, stages 1 without bit 8 (= q10jmdp2a behaviour) */
	w = PHONE; setw(&w); killfile = true; boot(); expect_untouched(&w, 0, "kill file"); CHECK(writes == 0, "kill file: writes");
	w = PHONE; w.stages = 0; setw(&w); boot(); expect_untouched(&w, 0, "stages 0"); CHECK(writes == 0, "stages 0: writes");
	w = PHONE; w.stages = 1; setw(&w); boot(); expect_untouched(&w, 0, "stages 1"); CHECK(writes == 0, "stages 1: writes");
	w = PHONE; w.stages = -1; setw(&w); boot(); expect_untouched(&w, 0, "stages -1"); CHECK(writes == 0, "stages -1: writes");
	/* 9: already all clear and gates open: no writes at all, success */
	w = PHONE; w.p[1] = 0; w.p[2] = 0; w.cg0 = 0; w.cg1 = 0; setw(&w); boot();
	CHECK(n2pw.code == 1 && writes == 0, "all open: code %u writes %d", n2pw.code, writes);

	/* 9b: another agent sets a display-shared MDP bit (MM 7) during the release: verification catches it, code 10, restored */
	w = PHONE; w.p[0] |= 0x400u; setw(&w); inj_reg = 1; inj_bit = 1u << 7; boot();
	CHECK(n2pw.code == 10, "inject: code %u", n2pw.code);
	CHECK(en[0] == w.p[0] && en[1] == (w.p[1] | (1u << 7)) && gates_equal(&w), "inject: not restored (en0 %#x)", en[0]);
	CHECK(!n2mdp.prot_owned_rule && sys_reads_bad == 0, "inject: owned rule / reads");

	/* 10: random sweep */
	srand(673);
	int ok = 0, codes[11] = { 0 };
	for (int it = 0; it < 30000 && fails < 20; it++) {
		w = PHONE;
		w.pwr = 0x10500fffu | ((uint32_t)rand() & 0x07ff7000u);                 /* random children / MDP power */
		if (rand() % 8 == 0) { w.pwr &= ~(1u << 19); }
		if (rand() % 16 == 0) { w.con |= 1u << (rand() % 13); }
		if (rand() % 16 == 0) { w.cfg1 |= 1u << 15; }
		for (int r = 0; r < 4; r++) {
			uint32_t rnd = (uint32_t)rand() ^ ((uint32_t)rand() << 15);
			w.p[r] = (rand() % 3 == 0) ? (rnd & ~MDPM[r]) | (rnd & EXCL[r]) : rnd;      /* often: only exclusive MDP bits */
			if (rand() % 6 == 0) { w.s1[r] = (uint32_t)rand() & EXCL[r]; }
			if (rand() % 10 == 0) { w.s0[r] = (uint32_t)rand() & EXCL[r]; }
		}
		w.p[3] &= 0x00300000u | ((uint32_t)rand() & 0xff);
		w.delay = rand() % 6; w.cg0 = (uint32_t)rand() ^ ((uint32_t)rand() << 16); w.cg1 = (uint32_t)rand() & 0xffffu;
		w.ifr = (uint32_t)rand() & 0x300u; w.cg0_stuck = rand() % 20 == 0;
		setw(&w);
		boot();
		uint32_t c = n2pw.code;
		if (c <= 10) { codes[c]++; }
		if (c == 1) {
			ok++;
			CHECK(truth_reachable(), "it %d: success but not reachable", it);
			CHECK((n2mdp.state & N2MDP_S_APB), "it %d: success but re-probe state %#x", it, n2mdp.state);
		} else if (c == 6 || c == 8 || c == 10) {
			CHECK(prot_equal(&w), "it %d: code %u protection not restored", it, c);
			CHECK(gates_equal(&w), "it %d: code %u gates changed", it, c);
		} else if (c >= 2 && c <= 5) {
			CHECK(prot_writes == 0 && prot_equal(&w) && gates_equal(&w), "it %d: code %u wrote something", it, c);
		} else if (c == 7) {
			CHECK(gates_equal(&w), "it %d: code 7 gates changed", it);
		} else {
			CHECK(0, "it %d: unexpected code %u", it, c);
		}
		CHECK(c == 1 || !n2mdp.prot_owned_rule, "it %d: owned rule after failure", it);
	}
	printf("sweep: %d ok; codes", ok);
	for (int c = 0; c <= 10; c++) { printf(" %d:%d", c, codes[c]); }
	printf("\n");
	printf(fails ? "T_MDP_PWRON FAILED %d\n" : "T_MDP_PWRON ALL OK\n", fails);
	return fails != 0;
}
