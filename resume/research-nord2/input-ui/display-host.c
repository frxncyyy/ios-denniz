
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NORD2_FB_PA 0x76e10000ULL
#define NORD2_FB_WIDTH 1080
#define NORD2_FB_HEIGHT 2400
#define NORD2_FB_PITCH 4352
#define NORD2_DSI_INTSTA 12
#define NORD2_DSI_BUSY (1u << 31)
#define NORD2_DSI_FRAME_DONE (1u << 4)
#define NORD2_DSI_START 0
#define NORD2_MUTEX_EN0 32
static uint32_t dsi[1024], mtx[1024], nord2_gui_owned, nord2_console_owner;
static uintptr_t nord2_va_fb, nord2_va_dsi, nord2_va_mutex;
static int enabled, stop;
static bool panicking, acquire_ok, outer_lock, complete;
static uint64_t ticks, daif, freq;
static unsigned barriers;
static uint32_t ovl[4][1024];
static uintptr_t ml_io_map(uint64_t pa, unsigned size) {
    assert(size == 4096);
    const uint64_t addr[4] = {0x14000000,0x14100000,0x14001000,0x14101000};
    for(unsigned i=0;i<4;i++) if(pa==addr[i]) return (uintptr_t)ovl[i];
    assert(0); return 0;
}
static bool panic_active(void) { return panicking; }
static void PE_parse_boot_argn(const char *name, void *value, unsigned size) {
    assert(size == sizeof(int));
    *(int *)value = strcmp(name, "nord2display") == 0 ? enabled : stop;
}
static uint64_t rsr(const char *reg) {
    if (!strcmp(reg,"DAIF")) return daif;
    if (!strcmp(reg,"CNTFRQ_EL0")) return freq;
    assert(!strcmp(reg,"CNTVCT_EL0"));
    if (complete && dsi[0] == 1) dsi[3] = NORD2_DSI_FRAME_DONE;
    return ++ticks;
}
static void wsr(const char *reg, uint64_t value) {
    if (!strcmp(reg,"DAIFSet")) daif |= value;
    else { assert(!strcmp(reg,"DAIF")); daif = value; }
}
#define __builtin_arm_rsr64(r) rsr(r)
#define __builtin_arm_wsr(r,v) wsr(r,v)
#define __builtin_arm_wsr64(r,v) wsr(r,v)
static unsigned nord2_raw_core(void) { return 3; }
static bool nord2_console_acquire(unsigned owner, bool *outer) {
    assert(owner == 4 && (daif & 15) == 15);
    *outer = outer_lock;
    return acquire_ok;
}
static volatile uint32_t *nord2_ovl[4];
static uint32_t nord2_ovl_src[2], nord2_ovl_rdma[2], nord2_ovl_path[2];
static bool nord2_ovl_checked, nord2_ovl_saved, nord2_ovl_removed;

static void
nord2_display_overlay_init(void)
{
	if (nord2_ovl_checked) { return; }
	nord2_ovl_checked = true;
	const uint64_t pa[4] = {0x14000000, 0x14100000, 0x14001000, 0x14101000};
	for (unsigned i = 0; i < 4; i++) {
		nord2_ovl[i] = (volatile uint32_t *)ml_io_map(pa[i], 0x1000);
		if (!nord2_ovl[i]) { return; }
	}
	/* Exact inherited dual-pipe composition from the saved hardware dump.
	 * Keep the 2L GUI buffers and relay path; remove only LK's L1 overlay. */
	for (unsigned i = 0; i < 2; i++) {
		volatile uint32_t *o = nord2_ovl[i], *g = nord2_ovl[i + 2];
		printf("NORD2 DISPLAY: pipe=%u LK src=%#x addr=%#x pitch=%#x GUI src=%#x addr=%#x\n",
		    i, o[0x2c/4], o[0xf60/4], o[0x64/4], g[0x2c/4], g[0xf40/4]);
		if (o[0x2c/4] != 2 || g[0x2c/4] != 1 ||
		    o[0xf60/4] != 0x77806000u + i * 2160 ||
		    g[0xf40/4] != NORD2_FB_PA + i * 2160 ||
		    (o[0x64/4] & 0xffff) != 4320 ||
		    (g[0x44/4] & 0xffff) != NORD2_FB_PITCH ||
		    o[0x58/4] != 0x0960021c || g[0x38/4] != 0x0960021c) {
			printf("NORD2 DISPLAY: unexpected overlay layout; leaving layers unchanged\n");
			return;
		}
		nord2_ovl_src[i] = o[0x2c/4];
		nord2_ovl_rdma[i] = o[0xe0/4];
		nord2_ovl_path[i] = o[0x24/4];
	}
	nord2_ovl_saved = true;
}

