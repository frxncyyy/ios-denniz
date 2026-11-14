/*
 * 0401: display IOMMU (MTK M4U0, v7s short-descriptor tables) for the OVL layers -- PURE part (stdint only, host-tested).
 *
 * The OVL ADDR registers are 32 bits and, with the larb port's MMU_EN = 0 (how LK leaves them), physical: only the display pool
 * (0394, 0xC0000000+256 MiB) and the framebuffer carve-out are readable.  With MMU_EN = 1 on the OVL*_2L ports every address the
 * port issues is an IOVA translated by M4U0 through ONE page table (domain 0).  This header builds that table:
 *
 *   IOVA 0x76e00000..0x78c00000  identity 1 MiB sections  (framebuffer carve-out: panel buffers, test slot)     -> mode A unchanged
 *   IOVA 0xC0000000..0xE0000000  identity 1 MiB sections  (display pool 0394 + camera/JPEG/GNSS buffers)        -> mode A unchanged
 *   IOVA 0x90000000..0xB0000000  the WINDOW: 4 KiB pages of any IOSurface (scattered, PA up to 34 bits)        -> mode B
 *   anything else                invalid (translation fault -> IVRP page + fault status -> auto-disable)
 *
 * so turning translation on or off never changes what a physically-programmed layer shows, and the ports can be switched while
 * a pool/panel layer is programmed.  Tables (L1 64 KiB = 16384 entries because Linux runs this M4U with 34-bit IOVAs, mt6893_defconfig
 * CONFIG_MTK_IOMMU_PGTABLE_EXT=34; the 4 GiB we use are its first 4096 entries) and the L2 tables of the window live in ONE block of
 * the display pool: below 4 GiB (v7s L1 table pointers carry no PA[33:32]; Linux puts L2 in DMA32 too) and write-combined (the walker
 * needs no cache maintenance).
 *
 * PTE layout = drivers/iommu/io-pgtable-arm-v7s.c with CONFIG_MTK_IOMMU_V2, quirks ARM_NS | NO_PERMS | ARM_MTK_4GB [V-code; quirk set
 * mtk_iommu_v2.c:1745-1760, work/NOTES.md "0401 display IOMMU"]:
 *   small page  PA[31:12] | PA32<<9 | PA33<<4 | nG(bit 11) | TEX=1(bit 6, normal non-cacheable) | 0b10
 *   section     PA[31:20] | NS(bit 19) | nG(bit 17) | TEX=1(bit 12) | 0b10
 *   L1 -> L2    L2PA[31:10] | NS_TABLE(bit 3) | 0b01
 */
#ifndef SWEET_FB_IOMMU_H
#define SWEET_FB_IOMMU_H

#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------- address plan */
#define SWEET_FB_IOMMU_WIN_BASE         0x90000000u
#define SWEET_FB_IOMMU_WIN_SIZE         0x20000000u     /* 512 MiB: ~46 full-screen surfaces + guards */
#define SWEET_FB_IOMMU_ID_FB_BASE       0x76e00000u     /* 1 MiB aligned cover of 0x76e10000..0x78c00000 */
#define SWEET_FB_IOMMU_ID_FB_END        0x78c00000u
#define SWEET_FB_IOMMU_ID_LOW_BASE      0xC0000000u     /* pool (0394) + 0xD0000000.. camera/JPEG/GNSS: a non-display port that had */
#define SWEET_FB_IOMMU_ID_LOW_END       0xE0000000u     /* MMU_EN set would still reach its physical buffers through this table */
#define SWEET_FB_IOMMU_GRANULE          0x10000u        /* IOVA allocation unit */
#define SWEET_FB_IOMMU_NGRAN            (SWEET_FB_IOMMU_WIN_SIZE / SWEET_FB_IOMMU_GRANULE)      /* 8192 */
#define SWEET_FB_IOMMU_GUARD_GRAN       1u              /* unmapped granule after each mapping: an overrun faults, never reads a neighbour */
#define SWEET_FB_IOMMU_PA_LIMIT         (1ull << 34)

