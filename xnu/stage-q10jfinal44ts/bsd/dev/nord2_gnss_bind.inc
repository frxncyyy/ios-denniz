/* SPDX-License-Identifier: MIT */
/*
 * nord2_gnss_bind.inc -- patch 0580 (gnss-20261114): the GNSS line of 12 Nov 2026 (patches 0350, 0351, 0353..0359, 0362..0364,
 * phone-proven on q10jfg/q10jfj: S11 = DSP code download 50/50 fragments acked + HOLD, gnssd3 engine threads running) ported
 * onto the q10jcp chain.  This file is the sweet_touch.c binding block of 0351/0353/0354/0355/0357/0359/0363 moved out of
 * sweet_touch.c unchanged, except:
 *   - io_submap budget (3 free pages): the always-on SPM / GALS / infracfg pages are NOT mapped again; the Wi-Fi probe's own
 *     io_submap mappings (nw.spm / nw.gals / nw.infra, ni_wifi_power_probe) are reused.  No mapping -> the guard refuses.
 *   - the 0355 bisection controls k=3/4 (plain ml_io_map through io_submap) are removed (refused, 0x0700).
 * Every other GNSS mapping is ml_io_map_unmappable (kernel_map, Device) or Wi-Fi's host page (npa_native.host).
 */
#ifndef NORD2_GNSS_BIND_INC
#define NORD2_GNSS_BIND_INC 1
#include "nord2_gpsdl.inc"   /* 0350: /dev/gpsdl0,1 GNSS data-link nodes + MT6893 GNSS power core */
/*
 * 0351: GNSS binding to the Wi-Fi persistent conninfra owner. The GNSS block needs the shared connectivity hardware that
 * ni_wifi_power_probe() powers and RETAINS (nord2wifi=2 nord2wifipersist=1, "retained=1 ... stage10"): rails, conn_infra
 * on, A-die. Without that state no GNSS register is mapped or touched. The GNSS core then only uses its own votes
 * (WAKEPU_GPS, GP_FUNCTION_EN, BGFSYS power, A-die top-clock GPS vote) and the 16 allow-listed registers of gnss_pwr.
 */
static bool
n2gd_conninfra_ready(void)
{
	return npa_persistent_lifetime && !npa_native.state.fatal && npa_native.state.result.phase == 10;
}

/* 0353: the six always-on predicates the Wi-Fi driver checks before each conn_infra access (wifi_main_wfdma_bind.inc
 * nwfn_guard): SPM, GALS and infracfg pages are always-on AP registers, safe to read at any time. */
static vm_offset_t n2gd_ao_spm, n2gd_ao_gals, n2gd_ao_infra;
static bool
n2gd_ao_predicates(uint32_t out[6])
{
	/* 0580: reuse the Wi-Fi probe's io_submap mappings of the same three pages (no new io_submap page) */
	if (!n2gd_ao_spm) { n2gd_ao_spm = nw.spm; }
	if (!n2gd_ao_gals) { n2gd_ao_gals = nw.gals; }
	if (!n2gd_ao_infra) { n2gd_ao_infra = nw.infra; }
	if (!n2gd_ao_spm || !n2gd_ao_gals || !n2gd_ao_infra) { return false; }
	out[0] = *(volatile uint32_t *)(n2gd_ao_spm + 0x304); out[1] = *(volatile uint32_t *)(n2gd_ao_spm + 0x16c);
	out[2] = *(volatile uint32_t *)(n2gd_ao_spm + 0x170); out[3] = *(volatile uint32_t *)(n2gd_ao_gals + 0x504);
	out[4] = *(volatile uint32_t *)(n2gd_ao_infra + 0x228); out[5] = *(volatile uint32_t *)(n2gd_ao_infra + 0x724);
	return (out[0] & 0x1f) == 0xd && (out[1] & 2) && (out[2] & 2) && (out[3] & 1) && !(out[4] & 0x2000) && !(out[5] & 4);
}

/* 0354: Wi-Fi's guard, read-only: the six predicates, its GIC check (GICD+0x130 bits 23/26 must be clear) and the Wi-Fi
 * driver's software ownership state (no register). out[6] = GICD+0x130, out[7] = ap<<16|wf, out[8] = asleep<<16|stage. */