static void
nord2_display_overlay_remove(void)
{
	if (!nord2_ovl_saved || nord2_ovl_removed) { return; }
	nord2_ovl_removed = true; /* return path can recover a partial update */
	for (unsigned i = 0; i < 2; i++) {
		volatile uint32_t *o = nord2_ovl[i];
		o[0x24/4] = nord2_ovl_path[i] & ~(1u << 5);
		o[0x2c/4] = nord2_ovl_src[i] & ~2u;
		o[0xe0/4] = 0; /* same L1 RDMA disable as mtk_ovl_layer_off */
	}
	barriers++;
	printf("NORD2 DISPLAY: inherited LK warning overlay disabled on both pipes\n");
}

static void
nord2_display_overlay_restore(void)
{
	if (!nord2_ovl_removed) { return; }
	for (unsigned i = 0; i < 2; i++) {
		volatile uint32_t *o = nord2_ovl[i];
		o[0x24/4] = nord2_ovl_path[i];
		o[0xe0/4] = nord2_ovl_rdma[i];
		o[0x2c/4] = nord2_ovl_src[i];
	}
	barriers++;
	nord2_ovl_removed = false;
}

int
nord2_display_info(uint64_t *phys, uint32_t *width, uint32_t *height, uint32_t *pitch)
{
	int enabled = 0, stop = 0;
	PE_parse_boot_argn("nord2display", &enabled, sizeof(enabled));
	PE_parse_boot_argn("nord2stopat", &stop, sizeof(stop));
	if (enabled != 1 || stop != 23 || !nord2_va_fb || !nord2_va_dsi || !nord2_va_mutex) {
		return 0;
	}
	nord2_display_overlay_init();
	*phys = NORD2_FB_PA;
	*width = NORD2_FB_WIDTH;
	*height = NORD2_FB_HEIGHT;
	*pitch = NORD2_FB_PITCH;
	return 1;
}

/* Drain any console drawing before the first GUI copy. Later logs remain in
 * both buffers but stop modifying pixels; panic output can draw again. */
int
nord2_display_begin(void)
{
	uint64_t daif = __builtin_arm_rsr64("DAIF");
	__builtin_arm_wsr("DAIFSet", 0xf);
	bool outer;
	bool ok = nord2_console_acquire(nord2_raw_core() + 1, &outer);
	if (ok) {
		nord2_gui_owned = 1;
		if (outer) { __atomic_store_n(&nord2_console_owner, 0, __ATOMIC_RELEASE); }
	}
	__builtin_arm_wsr64("DAIF", daif);
	return ok;
}

int
nord2_display_refresh(void)
{
	if (!nord2_gui_owned || panic_active()) { return 0; }
	volatile uint32_t *dsi = (volatile uint32_t *)nord2_va_dsi;
	volatile uint32_t *mtx = (volatile uint32_t *)nord2_va_mutex;
	if (!dsi || !mtx) { return 0; }
	uint64_t freq = __builtin_arm_rsr64("CNTFRQ_EL0");
	if (!freq) { return 0; }
	uint64_t deadline = __builtin_arm_rsr64("CNTVCT_EL0") + freq / 100;
	while (dsi[NORD2_DSI_INTSTA / 4] & NORD2_DSI_BUSY) {
		if (__builtin_arm_rsr64("CNTVCT_EL0") >= deadline) { return 0; }
	}
	nord2_display_overlay_remove();
	/* MT6893 INTSTA is write-zero-to-clear (mtk_dsi.c IRQ handler). */
	dsi[NORD2_DSI_INTSTA / 4] = ~NORD2_DSI_FRAME_DONE;
	mtx[NORD2_MUTEX_EN0 / 4] = 1;
	dsi[NORD2_DSI_START / 4] = 0;
	barriers++;
	dsi[NORD2_DSI_START / 4] = 1;
	deadline = __builtin_arm_rsr64("CNTVCT_EL0") + freq / 50;
	while (!(dsi[NORD2_DSI_INTSTA / 4] & NORD2_DSI_FRAME_DONE)) {
		if (__builtin_arm_rsr64("CNTVCT_EL0") >= deadline) { return 0; }
	}
	dsi[NORD2_DSI_INTSTA / 4] = ~NORD2_DSI_FRAME_DONE;
	return 1;
}