/* ---------------------------------------------------------------- v7s geometry */
#define SWEET_FB_IOMMU_L1_ENTRIES       16384u          /* 34-bit IOVA: IOVA[33:20] */
#define SWEET_FB_IOMMU_L1_BYTES         (SWEET_FB_IOMMU_L1_ENTRIES * 4u)        /* 64 KiB, 64 KiB aligned (pool granule) */
#define SWEET_FB_IOMMU_L2_ENTRIES       256u
#define SWEET_FB_IOMMU_L2_BYTES         1024u
#define SWEET_FB_IOMMU_NL2              (SWEET_FB_IOMMU_WIN_SIZE >> 20)         /* 512 */
#define SWEET_FB_IOMMU_OFF_L2           SWEET_FB_IOMMU_L1_BYTES
#define SWEET_FB_IOMMU_OFF_SCRATCH      (SWEET_FB_IOMMU_OFF_L2 + SWEET_FB_IOMMU_NL2 * SWEET_FB_IOMMU_L2_BYTES)    /* IVRP / dummy page */
#define SWEET_FB_IOMMU_BLOCK_BYTES      (SWEET_FB_IOMMU_OFF_SCRATCH + 0x1000u)  /* 580 KiB of pool */

#define SWEET_FB_IOMMU_T_FAULT          0u
#define SWEET_FB_IOMMU_T_TABLE          1u
#define SWEET_FB_IOMMU_T_PAGE           2u              /* small page (L2) / section (L1) */
#define SWEET_FB_IOMMU_PG_PA32          (1u << 9)
#define SWEET_FB_IOMMU_PG_PA33          (1u << 4)
#define SWEET_FB_IOMMU_PG_ATTR          ((1u << 11) | (1u << 6))                /* nG | TEX=1 (attr block << 4) */
#define SWEET_FB_IOMMU_SEC_ATTR         ((1u << 19) | (1u << 17) | (1u << 12))  /* NS | nG | TEX=1 (attr block << 10) */
#define SWEET_FB_IOMMU_TBL_ATTR         (1u << 3)                               /* NS_TABLE */
#define SWEET_FB_IOMMU_BAD_PA           (~0ull)

static inline uint32_t
sweet_fb_iommu_pte_page(uint64_t pa)
{
	return (uint32_t)(pa & 0xfffff000u) | ((pa >> 32) & 1u ? SWEET_FB_IOMMU_PG_PA32 : 0u) | ((pa >> 33) & 1u ? SWEET_FB_IOMMU_PG_PA33 : 0u) |
	       SWEET_FB_IOMMU_PG_ATTR | SWEET_FB_IOMMU_T_PAGE;
}

static inline uint32_t
sweet_fb_iommu_pte_section(uint32_t pa)
{
	return (pa & 0xfff00000u) | SWEET_FB_IOMMU_SEC_ATTR | SWEET_FB_IOMMU_T_PAGE;
}

static inline uint32_t
sweet_fb_iommu_pte_table(uint32_t l2pa)
{
	return (l2pa & 0xfffffc00u) | SWEET_FB_IOMMU_TBL_ATTR | SWEET_FB_IOMMU_T_TABLE;
}

static inline bool
sweet_fb_iommu_in_window(uint64_t a, uint64_t end)
{
	return end > a && a >= SWEET_FB_IOMMU_WIN_BASE && end <= (uint64_t)SWEET_FB_IOMMU_WIN_BASE + SWEET_FB_IOMMU_WIN_SIZE;
}

/* ---------------------------------------------------------------- tables */
struct sweet_fb_iommu_pt {
	volatile uint32_t *l1;          /* SWEET_FB_IOMMU_L1_ENTRIES */
	volatile uint32_t *l2;          /* SWEET_FB_IOMMU_NL2 tables of 256, contiguous after l1 */
	uint64_t l1_pa, l2_pa;          /* both < 4 GiB */
	uint32_t mapped;                /* window pages valid now */
};

