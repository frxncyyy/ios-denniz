/* SPDX-License-Identifier: MIT */
/*
 * t_p031x.c -- host test of the VENC fixes (opus-kern hunks in K02 nord2_vcodec.inc, shipped in 0310) and of 0311's
 * nord2_vcodec_at.inc, on top of K02's model and tests.
 *
 * run_tests.sh builds a /tmp tree = kernel/b + 0310 + 0311, copies K02's test_k02/t_k02.c next to it with three
 * model edits (VENC windows 0x3000, MDP domain on at reset, main renamed to k02_main) and compiles THIS file, which includes
 * that copy, then nord2_vcodec_at.inc (0311).  So every K02 check still runs against the patched driver, then the 0311 checks below:
 *   - VENC/VENC_C1 windows: offset 0x2ffc accepted, 0x3000 rejected (vpud maps 0x3000);
 *   - VENC_C1 and VDEC_CORE refuse with EACCES, writing nothing, while the MDP domain is off or mdp_sel is gated;
 *     every block refuses while disp_sel is gated;
 *   - MMSRAM route bits 16-19 cleared on VENC ports (Linux sets them on 5,6,13,14,21,22), JPEG ports untouched;
 *   - autotest command 8 (n2vc_at_command): step 0 with everything off touches nothing; 1-2-3-4 on VENC and core 1;
 *     argument and order errors; step 4 leaves the domains as found; n2vc_ven_refs() for the JPEG step-4 guard.
 * Expected values are literals from the sources cited in the driver (smi_drv.c, pg.c, clk-mt6885.c), not from the driver.
 */
#define main k02_main
#include "t_k02.c"
#undef main
#include "../bsd/dev/nord2_vcodec_at.inc"

static int at(uint32_t arg, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t expect)
{
	uint32_t r[16] = { r0, r1, r2 };
	expect_mask = expect; expect_arm = 1;
	int res = (int)n2vc_at_command(arg, r);
	expect_arm = 0;
	return res;
}

static uint32_t con(unsigned blk) { return mr(W_SPM, md[blk].con); }
static bool sta_on(unsigned blk) { return (mr(W_SPM, 0x16c) >> md[blk].sta) & 1u; }

static void test_windows(void)
{
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC, 1, 1u << RWVC_BLK_VENC) == 0);
	struct rwvc_reg_op op = { RWVC_OP_WRITE, RWVC_WIN_VENC, 0, 0x2ffc, 0x13579bdfu, 0 };
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)&op, 1, 0, 0, 0 };
	CHECK(call(RWVC_REGS, &b, 0) == 0 && b.out_done == 1 && mr(W_VENC, 0x2ffc) == 0x13579bdfu);
	op.offset = 0x3000;
	CHECK(call(RWVC_REGS, &b, 0) == EINVAL && b.out_done == 0);
	op.offset = 0x2000; op.op = RWVC_OP_READ;      /* 0x2000..0x2fff were EINVAL before 0311 */
	setr(W_VENC, 0x2000, 0x2468ace0u);
	CHECK(call(RWVC_REGS, &b, 0) == 0 && op.value == 0x2468ace0u);
	close_expect(F, 1u << RWVC_BLK_VENC);
	CHECK(!sta_on(RWVC_BLK_VENC));
}

