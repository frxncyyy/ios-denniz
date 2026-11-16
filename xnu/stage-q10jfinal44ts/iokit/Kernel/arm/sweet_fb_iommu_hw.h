/*
 * 0401: display IOMMU, KERNEL part.  Included ONCE, by SweetSurface.cpp after the 0394 pool code (it uses sweet_pool_take /
 * sweet_pool_kva / sweet_pool_flush for its page tables).  Pure logic: sweet_fb_iommu.h.  Design + bring-up: work/NOTES.md "0401 display IOMMU".
 *
 * Hardware [V-code, Linux mtk_iommu_v2.c / mt6885 mt_iommu_plat.h / pseudo_m4u_v2.c / clk-mt6885-mm.c, live.dts]:
 *   M4U0 0x1411a000 (bank 0 = normal domain; MMU0 serves larb0/larb1), SPI 300.  Display larbs: larb0 0x14118000, larb1 0x14119000;
 *   per-port NON_SEC_CON 0x380+4p: bit0 MMU_EN, bits[15:8] IOVA[33:32] extension (must stay 0), non-secure writable (no ATF call).
 *   OVL0_2L (pipe 0) reads through larb1 port 6, OVL1_2L (pipe 1) through larb0 port 6 (one data port for all layers of an OVL).
 *   MMSYS_CG_CON1 0x14116110 (status) / 0x14116118 (clear = ungate): bit19 SMI_COMMON, 23 SMI_GALS, 27 SMI_INFRA, 31 SMI_IOMMU.
 * Only the two OVL*_2L data ports are ever switched; every other port of every larb is left as it is (physical).
 */
#ifndef SWEET_FB_IOMMU_HW_H
#define SWEET_FB_IOMMU_HW_H
#include "sweet_fb_iommu.h"
#include <kern/thread_call.h>      /* 0403: stage watchdog */
#include <kern/clock.h>

extern "C" {
extern unsigned long gPhysBase, gPhysSize;
vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);
}
/* 0410: every IOMMU-side register map is made ONCE with ml_io_map_unmappable (kernel_map), never from the 8 MiB io_submap: 0401's five
 * permanent ml_io_map calls (80 KiB of 16 KiB pages) left the nearly full io_submap without room for the SCP thread's delayed maps at
 * ~108 s uptime (the iot1/iosub hangs).  Each map is logged ("NORD2 FB IOMAP:") and followed by a 200 ms console drain. */
#define N2IO_WIMG_IO            0x7u            /* VM_WIMG_IO (osfmk/arm/memory_types.h) */
static volatile uint32_t *
n2io_iomap(uint64_t pa, uint32_t size, const char *what)
{
	vm_offset_t va = ml_io_map_unmappable((vm_offset_t)pa, (vm_size_t)size, N2IO_WIMG_IO);
	IOLog("NORD2 FB IOMAP: %s pa %#llx +%#x -> %#lx (ml_io_map_unmappable, outside io_submap)%s\n", what, pa, size, (unsigned long)va, va ? "" : " FAILED");
	IOSleep(200);
	return (volatile uint32_t *)va;
}

#define N2IO_M4U0               0x1411a000UL
#define N2IO_LARB0              0x14118000UL
#define N2IO_LARB1              0x14119000UL
#define N2IO_MMSYS_CG1          0x14116110UL    /* +8 = clear (ungate) */
#define N2IO_CG_BITS            ((1u << 19) | (1u << 23) | (1u << 27) | (1u << 31))
#define N2IO_GICD               0x0c000000UL
#define N2IO_GICD_ISEN_M4U0     (0x100u + 4u * (332u / 32u))    /* SPI 300 = INTID 332 */
#define N2IO_GICD_BIT_M4U0      (1u << (332u % 32u))
/* M4U0 register offsets */
#define N2IO_PT_BASE            0x000u
#define N2IO_INVLDT             0x020u
#define N2IO_INVLD_START        0x024u
#define N2IO_INVLD_END          0x028u
#define N2IO_INVLD_SEL          0x02cu
#define N2IO_CTRL               0x110u
#define N2IO_TFRP               0x114u
#define N2IO_INT_CTRL0          0x120u
#define N2IO_INT_MAIN           0x124u
#define N2IO_CPE_DONE           0x12cu
#define N2IO_L2_FAULT_ST        0x130u
#define N2IO_FAULT_ST1          0x134u
#define N2IO_FAULT_VA0          0x13cu
#define N2IO_FAULT_PA0          0x140u
#define N2IO_INT_ID0            0x150u
#define N2IO_INT_MAIN_MMU0      0x7u            /* translation fault, multi-hit, invalid PA (not TLB miss) */
#define N2IO_PORT_EXT_MASK      0xff00u
#define N2IO_PORT(p)            (0x380u + 4u * (p))
#define N2IO_OVL_PORT           6u              /* larb1 p6 = OVL0_2L (pipe 0), larb0 p6 = OVL1_2L (pipe 1) */
#define N2IO_LARB_PORTS         15u

static int           n2io_mode = 3;             /* kern.n2fbiommu (effective; 0 after an auto-disable).  0411: default on (boot-arg n2fbiommu) */
static volatile bool n2io_boot_pending;         /* 0411: boot arming scheduled, not run yet: no lazy arming from the display path */
static int           n2io_probe_last;
static IOLock       *n2io_lock;
static uint32_t      n2io_state;                /* 0 unarmed, 1 armed, 2 refused for this boot */
static const char   *n2io_why = "not armed";
static bool          n2io_ports_on, n2io_irq_on;
static volatile uint32_t *n2io_m4u, *n2io_larb[2], *n2io_cg, *n2io_gicd;   /* n2io_larb[pipe]: pipe 0 = larb1, pipe 1 = larb0 */
static struct sweet_fb_iommu_pt n2io_pt;
static struct sweet_fb_iova n2io_iova;
static struct sweet_fb_iommu_ent n2io_ent[SWEET_FB_IOMMU_CACHE];
static struct sweet_fb_iommu_prog n2io_prog;
static uint64_t      n2io_gen = 1;
static uint64_t      n2io_scratch_pa;
static uint32_t      n2io_ptbase_val;
static uint64_t      n2io_log_at;
/* 0403 staged arming (kern.n2fbiommu_stage): every risky write is logged first and followed by a 200 ms pause so the console shows the last
 * step reached; stage 2/3 port enables are switched off by a thread_call watchdog after n2io_wd_ms, independent of the display thread. */
static bool          n2io_stepwise, n2io_no_irq;
static int           n2io_stage_last;
static int n2io_sub_dump(void);      /* 0410: used by n2io_arm_locked before PT_BASE */
static uint32_t      n2io_wd_ms = 2000;
static uint32_t      n2io_stage_ports;          /* bit k: stage-enabled port of n2io_larb[k] (0 = larb1 p6 / OVL0_2L, 1 = larb0 p6 / OVL1_2L) */
static thread_call_t n2io_wd_tc;
static void n2io_step(const char *what) { if (n2io_stepwise) { IOLog("NORD2 FB IOMMU: stage step: %s\n", what); IOSleep(200); } }
static uint32_t      n2io_cpu_seq[SWEET_SURFACE_MAX_ID];        /* write-unlocks per surface id (IOSurfaceUnlock without ReadOnly) */
static struct {
	uint64_t maps, hits, unmaps, evicts, map_fail, verify_bad, refused, layers, dry, cleans;
	uint32_t enables, disables, faults, tlb_timeouts, logged;
} n2io_st;

static inline uint32_t n2io_rd(volatile uint32_t *b, uint32_t off) { uint32_t v = b[off / 4]; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static inline void n2io_wr(volatile uint32_t *b, uint32_t off, uint32_t v) { __asm__ volatile("dmb oshst" ::: "memory"); b[off / 4] = v; __asm__ volatile("dsb osh" ::: "memory"); }

static uint64_t
n2io_now_ms(void)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000000ull;
}