/* Lay the tables over one block (va/pa of SWEET_FB_IOMMU_BLOCK_BYTES, pa 64 KiB aligned, < 4 GiB) and fill them: identity sections,
 * window L1 entries pointing at the (zeroed) L2 tables, everything else invalid.  false = block unusable. */
static inline bool
sweet_fb_iommu_pt_init(struct sweet_fb_iommu_pt *pt, volatile void *va, uint64_t pa)
{
	if (!va || (pa & 0xffffu) != 0 || pa + SWEET_FB_IOMMU_BLOCK_BYTES > 0x100000000ull) { return false; }
	pt->l1 = (volatile uint32_t *)va;
	pt->l2 = (volatile uint32_t *)(volatile void *)((volatile uint8_t *)va + SWEET_FB_IOMMU_OFF_L2);
	pt->l1_pa = pa; pt->l2_pa = pa + SWEET_FB_IOMMU_OFF_L2; pt->mapped = 0;
	for (uint32_t i = 0; i < SWEET_FB_IOMMU_NL2 * SWEET_FB_IOMMU_L2_ENTRIES; i++) { pt->l2[i] = 0; }
	for (uint32_t i = 0; i < SWEET_FB_IOMMU_L1_ENTRIES; i++) {
		uint32_t a = i << 20, e = 0;
		if (i < 4096u && ((a >= SWEET_FB_IOMMU_ID_FB_BASE && a < SWEET_FB_IOMMU_ID_FB_END) || (a >= SWEET_FB_IOMMU_ID_LOW_BASE && a < SWEET_FB_IOMMU_ID_LOW_END))) {
			e = sweet_fb_iommu_pte_section(a);
		} else if (i < 4096u && a >= SWEET_FB_IOMMU_WIN_BASE && a - SWEET_FB_IOMMU_WIN_BASE < SWEET_FB_IOMMU_WIN_SIZE) {
			e = sweet_fb_iommu_pte_table((uint32_t)(pt->l2_pa + ((a - SWEET_FB_IOMMU_WIN_BASE) >> 20) * SWEET_FB_IOMMU_L2_BYTES));
		}
		pt->l1[i] = e;
	}
	return true;
}

static inline volatile uint32_t *
sweet_fb_iommu_l2e(struct sweet_fb_iommu_pt *pt, uint32_t iova)
{
	return &pt->l2[(iova - SWEET_FB_IOMMU_WIN_BASE) >> 12];
}

/* One window page.  false (nothing written) = iova outside the window / unaligned, pa unaligned or >= 2^34, or already mapped. */
static inline bool
sweet_fb_iommu_map_page(struct sweet_fb_iommu_pt *pt, uint32_t iova, uint64_t pa)
{
	if ((iova & 0xfffu) != 0 || (pa & 0xfffu) != 0 || pa >= SWEET_FB_IOMMU_PA_LIMIT || !sweet_fb_iommu_in_window(iova, (uint64_t)iova + 0x1000u)) { return false; }
	volatile uint32_t *e = sweet_fb_iommu_l2e(pt, iova);
	if (*e != 0) { return false; }
	*e = sweet_fb_iommu_pte_page(pa);
	pt->mapped++;
	return true;
}

static inline void
sweet_fb_iommu_unmap(struct sweet_fb_iommu_pt *pt, uint32_t iova, uint32_t pages)
{
	for (uint32_t i = 0; i < pages; i++) {
		uint64_t a = (uint64_t)iova + (uint64_t)i * 0x1000u;
		if (!sweet_fb_iommu_in_window(a, a + 0x1000u)) { break; }
		volatile uint32_t *e = sweet_fb_iommu_l2e(pt, (uint32_t)a);
		if (*e != 0) { *e = 0; pt->mapped--; }
	}
}

