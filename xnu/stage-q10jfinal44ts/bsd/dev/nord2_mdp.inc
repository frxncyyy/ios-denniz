/* SPDX-License-Identifier: MIT */
/*
 * nord2_mdp.inc -- 0670: MT6893 MDP (Media Data Path, mdpsys 0x1f000000) probe = stage 0 of the MDP offload plan;
 * 0671 adds the functional stages 1-3 (DMA self-test, function matrix, M4U1) and /dev/redwood-mdp (see the 0671 block below);
 * 0673 adds the bring-up stage P (owned-protection model + vendor-order release of exclusive MDP bits + MDP gates, see the
 * 0673 blocks) and lets the probe read mdpsys once P verified that no MDP-owned protection bit is set;
 * 0674 adds the larb2 SMI bring-up before S1, mutex end-of-frame as a second done indicator and full pipe dumps;
 * 0675: larb2 setup optional (OSTDL never lowered, byte-mirrored read-back, S1 refused only for a real blocker), short log lines;
 * 0676: vendor RDMA buffer words / RDMA_CON / WROT FIFO_TEST, ROT_EN frame bits, INT clear, main-buffer lines (job.h) + retry ladder;
 * 0677: vendor WROT start offsets for rotation / mirror (job.h), guard below the destination, mismatch write map;
 * 0678: S2 = NV12 conversion / rotation / scaling with the vendor NV12 RDMA words, RSZ values, TCC relay (job.h); stops at the
 * first failing S2 job; default kern.n2mdp_stages 0x102;
 * 0679: AAL2 / TDSHP2 tile sizes (job.h), scale-pipe engine dump in the diagnostics;
 * 0680: TDSHP2 CTRL enable + CFG FIFO/relay (job.h), selector/mutex readback in the scale dump;
 * 0681: multi-tile and RGB-source scaling refused (known limitation, job.h); the scaletile test is gone from S2;
 * 0682: S2 DIAG jobs (rgbscale1, nv12scale3t, nv12scale3o, rgbscale3t) that tell RGB-source from multi-tile problems; vendor RSZ
 * tile bits 11/12 for multi-tile jobs;
 * 0683: NV12 multi-tile scaling allowed; RGB-source scaling behind N2MDP_CAP_RGB_SCALE (smooth-pattern jobs); default stages 0x103;
 * kern.n2mdp_publish re-offers the 0672 service (registration still needs kern.n2mdp_svc = 1, default 0);
 * 0686: NV12 destinations (WROT 2-plane, vendor chroma offsets / uv table) behind N2MDP_CAP_NV12_DST, 20 us WAIT polling;
 * 0687: service speed: IOVA mapping cache (8 entries, page-list hash), page-table flush of the touched range only, 5 us WAIT poll
 * for the first 3 ms, map / run timing for the service log;
 * 0688: MDP clock (mdp_sel parent, TVDPLL rate) in the probe log, compile timing;
 * 0689: per-tile hw times, MDP lock wait, S2 TIMING jobs (rot 0 vs rot 90, tile widths 512..64 px);
 * 0690: two pipes at once: unscaled jobs also run on RDMA2 -> WROT2 (direct lane, job.h), one lock per pipe + a map lock + a GCE
 * token spin lock (the global MDP lock became a rw lock: stages / probe exclusive, service blits shared), resumable executor with
 * per-run statistics (the 0689 globals mixed the two request streams), S2 pipe-2 capability jobs + a dual-pipe job (both pipes at
 * once, exact) -> N2MDP_CAP_DUAL; S3 TIMING jobs (same frame physical vs IOVA, IOVA tile widths) with the vendor M4U1 write
 * ordering / throttle setup measured before / after (kept only if not slower) and the best IOVA rotation tile width for the
 * service; IOVA cache entries pinned while a job uses them (eviction / fault drop never unmaps a range in use);
 * 0691: q10jfinal19 phone: a mediaanalysisd 3088x2320 420f -> 2016x1514 420f downscale (scale lane, 788-px input tiles, NV12
 * destination: a combination no boot job ever ran) stalled; the abort left WROT2 stuck in soft reset (next jobs: poll timeout at
 * op 13 = WROT2 RST_STAT), 3 failures -> the whole service (camera too) shut down.  Now: abort recovery as the vendor does it
 * (cmdqMdpResetEng: WROT release polled; engines that do not come back -> MDPSYS SW0_RST_B pulse for the lane + MOUT reset),
 * a lane that stays dead is never used again; scaling only inside what S2 verified (input tile <= 512 px, RGB destinations;
 * frames above 1024x512 need the new S2 scbig job = N2MDP_CAP_SCALE_BIG), any scaled failure switches scaling off for the boot
 * (camera rotations untouched); the service no longer counts scaled failures toward its automatic shutdown.
 * (gpu/build/mdp-20261114/MDP_OFFLOAD.md).  Answers "is the MDP powered, clocked and reachable on this phone, and in what
 * state did the bootloader leave it" before any functional stage (DMA, scaling, rotation) is attempted.
 *
 * Included once by bsd/dev/sweet_touch.c after nord2_vcodec_at.inc (uses ni_rd/ni_wr only).  Own thread, own mappings
 * (ml_io_map_unmappable: no io_submap page), never touches the PMIC/pwrap bus, the display, the codec blocks or any power
 * domain.  Sources of every number: nord2-current-kernel drivers/misc/mediatek/mdp/mt6893/{mdp_base.h,mdp.c},
 * drivers/clk/mediatek/{clk-mt6885-mdp.c,clk-mt6885-pg.c,clk-mt6885.c}, drivers/mailbox/mtk-cmdq-mailbox.c,
 * include/dt-bindings/memory/mt6885-larb-port.h, ios-sweet/dtb/nord2/live.dts (path:line in MDP_OFFLOAD.md section 2).
 *
 * Levels (kern.n2mdp_probe, written by a payload or 0 at boot; the boot probe runs at +45 s with N2MDP_BOOT_LEVEL):
 *   1 = READ-ONLY.  SPM power status, infracfg bus protection, topckgen mdp_sel, infracfg GCE clock gates.  Only if the MDP
 *       domain is on (PWR_STATUS and _2ND bit 19), its bus protection released and mdp_sel ungated: mdpsys CG_CON0..2 and the
 *       topology (MOUT/SEL) registers; then the registers of every engine whose own clock gate is open (RDMA0-3, RSZ0-3,
 *       WROT0-3, mutex), larb2/larb3 port MMU bits and M4U1 only if the MDP SMI clocks are open.  GCE-D/GCE-M thread
 *       state only if their infracfg gate is open.  No register is written.
 *   2 = level 1, then opens (CG_CLR) the clocks RDMA0, RSZ0, WROT0, MUTEX0, APB bus and the MDP SMI clocks (0x1112 =
 *       exactly what Linux writes at MDP power-on, clk-mt6885-pg.c:1147-1149) that were closed, reads the same engine
 *       registers again, and closes (CG_SET) exactly the gates it opened.  Writes only mdpsys CG_SET/CG_CLR registers.
 *       Never enables an engine, never starts a DMA, never writes a larb / M4U / GCE / SPM / infracfg register.
 * Log: "NORD2 MDP: ..." lines and a final "NORD2 MDP VERDICT: power=.. prot=.. mdp_sel=.. apb=.. smi=.. engines_open=..
 *       rdma0_en=.. wrot0_en=.. larb2_mmu=.. m4u1_pt=.. gced_thr=.. gcem_thr=.. lvl=..".  kern.n2mdp_state = verdict bits.
 * Kill switch: kern.n2mdp_probe = -1 before +45 s (payload) or build without 0670 (nothing else depends on it).
 * Host test: gpu/build/mdp-20261114/test/t_mdp_probe.c (register model; fails on any MDP read while the domain is off,
 * protected or its mux gated, on any engine read with its gate closed, on any write at level 1, and on a gate left open).
 */
#ifndef N2MDP_BOOT_LEVEL
#define N2MDP_BOOT_LEVEL 1
#endif

#define N2MDP_SPM_PA     0x10006000u
#define N2MDP_TOP_PA     0x10000000u
#define N2MDP_INFRA_PA   0x10001000u
#define N2MDP_SYS_PA     0x1f000000u     /* mdpsys_config; the window covers mutex .. M4U1 .. COLOR1 (0x1f02d000) */
#define N2MDP_SYS_BYTES  0x2e000u
#define N2MDP_GCED_PA    0x10228000u     /* gce_mbox (display GCE), live.dts:4650 */
#define N2MDP_GCEM_PA    0x10318000u     /* gce_mbox_m (MDP GCE), live.dts:5132 */

/* SPM (clk-mt6885-pg.c:167-190) */
#define N2MDP_PWR_STA    0x16cu
#define N2MDP_PWR_STA2   0x170u
#define N2MDP_MDP_CON    0x34cu
#define N2MDP_DIS_CON    0x350u
#define N2MDP_MDP_BIT    (1u << 19)
#define N2MDP_DIS_BIT    (1u << 20)
/* topckgen CLK_CFG_1: mdp_sel bits 11:8, gate bit 15 (clk-mt6885.c; same constant as nord2_vcodec.inc N2VC_MDP_GATE) */
#define N2MDP_CFG1       0x020u
#define N2MDP_MDPSEL_GATE (1u << 15)
/* infracfg_ao bus protection status (clk-mt6885-pg.c:211,229,241) and the MDP masks (pg.c:411-460) */
#define N2MDP_PROT_STA1  0x228u
#define N2MDP_PROT_MM    0x2ecu
#define N2MDP_PROT_MM2   0xdd8u
#define N2MDP_P_STA1     ((1u << 10) | (1u << 23))
#define N2MDP_P_MM       ((1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | \
                          (1u << 18) | (1u << 19) | (1u << 22) | (1u << 23) | (1u << 28) | (1u << 29) | (1u << 30) | (1u << 31))
#define N2MDP_P_MM2      ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 11))
/* infracfg_ao IFRAO0 gates (clk-mt6885.c:2109-2113, 2245-2247, 2279): status 0x90, 1 = gated */
#define N2MDP_IFRAO0_STA 0x090u
#define N2MDP_IFR_GCE    (1u << 8)
#define N2MDP_IFR_GCE2   (1u << 9)
/* mdpsys (mdp.c:830-960, clk-mt6885-mdp.c:26-190): CG_CONn status (1 = gated), +4 SET (gate), +8 CLR (open) */
#define N2MDP_CG0        0x100u
#define N2MDP_CG1        0x110u
#define N2MDP_CG2        0x120u
#define N2MDP_CG0_APB    (1u << 31)
#define N2MDP_CG0_MUTEX0 (1u << 15)
#define N2MDP_CG1_SMI    0x1112u         /* APMCU_GALS bit 1, SMI0 4, SMI1 8, SMI2 12 (GATE_DUMMY in Linux) */
#define N2MDP_OFF_MUTEX  0x1000u
#define N2MDP_OFF_SMI    0x2000u
#define N2MDP_OFF_LARB2  0x3000u
#define N2MDP_OFF_LARB3  0x4000u
#define N2MDP_OFF_M4U1   0x27000u
#define N2MDP_LARB_PORTS 6u              /* mt6885-larb-port.h:54-67: L2 RDMA0,RDMA2,WROT0,WROT2,FG0,FAKE0; L3 RDMA1,3,WROT1,3,FG1,FAKE1 */

struct n2mdp_eng { const char *name; uint32_t off; uint8_t cg; uint8_t kind; };
enum { N2MDP_K_RDMA = 0, N2MDP_K_RSZ = 1, N2MDP_K_WROT = 2 };
/* mdp_base.h (mt6893): bases; clk-mt6885-mdp.c: CG_CON0 bit of each engine */
static const struct n2mdp_eng n2mdp_engs[12] = {
	{ "RDMA0", 0x06000u, 0,  N2MDP_K_RDMA }, { "RDMA1", 0x07000u, 16, N2MDP_K_RDMA },
	{ "RDMA2", 0x08000u, 8,  N2MDP_K_RDMA }, { "RDMA3", 0x09000u, 24, N2MDP_K_RDMA },
	{ "RSZ0",  0x12000u, 4,  N2MDP_K_RSZ },  { "RSZ1",  0x13000u, 20, N2MDP_K_RSZ },
	{ "RSZ2",  0x14000u, 10, N2MDP_K_RSZ },  { "RSZ3",  0x15000u, 26, N2MDP_K_RSZ },
	{ "WROT0", 0x16000u, 7,  N2MDP_K_WROT }, { "WROT1", 0x17000u, 23, N2MDP_K_WROT },
	{ "WROT2", 0x18000u, 14, N2MDP_K_WROT }, { "WROT3", 0x19000u, 30, N2MDP_K_WROT },
};
/* read-only registers per kind (mdp_common.c cmdq_mdp_dump_rdma/rot, mt6893/mdp.c cmdq_mdp_dump_rsz):
 * RDMA 0x000 EN, 0x008 RESET, 0x030 SRC_CON, 0x060 MF_SRC_SIZE, 0x400 MON_STA_0, 0x408 MON_STA_1, 0xf00 SRC_BASE_0
 * RSZ  0x000 ENABLE, 0x004 CONTROL_1, 0x008 CONTROL_2, 0x010 INPUT_IMAGE, 0x014 OUTPUT_IMAGE
 * WROT 0x000 VIDO_CTRL, 0x004 MAIN_BUF_SIZE, 0x008 SOFT_RST, 0x00c SOFT_RST_STAT, 0x024 IN_SIZE, 0xf00 BASE_ADDR */
static const uint16_t n2mdp_regs[3][7] = {
	{ 0x000, 0x008, 0x030, 0x060, 0x400, 0x408, 0xf00 },
	{ 0x000, 0x004, 0x008, 0x010, 0x014, 0xffff, 0xffff },
	{ 0x000, 0x004, 0x008, 0x00c, 0x024, 0xf00, 0xffff },
};
/* topology (mdp.c:870-930): RDMA0..3 MOUT_EN, RSZ0..3 SEL_IN, WROT0..3 SEL_IN, MOUT masks */
static const uint16_t n2mdp_topo[] = { 0xf20, 0xf24, 0xf28, 0xf2c, 0xf60, 0xf64, 0xf68, 0xf6c, 0xf70, 0xf74, 0xf78, 0xf7c,
	0xfd0, 0xfd4, 0xfd8, 0x0f0 };

#ifdef N2MDP_HOST
extern vm_offset_t n2mdp_host_map(uint32_t pa, uint32_t bytes);
#define n2mdp_map(pa, bytes) n2mdp_host_map((pa), (bytes))
#define N2MDP_LOG printf
#else
#define n2mdp_map(pa, bytes) ml_io_map_unmappable((pa), (bytes), VM_WIMG_IO)
#define N2MDP_LOG printf
#endif

static struct {
	vm_offset_t spm, top, infra, sys, gced, gcem;
	uint32_t state;              /* kern.n2mdp_state: verdict bits (see n2mdp_probe) */
	uint32_t runs;
	bool prot_owned_rule;        /* 0673: set by the bring-up stage after it verified the MDP-owned protection bits clear */
} n2mdp;

/* ======================================================================================================================
 * 0673: MDP bus-protection model (clk-mt6885-pg.c:200-262 registers, 300-540 masks, 2921-3110 spm_mtcmos_ctrl_mdp).
 * The infracfg protection registers are SHARED by every MM domain.  MDP's own power-on masks include the bits of the domains
 * that sit behind the MDP SMI common (CAM, CAM_RAWA/C, VEN_CORE1, ...), and the vendor kernel leaves those bits SET whenever
 * such a child domain is off (Android's normal state with the camera / encoder idle).  A set bit of a powered-off child
 * domain therefore does NOT block MDP traffic.  Phone log 2026-11-15 (q10jmdp0/1/2a): STA1 0xe70c508e (MDP bits 10/23
 * clear), MM 0x55554815 -> MDP-mask bits set = 0x50440014 = CAM 2,4 + CAM_RAWA 18 + CAM_RAWC 22 + VEN_CORE1 28,30, MM_2 0x141
 * -> 0x141 = VEN 0 + VDE2 6 + ISP 8; all those domains are off in PWR_STATUS 0x10580fff.  The 0670 probe counted them as
 * "MDP protection SET" (any bit of the MDP masks) and never read mdpsys.
 * Rule used after the bring-up stage verified it: an MDP-mask bit must be clear unless a powered-off other domain owns it.
 * The bring-up stage WRITES only the bits that no other domain shares (exclusive MDP bits) and never a display-domain bit. */
struct n2mdp_preg { const char *name; uint16_t en, set, clr, sta1; };
static const struct n2mdp_preg n2mdp_pregs[4] = {
	{ "PROTECTEN",                0x220u, 0x2a0u, 0x2a4u, 0x228u },
	{ "PROTECTEN_MM",             0x2d0u, 0x2d4u, 0x2d8u, 0x2ecu },
	{ "PROTECTEN_MM_2",           0xdc8u, 0xdccu, 0xdd0u, 0xdd8u },
	{ "PROTECTEN_SUB_INFRA_VDNR", 0xbb4u, 0xbb8u, 0xbbcu, 0xbc4u },
};
/* MDP steps in the vendor power-ON release order (pg.c:3071-3107: step2:0..3 then step1:0..2) */
struct n2mdp_pstep { const char *name; uint8_t reg; uint32_t mask; };
#define N2MDP_NPSTEPS 7u
static const struct n2mdp_pstep n2mdp_psteps[N2MDP_NPSTEPS] = {
	{ "step2:0", 0, 0x00800000u }, { "step2:1", 1, 0xa08802a8u }, { "step2:2", 2, 0x00000a82u }, { "step2:3", 3, 0x00100000u },
	{ "step1:0", 0, 0x00000400u }, { "step1:1", 1, 0x50440154u }, { "step1:2", 2, 0x00000155u },
};
/* vendor power-DOWN order (pg.c:2933-3006: step1:0..2 then step2:0..3) = order used to put released bits back */
static const uint8_t n2mdp_pdown_order[N2MDP_NPSTEPS] = { 4, 5, 6, 0, 1, 2, 3 };
/* other domains' protection bits (step1 | step2) in the registers MDP uses, with their PWR_STATUS bit (pg.c:542-570) */
struct n2mdp_pother { const char *name; uint8_t reg, sta_bit; uint32_t mask; };
static const struct n2mdp_pother n2mdp_pothers[] = {
	{ "ISP", 2, 12, 0x00000300u }, { "VDE2", 2, 16, 0x000000c0u }, { "VEN", 2, 17, 0x00000003u },
	{ "ISP2", 1, 13, 0x0000c000u }, { "IPE", 1, 14, 0x00030000u }, { "VDE", 1, 15, 0x03000000u }, { "VEN", 1, 17, 0x0c000000u },
	{ "VEN_CORE1", 1, 18, 0xf0000000u }, { "CAM", 1, 23, 0x0000003fu }, { "CAM_RAWA", 1, 24, 0x000c0000u },
	{ "CAM_RAWB", 1, 25, 0x00300000u }, { "CAM_RAWC", 1, 26, 0x00c00000u },
};
#define N2MDP_NPOTHERS (sizeof(n2mdp_pothers) / sizeof(n2mdp_pothers[0]))
/* display (DIS, bit 20) domain bits (DIS_PROT_STEP*): never written by this file; set while DIS is on = not usable */
static const uint32_t n2mdp_dis_bits[4] = { 0x00000040u, 0x05115541u | 0x0a228282u, 0u, 0x00200000u };

static uint32_t
n2mdp_prot_mdp(unsigned r)
{
	uint32_t m = 0;
	for (unsigned k = 0; k < N2MDP_NPSTEPS; k++) { if (n2mdp_psteps[k].reg == r) { m |= n2mdp_psteps[k].mask; } }
	return m;
}
/* bits of register r owned by another domain that is powered off (pwr = PWR_STATUS & PWR_STATUS_2ND) */
static uint32_t
n2mdp_prot_child_off(unsigned r, uint32_t pwr)
{
	uint32_t m = 0;
	for (unsigned k = 0; k < N2MDP_NPOTHERS; k++) {
		if (n2mdp_pothers[k].reg == r && !(pwr & (1u << n2mdp_pothers[k].sta_bit))) { m |= n2mdp_pothers[k].mask; }
	}
	return m;
}
/* MDP-mask bits no other domain shares (the only bits the bring-up stage may write) */
static uint32_t
n2mdp_prot_exclusive(unsigned r)
{
	uint32_t m = n2mdp_prot_mdp(r) & ~n2mdp_dis_bits[r];
	for (unsigned k = 0; k < N2MDP_NPOTHERS; k++) { if (n2mdp_pothers[k].reg == r) { m &= ~n2mdp_pothers[k].mask; } }
	return m;
}
/* MDP-mask bits set in v that are not explained by a powered-off child domain: must be 0 before any mdpsys access */
static uint32_t
n2mdp_prot_owned(unsigned r, uint32_t v, uint32_t pwr)
{
	return v & n2mdp_prot_mdp(r) & ~n2mdp_prot_child_off(r, pwr);
}
/* name of the first other domain owning bit b of register r (log only) */
static const char *
n2mdp_prot_owner(unsigned r, unsigned b)
{
	if (n2mdp_dis_bits[r] & (1u << b)) { return "DIS"; }
	for (unsigned k = 0; k < N2MDP_NPOTHERS; k++) {
		if (n2mdp_pothers[k].reg == r && (n2mdp_pothers[k].mask & (1u << b))) { return n2mdp_pothers[k].name; }
	}
	return "MDP only";
}
static unsigned n2mdp_lowbit(uint32_t v) { unsigned b = 0; while (b < 31 && !(v & (1u << b))) { b++; } return b; }
static int n2mdp_req;            /* kern.n2mdp_probe: 1 / 2 = run that level once; -1 = never (kill); cleared after a run */
static int n2mdp_boot_level = N2MDP_BOOT_LEVEL;

static bool
n2mdp_maps(void)
{
	if (!n2mdp.spm) { n2mdp.spm = n2mdp_map(N2MDP_SPM_PA, 0x1000); }
	if (!n2mdp.top) { n2mdp.top = n2mdp_map(N2MDP_TOP_PA, 0x1000); }
	if (!n2mdp.infra) { n2mdp.infra = n2mdp_map(N2MDP_INFRA_PA, 0x1000); }
	if (!n2mdp.sys) { n2mdp.sys = n2mdp_map(N2MDP_SYS_PA, N2MDP_SYS_BYTES); }
	if (!n2mdp.gced) { n2mdp.gced = n2mdp_map(N2MDP_GCED_PA, 0x1000); }
	if (!n2mdp.gcem) { n2mdp.gcem = n2mdp_map(N2MDP_GCEM_PA, 0x1000); }
	return n2mdp.spm && n2mdp.top && n2mdp.infra && n2mdp.sys && n2mdp.gced && n2mdp.gcem;
}

