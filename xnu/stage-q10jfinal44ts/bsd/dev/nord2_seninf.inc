/*
 * nord2_seninf.inc - the MT6893 (ISP6s) CSI-2 receiver for the Nord 2's cameras.
 *
 * The register layer is MediaTek's own ISP6s seninf driver (GPL-2.0, TCLOpenSource/mt9653
 * drivers/media/platform/mtk-isp/isp_60s/cam/mtk_cam-seninf-hw.c and its register headers), unchanged but for the
 * Linux shims in n2seninf/compat.h. The addresses are the Nord 2's (OnePlus kernel mt6885/seninf/seninf_cfg.h):
 * SENINF 0x1A004000 (+0xA000), MIPI RX analog 0x11C80000 (+0x10000).
 * Power and clocks (OnePlus clk-mt6885-pg.c / clk-mt6885.c): the CAM MTCMOS (SPM 0x35c, status bit 23, bus
 * protection steps), topckgen cam_sel (CLK_CFG_2 bits 16-19 = 3 univpll_d4, update bit 10), seninf/seninf1-3_sel
 * (CLK_CFG_10 bits 16/24, CLK_CFG_11 bits 0/8 = 5 univpll_d6 416 MHz, CLK_CFG_UPDATE1 bits 11-14), camsys_main CG
 * (0x1A000000, clear 0x8: larb13 0, dfp 1, larb14 2, larb15 3, cam 6, camtg 7, seninf 8, camsv0-3 9-12).
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wformat"
#pragma clang diagnostic ignored "-Wpointer-arith"
#pragma clang diagnostic ignored "-Wgnu-pointer-arith"
#pragma clang diagnostic ignored "-Wvoid-pointer-to-int-cast"
#define PAGE_SIZE_N2SENINF 4096
#include "n2seninf/seninf_hw.c"
#pragma clang diagnostic pop

static vm_offset_t n2s_if, n2s_ana, n2s_spm, n2s_infra, n2s_camsys, n2s_mdp, n2s_camsub, n2s_dispsmi, n2s_larb13, n2s_larb14;
static struct seninf_ctx n2s_ctx;

static bool
n2s_wait(vm_offset_t b, uint32_t off, uint32_t mask, uint32_t want)
{
	for (unsigned i = 0; i < 2000; i++) { if ((ni_rd(b, off) & mask) == want) { return true; } delay(10); }
	return false;
}


/*
 * The MDP domain and the SMI path (measured on stock Android 2026-11-09, gpu/build/cam-android-regs-20261109/REPORT.md):
 * larb13 goes out through mdp_smi_common (0x1f002000), larb13 and larb14 both through sysram_smi_common (0x1f022000), and the
 * camsys registers themselves sit behind the MDP bus. spm_mtcmos_ctrl_mdp (clk-mt6885-pg.c): MDP_PWR_CON 0x34c, status bit 19,
 * then its bus protection; mdp_sel = CLK_CFG_1 bits 8-11 = 3, update bit 5; mdpsys CG1 (0x110, clear 0x118): APMCU_GALS 1,
 * SMI0 4, SMI1 8, SMI2 12. The SMI values are smi_conf.h (mt6885) = the stock readback.
 */