/* What the hardware would do for iova: the PA, or SWEET_FB_IOMMU_BAD_PA (translation fault).  Follows table pointers by PA (only the
 * L2 block of this pt is reachable; any other pointer is reported as a fault: a corrupted table). */
static inline uint64_t
sweet_fb_iommu_walk(const struct sweet_fb_iommu_pt *pt, uint64_t iova)
{
	if (iova >= (1ull << 34)) { return SWEET_FB_IOMMU_BAD_PA; }
	uint32_t e1 = pt->l1[(iova >> 20) & (SWEET_FB_IOMMU_L1_ENTRIES - 1u)];
	switch (e1 & 3u) {
	case SWEET_FB_IOMMU_T_PAGE: {
		if (e1 & (1u << 18)) { return SWEET_FB_IOMMU_BAD_PA; }          /* supersection: never written */
		uint64_t pa = (e1 & 0xfff00000u) | (iova & 0xfffffu);
		if (e1 & SWEET_FB_IOMMU_PG_PA32) { pa |= 1ull << 32; }
		if (e1 & SWEET_FB_IOMMU_PG_PA33) { pa |= 1ull << 33; }
		return pa;
	}
	case SWEET_FB_IOMMU_T_TABLE: {
		uint64_t l2pa = e1 & 0xfffffc00u;
		if (l2pa < pt->l2_pa || l2pa >= pt->l2_pa + (uint64_t)SWEET_FB_IOMMU_NL2 * SWEET_FB_IOMMU_L2_BYTES) { return SWEET_FB_IOMMU_BAD_PA; }
		uint32_t e2 = pt->l2[(l2pa - pt->l2_pa) / 4u + ((iova >> 12) & 0xffu)];
		if ((e2 & 2u) == 0) { return SWEET_FB_IOMMU_BAD_PA; }           /* invalid or large page (never written) */
		uint64_t pa = (e2 & 0xfffff000u) | (iova & 0xfffu);
		if (e2 & SWEET_FB_IOMMU_PG_PA32) { pa |= 1ull << 32; }
		if (e2 & SWEET_FB_IOMMU_PG_PA33) { pa |= 1ull << 33; }
		return pa;
	}
	default:
		return SWEET_FB_IOMMU_BAD_PA;
	}
}

/* ---------------------------------------------------------------- IOVA allocator (window granules, first fit, guard after each) */
#define SWEET_FB_IOVA_FAIL              0xffffffffu
struct sweet_fb_iova {
	uint64_t used[SWEET_FB_IOMMU_NGRAN / 64];       /* granule taken (incl. guards) */
	uint64_t head[SWEET_FB_IOMMU_NGRAN / 64];       /* first granule of an allocation */
	uint32_t live, granules, fails, bad_free;
};

static inline bool sweet_fb_iova_bit(const uint64_t *m, uint32_t g) { return (m[g >> 6] >> (g & 63u)) & 1u; }
static inline void sweet_fb_iova_set(uint64_t *m, uint32_t g, bool v) { if (v) { m[g >> 6] |= 1ull << (g & 63u); } else { m[g >> 6] &= ~(1ull << (g & 63u)); } }
static inline uint32_t sweet_fb_iova_ngran(uint64_t bytes) { return (uint32_t)((bytes + SWEET_FB_IOMMU_GRANULE - 1u) / SWEET_FB_IOMMU_GRANULE) + SWEET_FB_IOMMU_GUARD_GRAN; }

static inline void
sweet_fb_iova_init(struct sweet_fb_iova *a)
{
	for (uint32_t i = 0; i < SWEET_FB_IOMMU_NGRAN / 64; i++) { a->used[i] = 0; a->head[i] = 0; }
	a->live = a->granules = a->fails = a->bad_free = 0;
}

