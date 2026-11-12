/* SPDX-License-Identifier: MIT */
/*
 * nord2_vcodec_at.inc -- 0311 (opus-kern): the VENC bring-up steps behind autotest command 8 (nord2_autotest.inc), the
 * VENC counterpart of 0298's JPEG steps (nord2_hwcodec.inc, command 7).  Host tool: gpu/hwcodec/tools/hwv.py.
 *
 * Included by bsd/dev/sweet_touch.c right after nord2_jpegdev.inc, i.e. after nord2_vcodec.inc (K02), whose static helpers
 * and tables it uses: n2vc_lock/unlock, n2vc_pa, n2vc_status_on, n2vc_usable, n2vc_acquire_locked, n2vc_release_locked,
 * n2vc_blocks, n2vc_wins, n2vc.refs/on, n2vc_gic_pending and the N2VC_* register names.  Nothing here runs by itself: every
 * step is one USB command on the autotest thread; references taken by step 1 are the autotest's own (struct n2vc_open
 * n2vc_at_file), counted with every other user of the domain, so the domain only powers off when nobody holds it.
 * The host test (kern/p031x/test) compiles this file against K02's register model with N2VC_HOST.
 */
/* ---- 0311: VENC phone bring-up steps (autotest command 8, nord2_autotest.inc, host tool gpu/hwcodec/tools/hwv.py) -------
 * Modelled on 0298's JPEG steps. arg bits 0-7 = step, bit 8 = include VENC core 1 (VEN_CORE1, larb8; needs MDP on),
 * bit 9 (step 1 only) = first move venc_sel to parent 4, mainpll_d4_d2 = 273 MHz, Linux's lowest VENC step, which mmdvfs pairs
 * with the lowest vcore OPP (mt6893.dts mmdvfs_pmqos venc_step3 / vopp_steps; 624 MHz = venc_step0 needs vcore OPP 1);
 * bit 10 (step 1 only) = instead parent 12, univpll_d4 = 624 MHz, the phone's boot value.  Only while VEN is off and
 * unreferenced.  Without either bit step 1 uses the driver's vcodecd default (N2VC_VENC_SLOW = 1: 273 MHz), programmed by
 * K02's power-on only while VEN and VENC_C1 are off; a running mux (e.g. the JPEG driver holding VEN) is kept.
 *   0 status   read only: SPM, bus protection, venc_sel, the SMI commons' clock words, then per core only what is safe to read
 *              (larb only with the domain usable and the larb gate open, VENC only with its gate open), GIC pending bits.
 *   1 power    RWVC_POWER on for VENC (and core 1) on the autotest's own reference: mux, domain, protection, gates, larb ports.
 *   2 bus      read back the larb configuration of every VENC port (IOMMU off, no MMSRAM, OSTDL) and that the JPEG ports were
 *              not written; clear a pending VENC interrupt status exactly as Linux does (only bits that are set).
 *   3 regs     r0 = 0: read VENC +0x5c (interrupt status) only.  r0 = offset (4-aligned, < 0x3000): read 16 words from there;
 *              r1 = 1 additionally writes r2 at r0, reads it back and RESTORES the old value (a write/read-back smoke test).
 *              Core 1 window instead with arg bit 8.
 *   4 off      drop the autotest's references (core 1 first); the domain powers off if nobody else holds it.
 * Results: 1 done; 16 bad step, 17 mapping, 18 display domain off, 19 MDP domain off (core 1), 20 power sequence failed,
 * 21 not powered (run step 1), 22 bad arguments, 23 larb read-back wrong, 24 write read-back differs, 25 VEN busy elsewhere
 * (bit 9 while VEN is on or held).
 */
enum {
	N2VC_AT_OK = 1, N2VC_AT_E_STEP = 16, N2VC_AT_E_MAP = 17, N2VC_AT_E_DIS = 18, N2VC_AT_E_MDP = 19, N2VC_AT_E_POWER = 20,
	N2VC_AT_E_NOT_ON = 21, N2VC_AT_E_ARGS = 22, N2VC_AT_E_LARB = 23, N2VC_AT_E_READBACK = 24, N2VC_AT_E_BUSY = 25,
};
#define N2VC_AT_MMSYS_PA  0x14116000u   /* mmsys_config (mt6893.dts:4269): CG_CON1 0x110, SMI bits 0x88880000 (pg.c:1150-1152) */
#define N2VC_AT_MDPSYS_PA 0x1f000000u   /* mdpsys_config (mt6893.dts:3728): CG_CON1 0x110, SMI bits 0x1112 (pg.c:1147-1149) */
static struct n2vc_open n2vc_at_file;   /* the autotest's references; not in n2vc.files (no device open) */