/* Register blocks (display power domain: up while the panel runs; LK configured it). */
static bool
n2io_map_regs(void)
{
	if (n2io_larb[0] == NULL) {
		n2io_cg = n2io_iomap(N2IO_MMSYS_CG1 & ~0xfffUL, 0x1000, "MMSYS config");      /* 0410: kernel_map, once */
		n2io_larb[0] = n2io_iomap(N2IO_LARB1, 0x1000, "larb1");
		n2io_larb[1] = n2io_iomap(N2IO_LARB0, 0x1000, "larb0");
		n2io_m4u = n2io_iomap(N2IO_M4U0, 0x1000, "M4U0");
		n2io_gicd = n2io_iomap(N2IO_GICD, 0x1000, "GICD");
	}
	return n2io_cg != NULL && n2io_larb[0] != NULL && n2io_larb[1] != NULL && n2io_m4u != NULL && n2io_gicd != NULL;
}

static uint32_t n2io_cg_gated(void) { return n2io_rd(n2io_cg, N2IO_MMSYS_CG1 & 0xfffu) & N2IO_CG_BITS; }

/* kern.n2fbiommu_probe=1: READ-ONLY dump (log only).  Touches M4U0 only if its clocks are ungated (a gated MMSYS block can hang the bus). */
static void
n2io_probe(void)
{
	if (!n2io_map_regs()) { IOLog("NORD2 FB IOMMU: probe: register blocks not mappable\n"); return; }
	uint32_t cg = n2io_rd(n2io_cg, N2IO_MMSYS_CG1 & 0xfffu), gic = n2io_rd(n2io_gicd, N2IO_GICD_ISEN_M4U0);
	IOLog("NORD2 FB IOMMU: probe: MMSYS_CG_CON1 %08x (gated of smi_common/gals/infra/iommu: %08x) GICD_ISENABLER10 %08x (spi300 m4u0 %s)\n", cg,
	    cg & N2IO_CG_BITS, gic, (gic & N2IO_GICD_BIT_M4U0) ? "ENABLED" : "masked");
	for (unsigned l = 0; l < 2; l++) {
		volatile uint32_t *b = n2io_larb[l == 0 ? 1 : 0];
		uint32_t v[N2IO_LARB_PORTS];
		for (unsigned p = 0; p < N2IO_LARB_PORTS; p++) { v[p] = n2io_rd(b, N2IO_PORT(p)); }
		IOLog("NORD2 FB IOMMU: probe: larb%u 0x380.. %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", l, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7],
		    v[8], v[9], v[10], v[11], v[12], v[13], v[14]);
	}
	if ((cg & ((1u << 27) | (1u << 31))) != 0) { IOLog("NORD2 FB IOMMU: probe: M4U0 clock gated: M4U0 registers NOT read\n"); return; }
	static const uint16_t off[] = { 0x000, 0x020, 0x024, 0x028, 0x02c, 0x048, 0x050, 0x054, 0x110, 0x114, 0x120, 0x124, 0x12c, 0x130, 0x134,
		                        0x138, 0x13c, 0x140, 0x144, 0x148, 0x150, 0x154 };
	uint32_t m[sizeof(off) / sizeof(off[0])];
	for (unsigned i = 0; i < sizeof(off) / sizeof(off[0]); i++) { m[i] = n2io_rd(n2io_m4u, off[i]); }
	IOLog("NORD2 FB IOMMU: probe: m4u0 pt_base %08x inv %08x %08x %08x %08x misc %08x dcm %08x wrlen %08x ctrl %08x (4gb bit17 %u) tfrp %08x int0 %08x "
	    "int_main %08x cpe %08x l2f %08x fst1 %08x twva %08x fva0 %08x fpa0 %08x fva1 %08x fpa1 %08x id0 %08x id1 %08x\n", m[0], m[1], m[2], m[3], m[4], m[5],
	    m[6], m[7], m[8], (m[8] >> 17) & 1u, m[9], m[10], m[11], m[12], m[13], m[14], m[15], m[16], m[17], m[18], m[19], m[20], m[21]);
	IOLog("NORD2 FB IOMMU: mode=%d state=%u (%s) ports=%u irq=%u maps=%llu hits=%llu unmaps=%llu evicts=%llu live=%u pages=%u iova_kib=%u map_fail=%llu "
	    "verify_bad=%llu refused=%llu layers=%llu dry=%llu enables=%u disables=%u faults=%u tlb_to=%u\n", n2io_mode, n2io_state, n2io_why, n2io_ports_on,
	    n2io_irq_on, n2io_st.maps, n2io_st.hits, n2io_st.unmaps, n2io_st.evicts, n2io_iova.live, n2io_pt.mapped, n2io_iova.granules * 64u, n2io_st.map_fail,
	    n2io_st.verify_bad, n2io_st.refused, n2io_st.layers, n2io_st.dry, n2io_st.enables, n2io_st.disables, n2io_st.faults, n2io_st.tlb_timeouts);
}

static void
n2io_tlb_all(void)
{
	n2io_wr(n2io_m4u, N2IO_INVLD_SEL, 3u);
	n2io_wr(n2io_m4u, N2IO_INVLDT, 2u);
}

/* Range invalidate (Linux mtk_iommu_tlb_flush_range_sync); CPE_DONE timeout (5 ms) -> invalidate all. */
static void
n2io_tlb_range(uint32_t iova, uint32_t bytes)
{
	if (bytes == 0) { return; }
	n2io_wr(n2io_m4u, N2IO_INVLD_SEL, 3u);
	n2io_wr(n2io_m4u, N2IO_INVLD_START, iova & ~0xfffu);
	n2io_wr(n2io_m4u, N2IO_INVLD_END, (iova + bytes - 1u) & ~0xfffu);
	n2io_wr(n2io_m4u, N2IO_INVLDT, 1u);
	unsigned i = 0;
	while (n2io_rd(n2io_m4u, N2IO_CPE_DONE) == 0 && i++ < 500) { IODelay(10); }
	if (i > 500) { n2io_st.tlb_timeouts++; n2io_tlb_all(); }
	n2io_wr(n2io_m4u, N2IO_INVLD_START, 0); n2io_wr(n2io_m4u, N2IO_INVLD_END, 0); n2io_wr(n2io_m4u, N2IO_CPE_DONE, 0);
}

static void
n2io_refuse(const char *why)
{
	n2io_state = 2; n2io_why = why; n2io_mode = 0;
	IOLog("NORD2 FB IOMMU: REFUSED for this boot: %s\n", why);
}