static bool
n2s_mdp_power(void)
{
	const uint32_t con = 0x34c, sta = 1u << 19;
	ni_wr(n2s_spm, 0x000, (0xb16u << 16) | 1u);
	printf("NORD2 SENINF: before: pwr status %#x/%#x mdp con %#x cam con %#x prot %#x 1 %#x mm %#x mm2 %#x 2 %#x vdnr %#x CLK_CFG_1 %#x\n",
	    ni_rd(n2s_spm, 0x16c), ni_rd(n2s_spm, 0x170), ni_rd(n2s_spm, con), ni_rd(n2s_spm, 0x35c), ni_rd(n2s_infra, 0x228), ni_rd(n2s_infra, 0x258),
	    ni_rd(n2s_infra, 0x2ec), ni_rd(n2s_infra, 0xdd8), ni_rd(n2s_infra, 0x724), ni_rd(n2s_infra, 0xbc4), ni_rd(n2c_top, 0x20));
	/* mdp_sel = mainpll_d4_d2 (stock: CLK_CFG_1 0x09090304), ungated */
	ni_wr(n2c_top, 0x28, 0x8fu << 8); ni_wr(n2c_top, 0x24, 3u << 8); ni_wr(n2c_top, 0x04, 1u << 5);
	if ((ni_rd(n2s_spm, 0x16c) & sta) && (ni_rd(n2s_spm, 0x170) & sta)) {
		printf("NORD2 SENINF: MDP domain already on (con %#x)\n", ni_rd(n2s_spm, con));
	} else {
		ni_mask(n2s_spm, con, 1u << 2, 1u << 2);
		ni_mask(n2s_spm, con, 1u << 3, 1u << 3);
		if (!n2s_wait(n2s_spm, 0x16c, sta, sta) || !n2s_wait(n2s_spm, 0x170, sta, sta)) { printf("NORD2 SENINF: MDP power status timeout\n"); return false; }
		ni_mask(n2s_spm, con, 1u << 4, 0);
		ni_mask(n2s_spm, con, 1u << 1, 0);
		ni_mask(n2s_spm, con, 1u << 0, 1u << 0);
		ni_mask(n2s_spm, con, 1u << 8, 0);
		if (!n2s_wait(n2s_spm, con, 1u << 12, 0)) { printf("NORD2 SENINF: MDP SRAM ack timeout\n"); return false; }
	}
	/* bus protection off: step 2 (0..3), then step 1 (0..2) */
	ni_wr(n2s_infra, 0x2a4, 1u << 23);
	ni_wr(n2s_infra, 0x2d8, (1u << 3) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 19) | (1u << 23) | (1u << 29) | (1u << 31));
	ni_wr(n2s_infra, 0xdd0, (1u << 1) | (1u << 7) | (1u << 9) | (1u << 11));
	ni_wr(n2s_infra, 0xbbc, 1u << 20);
	ni_wr(n2s_infra, 0x2a4, 1u << 10);
	ni_wr(n2s_infra, 0x2d8, (1u << 2) | (1u << 4) | (1u << 6) | (1u << 8) | (1u << 18) | (1u << 22) | (1u << 28) | (1u << 30));
	ni_wr(n2s_infra, 0xdd0, (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 8));
	delay(100);
	ni_wr(n2s_mdp, 0x118, (1u << 1) | (1u << 4) | (1u << 8) | (1u << 12));
	delay(100);
	printf("NORD2 SENINF: MDP on: con %#x status %#x/%#x prot %#x mm %#x mm2 %#x vdnr %#x mdp CG %#x/%#x\n", ni_rd(n2s_spm, con), ni_rd(n2s_spm, 0x16c),
	    ni_rd(n2s_spm, 0x170), ni_rd(n2s_infra, 0x228), ni_rd(n2s_infra, 0x2ec), ni_rd(n2s_infra, 0xdd8), ni_rd(n2s_infra, 0xbc4),
	    ni_rd(n2s_mdp, 0x100), ni_rd(n2s_mdp, 0x110));
	return true;
}