static void test_mdp_parent(void)
{
	/* MDP domain off: VENC_C1 and VDEC_CORE refused before any write to their PWR_CON or protection bits */
	model_init();
	setr(W_SPM, 0x16c, 1u << 20);
	setr(W_SPM, 0x170, 1u << 20);
	F = n2vc_open_create(0);
	uint32_t mm0 = mr(W_INFRA, 0x2d0), mm20 = mr(W_INFRA, 0xdc8);
	CHECK(power(RWVC_BLK_VENC_C1, 1, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1)) == EACCES);
	CHECK(con(RWVC_BLK_VENC_C1) == 0x1112u && !sta_on(RWVC_BLK_VENC_C1));
	CHECK((mr(W_INFRA, 0x2d0) & 0xf0000000u) == (mm0 & 0xf0000000u));
	CHECK(!sta_on(RWVC_BLK_VENC) && n2vc.refs[RWVC_BLK_VENC] == 0);   /* the hidden parent reference was given back */
	CHECK(power(RWVC_BLK_VDEC_CORE, 1, (1u << RWVC_BLK_VDEC_SOC) | (1u << RWVC_BLK_VDEC_CORE)) == EACCES);
	CHECK(con(RWVC_BLK_VDEC_CORE) == 0x1112u && (mr(W_INFRA, 0xdc8) & 0xc0u) == (mm20 & 0xc0u));
	CHECK(!sta_on(RWVC_BLK_VDEC_SOC) && n2vc.refs[RWVC_BLK_VDEC_SOC] == 0);
	/* VEN itself (larb7, DISP common) and VDEC_SOC (larb5) do not need MDP */
	CHECK(power(RWVC_BLK_VENC, 1, 1u << RWVC_BLK_VENC) == 0);
	CHECK(power(RWVC_BLK_VDEC_SOC, 1, 1u << RWVC_BLK_VDEC_SOC) == 0);
	close_expect(F, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VDEC_SOC));
	/* MDP on but mdp_sel gated (CLK_CFG_1 bit 15) */
	model_init();
	setr(W_TOP, 0x20, 1u << 15);
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC_C1, 1, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1)) == EACCES);
	CHECK(con(RWVC_BLK_VENC_C1) == 0x1112u && !sta_on(RWVC_BLK_VENC));
	CHECK(power(RWVC_BLK_VENC, 1, 1u << RWVC_BLK_VENC) == 0);
	close_expect(F, 1u << RWVC_BLK_VENC);
	/* disp_sel gated (bit 7): nothing powers */
	model_init();
	setr(W_TOP, 0x20, 1u << 7);
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC, 1, 1u << RWVC_BLK_VENC) == EACCES && con(RWVC_BLK_VENC) == 0x1112u);
	CHECK(mr(W_TOP, 0xe0) == 0x0c030601u);           /* not even the mux was touched */
	close_expect(F, 0);
	/* everything as on the phone (MDP + DIS on, both muxes running): core 1 powers, and both go off again */
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC_C1, 1, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1)) == 0);
	CHECK(sta_on(RWVC_BLK_VENC) && sta_on(RWVC_BLK_VENC_C1) && con(RWVC_BLK_VENC_C1) == 0x0du);
	close_expect(F, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1));
	CHECK(!sta_on(RWVC_BLK_VENC) && !sta_on(RWVC_BLK_VENC_C1));
}

static void test_sysram_bits(void)
{
	static const unsigned sram_ports[6] = { 5, 6, 13, 14, 21, 22 };
	model_init();
	for (unsigned i = 0; i < 6; i++) { setr(W_L7, 0x380 + 4 * sram_ports[i], 0x000f0001u); setr(W_L8, 0x380 + 4 * sram_ports[i], 0x000f0001u); }
	setr(W_L7, 0x380 + 4 * 7, 0x000f5501u);          /* a JPEG port: not ours, must stay as it is */
	setr(W_L7, 0x380 + 4 * 24, 0x000f0001u);
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC_C1, 1, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1)) == 0);
	for (unsigned p = 0; p < 27; p++) {
		bool venc = p <= 6 || (p >= 13 && p <= 22);
		if (!venc) { continue; }
		CHECKMSG(mr(W_L7, 0x380 + 4 * p) == 0, "larb7 port %u nonsec %#x", p, mr(W_L7, 0x380 + 4 * p));
		CHECKMSG(mr(W_L8, 0x380 + 4 * p) == 0, "larb8 port %u nonsec %#x", p, mr(W_L8, 0x380 + 4 * p));
	}
	CHECK(mr(W_L7, 0x380 + 4 * 7) == 0x000f5501u && mr(W_L7, 0x380 + 4 * 24) == 0x000f0001u);
	close_expect(F, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1));
	/* a sysram bit that does not clear is a power-on failure (the larb read-back covers bits 16-19 too) */
	model_init();
	setr(W_L7, 0x380 + 4 * 13, 0x000f0000u);
	MDL.larb_stuck = 13;
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC, 1, 1u << RWVC_BLK_VENC) == EIO);
	CHECK(!sta_on(RWVC_BLK_VENC) && n2vc.refs[RWVC_BLK_VENC] == 0);
	close_expect(F, 1u << RWVC_BLK_VENC);
}