/* IOVA of bytes (> 0), or SWEET_FB_IOVA_FAIL. */
static inline uint32_t
sweet_fb_iova_alloc(struct sweet_fb_iova *a, uint64_t bytes)
{
	if (bytes == 0 || bytes > SWEET_FB_IOMMU_WIN_SIZE) { a->fails++; return SWEET_FB_IOVA_FAIL; }
	uint32_t n = sweet_fb_iova_ngran(bytes), run = 0;
	for (uint32_t g = 0; g < SWEET_FB_IOMMU_NGRAN; g++) {
		if (sweet_fb_iova_bit(a->used, g)) { run = 0; continue; }
		if (++run == n) {
			uint32_t g0 = g + 1u - n;
			for (uint32_t k = g0; k <= g; k++) { sweet_fb_iova_set(a->used, k, true); }
			sweet_fb_iova_set(a->head, g0, true);
			a->live++; a->granules += n;
			return SWEET_FB_IOMMU_WIN_BASE + g0 * SWEET_FB_IOMMU_GRANULE;
		}
	}
	a->fails++;
	return SWEET_FB_IOVA_FAIL;
}

/* false (nothing changed, bad_free counted) unless [iova, bytes) is exactly one live allocation. */
static inline bool
sweet_fb_iova_free(struct sweet_fb_iova *a, uint32_t iova, uint64_t bytes)
{
	uint32_t n = sweet_fb_iova_ngran(bytes);
	if (bytes == 0 || iova < SWEET_FB_IOMMU_WIN_BASE || ((iova - SWEET_FB_IOMMU_WIN_BASE) % SWEET_FB_IOMMU_GRANULE) != 0) { a->bad_free++; return false; }
	uint32_t g0 = (iova - SWEET_FB_IOMMU_WIN_BASE) / SWEET_FB_IOMMU_GRANULE;
	if (g0 + n > SWEET_FB_IOMMU_NGRAN || !sweet_fb_iova_bit(a->head, g0)) { a->bad_free++; return false; }
	for (uint32_t k = g0; k < g0 + n; k++) {
		if (!sweet_fb_iova_bit(a->used, k) || (k > g0 && sweet_fb_iova_bit(a->head, k))) { a->bad_free++; return false; }
	}
	if (g0 + n < SWEET_FB_IOMMU_NGRAN && sweet_fb_iova_bit(a->used, g0 + n) && !sweet_fb_iova_bit(a->head, g0 + n)) { a->bad_free++; return false; }  /* longer allocation */
	for (uint32_t k = g0; k < g0 + n; k++) { sweet_fb_iova_set(a->used, k, false); }
	sweet_fb_iova_set(a->head, g0, false);
	a->live--; a->granules -= n;
	return true;
}

/* ---------------------------------------------------------------- per-surface mapping cache */
#define SWEET_FB_IOMMU_CACHE            96u      /* 0416: every surface goes through the IOMMU (was 48) */
struct sweet_fb_iommu_ent {
	const void *obj;                /* SweetSurface (no retain: SweetSurface::free unmaps) */
	uint32_t id, iova, bytes, pages;
	uint32_t cpu_seq;               /* CPU-write count (IOSurfaceUnlock) at the last cache clean of the pixels */
	uint64_t stamp;                 /* planning generation of the last lookup */
};

/* The OVL ADDR values programmed now (2 pipes x 2 layers), from the register lists the framebuffer writes between transfers. */
/* 0415: per pipe the ADDR of slots 0..5 (OVL*_2L layers 0/1, cascaded 4L layers 0..3); 0 = slot off */
#define SWEET_FB_IOMMU_PSLOTS  6u
struct sweet_fb_iommu_prog { uint32_t addr[2][SWEET_FB_IOMMU_PSLOTS]; };

static inline bool
sweet_fb_iommu_ent_programmed(const struct sweet_fb_iommu_ent *e, const struct sweet_fb_iommu_prog *p)
{
	for (unsigned k = 0; k < 2; k++) {
		for (unsigned i = 0; i < SWEET_FB_IOMMU_PSLOTS; i++) {
			uint32_t a = p->addr[k][i];
			if (e->obj && a >= e->iova && (uint64_t)a < (uint64_t)e->iova + e->bytes) { return true; }
		}
	}
	return false;
}