/* Global M4U0 setup (lock held, thread context).  Never touches a port. */
static bool
n2io_arm_locked(void)
{
	if (n2io_state != 0) { return n2io_state == 1; }
	if (!n2io_map_regs()) { n2io_refuse("register blocks not mappable"); return false; }
	uint32_t g = n2io_cg_gated();
	if (g != 0) { n2io_step("ungate MMSYS SMI/IOMMU clocks (write CG_CON1 clear)"); }
	if (g != 0) {      /* Linux only ever ungates these ("dummy" gates); MMSYS is powered while the panel runs */
		n2io_wr(n2io_cg, (N2IO_MMSYS_CG1 & 0xfffu) + 8u, g);
		IOLog("NORD2 FB IOMMU: ungated MMSYS clocks %08x -> gated now %08x\n", g, n2io_cg_gated());
		if (n2io_cg_gated() != 0) { n2io_refuse("SMI/IOMMU clocks stay gated"); return false; }
	}
	for (unsigned k = 0; k < 2; k++) {
		for (unsigned p = 0; p < N2IO_LARB_PORTS; p++) {
			uint32_t v = n2io_rd(n2io_larb[k], N2IO_PORT(p));
			if (v & (1u | N2IO_PORT_EXT_MASK)) {      /* someone translates on a display larb: its table is the current PT_BASE */
				IOLog("NORD2 FB IOMMU: larb%u port %u NON_SEC_CON %08x (MMU_EN/ext set)\n", k == 0 ? 1u : 0u, p, v);
				n2io_refuse("a display larb port already translates (MMU_EN or IOVA extension set)");
				return false;
			}
		}
	}
	uint64_t pa = sweet_pool_take(SWEET_FB_IOMMU_BLOCK_BYTES);
	if (pa == 0) { n2io_refuse("no display-pool block for the page tables (pool off/full/disabled)"); return false; }
	volatile uint8_t *va = sweet_pool_kva + (pa - SWEET_FB_HW_POOL_BASE);
	if (!sweet_fb_iommu_pt_init(&n2io_pt, va, pa)) { sweet_pool_give(pa, SWEET_FB_IOMMU_BLOCK_BYTES); n2io_refuse("page-table block misplaced"); return false; }
	sweet_pool_flush((vm_offset_t)va, SWEET_FB_IOMMU_BLOCK_BYTES);
	sweet_fb_iova_init(&n2io_iova);
	n2io_scratch_pa = pa + SWEET_FB_IOMMU_OFF_SCRATCH;
	uint32_t old_pt = n2io_rd(n2io_m4u, N2IO_PT_BASE), old_tfrp = n2io_rd(n2io_m4u, N2IO_TFRP), ctrl = n2io_rd(n2io_m4u, N2IO_CTRL);
	uint32_t old_int = n2io_rd(n2io_m4u, N2IO_INT_MAIN), gic = n2io_rd(n2io_gicd, N2IO_GICD_ISEN_M4U0);
	n2io_ptbase_val = (uint32_t)(n2io_pt.l1_pa & 0xffffff80u) | (uint32_t)((n2io_pt.l1_pa >> 32) & 7u);
	{   /* 0410: never point PT_BASE at our table while another powered M4U0 master translates through the current one */
		int f = n2io_sub_dump();
		if (f != 0) { n2io_refuse(f < 0 ? "SPM/larb registers not mappable for the M4U0 port check" : "a powered M4U0 larb (5/7/11/14/17/19/20) has an MMU_EN port"); return false; }
	}
	n2io_step("write M4U0 PT_BASE (no port translates yet)");
	n2io_wr(n2io_m4u, N2IO_PT_BASE, n2io_ptbase_val);
	n2io_step("write M4U0 TFRP");
	n2io_wr(n2io_m4u, N2IO_TFRP, (uint32_t)(n2io_scratch_pa & 0xffffff80u));     /* faulting reads land on a zero page in the pool */
	n2io_irq_on = false;
	if (!(gic & N2IO_GICD_BIT_M4U0) && !n2io_no_irq) {      /* line masked at the GIC: the fault status latches, no unhandled interrupt can reach XNU */
		n2io_wr(n2io_m4u, N2IO_INT_MAIN, old_int | N2IO_INT_MAIN_MMU0);
		n2io_irq_on = true;
	}
	n2io_step("M4U0 TLB invalidate all");
	n2io_tlb_all();
	uint32_t rb = n2io_rd(n2io_m4u, N2IO_PT_BASE);
	IOLog("NORD2 FB IOMMU: armed: tables pa %#llx (l1 64 KiB, %u l2, window %#x+%u MiB, identity fb %#x-%#x low %#x-%#x), PT_BASE %08x -> %08x (read %08x), "
	    "TFRP %08x -> %08llx, CTRL %08x (4gb bit17 %u), INT_MAIN %08x -> %08x (gic spi300 %s)\n", pa, SWEET_FB_IOMMU_NL2, SWEET_FB_IOMMU_WIN_BASE,
	    SWEET_FB_IOMMU_WIN_SIZE >> 20, SWEET_FB_IOMMU_ID_FB_BASE, SWEET_FB_IOMMU_ID_FB_END, SWEET_FB_IOMMU_ID_LOW_BASE, SWEET_FB_IOMMU_ID_LOW_END, old_pt,
	    n2io_ptbase_val, rb, old_tfrp, n2io_scratch_pa & 0xffffff80u, ctrl, (ctrl >> 17) & 1u, old_int, n2io_rd(n2io_m4u, N2IO_INT_MAIN),
	    (gic & N2IO_GICD_BIT_M4U0) ? "ENABLED: fault interrupts left off, status polled" : "masked");
	if (rb != n2io_ptbase_val) { n2io_refuse("PT_BASE does not read back"); return false; }
	n2io_state = 1; n2io_why = "armed";
	return true;
}

/* 0415: the OVL data ports of both pipes switch together: p6 (OVL*_2L) and p5 (the cascaded 4L OVL*) of larb0 and larb1, between transfers */
#define N2IO_OVL4_PORT          5u
static void
n2io_ports(bool on)
{
	for (unsigned k = 0; k < 2; k++) {
		for (unsigned q = 0; q < 2; q++) {
			uint32_t port = q ? N2IO_OVL4_PORT : N2IO_OVL_PORT;
			uint32_t v = n2io_rd(n2io_larb[k], N2IO_PORT(port));
			n2io_wr(n2io_larb[k], N2IO_PORT(port), on ? (v | 1u) : (v & ~1u));
		}
	}
	n2io_ports_on = on;
	if (on) { n2io_st.enables++; } else { n2io_st.disables++; }
	if (n2io_st.enables + n2io_st.disables <= 16) {
		IOLog("NORD2 FB IOMMU: OVL ports MMU_EN %s: larb1 p6 %08x p5 %08x larb0 p6 %08x p5 %08x\n", on ? "ON" : "off",
		    n2io_rd(n2io_larb[0], N2IO_PORT(N2IO_OVL_PORT)), n2io_rd(n2io_larb[0], N2IO_PORT(N2IO_OVL4_PORT)),
		    n2io_rd(n2io_larb[1], N2IO_PORT(N2IO_OVL_PORT)), n2io_rd(n2io_larb[1], N2IO_PORT(N2IO_OVL4_PORT)));
	}
}

static void
n2io_unmap_ent(struct sweet_fb_iommu_ent *e)
{
	sweet_fb_iommu_unmap(&n2io_pt, e->iova, e->pages);
	sweet_pool_flush((vm_offset_t)sweet_fb_iommu_l2e(&n2io_pt, e->iova), (size_t)e->pages * 4u);
	n2io_tlb_range(e->iova, e->pages * 0x1000u);
	(void)sweet_fb_iova_free(&n2io_iova, e->iova, (uint64_t)e->pages * 0x1000u);
	n2io_st.unmaps++;
	n2io_wod_unmapped(e);      /* 0480: the hold of a wire-on-demand surface goes after the unmap + TLB invalidate */
	e->obj = NULL;
}

static bool
n2io_ram(uint64_t pa)
{
	return !(pa & 0xfffu) && ((pa >= gPhysBase && pa + 0x1000u <= (uint64_t)gPhysBase + gPhysSize) || sweet_fb_hw_pool_contains(pa, 0x1000u) ||
	    sweet_heap_page_ok(pa));      /* 0440: graphics-heap pages (only ever reached through an IOVA PTE, never as a physical OVL address) */
}