/* true when the block's registers may be read: domain usable and the gate bit set (larb: bit 0, VENC: bit 4) */
static bool
n2vc_at_gate(unsigned blk, unsigned bit)
{
	if (!n2vc_usable(blk)) { return false; }
	vm_offset_t g = n2vc_pa(n2vc_blocks[blk].gate_pa, 0x1000, 1);
	return g && (ni_rd(g, 0x0) & (1u << bit));
}

static void
n2vc_at_dump(const char *when, unsigned flags)
{
	vm_offset_t spm = n2vc_pa(N2VC_SPM_PA, 0x1000, 1), infra = n2vc_pa(N2VC_INFRA_PA, 0x1000, 1), top = n2vc_pa(N2VC_TOP_PA, 0x1000, 1);
	if (!spm || !infra || !top) { printf("NORD2 VCODEC: %s: mapping failed\n", when); return; }
	uint32_t s0 = ni_rd(spm, N2VC_PWR_STA), s1 = ni_rd(spm, N2VC_PWR_STA2);
	printf("NORD2 VCODEC: %s: spm status %#x/%#x VEN %#x VEN_C1 %#x MDP %#x DIS %#x | prot mm %#x ack %#x mm2 %#x ack %#x | CLK_CFG_13 %#x"
	    " | refs VEN %u C1 %u (autotest %u/%u)\n", when, s0, s1, ni_rd(spm, 0x344), ni_rd(spm, 0x348), ni_rd(spm, 0x34c), ni_rd(spm, 0x350),
	    ni_rd(infra, N2VC_MM_OFF), ni_rd(infra, N2VC_MM_STA), ni_rd(infra, N2VC_MM2_OFF), ni_rd(infra, N2VC_MM2_STA), ni_rd(top, N2VC_CFG13),
	    n2vc.refs[RWVC_BLK_VENC], n2vc.refs[RWVC_BLK_VENC_C1], n2vc_at_file.refs[RWVC_BLK_VENC], n2vc_at_file.refs[RWVC_BLK_VENC_C1]);
	uint32_t cfg1 = ni_rd(top, N2VC_CFG1);
	printf("NORD2 VCODEC: %s: CLK_CFG_1 %#x (disp_sel %s, mdp_sel %s)\n", when, cfg1, (cfg1 & N2VC_DISP_GATE) ? "GATED" : "on",
	    (cfg1 & N2VC_MDP_GATE) ? "GATED" : "on");
	if ((s0 & s1 & N2VC_DIS_BIT) != 0 && !(cfg1 & N2VC_DISP_GATE)) {
		vm_offset_t mm = n2vc_pa(N2VC_AT_MMSYS_PA, 0x1000, 1);
		if (mm) { printf("NORD2 VCODEC: %s: mmsys CG_CON1 %#x (DISP SMI clocks gated: %#x)\n", when, ni_rd(mm, 0x110), ni_rd(mm, 0x110) & 0x88880000u); }
	}
	if ((s0 & s1 & N2VC_MDP_BIT) != 0 && !(cfg1 & N2VC_MDP_GATE)) {
		vm_offset_t mdp = n2vc_pa(N2VC_AT_MDPSYS_PA, 0x1000, 1);
		if (mdp) { printf("NORD2 VCODEC: %s: mdpsys CG_CON1 %#x (MDP SMI clocks gated: %#x)\n", when, ni_rd(mdp, 0x110), ni_rd(mdp, 0x110) & 0x1112u); }
	}
	for (unsigned blk = RWVC_BLK_VENC; blk <= RWVC_BLK_VENC_C1; blk++) {
		if (blk == RWVC_BLK_VENC_C1 && !(flags & 1u) && !n2vc_status_on(&n2vc_blocks[blk])) { continue; }
		const struct n2vc_blk *b = &n2vc_blocks[blk];
		if (!n2vc_usable(blk)) { printf("NORD2 VCODEC: %s: %s domain not usable: its registers are not read\n", when, b->name); continue; }
		if (ni_rd(top, N2VC_CFG13) & (1u << 31)) { printf("NORD2 VCODEC: %s: venc_sel gated: %s registers are not read\n", when, b->name); continue; }
		vm_offset_t g = n2vc_pa(b->gate_pa, 0x1000, 1);
		uint32_t cg = g ? ni_rd(g, 0x0) : 0;
		printf("NORD2 VCODEC: %s: %s gates %#x\n", when, b->name, cg);
		if (cg & 1u) {
			vm_offset_t l = n2vc_pa(b->larb->pa, 0x1000, 1);
			if (l) {
				uint32_t ns_or = 0, ns_and = 0xffffffffu, os_mis = 0;
				for (unsigned p = 0; p < b->larb->nports; p++) {
					if (!(b->larb->ports & (1u << p))) { continue; }
					uint32_t v = ni_rd(l, 0x380 + 4 * p);
					ns_or |= v; ns_and &= v;
					if ((ni_rd(l, 0x200 + 4 * p) & 0xffu) != b->larb->ostdl[p]) { os_mis |= 1u << p; }
				}
				printf("NORD2 VCODEC: %s: larb%u stat %#x 0x24 %#x 0x40 %#x 0x524 %#x | VENC ports nonsec or %#x and %#x, ostdl mismatch %#x"
				    " | jpeg nonsec7-10 %#x %#x %#x %#x\n", when, blk == RWVC_BLK_VENC ? 7u : 8u, ni_rd(l, 0x0), ni_rd(l, 0x24), ni_rd(l, 0x40),
				    ni_rd(l, 0x524), ns_or, ns_and, os_mis, ni_rd(l, 0x39c), ni_rd(l, 0x3a0), ni_rd(l, 0x3a4), ni_rd(l, 0x3a8));
			}
		}
		if (cg & (1u << 4)) {
			vm_offset_t v = n2vc_pa(n2vc_wins[blk == RWVC_BLK_VENC ? RWVC_WIN_VENC : RWVC_WIN_VENC_C1].pa, 0x3000, 1);
			if (v) {
				printf("NORD2 VCODEC: %s: %s irq status +0x5c %#x; GIC INTID %u pending %d\n", when, b->name, ni_rd(v, 0x5c),
				    blk == RWVC_BLK_VENC ? N2VC_INTID_VENC : N2VC_INTID_VENC_C1,
				    n2vc_gic_pending(blk == RWVC_BLK_VENC ? N2VC_INTID_VENC : N2VC_INTID_VENC_C1));
			}
		}
	}
}

