/* SPDX-License-Identifier: MIT
 * gnss-20261114/test/test_pipe.c -- host test of the 0580/0581 nord2_gpsdl.inc (the exact file of c/ or b/), HOLD pipe part:
 * kernel shims + a fake conn_infra / GPS bus that records every register access.  run.sh builds it twice (c/ = 0581, b/ = 0580).
 * Checks (0581): gate = Wi-Fi guard + gps_top_off ACTIVE before any GPS-bus access; refusals transient (EAGAIN), pipe down
 * after 3000; quiet TX; observe-only bit; heartbeat; default-on start(), kill switch sysctl kern.n2gnss bit 0, observe bit 1. */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include "nord2_gnss_pwr.inc"
#define N2GD_HOST 1
#define __unused __attribute__((unused))
typedef int dev_t; typedef char *caddr_t; struct proc;
typedef long user_ssize_t;
typedef uintptr_t vm_offset_t;
struct uio { uint8_t *p; long resid; };
static long uio_resid(struct uio *u) { return u->resid; }
static int g_rdir;
static int uiomove(caddr_t b, int n, struct uio *u) { if (n > u->resid) n = (int)u->resid; if (g_rdir) memcpy(u->p, b, (size_t)n); else memcpy(b, u->p, (size_t)n); u->p += n; u->resid -= n; return 0; }
#define IO_NDELAY 0x0010
#define minor(d) ((d) & 0xff)
#define makedev(ma, mi) (((ma) << 8) | (mi))
typedef struct { int x; } lck_grp_t; typedef struct { int held; } lck_mtx_t;
#define LCK_GRP_ATTR_NULL 0
#define LCK_ATTR_NULL 0
static lck_grp_t g_grp;
static lck_grp_t *lck_grp_alloc_init(const char *n, int a) { (void)n; (void)a; return &g_grp; }
static void lck_mtx_init(lck_mtx_t *m, lck_grp_t *g, int a) { (void)g; (void)a; m->held = 0; }
static void lck_mtx_lock(lck_mtx_t *m) { m->held++; }
static void lck_mtx_unlock(lck_mtx_t *m) { m->held--; }
struct cdevsw { int (*d_open)(dev_t, int, int, struct proc *); int (*d_close)(dev_t, int, int, struct proc *);
	int (*d_ioctl)(dev_t, unsigned long, caddr_t, int, struct proc *); int (*d_read)(dev_t, struct uio *, int); int (*d_write)(dev_t, struct uio *, int); };
static int cdevsw_add(int i, const struct cdevsw *c) { (void)i; (void)c; return 42; }
static int cdevsw_remove(int i, const struct cdevsw *c) { (void)i; (void)c; return 0; }
#define DEVFS_CHAR 0
#define UID_ROOT 0
#define GID_WHEEL 0
static int g_nodes; static void *devfs_make_node(int d, int t, int u, int g, int m, const char *f, ...) { (void)d;(void)t;(void)u;(void)g;(void)m;(void)f; g_nodes++; return &g_nodes; }
static int g_gdv = 9, g_have_gdv = 1;
static int PE_parse_boot_argn(const char *n, void *v, int s) { (void)s; if (!strcmp(n, "n2gdv") && g_have_gdv) { *(int *)v = g_gdv; return 1; } return 0; }
#define _IOWR(g, n, t) (0xc0000000u | ((uint32_t)sizeof(t) << 16) | ((uint32_t)(g) << 8) | (n))
#define _IOW(g, n, t) (0x80000000u | ((uint32_t)sizeof(t) << 16) | ((uint32_t)(g) << 8) | (n))
static uint64_t g_now;                                     /* ns */
static uint64_t mach_absolute_time(void) { return g_now; }
static void absolutetime_to_nanoseconds(uint64_t a, uint64_t *ns) { *ns = a; }
static void delay(unsigned us) { g_now += (uint64_t)us * 1000u; }
static int g_lines, g_verbose; static char g_last[512];
#define __builtin_arm_dsb(x) __sync_synchronize()
#define VM_WIMG_IO 0
#ifndef __printflike
#define __printflike(a, b) __attribute__((format(printf, a, b)))
#endif
static uint8_t g_trace[0x40000];
static vm_offset_t ml_io_map_unmappable(uint64_t pa, uint64_t sz, int w) { (void)sz; (void)w; return pa == 0x4811f000u ? (vm_offset_t)g_trace : 0; }
static int cpu_number(void) { return 3; }
#define printf(...) tprintf(__VA_ARGS__)
static int tprintf(const char *f, ...) { va_list a; va_start(a, f); vsnprintf(g_last, sizeof g_last, f, a); va_end(a); g_lines++; if (g_verbose) fputs(g_last, stdout); return 0; }
#include "nord2_gpsdl.inc"
#undef printf
extern char *getenv(const char *);