/* engine registers, only for engines whose own gate (cg0 bit) is open; returns how many engines were read */
static unsigned
n2mdp_engines(uint32_t cg0, const char *when, uint32_t *rdma0_en, uint32_t *wrot0_en)
{
	unsigned n = 0;
	for (unsigned i = 0; i < 12; i++) {
		const struct n2mdp_eng *e = &n2mdp_engs[i];
		if (cg0 & (1u << e->cg)) { continue; }
		uint32_t v[7] = { 0 };
		for (unsigned r = 0; r < 7; r++) {
			if (n2mdp_regs[e->kind][r] != 0xffff) { v[r] = ni_rd(n2mdp.sys, e->off + n2mdp_regs[e->kind][r]); }
		}
		if (i == 0) { *rdma0_en = v[0]; }
		if (i == 8) { *wrot0_en = v[0]; }
		N2MDP_LOG("NORD2 MDP: %s %s regs %#x %#x %#x %#x %#x %#x %#x\n", when, e->name, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
		n++;
	}
	return n;
}

/* larb2/larb3 port MMU bits and M4U1 (only with the MDP SMI clocks open); returns larb2 port bit0 mask */
static uint32_t
n2mdp_smi(const char *when, uint32_t *m4u_pt)
{
	uint32_t l2 = 0, l3 = 0;
	for (unsigned p = 0; p < N2MDP_LARB_PORTS; p++) {
		uint32_t a = ni_rd(n2mdp.sys, N2MDP_OFF_LARB2 + 0x380u + 4u * p);
		uint32_t b = ni_rd(n2mdp.sys, N2MDP_OFF_LARB3 + 0x380u + 4u * p);
		if (a & 1u) { l2 |= 1u << p; }
		if (b & 1u) { l3 |= 1u << p; }
		if (p == 0) { N2MDP_LOG("NORD2 MDP: %s larb2 p0 %#x larb3 p0 %#x (NON_SEC_CON: bit0 MMU_EN, 15:8 address bits 32+)\n", when, a, b); }
	}
	*m4u_pt = ni_rd(n2mdp.sys, N2MDP_OFF_M4U1 + 0x000u);
	N2MDP_LOG("NORD2 MDP: %s larb2 MMU_EN ports %#x larb3 %#x | M4U1 PT_BASE %#x CTRL %#x INT_CTRL0 %#x INT_MAIN %#x FAULT_ST1 %#x"
	    " | smi_common 0x0 %#x larb2 STAT %#x\n", when, l2, l3, *m4u_pt, ni_rd(n2mdp.sys, N2MDP_OFF_M4U1 + 0x110u),
	    ni_rd(n2mdp.sys, N2MDP_OFF_M4U1 + 0x120u), ni_rd(n2mdp.sys, N2MDP_OFF_M4U1 + 0x124u),
	    ni_rd(n2mdp.sys, N2MDP_OFF_M4U1 + 0x134u), ni_rd(n2mdp.sys, N2MDP_OFF_SMI + 0x0u), ni_rd(n2mdp.sys, N2MDP_OFF_LARB2 + 0x0u));
	return l2;
}

/* GCE thread table (mtk-cmdq-mailbox.c:32-61): which of the 24 threads are enabled (someone else's GCE use) */
static uint32_t
n2mdp_gce(vm_offset_t g, const char *name)
{
	uint32_t en = 0;
	for (unsigned t = 0; t < 24; t++) {
		if (ni_rd(g, 0x100u + 0x80u * t + 0x04u) & 1u) { en |= 1u << t; }
	}
	N2MDP_LOG("NORD2 MDP: %s IRQ_STATUS %#x LOADED_THR %#x enabled threads %#x\n", name, ni_rd(g, 0x10u), ni_rd(g, 0x18u), en);
	return en;
}

/* state bits */
#define N2MDP_S_RAN      (1u << 0)
#define N2MDP_S_POWER    (1u << 1)
#define N2MDP_S_PROT     (1u << 2)      /* bus protection still set (bad) */
#define N2MDP_S_MUXGATE  (1u << 3)      /* mdp_sel gated (bad) */
#define N2MDP_S_APB      (1u << 4)      /* APB bus clock open */
#define N2MDP_S_SMI      (1u << 5)      /* MDP SMI clocks all open */
#define N2MDP_S_ENGREAD  (1u << 6)      /* at least one engine read */
#define N2MDP_S_LVL2     (1u << 7)      /* level 2 ran (gates opened and closed again) */
#define N2MDP_S_RESTORED (1u << 8)      /* level 2: CG state after == before */
#define N2MDP_S_GCEM_ON  (1u << 9)
#define N2MDP_S_GCED_ON  (1u << 10)
#define N2MDP_S_NOMAP    (1u << 15)

static uint32_t
n2mdp_probe(int level)
{
	uint32_t st = N2MDP_S_RAN;
	if (!n2mdp_maps()) { N2MDP_LOG("NORD2 MDP: mapping failed\n"); return st | N2MDP_S_NOMAP; }
	uint32_t s0 = ni_rd(n2mdp.spm, N2MDP_PWR_STA), s1 = ni_rd(n2mdp.spm, N2MDP_PWR_STA2);
	uint32_t p1 = ni_rd(n2mdp.infra, N2MDP_PROT_STA1), pm = ni_rd(n2mdp.infra, N2MDP_PROT_MM), pm2 = ni_rd(n2mdp.infra, N2MDP_PROT_MM2);
	uint32_t cfg1 = ni_rd(n2mdp.top, N2MDP_CFG1), ifr = ni_rd(n2mdp.infra, N2MDP_IFRAO0_STA);
	bool power = (s0 & s1 & N2MDP_MDP_BIT) != 0;
	bool prot = (p1 & N2MDP_P_STA1) || (pm & N2MDP_P_MM) || (pm2 & N2MDP_P_MM2);
	bool mux = (cfg1 & N2MDP_MDPSEL_GATE) != 0;
	{	/* 0673: owned / child-explained breakdown (read-only, infracfg_ao is always on) */
		uint32_t psub = ni_rd(n2mdp.infra, n2mdp_pregs[3].sta1), pwr = s0 & s1, sv[4] = { p1, pm, pm2, psub }, own[4];
		for (unsigned r = 0; r < 4; r++) { own[r] = n2mdp_prot_owned(r, sv[r], pwr); }
		N2MDP_LOG("NORD2 MDP: prot breakdown (MDP-mask bits set / owned by a powered-off child / MDP-owned): sta1 %#x/%#x/%#x"
		    " mm %#x/%#x/%#x mm2 %#x/%#x/%#x sub_vdnr %#x (raw %#x)/%#x/%#x | rule %s\n",
		    p1 & n2mdp_prot_mdp(0), p1 & n2mdp_prot_mdp(0) & n2mdp_prot_child_off(0, pwr), own[0],
		    pm & n2mdp_prot_mdp(1), pm & n2mdp_prot_mdp(1) & n2mdp_prot_child_off(1, pwr), own[1],
		    pm2 & n2mdp_prot_mdp(2), pm2 & n2mdp_prot_mdp(2) & n2mdp_prot_child_off(2, pwr), own[2],
		    psub & n2mdp_prot_mdp(3), psub, psub & n2mdp_prot_mdp(3) & n2mdp_prot_child_off(3, pwr), own[3],
		    n2mdp.prot_owned_rule ? "owned (0673 bring-up verified)" : "any MDP-mask bit (0670)");
		if (n2mdp.prot_owned_rule) { prot = (own[0] | own[1] | own[2] | own[3]) != 0; }
		else if (psub & n2mdp_prot_mdp(3)) { prot = true; }      /* 0670 rule + MDP step2:3 (SUB_INFRA_VDNR bit 20), was not read */
	}
	N2MDP_LOG("NORD2 MDP: level %d run %u | spm status %#x/%#x MDP_PWR_CON %#x DIS_PWR_CON %#x (MDP %s, DIS %s) | prot sta1 %#x mm %#x"
	    " mm2 %#x (MDP bits %s) | CLK_CFG_1 %#x (mdp_sel parent %u, %s) | IFRAO0 %#x (GCE %s, GCE2 %s)\n", level, n2mdp.runs, s0, s1,
	    ni_rd(n2mdp.spm, N2MDP_MDP_CON), ni_rd(n2mdp.spm, N2MDP_DIS_CON), power ? "on" : "OFF", (s0 & s1 & N2MDP_DIS_BIT) ? "on" : "OFF",
	    p1, pm, pm2, prot ? "SET" : "clear", cfg1, (cfg1 >> 8) & 0xfu, mux ? "GATED" : "running", ifr,
	    (ifr & N2MDP_IFR_GCE) ? "gated" : "open", (ifr & N2MDP_IFR_GCE2) ? "gated" : "open");
	{	/* 0688: which MDP clock: mdp_sel parent (clk-mt6885.c mdp_parents) and, for parent 8, the TVDPLL rate (apmixedsys 0x1000c000:
		 * TVDPLL_CON1 0x384 = pcw[21:0] (14 fractional bits) | posdiv[26:24]); live.dts mmdvfs mdp_step0 594 MHz = TOP_TVDPLL_CK,
		 * step1 416 = UNIVPLL_D6, step2 312 = UNIVPLL_D4_D2, step3 273 = MAINPLL_D4_D2.  Read only. */
		static const char *const mp[11] = { "26M", "mainpll_d5_d2", "mmpll_d6_d2", "mainpll_d4_d2 (273)", "univpll_d4_d2 (312)",
			"mainpll_d6", "univpll_d6 (416)", "mainpll_d4", "tvdpll (mmdvfs step0)", "univpll_d4", "mmpll_d5_d2" };
		static vm_offset_t apm;
		if (!apm) { apm = n2mdp_map(0x1000c000u, 0x1000); }
		uint32_t par = (cfg1 >> 8) & 0xfu, c0 = apm ? ni_rd(apm, 0x380u) : 0, c1 = apm ? ni_rd(apm, 0x384u) : 0;
		uint64_t khz = ((uint64_t)(c1 & 0x3fffffu) * 26000u) >> (14u + ((c1 >> 24) & 7u));
		N2MDP_LOG("NORD2 MDP: clock mdp_sel parent %u (%s) | TVDPLL CON0 %#x CON1 %#x = %u MHz (pll %s)\n", par, par < 11u ? mp[par] : "?",
		    c0, c1, (uint32_t)(khz / 1000u), (c0 & 1u) ? "on" : "OFF");
	}
	if (power) { st |= N2MDP_S_POWER; }
	if (prot) { st |= N2MDP_S_PROT; }
	if (mux) { st |= N2MDP_S_MUXGATE; }
	uint32_t gced = 0, gcem = 0;
	if (!(ifr & N2MDP_IFR_GCE)) { gced = n2mdp_gce(n2mdp.gced, "GCE-D 0x10228000"); st |= N2MDP_S_GCED_ON; }
	if (!(ifr & N2MDP_IFR_GCE2)) { gcem = n2mdp_gce(n2mdp.gcem, "GCE-M 0x10318000"); st |= N2MDP_S_GCEM_ON; }
	uint32_t cg0 = 0xffffffffu, cg1 = 0xffffffffu, cg2 = 0xffffffffu, rdma0_en = 0, wrot0_en = 0, l2mmu = 0, m4u_pt = 0;
	unsigned nread = 0;
	if (power && !prot && !mux) {
		cg0 = ni_rd(n2mdp.sys, N2MDP_CG0); cg1 = ni_rd(n2mdp.sys, N2MDP_CG1); cg2 = ni_rd(n2mdp.sys, N2MDP_CG2);
		N2MDP_LOG("NORD2 MDP: mdpsys CG_CON0 %#x CG_CON1 %#x CG_CON2 %#x (1 = gated; APB %s, MUTEX0 %s, SMI clocks %s)\n", cg0, cg1, cg2,
		    (cg0 & N2MDP_CG0_APB) ? "gated" : "open", (cg0 & N2MDP_CG0_MUTEX0) ? "gated" : "open",
		    (cg1 & N2MDP_CG1_SMI) ? "some gated" : "open");
		uint32_t t[sizeof(n2mdp_topo) / sizeof(n2mdp_topo[0])];
		for (unsigned i = 0; i < sizeof(n2mdp_topo) / sizeof(n2mdp_topo[0]); i++) { t[i] = ni_rd(n2mdp.sys, n2mdp_topo[i]); }
		N2MDP_LOG("NORD2 MDP: topology RDMA0-3 MOUT %#x %#x %#x %#x RSZ0-3 SEL %#x %#x %#x %#x WROT0-3 SEL %#x %#x %#x %#x"
		    " MOUT_MASK %#x %#x %#x MISC %#x\n", t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], t[10], t[11], t[12],
		    t[13], t[14], t[15]);
		if (!(cg0 & N2MDP_CG0_APB)) {
			st |= N2MDP_S_APB;
			if (!(cg0 & N2MDP_CG0_MUTEX0)) {
				N2MDP_LOG("NORD2 MDP: mutex INTEN %#x INTSTA %#x MUTEX0 EN %#x MOD0 %#x MOD1 %#x\n", ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x0u),
				    ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x4u), ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x20u),
				    ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x30u), ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x34u));
			}
			nread = n2mdp_engines(cg0, "as found", &rdma0_en, &wrot0_en);
			if (!(cg1 & N2MDP_CG1_SMI)) { st |= N2MDP_S_SMI; l2mmu = n2mdp_smi("as found", &m4u_pt); }
		}
		if (nread) { st |= N2MDP_S_ENGREAD; }
		if (level >= 2) {
			uint32_t open0 = cg0 & (N2MDP_CG0_APB | N2MDP_CG0_MUTEX0 | (1u << 0) | (1u << 4) | (1u << 7));
			uint32_t open1 = cg1 & N2MDP_CG1_SMI;
			if (open1) { ni_wr(n2mdp.sys, N2MDP_CG1 + 8u, open1); }
			if (open0) { ni_wr(n2mdp.sys, N2MDP_CG0 + 8u, open0); }
			uint32_t c0 = ni_rd(n2mdp.sys, N2MDP_CG0), c1 = ni_rd(n2mdp.sys, N2MDP_CG1);
			N2MDP_LOG("NORD2 MDP: level 2 opened CG0 %#x CG1 %#x -> CG_CON0 %#x CG_CON1 %#x\n", open0, open1, c0, c1);
			if (!(c0 & N2MDP_CG0_APB)) {
				if (!(c0 & N2MDP_CG0_MUTEX0)) {
					N2MDP_LOG("NORD2 MDP: opened: mutex INTEN %#x INTSTA %#x MUTEX0 EN %#x MOD0 %#x\n", ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x0u),
					    ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x4u), ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x20u),
					    ni_rd(n2mdp.sys, N2MDP_OFF_MUTEX + 0x30u));
				}
				nread += n2mdp_engines(c0, "opened", &rdma0_en, &wrot0_en);
				if (!(c1 & N2MDP_CG1_SMI)) { l2mmu = n2mdp_smi("opened", &m4u_pt); }
			}
			if (open0) { ni_wr(n2mdp.sys, N2MDP_CG0 + 4u, open0); }
			if (open1) { ni_wr(n2mdp.sys, N2MDP_CG1 + 4u, open1); }
			uint32_t a0 = ni_rd(n2mdp.sys, N2MDP_CG0), a1 = ni_rd(n2mdp.sys, N2MDP_CG1);
			st |= N2MDP_S_LVL2;
			if (a0 == cg0 && a1 == cg1) { st |= N2MDP_S_RESTORED; }
			if (nread) { st |= N2MDP_S_ENGREAD; }
			N2MDP_LOG("NORD2 MDP: level 2 closed again: CG_CON0 %#x CG_CON1 %#x (%s)\n", a0, a1,
			    (a0 == cg0 && a1 == cg1) ? "same as found" : "DIFFERENT from found");
		}
	} else {
		N2MDP_LOG("NORD2 MDP: mdpsys not read (%s%s%s)\n", power ? "" : "domain off ", prot ? "bus protection set " : "",
		    mux ? "mdp_sel gated" : "");
	}
	N2MDP_LOG("NORD2 MDP VERDICT: power=%d prot=%d mdp_sel=%s apb=%s smi=%s cg0=%#x cg1=%#x engines_read=%u rdma0_en=%#x wrot0_en=%#x"
	    " larb2_mmu=%#x m4u1_pt=%#x gced_thr=%#x gcem_thr=%#x lvl=%d state=%#x\n", power, prot, mux ? "gated" : "on",
	    (st & N2MDP_S_APB) ? "open" : "closed", (st & N2MDP_S_SMI) ? "open" : "closed", cg0, cg1, nread, rdma0_en, wrot0_en,
	    l2mmu, m4u_pt, gced, gcem, level, st);
	return st;
}


/* ======================================================================================================================
 * 0671: functional stages (gpu/build/mdp-20261114/MDP_OFFLOAD.md section 7).  State machine, one boot:
 *   stage 0  probe (above)                          -> must find: domain on, protection clear, mdp_sel running
 *   stage 1  DMA self-test: BGRA 256x256 copy + 90-degree rotation, RDMA0 -> WROT0 direct, buffers in the vcodec DMA pool
 *            (known PA < 4 GiB), larb2 ports in bypass (MMU_EN 0), CPU-programmed, completion = WROT frame-done GCE-M event
 *            read through the GCE token registers, exact CPU-reference compare + guard bands; timeout -> abort (mutex off,
 *            engine resets), gates back as found
 *   stage 2  function matrix: 180/270/flip, tiled copy/rotation, NV12 (420v/420f) -> BGRA, scaling down/up, scale + rotate;
 *            each test sets one capability bit; R/B order self-calibrated
 *   stage 3  M4U1 page tables (0401's sweet_fb_iommu.h): copy from the pool to scattered kernel pages above 4 GiB through IOVAs
 *   service  (0672, SweetSurface.cpp) registers AppleM2ScalerCSCDriver only if stage 3 passed in this boot and no kill file
 * Each stage runs only if the previous one passed in this boot.  Results: kern.n2mdp_stage, kern.n2mdp_caps, kern.n2mdp_s1,
 * kern.n2mdp_s2, kern.n2mdp_s3, log lines "NORD2 MDP S1/S2/S3 ..." and "NORD2 MDP STAGES: ...".
 * Kill switches: file /private/var/db/nord2-mdp-off (probe only), sysctl kern.n2mdp_stages (bits 7:0 max stage, bit 8 = 0673
 * bring-up stage P; default N2MDP_STAGES),
 * kern.n2mdp_probe = -1 before +45 s.
 * ==================================================================================================================== */
#include "nord2_mdp_job.h"

#ifndef N2MDP_STAGES
#define N2MDP_STAGES 0x103   /* q10jmdp13: bring-up + S1 + S2 + S3 (M4U1 IOVA passed on q10jmdp12s3) */
#endif

#define N2MDP_CAP_COPY      (1u << 0)
#define N2MDP_CAP_ROT90     (1u << 1)
#define N2MDP_CAP_ROT180    (1u << 2)
#define N2MDP_CAP_ROT270    (1u << 3)
#define N2MDP_CAP_FLIP      (1u << 4)
#define N2MDP_CAP_TILE      (1u << 5)
#define N2MDP_CAP_NV12V     (1u << 6)
#define N2MDP_CAP_NV12F     (1u << 7)
#define N2MDP_CAP_SCALE_DN  (1u << 8)
#define N2MDP_CAP_SCALE_UP  (1u << 9)
#define N2MDP_CAP_SCALE_ROT (1u << 10)
#define N2MDP_CAP_IOVA      (1u << 11)
#define N2MDP_CAP_RGB_SCALE (1u << 12)
#define N2MDP_CAP_DUAL      (1u << 14)   /* 0690: pipe-2 direct lane + both pipes at once verified in this boot (service runs 2 jobs) */
#define N2MDP_CAP_SCALE_BIG (1u << 15)   /* 0691: S2 scbig (1024x1024 NV12 -> 640x640, 512-px input tiles) passed: scaled frames > 1024x512 */
/* 0691: largest scaling input tile S2 ever ran (scaledown 512x512 -> 256x256); the service splits scaled jobs so no tile exceeds it */
#define N2MDP_SCALE_MAX_IN 512u
/* output tile width (16-px units) whose input span (with the 4-px filter margins) stays <= N2MDP_SCALE_MAX_IN */
static inline uint32_t
n2mdp_scale_tile16(uint32_t cw, uint32_t ow)
{
	if (!cw || !ow) { return 32u; }
	uint64_t t = ((uint64_t)(N2MDP_SCALE_MAX_IN - 16u) * ow / cw) / 16u;
	return t < 2u ? 2u : t > 32u ? 32u : (uint32_t)t;
}
static uint32_t n2mdp_scale_off;           /* 0691: a scaled job failed in this boot: scaling refused from then on */
#define N2MDP_CAP_NV12_DST  (1u << 13)   /* 0686: NV12 destinations (NV12 -> NV12 rotation / copy jobs passed in this boot) */   /* 0683: RGB-source scaling (smooth-pattern jobs passed in this boot) */

/* gates the functional stages keep open (CG_CON0): RDMA0 0, RSZ2 10? no: RSZ0 4 .. see clk-mt6885-mdp.c */
#define N2MDP_CG0_FUNC  ((1u << 0) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 12) | (1u << 13) | (1u << 14) | \
                         (1u << 15) | (1u << 31))   /* RDMA0 WROT0 RDMA2 AAL2 RSZ2 TDSHP2 TCC2 WROT2 MUTEX0 APB */
#define N2MDP_IFRAO0_CLR 0x084u
#define N2MDP_GCE_TOKEN_ID  0x60u
#define N2MDP_GCE_TOKEN_VAL 0x64u
#define N2MDP_GCE_TOKEN_UPD 0x68u

#define N2MDP_POOL_BYTES   0x900000u     /* 9 MiB of the vcodec pool: test buffers (8 MiB) + page-table block (1 MiB) */
#define N2MDP_BUF_SRC      0x000000u
#define N2MDP_BUF_DST      0x300000u
#define N2MDP_BUF_SPAN     0x200000u     /* largest test buffer is 1 MiB; 1 MiB guard band after each */
#define N2MDP_BUF_PT       0x800000u     /* 1 MiB: 64 KiB-aligned M4U1 table block (580 KiB used) */
#define N2MDP_GUARD_BYTE   0xa5u
#define N2MDP_PREGUARD     0x100000u     /* 0677: pool 0x200000-0x2fffff (unused) = guard below the destination */
#define N2MDP_BUF_SRC2     0x120000u     /* 0690: dual job, second source (1024x768 NV12 = 0x120000 bytes; ends in the pre-guard) */
#define N2MDP_BUF_DST2     0x600000u     /* 0690: dual job, second destination + guard up to 0x7fffff (table block starts at 0x800000) */
/* 0690: kern.n2mdp_tune bits (default all): 1 = vendor M4U1 write setup at S3 (kept only if the S3 TIMING rerun is not slower),
 * 2 = second pipe for unscaled service requests (needs N2MDP_CAP_DUAL), 4 = S3-measured rotation tile width for the service */
#define N2MDP_TUNE_M4U     1
#define N2MDP_TUNE_DUAL    2
#define N2MDP_TUNE_TILE    4
static int n2mdp_tune = N2MDP_TUNE_M4U | N2MDP_TUNE_DUAL | N2MDP_TUNE_TILE;
static int n2mdp_svc_tile_px;            /* kern.n2mdp_svc_tile: 0 = S3 choice (or 512), else 64..512 px for rotated unscaled requests */
static uint32_t n2mdp_auto_tile16;       /* S3 choice in 16-px units (0 = default 512 px) */

#ifdef N2MDP_HOST
extern bool n2mdp_host_pool(uint32_t bytes, uint32_t *pa, vm_offset_t *va);
extern void n2mdp_host_delay(uint32_t us);
extern uint64_t n2mdp_host_now(void);
extern bool n2mdp_host_killfile(void);
extern bool n2mdp_host_alloc_hi(uint32_t bytes, vm_offset_t *va, uint64_t *pa_of_page /* bytes/4096 entries */);
#define n2mdp_delay(us) n2mdp_host_delay(us)
#define n2mdp_now_us() n2mdp_host_now()
#define n2mdp_flush(va, len) ((void)(va), (void)(len))
#define n2mdp_lock() ((void)0)
#define n2mdp_unlock() ((void)0)
/* 0690: host model of the per-pipe locks (single-threaded tests mark a pipe busy to see the other one taken) */
static bool n2mdp_host_pipe_busy[2];
static bool n2mdp_pipe_trylock(unsigned i) { if (n2mdp_host_pipe_busy[i]) { return false; } n2mdp_host_pipe_busy[i] = true; return true; }
static void n2mdp_pipe_lock(unsigned i) { n2mdp_host_pipe_busy[i] = true; }
static void n2mdp_pipe_unlock(unsigned i) { n2mdp_host_pipe_busy[i] = false; }
#define n2mdp_svc_enter() ((void)0)
#define n2mdp_svc_exit() ((void)0)
#define n2mdp_map_lock() ((void)0)
#define n2mdp_map_unlock() ((void)0)
#define n2mdp_gce_lock() ((void)0)
#define n2mdp_gce_unlock() ((void)0)
#else
#include <sys/vnode.h>
#include <kern/locks.h>
#define n2mdp_delay(us) delay((int)(us))
static uint64_t n2mdp_now_us(void) { uint64_t ns; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000u; }
static void
n2mdp_flush(vm_offset_t va, uint32_t len)
{
	for (vm_offset_t a = va & ~63ul; a < va + len; a += 64) { __asm__ volatile("dc civac, %0" :: "r"(a) : "memory"); }
	__asm__ volatile("dsb sy" ::: "memory");
}
static LCK_GRP_DECLARE(n2mdp_grp, "nord2-mdp");
/* 0690: the MDP lock is a rw lock: exclusive = stages, probe, bring-up (everything that was under the old mutex); shared = service
 * blits, which then take ONE pipe lock (pipe 0 or pipe 2) for their run, the map lock for the IOVA cache / page tables / fault
 * state, and the GCE spin lock for the two-register token read.  Order: rw -> pipe -> map; the spin lock is innermost. */