static void
n2s_smi_common(vm_offset_t b)
{
	ni_wr(b, 0x100, 0xb); ni_wr(b, 0x220, 0x4514); ni_wr(b, 0x234, 0x0e100e10); ni_wr(b, 0x238, 0x0506090a); ni_wr(b, 0x23c, 0x0506090a);
	ni_wr(b, 0x300, 0x4f1); ni_wr(b, 0x444, 1);
}
static void
n2s_smi_subcom(vm_offset_t b)
{
	ni_wr(b, 0x100, 0xa); ni_wr(b, 0x244, 0x2105); ni_wr(b, 0x300, 0x4f1); ni_wr(b, 0x444, 1);
}
static void
n2s_smi_larb(vm_offset_t b, const uint8_t *ostd, unsigned ports)
{
	ni_wr(b, 0x24, 0x370256); ni_wr(b, 0x40, 1); ni_wr(b, 0xf0, 1); ni_wr(b, 0x524, 0x370256); ni_wr(b, 0x5f0, 1);
	for (unsigned p = 0; p < ports; p++) { ni_wr(b, 0x200 + 4 * p, ostd[p]); }
	for (unsigned p = 0; p < 32; p++) { ni_mask(b, 0x380 + 4 * p, 1u, 0); }    /* IOMMU off: the DMA takes physical addresses */
}
/* after MDP and CAM are on and the CAM clocks run */
static void
n2s_smi(void)
{
	static const uint8_t o13[12] = { 2, 0xc, 0xc, 0xe, 6, 6, 6, 6, 6, 0x12, 6, 1 }, o14[6] = { 2, 0xc, 0xc, 0x28, 0x12, 6 };
	printf("NORD2 SENINF: SMI before: mdp common %#x/%#x sysram %#x/%#x disp common %#x/%#x cam subcom %#x %#x %#x larb13 %#x/%#x larb14 %#x/%#x\n",
	    ni_rd(n2s_mdp, 0x2100), ni_rd(n2s_mdp, 0x2220), ni_rd(n2s_mdp, 0x22100), ni_rd(n2s_mdp, 0x22220), ni_rd(n2s_dispsmi, 0x100),
	    ni_rd(n2s_dispsmi, 0x220), ni_rd(n2s_camsub, 0x100), ni_rd(n2s_camsub, 0x1100), ni_rd(n2s_camsub, 0x2100), ni_rd(n2s_larb13, 0x24),
	    ni_rd(n2s_larb13, 0x398), ni_rd(n2s_larb14, 0x24), ni_rd(n2s_larb14, 0x38c));
	n2s_smi_common(n2s_mdp + 0x2000);
	ni_wr(n2s_mdp, 0x22100, 2); ni_wr(n2s_mdp, 0x22220, 0x1111); ni_wr(n2s_mdp, 0x22300, 0x4f1); ni_wr(n2s_mdp, 0x22444, 1);   /* sysram */
	n2s_smi_subcom(n2s_mdp + 0x23000); n2s_smi_subcom(n2s_mdp + 0x24000);
	if (ni_rd(n2s_dispsmi, 0x220) != 0x4514) { n2s_smi_common(n2s_dispsmi); }       /* the display's: only when not set up */
	if (ni_rd(n2s_dispsmi, 0x1100) != 0xa) { n2s_smi_subcom(n2s_dispsmi + 0x1000); }
	if (ni_rd(n2s_dispsmi, 0x2100) != 0xa) { n2s_smi_subcom(n2s_dispsmi + 0x2000); }
	n2s_smi_subcom(n2s_camsub); n2s_smi_subcom(n2s_camsub + 0x1000); n2s_smi_subcom(n2s_camsub + 0x2000);
	n2s_smi_larb(n2s_larb13, o13, 12); n2s_smi_larb(n2s_larb14, o14, 6);
	printf("NORD2 SENINF: SMI after: mdp common %#x/%#x sysram %#x disp common %#x/%#x larb13 stat %#x larb14 stat %#x port3 %#x\n",
	    ni_rd(n2s_mdp, 0x2100), ni_rd(n2s_mdp, 0x2220), ni_rd(n2s_mdp, 0x22220), ni_rd(n2s_dispsmi, 0x100), ni_rd(n2s_dispsmi, 0x220),
	    ni_rd(n2s_larb13, 0x0), ni_rd(n2s_larb14, 0x0), ni_rd(n2s_larb14, 0x38c));
}