/* ---- fake hardware: conn_infra host page + GPS bus (USRT, BGF DMA) */
static uint32_t regs[256][2]; static int nregs;
static long g_gps_bus_access, g_access;                    /* GPS bus = AP 0x18c00000.. */
static int g_guard_ok = 1, g_top = 2, g_sel = 0x80;
static uint32_t *R(uint32_t pa) { for (int i = 0; i < nregs; i++) if (regs[i][0] == pa) return &regs[i][1]; regs[nregs][0] = pa; regs[nregs][1] = 0; return &regs[nregs++][1]; }
static uint8_t g_ring[0x10000];
#ifndef N2GL_RING_OFF
#define N2GL_RING_OFF 0u
#endif
static uint32_t g_rx_pending;                              /* bytes the fake DSP delivers on the next RX arming */
static uint32_t fr(void *c, uint32_t pa) { (void)c; g_access++; if (pa >= 0x18c00000u) g_gps_bus_access++;
	if (pa == N2G_CSR_GPS_CFG2HOST_DBG) return (uint32_t)g_top;
	if (pa == N2G_CSR_HOST2GPS_DBG_SEL) return (uint32_t)g_sel;
	if (pa == N2G_DMA_INTSTA(N2GL_TXCH)) return N2G_DMA_BIT;
	if (pa == N2G_DMA_STATE(N2GL_TXCH)) return 1;
	if (pa == N2G_DMA_RLCT(N2GL_RXCH) && g_rx_pending) { uint32_t at = (*R(N2G_DMA_PGMADDR(N2GL_RXCH)) - 0x78000000u - N2GL_RING_OFF); for (uint32_t i = 0; i < g_rx_pending; i++) g_ring[at + i] = (uint8_t)(0x40 + i); }
	if (pa == N2G_DMA_RLCT(N2GL_RXCH)) { uint32_t cnt = *R(N2G_DMA_COUNT(N2GL_RXCH)) & 0xffff; uint32_t w = g_rx_pending / 4u; return cnt > w ? cnt - w : 0; }
	if (pa == N2G_DMA_INTSTA(N2GL_RXCH)) return 0;
	if (pa == N2G_USRT_STA(0)) return g_rx_pending ? 2u : 0u;   /* NODAINTB after a burst */
	return *R(pa); }
static long g_rxstart;
static void fw(void *c, uint32_t pa, uint32_t v) { (void)c; g_access++; if (pa >= 0x18c00000u) g_gps_bus_access++;
	if (pa == N2G_DMA_START(N2GL_RXCH) && (v & N2G_DMA_BIT)) g_rxstart++;
	*R(pa) = v;
	if (pa == N2G_DMA_START(N2GL_RXCH) && (v & N2G_DMA_BIT) && g_rx_pending) {          /* the DSP writes its burst */
		uint32_t at = (*R(N2G_DMA_PGMADDR(N2GL_RXCH)) - 0x78000000u - N2GL_RING_OFF);
		for (uint32_t i = 0; i < g_rx_pending; i++) g_ring[at + i] = (uint8_t)(0x40 + i);
	} }