static LCK_RW_DECLARE(n2mdp_rw, &n2mdp_grp);
static LCK_MTX_DECLARE(n2mdp_pmtx0, &n2mdp_grp);
static LCK_MTX_DECLARE(n2mdp_pmtx2, &n2mdp_grp);
static LCK_MTX_DECLARE(n2mdp_map_mtx, &n2mdp_grp);
static LCK_SPIN_DECLARE(n2mdp_gce_spin, &n2mdp_grp);
#define n2mdp_lock() lck_rw_lock_exclusive(&n2mdp_rw)
#define n2mdp_unlock() lck_rw_unlock_exclusive(&n2mdp_rw)
#define n2mdp_svc_enter() lck_rw_lock_shared(&n2mdp_rw)
#define n2mdp_svc_exit() lck_rw_unlock_shared(&n2mdp_rw)
#define n2mdp_map_lock() lck_mtx_lock(&n2mdp_map_mtx)
#define n2mdp_map_unlock() lck_mtx_unlock(&n2mdp_map_mtx)
#define n2mdp_gce_lock() lck_spin_lock(&n2mdp_gce_spin)
#define n2mdp_gce_unlock() lck_spin_unlock(&n2mdp_gce_spin)
static bool n2mdp_pipe_trylock(unsigned i) { return lck_mtx_try_lock(i ? &n2mdp_pmtx2 : &n2mdp_pmtx0) != 0; }
static void n2mdp_pipe_lock(unsigned i) { lck_mtx_lock(i ? &n2mdp_pmtx2 : &n2mdp_pmtx0); }
static void n2mdp_pipe_unlock(unsigned i) { lck_mtx_unlock(i ? &n2mdp_pmtx2 : &n2mdp_pmtx0); }
#endif

static struct {
	uint32_t stage;            /* highest stage passed in this boot (0 = none) */
	uint32_t caps;             /* N2MDP_CAP_* */
	uint32_t s1, s2_pass, s2_fail, s3;   /* s1/s3: 0 not run, 1 pass, >1 failure code; s2: test bitmasks */
	uint32_t rgb_swap;
	uint32_t s2_diag;          /* 0682: S2 diagnostic jobs that passed (bit = test index) */
	uint32_t wrot_lines;       /* 0676: WROT main-buffer lines that worked (0 = n2mdp_wrot_lines default) */
	uint32_t jobs, job_fail, last_us, max_us, timeouts;
	uint32_t cg0_found, cg1_found, opened0, opened1;
	bool gates_open, gce_opened, pool_ok, killed;
	uint32_t pool_pa;
	vm_offset_t pool_va;
	uint32_t larb_found[4];
	/* stage 3 */
	bool iommu_on;
	uint32_t ptbase, l1_pa;
} n2mx;
static int n2mdp_max_stage = N2MDP_STAGES;

static inline uint32_t n2mdp_rd(uint32_t off) { return ni_rd(n2mdp.sys, off); }
static inline void n2mdp_wr(uint32_t off, uint32_t v) { ni_wr(n2mdp.sys, off, v); }

static uint32_t
n2mdp_gce_event(uint32_t ev)
{
	n2mdp_gce_lock();        /* 0690: ID write + VAL read must not interleave with the other pipe's poll */
	ni_wr(n2mdp.gcem, N2MDP_GCE_TOKEN_ID, ev & 0x3ffu);
	uint32_t v = ni_rd(n2mdp.gcem, N2MDP_GCE_TOKEN_VAL);
	n2mdp_gce_unlock();
	return v;
}
static void n2mdp_gce_clear(uint32_t ev) { ni_wr(n2mdp.gcem, N2MDP_GCE_TOKEN_UPD, ev & 0x3ffu); }

/* open the clocks the functional stages need (records what it changed so a failure can restore them) */
static bool
n2mdp_gates_on(void)
{
	if (n2mx.gates_open) { return true; }
	uint32_t c0 = n2mdp_rd(N2MDP_CG0), c1 = n2mdp_rd(N2MDP_CG1);
	n2mx.cg0_found = c0; n2mx.cg1_found = c1;
	n2mx.opened0 = c0 & N2MDP_CG0_FUNC; n2mx.opened1 = c1 & N2MDP_CG1_SMI;
	if (n2mx.opened1) { n2mdp_wr(N2MDP_CG1 + 8u, n2mx.opened1); }
	if (n2mx.opened0) { n2mdp_wr(N2MDP_CG0 + 8u, n2mx.opened0); }
	uint32_t ifr = ni_rd(n2mdp.infra, N2MDP_IFRAO0_STA);
	if (ifr & N2MDP_IFR_GCE2) { ni_wr(n2mdp.infra, N2MDP_IFRAO0_CLR, N2MDP_IFR_GCE2); n2mx.gce_opened = true; }
	uint32_t a0 = n2mdp_rd(N2MDP_CG0), a1 = n2mdp_rd(N2MDP_CG1), a2 = ni_rd(n2mdp.infra, N2MDP_IFRAO0_STA);
	bool ok = !(a0 & N2MDP_CG0_FUNC) && !(a1 & N2MDP_CG1_SMI) && !(a2 & N2MDP_IFR_GCE2);
	N2MDP_LOG("NORD2 MDP: gates %s: CG0 %#x -> %#x CG1 %#x -> %#x IFRAO0 %#x -> %#x\n", ok ? "open" : "DID NOT OPEN", c0, a0, c1, a1, ifr, a2);
	n2mx.gates_open = ok;
	return ok;
}
static void
n2mdp_gates_restore(void)
{
	if (n2mx.opened0) { n2mdp_wr(N2MDP_CG0 + 4u, n2mx.opened0); }
	if (n2mx.opened1) { n2mdp_wr(N2MDP_CG1 + 4u, n2mx.opened1); }
	if (n2mx.gce_opened) { ni_wr(n2mdp.infra, N2MDP_IFRAO0_CLR - 4u, N2MDP_IFR_GCE2); n2mx.gce_opened = false; }   /* 0x80 = SET */
	n2mx.opened0 = n2mx.opened1 = 0; n2mx.gates_open = false;
	N2MDP_LOG("NORD2 MDP: gates restored: CG0 %#x CG1 %#x\n", n2mdp_rd(N2MDP_CG0), n2mdp_rd(N2MDP_CG1));
}

/* ======================================================================================================================
 * 0673: bring-up stage "P" (kern.n2mdp_stages bit 0x100): the MDP part of the vendor power-on (spm_mtcmos_ctrl_mdp
 * STA_POWER_ON after the SPM steps, then enable_subsys_hwcg(SYS_MDP)) on a domain that is already powered:
 *   1. checks: PWR_STATUS and _2ND bit 19 on, MDP_PWR_CON = on state (RST_B ON ON_2ND set; ISO CLK_DIS SRAM_PDN SRAM_ACK
 *      clear), mdp_sel ungated.  The SPM power-on itself is NOT done here (domain shared with the codec code).
 *   2. protection: for each step in the vendor release order step2:0 2:1 2:2 2:3 1:0 1:1 1:2, clear (write 1 to the _CLR
 *      register) only the EXCLUSIVE MDP bits that are set in EN or STA1, then poll STA1 and EN until those bits read 0
 *      (<= N2MDP_PROT_POLLS x N2MDP_PROT_POLL_US).  Bits shared with another domain are never written: if one is set while that
 *      domain is on (or it is a display bit) the stage stops before writing anything.  A step that does not clear ->
 *      every bit released so far is set again in the vendor power-down order (step1 then step2) with its ack polled, and
 *      the register/bit is logged.
 *   3. verification: all four STA1 registers re-read, MDP-owned bits (owned rule above) must be 0.
 *   4. gates: mdpsys CG_CON1 CLR (0x118) <- 0x1112 (SMI0/1/2 + APMCU_GALS, as enable_subsys_hwcg), then CG_CON0 CLR (0x108)
 *      <- APB 31 | MUTEX0 15 | RDMA0 0 | RSZ0 4 | WROT0 7 (only the gated ones), infracfg IFRAO0 CLR (0x84) <- GCE2 bit 9
 *      if gated (GCE bit 8 = display GCE: not touched); read back; any gate still closed -> gates and protection restored.
 *   5. the caller re-runs the read-only probe with the owned rule.
 * Result kern.n2mdp_pwron: 0 not run, 1 OK, 2 domain off, 3 PWR_CON not on, 4 mdp_sel gated, 5 shared/display bit set,
 * 6 release ack timeout (restored), 7 restore failed, 8 gates did not open (restored), 9 maps, 10 owned bit set after release.
 * ==================================================================================================================== */
#define N2MDP_PWRON_BIT       0x100
#define N2MDP_PROT_POLLS      200u
#define N2MDP_PROT_POLL_US    5u
#define N2MDP_CON_ON_MASK     0x111fu    /* SRAM_ACK 12 | SRAM_PDN 8 | CLK_DIS 4 | ON_2ND 3 | ON 2 | ISO 1 | RST_B 0 */
#define N2MDP_CON_ON_VAL      0x000du    /* ON_2ND | ON | RST_B */
#define N2MDP_CG0_PWRON       (N2MDP_CG0_APB | N2MDP_CG0_MUTEX0 | (1u << 0) | (1u << 4) | (1u << 7))

static struct {
	uint32_t code;
	uint32_t released[N2MDP_NPSTEPS];     /* bits this stage cleared per step that were set in EN (put back on failure) */
	uint32_t opened0, opened1;
	bool gce_opened;
} n2pw;

static const char *const n2mdp_pwron_why[11] = { "not run", "OK", "domain off (SPM power-on not attempted)",
	"MDP_PWR_CON not in the on state", "mdp_sel gated (topckgen mux not touched)", "shared or display protection bit set",
	"release ack timeout (protection restored)", "RESTORE FAILED", "gates did not open (restored)", "mapping failed",
	"MDP-owned protection bit set after release (restored)" };

/* poll register `off` of infracfg until (v & bits) == want; returns microseconds spent or -1 on timeout */
static int
n2mdp_prot_poll(uint16_t off, uint32_t bits, uint32_t want)
{
	for (unsigned k = 0; k <= N2MDP_PROT_POLLS; k++) {
		if ((ni_rd(n2mdp.infra, off) & bits) == want) { return (int)(k * N2MDP_PROT_POLL_US); }
		if (k < N2MDP_PROT_POLLS) { n2mdp_delay(N2MDP_PROT_POLL_US); }
	}
	return -1;
}

/* put back every bit released so far, vendor power-down order; returns false if an ack did not come */
static bool
n2mdp_prot_restore(void)
{
	bool ok = true;
	for (unsigned i = 0; i < N2MDP_NPSTEPS; i++) {
		unsigned k = n2mdp_pdown_order[i];
		uint32_t b = n2pw.released[k];
		if (!b) { continue; }
		const struct n2mdp_preg *R = &n2mdp_pregs[n2mdp_psteps[k].reg];
		ni_wr(n2mdp.infra, R->set, b);
		int us = n2mdp_prot_poll(R->sta1, b, b);
		if (us < 0) {
			ok = false;
			N2MDP_LOG("NORD2 MDP PWRON: RESTORE STUCK %s %s_SET <- %#x: STA1 (%#x) %#x after %u us, bit %u not acked\n",
			    n2mdp_psteps[k].name, R->name, b, R->sta1, ni_rd(n2mdp.infra, R->sta1), N2MDP_PROT_POLLS * N2MDP_PROT_POLL_US,
			    n2mdp_lowbit(b & ~ni_rd(n2mdp.infra, R->sta1)));
		} else {
			N2MDP_LOG("NORD2 MDP PWRON: restored %s %s_SET <- %#x (acked after %d us)\n", n2mdp_psteps[k].name, R->name, b, us);
			n2pw.released[k] = 0;
		}
	}
	return ok;
}

static void
n2mdp_pwron_gates_restore(void)
{
	if (n2pw.opened0) { n2mdp_wr(N2MDP_CG0 + 4u, n2pw.opened0); }
	if (n2pw.opened1) { n2mdp_wr(N2MDP_CG1 + 4u, n2pw.opened1); }
	if (n2pw.gce_opened) { ni_wr(n2mdp.infra, N2MDP_IFRAO0_CLR - 4u, N2MDP_IFR_GCE2); }
	n2pw.opened0 = n2pw.opened1 = 0; n2pw.gce_opened = false;
}

static uint32_t
n2mdp_pwron_inner(void)
{
	if (!n2mdp_maps()) { return 9; }
	uint32_t s0 = ni_rd(n2mdp.spm, N2MDP_PWR_STA), s1 = ni_rd(n2mdp.spm, N2MDP_PWR_STA2), pwr = s0 & s1;
	uint32_t con = ni_rd(n2mdp.spm, N2MDP_MDP_CON), cfg1 = ni_rd(n2mdp.top, N2MDP_CFG1);
	N2MDP_LOG("NORD2 MDP PWRON: start | spm %#x/%#x MDP_PWR_CON %#x CLK_CFG_1 %#x\n", s0, s1, con, cfg1);
	if (!(pwr & N2MDP_MDP_BIT)) { return 2; }
	if ((con & N2MDP_CON_ON_MASK) != N2MDP_CON_ON_VAL) { return 3; }
	if (cfg1 & N2MDP_MDPSEL_GATE) { return 4; }

	/* 2a. plan: nothing is written before every step is known to be safe */
	uint32_t en[4], sta[4], rel[N2MDP_NPSTEPS];
	for (unsigned r = 0; r < 4; r++) {
		en[r] = ni_rd(n2mdp.infra, n2mdp_pregs[r].en); sta[r] = ni_rd(n2mdp.infra, n2mdp_pregs[r].sta1);
		uint32_t v = en[r] | sta[r], own = n2mdp_prot_owned(r, v, pwr), ex = n2mdp_prot_exclusive(r);
		N2MDP_LOG("NORD2 MDP PWRON: %s EN %#x STA1 %#x | MDP mask %#x set %#x, child-off %#x, MDP-owned set %#x (exclusive %#x)\n",
		    n2mdp_pregs[r].name, en[r], sta[r], n2mdp_prot_mdp(r), v & n2mdp_prot_mdp(r), v & n2mdp_prot_mdp(r) &
		    n2mdp_prot_child_off(r, pwr), own, own & ex);
		if (own & ~ex) {
			unsigned b = n2mdp_lowbit(own & ~ex);
			N2MDP_LOG("NORD2 MDP PWRON: %s bit %u is set and shared with %s (domain on or display): not written, stage stops\n",
			    n2mdp_pregs[r].name, b, n2mdp_prot_owner(r, b));
			return 5;
		}
	}
	for (unsigned k = 0; k < N2MDP_NPSTEPS; k++) {
		unsigned r = n2mdp_psteps[k].reg;
		rel[k] = (en[r] | sta[r]) & n2mdp_psteps[k].mask & n2mdp_prot_exclusive(r);
	}
	/* 2b. release, vendor order (a bit may sit in two steps of one register: cleared by the first) */
	unsigned nrel = 0;
	for (unsigned k = 0; k < N2MDP_NPSTEPS; k++) {
		const struct n2mdp_preg *R = &n2mdp_pregs[n2mdp_psteps[k].reg];
		for (unsigned j = 0; j < k; j++) { if (n2mdp_psteps[j].reg == n2mdp_psteps[k].reg) { rel[k] &= ~rel[j]; } }
		uint32_t b = rel[k];
		if (!b) { continue; }
		ni_wr(n2mdp.infra, R->clr, b);
		n2pw.released[k] = b & en[n2mdp_psteps[k].reg];   /* put back only what EN really had (a bit may be set in STA1 only) */
		nrel++;
		int us = n2mdp_prot_poll(R->sta1, b, 0), us2 = us < 0 ? -1 : n2mdp_prot_poll(R->en, b, 0);
		if (us < 0 || us2 < 0) {
			uint32_t st = ni_rd(n2mdp.infra, R->sta1), e = ni_rd(n2mdp.infra, R->en);
			N2MDP_LOG("NORD2 MDP PWRON: STUCK %s %s_CLR <- %#x: %s %#x still has %#x after %u us (bit %u)\n", n2mdp_psteps[k].name, R->name,
			    b, us < 0 ? "STA1" : "EN", us < 0 ? st : e, (us < 0 ? st : e) & b, N2MDP_PROT_POLLS * N2MDP_PROT_POLL_US,
			    n2mdp_lowbit((us < 0 ? st : e) & b));
			return n2mdp_prot_restore() ? 6 : 7;
		}
		N2MDP_LOG("NORD2 MDP PWRON: released %s %s_CLR <- %#x (STA1 clear after %d us, EN after %d us)\n", n2mdp_psteps[k].name, R->name,
		    b, us, us2);
	}
	if (!nrel) { N2MDP_LOG("NORD2 MDP PWRON: no exclusive MDP protection bit set: nothing to release\n"); }
	/* 3. verify with fresh reads */
	pwr = ni_rd(n2mdp.spm, N2MDP_PWR_STA) & ni_rd(n2mdp.spm, N2MDP_PWR_STA2);
	for (unsigned r = 0; r < 4; r++) {
		uint32_t v = ni_rd(n2mdp.infra, n2mdp_pregs[r].sta1), own = n2mdp_prot_owned(r, v, pwr);
		if (own || !(pwr & N2MDP_MDP_BIT)) {
			N2MDP_LOG("NORD2 MDP PWRON: after release %s STA1 %#x MDP-owned %#x (bit %u, %s), MDP power %s\n", n2mdp_pregs[r].name, v, own,
			    n2mdp_lowbit(own), n2mdp_prot_owner(r, n2mdp_lowbit(own)), (pwr & N2MDP_MDP_BIT) ? "on" : "OFF");
			return n2mdp_prot_restore() ? 10 : 7;
		}
	}
	/* 4. gates (mdpsys config reads are allowed now: domain on, protection verified, mdp_sel running) */
	uint32_t c0 = n2mdp_rd(N2MDP_CG0), c1 = n2mdp_rd(N2MDP_CG1), ifr = ni_rd(n2mdp.infra, N2MDP_IFRAO0_STA);
	n2pw.opened1 = c1 & N2MDP_CG1_SMI;
	n2pw.opened0 = c0 & N2MDP_CG0_PWRON;
	if (n2pw.opened1) { n2mdp_wr(N2MDP_CG1 + 8u, N2MDP_CG1_SMI); }   /* exactly enable_subsys_hwcg(SYS_MDP) */
	if (n2pw.opened0) { n2mdp_wr(N2MDP_CG0 + 8u, n2pw.opened0); }
	if (ifr & N2MDP_IFR_GCE2) { ni_wr(n2mdp.infra, N2MDP_IFRAO0_CLR, N2MDP_IFR_GCE2); n2pw.gce_opened = true; }
	uint32_t a0 = n2mdp_rd(N2MDP_CG0), a1 = n2mdp_rd(N2MDP_CG1), a2 = ni_rd(n2mdp.infra, N2MDP_IFRAO0_STA);
	bool ok = !(a0 & N2MDP_CG0_PWRON) && !(a1 & N2MDP_CG1_SMI) && !(a2 & N2MDP_IFR_GCE2);
	N2MDP_LOG("NORD2 MDP PWRON: gates %s: CG_CON0 %#x -> %#x (opened %#x) CG_CON1 %#x -> %#x (CLR1 <- %#x) IFRAO0 %#x -> %#x\n",
	    ok ? "open" : "DID NOT OPEN", c0, a0, n2pw.opened0, c1, a1, n2pw.opened1 ? N2MDP_CG1_SMI : 0u, ifr, a2);
	if (!ok) {
		n2mdp_pwron_gates_restore();
		return n2mdp_prot_restore() ? 8 : 7;
	}
	return 1;
}

/* runs under n2mdp_lock; serialised against the codec power code (it moves the shared protection registers) */
static uint32_t
n2mdp_pwron(void)
{
	memset(&n2pw, 0, sizeof(n2pw));
#ifndef N2MDP_HOST
	n2vc_lock();
#endif
	uint32_t c = n2mdp_pwron_inner();
#ifndef N2MDP_HOST
	n2vc_unlock();
#endif
	n2pw.code = c;
	if (c == 1) { n2mdp.prot_owned_rule = true; }
	N2MDP_LOG("NORD2 MDP PWRON: result %u (%s)\n", c, c <= 10 ? n2mdp_pwron_why[c] : "?");
	return c;
}

/* larb2 ports p0..p3 (RDMA0, RDMA2, WROT0, WROT2): bit0 MMU_EN = translate through M4U1, 0 = physical.
 * 0674: physical mode also clears the address-bits-32+ field 15:8 (0xff01, as the codec's larb bring-up, pseudo_m4u_v2.c:563-575) */
#define N2MDP_LARB_NS_PHYS 0x0000ff01u
static void
n2mdp_larb_mode(bool iova)
{
	for (unsigned p = 0; p < 4; p++) {
		uint32_t v = n2mdp_rd(N2MDP_W_LARB2 + 0x380u + 4u * p);
		uint32_t w = iova ? (v | 1u) : (v & ~N2MDP_LARB_NS_PHYS);
		if (w != v) { n2mdp_wr(N2MDP_W_LARB2 + 0x380u + 4u * p, w); }
	}
}
/* 0690: only the two ports of one pipe (pipe 0: p0 RDMA0 + p2 WROT0; pipe 2: p1 RDMA2 + p3 WROT2), so a job ending on one pipe
 * never switches the ports of a job still running on the other pipe */
static void
n2mdp_larb_mode_pipe(uint32_t pipe, bool iova)
{
	unsigned pr = pipe ? 1u : 0u, pw = pipe ? 3u : 2u;
	for (unsigned q = 0; q < 2; q++) {
		unsigned p = q ? pw : pr;
		uint32_t v = n2mdp_rd(N2MDP_W_LARB2 + 0x380u + 4u * p);
		uint32_t w = iova ? (v | 1u) : (v & ~N2MDP_LARB_NS_PHYS);
		if (w != v) { n2mdp_wr(N2MDP_W_LARB2 + 0x380u + 4u * p, w); }
	}
}

/* ======================================================================================================================
 * 0674: larb2 bring-up = what the vendor SMI driver does for larb2 when SYS_MDP powers on (smi_drv.c smi_subsys_after_on:
 * mtk_smi_conf_set + smi_larb_port_set; mt6885/smi_conf.h smi_larb2_conf_pair :80-84, smi_larb2_init_pair :202-209):
 *   SMI_LARB_CMD_THRT_CON 0x024 = 0x370256, SMI_LARB_SW_FLAG 0x040 = 1, INT_SMI_LARB_CMD_THRT_CON 0x524 = 0x370256,
 *   SMI_LARB_OSTDL_PORT(p) 0x200+4p = { 5, 5, 5, 5, 1, 0x3f } (outstanding-request limits: 0 = the port can never issue),
 *   NON_SEC_CON(p) |= 0x8 (bw throttle, smi_larb_bw_thrt_en_port).  The q10jmdp3 boot left this larb unconfigured (no SMI
 *   driver on iOS; its MMU_EN bits were set by the boot chain with M4U1 PT_BASE 0) and S1 stalled with RDMA0 in "data running".
 *   SMI_LARB_SLP_CON 0x00c bit 0 (sleep protect) set -> cleared, bit 16 polled clear <= 1 ms; still set -> S1 not started.
 * Only larb2 (MDP-only ports) is written; the MDP SMI common (shared with VDEC larb4) is read for the log, never written.
 * ==================================================================================================================== */