int main(void) {
    uint64_t phys=0; uint32_t w=0,h=0,pitch=0;
    enabled=1; stop=23; nord2_va_fb=1;
    nord2_va_dsi=(uintptr_t)dsi; nord2_va_mutex=(uintptr_t)mtx;
    assert(nord2_display_info(&phys,&w,&h,&pitch));
    assert(phys==0x76e10000 && w==1080 && h==2400 && pitch==4352);
    for (stop=0;stop<26;stop++) if(stop!=23)
        assert(!nord2_display_info(&phys,&w,&h,&pitch));
    stop=23; enabled=0; assert(!nord2_display_info(&phys,&w,&h,&pitch));
    enabled=1; nord2_va_fb=0; assert(!nord2_display_info(&phys,&w,&h,&pitch));
    nord2_va_fb=1; nord2_va_dsi=0; assert(!nord2_display_info(&phys,&w,&h,&pitch));
    nord2_va_dsi=(uintptr_t)dsi;
    daif=0x80; acquire_ok=false;
    assert(!nord2_display_begin() && !nord2_gui_owned && daif==0x80);
    acquire_ok=true; outer_lock=true; nord2_console_owner=4;
    assert(nord2_display_begin() && nord2_gui_owned && !nord2_console_owner && daif==0x80);
    nord2_gui_owned=0; assert(!nord2_display_refresh() && !ticks && !barriers);
    nord2_gui_owned=1; panicking=true;
    assert(!nord2_display_refresh() && !ticks && !barriers);
    panicking=false; freq=0; assert(!nord2_display_refresh() && !barriers);
    freq=10000; dsi[3]=NORD2_DSI_BUSY;
    assert(!nord2_display_refresh() && !dsi[0] && !mtx[8] && ticks<103 && !barriers);
    ticks=0; dsi[3]=0; complete=false;
    assert(!nord2_display_refresh() && dsi[0]==1 && mtx[8]==1 && ticks<205 && barriers==1);
    ticks=0; dsi[0]=dsi[3]=0; complete=true;
    assert(nord2_display_refresh() && dsi[3]==~NORD2_DSI_FRAME_DONE && barriers==2);
    assert(ticks<10);
    /* Only the exact known composition is eligible; both pipes must match. */
    for(unsigned i=0;i<2;i++) {
        ovl[i][0x2c/4]=2; ovl[i+2][0x2c/4]=1;
        ovl[i][0xf60/4]=0x77806000+i*2160;
        ovl[i+2][0xf40/4]=0x76e10000+i*2160;
        ovl[i][0x64/4]=4320; ovl[i+2][0x44/4]=4352;
        ovl[i][0x58/4]=ovl[i+2][0x38/4]=0x0960021c;
        ovl[i][0xe0/4]=1; ovl[i][0x24/4]=0xabcdef;
    }
    nord2_ovl_checked=false; ovl[1][0xf60/4]++;
    nord2_display_overlay_init();
    assert(!nord2_ovl_saved); nord2_display_overlay_remove();
    assert(ovl[0][0x2c/4]==2 && ovl[1][0x2c/4]==2);
    ovl[1][0xf60/4]--; nord2_ovl_checked=false;
    nord2_display_overlay_init(); assert(nord2_ovl_saved);
    uint32_t saved[4][1024]; memcpy(saved,ovl,sizeof(saved));
    nord2_display_overlay_remove(); assert(nord2_ovl_removed);
    for(unsigned i=0;i<2;i++) {
        assert(ovl[i][0x2c/4]==0 && ovl[i][0xe0/4]==0);
        assert(ovl[i][0x24/4]==(0xabcdef & ~(1<<5)));
        assert(!memcmp(saved[i+2],ovl[i+2],sizeof(ovl[0])));
    }
    nord2_display_overlay_restore();
    assert(!nord2_ovl_removed && !memcmp(saved,ovl,sizeof(saved)));
    puts("PASS: display opt-in, mapping guards, console handoff, panic exclusion,"
         " busy/FRAME_DONE timeouts, W0C, two-pipe overlay guards/removal/restore");
}