/* the CAM MTCMOS: spm_mtcmos_ctrl_cam (clk-mt6885-pg.c), STA_POWER_ON */
static bool
n2s_cam_power(void)
{
	const uint32_t con = 0x35c, sta = 1u << 23;
	ni_wr(n2s_spm, 0x000, (0xb16u << 16) | 1u);                      /* POWERON_CONFIG_EN key */
	if ((ni_rd(n2s_spm, 0x16c) & sta) && (ni_rd(n2s_spm, 0x170) & sta)) {
		printf("NORD2 SENINF: CAM domain already on (con %#x)\n", ni_rd(n2s_spm, con));
	} else {
		printf("NORD2 SENINF: CAM domain on (con %#x)\n", ni_rd(n2s_spm, con));
		ni_mask(n2s_spm, con, 1u << 2, 1u << 2);                  /* PWR_ON */
		ni_mask(n2s_spm, con, 1u << 3, 1u << 3);                  /* PWR_ON_2ND */
		if (!n2s_wait(n2s_spm, 0x16c, sta, sta) || !n2s_wait(n2s_spm, 0x170, sta, sta)) {
			printf("NORD2 SENINF: CAM power status timeout\n");
			return false;
		}
		ni_mask(n2s_spm, con, 1u << 4, 0);                        /* PWR_CLK_DIS */
		ni_mask(n2s_spm, con, 1u << 1, 0);                        /* PWR_ISO */
		ni_mask(n2s_spm, con, 1u << 0, 1u << 0);                  /* PWR_RST_B */
		ni_mask(n2s_spm, con, 1u << 8, 0);                        /* SRAM_PDN */
		if (!n2s_wait(n2s_spm, con, 1u << 12, 0)) { printf("NORD2 SENINF: CAM SRAM ack timeout\n"); return false; }
	}
#ifdef N2S_KEEP_PROT
	/* q10g6..g8: clearing the bus protection with the MDP domain off killed the system 27-44 s later. */
	printf("NORD2 SENINF: CAM bus protection left alone: prot1 %#x/%#x mm %#x/%#x prot2 %#x/%#x\n", ni_rd(n2s_infra, 0x250),
	    ni_rd(n2s_infra, 0x258), ni_rd(n2s_infra, 0x2d0), ni_rd(n2s_infra, 0x2ec), ni_rd(n2s_infra, 0x710), ni_rd(n2s_infra, 0x724));
	if (1) { return true; }
#endif
	printf("NORD2 SENINF: CAM bus protection off\n");
	ni_wr(n2s_infra, 0x2ac, 1u << 22);                               /* PROTECTEN_1_CLR: STEP2_0 */
	ni_wr(n2s_infra, 0x2d8, (1u << 1) | (1u << 3) | (1u << 5));      /* PROTECTEN_MM_CLR: STEP2_1 */
	ni_wr(n2s_infra, 0x718, 1u << 1);                                /* PROTECTEN_2_CLR: STEP1_0 */
	ni_wr(n2s_infra, 0x2d8, (1u << 0) | (1u << 2) | (1u << 4));      /* PROTECTEN_MM_CLR: STEP1_1 */
	delay(200);
	printf("NORD2 SENINF: CAM on: con %#x status %#x/%#x prot1 sta %#x mm sta %#x prot2 sta %#x (bit1 must be 0)\n", ni_rd(n2s_spm, con),
	    ni_rd(n2s_spm, 0x16c), ni_rd(n2s_spm, 0x170), ni_rd(n2s_infra, 0x258), ni_rd(n2s_infra, 0x2ec), ni_rd(n2s_infra, 0x724));
	return true;
}

static void __attribute__((unused))
n2s_clock_muxes(void)
{
	ni_wr(n2c_top, 0x38, 0x8fu << 16); ni_wr(n2c_top, 0x34, 3u << 16); ni_wr(n2c_top, 0x04, 1u << 10);
	ni_wr(n2c_top, 0xb8, (0x87u << 16) | (0x87u << 24)); ni_wr(n2c_top, 0xb4, (5u << 16) | (5u << 24));
	ni_wr(n2c_top, 0xc8, 0x87u | (0x87u << 8)); ni_wr(n2c_top, 0xc4, 5u | (5u << 8));
	ni_wr(n2c_top, 0x08, 0xfu << 11);
	printf("NORD2 SENINF: muxes: CLK_CFG_2 %#x CLK_CFG_10 %#x CLK_CFG_11 %#x\n", ni_rd(n2c_top, 0x30), ni_rd(n2c_top, 0xb0), ni_rd(n2c_top, 0xc0));
}
static void __attribute__((unused))
n2s_clock_gates(void)
{
	ni_wr(n2s_camsys, 0x8, 0x1fcfu);
	printf("NORD2 SENINF: camsys CG %#x\n", ni_rd(n2s_camsys, 0x0));
}
static void __attribute__((unused))
n2s_clocks(void)
{
	/* cam_sel = univpll_d4 */
	ni_wr(n2c_top, 0x38, 0x8fu << 16); ni_wr(n2c_top, 0x34, 3u << 16); ni_wr(n2c_top, 0x04, 1u << 10);
	/* seninf_sel, seninf1_sel = univpll_d6 (416 MHz) */
	ni_wr(n2c_top, 0xb8, (0x87u << 16) | (0x87u << 24)); ni_wr(n2c_top, 0xb4, (5u << 16) | (5u << 24));
	/* seninf2_sel, seninf3_sel */
	ni_wr(n2c_top, 0xc8, 0x87u | (0x87u << 8)); ni_wr(n2c_top, 0xc4, 5u | (5u << 8));
	ni_wr(n2c_top, 0x08, 0xfu << 11);
	/* camsys_main gates: larb13-15, dfp, cam, camtg, seninf, camsv0-3 */
	ni_wr(n2s_camsys, 0x8, 0x1fcfu);
	printf("NORD2 SENINF: clocks: CLK_CFG_2 %#x CLK_CFG_10 %#x CLK_CFG_11 %#x camsys CG %#x\n", ni_rd(n2c_top, 0x30),
	    ni_rd(n2c_top, 0xb0), ni_rd(n2c_top, 0xc0), ni_rd(n2s_camsys, 0x0));
}