/* Entry to evict to make room: the least recently looked-up one that the OVL cannot read (no programmed ADDR inside it) and that was
 * not looked up in the current planning generation (a frame being planned may still use it).  -1 = none may go. */
static inline int
sweet_fb_iommu_victim(const struct sweet_fb_iommu_ent *e, unsigned n, const struct sweet_fb_iommu_prog *p, uint64_t gen)
{
	int best = -1;
	for (unsigned i = 0; i < n; i++) {
		if (!e[i].obj || e[i].stamp >= gen || sweet_fb_iommu_ent_programmed(&e[i], p)) { continue; }
		if (best < 0 || e[i].stamp < e[best].stamp) { best = (int)i; }
	}
	return best;
}

/* Gate decision before a register list is written (between transfers).  has_iova: the list programs a window address. */
enum { SWEET_FB_IOMMU_G_KEEP = 0, SWEET_FB_IOMMU_G_ENABLE, SWEET_FB_IOMMU_G_DISABLE, SWEET_FB_IOMMU_G_REFUSE };
static inline int
sweet_fb_iommu_gate_decide(bool has_iova, bool ports_on, bool armed, bool other_pipe_iova)
{
	if (has_iova) { return !armed ? SWEET_FB_IOMMU_G_REFUSE : (ports_on ? SWEET_FB_IOMMU_G_KEEP : SWEET_FB_IOMMU_G_ENABLE); }
	if (ports_on && !armed && !other_pipe_iova) { return SWEET_FB_IOMMU_G_DISABLE; }     /* identity maps: safe with panel/pool layers */
	return SWEET_FB_IOMMU_G_KEEP;
}

/* 0408: overlay init check R11 with the display IOMMU.  v = NON_SEC_CON of larb0 p5, larb0 p6, larb1 p5, larb1 p6 (OVL1, OVL1_2L,
 * OVL0, OVL0_2L data ports); owned = OVL*_2L ports whose MMU_EN the armed display IOMMU itself set (bit0 larb0 p6, bit1 larb1 p6; 0 when
 * not armed).  Accepted: every port physical, or an OVL*_2L port translated by the armed IOMMU (its table maps the panel buffers and the
 * pool 1:1, so the CPU-path base state reads the same pixels).  Refused: MMU_EN on a 4L port (p5), any IOVA extension bits [15:8], or an
 * MMU_EN the IOMMU code did not set (someone else's table: the addresses would be foreign IOVAs). */
static inline bool
sweet_fb_iommu_init_ports_ok(const uint32_t v[4], unsigned owned)
{
	/* 0415: owned bit2 = larb0 p5, bit3 = larb1 p5 (the cascaded 4L OVL ports, switched together with p6 since 0415) */
	if (((v[0] | v[1] | v[2] | v[3]) & 0xff00u) != 0) { return false; }
	if ((v[0] & 1u) && !(owned & 4u)) { return false; }
	if ((v[1] & 1u) && !(owned & 1u)) { return false; }
	if ((v[2] & 1u) && !(owned & 8u)) { return false; }
	if ((v[3] & 1u) && !(owned & 2u)) { return false; }
	return true;
}

/* 0415: "another slot already shows an IOVA": any window address in prog except pipe `pipe` slots [s0, s0+ns) (the ones being written) */
static inline bool
sweet_fb_iommu_prog_other(const struct sweet_fb_iommu_prog *p, unsigned pipe, unsigned s0, unsigned ns)
{
	for (unsigned k = 0; k < 2; k++) {
		for (unsigned i = 0; i < SWEET_FB_IOMMU_PSLOTS; i++) {
			if (k == pipe && i >= s0 && i < s0 + ns) { continue; }
			uint32_t a = p->addr[k][i];
			if (sweet_fb_iommu_in_window(a, (uint64_t)a + 1u)) { return true; }
		}
	}
	return false;
}

