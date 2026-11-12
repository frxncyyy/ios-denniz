/* SPDX-License-Identifier: MIT -- host test of kern/bsd/dev/nord2_gpsdl.inc with kernel shims and a fake conn_infra.
 * cc -O1 -Wall -Wextra -Wno-unused-function -I. test_gpsdl.c gnss_pwr.c && ./a.out */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#define N2GD_HOST 1
#define __unused __attribute__((unused))
typedef int dev_t; typedef char *caddr_t; struct proc;
typedef long user_ssize_t;
struct uio { uint8_t *p; long resid; };
static long uio_resid(struct uio *u) { return u->resid; }
static int g_rdir;   /* 1 = uio is a read destination */
static int uiomove(caddr_t b, int n, struct uio *u) { if (n > u->resid) n = (int)u->resid; if (g_rdir) memcpy(u->p, b, (size_t)n); else memcpy(b, u->p, (size_t)n); u->p += n; u->resid -= n; return 0; }
#define IO_NDELAY 0x4
#define PZERO 22
#define PCATCH 0x100
static int msleep(void *c, void *m, int p, const char *w, void *ts) { (void)c; (void)m; (void)p; (void)w; (void)ts; return EINTR; }
static void wakeup(void *c) { (void)c; }
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
static int g_bootarg = 1; static int PE_parse_boot_argn(const char *n, void *v, int s) { (void)s; if (strcmp(n, "n2gps")) return 0; *(int *)v = g_bootarg; return 1; }  /* image uses the short alias */
#define _IOWR(g, n, t) (0xc0000000u | ((uint32_t)sizeof(t) << 16) | ((uint32_t)(g) << 8) | (n))
#include "kern/bsd/dev/nord2_gpsdl.inc"
static uint8_t g_ring[2][2 * N2G_RING_BYTES];
static int n2gd_rings(int l, uint8_t **va, uint32_t *pa) { *va = g_ring[l]; *pa = 0xdfe00000u + (uint32_t)l * 0x8000u; return 0; }
static int g_kicks; static void n2gd_worker_kick(void) { g_kicks++; }

/* fake conn_infra: everything acks */
static uint32_t regs[64][2]; static int nregs, nwrites, g_ready = 1, g_hwver = N2G_CONN_HW_VER_MT6893;
static uint32_t *R(uint32_t pa) { for (int i = 0; i < nregs; i++) if (regs[i][0] == pa) return &regs[i][1]; regs[nregs][0] = pa; regs[nregs][1] = 0; return &regs[nregs++][1]; }
static uint32_t fr(void *c, uint32_t pa) { (void)c;
	if (pa == N2G_CONN_HW_VER) return (uint32_t)g_hwver;
	if (pa == N2G_RGU_BGFSYS_PWR_ACK) return 3u << 24;
	if (pa == N2G_CSR_GPS_CFG2HOST_DBG) return 2;
	if (pa == N2G_WTSLP_WB_STA) return 0;
	if (pa == N2G_USRT_D2AF(0) || pa == N2G_USRT_D2AF(1)) return 8;
	if (pa == N2G_CFG_GALS_CONN2GPS || pa == N2G_CFG_GALS_GPS2CONN) { uint32_t v = *R(pa); return (v & 0x11) | ((v & 1) << 3) | ((v & 0x10) << 3); }
	return *R(pa); }
static void fw(void *c, uint32_t pa, uint32_t v) { (void)c; *R(pa) = v; nwrites++; }
static void fd(void *c, unsigned us) { (void)c; (void)us; }
static bool n2gd_conninfra_ready(void) { return g_ready; }
static int n2gd_hw(struct n2g_hw *hw) { struct n2g_hw h = { 0, fr, fw, fd, 0, 0xdfe00000u }; *hw = h; return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void)
{
	uint32_t v;
	CHECK(n2gd_open(makedev(42, 0), 0, 0, 0) == ENXIO);              /* before start */
	CHECK(nord2_gpsdl_start() == 0 && g_nodes == 2 && n2gd.enabled);
	g_ready = 0; CHECK(n2gd_open(makedev(42, 0), 0, 0, 0) == ENODEV && nwrites == 0);   /* conninfra not owned: no MMIO */
	g_ready = 1; g_hwver = 0x1234; CHECK(n2gd_open(makedev(42, 0), 0, 0, 0) == EIO && nwrites == 2 && *R(N2G_CSR_WAKEPU_GPS) == 0 && !n2gd.powered); /* only the wake vote, released */
	g_hwver = N2G_CONN_HW_VER_MT6893;
	CHECK(n2gd_open(makedev(42, 0), 0, 0, 0) == 0 && n2gd.powered && n2gd.opens[0] == 1);
	int w = nwrites; CHECK(n2gd_open(makedev(42, 1), 0, 0, 0) == 0 && nwrites - w == 7);   /* second link: only its 7 USRT writes, no new power-on */
	CHECK(n2gd_open(makedev(42, 2), 0, 0, 0) == ENXIO);
	CHECK(n2gd_ioctl(makedev(42, 0), N2GD_IOC(GPSDL_IOC_QUERY_STATUS), (caddr_t)&v, 0, 0) == 0 && v == 3);   /* powered + link up */
	CHECK(n2gd.link_up[0] && n2gd.link_up[1] && g_kicks == 2 && (*R(N2G_USRT_CTRL(0)) & 3) == 3);
	CHECK(n2gd_ioctl(makedev(42, 0), N2GD_IOC(GPSDL_IOC_GPS_HWVER), (caddr_t)&v, 0, 0) == 0 && v == N2G_CONN_HW_VER_MT6893);
	CHECK(n2gd_ioctl(makedev(42, 0), N2GD_IOC(GPSDL_IOC_GPS_FWCTL), (caddr_t)&v, 0, 0) == ENOTSUP);
	CHECK(n2gd_ioctl(makedev(42, 0), 13, (caddr_t)&v, 0, 0) == ENOTSUP);            /* raw Linux number: shim must map */
	{ uint8_t msg[8] = "ABCDEFG", out[16]; struct uio u = { msg, 7 }; g_rdir = 0;
	  CHECK(n2gd_write(makedev(42, 0), &u, 0) == 0 && u.resid == 0);
	  CHECK(*R(N2G_DMA_CON(0)) == N2G_CON_L1_A2D && (*R(N2G_DMA_COUNT(0)) & 0xffff) == 2 && (*R(N2G_DMA_START(0)) & N2G_DMA_BIT));
	  CHECK(g_ring[0][0] == 'A' && g_ring[0][6] == 'G' && g_ring[0][7] == 0);    /* zero padded to a word */
	  struct uio r = { out, 16 }; g_rdir = 1;
	  CHECK(n2gd_read(makedev(42, 0), &r, IO_NDELAY) == EAGAIN);             /* nothing from the DSP */
	  n2gd_worker_step(); }
	CHECK(n2gd_close(makedev(42, 0), 0, 0, 0) == 0 && n2gd.opens[0] == 0 && n2gd_lock.held == 0);
	printf(fails ? "GPSDL TEST FAILED (%d)\n" : "GPSDL TEST OK\n", fails);
	return fails != 0;
}