static const uint8_t n2mdp_larb2_ostdl[6] = { 5, 5, 5, 5, 1, 0x3f };
/* 0675: every log line <= ~200 characters (the q10jmdp4 phone log cut the 0674 lines at ~256 and merged the next one) */
static void
n2mdp_larb_dump(const char *when)
{
	uint32_t L = N2MDP_W_LARB2, C = 0x2000u;
	N2MDP_LOG("NORD2 MDP LARB2 %s: STAT %#x IRQ %#x SLP_CON %#x CON %#x THRT %#x SW_FLAG %#x OSTDL_EN %#x INT_THRT %#x\n", when,
	    n2mdp_rd(L + 0x0u), n2mdp_rd(L + 0x8u), n2mdp_rd(L + 0xcu), n2mdp_rd(L + 0x10u), n2mdp_rd(L + 0x24u), n2mdp_rd(L + 0x40u),
	    n2mdp_rd(L + 0x60u), n2mdp_rd(L + 0x524u));
	N2MDP_LOG("NORD2 MDP LARB2 %s: OSTDL %#x %#x %#x %#x %#x %#x | NON_SEC %#x %#x %#x %#x %#x %#x\n", when,
	    n2mdp_rd(L + 0x200u), n2mdp_rd(L + 0x204u), n2mdp_rd(L + 0x208u), n2mdp_rd(L + 0x20cu), n2mdp_rd(L + 0x210u), n2mdp_rd(L + 0x214u),
	    n2mdp_rd(L + 0x380u), n2mdp_rd(L + 0x384u), n2mdp_rd(L + 0x388u), n2mdp_rd(L + 0x38cu), n2mdp_rd(L + 0x390u), n2mdp_rd(L + 0x394u));
	N2MDP_LOG("NORD2 MDP LARB2 %s: OSTD_MON %#x %#x %#x %#x | FIFO %#x BUS %#x THRT_STAT %#x MON_REQ %#x EXT_ONGOING %#x INT_ONGOING %#x\n", when,
	    n2mdp_rd(L + 0x280u), n2mdp_rd(L + 0x284u), n2mdp_rd(L + 0x288u), n2mdp_rd(L + 0x28cu), n2mdp_rd(L + 0xb0u), n2mdp_rd(L + 0xb4u),
	    n2mdp_rd(L + 0xb8u), n2mdp_rd(L + 0xbcu), n2mdp_rd(L + 0xc8u), n2mdp_rd(L + 0xccu));
	N2MDP_LOG("NORD2 MDP SMI-COMMON %s: L1LEN %#x BUS_SEL %#x M4U_TH %#x FIFO_TH1 %#x FIFO_TH2 %#x DCM %#x CLAMP %#x DUMMY %#x\n", when,
	    n2mdp_rd(C + 0x100u), n2mdp_rd(C + 0x220u), n2mdp_rd(C + 0x234u), n2mdp_rd(C + 0x238u), n2mdp_rd(C + 0x23cu), n2mdp_rd(C + 0x300u),
	    n2mdp_rd(C + 0x3c0u), n2mdp_rd(C + 0x444u));
	N2MDP_LOG("NORD2 MDP SMI-COMMON %s: DEBUG_S0-3 %#x %#x %#x %#x EXT0-3 %#x %#x %#x %#x M0 %#x M1 %#x MISC %#x\n", when,
	    n2mdp_rd(C + 0x400u), n2mdp_rd(C + 0x404u), n2mdp_rd(C + 0x408u), n2mdp_rd(C + 0x40cu), n2mdp_rd(C + 0x420u), n2mdp_rd(C + 0x424u),
	    n2mdp_rd(C + 0x428u), n2mdp_rd(C + 0x42cu), n2mdp_rd(C + 0x430u), n2mdp_rd(C + 0x434u), n2mdp_rd(C + 0x440u));
}
/* 0675: larb2 setup is optional and conservative.  q10jmdp4 phone [V-phone]: as found THRT 0x370246, SW_FLAG 0, OSTDL 0x3f3f on
 * every port (the field is mirrored in both bytes: writing 5 reads 0x505), NON_SEC 0x1, SLP_CON 0.  So the larb WAS usable
 * (maximum outstanding), and 0674's exact-value check (OSTDL == 5) was wrong.  Now:
 *   - THRT / SW_FLAG / INT_THRT: vendor values written (harmless, smi_conf.h larb2 pair);
 *   - OSTDL: left as found when its low byte is non-zero (never lowered); a zero limit (port could never issue) -> vendor value;
 *   - NON_SEC p0..p3: MMU_EN + address field cleared for physical jobs, bw-throttle bit 3 set (vendor).
 * Read-back compares the low byte for OSTDL (byte-mirrored) and logs expected-vs-read for every register that differs.
 * S1 is refused ONLY for a real blocker: sleep protect still set / not acked, a used port (0-3) with OSTDL low byte 0, or MMU_EN
 * still set on a used port (physical DMA would go through M4U1, whose table is empty). */
static bool
n2mdp_larb2_init(void)
{
	uint32_t L = N2MDP_W_LARB2;
	n2mdp_larb_dump("as found");
	if (n2mdp_rd(L + 0xcu) & 1u) {
		n2mdp_wr(L + 0xcu, n2mdp_rd(L + 0xcu) & ~1u);
		unsigned k = 0;
		while ((n2mdp_rd(L + 0xcu) & (1u << 16)) && k < 200u) { n2mdp_delay(5); k++; }
	}
	if (n2mdp_rd(L + 0x24u) != 0x370256u) { n2mdp_wr(L + 0x24u, 0x370256u); }
	if (n2mdp_rd(L + 0x40u) != 1u) { n2mdp_wr(L + 0x40u, 1u); }
	if (n2mdp_rd(L + 0x524u) != 0x370256u) { n2mdp_wr(L + 0x524u, 0x370256u); }
	uint32_t want_ostdl[6];
	for (unsigned p = 0; p < 6; p++) {
		uint32_t v = n2mdp_rd(L + 0x200u + 4u * p);
		want_ostdl[p] = v & 0xffu;
		if (!(v & 0xffu)) { n2mdp_wr(L + 0x200u + 4u * p, n2mdp_larb2_ostdl[p]); want_ostdl[p] = n2mdp_larb2_ostdl[p]; }
	}
	for (unsigned p = 0; p < 4; p++) {
		uint32_t v = n2mdp_rd(L + 0x380u + 4u * p);
		if (!(v & 0x8u)) { n2mdp_wr(L + 0x380u + 4u * p, v | 0x8u); }
	}
	n2mdp_larb_mode(false);
	n2mdp_larb_dump("configured");
	unsigned diffs = 0;
	bool blocker = false;
	uint32_t slp = n2mdp_rd(L + 0xcu);
	if (slp & ((1u << 16) | 1u)) { blocker = true; diffs++; N2MDP_LOG("NORD2 MDP LARB2 check: SLP_CON want bits 0/16 clear, read %#x (BLOCKER)\n", slp); }
	static const struct { uint16_t off; uint32_t want; const char *name; } fixed[3] = {
		{ 0x24u, 0x370256u, "CMD_THRT_CON" }, { 0x40u, 1u, "SW_FLAG" }, { 0x524u, 0x370256u, "INT_CMD_THRT_CON" } };
	for (unsigned i = 0; i < 3; i++) {
		uint32_t v = n2mdp_rd(L + fixed[i].off);
		if (v != fixed[i].want) { diffs++; N2MDP_LOG("NORD2 MDP LARB2 check: %s want %#x read %#x (not a blocker)\n", fixed[i].name, fixed[i].want, v); }
	}
	for (unsigned p = 0; p < 6; p++) {
		uint32_t v = n2mdp_rd(L + 0x200u + 4u * p);
		if ((v & 0xffu) != want_ostdl[p]) {
			diffs++;
			bool b = p < 4 && !(v & 0xffu);
			blocker |= b;
			N2MDP_LOG("NORD2 MDP LARB2 check: OSTDL p%u want low byte %#x read %#x (%s)\n", p, want_ostdl[p], v, b ? "BLOCKER" : "not a blocker");
		}
	}
	for (unsigned p = 0; p < 4; p++) {
		uint32_t v = n2mdp_rd(L + 0x380u + 4u * p);
		if (v & N2MDP_LARB_NS_PHYS) {
			diffs++;
			bool b = (v & 1u) != 0;
			blocker |= b;
			N2MDP_LOG("NORD2 MDP LARB2 check: NON_SEC p%u want bits %#x clear read %#x (%s)\n", p, N2MDP_LARB_NS_PHYS, v, b ? "BLOCKER: MMU_EN" : "not a blocker");
		}
	}
	N2MDP_LOG("NORD2 MDP LARB2 check: %u difference(s), %s\n", diffs, blocker ? "S1 NOT started" : "S1 continues");
	return !blocker;
}

static uint32_t n2mdp_diag_logged;
extern uint32_t n2mdp_last_tiles, n2mdp_tile_us[16];   /* 0689: defined with the service timing below */

/* 0674: full pipe dump (RDMA / WROT / mutex / topology / direct-link valid-ready / larb2), read-only except the WROT debug
 * selector 0x018 (vendor cmdq_mdp_dump_rot does the same).  Used before GO, shortly after GO and on every timeout. */
static void
n2mdp_diag(const struct n2mdp_prog *p, const char *when)
{
	uint32_t R = N2MDP_W_RDMA(p->pipe), W = N2MDP_W_WROT(p->pipe), X = N2MDP_W_MUTEX, mid = p->mutex;
	uint32_t m0 = n2mdp_rd(R + 0x400u), m1 = n2mdp_rd(R + 0x408u), hv = n2mdp_rd(R + 0x4d0u);
	N2MDP_LOG("NORD2 MDP DIAG %s: RDMA EN %#x INT_EN %#x INT_ST %#x CON %#x GMCIF %#x SRC_CON %#x BASE %#x END %#x\n", when,
	    n2mdp_rd(R + 0x0u), n2mdp_rd(R + 0x10u), n2mdp_rd(R + 0x18u), n2mdp_rd(R + 0x20u), n2mdp_rd(R + 0x28u), n2mdp_rd(R + 0x30u),
	    n2mdp_rd(R + 0xf00u), n2mdp_rd(R + 0x100u));
	N2MDP_LOG("NORD2 MDP DIAG %s: RDMA PITCH %#x SIZE %#x CLIP %#x OFS %#x | MON0 %#x (ack %u req %u) MON1 %#x (state %#x grep %u) h/v %u/%u\n",
	    when, n2mdp_rd(R + 0x60u), n2mdp_rd(R + 0x70u), n2mdp_rd(R + 0x78u), n2mdp_rd(R + 0x118u), m0, (m0 >> 11) & 1u, (m0 >> 10) & 1u, m1,
	    (m1 >> 8) & 0x7ffu, (m1 >> 20) & 1u, hv & 0xfffu, (hv >> 16) & 0xfffu);
	uint32_t dbg[8];
	for (unsigned k = 0; k < 8; k++) { n2mdp_wr(W + 0x018u, (k + 1u) << 8); dbg[k] = n2mdp_rd(W + 0x0d0u); }
	N2MDP_LOG("NORD2 MDP DIAG %s: WROT CTRL %#x MAIN_BUF %#x SUB_BUF %#x INT %#x TAR %#x IN %#x STRIDE %#x BASE %#x OFST %#x\n", when,
	    n2mdp_rd(W + 0x0u), n2mdp_rd(W + 0x8u), n2mdp_rd(W + 0xcu), n2mdp_rd(W + 0x1cu), n2mdp_rd(W + 0x24u), n2mdp_rd(W + 0x78u),
	    n2mdp_rd(W + 0x30u), n2mdp_rd(W + 0xf00u), n2mdp_rd(W + 0x2cu));
	N2MDP_LOG("NORD2 MDP DIAG %s: WROT ROT_EN %#x RST %#x/%#x EOL %#x PENDING0 %#x FRAME %#x | dbg1-8 %#x %#x %#x %#x %#x %#x %#x %#x\n", when,
	    n2mdp_rd(W + 0x7cu), n2mdp_rd(W + 0x10u), n2mdp_rd(W + 0x14u), n2mdp_rd(W + 0x70u), n2mdp_rd(W + 0xe0u), n2mdp_rd(W + 0x28u),
	    dbg[0], dbg[1], dbg[2], dbg[3], dbg[4], dbg[5], dbg[6], dbg[7]);
	N2MDP_LOG("NORD2 MDP DIAG %s: MUTEX INTEN %#x INTSTA %#x (sof %u eof %u) EN %#x SOF %#x MOD0 %#x MOD1 %#x | ev wrot %u rdma %u\n", when,
	    n2mdp_rd(X + 0x0u), n2mdp_rd(X + 0x4u), (n2mdp_rd(X + 0x4u) >> mid) & 1u, (n2mdp_rd(X + 0x4u) >> (mid + 16u)) & 1u,
	    n2mdp_rd(N2MDP_MUTEX_EN(mid)), n2mdp_rd(N2MDP_MUTEX_SOF(mid)), n2mdp_rd(N2MDP_MUTEX_MOD0(mid)), n2mdp_rd(N2MDP_MUTEX_MOD1(mid)),
	    n2mdp_gce_event(N2MDP_EV_WROT_DONE(p->pipe)), n2mdp_gce_event(N2MDP_EV_RDMA_DONE(p->pipe)));
	N2MDP_LOG("NORD2 MDP DIAG %s: TOPO RDMA0 MOUT %#x RDMA2 MOUT %#x WROT0 SEL %#x WROT2 SEL %#x MASK %#x %#x %#x | UNDERRUN %#x id %#x\n", when,
	    n2mdp_rd(0xf20u), n2mdp_rd(0xf28u), n2mdp_rd(0xf70u), n2mdp_rd(0xf78u), n2mdp_rd(0xfd0u), n2mdp_rd(0xfd4u), n2mdp_rd(0xfd8u),
	    n2mdp_rd(0xe00u), n2mdp_rd(0xe04u));
	N2MDP_LOG("NORD2 MDP DIAG %s: DL_VALID %#x %#x %#x DL_READY %#x %#x %#x | CG %#x %#x %#x | M4U1 FAULT %#x\n", when,
	    n2mdp_rd(0xfe0u), n2mdp_rd(0xfe4u), n2mdp_rd(0xfe8u), n2mdp_rd(0xff0u), n2mdp_rd(0xff4u), n2mdp_rd(0xff8u),
	    n2mdp_rd(0x100u), n2mdp_rd(0x110u), n2mdp_rd(0x120u), n2mdp_rd(0x27000u + 0x134u));
	if (p->pipe == N2MDP_PIPE_SCALE) {   /* 0679: the PQ engines and the RSZ of the scale pipe */
		uint32_t A = N2MDP_W_AAL(2), Z = N2MDP_W_RSZ(2), T = N2MDP_W_TDSHP(2), C = N2MDP_W_TCC(2);
		N2MDP_LOG("NORD2 MDP DIAG %s: AAL2 EN %#x CFG %#x SIZE %#x OUT %#x OFS %#x 0x200 %#x | TCC2 %#x\n", when, n2mdp_rd(A), n2mdp_rd(A + 0x20u),
		    n2mdp_rd(A + 0x30u), n2mdp_rd(A + 0x34u), n2mdp_rd(A + 0x38u), n2mdp_rd(A + 0x200u), n2mdp_rd(C));
		N2MDP_LOG("NORD2 MDP DIAG %s: RSZ2 EN %#x C1 %#x C2 %#x IN %#x OUT %#x HS %#x VS %#x LH %#x/%#x\n", when, n2mdp_rd(Z), n2mdp_rd(Z + 4u),
		    n2mdp_rd(Z + 8u), n2mdp_rd(Z + 0x10u), n2mdp_rd(Z + 0x14u), n2mdp_rd(Z + 0x18u), n2mdp_rd(Z + 0x1cu), n2mdp_rd(Z + 0x20u), n2mdp_rd(Z + 0x24u));
		N2MDP_LOG("NORD2 MDP DIAG %s: TDSHP2 0x000 %#x CTRL %#x CFG %#x IN %#x OFS %#x OUT %#x WIN %#x %#x\n", when, n2mdp_rd(T),
		    n2mdp_rd(T + 0x100u), n2mdp_rd(T + 0x110u), n2mdp_rd(T + 0x120u), n2mdp_rd(T + 0x124u), n2mdp_rd(T + 0x128u), n2mdp_rd(T + 0x64u),
		    n2mdp_rd(T + 0x68u));
		N2MDP_LOG("NORD2 MDP DIAG %s: SEL RDMA2_MOUT %#x AAL2(PQ2)_SEL %#x RSZ2_SEL %#x WROT2_SEL %#x PQ0_SEL %#x MOUT_MASK %#x %#x %#x | MOD0 %#x MOD1 %#x\n",
		    when, n2mdp_rd(0xf28u), n2mdp_rd(0xf38u), n2mdp_rd(0xf68u), n2mdp_rd(0xf78u), n2mdp_rd(0xf30u), n2mdp_rd(0xfd0u), n2mdp_rd(0xfd4u),
		    n2mdp_rd(0xfd8u), n2mdp_rd(N2MDP_MUTEX_MOD0(p->mutex)), n2mdp_rd(N2MDP_MUTEX_MOD1(p->mutex)));
	}
	n2mdp_larb_dump(when);
}

/* 0691: engine reset of one pipe as the vendor does it (mt6893/mdp.c cmdqMdpResetEng + mdp_common.c cmdq_mdp_loop_reset): RDMA
 * reset 1 -> MON_STA_1 bit 8 -> 0; WROT SOFT_RST 1 -> RST_STAT 1 -> 0 -> RST_STAT 0 (the 0671 abort never polled the release; on
 * q10jfinal19 WROT2 stayed at RST_STAT 1 and every later job timed out at op 13); TDSHP CTRL 0 / 2 / 0 for the scale lane.  Engines
 * that do not come back: MDPSYS SW0_RST_B (mdpsys 0x700, 0 = reset; pipe 0 RDMA0 0 / WROT0 7, pipe 2 RDMA2 8 AAL2 9 RSZ2 10 TDSHP2 12
 * TCC2 13 WROT2 14 = cmdq_mdp_reset_with_mmsys) pulse + MOUT reset (0xf00 bit 6 RDMA0 / 8 RDMA2, old & ~b, old | b, old & ~b), then
 * checked again.  Returns true when both engines are idle; a pipe that stays stuck is marked dead (never used again this boot). */
#define N2MDP_SW0_RST_B   0x700u
#define N2MDP_MOUT_RST    0xf00u
static uint32_t n2mdp_lane_dead;          /* bit 0 = pipe 0, bit 1 = pipe 2 */
static uint32_t n2mdp_recoveries, n2mdp_mmsys_resets;
static bool
n2mdp_poll_bits(uint32_t off, uint32_t mask, uint32_t want, unsigned tries)
{
	for (unsigned i = 0; i < tries; i++) { if ((n2mdp_rd(off) & mask) == want) { return true; } n2mdp_delay(10); }
	return (n2mdp_rd(off) & mask) == want;
}
static bool
n2mdp_engine_reset(uint32_t pipe)
{
	uint32_t R = N2MDP_W_RDMA(pipe), W = N2MDP_W_WROT(pipe);
	n2mdp_wr(R + R_RESET, 1u);
	bool r_ok = n2mdp_poll_bits(R + R_MON1, 1u << 8, 1u << 8, 100);
	n2mdp_wr(R + R_RESET, 0);
	n2mdp_wr(W + W_ROT_EN, 0);
	n2mdp_wr(W + W_SOFT_RST, 1u);
	bool w_ok = n2mdp_poll_bits(W + W_SOFT_RST_STAT, 1u, 1u, 100);
	n2mdp_wr(W + W_SOFT_RST, 0);
	w_ok = n2mdp_poll_bits(W + W_SOFT_RST_STAT, 1u, 0u, 100) && w_ok;
	if (pipe == N2MDP_PIPE_SCALE) {
		uint32_t T = N2MDP_W_TDSHP(2) + 0x100u;
		n2mdp_wr(T, 0); n2mdp_wr(T, 2u); n2mdp_wr(T, 0);
	}
	return r_ok && w_ok;
}
static bool
n2mdp_recover(uint32_t pipe, const char *why)
{
	n2mdp_recoveries++;
	if (n2mdp_engine_reset(pipe)) { N2MDP_LOG("NORD2 MDP RECOVER pipe %u (%s): engine reset ok\n", pipe, why); return true; }
	uint32_t bits = pipe ? ((1u << 8) | (1u << 9) | (1u << 10) | (1u << 12) | (1u << 13) | (1u << 14)) : ((1u << 0) | (1u << 7));
	uint32_t mb = pipe ? (1u << 8) : (1u << 6);
	n2mdp_gce_lock();            /* shared mdpsys registers (the other pipe may be running: its bits stay 1 = not reset) */
	n2mdp_wr(N2MDP_SW0_RST_B, ~bits);
	n2mdp_wr(N2MDP_SW0_RST_B, ~0u);
	uint32_t mo = n2mdp_rd(N2MDP_MOUT_RST);
	n2mdp_wr(N2MDP_MOUT_RST, mo & ~mb); n2mdp_wr(N2MDP_MOUT_RST, mo | mb); n2mdp_wr(N2MDP_MOUT_RST, mo & ~mb);
	n2mdp_gce_unlock();
	n2mdp_mmsys_resets++;
	bool ok = n2mdp_engine_reset(pipe);
	if (!ok) { n2mdp_lane_dead |= pipe ? 2u : 1u; }
	N2MDP_LOG("NORD2 MDP RECOVER pipe %u (%s): engine reset failed -> SW0_RST_B pulse %#x: %s (RDMA MON1 %#x WROT RST_STAT %#x)\n", pipe, why,
	    bits, ok ? "recovered" : "STILL STUCK, pipe off for this boot", n2mdp_rd(N2MDP_W_RDMA(pipe) + R_MON1), n2mdp_rd(N2MDP_W_WROT(pipe) + W_SOFT_RST_STAT));
	return ok;
}

/* abort: stop the mutex, reset the engines of the pipe; never leaves a DMA running */
static void
n2mdp_abort(const struct n2mdp_prog *p, const char *why)
{
	uint32_t R = N2MDP_W_RDMA(p->pipe), W = N2MDP_W_WROT(p->pipe);
	n2mdp_diag(p, "timeout");
	N2MDP_LOG("NORD2 MDP ABORT (%s): mutex EN %#x INTSTA %#x MOD0 %#x | RDMA EN %#x MON0 %#x MON1 %#x | WROT CTRL %#x ROT_EN %#x "
	    "RST_STAT %#x | ev wrot %u rdma %u\n", why, n2mdp_rd(N2MDP_MUTEX_EN(p->mutex)), n2mdp_rd(N2MDP_W_MUTEX + 4u),
	    n2mdp_rd(N2MDP_MUTEX_MOD0(p->mutex)), n2mdp_rd(R + R_EN), n2mdp_rd(R + 0x400u), n2mdp_rd(R + R_MON1), n2mdp_rd(W + W_CTRL),
	    n2mdp_rd(W + W_ROT_EN), n2mdp_rd(W + W_SOFT_RST_STAT), n2mdp_gce_event(N2MDP_EV_WROT_DONE(p->pipe)),
	    n2mdp_gce_event(N2MDP_EV_RDMA_DONE(p->pipe)));
	n2mdp_wr(N2MDP_MUTEX_EN(p->mutex), 0);
	n2mdp_wr(R + R_EN, 0);
	if (p->pipe == N2MDP_PIPE_SCALE) { n2mdp_wr(N2MDP_W_RSZ(2) + Z_ENABLE, 0); }
	n2mdp_wr(N2MDP_MUTEX_MOD0(p->mutex), 0);
	(void)n2mdp_recover(p->pipe, why);
}