/* Map desc (len bytes) for obj/id; lock held.  NULL = not mappable. */
static struct sweet_fb_iommu_ent *
n2io_map_locked(const void *obj, uint32_t id, IOMemoryDescriptor *desc, uint64_t len)
{
	uint32_t pages = (uint32_t)((len + 0xfffu) >> 12);
	if (desc == NULL || len == 0 || len > SWEET_FB_IOMMU_WIN_SIZE / 2) { return NULL; }
	int slot = -1;
	for (unsigned i = 0; i < SWEET_FB_IOMMU_CACHE; i++) { if (n2io_ent[i].obj == NULL) { slot = (int)i; break; } }
	uint32_t iova = slot >= 0 ? sweet_fb_iova_alloc(&n2io_iova, (uint64_t)pages << 12) : SWEET_FB_IOVA_FAIL;
	while (slot < 0 || iova == SWEET_FB_IOVA_FAIL) {
		int v = sweet_fb_iommu_victim(n2io_ent, SWEET_FB_IOMMU_CACHE, &n2io_prog, n2io_gen);
		if (v < 0) {
			if (iova != SWEET_FB_IOVA_FAIL) { (void)sweet_fb_iova_free(&n2io_iova, iova, (uint64_t)pages << 12); }
			n2io_st.map_fail++;
			if (n2io_st.map_fail <= 8) { IOLog("NORD2 FB IOMMU: map id %u refused at page 0 of %u: cache/window full of programmed or current mappings\n", id, pages); }
			return NULL;
		}
		n2io_unmap_ent(&n2io_ent[v]); n2io_st.evicts++;
		if (slot < 0) { slot = v; }
		if (iova == SWEET_FB_IOVA_FAIL) { iova = sweet_fb_iova_alloc(&n2io_iova, (uint64_t)pages << 12); }
	}
	uint64_t lo = ~0ull, hi = 0, prev = ~0ull;
	uint32_t segs = 0, i = 0;
	for (; i < pages; i++) {
		IOByteCount sl = 0;
		uint64_t pa = desc->getPhysicalSegment((IOByteCount)i << 12, &sl, kIOMemoryMapperNone);
		if (!n2io_ram(pa) || !sweet_fb_iommu_map_page(&n2io_pt, iova + (i << 12), pa)) { break; }
		if (pa != prev + 0x1000u) { segs++; }
		prev = pa; if (pa < lo) { lo = pa; } if (pa > hi) { hi = pa; }
	}
	if (i < pages) {      /* the i pages written go again; the whole allocation goes back */
		sweet_fb_iommu_unmap(&n2io_pt, iova, i);
		sweet_pool_flush((vm_offset_t)sweet_fb_iommu_l2e(&n2io_pt, iova), (size_t)pages * 4u);
		n2io_tlb_range(iova, pages << 12);
		(void)sweet_fb_iova_free(&n2io_iova, iova, (uint64_t)pages << 12);
		n2io_st.map_fail++;
		if (n2io_st.map_fail <= 8) { IOLog("NORD2 FB IOMMU: map id %u refused at page %u of %u (not DRAM or bad PA)\n", id, i, pages); }
		return NULL;
	}
	struct sweet_fb_iommu_ent *e = &n2io_ent[slot];
	e->obj = obj; e->id = id; e->iova = iova; e->pages = pages; e->bytes = (uint32_t)len; e->stamp = n2io_gen; e->cpu_seq = ~0u;
	sweet_pool_flush((vm_offset_t)sweet_fb_iommu_l2e(&n2io_pt, iova), (size_t)pages * 4u);
	n2io_tlb_range(iova, pages << 12);      /* TLBI_ON_MAP, as Linux */
	uint32_t bad = 0;      /* CPU walk of the table against the descriptor, every page */
	for (i = 0; i < pages; i++) {
		IOByteCount sl = 0;
		uint64_t pa = desc->getPhysicalSegment((IOByteCount)i << 12, &sl, kIOMemoryMapperNone);
		if (sweet_fb_iommu_walk(&n2io_pt, (uint64_t)iova + ((uint64_t)i << 12) + 0x7c4u) != pa + 0x7c4u) { bad++; }
	}
	n2io_st.maps++;
	if (bad != 0) { n2io_st.verify_bad++; }
	if (n2io_st.logged < 32 || bad != 0 || (n2io_st.maps % 256) == 0) {
		n2io_st.logged++;
		IOLog("NORD2 FB IOMMU: map id %u %llu bytes -> iova %#x pages %u segs %u pa %#llx..%#llx above_4g %u walk %s (live %u, window %u KiB used)\n", id, len,
		    iova, pages, segs, lo, hi + 0xfffu, lo >= 0x100000000ull ? 1u : 0u, bad ? "BAD" : "ok", n2io_iova.live, n2io_iova.granules * 64u);
	}
	if (bad != 0) { n2io_unmap_ent(e); return NULL; }
	return e;
}

#include "sweet_fb_iommu_wod.inc"      /* 0480: wire-on-demand surfaces: entry holds, idle unmap, page-wise cache clean */

/* Fault status (lock held).  true = a fault was latched (logged, cleared, IOMMU auto-disabled). */
static bool
n2io_poll_fault_locked(const char *where)
{
	if (n2io_state != 1) { return false; }
	uint32_t st = n2io_rd(n2io_m4u, N2IO_FAULT_ST1), l2 = n2io_rd(n2io_m4u, N2IO_L2_FAULT_ST);
	if ((st & 0x7fu) == 0) { return false; }
	uint32_t va = n2io_rd(n2io_m4u, N2IO_FAULT_VA0), fpa = n2io_rd(n2io_m4u, N2IO_FAULT_PA0), id = n2io_rd(n2io_m4u, N2IO_INT_ID0);
	n2io_st.faults++;
	IOLog("NORD2 FB IOMMU: fault (%s): fault_st1 %08x l2 %08x va %08x (iova %#x%s) pa %08x int_id %08x (larb axi %u port %u) ports %u prog %08x %08x | %08x %08x\n",
	    where, st, l2, va, va & 0xfffff000u, (va & 0x10u) ? " translation fault" : ((va & 0x20u) ? " invalid pa" : ""), fpa, id, (id >> 7) & 0x1fu,
	    (id >> 2) & 0x1fu, n2io_ports_on, n2io_prog.addr[0][0], n2io_prog.addr[0][1], n2io_prog.addr[1][0], n2io_prog.addr[1][1]);
	if (va & 0x20u) { sweet_heap_m4u_fault_pa(fpa); }      /* 0440: invalid-PA fault on a heap page: that region takes no new allocations */
	n2io_wr(n2io_m4u, N2IO_INT_CTRL0, n2io_rd(n2io_m4u, N2IO_INT_CTRL0) | (1u << 12));      /* clear */
	n2io_tlb_all();                                                                           /* re-arm: every walk starts again */
	if (n2io_rd(n2io_m4u, N2IO_PT_BASE) != n2io_ptbase_val) {                                  /* 0416: state lost: PT_BASE/TFRP back */
		n2io_wr(n2io_m4u, N2IO_PT_BASE, n2io_ptbase_val);
		n2io_wr(n2io_m4u, N2IO_TFRP, (uint32_t)(n2io_scratch_pa & 0xffffff80u));
		n2io_tlb_all();
	}
	if (n2io_st.faults <= 16 || (n2io_st.faults % 256) == 0) {
		IOLog("NORD2 FB IOMMU: fault #%u cleared, TLB invalidated, PT_BASE %08x: the IOMMU stays on (0416: no disable, no other path)\n", n2io_st.faults,
		    n2io_rd(n2io_m4u, N2IO_PT_BASE));
	}
	return true;
}

static void
n2io_stats_maybe(void)
{
	uint64_t now = n2io_now_ms();
	if (now - n2io_log_at < 10000) { return; }
	n2io_log_at = now;
	IOLog("NORD2 FB IOMMU: mode=%d state=%u ports=%u maps=%llu hits=%llu unmaps=%llu evicts=%llu live=%u pages=%u map_fail=%llu verify_bad=%llu refused=%llu "
	    "layers=%llu dry=%llu enables=%u disables=%u faults=%u tlb_to=%u cleans=%llu\n", n2io_mode, n2io_state, n2io_ports_on, n2io_st.maps, n2io_st.hits, n2io_st.unmaps,
	    n2io_st.evicts, n2io_iova.live, n2io_pt.mapped, n2io_st.map_fail, n2io_st.verify_bad, n2io_st.refused, n2io_st.layers, n2io_st.dry, n2io_st.enables,
	    n2io_st.disables, n2io_st.faults, n2io_st.tlb_timeouts, n2io_st.cleans);
}

static bool
n2io_lock_init(void)
{
	if (n2io_lock == NULL) {
		IOLock *l = IOLockAlloc();
		if (l != NULL && !OSCompareAndSwapPtr(NULL, l, (void * volatile *)&n2io_lock)) { IOLockFree(l); }
	}
	return n2io_lock != NULL;
}

/* ---------------------------------------------------------------- API (sweet_fb_iommu.h) */
extern "C" uint32_t
sweet_fb_iommu_mode(void)
{
	return (uint32_t)__atomic_load_n(&n2io_mode, __ATOMIC_RELAXED);
}

extern "C" bool
sweet_fb_iommu_layer(void *surface, uint64_t *iova, uint64_t *len)
{
	SweetSurface *s = (SweetSurface *)surface;
	if (n2io_boot_pending) { return false; }      /* 0411: the boot thread_call arms (stepwise, logged), never the present thread */
	int mode = __atomic_load_n(&n2io_mode, __ATOMIC_RELAXED);
	if (mode < 1 || s == NULL || s->memory == NULL || n2io_state == 2 || !n2io_lock_init()) { return false; }
	IOLockLock(n2io_lock);
	bool ok = false;
	struct sweet_fb_iommu_ent *e = NULL;
	if (n2io_arm_locked()) {
		for (unsigned i = 0; i < SWEET_FB_IOMMU_CACHE; i++) { if (n2io_ent[i].obj == s && n2io_ent[i].id == s->id) { e = &n2io_ent[i]; break; } }
		if (e != NULL) { n2io_st.hits++; e->stamp = n2io_gen; }
		else { e = n2io_map_surface(s); }      /* 0480: n2io_map_locked under a hold */
		/* The OVL does not snoop: pixels the CPU wrote (IOSurfaceUnlock only marks GPU imports dirty; zero-fill, kernel copy-in) may sit dirty
		 * in the cache.  Clean to PoC at the first map and after every CPU write-unlock; GPU-written frames cost nothing (lines already clean). */
		uint32_t cs = s->id < SWEET_SURFACE_MAX_ID ? __atomic_load_n(&n2io_cpu_seq[s->id], __ATOMIC_ACQUIRE) : 0u;
		IOBufferMemoryDescriptor *buf = OSDynamicCast(IOBufferMemoryDescriptor, s->memory);
		if (e != NULL && e->cpu_seq != cs && buf != NULL) {
			n2io_clean_surface(s, buf, e->bytes);      /* 0480: a pageable surface has no kernel mapping */
			e->cpu_seq = cs; n2io_st.cleans++;
		}
		if (e != NULL && mode >= 2 && n2io_mode >= 2) { *iova = e->iova; *len = e->bytes; ok = true; n2io_st.layers++; }
		else if (e != NULL) { n2io_st.dry++; }
		if (mode < 2) { n2io_gen++; }      /* dry run: nothing is planned with these, so no lookup has to stay "current" */
	}
	n2io_stats_maybe();
	IOLockUnlock(n2io_lock);
	return ok;
}