static uint32_t
n2vc_at_err(int err)
{
	return err == 0 ? N2VC_AT_OK : err == EACCES ? N2VC_AT_E_DIS : err == ENXIO ? N2VC_AT_E_MAP : N2VC_AT_E_POWER;
}

/* step 2: the larb as left by step 1, every VENC port; nothing but the interrupt status is written */
static uint32_t
n2vc_at_bus(unsigned blk)
{
	const struct n2vc_blk *b = &n2vc_blocks[blk];
	if (!n2vc.on[blk] || !n2vc_at_gate(blk, 0) || !n2vc_at_gate(blk, 4)) { return N2VC_AT_E_NOT_ON; }
	vm_offset_t l = n2vc_pa(b->larb->pa, 0x1000, 1);
	vm_offset_t v = n2vc_pa(n2vc_wins[blk == RWVC_BLK_VENC ? RWVC_WIN_VENC : RWVC_WIN_VENC_C1].pa, 0x3000, 1);
	if (!l || !v) { return N2VC_AT_E_MAP; }
	uint32_t bad = 0;
	if (ni_rd(l, 0x24) != 0x300256u || ni_rd(l, 0x40) != 1u || ni_rd(l, 0x524) != 0x300256u) { bad |= 1u << 31; }
	for (unsigned p = 0; p < b->larb->nports; p++) {
		if (!(b->larb->ports & (1u << p))) { continue; }
		if ((ni_rd(l, 0x380 + 4 * p) & b->larb->nsmask) || (ni_rd(l, 0x200 + 4 * p) & 0xffu) != b->larb->ostdl[p]) { bad |= 1u << p; }
	}
	uint32_t st = ni_rd(v, 0x5c);
	if (st & 0xbfu) { ni_wr(v, 0x60, st & 0xbfu); }          /* clean_irq_status(): write back the bits that are set */
	printf("NORD2 VCODEC: bus %s: larb ports wrong %#x (bit 31 = conf words); irq status %#x -> %#x\n", b->name, bad, st, ni_rd(v, 0x5c));
	return bad ? N2VC_AT_E_LARB : N2VC_AT_OK;
}

/* step 3: read (and optionally write, read back, restore) VENC registers */
static uint32_t
n2vc_at_regs(unsigned blk, const uint32_t *r)
{
	uint32_t off = r[0], wr = r[1], val = r[2];
	if ((off & 3u) || off > 0x3000u - 64u || (wr && !off)) { return N2VC_AT_E_ARGS; }
	if (!n2vc.on[blk] || !n2vc_at_gate(blk, 4)) { return N2VC_AT_E_NOT_ON; }
	vm_offset_t v = n2vc_pa(n2vc_wins[blk == RWVC_BLK_VENC ? RWVC_WIN_VENC : RWVC_WIN_VENC_C1].pa, 0x3000, 1);
	if (!v) { return N2VC_AT_E_MAP; }
	const char *name = n2vc_blocks[blk].name;
	if (!off) {
		printf("NORD2 VCODEC: regs %s: +0x5c %#x\n", name, ni_rd(v, 0x5c));
		return N2VC_AT_OK;
	}
	uint32_t w[16];
	for (unsigned i = 0; i < 16; i++) { w[i] = ni_rd(v, off + 4 * i); }
	printf("NORD2 VCODEC: regs %s +%#x: %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x\n", name, off,
	    w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15]);
	if (!wr) { return N2VC_AT_OK; }
	ni_wr(v, off, val);
	uint32_t got = ni_rd(v, off);
	ni_wr(v, off, w[0]);
	uint32_t back = ni_rd(v, off);
	printf("NORD2 VCODEC: regs %s +%#x: wrote %#x read %#x, restored %#x read %#x\n", name, off, val, got, w[0], back);
	return got == val ? N2VC_AT_OK : N2VC_AT_E_READBACK;
}