static bool
n2gd_wifi_guard(uint32_t out[9])
{
	n2gt("guard");                                             /* 0583 */
	bool ok = n2gd_ao_predicates(out);
	out[6] = nwfn_native.gic_va ? *(volatile uint32_t *)(nwfn_native.gic_va + 0x130) : 0xffffffffu;
	out[7] = (nwfn_native.ap_owned << 16) | (nwfn_native.wf_owned & 0xffff);
	out[8] = (nwfn_native.asleep << 16) | (nwfn_native.stage & 0xffff);
	return ok && nwfn_native.gic_va && !(out[6] & ((1u << 23) | (1u << 26)));
}
static vm_offset_t n2gd_wifi_host(void) { return npa_native.host; }
/* 0589: GICD write (only ICENABLER of the GPS SPIs, K=37/38 HOLD), traced */
static void
n2gd_gicd_wr(uint32_t off, uint32_t v)
{
	if (!nwfn_native.gic_va || (off != 0x1b0u && off != 0x1b4u)) { return; }
	n2gt("W gicd+%#x := %#x", off, v);
	*(volatile uint32_t *)(nwfn_native.gic_va + off) = v;
	__builtin_arm_dsb(0xf);
}
/* 0363: read-only GIC distributor word (ISENABLER/ISPENDR of the GPS INTIDs 415..421 = GIC SPI 383..389) */
static uint32_t
n2gd_gicd_rd(uint32_t off)
{
	return nwfn_native.gic_va ? *(volatile uint32_t *)(nwfn_native.gic_va + off) : 0xffffffffu;
}

#define N2GD_NPAGES 8                                      /* 0357: + GPS RGU_ON 0x18c00000; 0359: + BGF_GPS_DMA 0x18c10000 */
static const uint32_t n2gd_pages[N2GD_NPAGES] = { 0x18000000u, 0x18001000u, 0x18005000u, 0x18060000u, 0x18c01000u, 0x18c73000u, 0x18c00000u, 0x18c10000u };
static vm_offset_t n2gd_va[N2GD_NPAGES];

/*
 * 0355 (q10jr5): q10jr4 one5 reset the phone inside the ml_io_map of these 6 pages, with no register access. ml_io_map
 * allocates from XNU's 8 MiB io_submap with KMA_NOFAIL (osfmk/arm/io_map.c), so an exhausted io_submap is a panic, and a
 * panic never reaches this console (debug_log_init: gPanicBase not initialized). The Wi-Fi ROM window alone takes
 * 0x450000 of it, the stock FDT copy 0x100000, AMFI 0xd3000; pool_bind.inc already moved its 15 MiB claim to
 * ml_io_map_unmappable(..., VM_WIMG_IO) for that reason. GNSS now does the same: each page is mapped from kernel_map with
 * the same Device attributes, only when first used, and the host CSR page reuses Wi-Fi's own mapping (npa_native.host).
 */
static vm_offset_t
n2gd_map_page(int i)
{
	if (n2gd_va[i]) { return n2gd_va[i]; }
	if (n2gd_pages[i] == 0x18060000u) { n2gd_va[i] = npa_native.host; }                 /* no new mapping */
	else { n2gd_va[i] = ml_io_map_unmappable(n2gd_pages[i], 0x1000, VM_WIMG_IO); }      /* kernel_map, Device */
	return n2gd_va[i];
}

static volatile uint32_t *
n2gd_reg(uint32_t pa)
{
	if (!n2g_reg_allowed(pa)) { return NULL; }
	for (int i = 0; i < N2GD_NPAGES; i++) {
		if ((pa & ~0xfffu) == n2gd_pages[i]) {
			vm_offset_t va = n2gd_map_page(i);
			return va ? (volatile uint32_t *)(va + (pa & 0xfffu)) : NULL;
		}
	}
	return NULL;
}
static uint32_t
n2gd_rd(__unused void *ctx, uint32_t pa)
{
	volatile uint32_t *r = n2gd_reg(pa);
	if (r == NULL) { printf("NORD2 GNSS: refused read %#x\n", pa); return 0xffffffffu; }
	n2gt("R %#x", pa);                                         /* 0583: logged BEFORE the access */
	uint32_t v = *r;
	n2gt("r %#x = %#x", pa, v);
	return v;
}
static void
n2gd_wr(__unused void *ctx, uint32_t pa, uint32_t v)
{
	volatile uint32_t *r = n2gd_reg(pa);
	if (r == NULL) { printf("NORD2 GNSS: refused write %#x\n", pa); return; }
	n2gt("W %#x := %#x", pa, v);                               /* 0583: logged BEFORE the access */
	*r = v;
	__builtin_arm_dsb(0xf);
}
static void n2gd_delay(__unused void *ctx, unsigned us) { delay(us); }
static void n2gd_note(__unused void *ctx, const char *step, uint32_t a, uint32_t b) { printf("NORD2 GNSS: %s %#x %#x\n", step, a, b); }