/* 0401 gate, 0415: for slots [s0, s0+ns) of one pipe (2L: slots 0/1, 4L: slots 2..5); a[i] 0xffffffff = not in the list */
static bool
n2io_gate_slots(unsigned pipe, unsigned s0, unsigned ns, const uint32_t *a)
{
	if (pipe > 1 || s0 + ns > SWEET_FB_IOMMU_PSLOTS) { return false; }
	bool has = false;
	for (unsigned i = 0; i < ns; i++) { if (a[i] != 0xffffffffu && sweet_fb_iommu_in_window(a[i], (uint64_t)a[i] + 1u)) { has = true; } }
	if (!has && n2io_state != 1) { return true; }      /* never armed: nothing to do (the 0396 path, untouched) */
	if (!n2io_lock_init()) { return !has; }
	IOLockLock(n2io_lock);
	(void)n2io_poll_fault_locked("before regs");
	bool armed = n2io_state == 1 && n2io_mode >= 2;
	bool other = sweet_fb_iommu_prog_other(&n2io_prog, pipe, s0, ns);      /* 0415: every other slot of both pipes, 4L included */
	int d = sweet_fb_iommu_gate_decide(has, n2io_ports_on, armed, other);
	if (d == SWEET_FB_IOMMU_G_KEEP && has && n2io_ports_on &&
	    !((n2io_rd(n2io_larb[0], N2IO_PORT(N2IO_OVL_PORT)) & n2io_rd(n2io_larb[1], N2IO_PORT(N2IO_OVL_PORT)) &
	       n2io_rd(n2io_larb[0], N2IO_PORT(N2IO_OVL4_PORT)) & n2io_rd(n2io_larb[1], N2IO_PORT(N2IO_OVL4_PORT))) & 1u)) {      /* 0415: p5 too */
		IOLog("NORD2 FB IOMMU: OVL port MMU_EN lost (display power cycle?): enabling again\n");      /* else an IOVA would be read as a PA */
		n2io_ports_on = false; d = SWEET_FB_IOMMU_G_ENABLE;
	}
	if (d == SWEET_FB_IOMMU_G_ENABLE && n2io_rd(n2io_m4u, N2IO_PT_BASE) != n2io_ptbase_val) {      /* M4U state lost (power gating): re-arm */
		IOLog("NORD2 FB IOMMU: PT_BASE lost (%08x): rewriting PT_BASE/TFRP\n", n2io_rd(n2io_m4u, N2IO_PT_BASE));
		n2io_wr(n2io_m4u, N2IO_PT_BASE, n2io_ptbase_val);
		n2io_wr(n2io_m4u, N2IO_TFRP, (uint32_t)(n2io_scratch_pa & 0xffffff80u));
		n2io_tlb_all();
		if (n2io_rd(n2io_m4u, N2IO_PT_BASE) != n2io_ptbase_val) { IOLog("NORD2 FB IOMMU: PT_BASE does not stick: frame held, retried\n"); d = SWEET_FB_IOMMU_G_REFUSE; }
	}
	bool ok = true;
	if (d == SWEET_FB_IOMMU_G_REFUSE) { n2io_st.refused++; ok = false; }
	else if (d == SWEET_FB_IOMMU_G_ENABLE) { n2io_ports(true); }
	else if (d == SWEET_FB_IOMMU_G_DISABLE) { n2io_ports(false); }
	if (ok) {
		for (unsigned i = 0; i < ns; i++) { if (a[i] != 0xffffffffu) { n2io_prog.addr[pipe][s0 + i] = a[i]; } }
		n2io_gen++;      /* lookups before this write are no longer "being planned" */
	}
	IOLockUnlock(n2io_lock);
	return ok;
}

extern "C" bool
sweet_fb_iommu_gate(unsigned pipe, uint32_t addr0, uint32_t addr1)
{
	uint32_t a[2] = { addr0, addr1 };
	return n2io_gate_slots(pipe, 0, 2, a);
}

extern "C" bool
sweet_fb_iommu_gate4(unsigned pipe, const uint32_t a[4])      /* 0415: the cascaded 4L block = slots 2..5 */
{
	return n2io_gate_slots(pipe, 2, 4, a);
}

/* 0408: for the overlay init check (SweetFramebuffer.cpp hwInit R11).  n2io_larb[0] = larb1, n2io_larb[1] = larb0; stage bit k = n2io_larb[k]. */
extern "C" unsigned
sweet_fb_iommu_owned_ports(void)
{
	if (__atomic_load_n(&n2io_state, __ATOMIC_ACQUIRE) != 1) { return 0; }
	unsigned st = __atomic_load_n(&n2io_stage_ports, __ATOMIC_RELAXED), m = 0;
	if (__atomic_load_n(&n2io_ports_on, __ATOMIC_RELAXED)) { st |= 3u; m |= 0xcu; }      /* 0415: + larb0 p5 (bit2), larb1 p5 (bit3) */
	if (st & 2u) { m |= 1u; }      /* larb0 p6 */
	if (st & 1u) { m |= 2u; }      /* larb1 p6 */
	return m;
}

extern "C" bool
sweet_fb_iommu_frame_done(void)
{
	if (n2io_state != 1 || n2io_lock == NULL) { return false; }
	IOLockLock(n2io_lock);
	bool f = n2io_poll_fault_locked("frame done");
	n2io_stats_maybe();
	IOLockUnlock(n2io_lock);
	return f;
}

/* IOSurfaceUnlock (write): the next IOMMU scan of this surface cleans its pixels first. */
static void
sweet_fb_iommu_cpu_wrote(const SweetSurface *s)
{
	if (s != NULL && s->id < SWEET_SURFACE_MAX_ID) { (void)__atomic_add_fetch(&n2io_cpu_seq[s->id], 1u, __ATOMIC_RELEASE); }
}

/* SweetSurface::free: the display no longer holds it (it would hold a retain), so its mapping goes. */
static void
sweet_fb_iommu_surface_gone(const void *obj)
{
	if (n2io_state != 1 || n2io_lock == NULL) { return; }
	IOLockLock(n2io_lock);
	for (unsigned i = 0; i < SWEET_FB_IOMMU_CACHE; i++) {
		if (n2io_ent[i].obj != obj) { continue; }
		if (sweet_fb_iommu_ent_programmed(&n2io_ent[i], &n2io_prog)) { IOLog("NORD2 FB IOMMU: surface %u freed while its IOVA %#x is programmed\n", n2io_ent[i].id, n2io_ent[i].iova); }
		n2io_unmap_ent(&n2io_ent[i]);
	}
	IOLockUnlock(n2io_lock);
}