/* 0411: boot default of kern.n2fbiommu: 3 (every eligible layer through an IOVA) unless the boot-arg n2fbiommu=0..3 says otherwise. */
static inline int
sweet_fb_iommu_boot_mode(bool bootarg_present, int bootarg)
{
	if (!bootarg_present) { return 3; }
	return bootarg < 0 ? 0 : (bootarg > 3 ? 3 : bootarg);
}

/* ---------------------------------------------------------------- kernel API (SweetSurface.cpp, 0401) */
#ifdef __cplusplus
extern "C" {
#endif
/* kern.n2fbiommu: 0 off (pool only), 1 map-only dry run, 2 IOVA layers in single-layer frames, 3 all frames */
uint32_t sweet_fb_iommu_mode(void);
/* IOVA/length of a surface for the overlay planner (pa/len of struct sweet_fb_hw_in); false = not through the IOMMU (CPU path). */
bool sweet_fb_iommu_layer(void *surface, uint64_t *iova, uint64_t *len);
/* Before an OVL register list of pipe k is written (between transfers): addr0/addr1 = the layer 0/1 ADDR values in the list
 * (0xffffffff = not in the list).  Switches the OVL ports' MMU_EN as needed; false = the list must not be written. */
bool sweet_fb_iommu_gate(unsigned pipe, uint32_t addr0, uint32_t addr1);
/* 0415: the same for the cascaded 4L block of pipe k: a[j] = layer j ADDR in the list, 0 = layer turned off, 0xffffffff = not in the list. */
bool sweet_fb_iommu_gate4(unsigned pipe, const uint32_t a[4]);
/* After a HW frame (FRAME_DONE or timeout): true = an M4U translation fault was seen (logged, IOMMU auto-disabled). */
bool sweet_fb_iommu_frame_done(void);
/* 0408: OVL*_2L ports whose MMU_EN the armed display IOMMU set (bit0 larb0 p6, bit1 larb1 p6); 0 when not armed. */
unsigned sweet_fb_iommu_owned_ports(void);
/* 0411: called once by the framebuffer's start: the display IOMMU is armed by a thread_call n2fbiommu_delay s later (default 20). */
void sweet_fb_iommu_boot(void);
#ifdef __cplusplus
}

/* The ADDR values of a framebuffer register list (struct sweet_fb_hw_reg: off, val, mask). */
template <typename R>
static inline bool
sweet_fb_iommu_gate_list(unsigned pipe, const R *r, unsigned n)
{
	uint32_t a[2] = { 0xffffffffu, 0xffffffffu };
	for (unsigned i = 0; i < n; i++) {
		if (r[i].off == 0xf40u) { a[0] = r[i].val; }
		if (r[i].off == 0xf60u) { a[1] = r[i].val; }
	}
	return sweet_fb_iommu_gate(pipe, a[0], a[1]);
}

/* 0415: a 4L block list: layer j ADDR at 0xf40 + 0x20 j; RDMA_CTRL (0xc0 + 0x20 j) with bit 0 clear and no ADDR = layer off */
template <typename R>
static inline bool
sweet_fb_iommu_gate_list4(unsigned pipe, const R *r, unsigned n)
{
	uint32_t a[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
	bool off[4] = { false, false, false, false };
	for (unsigned i = 0; i < n; i++) {
		for (unsigned j = 0; j < 4; j++) {
			if (r[i].off == 0xf40u + 0x20u * j) { a[j] = r[i].val; }
			if (r[i].off == 0x0c0u + 0x20u * j && !(r[i].val & 1u)) { off[j] = true; }
		}
	}
	for (unsigned j = 0; j < 4; j++) { if (off[j] && a[j] == 0xffffffffu) { a[j] = 0; } }
	return sweet_fb_iommu_gate4(pipe, a);
}
#endif

#endif /* SWEET_FB_IOMMU_H */