/* GPS DMA buffers are milestone 2; until then the EMI remap points at the CONN EMI region the Wi-Fi owner already
 * remaps (0x8a000000 in npa_persistent_window), which the GNSS DMA does not use yet (no DMA is started). */
#define N2GD_EMI_BASE 0xdfc00000u                     /* 0584: GPS EMI remap base, vendor layout (0359: 0xdfe00000) */
/* 0359: GPS DMA rings in the unused physical gap 0xDA800000..0xDFE70000 (redwood_vcodec.h; below XNU's RAM window at
 * 0x1_42000000): 64 KiB at 0xDFE00000, kernel_map Device mapping, zeroed once. */
#define N2GD_RING_WIN 0x10000u
static int
n2gd_ringwin(uint8_t **va)
{
	static uint8_t *w;
	if (!w) {
		w = (uint8_t *)ml_io_map_unmappable(N2GD_EMI_BASE + N2GL_RING_OFF, N2GD_RING_WIN, VM_WIMG_IO);   /* 0584: rings at the vendor offset */
		if (!w) { printf("NORD2 GNSS: ring window map failed\n"); return ENOMEM; }
		for (uint32_t i = 0; i < N2GD_RING_WIN; i += 4) { *(volatile uint32_t *)(void *)(w + i) = 0; }
		printf("NORD2 GNSS: ring window %#x (remap base %#x + %#x, 0584) mapped at %p, zeroed\n", N2GD_EMI_BASE + N2GL_RING_OFF, N2GD_EMI_BASE, N2GL_RING_OFF, w);
	}
	*va = w;
	return 0;
}

static int
n2gd_hw(struct n2g_hw *hw)
{
	/* 0355: nothing is mapped here any more; n2gd_reg maps each page on its first access (n2gd_map_page) */
	if (!npa_native.host) { printf("NORD2 GNSS: no Wi-Fi host mapping\n"); return ENODEV; }
	hw->ctx = NULL; hw->read32 = n2gd_rd; hw->write32 = n2gd_wr; hw->delay_us = n2gd_delay; hw->log = n2gd_note;
	hw->emi_base_pa = N2GD_EMI_BASE;
	return 0;
}

/*
 * 0355 mapping bisection, one action per boot (ioctls 121..130), after the same read-only Wi-Fi guard as 0354, a console
 * marker and a 2 s pause. No register of a newly mapped page is ever accessed here.
 *   121 (k=1): inventory only: virtual addresses of the existing io_submap mappings and of Wi-Fi's kernel_map pool
 *   122 (k=2): the fixed path: n2gd_map_page for all 6 pages (5 x ml_io_map_unmappable + Wi-Fi's host page)
 *   123, 124 (k=3, 4): removed in 0580 (they were plain io_submap ml_io_map controls)
 *   125..130 (k=5..10): ml_io_map_unmappable of exactly one GNSS page: 0x18000000, 0x18001000, 0x18005000,
 *                       0x18060000 (a fresh second mapping), 0x18c01000, 0x18c73000
 */