/* ---------------------------------------------------------------- sysctls */
static int
n2io_sysctl_mode SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = n2io_mode, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 3) { return EINVAL; }
	if (!n2io_lock_init()) { return ENOMEM; }
	IOLockLock(n2io_lock);
	int old = n2io_mode;
	if (v > 0 && n2io_state == 2) { err = EPERM; }
	else {
		n2io_mode = v;
		if (v > 0 && !n2io_arm_locked()) { err = EPERM; }      /* arm = tables + PT_BASE only; ports switch at the next HW frame */
	}
	IOLog("NORD2 FB IOMMU: MODE old=%d new=%d why=sysctl state=%u (%s)%s\n", old, n2io_mode, n2io_state, n2io_why, err ? " REFUSED" : "");
	IOLockUnlock(n2io_lock);
	return err;
}
static int
n2io_sysctl_probe SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = n2io_probe_last, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	n2io_probe_last = v;
	if (v == 1) {
		if (n2io_lock_init()) { IOLockLock(n2io_lock); }
		n2io_probe();
		if (n2io_lock != NULL) { IOLockUnlock(n2io_lock); }
	}
	return 0;
}
SYSCTL_PROC(_kern, OID_AUTO, n2fbiommu, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2io_sysctl_mode, "I", "0401: display IOMMU 0 off 1 map-only 2 single 3 all");
SYSCTL_PROC(_kern, OID_AUTO, n2fbiommu_probe, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2io_sysctl_probe, "I", "0401: 1 = log M4U0/larb/clock registers (read-only)");
/* ---------------------------------------------------------------- 0403 staged arming */
static void
n2io_stage_port(unsigned k, bool on)
{
	uint32_t v = n2io_rd(n2io_larb[k], N2IO_PORT(N2IO_OVL_PORT));
	n2io_wr(n2io_larb[k], N2IO_PORT(N2IO_OVL_PORT), on ? (v | 1u) : (v & ~1u));
	if (on) { n2io_stage_ports |= 1u << k; } else { n2io_stage_ports &= ~(1u << k); }
	IOLog("NORD2 FB IOMMU: stage port %s p6 MMU_EN %s: %08x -> %08x\n", k == 0 ? "larb1 (OVL0_2L, left half)" : "larb0 (OVL1_2L, right half)", on ? "ON" : "off", v,
	    n2io_rd(n2io_larb[k], N2IO_PORT(N2IO_OVL_PORT)));
}

static void
n2io_wd_fire(thread_call_param_t, thread_call_param_t)
{
	IOLockLock(n2io_lock);
	uint32_t was = n2io_stage_ports, st = n2io_state == 1 ? n2io_rd(n2io_m4u, N2IO_FAULT_ST1) : 0;
	bool f = n2io_poll_fault_locked("stage watchdog");
	for (unsigned k = 0; k < 2; k++) { if (was & (1u << k)) { n2io_stage_port(k, false); } }
	IOLog("NORD2 FB IOMMU: stage watchdog after %u ms: ports 0x%x off, fault_st1 %08x%s; display survived identity translation\n", n2io_wd_ms, was, st,
	    f ? " (FAULT logged above)" : "");
	IOLockUnlock(n2io_lock);
}

/* kern.n2fbiommu_stage: 1 = arm (PT_BASE, TFRP, TLB; no interrupt enable; no port), logged step by step;
 * 2 = MMU_EN on larb0 p6 only (OVL1_2L, right half; panel buffers identity-mapped) for kern.n2fbiommu_wdms; 3 = both ports for that time;
 * 0 = stage ports off now.  Only while kern.n2fbiommu < 2 (never mixes with IOVA frames). */
static int
n2io_sysctl_stage SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = n2io_stage_last, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 3) { return EINVAL; }
	if (!n2io_lock_init()) { return ENOMEM; }
	if (n2io_wd_tc == NULL) { n2io_wd_tc = thread_call_allocate(n2io_wd_fire, NULL); }
	IOLog("NORD2 FB IOMMU: STAGE %d requested (state %u, mode %d, stage ports 0x%x, watchdog %u ms)\n", v, n2io_state, n2io_mode, n2io_stage_ports, n2io_wd_ms);
	IOSleep(200);
	IOLockLock(n2io_lock);
	n2io_stage_last = v;
	if (n2io_mode >= 2 || n2io_ports_on) { err = EBUSY; }
	else if (v == 1) {
		n2io_stepwise = true; n2io_no_irq = true;
		bool ok = n2io_arm_locked();
		n2io_stepwise = false;
		IOLog("NORD2 FB IOMMU: STAGE 1 %s: PT_BASE read %08x fault_st1 %08x\n", ok ? "done" : "REFUSED", n2io_m4u ? n2io_rd(n2io_m4u, N2IO_PT_BASE) : 0u,
		    (ok && n2io_m4u) ? n2io_rd(n2io_m4u, N2IO_FAULT_ST1) : 0u);
		if (!ok) { err = EPERM; }
	} else if (v >= 2) {
		if (n2io_state != 1 || n2io_wd_tc == NULL) { err = EPERM; }
		else {
			n2io_stepwise = true;
			n2io_step(v == 2 ? "MMU_EN on larb0 p6 (OVL1_2L) with identity map, watchdog armed" : "MMU_EN on larb1 p6 + larb0 p6 with identity map, watchdog armed");
			n2io_stepwise = false;
			uint64_t dl = 0;
			clock_interval_to_deadline(n2io_wd_ms, kMillisecondScale, &dl);
			thread_call_enter_delayed(n2io_wd_tc, dl);      /* armed BEFORE the enable: it fires even if this thread stalls */
			n2io_stage_port(1, true);
			if (v == 3) { n2io_stage_port(0, true); }
		}
	} else {
		for (unsigned k = 0; k < 2; k++) { if (n2io_stage_ports & (1u << k)) { n2io_stage_port(k, false); } }
	}
	IOLockUnlock(n2io_lock);
	return err;
}
SYSCTL_PROC(_kern, OID_AUTO, n2fbiommu_stage, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2io_sysctl_stage, "I", "0403: staged arming 1 arm 2 one port 3 both (watchdog)");
SYSCTL_UINT(_kern, OID_AUTO, n2fbiommu_wdms, CTLFLAG_RW | CTLFLAG_LOCKED, &n2io_wd_ms, 0, "0403: stage 2/3 watchdog ms");
/* ---------------------------------------------------------------- 0409 stage-1 sub-steps (kern.n2fbiommu_sub, bisect of the T1 lockup)
 * Bits: 0x01 prep (map the display pool, take + fill the 580 KiB table block: no M4U write), 0x02 PT_BASE write, 0x04 TFRP write,
 * 0x08 TLB invalidate all (INVLID_SEL 3 + INVLDT 2), 0x10 read-only dump of the NON_SEC_CON ports of the other M4U0 larbs (5 VDEC,
 * 7 VENC/JPEG, 11 ISP2, 14 CAM, 17 CAM_RAWB, 19/20 IPE) -- only larbs whose SPM power domain reads ON in PWR_STATUS and PWR_STATUS_2ND,
 * 0x80 undo (PT_BASE 0, TFRP 0, TLB invalidate all).  0x02/0x04/0x08 imply 0x01 if not prepared yet.  Never touches CTRL, INT_MAIN, a
 * clock gate or any larb port; refused (EBUSY) once armed by kern.n2fbiommu / kern.n2fbiommu_stage (only 0x10 allowed then); undo
 * combined with a write bit = EINVAL.  Every write logged first + 200 ms console drain; "SUB 0x.. done" line with a register readback.
 * Unset (default) = no code runs: the 0401/0403 paths are unchanged.  The pure order/refusal rule is host-tested (test/iommusub_host_test.c
 * extracts the block between the PURE markers). */
/* 0409-PURE-BEGIN */
enum { N2IO_SUB_PREP = 0x01, N2IO_SUB_PTBASE = 0x02, N2IO_SUB_TFRP = 0x04, N2IO_SUB_TLB = 0x08, N2IO_SUB_DUMP = 0x10, N2IO_SUB_UNDO = 0x80,
       N2IO_SUB_ALL = 0x9f };