/* CAM's path to memory (clk-mt6885-pg.c CAM steps). Off only while a capture runs: q10g5/g6 died 26-44 s after it was
 * cleared for good; q10gb: with it on, the CAMSV DMA never leaves reset. */
static void __attribute__((unused))
n2s_cam_bus(bool open)
{
	if (open) {
		ni_wr(n2s_infra, 0x2ac, 1u << 22); ni_wr(n2s_infra, 0x2d8, (1u << 1) | (1u << 3) | (1u << 5));
		ni_wr(n2s_infra, 0x718, 1u << 1);  ni_wr(n2s_infra, 0x2d8, (1u << 0) | (1u << 2) | (1u << 4));
	} else {
		ni_wr(n2s_infra, 0x714, 1u << 1);  (void)n2s_wait(n2s_infra, 0x724, 1u << 1, 1u << 1);
		ni_wr(n2s_infra, 0x2d4, (1u << 0) | (1u << 2) | (1u << 4)); (void)n2s_wait(n2s_infra, 0x2ec, 0x15u, 0x15u);
		ni_wr(n2s_infra, 0x2a8, 1u << 22); (void)n2s_wait(n2s_infra, 0x258, 1u << 22, 1u << 22);
		ni_wr(n2s_infra, 0x2d4, (1u << 1) | (1u << 3) | (1u << 5)); (void)n2s_wait(n2s_infra, 0x2ec, 0x2au, 0x2au);
	}
	printf("NORD2 SENINF: CAM bus %s: prot1 %#x mm %#x prot2 %#x\n", open ? "OPEN" : "closed", ni_rd(n2s_infra, 0x258),
	    ni_rd(n2s_infra, 0x2ec), ni_rd(n2s_infra, 0x724));
}

static bool
n2s_init(void)
{
	if (!n2s_if) {
		n2s_if = ml_io_map_unmappable(0x1a004000, 0xa000, VM_WIMG_IO);
		n2s_ana = ml_io_map_unmappable(0x11c80000, 0x10000, VM_WIMG_IO);
		n2s_spm = ml_io_map_unmappable(0x10006000, 0x1000, VM_WIMG_IO);
		n2s_infra = ml_io_map_unmappable(0x10001000, 0x1000, VM_WIMG_IO);
		n2s_camsys = ml_io_map_unmappable(0x1a000000, 0x1000, VM_WIMG_IO);
		n2s_mdp = ml_io_map_unmappable(0x1f000000, 0x25000, VM_WIMG_IO);
		n2s_camsub = ml_io_map_unmappable(0x1a00c000, 0x3000, VM_WIMG_IO);
		n2s_dispsmi = ml_io_map_unmappable(0x1411f000, 0x3000, VM_WIMG_IO);
		n2s_larb13 = ml_io_map_unmappable(0x1a001000, 0x1000, VM_WIMG_IO);
		n2s_larb14 = ml_io_map_unmappable(0x1a002000, 0x1000, VM_WIMG_IO);
	}
#ifdef N2S_BISECT
	/* q10g6: the system died ~73 s after boot whenever the CAM domain was on: one step at a time, a heartbeat between */
	uint64_t t0 = mach_absolute_time();
	for (unsigned s = 0; s < 150; s++) {
		uint64_t ns; absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
		if (s == 0) { printf("NORD2 BISECT: step 1 CAM MTCMOS + bus protection\n"); if (!n2s_cam_power()) { return false; } }
		
		
		printf("NORD2 BISECT: heartbeat %u s (since start %llu ms) spm status %#x\n", s, ns / 1000000ULL, ni_rd(n2s_spm, 0x16c));
		n2c_sleep(1000);
	}
	printf("NORD2 BISECT: step 4 SENINF\n");
#else
	if (!n2s_mdp_power()) { return false; }
	if (!n2s_cam_power()) { return false; }
	n2s_clocks();
	n2s_smi();
#endif
	memset(&n2s_ctx, 0, sizeof(n2s_ctx));
	mtk_cam_seninf_init_iomem(&n2s_ctx, (void *)n2s_if, (void *)n2s_ana);
	printf("NORD2 SENINF: top %#x %#x %#x\n", ni_rd(n2s_if, 0x0), ni_rd(n2s_if, 0x4), ni_rd(n2s_if, 0x8));
	return true;
}