static uint32_t
n2gd_mapone(int k)
{
	uint32_t g[9] = {0};
	bool ok = n2gd_wifi_guard(g);
	printf("NORD2 GNSS MAP %d: spm304=%#x gals504=%#x infra228=%#x infra724=%#x gic130=%#x wifi ap=%u wf=%u asleep=%u stage=%u -> %s\n",
	    k, g[0], g[3], g[4], g[5], g[6], g[7] >> 16, g[7] & 0xffff, g[8] >> 16, g[8] & 0xffff, ok ? "guard ok" : "GUARD REFUSES");
	printf("NORD2 GNSS MAP %d: io_submap VAs: ao_spm=%#lx ao_gals=%#lx ao_infra=%#lx host=%#lx sched=%#lx dummy=%#lx gic=%#lx wfdma=%#lx | kernel_map pool=%#lx\n",
	    k, (unsigned long)n2gd_ao_spm, (unsigned long)n2gd_ao_gals, (unsigned long)n2gd_ao_infra, (unsigned long)npa_native.host,
	    (unsigned long)nwfn_native.scheduler_va, (unsigned long)nwfn_native.dummy_va, (unsigned long)nwfn_native.gic_va,
	    (unsigned long)nppa_native.wfdma, (unsigned long)nppa_native.pool);
	if (k == 1) { return 1; }
	if (!ok) { return 0x0100u; }
	if (k == 2) {
		printf("NORD2 GNSS MAP 2: about to map the 6 GNSS pages from kernel_map (ml_io_map_unmappable, host page = Wi-Fi's), no access\n");
		delay(2000000);
		for (int i = 0; i < N2GD_NPAGES; i++) {
			vm_offset_t va = n2gd_map_page(i);
			printf("NORD2 GNSS MAP 2: %#x -> %#lx\n", n2gd_pages[i], (unsigned long)va);
			if (!va) { return 0x0300u; }
		}
		printf("NORD2 GNSS MAP 2: done\n");
		return 1;
	}
	if (k == 3 || k == 4) { return 0x0700u; }                /* 0580: io_submap controls removed (3 free pages) */
	if (k >= 5 && k <= 10) {
		uint32_t pa = n2gd_pages[k - 5];
		printf("NORD2 GNSS MAP %d: about to ml_io_map_unmappable(%#x, 0x1000, VM_WIMG_IO) (no access)\n", k, pa);
		delay(2000000);
		vm_offset_t va = ml_io_map_unmappable(pa, 0x1000, VM_WIMG_IO);
		printf("NORD2 GNSS MAP %d: done %#x -> %#lx\n", k, pa, (unsigned long)va);
		return va ? 1u : 0x0300u;
	}
	return 0x0700u;
}
/*
 * 0585 (gnss-20261114; = 0582 rebased onto 0583/0584): the GPS L1 external LNA. Board DT (dtb/nord2/live.dts, gps@18C00000 pinctrl): "gps_l1_lna_dsp_ctrl" =
 * pinmux 0xa202 = GPIO162 mode 2 = GPS_L1_ELNA_EN (pinctrl-mtk-mt6885.h: 0 GPIO162, 1 ANT_SEL0, 2 GPS_L1_ELNA_EN); "disable" =
 * mode 0 output-low; "enable" = mode 0 output-high. The vendor gps_dl driver selects dsp_ctrl when the L1 DSP is on
 * (gps_dl_linux_plat_drv.c gps_dl_lna_pin_ctrl(link 0, dsp_on = true, force = false)): the DSP itself switches the LNA.
 * Nothing on this port ever did that, so the LNA in front of the GNSS input stayed off (pin in its boot mode).
 * GPIO mode register: base 0x10005000 (ni_gpio, already mapped), MODE20 = +0x440, pin 162 = bits [11:8] (mt6885_pin_mode_range
 * PIN_FIELD_BASE(160, 167, 0, 0x0440, 0x10, 0, 4)). Read-modify-write like the Wi-Fi probe's mode writes (ni_mask 0x450/0x460).
 * sysctl kern.n2gnss bit 2 = leave the pin alone.
 */
static void
n2gd_lna_l1_dsp_ctrl(void)
{
	if (n2gd_ctl & 4) { printf("NORD2 GNSS LNA: GPIO162 left alone (kern.n2gnss bit 2)\n"); return; }
	if (!ni_gpio) { printf("NORD2 GNSS LNA: GPIO block not mapped, GPIO162 untouched\n"); return; }
	uint32_t before = ni_rd(ni_gpio, 0x440);
	ni_mask(ni_gpio, 0x440, 0xfu << 8, 2u << 8);
	uint32_t after = ni_rd(ni_gpio, 0x440);
	printf("NORD2 GNSS LNA: GPIO162 mode %u -> %u (2 = GPS_L1_ELNA_EN, DSP-controlled L1 external LNA; MODE20 %#x -> %#x)\n",
	    (before >> 8) & 0xfu, (after >> 8) & 0xfu, before, after);
}
#endif /* NORD2_GNSS_BIND_INC */