static void test_autotest(void)
{
	/* step 0 with everything off: reads only SPM/INFRA/TOP; any block access would be a model violation */
	model_init();
	int v0 = violations;
	uint32_t spm_before[0x400];
	memcpy(spm_before, wm[W_SPM], sizeof(spm_before));
	CHECK(at(0, 0, 0, 0, 0) == 1);
	CHECK(at(0x100, 0, 0, 0, 0) == 1);
	CHECK(memcmp(spm_before, wm[W_SPM], sizeof(spm_before)) == 0 && violations == v0);
	/* order and argument errors */
	CHECK(at(5, 0, 0, 0, 0) == 16 && at(0x200, 0, 0, 0, 0) == 16);
	CHECK(at(2, 0, 0, 0, 0) == 21 && at(3, 0, 0, 0, 0) == 21);
	CHECK(at(3, 2, 0, 0, 0) == 22 && at(3, 0x3000, 0, 0, 0) == 22 && at(3, 0, 1, 5, 0) == 22);
	/* the normal run on core 0 */
	CHECK(at(1, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1);
	CHECK(sta_on(RWVC_BLK_VENC) && n2vc.on[RWVC_BLK_VENC] && n2vc_ven_refs() == 1);
	CHECK(at(1, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && n2vc_ven_refs() == 1);   /* idempotent: one autotest reference */
	setr(W_VENC, 0x5c, 0x4u);                       /* a pending frame-done: step 2 clears it the Linux way */
	CHECK(at(2, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && mr(W_VENC, 0x5c) == 0 && mr(W_VENC, 0x60) == 0x4u);
	setr(W_VENC, 0x100, 0x11112222u);
	CHECK(at(3, 0, 0, 0, 0) == 1);
	CHECK(at(3, 0x100, 0, 0, 0) == 1 && mr(W_VENC, 0x100) == 0x11112222u);
	CHECK(at(3, 0x100, 1, 0xa5a5a5a5u, 1u << RWVC_BLK_VENC) == 1 && mr(W_VENC, 0x100) == 0x11112222u);   /* restored */
	CHECK(at(3, 0x2fc0, 1, 0x1u, 1u << RWVC_BLK_VENC) == 1);
	/* larb read-back catches a port that went back to translation */
	setr(W_L7, 0x380 + 4 * 15, 0x1u);
	CHECK(at(2, 0, 0, 0, 1u << RWVC_BLK_VENC) == 23);
	setr(W_L7, 0x380 + 4 * 15, 0x0u);
	/* the JPEG autotest's step 4 must be refused now: n2vc_ven_refs() != 0 */
	CHECK(n2vc_ven_refs() == 1);
	CHECK(at(4, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1);
	CHECK(!sta_on(RWVC_BLK_VENC) && n2vc_ven_refs() == 0 && con(RWVC_BLK_VENC) == 0x1112u);
	CHECK(at(4, 0, 0, 0, 0) == 1);                   /* off twice is fine */
	/* with core 1 */
	CHECK(at(0x101, 0, 0, 0, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1)) == 1);
	CHECK(sta_on(RWVC_BLK_VENC_C1) && n2vc.on[RWVC_BLK_VENC_C1]);
	CHECK(at(0x102, 0, 0, 0, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1)) == 1);
	CHECK(at(0x103, 0x100, 1, 0x5au, 1u << RWVC_BLK_VENC_C1) == 1 && mr(W_VENC1, 0x100) == 0);
	CHECK(at(0x104, 0, 0, 0, (1u << RWVC_BLK_VENC) | (1u << RWVC_BLK_VENC_C1)) == 1);
	CHECK(!sta_on(RWVC_BLK_VENC) && !sta_on(RWVC_BLK_VENC_C1));
	/* another holder keeps VEN on across the autotest's step 4 */
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC, 1, 1u << RWVC_BLK_VENC) == 0);
	CHECK(at(1, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && n2vc_ven_refs() == 2);
	CHECK(at(4, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && sta_on(RWVC_BLK_VENC) && n2vc_ven_refs() == 1);
	close_expect(F, 1u << RWVC_BLK_VENC);
	CHECK(!sta_on(RWVC_BLK_VENC));
	/* core 1 asked for while MDP is off: core 0 is up, result 19, step 4 cleans up */
	model_init();
	setr(W_SPM, 0x16c, 1u << 20);
	setr(W_SPM, 0x170, 1u << 20);
	CHECK(at(0x101, 0, 0, 0, 1u << RWVC_BLK_VENC) == 19);
	CHECK(con(RWVC_BLK_VENC_C1) == 0x1112u && sta_on(RWVC_BLK_VENC));
	CHECK(at(0x104, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && !sta_on(RWVC_BLK_VENC));
	/* q10jx: step 1 without clock bits = the driver default N2VC_VENC_SLOW -> parent 4 from the boot value 12 */
	model_init();
	CHECK(at(1, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && (mr(W_TOP, 0xe0) >> 24) == (N2VC_VENC_SLOW ? 4u : 12u));
	CHECK(at(4, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1);
	/* bit 9: venc_sel to parent 4 (273 MHz) before power, only while VEN is off; refused on other steps */
	model_init();
	CHECK(at(0x202, 0, 0, 0, 0) == 16 && mr(W_TOP, 0xe0) == 0x0c030601u);
	CHECK(at(0x201, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1);
	CHECK(mr(W_TOP, 0xe0) == 0x04030601u && sta_on(RWVC_BLK_VENC) && mr(W_TOP, 0x8) == (1u << 24));
	CHECK(at(0x201, 0, 0, 0, 1u << RWVC_BLK_VENC) == 25 && mr(W_TOP, 0xe0) == 0x04030601u);   /* VEN on: no change */
	CHECK(at(4, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && !sta_on(RWVC_BLK_VENC));
	F = n2vc_open_create(0);                         /* the driver keeps the running 273 MHz parent */
	CHECK(power(RWVC_BLK_VENC, 1, 1u << RWVC_BLK_VENC) == 0 && mr(W_TOP, 0xe0) == 0x04030601u);
	close_expect(F, 1u << RWVC_BLK_VENC);
	/* bit 10: back to parent 12 (624 MHz) while VEN is off; bits 9+10 together and on other steps are refused */
	CHECK(at(0x601, 0, 0, 0, 0) == 16 && at(0x403, 0, 0, 0, 0) == 16);
	CHECK(at(0x401, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && (mr(W_TOP, 0xe0) >> 24) == 12u && sta_on(RWVC_BLK_VENC));
	CHECK(at(4, 0, 0, 0, 1u << RWVC_BLK_VENC) == 1 && !sta_on(RWVC_BLK_VENC));
	/* display off: step 1 = 18, nothing powered */
	model_init();
	setr(W_SPM, 0x16c, 1u << 19);
	setr(W_SPM, 0x170, 1u << 19);
	CHECK(at(1, 0, 0, 0, 0) == 18 && !sta_on(RWVC_BLK_VENC) && con(RWVC_BLK_VENC) == 0x1112u);
	CHECK(at(4, 0, 0, 0, 0) == 1);
}


#ifdef N2VC_SPIN_STEP_US
/* 0314: adaptive wait.  Return latency after the status is set: <= one spin step (20 us) inside kern.n2vc.spin_us, <= one
 * sleep step after it; spin_us = 0 sleeps from the first poll; sleep_us is clamped to 50..1000. */
static void wait_lat(unsigned irq, uint32_t after, uint32_t maxlat)
{
	struct rwvc_wait_irq w;
	MDL.irq_armed[irq] = true; MDL.irq_at[irq] = now_us + after; MDL.irq_bits[irq] = irq == RWVC_IRQ_VENC ? 0x4 : 0x10000;
	uint64_t ready = now_us + after;
	CHECK(wait_irq(irq, 100, &w, 0) == 0 && w.out_count == 1);
	CHECKMSG(now_us >= ready && now_us - ready <= maxlat, "irq %u after %u: latency %llu > %u", irq, after,
	    (unsigned long long)(now_us - ready), maxlat);
}
static void test_adaptive_wait(void)
{
	model_init();
	F = n2vc_open_create(0);
	CHECK(power(RWVC_BLK_VENC, 1, 1u << 0) == 0);
	n2vc_spin_us = 2000u; n2vc_sleep_step = 200u;
	wait_lat(RWVC_IRQ_VENC, 300, 20);
	wait_lat(RWVC_IRQ_VENC, 1990, 20);
	wait_lat(RWVC_IRQ_VENC, 8000, 200);      /* a 1080p decode-length wait */
	n2vc_spin_us = 0;
	wait_lat(RWVC_IRQ_VENC, 300, 200);
	n2vc_sleep_step = 0;                     /* clamped to 50 */
	wait_lat(RWVC_IRQ_VENC, 300, 50);
	n2vc_sleep_step = 100000u;               /* clamped to 1000 */
	wait_lat(RWVC_IRQ_VENC, 300, 1000);
	n2vc_spin_us = 2000u; n2vc_sleep_step = 200u;
	/* RWVC_OP_POLL uses the same steps */
	struct rwvc_reg_op op = { RWVC_OP_POLL, RWVC_WIN_VENC, 0, 0x104, 1, 1 };
	struct rwvc_reg_batch b = { (uint64_t)(uintptr_t)&op, 1, 0, 100000u, 0 };
	MDL.poll_armed = true; MDL.poll_w = W_VENC; MDL.poll_off = 0x104; MDL.poll_at = now_us + 3000; MDL.poll_value = 1;
	uint64_t ready = now_us + 3000;
	CHECK(call(RWVC_REGS, &b, 0) == 0 && b.out_done == 1);
	CHECKMSG(now_us >= ready && now_us - ready <= 200, "poll latency %llu", (unsigned long long)(now_us - ready));
	/* the window cache: a second open sees the same mapping and the batch still refuses an unpowered block */
	CHECK(power(RWVC_BLK_VENC, 0, 1u << 0) == 0);
	op.op = RWVC_OP_READ;
	CHECK(call(RWVC_REGS, &b, 0) == EACCES);
	close_expect(F, 0);
}
#endif

int main(void)
{
	int k = k02_main();
	int f0 = fails, c0 = checks;
	test_windows();
	test_mdp_parent();
	test_sysram_bits();
	test_autotest();
#ifdef N2VC_SPIN_STEP_US
	test_adaptive_wait();
#endif
	if (violations) { printf("%d model violations\n", violations); }
	printf("0311 checks: %d/%d\n", (checks - c0) - (fails - f0), checks - c0);
	printf("N2TEST P031X %s %d/%d (violations %d)\n", k || fails || violations ? "FAIL" : "PASS", checks - fails, checks, violations);
	return k || fails || violations ? 1 : 0;
}