/* 0690: resumable executor.  n2mdp_x_step() runs the ops of one program until a WAIT whose tile is not finished (returns 0 =
 * pending) or until the end / an error (returns 1).  One exec per pipe in flight: a single thread can drive both pipes
 * (n2mdp_run2, the S2 dual job) and each service thread drives its own pipe (n2mdp_run_x).  Per-run statistics live in the exec,
 * not in globals (the 0689 globals were read after the lock was released: "tiles 0" lines for the 2048x1536 stream).
 * Timeout per tile: 200 ms of wall time (was ~9800 polls). */
struct n2mdp_xstat { uint32_t hw_us, run_us, tiles, polls, tile_us[4], tile_max_us; };
struct n2mdp_exec {
	const struct n2mdp_prog *p;
	uint32_t pc, k;               /* next op; polls of the current WAIT */
	uint64_t t_start, t0, busy;
	int err;
	bool done;
	struct n2mdp_xstat st;
};
#define N2MDP_TILE_TIMEOUT_US 200000u
static void
n2mdp_x_init(struct n2mdp_exec *x, const struct n2mdp_prog *p)
{
	memset(x, 0, sizeof(*x));
	x->p = p;
	x->t_start = n2mdp_now_us();
}
static int
n2mdp_x_step(struct n2mdp_exec *x)
{
	const struct n2mdp_prog *p = x->p;
	while (!x->done) {
		if (x->pc >= p->n) { x->done = true; break; }
		const struct n2mdp_op *o = &p->ops[x->pc];
		switch (o->op) {
		case N2MDP_OP_WRITE: {
			uint32_t v = o->mask == 0xffffffffu ? o->val : ((n2mdp_rd(o->off) & ~o->mask) | (o->val & o->mask));
			n2mdp_wr(o->off, v);
			x->pc++;
			break;
		}
		case N2MDP_OP_POLL: {
			unsigned k = 0;
			while ((n2mdp_rd(o->off) & o->mask) != o->val) {
				if (++k > 500) {
					n2mdp_abort(p, "poll");
					N2MDP_LOG("NORD2 MDP: poll timeout at op %u off %#x want %#x/%#x\n", x->pc, o->off, o->val, o->mask);
					x->err = EIO; x->done = true;
					return 1;
				}
				n2mdp_delay(10);
			}
			x->pc++;
			break;
		}
		case N2MDP_OP_GO:
			n2mdp_gce_clear(o->val);
			n2mdp_gce_clear(N2MDP_EV_RDMA_DONE(p->pipe));
			n2mdp_wr(N2MDP_W_MUTEX + 4u, ~((1u << p->mutex) | (1u << (p->mutex + 16u))));   /* INTSTA: 0 clears (sof, eof) */
			if (n2mdp_diag_logged < 2) { n2mdp_diag(p, "before GO"); }
			x->t0 = n2mdp_now_us();
			x->k = 0;
			n2mdp_wr(o->off, (n2mdp_rd(o->off) & ~1u) | 1u);
			if (n2mdp_diag_logged < 2) { n2mdp_delay(50); n2mdp_diag(p, "GO+50us"); }
			x->pc++;
			break;
		case N2MDP_OP_WAIT: {
			/* 0674: done = WROT frame-done GCE event OR the mutex end-of-frame status bit (INTSTA bit id+16 = eof, bit id = sof;
			 * mtk_drm_ddp.c mtk_disp_mutex_irq_handler); the q10jmdp3 log had sof set and eof clear at the timeout */
			if (!(n2mdp_gce_event(o->val) & 1u) && !((n2mdp_rd(N2MDP_W_MUTEX + 4u) >> (p->mutex + 16u)) & 1u)) {
				if (n2mdp_now_us() - x->t0 > N2MDP_TILE_TIMEOUT_US) {
					n2mx.timeouts++;
					n2mdp_abort(p, "frame-done event never came");
					x->err = ETIMEDOUT; x->done = true;
					return 1;
				}
				x->k++; x->st.polls++;
				return 0;
			}
			uint32_t dt = (uint32_t)(n2mdp_now_us() - x->t0);
			x->busy += dt;
			if (x->st.tiles < 4u) { x->st.tile_us[x->st.tiles] = dt; }
			if (dt > x->st.tile_max_us) { x->st.tile_max_us = dt; }
			x->st.tiles++;
			if (n2mdp_diag_logged < 3) {
				n2mdp_diag_logged++;
				N2MDP_LOG("NORD2 MDP: tile done after %u us: mutex EN %#x INTSTA %#x RDMA MON1 %#x ev wrot %u rdma %u\n", dt,
				    n2mdp_rd(o->off), n2mdp_rd(N2MDP_W_MUTEX + 4u), n2mdp_rd(N2MDP_W_RDMA(p->pipe) + R_MON1), n2mdp_gce_event(o->val),
				    n2mdp_gce_event(N2MDP_EV_RDMA_DONE(p->pipe)));
				n2mdp_diag(p, "done");
			}
			x->pc++;
			break;
		}
		case N2MDP_OP_END:
		default:
			x->done = true;
			break;
		}
	}
	x->st.hw_us = (uint32_t)x->busy;
	x->st.run_us = (uint32_t)(n2mdp_now_us() - x->t_start);
	return 1;
}
/* poll period: 5 us for the first ~600 polls of a tile, then 20 us (0687 values) */
static inline uint32_t n2mdp_poll_us(uint32_t k) { return k < 600u ? 5u : 20u; }

/* run a compiled program; returns 0, ETIMEDOUT (wait), EIO (poll) */
static int
n2mdp_run_x(const struct n2mdp_prog *p, struct n2mdp_xstat *st)
{
	struct n2mdp_exec x;
	n2mdp_x_init(&x, p);
	while (!n2mdp_x_step(&x)) { n2mdp_delay(n2mdp_poll_us(x.k)); }
	if (st) { *st = x.st; }
	return x.err;
}
/* 0690: two programs on different pipes at once, driven by this thread (S2 dual job) */
static void
n2mdp_run2(const struct n2mdp_prog *a, const struct n2mdp_prog *b, struct n2mdp_xstat *sa, struct n2mdp_xstat *sb, int *ea, int *eb,
    uint32_t *wall_us)
{
	struct n2mdp_exec xa, xb;
	uint64_t t0 = n2mdp_now_us();
	n2mdp_x_init(&xa, a);
	n2mdp_x_init(&xb, b);
	bool da = false, db = false;
	for (;;) {
		if (!da) { da = n2mdp_x_step(&xa) != 0; }
		if (!db) { db = n2mdp_x_step(&xb) != 0; }
		if (da && db) { break; }
		n2mdp_delay(n2mdp_poll_us(xa.k < xb.k ? xa.k : xb.k));
	}
	*sa = xa.st; *sb = xb.st; *ea = xa.err; *eb = xb.err;
	*wall_us = (uint32_t)(n2mdp_now_us() - t0);
}
/* legacy entry (stage tests, S3): also fills the 0689 globals read by the S2 TIMING log (exclusive lock held there) */
static struct n2mdp_xstat n2mdp_tr_st;      /* 0690: statistics of the last n2mdp_run (S3 TIMING log) */
static int
n2mdp_run(const struct n2mdp_prog *p, uint32_t *hw_us)
{
	struct n2mdp_xstat st;
	int e = n2mdp_run_x(p, &st);
	n2mdp_tr_st = st;
	if (hw_us) { *hw_us = st.hw_us; }
	n2mdp_last_tiles = st.tiles;
	for (unsigned i = 0; i < 4u; i++) { n2mdp_tile_us[i] = st.tile_us[i]; }
	return e;
}