/*
 * The IMX766's receiver: CSI port 2 in C-PHY mode, 3 trios, into SENINF5. The TCL driver's C-PHY path kills this SoC
 * (q10gd/gf/gg: dead inside mtk_cam_seninf_set_csi_mipi), so these are the register values of stock Android while the
 * sensor streams (gpu/build/cam-android-regs-20261109/imx766/ana2.log, sen_8.log), written block by block.
 */
struct n2s_rv { uint16_t off; uint32_t val; };
static void
n2s_replay(const char *what, vm_offset_t base, const struct n2s_rv *t, unsigned n)
{
	(void)what;
	for (unsigned i = 0; i < n; i++) { ni_wr(base, t[i].off, t[i].val); }
}
static void
n2s_rx_cphy2(void)
{
	static const struct n2s_rv ana_a[] = { { 0x04, 0x00420844 }, { 0x08, 0x44001111 }, { 0x0c, 0x02001010 }, { 0x10, 0x00001111 }, { 0x14, 0x00000057 },
	    { 0x18, 0x04090909 }, { 0x1c, 0x04090909 }, { 0x20, 0x203f413f }, { 0x24, 0x30030000 }, { 0x40, 0x03330000 }, { 0x54, 0x00000111 },
	    { 0x60, 0x03330000 }, { 0x80, 0x00220000 }, { 0xf0, 0x000200e2 }, { 0x00, 0x00170013 } };
	static const struct n2s_rv ana_b[] = { { 0x04, 0x00420844 }, { 0x08, 0x44000f0f }, { 0x0c, 0x02001010 }, { 0x10, 0x00001111 }, { 0x14, 0x00000057 },
	    { 0x18, 0x04090909 }, { 0x1c, 0x04090909 }, { 0x20, 0x203f643f }, { 0x24, 0x30030000 }, { 0x40, 0x00130000 }, { 0x54, 0x00000111 },
	    { 0x70, 0x00130000 }, { 0x90, 0x01010000 }, { 0xf0, 0x000200e2 }, { 0x00, 0x00170013 } };
	static const struct n2s_rv dphy[] = { { 0x10, 0x10130000 }, { 0x14, 0x10130000 }, { 0x20, 0x10273002 }, { 0x24, 0x10273002 }, { 0x28, 0x10273002 },
	    { 0x2c, 0x10273002 } };
	static const struct n2s_rv cphy[] = { { 0x08, 0x44440012 }, { 0x10, 0x0e492300 }, { 0x14, 0x0e492301 }, { 0x18, 0x12492410 }, { 0x24, 0x00050000 },
	    { 0x34, 0x01050000 }, { 0x44, 0x0f010000 }, { 0x54, 0x09000000 }, { 0xe4, 0x00010101 }, { 0x00, 0x00000007 } };
	static const struct n2s_rv sen5[] = { { 0xa04, 0x001f0037 }, { 0xa08, 0x00000402 }, { 0xa10, 0x00000106 }, { 0xa00, 0x00000007 }, { 0x200, 0x00000001 },
	    { 0x210, 0x00000001 } };
	n2s_replay("CSI2A analog", n2s_ana + 0x8000, ana_a, sizeof(ana_a) / sizeof(ana_a[0]));
	n2s_replay("CSI2B analog", n2s_ana + 0x9000, ana_b, sizeof(ana_b) / sizeof(ana_b[0]));
	n2s_replay("CSI2 D-PHY top", n2s_ana + 0xa000, dphy, sizeof(dphy) / sizeof(dphy[0]));
	n2s_replay("CSI2 C-PHY top", n2s_ana + 0xb000, cphy, sizeof(cphy) / sizeof(cphy[0]));
	ni_wr(n2s_if, 0x68, 2);
	n2s_replay("SENINF5 CSI2", n2s_if + 0x4000, sen5, sizeof(sen5) / sizeof(sen5[0]));
	printf("NORD2 SENINF: replay done: ana %#x %#x cphy %#x seninf5 %#x %#x %#x\n", ni_rd(n2s_ana, 0x8000), ni_rd(n2s_ana, 0x9000), ni_rd(n2s_ana, 0xb000),
	    ni_rd(n2s_if, 0x4a00), ni_rd(n2s_if, 0x4a04), ni_rd(n2s_if, 0x4a10));
}