uint32_t
n2vc_at_command(uint32_t arg, const uint32_t *r)
{
	unsigned step = arg & 0xffu, flags = (arg >> 8) & 0xffu;
	bool c1 = (flags & 1u) != 0;
	unsigned blk = c1 ? RWVC_BLK_VENC_C1 : RWVC_BLK_VENC;
	uint32_t res = N2VC_AT_OK;
	if (step > 4 || (flags & ~7u) || ((flags & 6u) && step != 1) || (flags & 6u) == 6u) { return N2VC_AT_E_STEP; }
	n2vc_lock();
	printf("NORD2 VCODEC: at step %u%s\n", step, c1 ? " (core 1)" : "");
	switch (step) {
	case 0:
		n2vc_at_dump("status", flags);
		break;
	case 1: {
		n2vc_at_dump("before", flags);
		n2vc_at_file.used = 1;
		int err = 0;
		/* venc_sel: K02's power-on programs the parent passed here, only while VEN and VENC_C1 are both off (a running
		 * domain is never re-muxed).  Default = the driver's own default for vcodecd (N2VC_VENC_SLOW: 273 MHz). */
		unsigned parent = (flags & 2u) ? N2VC_VENC_PARENT_SLOW : (flags & 4u) ? N2VC_VENC_PARENT_FAST :
		    (N2VC_VENC_SLOW ? N2VC_VENC_PARENT_SLOW : N2VC_VENC_PARENT_FAST);
		if ((flags & 6u) && (n2vc.refs[RWVC_BLK_VENC] || n2vc.refs[RWVC_BLK_VENC_C1] ||
		    n2vc_status_on(&n2vc_blocks[RWVC_BLK_VENC]))) {
			printf("NORD2 VCODEC: venc_sel not changed: VEN is on or held (refs %u)\n", n2vc.refs[RWVC_BLK_VENC]);
			res = N2VC_AT_E_BUSY;
			break;
		}
		if (!n2vc_at_file.refs[RWVC_BLK_VENC]) { err = n2vc_acquire_ex(RWVC_BLK_VENC, &n2vc_at_file, false, parent); }
		if (!err && c1 && !n2vc_at_file.refs[RWVC_BLK_VENC_C1]) {
			vm_offset_t spm = n2vc_pa(N2VC_SPM_PA, 0x1000, 1);
			if (spm && !(ni_rd(spm, N2VC_PWR_STA) & ni_rd(spm, N2VC_PWR_STA2) & N2VC_MDP_BIT)) { res = N2VC_AT_E_MDP; }
			else { err = n2vc_acquire_ex(RWVC_BLK_VENC_C1, &n2vc_at_file, false, parent); }
		}
		if (res == N2VC_AT_OK) { res = n2vc_at_err(err); }
		n2vc_at_dump("power", flags);
		break;
	}
	case 2:
		res = n2vc_at_bus(RWVC_BLK_VENC);
		if (res == N2VC_AT_OK && c1) { res = n2vc_at_bus(RWVC_BLK_VENC_C1); }
		break;
	case 3:
		res = n2vc_at_regs(blk, r);
		break;
	default: {
		int err = 0;
		while (n2vc_at_file.refs[RWVC_BLK_VENC_C1] && !err) { err = n2vc_release_locked(RWVC_BLK_VENC_C1, &n2vc_at_file, false); }
		while (n2vc_at_file.refs[RWVC_BLK_VENC] && !err) { err = n2vc_release_locked(RWVC_BLK_VENC, &n2vc_at_file, false); }
		res = err ? N2VC_AT_E_POWER : N2VC_AT_OK;
		n2vc_at_dump("off", 1u);
		break;
	}
	}
	printf("NORD2 VCODEC: at step %u result %u\n", step, res);
	n2vc_unlock();
	return res;
}

/* 0311: the JPEG autotest (command 7) step 4 powers VEN off by itself; nord2_autotest.inc refuses it while this is non-zero */
uint32_t
n2vc_ven_refs(void)
{
	n2vc_lock();
	uint32_t n = n2vc.refs[RWVC_BLK_VENC];
	n2vc_unlock();
	return n;
}