/* ---- self-tests ----------------------------------------------------------------------------------------------------- */
struct n2mdp_test {
	const char *name;
	uint8_t sfmt, dfmt, rot, flip, full, tile16, exact, stage;
	uint16_t sw, sh, dw, dh;
	uint32_t cap;
	uint8_t diag;           /* 0682: S2 diagnostic job: logged as S2 DIAG, never fails / stops S2, no capability */
	uint8_t pipe2;          /* 0690: run the (unscaled) job on RDMA2 -> WROT2 */
	uint8_t iova;           /* 0690: S3 TIMING rows: addresses are IOVAs (larb ports translate through M4U1) */
};
static const struct n2mdp_test n2mdp_tests[] = {
	{ "copy256",     N2MF_BGRA, N2MF_BGRA, 0, 0, 0, 0,  1, 1, 256, 256, 256, 256, N2MDP_CAP_COPY, 0, 0, 0 },
	{ "rot90",       N2MF_BGRA, N2MF_BGRA, 1, 0, 0, 0,  1, 1, 256, 256, 256, 256, N2MDP_CAP_ROT90, 0, 0, 0 },
	/* 0678: S2 starts with the jobs the camera / VideoToolbox path needs: NV12 -> BGRA (video / full range, WROT matrix 6 / 4),
	 * NV12 -> BGRA rotated 90, NV12 -> BGRA 1/2 and 2x (RSZ alg 2 / alg 0); it stops at the first job that fails */
	{ "nv12v",       N2MF_NV12, N2MF_BGRA, 0, 0, 0, 0,  0, 2, 256, 256, 256, 256, N2MDP_CAP_NV12V, 0, 0, 0 },
	{ "nv12f",       N2MF_NV12, N2MF_BGRA, 0, 0, 1, 0,  0, 2, 256, 256, 256, 256, N2MDP_CAP_NV12F, 0, 0, 0 },
	{ "nv12rot90",   N2MF_NV12, N2MF_BGRA, 1, 0, 1, 0,  0, 2, 256, 256, 256, 256, N2MDP_CAP_NV12F, 0, 0, 0 },
	{ "scaledown",   N2MF_NV12, N2MF_BGRA, 0, 0, 1, 0,  0, 2, 512, 512, 256, 256, N2MDP_CAP_SCALE_DN, 0, 0, 0 },
	{ "scaleup",     N2MF_NV12, N2MF_BGRA, 0, 0, 1, 0,  0, 2, 128, 128, 256, 256, N2MDP_CAP_SCALE_UP, 0, 0, 0 },
	{ "rot180",      N2MF_BGRA, N2MF_BGRA, 2, 0, 0, 0,  1, 2, 256, 128, 256, 128, N2MDP_CAP_ROT180, 0, 0, 0 },
	{ "rot270",      N2MF_BGRA, N2MF_BGRA, 3, 0, 0, 0,  1, 2, 256, 128, 128, 256, N2MDP_CAP_ROT270, 0, 0, 0 },
	{ "flip",        N2MF_BGRA, N2MF_BGRA, 0, 1, 0, 0,  1, 2, 256, 128, 256, 128, N2MDP_CAP_FLIP, 0, 0, 0 },
	{ "tilecopy",    N2MF_BGRA, N2MF_BGRA, 0, 0, 0, 16, 1, 2, 1024, 64, 1024, 64, N2MDP_CAP_TILE, 0, 0, 0 },
	{ "tilerot90",   N2MF_BGRA, N2MF_BGRA, 1, 0, 0, 16, 1, 2, 1024, 64, 64, 1024, N2MDP_CAP_TILE, 0, 0, 0 },
	{ "scalerot",    N2MF_NV12, N2MF_BGRA, 1, 0, 1, 0,  0, 2, 512, 256, 128, 256, N2MDP_CAP_SCALE_ROT, 0, 0, 0 },
	/* 0682 S2 DIAG jobs (run after the 13 S2 jobs, never stop S2):
	 *   rgbscale1    BGRA 512x256 -> 256x128, 1 tile: RGB source path (RDMA RGB->YUV matrix 0 [V-dis: JPEG profile -> RDMA mtx 0,
	 *                WROT mtx 4]) without tiling
	 *   nv12scale3t  NV12 1024x64 -> 768x48, 3 tiles of 256: tiling without the RGB path
	 *   nv12scale3o  NV12 768x64 -> 1024x48 (horizontal 6-tap upscale, alg 0), 4 tiles: tiling with the 6-tap filter
	 *   rgbscale3t   BGRA 1024x64 -> 768x48, 3 tiles = the q10jmdp10 scaletile job */
	{ "rgbscale1",   N2MF_BGRA, N2MF_BGRA, 0, 0, 0, 0,  0, 2, 512, 256, 256, 128, 0, 1, 0, 0 },
	{ "nv12scale3t", N2MF_NV12, N2MF_BGRA, 0, 0, 1, 16, 0, 2, 1024, 64, 768, 48, 0, 1, 0, 0 },
	{ "nv12scale3o", N2MF_NV12, N2MF_BGRA, 0, 0, 1, 16, 0, 2, 768, 64, 1024, 48, 0, 1, 0, 0 },
	{ "rgbscale3t",  N2MF_BGRA, N2MF_BGRA, 0, 0, 0, 16, 0, 2, 1024, 64, 768, 48, 0, 1, 0, 0 },
	/* 0683: RGB-source scaling on a SMOOTH pattern (no 8-px blue checkerboard, no marker).  The vendor scale pipe carries RGB sources
	 * as YUV (RDMA RGB->YUV, WROT YUV->RGB [V-dis]) and the PQ/RSZ chain resamples chroma, so sharp chroma edges cannot survive
	 * exactly (q10jmdp12s3 rgbscale1: errors only at the checkerboard edges, interior pixels right).  These capability jobs
	 * (diag 2) never stop S2; when both pass, N2MDP_CAP_RGB_SCALE allows RGB-source scaling in the service/blit path.  Accepted
	 * tolerance = the S2 non-exact rule (mean <= 6, <= 1 % of samples over 32) on smooth content. */
	{ "rgbscale1s",  N2MF_BGRA, N2MF_BGRA, 0, 0, 0, 0,  0, 2, 512, 256, 256, 128, N2MDP_CAP_RGB_SCALE, 2, 0, 0 },
	{ "rgbscale3s",  N2MF_BGRA, N2MF_BGRA, 0, 0, 0, 16, 0, 2, 1024, 64, 768, 48, N2MDP_CAP_RGB_SCALE, 2, 0, 0 },
	/* 0686: NV12 -> NV12 (the camera's real request is 420f 1504x1128 -> 420f 1128x1504 rotated 90; 752x564 here = same aspect, fits
	 * the test buffers).  Capability jobs (diag 3): all must pass for N2MDP_CAP_NV12_DST; they never stop S2. */
	{ "nv2nv",       N2MF_NV12, N2MF_NV12, 0, 0, 1, 0,  0, 2, 256, 256, 256, 256, N2MDP_CAP_NV12_DST, 3, 0, 0 },
	{ "nv2nvrot90",  N2MF_NV12, N2MF_NV12, 1, 0, 1, 0,  0, 2, 256, 256, 256, 256, N2MDP_CAP_NV12_DST, 3, 0, 0 },
	{ "nv2nvrot180", N2MF_NV12, N2MF_NV12, 2, 0, 1, 0,  0, 2, 256, 256, 256, 256, N2MDP_CAP_NV12_DST, 3, 0, 0 },
	{ "nv2nvrot270", N2MF_NV12, N2MF_NV12, 3, 0, 1, 0,  0, 2, 256, 256, 256, 256, N2MDP_CAP_NV12_DST, 3, 0, 0 },
	{ "nv2nvcam90",  N2MF_NV12, N2MF_NV12, 1, 0, 1, 0,  0, 2, 752, 564, 564, 752, N2MDP_CAP_NV12_DST, 3, 0, 0 },
	/* 0689: timing jobs (diag 5, logged as S2 TIMING, no capability): 1024x768 NV12 -> NV12 rot 0 vs rot 90 and rot 90 with tile widths
	 * 512 / 256 / 128 / 64 px -> rotation penalty and whether narrow rotation tiles write DRAM faster (the 1504x1128 camera rotation
	 * runs at ~0.45 px/clk at 594 MHz) */
	{ "tim0_512",    N2MF_NV12, N2MF_NV12, 0, 0, 1, 32, 0, 2, 1024, 768, 1024, 768, 0, 5, 0, 0 },
	{ "tim90_512",   N2MF_NV12, N2MF_NV12, 1, 0, 1, 32, 0, 2, 1024, 768, 768, 1024, 0, 5, 0, 0 },
	{ "tim90_256",   N2MF_NV12, N2MF_NV12, 1, 0, 1, 16, 0, 2, 1024, 768, 768, 1024, 0, 5, 0, 0 },
	{ "tim90_128",   N2MF_NV12, N2MF_NV12, 1, 0, 1, 8,  0, 2, 1024, 768, 768, 1024, 0, 5, 0, 0 },
	{ "tim90_64",    N2MF_NV12, N2MF_NV12, 1, 0, 1, 4,  0, 2, 1024, 768, 768, 1024, 0, 5, 0, 0 },
	/* 0690: pipe-2 direct lane (RDMA2 -> WROT2) capability jobs (diag 6): the pipe-0 unscaled S1/S2 set again on pipe 2; all must pass
	 * (+ the dual job, both pipes at once) for N2MDP_CAP_DUAL, which lets the service run two unscaled requests at the same time */
	{ "p2copy256",   N2MF_BGRA, N2MF_BGRA, 0, 0, 0, 0,  1, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2rot90",     N2MF_BGRA, N2MF_BGRA, 1, 0, 0, 0,  1, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2nv12v",     N2MF_NV12, N2MF_BGRA, 0, 0, 0, 0,  0, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2nv12f",     N2MF_NV12, N2MF_BGRA, 0, 0, 1, 0,  0, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2nv12rot90", N2MF_NV12, N2MF_BGRA, 1, 0, 1, 0,  0, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2rot180",    N2MF_BGRA, N2MF_BGRA, 2, 0, 0, 0,  1, 2, 256, 128, 256, 128, 0, 6, 1, 0 },
	{ "p2rot270",    N2MF_BGRA, N2MF_BGRA, 3, 0, 0, 0,  1, 2, 256, 128, 128, 256, 0, 6, 1, 0 },
	{ "p2flip",      N2MF_BGRA, N2MF_BGRA, 0, 1, 0, 0,  1, 2, 256, 128, 256, 128, 0, 6, 1, 0 },
	{ "p2tilecopy",  N2MF_BGRA, N2MF_BGRA, 0, 0, 0, 16, 1, 2, 1024, 64, 1024, 64, 0, 6, 1, 0 },
	{ "p2tilerot90", N2MF_BGRA, N2MF_BGRA, 1, 0, 0, 16, 1, 2, 1024, 64, 64, 1024, 0, 6, 1, 0 },
	{ "p2nv2nv",     N2MF_NV12, N2MF_NV12, 0, 0, 1, 0,  0, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2nv2nvr90",  N2MF_NV12, N2MF_NV12, 1, 0, 1, 0,  0, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2nv2nvr180", N2MF_NV12, N2MF_NV12, 2, 0, 1, 0,  0, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2nv2nvr270", N2MF_NV12, N2MF_NV12, 3, 0, 1, 0,  0, 2, 256, 256, 256, 256, 0, 6, 1, 0 },
	{ "p2nv2nvcam",  N2MF_NV12, N2MF_NV12, 1, 0, 1, 0,  0, 2, 752, 564, 564, 752, 0, 6, 1, 0 },
	{ "p2tim90_512", N2MF_NV12, N2MF_NV12, 1, 0, 1, 32, 0, 2, 1024, 768, 768, 1024, 0, 5, 1, 0 },
	/* 0691: large-frame downscale the way the service now tiles it (ratio ~0.63 like the mediaanalysisd 3088x2320 -> 2016x1514 request,
	 * 1024 rows, input tiles <= 512 px: tile16 = n2mdp_scale_tile16(1024, 640) = 19 -> 304 px, 3 tiles) -> N2MDP_CAP_SCALE_BIG */
	{ "scbig",       N2MF_NV12, N2MF_BGRA, 0, 0, 1, 19, 0, 2, 1024, 1024, 640, 640, N2MDP_CAP_SCALE_BIG, 8, 0, 0 },
	/* 0690: S3 TIMING rows (stage 4 = run by n2mdp_s3_timing after S3 passed, never by the stage loop): the S2 timing frame again,
	 * physical vs through M4U1 (pool pages mapped linearly, so only the translation differs), and the IOVA tile widths */
	{ "phys90_512",  N2MF_NV12, N2MF_NV12, 1, 0, 1, 32, 0, 4, 1024, 768, 768, 1024, 0, 7, 0, 0 },
	{ "iova90_512",  N2MF_NV12, N2MF_NV12, 1, 0, 1, 32, 0, 4, 1024, 768, 768, 1024, 0, 7, 0, 1 },
	{ "iova90_256",  N2MF_NV12, N2MF_NV12, 1, 0, 1, 16, 0, 4, 1024, 768, 768, 1024, 0, 7, 0, 1 },
	{ "iova90_128",  N2MF_NV12, N2MF_NV12, 1, 0, 1, 8,  0, 4, 1024, 768, 768, 1024, 0, 7, 0, 1 },
	{ "iova90_64",   N2MF_NV12, N2MF_NV12, 1, 0, 1, 4,  0, 4, 1024, 768, 768, 1024, 0, 7, 0, 1 },
	{ "iova0_512",   N2MF_NV12, N2MF_NV12, 0, 0, 1, 32, 0, 4, 1024, 768, 1024, 768, 0, 7, 0, 1 },
};
#define N2MDP_NTESTS (sizeof(n2mdp_tests) / sizeof(n2mdp_tests[0]))

static struct n2mdp_prog n2mdp_prog_buf;     /* one program at a time (under n2mdp_lock) */
static struct n2mdp_prog n2mdp_pprog[2];     /* 0690: per-pipe programs of the service (under that pipe's lock) and of the dual job */

static bool
n2mdp_pool_get(void)
{
	if (n2mx.pool_ok) { return true; }
#ifdef N2MDP_HOST
	n2mx.pool_ok = n2mdp_host_pool(N2MDP_POOL_BYTES, &n2mx.pool_pa, &n2mx.pool_va);
#else
	n2vc_lock();
	uint32_t pg = n2vc_pool_take(N2MDP_POOL_BYTES / 4096u);
	n2vc_unlock();
	if (pg == 0xffffffffu) { N2MDP_LOG("NORD2 MDP: no %u KiB in the vcodec pool\n", N2MDP_POOL_BYTES >> 10); return false; }
	n2mx.pool_pa = RW_CLAIM_VCODEC_PA + pg * 4096u;
	n2mx.pool_va = ml_io_map_unmappable(n2mx.pool_pa, N2MDP_POOL_BYTES, VM_WIMG_DEFAULT);
	if (!n2mx.pool_va) {
		n2vc_lock(); for (uint32_t k = 0; k < N2MDP_POOL_BYTES / 4096u; k++) { n2vc_pg_mark(pg + k, false); } n2vc_unlock();
		return false;
	}
	n2mx.pool_ok = true;
#endif
	N2MDP_LOG("NORD2 MDP: test buffers pa %#x+%#x (vcodec pool)\n", n2mx.pool_pa, N2MDP_POOL_BYTES);
	return n2mx.pool_ok;
}

static void
n2mdp_img_setup(struct n2mdp_img *im, uint32_t fmt, uint32_t w, uint32_t h, uint32_t base_pa, uint8_t full)
{
	im->fmt = fmt; im->w = w; im->h = h; im->full_range = full; im->bt709 = 0;
	im->pitch[0] = (w * n2mf_bpp(fmt) + 63u) & ~63u;
	im->addr[0] = base_pa;
	if (n2mf_is_yuv(fmt)) {
		im->pitch[1] = (w + 63u) & ~63u;
		im->addr[1] = base_pa + ((im->pitch[0] * h + 4095u) & ~4095u);
	} else { im->pitch[1] = 0; im->addr[1] = 0; }
}
static uint32_t n2mdp_img_bytes(const struct n2mdp_img *im)
{
	return (uint32_t)(n2mf_is_yuv(im->fmt) ? (im->addr[1] - im->addr[0]) + n2mdp_plane_bytes(im, 1) : n2mdp_plane_bytes(im, 0));
}

/* returns 0 pass; 2 compile, 3 timeout/poll, 4 guard, 5 compare */
static int
n2mdp_test_run(const struct n2mdp_test *t, uint32_t src_pa, vm_offset_t src_va, uint32_t dst_pa, vm_offset_t dst_va, uint32_t *out_crc)
{
	struct n2mdp_job j = { 0 };
	n2mdp_img_setup(&j.src, t->sfmt, t->sw, t->sh, src_pa, t->full);
	n2mdp_img_setup(&j.dst, t->dfmt, t->dw, t->dh, dst_pa, 0);
	j.rot = t->rot; j.flip = t->flip; j.tile_w = t->tile16; j.rgb_swap = (uint8_t)n2mx.rgb_swap; j.wrot_lines = (uint8_t)n2mx.wrot_lines;
	j.allow_unverified = t->diag;
	j.pipe2 = t->pipe2;     /* 0690 */
	uint8_t *s0 = (uint8_t *)src_va, *s1 = n2mf_is_yuv(j.src.fmt) ? (uint8_t *)src_va + (j.src.addr[1] - j.src.addr[0]) : NULL;
	uint32_t sbytes = n2mdp_img_bytes(&j.src), dbytes = n2mdp_img_bytes(&j.dst);
	n2mdp_pattern(&j.src, s0, s1, (uint32_t)(t - n2mdp_tests) | (t->diag == 2 ? 0x100u : 0u));
	memset((void *)dst_va, N2MDP_GUARD_BYTE, N2MDP_BUF_SPAN);
	memset((void *)(dst_va - N2MDP_PREGUARD), N2MDP_GUARD_BYTE, N2MDP_PREGUARD);   /* 0677: guard BELOW the destination too */
	n2mdp_flush(src_va, sbytes);
	n2mdp_flush(dst_va, N2MDP_BUF_SPAN);
	n2mdp_flush(dst_va - N2MDP_PREGUARD, N2MDP_PREGUARD);
	struct n2mdp_prog *p = &n2mdp_prog_buf;
	int retry_swap = 0;
again:
	if (n2mdp_compile(&j, p) != 0) { N2MDP_LOG("NORD2 MDP: %s: compile error %d\n", t->name, p->err); return 2; }
	uint32_t us = 0;
	n2mdp_larb_mode(t->iova != 0);            /* 0690: S3 TIMING rows translate through M4U1 */
	int e = n2mdp_run(p, &us);
	if (t->iova) { n2mdp_larb_mode(false); }
	n2mdp_flush(dst_va, N2MDP_BUF_SPAN);
	n2mx.jobs++;
	if (e) {
		n2mx.job_fail++;
		const uint8_t *dd = (const uint8_t *)dst_va;
		uint32_t changed = 0, first = 0xffffffffu, last = 0;
		for (uint32_t k = 0; k < N2MDP_BUF_SPAN; k++) { if (dd[k] != N2MDP_GUARD_BYTE) { changed++; if (first == 0xffffffffu) { first = k; } last = k; } }
		const uint8_t *ss = (const uint8_t *)src_va;
		N2MDP_LOG("NORD2 MDP: %s failed (%s): destination bytes written %u of %u (first %#x last %#x), first src bytes %02x%02x%02x%02x dst %02x%02x%02x%02x\n",
		    t->name, e == ETIMEDOUT ? "timeout" : "poll", changed, dbytes, first, last, ss[0], ss[1], ss[2], ss[3], dd[0], dd[1], dd[2], dd[3]);
		return 3;
	}
	n2mx.last_us = us; if (us > n2mx.max_us) { n2mx.max_us = us; }
	const uint8_t *d = (const uint8_t *)dst_va;
	uint32_t bad_guard = 0;
	n2mdp_flush(dst_va - N2MDP_PREGUARD, N2MDP_PREGUARD);
	for (uint32_t k = dbytes; k < N2MDP_BUF_SPAN; k += 61u) { if (d[k] != N2MDP_GUARD_BYTE) { bad_guard++; } }
	if (bad_guard) { N2MDP_LOG("NORD2 MDP: %s: %u guard bytes overwritten after %u bytes of output\n", t->name, bad_guard, dbytes); return 4; }
	{	/* 0677: the 1 MiB below the destination (rot90 with the 0671 offset walked backwards from the buffer start) */
		const uint8_t *g = (const uint8_t *)(dst_va - N2MDP_PREGUARD);
		uint32_t nb = 0, lo = 0xffffffffu;
		for (uint32_t k = 0; k < N2MDP_PREGUARD; k++) { if (g[k] != N2MDP_GUARD_BYTE) { nb++; if (lo == 0xffffffffu) { lo = k; } } }
		if (nb) {
			N2MDP_LOG("NORD2 MDP: %s: %u bytes written BELOW the destination (lowest at dst-%#x)\n", t->name, nb, N2MDP_PREGUARD - lo);
			return 4;
		}
	}
	struct n2mdp_cmp c;
	int m = n2mf_is_yuv(j.dst.fmt) ? n2mdp_compare_nv12(&j, s0, s1, d, d + (j.dst.addr[1] - j.dst.addr[0]), &c)   /* 0686 */
	    : n2mdp_compare(&j, s0, s1, d, t->exact, &c);
	*out_crc = n2mdp_crc32(d, dbytes, 0);
	N2MDP_LOG("NORD2 MDP: %s %ux%u %s -> %ux%u rot %u flip %u tiles %u pipe %u: %s (max err %u mean %u.%02u over %u/%u first %u,%u "
	    "swapped-better %u) hw %u us crc %08x\n", t->name, t->sw, t->sh, n2mf_is_yuv(t->sfmt) ? (t->full ? "420f" : "420v") : "BGRA",
	    t->dw, t->dh, t->rot, t->flip, p->tiles, p->pipe, m ? "MISMATCH" : "ok", c.max_err, c.samples ? c.sum_err / c.samples : 0,
	    c.samples ? (c.sum_err * 100u / c.samples) % 100u : 0, c.over, c.samples, c.first_x, c.first_y, c.swapped_better, us, *out_crc);
	if (m) {	/* 0677: where did the hardware write?  count + first/last non-guard byte + the first 4 output pixels */
		uint32_t nw = 0, fi = 0xffffffffu, la = 0;
		for (uint32_t k = 0; k < N2MDP_BUF_SPAN; k++) { if (d[k] != N2MDP_GUARD_BYTE) { nw++; if (fi == 0xffffffffu) { fi = k; } la = k; } }
		N2MDP_LOG("NORD2 MDP: %s mismatch detail: %u of %u destination bytes written (first %#x last %#x) px0 %02x%02x%02x%02x px1 %02x%02x%02x%02x"
		    " px(w-1) %02x%02x%02x%02x\n", t->name, nw, dbytes, fi, la, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
		    d[(t->dw - 1u) * 4u], d[(t->dw - 1u) * 4u + 1u], d[(t->dw - 1u) * 4u + 2u], d[(t->dw - 1u) * 4u + 3u]);
	}
	if (m && c.swapped_better && !retry_swap) {
		retry_swap = 1; j.rgb_swap ^= 1u;
		memset((void *)dst_va, N2MDP_GUARD_BYTE, N2MDP_BUF_SPAN); n2mdp_flush(dst_va, N2MDP_BUF_SPAN);
		goto again;
	}
	if (!m && retry_swap) { n2mx.rgb_swap = j.rgb_swap; N2MDP_LOG("NORD2 MDP: R/B order calibrated: rgb_swap %u\n", n2mx.rgb_swap); }
	return m ? 5 : 0;
}

/* 0690: dual-pipe job: pipe 0 (RDMA0 -> WROT0) and pipe 2 (RDMA2 -> WROT2) started together by this thread, each on its own
 * buffers (A: SRC -> DST, B: SRC2 -> DST2), both outputs must be exact and stay inside their buffers.  Pass 0 runs A alone then B
 * alone (serial), pass 1 both at once: "wall" vs "serial" is what two concurrent service requests gain, and the per-pipe hw times
 * show how much each pipe slows down when both use DRAM.  1024x768 NV12 -> NV12, A rot 90, B rot 270. */
static bool
n2mdp_dual_guard_ok(vm_offset_t dva, uint32_t dbytes, uint32_t span)
{
	const uint8_t *d = (const uint8_t *)dva;
	for (uint32_t k = dbytes; k < span; k += 61u) { if (d[k] != N2MDP_GUARD_BYTE) { return false; } }
	return true;
}
static bool
n2mdp_dual_test(void)
{
	struct n2mdp_job ja, jb;
	const uint32_t W = 1024, H = 768;
	memset(&ja, 0, sizeof(ja)); memset(&jb, 0, sizeof(jb));
	n2mdp_img_setup(&ja.src, N2MF_NV12, W, H, n2mx.pool_pa + N2MDP_BUF_SRC, 1);
	n2mdp_img_setup(&ja.dst, N2MF_NV12, H, W, n2mx.pool_pa + N2MDP_BUF_DST, 0);
	n2mdp_img_setup(&jb.src, N2MF_NV12, W, H, n2mx.pool_pa + N2MDP_BUF_SRC2, 1);
	n2mdp_img_setup(&jb.dst, N2MF_NV12, H, W, n2mx.pool_pa + N2MDP_BUF_DST2, 0);
	ja.rot = 1; jb.rot = 3; jb.pipe2 = 1; ja.wrot_lines = jb.wrot_lines = (uint8_t)n2mx.wrot_lines;
	vm_offset_t sav = n2mx.pool_va + N2MDP_BUF_SRC, sbv = n2mx.pool_va + N2MDP_BUF_SRC2;
	vm_offset_t dav = n2mx.pool_va + N2MDP_BUF_DST, dbv = n2mx.pool_va + N2MDP_BUF_DST2;
	uint32_t s1off = ja.src.addr[1] - ja.src.addr[0], d1off = ja.dst.addr[1] - ja.dst.addr[0];
	uint32_t sbytes = n2mdp_img_bytes(&ja.src), dbytes = n2mdp_img_bytes(&ja.dst);
	if (N2MDP_BUF_SRC2 + sbytes > N2MDP_BUF_DST || dbytes > N2MDP_BUF_SPAN) { return false; }
	n2mdp_pattern(&ja.src, (uint8_t *)sav, (uint8_t *)sav + s1off, 61);
	n2mdp_pattern(&jb.src, (uint8_t *)sbv, (uint8_t *)sbv + s1off, 62);
	n2mdp_flush(sav, sbytes); n2mdp_flush(sbv, sbytes);
	struct n2mdp_xstat a1, b1, a2, b2;
	memset(&a1, 0, sizeof(a1)); memset(&b1, 0, sizeof(b1)); memset(&a2, 0, sizeof(a2)); memset(&b2, 0, sizeof(b2));
	uint32_t wall = 0, serial = 0;
	const char *why = NULL;
	for (int pass = 0; pass < 2 && !why; pass++) {
		memset((void *)dav, N2MDP_GUARD_BYTE, N2MDP_BUF_SPAN); n2mdp_flush(dav, N2MDP_BUF_SPAN);
		memset((void *)dbv, N2MDP_GUARD_BYTE, N2MDP_BUF_SPAN); n2mdp_flush(dbv, N2MDP_BUF_SPAN);
		if (n2mdp_compile(&ja, &n2mdp_pprog[0]) || n2mdp_compile(&jb, &n2mdp_pprog[1])) { why = "compile"; break; }
		if (n2mdp_pprog[0].pipe != N2MDP_PIPE_DIRECT || n2mdp_pprog[1].pipe != N2MDP_PIPE_SCALE || n2mdp_pprog[1].scaled) { why = "pipe choice"; break; }
		n2mdp_larb_mode(false);
		int ea = 0, eb = 0;
		if (pass == 0) {
			uint64_t t0 = n2mdp_now_us();
			ea = n2mdp_run_x(&n2mdp_pprog[0], &a1);
			if (!ea) { eb = n2mdp_run_x(&n2mdp_pprog[1], &b1); }
			serial = (uint32_t)(n2mdp_now_us() - t0);
		} else {
			n2mdp_run2(&n2mdp_pprog[0], &n2mdp_pprog[1], &a2, &b2, &ea, &eb, &wall);
		}
		n2mx.jobs += 2;
		n2mdp_flush(dav, N2MDP_BUF_SPAN); n2mdp_flush(dbv, N2MDP_BUF_SPAN);
		if (ea || eb) { n2mx.job_fail++; why = ea ? "pipe 0 did not finish" : "pipe 2 did not finish"; break; }
		if (!n2mdp_dual_guard_ok(dav, dbytes, N2MDP_BUF_SPAN) || !n2mdp_dual_guard_ok(dbv, dbytes, N2MDP_BUF_SPAN)) { why = "write outside a destination"; break; }
		struct n2mdp_cmp ca, cb;
		int ma = n2mdp_compare_nv12(&ja, (uint8_t *)sav, (uint8_t *)sav + s1off, (uint8_t *)dav, (uint8_t *)dav + d1off, &ca);
		int mb = n2mdp_compare_nv12(&jb, (uint8_t *)sbv, (uint8_t *)sbv + s1off, (uint8_t *)dbv, (uint8_t *)dbv + d1off, &cb);
		if (ma || mb || ca.max_err || cb.max_err) {
			N2MDP_LOG("NORD2 MDP S2 DUAL: pass %d mismatch: pipe 0 max err %u over %u, pipe 2 max err %u over %u\n", pass, ca.max_err, ca.over, cb.max_err, cb.over);
			why = "output mismatch";
		}
	}
	if (why) { N2MDP_LOG("NORD2 MDP S2 DUAL: FAILED (%s): dual-pipe service off\n", why); return false; }
	N2MDP_LOG("NORD2 MDP S2 DUAL: ok | alone p0 hw %u run %u, p2 hw %u run %u (serial %u us) | together p0 hw %u p2 hw %u wall %u us (%u%% of serial)\n",
	    a1.hw_us, a1.run_us, b1.hw_us, b1.run_us, serial, a2.hw_us, b2.hw_us, wall, serial ? wall * 100u / serial : 0u);
	return true;
}

static inline uint32_t n2mdp_bit(unsigned i) { return i < 32u ? 1u << i : 0u; }   /* 0690: the test table has more than 32 rows */
static uint32_t
n2mdp_stage_tests(unsigned stage, uint32_t *pass_mask, uint32_t *fail_mask)
{
	uint32_t pass = 0, fail = 0, code = 1, rgb_pass = 0, rgb_fail = 0, nvd_pass = 0, nvd_fail = 0, p2_n = 0, p2_pass = 0, p2_fail = 0;
	for (unsigned i = 0; i < N2MDP_NTESTS; i++) {
		const struct n2mdp_test *t = &n2mdp_tests[i];
		if (t->stage != stage) { continue; }
		uint32_t crc = 0;
		int r = n2mdp_test_run(t, n2mx.pool_pa + N2MDP_BUF_SRC, n2mx.pool_va + N2MDP_BUF_SRC, n2mx.pool_pa + N2MDP_BUF_DST,
		    n2mx.pool_va + N2MDP_BUF_DST, &crc);
		/* 0676: the very first job of the boot did not finish -> retry it with other WROT main-buffer line counts (each try
		 * ended with the abort's engine resets; at most 3 more tries of <= ~190 ms); the count that works is kept */
		static const uint8_t alt_lines[3] = { 16, 8, 4 };
		for (unsigned a = 0; r == 3 && stage == 1 && i == 0 && n2mx.jobs == n2mx.job_fail && a < 3; a++) {
			N2MDP_LOG("NORD2 MDP: %s retry %u with WROT main-buffer lines %u (previous %u timed out)\n", t->name, a + 1, alt_lines[a],
			    n2mx.wrot_lines ? n2mx.wrot_lines : 32u);
			n2mx.wrot_lines = alt_lines[a];
			r = n2mdp_test_run(t, n2mx.pool_pa + N2MDP_BUF_SRC, n2mx.pool_va + N2MDP_BUF_SRC, n2mx.pool_pa + N2MDP_BUF_DST,
			    n2mx.pool_va + N2MDP_BUF_DST, &crc);
			if (r != 3) { N2MDP_LOG("NORD2 MDP: WROT main-buffer lines %u: %s\n", alt_lines[a], r == 0 ? "PASS, kept" : "finished but failed"); }
		}
		if (r == 3 && stage == 1 && i == 0) { n2mx.wrot_lines = 0; }
		if (t->diag) {   /* 0682: separates the multi-tile / RGB-source scale questions; never affects S2 */
			N2MDP_LOG("NORD2 MDP S2 DIAG %s: %s (code %d)\n", t->name, r == 0 ? "PASS" : "FAIL", r ? r + 1 : 1);
			if (r == 0) { n2mx.s2_diag |= n2mdp_bit(i); }
			if (t->diag == 2) { if (r == 0 && !(rgb_fail)) { rgb_pass++; } else { rgb_fail = 1; } }
			if (t->diag == 3) { if (r == 0 && !nvd_fail) { nvd_pass++; } else { nvd_fail = 1; } }
			if (t->diag == 6) { p2_n++; if (r == 0 && !p2_fail) { p2_pass++; } else { p2_fail = 1; } }   /* 0690: pipe-2 direct lane */
			if (t->diag == 8 && r == 0) { n2mx.caps |= t->cap; N2MDP_LOG("NORD2 MDP S2: large-frame scaling enabled (%s passed)\n", t->name); }   /* 0691 */
			if (t->diag == 5) {   /* 0689: timing job: hw time per tile width / rotation (rotation penalty, tile-width effect) */
				N2MDP_LOG("NORD2 MDP S2 TIMING %s: %ux%u NV12 -> NV12 rot %u tile %u px: %s hw %u us over %u tiles (%u %u %u %u ...)\n", t->name, t->sw,
				    t->sh, t->rot, (t->tile16 ? t->tile16 : 32u) * 16u, r == 0 ? "ok" : "FAILED", n2mx.last_us, n2mdp_last_tiles,
				    n2mdp_tile_us[0], n2mdp_tile_us[1], n2mdp_tile_us[2], n2mdp_tile_us[3]);
			}
			continue;
		}
		if (r == 0) { pass |= n2mdp_bit(i); n2mx.caps |= t->cap; }
		else {
			fail |= n2mdp_bit(i); if (code == 1) { code = (uint32_t)r + 1u; }
			if (r == 3 || r == 4) { break; }     /* the hardware did not finish or wrote outside: stop this stage at once */
			if (stage == 2) {                    /* 0678: S2 stops at its first failing job */
				N2MDP_LOG("NORD2 MDP S2: stopped at the first failing job (%s, code %d); remaining S2 jobs skipped\n", t->name, r + 1);
				break;
			}
		}
	}
	/* a capability shared by several tests (TILE) counts only if all of them passed */
	for (unsigned i = 0; i < N2MDP_NTESTS; i++) { if (fail & n2mdp_bit(i)) { n2mx.caps &= ~n2mdp_tests[i].cap; } }
	if (stage == 2 && nvd_pass == 5u && !nvd_fail) { n2mx.caps |= N2MDP_CAP_NV12_DST; N2MDP_LOG("NORD2 MDP S2: NV12 destinations enabled (NV12 -> NV12 jobs passed)\n"); }
	if (stage == 2 && rgb_pass == 2u && !rgb_fail) { n2mx.caps |= N2MDP_CAP_RGB_SCALE; N2MDP_LOG("NORD2 MDP S2: RGB-source scaling enabled (smooth jobs passed)\n"); }
	if (stage == 2 && p2_n) {   /* 0690: second pipe for unscaled service requests: every pipe-2 job + the dual job (both pipes at once) */
		if (p2_pass == p2_n && !p2_fail && n2mdp_dual_test()) {
			n2mx.caps |= N2MDP_CAP_DUAL;
			N2MDP_LOG("NORD2 MDP S2: dual pipe enabled (%u pipe-2 jobs + dual job passed)\n", p2_n);
		} else if (p2_pass != p2_n || p2_fail) {
			N2MDP_LOG("NORD2 MDP S2: dual pipe off (pipe-2 jobs passed %u of %u)\n", p2_pass, p2_n);
		}
	}
	*pass_mask = pass; *fail_mask = fail;
	return fail ? code : 1u;
}

/* ---- stage 3: M4U1 ------------------------------------------------------------------------------------------------- */
#include "../../iokit/Kernel/arm/sweet_fb_iommu.h"
#define N2MDP_M4U1        0x27000u
static struct sweet_fb_iommu_pt n2mdp_pt;
static struct sweet_fb_iova n2mdp_iova;

static void
n2mdp_m4u_tlb_all(void)
{
	n2mdp_wr(N2MDP_M4U1 + 0x02cu, 3u);   /* INVLD_SEL */
	n2mdp_wr(N2MDP_M4U1 + 0x020u, 2u);   /* INVLDT: all */
	n2mdp_delay(5);
}

/* other masters on M4U1 (larb 4/8/9/13/16/18): refuse to arm while a powered one has an MMU_EN port (0410's rule) */
static int
n2mdp_m4u1_others_clear(void)
{
#ifdef N2MDP_HOST
	return 0;
#else
	static const struct { uint32_t pa, sta; } l[6] = {
		{ 0x1602e000u, 1u << 16 }, { 0x17810000u, 1u << 18 }, { 0x1502e000u, 1u << 12 },
		{ 0x1a001000u, 1u << 23 }, { 0x1a00f000u, 1u << 24 }, { 0x1a011000u, 1u << 26 } };
	static vm_offset_t va[6];
	uint32_t s0 = ni_rd(n2mdp.spm, N2MDP_PWR_STA), s1 = ni_rd(n2mdp.spm, N2MDP_PWR_STA2);
	for (unsigned k = 0; k < 6; k++) {
		if (!(s0 & s1 & l[k].sta)) { continue; }
		if (!va[k]) { va[k] = ml_io_map_unmappable(l[k].pa, 0x1000, VM_WIMG_IO); }
		if (!va[k]) { return -1; }
		for (unsigned p = 0; p < 32; p++) {
			if (ni_rd(va[k], 0x380u + 4u * p) & 1u) { N2MDP_LOG("NORD2 MDP S3: larb at %#x port %u translates (MMU_EN): M4U1 not armed\n", l[k].pa, p); return 1; }
		}
	}
	return 0;
#endif
}

static int
n2mdp_m4u1_arm(void)
{
	if (n2mx.iommu_on) { return 0; }
	uint32_t blk_pa = (n2mx.pool_pa + N2MDP_BUF_PT + 0xffffu) & ~0xffffu;
	vm_offset_t blk_va = n2mx.pool_va + (blk_pa - n2mx.pool_pa);
	if (blk_pa + SWEET_FB_IOMMU_BLOCK_BYTES > n2mx.pool_pa + N2MDP_POOL_BYTES) { return 10; }
	memset((void *)blk_va, 0, SWEET_FB_IOMMU_BLOCK_BYTES);
	if (!sweet_fb_iommu_pt_init(&n2mdp_pt, (volatile void *)blk_va, blk_pa)) { return 11; }
	n2mdp_flush(blk_va, SWEET_FB_IOMMU_BLOCK_BYTES);
	sweet_fb_iova_init(&n2mdp_iova);
	int o = n2mdp_m4u1_others_clear();
	if (o != 0) { return o < 0 ? 12 : 13; }
	uint32_t old = n2mdp_rd(N2MDP_M4U1 + 0x000u), ctrl = n2mdp_rd(N2MDP_M4U1 + 0x110u);
	n2mx.ptbase = (uint32_t)(n2mdp_pt.l1_pa & 0xffffff80u) | (uint32_t)((n2mdp_pt.l1_pa >> 32) & 7u);
	n2mdp_wr(N2MDP_M4U1 + 0x000u, n2mx.ptbase);
	n2mdp_wr(N2MDP_M4U1 + 0x114u, (uint32_t)((blk_pa + SWEET_FB_IOMMU_OFF_SCRATCH) & 0xffffff80u));   /* TFRP */
	n2mdp_m4u_tlb_all();
	uint32_t rb = n2mdp_rd(N2MDP_M4U1 + 0x000u);
	N2MDP_LOG("NORD2 MDP S3: M4U1 PT_BASE %#x -> %#x (read %#x), CTRL %#x, tables pa %#x\n", old, n2mx.ptbase, rb, ctrl, blk_pa);
	if (rb != n2mx.ptbase) { return 14; }
	n2mx.iommu_on = true;
	return 0;
}

/* map n 4 KiB pages (PA list) at a fresh IOVA range; returns IOVA or 0 */
static uint32_t
n2mdp_iova_map(const uint64_t *pages, uint32_t n)
{
	uint32_t iova = sweet_fb_iova_alloc(&n2mdp_iova, (uint64_t)n * 4096u);
	if (iova == SWEET_FB_IOVA_FAIL) { return 0; }
	for (uint32_t k = 0; k < n; k++) {
		if (!sweet_fb_iommu_map_page(&n2mdp_pt, iova + k * 4096u, pages[k])) { sweet_fb_iommu_unmap(&n2mdp_pt, iova, k); sweet_fb_iova_free(&n2mdp_iova, iova, (uint64_t)n * 4096u); return 0; }
	}
	/* 0687: clean only the L2 entries just written (4 bytes per page, linear in the window) instead of the whole 580 KiB block
	 * (the L1 entries of the window are written once by sweet_fb_iommu_pt_init and flushed at arm time) */
	n2mdp_flush((vm_offset_t)&n2mdp_pt.l2[(iova - SWEET_FB_IOMMU_WIN_BASE) >> 12], n * 4u);
	n2mdp_m4u_tlb_all();
	return iova;
}
static void
n2mdp_iova_unmap(uint32_t iova, uint32_t n)
{
	sweet_fb_iommu_unmap(&n2mdp_pt, iova, n);
	sweet_fb_iova_free(&n2mdp_iova, iova, (uint64_t)n * 4096u);
	n2mdp_flush((vm_offset_t)&n2mdp_pt.l2[(iova - SWEET_FB_IOMMU_WIN_BASE) >> 12], n * 4u);   /* 0687: range only */
	n2mdp_m4u_tlb_all();
}

/* 0687: IOVA mapping cache for the service path.  Key = FNV-1a over the page list + page count; a hit re-uses the IOVA range
 * (no table writes, no TLB work).  The MDP only reaches a cached range when a request's page list hashes to the same key, i.e. the
 * same physical pages, so a stale entry (surface freed) is never used for other memory; entries are evicted LRU (unmapped), and the
 * whole cache is dropped on an IOVA fault.  8 entries ~ the camera's buffer rotation.
 * 0690: two jobs can run at once, so an entry in use is pinned (busy count): eviction only takes idle entries, a fault drop marks
 * busy entries dead (unmapped when their last user releases them), and with every entry busy the job gets a temporary mapping
 * that is unmapped at its end.  Everything here runs under the map lock (or the exclusive MDP lock at boot). */
#define N2MDP_MC_N 8u
static struct { uint64_t key; uint32_t n, iova, lru, busy; bool dead; } n2mdp_mc[N2MDP_MC_N];
static uint32_t n2mdp_mc_tick, n2mdp_mc_hits, n2mdp_mc_miss, n2mdp_mc_temp;
static uint32_t n2mdp_fault_gen;                                           /* 0690: incremented at every IOVA fault seen */
uint32_t n2mdp_last_map_us, n2mdp_last_run_us, n2mdp_last_compile_us;     /* 0687/0688: read by the service log (C linkage) */
uint32_t n2mdp_last_lockwait_us, n2mdp_last_tiles, n2mdp_tile_us[16];    /* 0689: wait for the MDP lock (another request running), tiles */
static uint64_t
n2mdp_mc_key(const uint64_t *pages, uint32_t n)
{
	uint64_t h = 0xcbf29ce484222325ull ^ n;
	for (uint32_t k = 0; k < n; k++) { h ^= pages[k] >> 12; h *= 0x100000001b3ull; }
	return h ? h : 1u;
}
static void
n2mdp_mc_drop(void)
{
	for (unsigned i = 0; i < N2MDP_MC_N; i++) {
		if (!n2mdp_mc[i].key) { continue; }
		if (n2mdp_mc[i].busy) { n2mdp_mc[i].dead = true; continue; }   /* 0690: unmapped by the last n2mdp_mc_put */
		n2mdp_iova_unmap(n2mdp_mc[i].iova, n2mdp_mc[i].n); n2mdp_mc[i].key = 0;
	}
}
/* IOVA of the page list (0 = none) pinned for one job; *slot = cache entry, or N2MDP_MC_N for a temporary mapping */
static uint32_t
n2mdp_mc_get(const uint64_t *pages, uint32_t n, unsigned *slot)
{
	uint64_t key = n2mdp_mc_key(pages, n);
	*slot = N2MDP_MC_N;
	for (unsigned i = 0; i < N2MDP_MC_N; i++) {
		if (n2mdp_mc[i].key == key && n2mdp_mc[i].n == n && !n2mdp_mc[i].dead) {
			n2mdp_mc[i].lru = ++n2mdp_mc_tick; n2mdp_mc[i].busy++; n2mdp_mc_hits++; *slot = i;
			return n2mdp_mc[i].iova;
		}
	}
	for (int tries = 0; tries < 2; tries++) {
		unsigned victim = N2MDP_MC_N;
		uint32_t best = 0xffffffffu;
		for (unsigned i = 0; i < N2MDP_MC_N; i++) {      /* a free entry, else the least recently used idle one */
			if (!n2mdp_mc[i].key) { victim = i; break; }
			if (!n2mdp_mc[i].busy && n2mdp_mc[i].lru < best) { best = n2mdp_mc[i].lru; victim = i; }
		}
		if (victim < N2MDP_MC_N && n2mdp_mc[victim].key) { n2mdp_iova_unmap(n2mdp_mc[victim].iova, n2mdp_mc[victim].n); n2mdp_mc[victim].key = 0; }
		uint32_t iova = n2mdp_iova_map(pages, n);
		if (iova) {
			n2mdp_mc_miss++;
			if (victim == N2MDP_MC_N) { n2mdp_mc_temp++; return iova; }   /* every entry busy: temporary mapping */
			n2mdp_mc[victim].key = key; n2mdp_mc[victim].n = n; n2mdp_mc[victim].iova = iova; n2mdp_mc[victim].lru = ++n2mdp_mc_tick;
			n2mdp_mc[victim].busy = 1; n2mdp_mc[victim].dead = false; *slot = victim;
			return iova;
		}
		/* IOVA window fragmented: drop every idle entry and retry once */
		for (unsigned i = 0; i < N2MDP_MC_N; i++) {
			if (n2mdp_mc[i].key && !n2mdp_mc[i].busy) { n2mdp_iova_unmap(n2mdp_mc[i].iova, n2mdp_mc[i].n); n2mdp_mc[i].key = 0; }
		}
	}
	return 0;
}
/* end of a job's use of a range from n2mdp_mc_get */
static void
n2mdp_mc_put(unsigned slot, uint32_t iova, uint32_t n)
{
	if (!iova) { return; }
	if (slot >= N2MDP_MC_N) { n2mdp_iova_unmap(iova, n); return; }
	if (n2mdp_mc[slot].busy) { n2mdp_mc[slot].busy--; }
	if (!n2mdp_mc[slot].busy && n2mdp_mc[slot].dead) {
		n2mdp_iova_unmap(n2mdp_mc[slot].iova, n2mdp_mc[slot].n);
		n2mdp_mc[slot].key = 0; n2mdp_mc[slot].dead = false;
	}
}

#ifndef N2MDP_HOST
extern addr64_t kvtophys(vm_offset_t va);
#endif

/* stage 3 test: BGRA 256x256 copy from the pool (IOVA) to scattered kernel pages above 4 GiB (IOVA), checked by the CPU */
static uint32_t
n2mdp_stage3(void)
{
	int a = n2mdp_m4u1_arm();
	if (a) { N2MDP_LOG("NORD2 MDP S3: M4U1 not armed (code %d)\n", a); return (uint32_t)a; }
	const uint32_t W = 256, H = 256, bytes = W * H * 4u, n = bytes / 4096u;
	static uint64_t spages[64], dpages[64];
	vm_offset_t dva = 0;
#ifdef N2MDP_HOST
	if (!n2mdp_host_alloc_hi(bytes, &dva, dpages)) { return 20; }
#else
	if (kmem_alloc(kernel_map, &dva, bytes, KMA_KOBJECT | KMA_ZERO, VM_KERN_MEMORY_DIAG) != KERN_SUCCESS) { return 20; }
	for (uint32_t k = 0; k < n; k++) { dpages[k] = kvtophys(dva + k * 4096u); if (!dpages[k]) { return 21; } }
#endif
	/* destination pages are used in REVERSE order through the IOVA range: the translation must be page-exact */
	uint64_t rev[64];
	for (uint32_t k = 0; k < n; k++) { rev[k] = dpages[n - 1u - k]; }
	for (uint32_t k = 0; k < n; k++) { spages[k] = (uint64_t)n2mx.pool_pa + N2MDP_BUF_SRC + k * 4096u; }
	uint32_t si = n2mdp_iova_map(spages, n), di = si ? n2mdp_iova_map(rev, n) : 0;
	if (!si || !di) { if (si) { n2mdp_iova_unmap(si, n); } return 22; }
	struct n2mdp_job j = { 0 };
	n2mdp_img_setup(&j.src, N2MF_BGRA, W, H, si, 0);
	n2mdp_img_setup(&j.dst, N2MF_BGRA, W, H, di, 0);
	j.rgb_swap = (uint8_t)n2mx.rgb_swap;
	uint8_t *s0 = (uint8_t *)(n2mx.pool_va + N2MDP_BUF_SRC);
	n2mdp_pattern(&j.src, s0, NULL, 99);
	memset((void *)dva, N2MDP_GUARD_BYTE, bytes);
	n2mdp_flush((vm_offset_t)s0, bytes); n2mdp_flush(dva, bytes);
	uint32_t res = 0, us = 0;
	if (n2mdp_compile(&j, &n2mdp_prog_buf)) { res = 23; goto out; }
	n2mdp_larb_mode(true);
	int e = n2mdp_run(&n2mdp_prog_buf, &us);
	n2mdp_larb_mode(false);
	uint32_t fault = n2mdp_rd(N2MDP_M4U1 + 0x134u), fva = n2mdp_rd(N2MDP_M4U1 + 0x13cu);
	if (fault) { n2mdp_wr(N2MDP_M4U1 + 0x120u, n2mdp_rd(N2MDP_M4U1 + 0x120u) | (1u << 12)); }
	if (e) { res = 24; goto out; }
	/* reassemble: logical page k of the image lives in dpages[n-1-k] = the kernel VA page (n-1-k) */
	{
		static uint8_t img[256 * 256 * 4];
		for (uint32_t k = 0; k < n; k++) { memcpy(img + k * 4096u, (const void *)(dva + (n - 1u - k) * 4096u), 4096u); }
		n2mdp_flush(dva, bytes);
		struct n2mdp_cmp c;
		int m = n2mdp_compare(&j, s0, NULL, img, true, &c);
		N2MDP_LOG("NORD2 MDP S3: IOVA copy %s (src iova %#x pool, dst iova %#x -> %u pages above 4 GiB, first pa %#llx), max err %u, "
		    "fault %#x va %#x, hw %u us\n", m ? "MISMATCH" : "ok", si, di, n, (unsigned long long)dpages[0], c.max_err, fault, fva, us);
		res = (m || fault) ? 25u : 1u;
	}
out:
	n2mdp_iova_unmap(di, n);
	n2mdp_iova_unmap(si, n);
#ifndef N2MDP_HOST
	kmem_free(kernel_map, dva, bytes);
#endif
	return res;
}

/* ---- 0690: S3 TIMING, vendor M4U1 write setup, service rotation tile width ---------------------------------------------
 * Runs once after S3 passed (exclusive MDP lock).  The S2 TIMING frame (1024x768 NV12 rot 90, ~1.4 ms physical = ~1 px/clk) is run
 * physical and through M4U1 with the SAME pool pages mapped linearly, so the difference is the translation alone; then IOVA tile
 * widths 256 / 128 / 64.  The camera requests ran at ~0.45 px/clk through IOVA (q10jmdp19: 2190 us per 512x1128 tile) while the
 * physical S2 jobs ran at ~1 px/clk: this tells translation cost from DRAM contention.
 * M4U1 is left by the boot chain as reset (PT_BASE 0); Android's mtk_iommu_hw_init (nord2-current-kernel drivers/iommu/
 * mtk_iommu_v2.c:4080, mt6885 mt_iommu_plat.h) clears MISC_CTRL IN_ORDER_WR_EN (bits 1/17, CONFIG_MTK_SMI_EXT, MM IOMMUs) and
 * WR_LEN_CTRL THROT_DIS (bits 5/21).  Applied here only when no other M4U1 master domain (VDEC/VENC/CAM larbs) is powered,
 * measured before / after, and put back if a job fails, faults or is >10 % slower.  COHERENCE_EN / DCM are not touched. */
#define N2MDP_M4U1_MISC   0x048u
#define N2MDP_M4U1_PRIO   0x04cu
#define N2MDP_M4U1_DCM    0x050u
#define N2MDP_M4U1_WRLEN  0x054u
#define N2MDP_M4U1_CTRL   0x110u
#define N2MDP_M4U1_MISC_INORDER  ((1u << 1) | (1u << 17))
#define N2MDP_M4U1_WRLEN_THRDIS  ((1u << 5) | (1u << 21))
static uint32_t n2mdp_m4u_state;     /* 0 not tried, 1 applied, 2 already set, 3 deferred (other masters powered), 4 reverted, 5 off */
static uint32_t
n2mdp_m4u1_others_powered(void)
{
#ifdef N2MDP_HOST
	return 0;
#else
	uint32_t s = ni_rd(n2mdp.spm, N2MDP_PWR_STA) & ni_rd(n2mdp.spm, N2MDP_PWR_STA2);
	return s & ((1u << 16) | (1u << 18) | (1u << 12) | (1u << 23) | (1u << 24) | (1u << 26));
#endif
}
static const struct n2mdp_test *
n2mdp_test_named(const char *name)
{
	for (unsigned i = 0; i < N2MDP_NTESTS; i++) { if (!strcmp(n2mdp_tests[i].name, name)) { return &n2mdp_tests[i]; } }
	return NULL;
}
/* one S3 TIMING row; returns run time in us (0 = failed / faulted) */
static uint32_t
n2mdp_s3_row(const char *name, uint32_t si, uint32_t di, const char *when)
{
	const struct n2mdp_test *t = n2mdp_test_named(name);
	if (!t) { return 0; }
	uint32_t crc = 0;
	memset(&n2mdp_tr_st, 0, sizeof(n2mdp_tr_st));
	int r = n2mdp_test_run(t, t->iova ? si : n2mx.pool_pa + N2MDP_BUF_SRC, n2mx.pool_va + N2MDP_BUF_SRC,
	    t->iova ? di : n2mx.pool_pa + N2MDP_BUF_DST, n2mx.pool_va + N2MDP_BUF_DST, &crc);
	uint32_t fault = n2mdp_rd(N2MDP_M4U1 + 0x134u);
	if (fault) {
		N2MDP_LOG("NORD2 MDP S3 TIMING %s: IOVA fault %#x va %#x\n", name, fault, n2mdp_rd(N2MDP_M4U1 + 0x13cu));
		n2mdp_wr(N2MDP_M4U1 + 0x120u, n2mdp_rd(N2MDP_M4U1 + 0x120u) | (1u << 12));
		r = 9;
	}
	struct n2mdp_xstat *x = &n2mdp_tr_st;
	N2MDP_LOG("NORD2 MDP S3 TIMING %s%s: %s rot %u tile %u px: %s hw %u run %u us polls %u tiles %u (%u %u %u %u)\n", name, when,
	    t->iova ? "iova" : "phys", t->rot, (t->tile16 ? t->tile16 : 32u) * 16u, r == 0 ? "ok" : "FAILED", x->hw_us, x->run_us, x->polls,
	    x->tiles, x->tile_us[0], x->tile_us[1], x->tile_us[2], x->tile_us[3]);
	return r == 0 ? (x->run_us ? x->run_us : 1u) : 0u;
}
static void
n2mdp_s3_timing(void)
{
	static uint64_t pg[N2MDP_BUF_SPAN / 4096u];
	const uint32_t n = N2MDP_BUF_SPAN / 4096u;
	for (uint32_t k = 0; k < n; k++) { pg[k] = (uint64_t)n2mx.pool_pa + N2MDP_BUF_SRC + k * 4096u; }
	uint32_t si = n2mdp_iova_map(pg, n);
	for (uint32_t k = 0; k < n; k++) { pg[k] = (uint64_t)n2mx.pool_pa + N2MDP_BUF_DST + k * 4096u; }
	uint32_t di = si ? n2mdp_iova_map(pg, n) : 0;
	if (!si || !di) { N2MDP_LOG("NORD2 MDP S3 TIMING: no IOVA range\n"); if (si) { n2mdp_iova_unmap(si, n); } return; }
	static const char *const wnames[4] = { "iova90_512", "iova90_256", "iova90_128", "iova90_64" };
	static const uint8_t w16[4] = { 32, 16, 8, 4 };
	uint32_t phys = n2mdp_s3_row("phys90_512", si, di, ""), run[4], best = 0;
	for (unsigned w = 0; w < 4; w++) {
		run[w] = n2mdp_s3_row(wnames[w], si, di, "");
		if (run[w] && (!run[best] || run[w] < run[best])) { best = w; }
	}
	(void)n2mdp_s3_row("iova0_512", si, di, "");
	/* service tile width: the fastest IOVA width, only if >= 3 % faster than 512 (each width was verified exact in S2 and here) */
	n2mdp_auto_tile16 = (run[0] && run[best] && best != 0 && run[best] * 100u < run[0] * 97u) ? w16[best] : 0u;
	N2MDP_LOG("NORD2 MDP S3 TIMING: 512-px rotation run phys %u us iova %u us (%u%%); best iova width %u px (%u us) -> service rotation tile %u px\n",
	    phys, run[0], phys ? run[0] * 100u / phys : 0u, w16[best] * 16u, run[best], n2mdp_auto_tile16 ? n2mdp_auto_tile16 * 16u : 512u);
	/* vendor M4U1 write setup */
	uint32_t misc = n2mdp_rd(N2MDP_M4U1 + N2MDP_M4U1_MISC), wl = n2mdp_rd(N2MDP_M4U1 + N2MDP_M4U1_WRLEN);
	N2MDP_LOG("NORD2 MDP S3 M4U1: as found MISC_CTRL %#x PRIORITY %#x DCM_DIS %#x WR_LEN %#x CTRL %#x\n", misc,
	    n2mdp_rd(N2MDP_M4U1 + N2MDP_M4U1_PRIO), n2mdp_rd(N2MDP_M4U1 + N2MDP_M4U1_DCM), wl, n2mdp_rd(N2MDP_M4U1 + N2MDP_M4U1_CTRL));
	uint32_t nmisc = misc & ~N2MDP_M4U1_MISC_INORDER, nwl = wl & ~N2MDP_M4U1_WRLEN_THRDIS, pw = n2mdp_m4u1_others_powered();
	if (!(n2mdp_tune & N2MDP_TUNE_M4U)) { n2mdp_m4u_state = 5; N2MDP_LOG("NORD2 MDP S3 M4U1: vendor write setup off (kern.n2mdp_tune)\n"); }
	else if (nmisc == misc && nwl == wl) { n2mdp_m4u_state = 2; N2MDP_LOG("NORD2 MDP S3 M4U1: already as the vendor sets it (in-order write off, throttle on)\n"); }
	else if (pw) { n2mdp_m4u_state = 3; N2MDP_LOG("NORD2 MDP S3 M4U1: vendor write setup NOT applied: other M4U1 master domains powered (%#x)\n", pw); }
	else {
		n2mdp_wr(N2MDP_M4U1 + N2MDP_M4U1_MISC, nmisc);
		n2mdp_wr(N2MDP_M4U1 + N2MDP_M4U1_WRLEN, nwl);
		uint32_t a0 = n2mdp_s3_row("iova90_512", si, di, " (vendor M4U1)");
		uint32_t ab = best ? n2mdp_s3_row(wnames[best], si, di, " (vendor M4U1)") : a0;
		bool worse = !a0 || !ab || (run[0] && a0 * 100u > run[0] * 110u) || (run[best] && ab * 100u > run[best] * 110u);
		if (worse) {
			n2mdp_wr(N2MDP_M4U1 + N2MDP_M4U1_MISC, misc);
			n2mdp_wr(N2MDP_M4U1 + N2MDP_M4U1_WRLEN, wl);
			n2mdp_m4u_state = 4;
		} else {
			n2mdp_m4u_state = 1;
			if (a0 && ab && best && ab * 100u < a0 * 97u) { n2mdp_auto_tile16 = w16[best]; }
			else if (a0 && ab && best && ab * 100u >= a0 * 97u) { n2mdp_auto_tile16 = 0; }
		}
		N2MDP_LOG("NORD2 MDP S3 M4U1: vendor write setup MISC_CTRL %#x -> %#x WR_LEN %#x -> %#x: 512 px %u -> %u us, %u px %u -> %u us: %s; service tile %u px\n",
		    misc, nmisc, wl, nwl, run[0], a0, w16[best] * 16u, run[best], ab, worse ? "REVERTED" : "kept",
		    n2mdp_auto_tile16 ? n2mdp_auto_tile16 * 16u : 512u);
	}
	n2mdp_iova_unmap(di, n);
	n2mdp_iova_unmap(si, n);
}

/* ---- the stage machine ------------------------------------------------------------------------------------------------ */
static bool
n2mdp_killfile(void)
{
#ifdef N2MDP_HOST
	return n2mdp_host_killfile();
#else
	vnode_t vp = NULLVP;
	if (vnode_lookup("/private/var/db/nord2-mdp-off", 0, &vp, vfs_context_kernel()) == 0) { vnode_put(vp); return true; }
	return false;
#endif
}

static void
n2mdp_stages(uint32_t probe_state)
{
	n2mdp_lock();
	if (n2mx.stage >= 1 || n2mx.s1 > 1 || n2pw.code > 1) { n2mdp_unlock(); return; }         /* once per boot */
	const char *why = NULL;
	int lvl = n2mdp_max_stage & 0xff;
	bool pwron = (n2mdp_max_stage & N2MDP_PWRON_BIT) != 0;
	if (n2mdp_max_stage < 0 || (lvl < 1 && !pwron)) { why = "kern.n2mdp_stages = 0"; }
	else if (n2mdp_killfile()) { why = "/private/var/db/nord2-mdp-off present"; n2mx.killed = true; }
	if (!why && pwron && n2pw.code == 0) {
		/* 0673 stage P: bring-up, then the read-only probe again with the owned-protection rule */
		if (n2mdp_pwron() == 1) {
			n2mdp.runs++;
			probe_state = n2mdp.state = n2mdp_probe(1);
			N2MDP_LOG("NORD2 MDP PWRON: re-probe state %#x (%s)\n", probe_state, (probe_state & N2MDP_S_APB) ? "mdpsys reachable, APB open" :
			    "mdpsys NOT reachable");
		}
	}
	if (!why && lvl < 1) { why = "kern.n2mdp_stages: functional stages off"; }
	else if (!why && (!(probe_state & N2MDP_S_POWER) || (probe_state & (N2MDP_S_PROT | N2MDP_S_MUXGATE | N2MDP_S_NOMAP)))) { why = "probe not OK"; }
	if (why) { N2MDP_LOG("NORD2 MDP STAGES: not run (%s)\n", why); n2mdp_unlock(); return; }
	if (!n2mdp_gates_on() || !n2mdp_pool_get()) { n2mx.s1 = 9; n2mdp_gates_restore(); N2MDP_LOG("NORD2 MDP STAGES: S1 cannot start (gates/pool)\n"); n2mdp_unlock(); return; }
	for (unsigned p = 0; p < 4; p++) { n2mx.larb_found[p] = n2mdp_rd(N2MDP_W_LARB2 + 0x380u + 4u * p); }
	if (!n2mdp_larb2_init()) { n2mx.s1 = 9; n2mdp_gates_restore(); N2MDP_LOG("NORD2 MDP STAGES: S1 cannot start (larb2)\n"); n2mdp_unlock(); return; }
	N2MDP_LOG("NORD2 MDP S1: larb2 ports as found %#x %#x %#x %#x (bit0 = MMU_EN; forced 0 for physical jobs)\n", n2mx.larb_found[0],
	    n2mx.larb_found[1], n2mx.larb_found[2], n2mx.larb_found[3]);
	uint32_t pm = 0, fm = 0;
	n2mx.s1 = n2mdp_stage_tests(1, &pm, &fm);
	N2MDP_LOG("NORD2 MDP S1: %s (pass %#x fail %#x)\n", n2mx.s1 == 1 ? "PASS" : "FAIL", pm, fm);
	if (n2mx.s1 != 1) { n2mx.caps = 0; n2mdp_gates_restore(); goto done; }
	n2mx.stage = 1;
	if (lvl >= 2) {
		uint32_t r = n2mdp_stage_tests(2, &n2mx.s2_pass, &n2mx.s2_fail);
		N2MDP_LOG("NORD2 MDP S2: %s (pass %#x fail %#x, caps %#x, rgb_swap %u)\n", r == 1 ? "PASS" : "PARTIAL", n2mx.s2_pass, n2mx.s2_fail,
		    n2mx.caps, n2mx.rgb_swap);
		if (r == 1) { n2mx.stage = 2; }                 /* 0678: S2 counts only when every S2 job passed */
	}
	if (n2mx.stage >= 2 && lvl >= 3) {
		n2mx.s3 = n2mdp_stage3();
		N2MDP_LOG("NORD2 MDP S3: %s (code %u)\n", n2mx.s3 == 1 ? "PASS" : "FAIL", n2mx.s3);
		if (n2mx.s3 == 1) { n2mx.stage = 3; n2mx.caps |= N2MDP_CAP_IOVA; n2mdp_s3_timing(); }   /* 0690 */
	}
done:
	N2MDP_LOG("NORD2 MDP STAGES: stage %u caps %#x s1 %u s2 pass %#x fail %#x s3 %u jobs %u fail %u timeouts %u max %u us\n", n2mx.stage,
	    n2mx.caps, n2mx.s1, n2mx.s2_pass, n2mx.s2_fail, n2mx.s3, n2mx.jobs, n2mx.job_fail, n2mx.timeouts, n2mx.max_us);
	n2mdp_unlock();
}

/* ---- service API (C linkage; used by 0672 AppleM2ScalerCSCDriver and /dev/redwood-mdp) -------------------------------- */
#include "nord2_mdp_api.h"

uint32_t
nord2_mdp_caps(void)
{
	return (n2mx.stage >= 3 && !n2mx.killed) ? n2mx.caps : 0;
}

/* one blit between two page-listed images (IOVA path).  Returns 0 or an errno; never leaves anything mapped.
 * 0690: two requests can run at once: scaled requests use pipe 2 (scale lane), unscaled ones pipe 0, or pipe 2's direct lane when
 * pipe 0 is busy and N2MDP_CAP_DUAL passed (kern.n2mdp_tune bit 2).  Locks: MDP rw lock shared (stages / probe exclusive) -> the
 * pipe lock for the whole job -> map lock around the IOVA cache / fault state only.  Statistics go to *st (per request). */
static uint32_t n2mdp_pwait[2];      /* requests waiting for each pipe */
static uint32_t n2mdp_busy_mask;     /* bit 0 = pipe 0 running a service job, bit 1 = pipe 2 */
static uint32_t n2mdp_pjobs[2], n2mdp_overlap;
int
nord2_mdp_blit_pages_st(const struct n2mdp_pimg *src, const struct n2mdp_pimg *dst, uint32_t rot, uint32_t flip, struct n2mdp_bstat *st)
{
	struct n2mdp_bstat lst;
	if (!st) { st = &lst; }
	memset(st, 0, sizeof(*st));
	uint32_t caps = nord2_mdp_caps();
	if (!(caps & N2MDP_CAP_IOVA)) { return ENODEV; }
	if (rot > 3 || src->npages == 0 || dst->npages == 0 || src->npages > N2MDP_MAX_PAGES || dst->npages > N2MDP_MAX_PAGES) { return EINVAL; }
	uint32_t need = N2MDP_CAP_COPY;
	if (rot == 1) { need |= N2MDP_CAP_ROT90; } else if (rot == 2) { need |= N2MDP_CAP_ROT180; } else if (rot == 3) { need |= N2MDP_CAP_ROT270; }
	if (flip & 1u) { need |= N2MDP_CAP_FLIP; }
	if (n2mf_is_yuv(src->fmt)) { need |= src->full_range ? N2MDP_CAP_NV12F : N2MDP_CAP_NV12V; }
	uint32_t ow = (rot & 1u) ? dst->h : dst->w, oh = (rot & 1u) ? dst->w : dst->h;
	bool scaled = ow != src->w || oh != src->h;
	if (scaled) {
		need |= (ow < src->w || oh < src->h) ? N2MDP_CAP_SCALE_DN : N2MDP_CAP_SCALE_UP;
		if (rot) { need |= N2MDP_CAP_SCALE_ROT; }
	}
	if (ow > 512u) { need |= N2MDP_CAP_TILE; }
	if (!n2mf_is_yuv(src->fmt) && scaled) { need |= N2MDP_CAP_RGB_SCALE; }   /* 0683 */
	if (n2mf_is_yuv(dst->fmt)) { need |= N2MDP_CAP_NV12_DST; }   /* 0686 */
	st->scaled = scaled ? 1u : 0u;
	if (scaled) {   /* 0691: scaling only inside what this boot verified */
		if (n2mdp_scale_off || (n2mdp_lane_dead & 2u)) { return ENOTSUP; }
		if (n2mf_is_yuv(dst->fmt)) { return ENOTSUP; }            /* scaled NV12 destinations: never run by a boot job */
		if (src->w > 1024u || src->h > 512u || ow > 1024u || oh > 1024u) { need |= N2MDP_CAP_SCALE_BIG; }
		if (src->w > 4096u || src->h > 4096u) { return ENOTSUP; }
	}
	if ((caps & need) != need) { return ENOTSUP; }
	if (n2mdp_lane_dead & 1u) { return ENODEV; }                     /* 0691: pipe 0 stuck after a failed recovery */
	uint64_t tl0 = n2mdp_now_us();
	n2mdp_svc_enter();
	unsigned pi;                         /* 0 = pipe 0, 1 = pipe 2 */
	if (scaled) { pi = 1; }
	else if ((caps & N2MDP_CAP_DUAL) && (n2mdp_tune & N2MDP_TUNE_DUAL) && !(n2mdp_lane_dead & 2u)) {
		if (n2mdp_pipe_trylock(0)) { pi = 0; goto locked; }
		if (n2mdp_pipe_trylock(1)) { pi = 1; goto locked; }
		pi = __atomic_load_n(&n2mdp_pwait[1], __ATOMIC_RELAXED) < __atomic_load_n(&n2mdp_pwait[0], __ATOMIC_RELAXED) ? 1u : 0u;
	} else { pi = 0; }
	__atomic_fetch_add(&n2mdp_pwait[pi], 1u, __ATOMIC_RELAXED);
	n2mdp_pipe_lock(pi);
	__atomic_fetch_sub(&n2mdp_pwait[pi], 1u, __ATOMIC_RELAXED);
locked:
	st->lockwait_us = (uint32_t)(n2mdp_now_us() - tl0);
	st->pipe = pi ? 2u : 0u;
	st->overlap = (__atomic_fetch_or(&n2mdp_busy_mask, 1u << pi, __ATOMIC_ACQ_REL) & (1u << (pi ^ 1u))) ? 1u : 0u;
	int err = 0;
	uint32_t tpx = 512u;
	unsigned ss = N2MDP_MC_N, ds = N2MDP_MC_N;
	uint64_t tm0 = n2mdp_now_us();
	n2mdp_map_lock();
	uint32_t gen0 = n2mdp_fault_gen;
	uint32_t si = n2mdp_mc_get(src->pages, src->npages, &ss);           /* 0687: cached IOVA ranges (0690: pinned) */
	uint32_t di = si ? n2mdp_mc_get(dst->pages, dst->npages, &ds) : 0;
	n2mdp_map_unlock();
	st->map_us = (uint32_t)(n2mdp_now_us() - tm0);
	struct n2mdp_prog *pg = &n2mdp_pprog[pi];
	struct n2mdp_xstat xs;
	memset(&xs, 0, sizeof(xs));
	if (!si || !di) { err = ENOMEM; goto out; }
	struct n2mdp_job j;
	memset(&j, 0, sizeof(j));
	j.src.fmt = src->fmt; j.src.w = src->w; j.src.h = src->h; j.src.full_range = src->full_range; j.src.bt709 = src->bt709;
	j.src.pitch[0] = src->pitch[0]; j.src.pitch[1] = src->pitch[1]; j.src.addr[0] = si + src->off[0]; j.src.addr[1] = src->pitch[1] ? si + src->off[1] : 0;
	j.dst.fmt = dst->fmt; j.dst.w = dst->w; j.dst.h = dst->h;
	j.dst.pitch[0] = dst->pitch[0]; j.dst.pitch[1] = dst->pitch[1]; j.dst.addr[0] = di + dst->off[0]; j.dst.addr[1] = dst->pitch[1] ? di + dst->off[1] : 0;
	j.rot = (uint8_t)rot; j.flip = (uint8_t)(flip & 1u); j.rgb_swap = (uint8_t)n2mx.rgb_swap;
	j.allow_unverified = (caps & N2MDP_CAP_RGB_SCALE) ? 1u : 0u;   /* 0683: RGB scaling only when this boot verified it */
	j.pipe2 = (pi == 1u && !scaled) ? 1u : 0u;                        /* 0690: direct lane of pipe 2 */
	if (!scaled && (rot & 1u)) {                                       /* 0690: rotation tile width (S3 TIMING choice / sysctl) */
		uint32_t t16 = n2mdp_svc_tile_px > 0 ? (uint32_t)n2mdp_svc_tile_px / 16u : ((n2mdp_tune & N2MDP_TUNE_TILE) ? n2mdp_auto_tile16 : 0u);
		if (t16 >= 4u && t16 <= 32u) { j.tile_w = (uint8_t)t16; tpx = t16 * 16u; }
	}
	if (scaled) {                                                      /* 0691: input tiles <= 512 px */
		uint32_t t16 = n2mdp_scale_tile16(src->w, ow);
		if (t16 < 32u) { j.tile_w = (uint8_t)t16; tpx = t16 * 16u; }
	}
	uint64_t tc0 = n2mdp_now_us();
	if (n2mdp_compile(&j, pg)) { err = EINVAL; goto out; }
	st->compile_us = (uint32_t)(n2mdp_now_us() - tc0);   /* 0688 */
	n2mdp_larb_mode_pipe(pg->pipe, true);                 /* 0690: only this pipe's two ports */
	err = n2mdp_run_x(pg, &xs);
	if (err == EIO && !(n2mdp_lane_dead & (pi ? 2u : 1u))) {   /* 0691: an engine reset poll failed; the abort recovered the pipe: once more */
		N2MDP_LOG("NORD2 MDP: pipe %u job retried after recovery\n", pi ? 2u : 0u);
		err = n2mdp_run_x(pg, &xs);
	}
	n2mdp_larb_mode_pipe(pg->pipe, false);
	if (err && scaled && !n2mdp_scale_off) {
		n2mdp_scale_off = 1;
		N2MDP_LOG("NORD2 MDP: scaled job %ux%u -> %ux%u failed (%d): scaling off for this boot (unscaled jobs continue)\n", src->w, src->h, ow, oh, err);
	}
	n2mdp_map_lock();
	n2mx.jobs++;
	if (err) { n2mx.job_fail++; }
	{
		uint32_t fault = n2mdp_rd(N2MDP_M4U1 + 0x134u);
		if (fault) {     /* translation fault: no result of a job that overlapped it is trustworthy */
			N2MDP_LOG("NORD2 MDP: IOVA fault %#x va %#x in a service blit (pipe %u)\n", fault, n2mdp_rd(N2MDP_M4U1 + 0x13cu), pi ? 2u : 0u);
			n2mdp_wr(N2MDP_M4U1 + 0x120u, n2mdp_rd(N2MDP_M4U1 + 0x120u) | (1u << 12));
			n2mdp_fault_gen++;
			n2mdp_mc_drop();                    /* 0687: never re-use mappings after a fault (0690: busy ones when released) */
		}
		if (n2mdp_fault_gen != gen0 && !err) { err = EFAULT; }
	}
	n2mdp_map_unlock();
out:
	n2mdp_map_lock();
	n2mdp_mc_put(ss, si, src->npages);
	n2mdp_mc_put(ds, di, dst->npages);
	n2mdp_pjobs[pi]++;
	if (st->overlap) { n2mdp_overlap++; }
	n2mdp_map_unlock();
	__atomic_fetch_and(&n2mdp_busy_mask, ~(1u << pi), __ATOMIC_ACQ_REL);
	st->hw_us = xs.hw_us; st->run_us = xs.run_us; st->tiles = xs.tiles; st->polls = xs.polls; st->tile_max_us = xs.tile_max_us;
	for (unsigned i = 0; i < 4u; i++) { st->tile_us[i] = xs.tile_us[i]; }
	st->tile_px = tpx;
	/* legacy globals (0687-0689 readers; racy by nature, kept for /dev/redwood-mdp users) */
	n2mdp_last_map_us = st->map_us; n2mdp_last_run_us = st->hw_us; n2mdp_last_compile_us = st->compile_us; n2mdp_last_lockwait_us = st->lockwait_us;
	n2mdp_pipe_unlock(pi);
	n2mdp_svc_exit();
	return err;
}
int
nord2_mdp_blit_pages(const struct n2mdp_pimg *src, const struct n2mdp_pimg *dst, uint32_t rot, uint32_t flip, uint32_t *hw_us)
{
	struct n2mdp_bstat st;
	int e = nord2_mdp_blit_pages_st(src, dst, rot, flip, &st);
	if (hw_us) { *hw_us = st.hw_us; }
	return e;
}

#ifndef N2MDP_HOST
SYSCTL_INT(_kern, OID_AUTO, n2mdp_probe, CTLFLAG_RW | CTLFLAG_LOCKED, &n2mdp_req, 0,
    "0670: run the MDP probe once (1 read-only, 2 + open/close engine clock gates); -1 = never (also cancels the boot probe)");
SYSCTL_INT(_kern, OID_AUTO, n2mdp_boot, CTLFLAG_RW | CTLFLAG_LOCKED, &n2mdp_boot_level, 0,
    "0670: level of the automatic probe at +45 s (0 = none)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_state, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mdp.state, 0, "0670: verdict bits of the last probe");
SYSCTL_INT(_kern, OID_AUTO, n2mdp_stages, CTLFLAG_RW | CTLFLAG_LOCKED, &n2mdp_max_stage, 0, "0671/0673: bits 7:0 highest functional stage to run (0..3), bit 8 (0x100) = 0673 MDP bring-up (bus protection + gates)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_pwron, CTLFLAG_RD | CTLFLAG_LOCKED, &n2pw.code, 0,
    "0673: bring-up result (0 not run, 1 OK, 2 domain off, 3 PWR_CON, 4 mdp_sel gated, 5 shared bit set, 6 ack timeout restored, 7 restore failed, 8 gates, 9 maps, 10 owned bit after release)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_stage, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mx.stage, 0, "0671: highest stage passed in this boot");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_caps, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mx.caps, 0, "0671: capability bits that passed");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_s1, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mx.s1, 0, "0671: stage 1 result (1 pass, >1 failure code)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_s2, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mx.s2_pass, 0, "0671: stage 2 tests passed (bitmask)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_s3, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mx.s3, 0, "0671: stage 3 result (1 pass)");
SYSCTL_INT(_kern, OID_AUTO, n2mdp_tune, CTLFLAG_RW | CTLFLAG_LOCKED, &n2mdp_tune, 0,
    "0690: 1 vendor M4U1 write setup at S3 (boot only), 2 second pipe for unscaled service requests, 4 S3 rotation tile width (default 7)");
SYSCTL_INT(_kern, OID_AUTO, n2mdp_svc_tile, CTLFLAG_RW | CTLFLAG_LOCKED, &n2mdp_svc_tile_px, 0,
    "0690: rotation tile width in px for unscaled service requests (64..512, multiple of 16); 0 = S3 TIMING choice");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_auto_tile, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mdp_auto_tile16, 0, "0690: S3 TIMING tile choice (16-px units, 0 = 512 px)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_m4u, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mdp_m4u_state, 0,
    "0690: M4U1 vendor write setup: 0 not tried, 1 applied, 2 already set, 3 deferred (other masters powered), 4 reverted, 5 off");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_overlap, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mdp_overlap, 0, "0690: service jobs that ran while the other pipe was busy");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_scale_off, CTLFLAG_RW | CTLFLAG_LOCKED, &n2mdp_scale_off, 0, "0691: 1 = scaled requests refused (set by a scaled failure; write 0 to retry)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_lane_dead, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mdp_lane_dead, 0, "0691: pipes stuck after recovery (bit 0 pipe 0, bit 1 pipe 2)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_recoveries, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mdp_recoveries, 0, "0691: engine recoveries (aborts)");
SYSCTL_UINT(_kern, OID_AUTO, n2mdp_mmsys_resets, CTLFLAG_RD | CTLFLAG_LOCKED, &n2mdp_mmsys_resets, 0, "0691: MDPSYS SW0_RST_B lane resets");

static nord2_mdp_surface_blit_fn n2mdp_surface_hook;
void nord2_mdp_set_surface_hook(nord2_mdp_surface_blit_fn fn) { n2mdp_surface_hook = fn; }
static nord2_mdp_publish_fn n2mdp_publish_hook;
static bool n2mdp_stages_done;
void
nord2_mdp_set_publish_hook(nord2_mdp_publish_fn fn)
{
	n2mdp_publish_hook = fn;
	if (n2mdp_stages_done && fn) { fn(nord2_mdp_caps()); }   /* stages already ran (late install) */
}

static int
n2mdp_dev_open(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p) { return 0; }
static int
n2mdp_dev_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p) { return 0; }
static int
n2mdp_dev_ioctl(__unused dev_t dev, u_long cmd, caddr_t data, __unused int flag, __unused struct proc *p)
{
	if (cmd == RWMDP_INFO) {
		struct rwmdp_info *i = (struct rwmdp_info *)data;
		memset(i, 0, sizeof(*i));
		i->version = 1; i->stage = n2mx.stage; i->caps = n2mx.caps; i->s1 = n2mx.s1; i->s2_pass = n2mx.s2_pass; i->s2_fail = n2mx.s2_fail;
		i->s3 = n2mx.s3; i->jobs = n2mx.jobs; i->job_fail = n2mx.job_fail; i->timeouts = n2mx.timeouts; i->last_us = n2mx.last_us;
		i->max_us = n2mx.max_us; i->rgb_swap = n2mx.rgb_swap; i->killed = n2mx.killed; i->probe_state = n2mdp.state;
		return 0;
	}
	if (cmd == RWMDP_BLIT_SURF) {
		struct rwmdp_blit_surf *b = (struct rwmdp_blit_surf *)data;
		nord2_mdp_surface_blit_fn fn = n2mdp_surface_hook;
		if (!fn) { return ENODEV; }
		b->hw_us = 0;
		b->result = (uint32_t)fn(b->src_id, b->dst_id, b->rot, b->flip, &b->hw_us);
		return 0;
	}
	return ENOTTY;
}
static const struct cdevsw n2mdp_cdevsw = {
	.d_open = n2mdp_dev_open, .d_close = n2mdp_dev_close, .d_read = eno_rdwrt, .d_write = eno_rdwrt,
	.d_ioctl = n2mdp_dev_ioctl, .d_stop = eno_stop, .d_reset = eno_reset, .d_select = eno_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0
};