/* Ordered actions for value v; returns their number (0..6) or -22 (EINVAL) / -16 (EBUSY). */
static inline int
n2io_sub_plan(uint32_t v, bool armed, bool prepped, uint8_t act[6])
{
	int n = 0;
	if (v & ~(uint32_t)N2IO_SUB_ALL) { return -22; }
	if ((v & N2IO_SUB_UNDO) && (v & (N2IO_SUB_PREP | N2IO_SUB_PTBASE | N2IO_SUB_TFRP | N2IO_SUB_TLB))) { return -22; }
	if (armed && (v & ~(uint32_t)N2IO_SUB_DUMP)) { return -16; }
	if (v & N2IO_SUB_DUMP) { act[n++] = N2IO_SUB_DUMP; }                 /* read-only, before anything is written */
	if ((v & N2IO_SUB_PREP) || ((v & (N2IO_SUB_PTBASE | N2IO_SUB_TFRP | N2IO_SUB_TLB)) && !prepped)) { act[n++] = N2IO_SUB_PREP; }
	if (v & N2IO_SUB_PTBASE) { act[n++] = N2IO_SUB_PTBASE; }
	if (v & N2IO_SUB_TFRP) { act[n++] = N2IO_SUB_TFRP; }
	if (v & N2IO_SUB_TLB) { act[n++] = N2IO_SUB_TLB; }
	if (v & N2IO_SUB_UNDO) { act[n++] = N2IO_SUB_UNDO; }
	return n;
}
/* 0410: the other M4U0 larbs' ports, read only when the SPM reports their domain on in PWR_STATUS and PWR_STATUS_2ND. */
static inline bool n2io_larb_powered(uint32_t s1, uint32_t s2, unsigned bit) { return ((s1 & s2) >> bit) & 1u; }
/* number of powered larbs with an MMU_EN port (en[i] = MMU_EN bit mask of larb i, read only if powered[i]) */
static inline int
n2io_foreign_count(const uint32_t *en, const bool *powered, unsigned n)
{
	int c = 0;
	for (unsigned i = 0; i < n; i++) { if (powered[i] && en[i] != 0) { c++; } }
	return c;
}
/* 0409-PURE-END */
static int  n2io_sub_last;
static bool n2io_sub_prepped;

/* prep: everything n2io_arm_locked does before its first M4U0 write, nothing written to M4U0 (lock held). */
static bool
n2io_sub_prep_locked(void)
{
	if (n2io_sub_prepped) { return true; }
	if (!n2io_map_regs()) { IOLog("NORD2 FB IOMMU: SUB prep: register blocks not mappable\n"); return false; }
	uint32_t g = n2io_cg_gated();
	if (g != 0) { IOLog("NORD2 FB IOMMU: SUB prep: SMI/IOMMU clocks gated (%08x): not ungated by a sub-step\n", g); return false; }
	for (unsigned k = 0; k < 2; k++) {
		for (unsigned p = 0; p < N2IO_LARB_PORTS; p++) {
			uint32_t v = n2io_rd(n2io_larb[k], N2IO_PORT(p));
			if (v & (1u | N2IO_PORT_EXT_MASK)) { IOLog("NORD2 FB IOMMU: SUB prep: larb%u port %u NON_SEC_CON %08x: refused\n", k == 0 ? 1u : 0u, p, v); return false; }
		}
	}
	n2io_step("SUB prep: map the display pool + take the table block (no M4U write)");
	uint64_t pa = sweet_pool_take(SWEET_FB_IOMMU_BLOCK_BYTES);
	if (pa == 0) { IOLog("NORD2 FB IOMMU: SUB prep: no display-pool block for the page tables\n"); return false; }
	volatile uint8_t *va = sweet_pool_kva + (pa - SWEET_FB_HW_POOL_BASE);
	if (!sweet_fb_iommu_pt_init(&n2io_pt, va, pa)) { sweet_pool_give(pa, SWEET_FB_IOMMU_BLOCK_BYTES); IOLog("NORD2 FB IOMMU: SUB prep: block misplaced\n"); return false; }
	sweet_pool_flush((vm_offset_t)va, SWEET_FB_IOMMU_BLOCK_BYTES);
	sweet_fb_iova_init(&n2io_iova);
	n2io_scratch_pa = pa + SWEET_FB_IOMMU_OFF_SCRATCH;
	n2io_ptbase_val = (uint32_t)(n2io_pt.l1_pa & 0xffffff80u) | (uint32_t)((n2io_pt.l1_pa >> 32) & 7u);
	n2io_sub_prepped = true;
	IOLog("NORD2 FB IOMMU: SUB prep: tables pa %#llx PT_BASE value %08x TFRP value %08x (pool mapped, M4U0 untouched)\n", pa, n2io_ptbase_val,
	    (uint32_t)(n2io_scratch_pa & 0xffffff80u));
	return true;
}

/* 0x10: other M4U0 larbs, read only if their power domain is on (a powered-off larb can hang the bus).  SPM 0x10006000: PWR_STATUS
 * 0x16c / PWR_STATUS_2ND 0x170 (clk-mt6885-pg.c); larb bases + domains from mt6885.dts. */
/* returns the number of powered larbs with an MMU_EN port (0 = PT_BASE redirects nobody else), -1 = SPM/larb not mappable.
 * 0410: SPM and larb maps cached (kernel_map, once), no more leaked ml_io_map per call. */
static int
n2io_sub_dump(void)
{
	static const struct { uint8_t larb, bit; uint32_t base; } L[] = {
		{ 5, 15, 0x1600d000u }, { 7, 17, 0x17010000u }, { 11, 13, 0x1582e000u }, { 14, 23, 0x1a002000u }, { 17, 25, 0x1a010000u },
		{ 19, 14, 0x1b10f000u }, { 20, 14, 0x1b00f000u } };
	static volatile uint32_t *spm, *lb[7];
	enum { NL = sizeof(L) / sizeof(L[0]) };
	if (spm == NULL) { spm = n2io_iomap(0x10006000UL, 0x1000, "SPM"); }
	if (spm == NULL) { IOLog("NORD2 FB IOMMU: SUB dump: SPM not mappable\n"); return -1; }
	uint32_t s1 = n2io_rd(spm, 0x16c), s2 = n2io_rd(spm, 0x170), en[NL] = { 0 };
	bool pw[NL] = { false };
	IOLog("NORD2 FB IOMMU: SUB dump: PWR_STATUS %08x PWR_STATUS_2ND %08x\n", s1, s2);
	for (unsigned i = 0; i < NL; i++) {
		if (!n2io_larb_powered(s1, s2, L[i].bit)) { IOLog("NORD2 FB IOMMU: SUB dump: larb%u power domain off: not read\n", L[i].larb); continue; }
		if (lb[i] == NULL) { lb[i] = n2io_iomap(L[i].base, 0x1000, "other M4U0 larb"); }
		if (lb[i] == NULL) { return -1; }
		pw[i] = true;
		volatile uint32_t *b = lb[i];
		uint32_t v[32];
		for (unsigned p = 0; p < 32; p++) { v[p] = n2io_rd(b, N2IO_PORT(p)); if (v[p] & 1u) { en[i] |= 1u << p; } }
		IOLog("NORD2 FB IOMMU: SUB dump: larb%u MMU_EN ports %08x | p0-7 %x %x %x %x %x %x %x %x p8-15 %x %x %x %x %x %x %x %x\n", L[i].larb, en[i],
		    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]);
	}
	int f = n2io_foreign_count(en, pw, NL);
	IOLog("NORD2 FB IOMMU: SUB dump: %d powered larb(s) with MMU_EN ports%s\n", f, f ? ": PT_BASE refused" : "");
	return f;
}