static void fd(void *c, unsigned us) { (void)c; delay(us); }
static bool n2gd_conninfra_ready(void) { return true; }
static int n2gd_hw(struct n2g_hw *hw) { struct n2g_hw h = { 0, fr, fw, fd, 0, 0xdfe00000u }; *hw = h; return 0; }
static bool n2gd_ao_predicates(uint32_t out[6]) { memset(out, 0, 24); out[0] = 0xd; return g_guard_ok; }
static bool n2gd_wifi_guard(uint32_t out[9]) { memset(out, 0, 36); out[7] = 1u << 16 | 1; out[8] = 6; return g_guard_ok; }
static vm_offset_t n2gd_wifi_host(void) { return 1; }
static uint32_t n2gd_mapone(int k) { (void)k; return 0; }
static int n2gd_ringwin(uint8_t **va) { *va = g_ring; return 0; }
static uint32_t n2gd_gicd_rd(uint32_t off) { (void)off; return 0; }
#ifdef GIC_0589
static int g_gicw; static void n2gd_gicd_wr(uint32_t off, uint32_t v) { (void)off; (void)v; g_gicw++; }
#endif
#ifdef LNA_0582
static int g_lna; static void n2gd_lna_l1_dsp_ctrl(void) { g_lna++; }
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s (last line: %s)\n", __FILE__, __LINE__, #c, g_last); fails++; } } while (0)
static int rd(uint8_t *out, int n, int ndelay) { struct uio u = { out, n }; g_rdir = 1; int e = n2gd_read(makedev(42, 0), &u, ndelay ? IO_NDELAY : 0); return e ? -e : (int)(n - u.resid); }
static int wr(const uint8_t *in, int n) { struct uio u = { (uint8_t *)in, n }; g_rdir = 0; int e = n2gd_write(makedev(42, 0), &u, 0); return e ? -e : (int)(n - u.resid); }
static void hold(void)
{	/* the state ladder K=31 leaves (n2gd_ladder: HOLD branch) */
	n2gd_hw(&n2gl_hw); n2gl_ring_va = g_ring; n2gl_rx.rd = n2gl_rx.wr = 0; n2gl_rx_armed = false;
	n2gl_rx_cap = 256; n2gl_rx_norearm = false; n2gd.powered = true; n2gl_v9hold = true; n2gd_dl_up = true;
	memset(g_ring + N2GL_RING, N2GL_FILL, N2GL_RING);
}
int main(void)
{
	g_verbose = getenv("V") != NULL;
	/* start(): default on, kill switch bit 1024 */
	CHECK(nord2_gpsdl_start() == 0 && n2gd.made && g_nodes == 2 && n2gd.enabled);
	{ uint32_t v = 0; n2gd_ctl = 1; CHECK(n2gd_ioctl(makedev(42, 0), N2GD_IOC(171), (caddr_t)&v, 0, 0) == ENODEV); n2gd_ctl = 0; }   /* kern.n2gnss bit 0 */
#ifdef PIPE_0581
	CHECK(!N2GQ_OBSERVE);
	uint8_t buf[512]; int r;
	/* kern.n2gnss bit 0: pipe refuses */
	n2gd_ctl = 1; hold(); CHECK(rd(buf, 512, 1) == -ENODEV && g_access == 0); n2gd_ctl = 0; n2gd_dl_up = false;
	/* not in HOLD: ENODEV, nothing accessed */
	CHECK(rd(buf, 512, 1) == -ENODEV && g_access == 0);
	hold();
	/* HOLD, top active, no data: EAGAIN after touching the GPS bus (RX armed) */
	r = rd(buf, 512, 1); CHECK(r == -EAGAIN && g_gps_bus_access > 0);
	/* the fake DSP delivers 64 bytes: next reads return them */
	g_rx_pending = 64; int got = 0;
	for (int i = 0; i < 5 && got < 64; i++) { r = rd(buf + got, 512 - got, 1); if (r > 0) got += r; }
	CHECK(got == 64 && buf[0] == 0x40 && buf[63] == 0x40 + 63);
	g_rx_pending = 0;
	/* top NOT active: EAGAIN, and NO GPS-bus access at all */
	g_top = 0; long b = g_gps_bus_access; r = rd(buf, 512, 1); CHECK(r == -EAGAIN && g_gps_bus_access == b && n2gq.top_bad == 1 && n2gd_dl_up);
	b = g_gps_bus_access; uint8_t f[15] = { 0xaa, 0xf0, 0x09, 0x00, 0x05, 0xfe, 0x32, 0, 0, 1, 0, 0x3f, 1, 0xaa, 0x0f };
	r = wr(f, 15); CHECK(r == -EAGAIN && g_gps_bus_access == b && n2gq.wr == 0);
	/* debug mux moved away from 0x80: also refused */
	g_top = 2; g_sel = 0; b = g_gps_bus_access; CHECK(rd(buf, 512, 1) == -EAGAIN && g_gps_bus_access == b); g_sel = 0x80;
	/* top back: write goes out (quiet: no per-register console lines after the first 24 writes' 'about to TX' line) */
	int l0 = g_lines; r = wr(f, 15); CHECK(r == 15 && n2gq.wr == 1 && n2gq.wr_bytes == 15 && memcmp(g_ring, f, 15) == 0);
	CHECK(g_lines - l0 <= 3);                                  /* about-to-TX line + maybe gps_top_off change + heartbeat */
	for (int i = 0; i < 30; i++) wr(f, 15);
	l0 = g_lines; wr(f, 15); CHECK(g_lines - l0 <= 1);         /* past the 24 logged writes: silent (heartbeat at most) */
	/* Wi-Fi guard refuses: transient EAGAIN, nothing accessed; pipe down after 3000 consecutive refusals */
	g_guard_ok = 0; long a = g_access; r = rd(buf, 512, 1); CHECK(r == -EAGAIN && g_access == a && n2gd_dl_up);
	g_guard_ok = 1; r = rd(buf, 512, 1); CHECK(r == -EAGAIN && n2gq.consec_ref == 0);   /* success resets the run */
	g_guard_ok = 0; for (int i = 0; i < 2999; i++) rd(buf, 512, 1);
	CHECK(n2gd_dl_up && n2gq.consec_ref == 2999);
	r = rd(buf, 512, 1); CHECK(r == -EIO && !n2gd_dl_up && strstr(g_last, "PIPE DOWN"));
	CHECK(rd(buf, 512, 1) == -ENODEV);
	/* heartbeat: one line per 2 s of pipe time */
	g_guard_ok = 1; hold(); n2gq.consec_ref = 0; int hb = 0; l0 = g_lines;
	for (int i = 0; i < 400; i++) { delay(10000); rd(buf, 512, 1); if (strstr(g_last, "NORD2 GNSS PIPE: t=")) hb++; }
	CHECK(hb >= 1 && g_lines - l0 < 10);
	/* observe-only (kern.n2gnss bit 1): top not active is counted but the bus is still accessed */
	n2gd_ctl = 2; g_top = 5; b = g_gps_bus_access; r = rd(buf, 512, 1); CHECK(r == -EAGAIN && g_gps_bus_access > b);
	n2gd_ctl = 0; g_top = 2;
	printf("pipe checks: reads %llu (data %llu) writes %llu top_bad %llu guard_ref %llu\n", (unsigned long long)n2gq.rd, (unsigned long long)n2gq.rd_data,
	    (unsigned long long)n2gq.wr, (unsigned long long)n2gq.top_bad, (unsigned long long)n2gq.guard_ref);
#endif
#ifdef GAP_0587
	{	/* 0587: gate samples slp_prot + bus_timeout_irq (host CSR only); K=33 gap watch samples 9 words per ms and survives */
		g_guard_ok = 1; g_top = 2; g_sel = 0x80; hold(); n2gq.consec_ref = 0; n2gq.cslog = 0;
		*R(N2G_CSR_BUS_TIMEOUT_IRQ) = 1; *R(N2G_CSR_SLP_PROT_CTRL) = 0xe00;
		long a0 = g_access; rd(buf, 512, 1); CHECK(g_access > a0 && n2gq.last_bto == 1 && n2gq.last_slp == 0xe00 && n2gq.cslog == 1);
		*R(N2G_CSR_BUS_TIMEOUT_IRQ) = 0; rd(buf, 512, 1); CHECK(n2gq.cslog == 2 && n2gq.last_bto == 0);
		n2gd_ctl = 16; uint32_t c0 = n2gq.cslog; *R(N2G_CSR_BUS_TIMEOUT_IRQ) = 1; rd(buf, 512, 1); CHECK(n2gq.cslog == c0); n2gd_ctl = 0;
		uint64_t t0 = g_now; g_rx_pending = 0; int l0 = g_lines; a0 = g_access;
		n2gl_k = 33; n2gl_stop = false; n2gb_gap_watch(50);
		CHECK(strstr(g_last, "S13 GAP END after 50 ms") != NULL && g_access - a0 >= 50 * 9 && g_now - t0 >= 50000000ull && g_lines - l0 <= 63 && !n2gl_rx_armed);
		printf("gap watch: %ld accesses, %d lines, %llu ms\n", g_access - a0, g_lines - l0, (unsigned long long)((g_now - t0) / 1000000u));
	}
#endif
#ifdef RX_0588
	{	/* 0588: K=34/36 pipe write closes the running D2A round and arms a fresh 64 B one before the TX; mode 0 does not */
		g_guard_ok = 1; g_top = 2; g_sel = 0x80; hold(); n2gq.consec_ref = 0; n2gl_rx_cap = 64; g_rx_pending = 0;
		uint8_t f2[15] = { 0xaa, 0xf0, 0x09, 0x00, 0x05, 0xfe, 0x32, 0, 0, 1, 0, 0x3f, 1, 0xaa, 0x0f };
		rd(buf, 512, 1); CHECK(n2gl_rx_armed);
		n2gq_mode = 0; long s0 = g_rxstart; CHECK(wr(f2, 15) == 15 && g_rxstart == s0);
		n2gq_mode = 2; s0 = g_rxstart; CHECK(wr(f2, 15) == 15 && g_rxstart == s0 + 1 && n2gl_rx_armed && n2gl_rx_len == 64);
		n2gq_mode = 0; n2gl_rx_cap = 0;
		printf("0588 rx-fresh checks done\n");
	}
#endif
#ifdef GIC_0589
	{	/* 0589: quiet pipe = no gate register access (guard refusal ignored), no console line; GIC words traced around TX in mode 16 */
		hold(); n2gq.consec_ref = 0; g_rx_pending = 0; n2gl_rx_cap = 256;
		uint8_t f3[15] = { 0xaa, 0xf0, 0x09, 0x00, 0x05, 0xfe, 0x32, 0, 0, 1, 0, 0x3f, 1, 0xaa, 0x0f };
		/* gps_top_off reads "not active" (g_top 0): the normal gate refuses, the quiet gate does not read it at all */
		n2gq_mode = 13; g_guard_ok = 1; g_top = 0; long gr = (long)n2gq.top_bad; int l0 = g_lines;
		CHECK(wr(f3, 15) == 15 && (long)n2gq.top_bad == gr && g_lines == l0);
		CHECK(rd(buf, 512, 1) == -EAGAIN && g_lines == l0);
		n2gq_mode = 29; CHECK(wr(f3, 15) == 15);
		n2gq_gic_mask(); CHECK(g_gicw == 0);                    /* fake GICD reads 0: nothing enabled, nothing written */
		n2gq_mode = 0; g_guard_ok = 1; g_top = 2;
		printf("0589 quiet-pipe checks done\n");
	}
#endif
#ifdef TX_0590
	{	/* 0590: K=39 pipe TX through the ladder's n2gl_tx (readback + printf lines); K=40 no-poll TX (first done check 200 us after START) */
		g_guard_ok = 1; g_top = 2; hold(); n2gq.consec_ref = 0; g_rx_pending = 0; n2gl_rx_cap = 64;
		uint8_t f4[15] = { 0xaa, 0xf0, 0x09, 0x00, 0x05, 0xfe, 0x32, 0, 0, 1, 0, 0x3f, 1, 0xaa, 0x0f };
		n2gq_mode = 111; int l0 = g_lines; CHECK(wr(f4, 15) == 15 && g_lines - l0 >= 3 && strstr(g_last, "write 0x18c10120") != NULL);
		n2gq_mode = 175; l0 = g_lines; uint64_t t0 = g_now; CHECK(wr(f4, 15) == 15 && g_lines == l0 && g_now - t0 >= 200000ull);
		n2gq_mode = 0; n2gl_rx_cap = 0;
		printf("0590 tx-variant checks done\n");
	}
#endif
#ifdef XCHG_0591
	{	/* 0591: ioctl 190 refuses without HOLD/blob (0x100), cpu id appended to trace lines */
		uint32_t v = 0; n2gd_dl_up = false; CHECK(n2gd_ioctl(makedev(42, 0), N2GD_IOC(190), (caddr_t)&v, 0, 0) == 0 && v == 0x100u);
		hold(); v = 0; CHECK(n2gd_ioctl(makedev(42, 0), N2GD_IOC(190), (caddr_t)&v, 0, 0) == 0 && v == 0x100u);   /* no blob uploaded */
		printf("0591 xchg190 refusal checks done\n");
	}
#endif
#ifdef LNA_0593
	{	uint32_t v = 0; int l0 = g_lna; n2gd_dl_up = false; CHECK(n2gd_ioctl(makedev(42, 0), N2GD_IOC(192), (caddr_t)&v, 0, 0) == 0 && v == 0x100u && g_lna == l0);
		hold(); v = 0; CHECK(n2gd_ioctl(makedev(42, 0), N2GD_IOC(192), (caddr_t)&v, 0, 0) == 0 && v == 1u && g_lna == l0 + 1);
		printf("0593 lna-ioctl checks done\n"); }
#endif
#ifdef TRACE_0583
	{ uint32_t *h = (uint32_t *)(void *)g_trace; CHECK(h[0] == 0x43474244u && h[2] > 0);
	  /* the ring holds text lines; the last ones are present in order */
	  char *txt = (char *)g_trace + 12; size_t st = h[1], sz = h[2], cap = sizeof g_trace - 12;
	  static char lin[0x40000]; size_t k = 0; for (size_t i = 0; i < sz; i++) lin[k++] = txt[(st + cap - sz + i) % cap]; lin[k] = 0;
	  CHECK(strstr(lin, "GT ") && strstr(lin, " ioctl 0x") && strstr(lin, " read #") && strstr(lin, "write 15 bytes")
#ifdef XCHG_0591
	    && strstr(lin, " c3\n") && strstr(lin, " kaddr n2gt_init ")
#endif
#ifdef GIC_0589
	    && strstr(lin, " gic pre en ") && strstr(lin, " gic post en ") && strstr(lin, " gic hold en ")
#endif
	    ); printf("trace ring: %u bytes, last line: %.80s\n", h[2], strrchr(lin, 'G')); }
#endif
	if (fails) printf("GNSS PIPE TEST: %d FAILURES\n", fails); else printf("GNSS PIPE TEST OK\n");
	return fails != 0;
}