static int n2mdp_chan;
static int n2mdp_pub_req;
SYSCTL_INT(_kern, OID_AUTO, n2mdp_publish, CTLFLAG_RW | CTLFLAG_LOCKED, &n2mdp_pub_req, 0,
    "0683: 1 = offer AppleM2ScalerCSCDriver again (after kern.n2mdp_svc=1), handled by the MDP thread within 1 s");

static void
n2mdp_thread(void *arg __unused, wait_result_t wr __unused)
{
	for (unsigned t = 1;; t++) {
		uint64_t deadline;
		clock_interval_to_deadline(1000, NSEC_PER_MSEC, &deadline);
		(void)assert_wait_deadline(&n2mdp_chan, THREAD_UNINT, deadline);
		thread_block(THREAD_CONTINUE_NULL);
		if (n2mdp_req < 0) { continue; }
		int level = 0;
		bool boot = false;
		if (n2mdp_req == 1 || n2mdp_req == 2) { level = n2mdp_req; n2mdp_req = 0; }
		else if (t == 45 && n2mdp_boot_level > 0) { level = n2mdp_boot_level > 2 ? 2 : n2mdp_boot_level; boot = true; }
		else if (t == 180 && n2mdp_boot_level > 0) { level = 1; }   /* second read-only look after the UI / camera / codecs ran */
		if (n2mdp_pub_req == 1 && n2mdp_stages_done) {   /* 0683: kern.n2mdp_publish = 1: call the service hook again (controlled run) */
			n2mdp_pub_req = 0;
			nord2_mdp_publish_fn pf = n2mdp_publish_hook;
			printf("NORD2 MDP: publish requested (caps %#x)\n", nord2_mdp_caps());
			if (pf) { pf(nord2_mdp_caps()); }
		}
		if (level) {
			n2mdp.runs++;
			n2mdp_lock();
			n2mdp.state = n2mdp_probe(level);
			n2mdp_unlock();
			if (boot) {
				n2mdp_stages(n2mdp.state);
				n2mdp_stages_done = true;
				nord2_mdp_publish_fn fn = n2mdp_publish_hook;
				if (fn) { fn(nord2_mdp_caps()); } else { printf("NORD2 MDP: no service hook (image without 0672)\n"); }
			}
		}
	}
}

static void
nord2_mdp_start(void)
{
	thread_t thread;
	int major = cdevsw_add(-1, &n2mdp_cdevsw);
	bool dev = major >= 0 && devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "redwood-mdp") != NULL;
	if (kernel_thread_start(n2mdp_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
		printf("NORD2 MDP: probe thread on (boot probe level %d at +45 s then stages %#x (bit 8 = 0673 bring-up); read-only again at +180 s; "
		    "/dev/redwood-mdp %s)\n", n2mdp_boot_level, (unsigned)n2mdp_max_stage, dev ? "made" : "NOT made");
	}
}
#endif /* !N2MDP_HOST */