/*
 * receive on a CSI port (4D1C ports 0..3): D-PHY 4 lanes or C-PHY 3 trios; RAW10 on VC0 into the SENINF mux `mux`.
 * 0292: the context and the mux are the caller's (two receivers at once: one context each, different muxes).
 */
static void
n2s_rx_x(struct seninf_ctx *c, int mux, int port, bool cphy, int lanes, int64_t pixel_rate)
{
	mtk_cam_seninf_init_port(c, port);
	c->is_cphy = cphy;
	c->num_data_lanes = lanes;
	c->mipi_pixel_rate = pixel_rate;
	c->fps_n = 0; c->fps_d = 0;
	memset(&c->vcinfo, 0, sizeof(c->vcinfo));
	c->vcinfo.cnt = 1;
	c->vcinfo.vc[0].vc = 0; c->vcinfo.vc[0].dt = 0x2b; c->vcinfo.vc[0].group = 0; c->vcinfo.vc[0].pixel_mode = 2;
#define N2S_STEP(x) do { } while (0)
	N2S_STEP("reset"); mtk_cam_seninf_reset(c, c->seninfIdx);
	N2S_STEP("set_vc"); mtk_cam_seninf_set_vc(c, c->seninfIdx, &c->vcinfo);
	if (cphy && port == CSI_PORT_2) { n2s_rx_cphy2(); } else { mtk_cam_seninf_set_csi_mipi(c); }
	N2S_STEP("mux"); mtk_cam_seninf_mux(c, mux);
	mtk_cam_seninf_set_mux_ctrl(c, mux, 0, 0, MIPI_SENSOR, 2);
	N2S_STEP("top mux");
	mtk_cam_seninf_set_top_mux_ctrl(c, mux, c->seninfIdx);
}
static void
n2s_rx(int port, bool cphy, int lanes, int64_t pixel_rate)
{
	n2s_rx_x(&n2s_ctx, SENINF_MUX1, port, cphy, lanes, pixel_rate);
}

static void
n2s_report_x(struct seninf_ctx *c, int muxi, const char *what)
{
	vm_offset_t csi2 = (vm_offset_t)c->reg_if_csi2[c->seninfIdx], mux = (vm_offset_t)c->reg_if_mux[muxi];
	struct mtk_cam_seninf_mux_meter m;
	mtk_cam_seninf_get_mux_meter(c, muxi, &m);
	printf("NORD2 SENINF: %s port %d seninf%d mux%d: csi2 irq %#x lines/frames %#x pkt %#x pktcnt %#x | mux ctrl %#x irq %#x size %ux%u hv %u hb %u vv %u vb %u | top mux %#x %#x cam mux src %#x %#x en %#x\n",
	    what, c->port, c->seninfIdx + 1, muxi + 1, ni_rd(csi2, 0xc8), ni_rd(csi2, 0xd0), ni_rd(csi2, 0xd4), ni_rd(csi2, 0xdc),
	    ni_rd(mux, 0x0), ni_rd(mux, 0x18), m.width, m.height, m.h_valid, m.h_blank, m.v_valid, m.v_blank,
	    ni_rd(n2s_if, 0x10), ni_rd(n2s_if, 0x14), ni_rd(n2s_if, 0x400), ni_rd(n2s_if, 0x404), ni_rd(n2s_if, 0x410));
	ni_wr(csi2, 0xc8, 0xffffffffu);      /* write-1-clear */
	ni_wr(mux, 0x18, 0xffffffffu);
}
static void
n2s_report(const char *what)
{
	n2s_report_x(&n2s_ctx, SENINF_MUX1, what);
}