static int
n2io_sysctl_sub SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = n2io_sub_last, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (!n2io_lock_init()) { return ENOMEM; }
	IOLog("NORD2 FB IOMMU: SUB %#x requested (state %u, mode %d, prepped %u)\n", (unsigned)v, n2io_state, n2io_mode, n2io_sub_prepped);
	IOSleep(200);
	IOLockLock(n2io_lock);
	n2io_sub_last = v;
	uint8_t act[6];
	int n = v < 0 ? -22 : n2io_sub_plan((uint32_t)v, n2io_state != 0, n2io_sub_prepped, act);
	if (n < 0) { err = -n; IOLog("NORD2 FB IOMMU: SUB %#x REFUSED (%s)\n", (unsigned)v, n == -16 ? "already armed" : "bad bits"); }
	n2io_stepwise = true;
	for (int i = 0; err == 0 && i < n; i++) {
		switch (act[i]) {
		case N2IO_SUB_DUMP: n2io_sub_dump(); break;
		case N2IO_SUB_PREP: if (!n2io_sub_prep_locked()) { err = EPERM; } break;
		case N2IO_SUB_PTBASE:
			if (n2io_sub_dump() != 0) { err = EPERM; IOLog("NORD2 FB IOMMU: SUB PT_BASE refused: another M4U0 master translates or its larb is not readable\n"); break; }      /* 0410 */
			n2io_step("SUB write M4U0 PT_BASE");
			n2io_wr(n2io_m4u, N2IO_PT_BASE, n2io_ptbase_val); break;
		case N2IO_SUB_TFRP:
			n2io_step("SUB write M4U0 TFRP");
			n2io_wr(n2io_m4u, N2IO_TFRP, (uint32_t)(n2io_scratch_pa & 0xffffff80u)); break;
		case N2IO_SUB_TLB:
			n2io_step("SUB M4U0 TLB invalidate all");
			n2io_tlb_all(); break;
		case N2IO_SUB_UNDO:
			if (!n2io_map_regs()) { err = EPERM; break; }
			n2io_step("SUB undo: M4U0 PT_BASE 0, TFRP 0, TLB invalidate all");
			n2io_wr(n2io_m4u, N2IO_PT_BASE, 0); n2io_wr(n2io_m4u, N2IO_TFRP, 0); n2io_tlb_all(); break;
		}
	}
	n2io_stepwise = false;
	if (n >= 0) {
		bool m = n2io_m4u != NULL;
		IOLog("NORD2 FB IOMMU: SUB %#x %s: PT_BASE %08x TFRP %08x INVLID_SEL %08x fault_st1 %08x l2 %08x\n", (unsigned)v, err ? "FAILED" : "done",
		    m ? n2io_rd(n2io_m4u, N2IO_PT_BASE) : 0u, m ? n2io_rd(n2io_m4u, N2IO_TFRP) : 0u, m ? n2io_rd(n2io_m4u, N2IO_INVLD_SEL) : 0u,
		    m ? n2io_rd(n2io_m4u, N2IO_FAULT_ST1) : 0u, m ? n2io_rd(n2io_m4u, N2IO_L2_FAULT_ST) : 0u);
	}
	IOLockUnlock(n2io_lock);
	return err;
}
SYSCTL_PROC(_kern, OID_AUTO, n2fbiommu_sub, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2io_sysctl_sub, "I", "0409: stage-1 sub-steps 1 prep 2 PT_BASE 4 TFRP 8 TLB 16 dump 128 undo");

/* ---------------------------------------------------------------- 0411 default-on: armed at boot by a thread_call
 * Arming = the 0401/0403/0410 sequence (pool table block, 0x10 check of the other M4U0 larbs, PT_BASE, TFRP, fault interrupt status bits
 * in INT_MAIN with the GIC line masked = latched + polled per frame, TLB invalidate), every write logged first + 200 ms (stepwise).  Ports:
 * switched by the 0401 gate between transfers when a frame programs an IOVA (after overlay init; 0408 lets init accept them).  Failure =
 * REFUSED for the boot, kern.n2fbiommu 0: the pool (0394, default on since 0411) keeps the overlay composing physically. */
static thread_call_t n2io_boot_tc;
/* 0470: armed 3 s after the framebuffer's start (was 20: ~440 frames HELD on the boot screen).  The 20 s kept clear of the SCP/io_submap
 * mapping window, closed since 0410 (unmappable kernel_map register maps; powered M4U0 larbs checked before PT_BASE).  The fire waits
 * (250 ms polls, no M4U0 access) until IOCoreSurfaceRoot has started (the pool tables come from it) and the MMSYS SMI/IOMMU clocks
 * read ungated, at most until 20 s after start; then the 0401 path (which ungates them itself) runs as before.  kern.n2fbiommu_delay /
 * boot-arg n2fbiommu_delay = seconds; a write while arming is pending re-times it from now. */
static uint32_t      n2io_boot_delay_s = 3;
static uint64_t      n2io_boot_t0_ms;
static int           n2io_boot_want;      /* 0470: kern.n2fbiommu chosen at boot: restored after a refusal so the retry can arm */
static void
n2io_boot_after_ms(uint32_t ms)
{
	uint64_t dl = 0;
	clock_interval_to_deadline(ms, kMillisecondScale, &dl);
	thread_call_enter_delayed(n2io_boot_tc, dl);
}
static void
n2io_boot_fire(thread_call_param_t, thread_call_param_t)
{
	static bool told;
	bool late = n2io_now_ms() - n2io_boot_t0_ms >= 20000u;
	uint32_t g = (sweet_surface_lock != NULL && n2io_map_regs()) ? n2io_cg_gated() : 0xffffffffu;
	if (!late && (sweet_surface_lock == NULL || g != 0)) {
		if (!told) { told = true; IOLog("NORD2 FB IOMMU: BOOT ARM waits: surface root %s, MMSYS SMI/IOMMU gated %08x\n", sweet_surface_lock != NULL ? "up" : "not started", g); }
		n2io_boot_after_ms(250);
		return;
	}
	IOLog("NORD2 FB IOMMU: BOOT ARM begin (kern.n2fbiommu %d, %llu ms after fb start): each M4U0/larb step below is logged before it runs\n",
	    n2io_mode, n2io_now_ms() - n2io_boot_t0_ms);
	IOSleep(200);
	IOLockLock(n2io_lock);
	bool ok = false;
	if (n2io_mode > 0 && n2io_state == 0) {
		n2io_stepwise = true;
		ok = n2io_arm_locked();
		n2io_stepwise = false;
	}
	if (!ok && n2io_state == 2) { n2io_state = 0; n2io_mode = n2io_boot_want; }      /* 0416: a refusal is not final: retried (0470: in mode) */
	if (!ok && n2io_mode == 0) {      /* 0470: kern.n2fbiommu=0 written meanwhile: stop, frames go the pool-only way */
		n2io_boot_pending = false;
		IOLog("NORD2 FB IOMMU: BOOT ARM off (kern.n2fbiommu 0)\n");
		IOLockUnlock(n2io_lock);
		return;
	}
	if (!ok) {
		static uint32_t tries;
		uint32_t sec = tries < 4 ? (5u << tries) : 60u;
		tries++;
		n2io_boot_after_ms(sec * 1000u);
		IOLog("NORD2 FB IOMMU: BOOT ARM REFUSED (%s): retry %u in %u s, frames held meanwhile (no other path)\n", n2io_why, tries, sec);
		IOLockUnlock(n2io_lock);
		return;
	}
	n2io_boot_pending = false;
	IOLog("NORD2 FB IOMMU: BOOT ARM %s (%s): kern.n2fbiommu %d, irq_status %u; %s\n", ok ? "done" : "REFUSED", n2io_why, n2io_mode, n2io_irq_on,
	    ok ? "IOVA layers from the next frame" : "pool-only overlay composition continues (no CPU copy)");
	IOLockUnlock(n2io_lock);
}

static int
n2io_sysctl_delay SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	uint32_t v = n2io_boot_delay_s;
	int changed = 0, err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v > 600) { return EINVAL; }
	n2io_boot_delay_s = v;
	if (n2io_boot_pending && n2io_boot_tc != NULL) { n2io_boot_after_ms(v * 1000u); }
	return 0;
}
SYSCTL_PROC(_kern, OID_AUTO, n2fbiommu_delay, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, n2io_sysctl_delay, "IU", "0470: boot arming delay s (default 3)");

extern "C" void
sweet_fb_iommu_boot(void)
{
	static bool once;
	if (once) { return; }
	once = true;
	int v = 0; uint32_t d = 0;
	bool have = PE_parse_boot_argn("n2fbiommu", &v, sizeof(v));
	n2io_mode = sweet_fb_iommu_boot_mode(have, v);
	n2io_boot_want = n2io_mode;
	if (PE_parse_boot_argn("n2fbiommu_delay", &d, sizeof(d)) && d <= 600) { n2io_boot_delay_s = d; }
	if (n2io_mode == 0 || !n2io_lock_init()) { n2io_mode = 0; IOLog("NORD2 FB IOMMU: BOOT ARM off (kern.n2fbiommu 0)\n"); return; }
	n2io_boot_tc = thread_call_allocate(n2io_boot_fire, NULL);
	if (n2io_boot_tc == NULL) { n2io_mode = 0; IOLog("NORD2 FB IOMMU: BOOT ARM off (no thread_call)\n"); return; }
	n2io_boot_pending = true;
	n2io_boot_t0_ms = n2io_now_ms();
	n2io_boot_after_ms(n2io_boot_delay_s * 1000u);
	IOLog("NORD2 FB IOMMU: BOOT ARM scheduled in %u s (kern.n2fbiommu %d; kern.n2fbiommu_delay / boot-args n2fbiommu=0 off, n2fbiommu_delay=S)\n", n2io_boot_delay_s, n2io_mode);
}


#endif /* SWEET_FB_IOMMU_HW_H */
