/* SPDX-License-Identifier: MIT */
/*
 * nord2_gpsdl.inc -- patch 0350 SKELETON: /dev/gpsdl0 (GNSS L1) and /dev/gpsdl1 (L5) for the Nord2 XNU port, the
 * character devices MediaTek's mnld/libmnl.so talk to on Android (drivers/misc/mediatek/connectivity/gps/data_link,
 * linux/gps_each_device.c). Same style as nord2_jpegdev.inc: included by bsd/dev/sweet_touch.c, host builds define
 * N2GD_HOST and supply the kernel shims.
 *
 * Milestone 1 (this file + ../../../gnss_pwr.c, host-tested by test_gnss_pwr.c):
 *   - nodes, open/close bookkeeping, ioctl numbers of the Linux driver (GPSDL_IOC_*), GPSDL_IOC_QUERY_STATUS;
 *   - power path = n2g_common_on() + n2g_dsp_on() on the first open of a link, gated by boot-arg nord2gnss=1 AND the
 *     (or n2gps=1) AND the Wi-Fi bring-up's conninfra owner being powered (n2gd_conninfra_ready(), to be wired to the persistent/wake state);
 *   - read/write/select return ENODEV until milestone 2 (the DMA rings) exists: no EMI/DMA is touched here.
 * Milestone 2 (patch 0352, ../../../gnss_dl.c, host-tested by test_gnss_dl.c + test_gpsdl.c): read/write/select over the
 *   per-link TX/RX rings below, a worker thread polling the links every N2GD_POLL_US while a link is open.
 * Originally planned for M2: the per-link TX/RX DMA rings in a reserved EMI window (GPS_DL_TX/RX_BUF_SIZE 8 KiB),
 *   BGF_GPS_DMA (bus 0x80010000 -> AP 0x18C10000) channel setup, the GNSS IRQs (GIC SPI 383..387 in the Android DT),
 *   USRT APB mailbox for DSP wake/sleep (gps_usrt_apb.h), the DSP state machine (gps_dsp_fsm.c: RESET_DONE -> MVCD ->
 *   WORKING; the DSP code itself is sent by libmnl over this data link, "_BootDSP_UART_V2"/"Send_DspCode").
 *
 * Linux ioctls are plain numbers (cmd 6..21); on XNU the userland shim (n2mnld's ioctl wrapper) maps them to
 * N2GD_IOC(n) below, which encode the argument size, so the kernel copyin/copyout stays bounded.
 */
#ifndef NORD2_GPSDL_INC
#define NORD2_GPSDL_INC 1

#ifdef N2GD_HOST
#include "../../../gnss_dl.h"
#else
#include "nord2_gnss_pwr.inc"   /* gnss_pwr.h + gnss_pwr.c with N2G_API static (patch 0350) */
#include "nord2_gnss_dl.inc"    /* gnss_dl.h + gnss_dl.c with N2G_API static (patch 0352) */
#endif

#ifdef N2GD_HOST
/* kernel types and functions are supplied by the host test */
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/ioccom.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <kern/thread.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
#include <pexpert/pexpert.h>
#endif

#define N2GD_LINKS        2                                  /* gpsdl0 = L1, gpsdl1 = L5 */
#define N2GD_IOC(n)       _IOWR('G', (n), uint32_t)          /* Linux GPSDL_IOC_* n, 4-byte in/out argument */
enum {                                                         /* gps_dl_linux.c numbers */
	GPSDL_IOC_GPS_HWVER = 6, GPSDL_IOC_GPS_IC_HW_VERSION = 7, GPSDL_IOC_GPS_IC_FW_VERSION = 8,
	GPSDL_IOC_D1_EFUSE_GET = 9, GPSDL_IOC_RTC_FLAG = 10, GPSDL_IOC_CO_CLOCK_FLAG = 11, GPSDL_IOC_TRIGGER_ASSERT = 12,
	GPSDL_IOC_QUERY_STATUS = 13, GPSDL_IOC_TAKE_GPS_WAKELOCK = 14, GPSDL_IOC_GIVE_GPS_WAKELOCK = 15,
	GPSDL_IOC_GET_GPS_LNA_PIN = 16, GPSDL_IOC_GPS_FWCTL = 17, GPSDL_IOC_GPS_HW_SUSPEND = 18,
	GPSDL_IOC_GPS_HW_RESUME = 19, GPSDL_IOC_GPS_LISTEN_RST_EVT = 20, GPSDL_IOC_GPS_GET_MD_STATUS = 21,
};

#define N2GD_POLL_US      1000                               /* worker period while a link is open (I: vendor polls 1 ms-ish) */
struct n2gd_state {
	bool made, enabled, powered, worker;
	int major;
	unsigned opens[N2GD_LINKS];
	bool link_up[N2GD_LINKS];
	struct n2g_link link[N2GD_LINKS];
	struct n2g_hw hw;
	int last_err;
	uint32_t conn_hw_ver;
};
static struct n2gd_state n2gd;
static lck_grp_t *n2gd_grp;
static lck_mtx_t n2gd_lock;

/* integration points (milestone 1 stubs; the integrator binds them to the existing owners) */
static bool n2gd_conninfra_ready(void);                        /* Wi-Fi persistent init done + HOST1a0 wake vote held */
static int n2gd_hw(struct n2g_hw *hw);                         /* fills callbacks over the mapped allow-listed pages */
static int n2gd_rings(int link, uint8_t **va, uint32_t *pa);  /* 0352: 2 x 8 KiB uncached ring window per link (EMI) */
static void n2gd_worker_kick(void);                            /* 0352: start the poll thread once */

static int
n2gd_power_on_locked(void)
{
	if (n2gd.powered) { return 0; }
	if (!n2gd.enabled || !n2gd_conninfra_ready()) { return ENODEV; }
	struct n2g_hw hw;
	int e = n2gd_hw(&hw);
	if (e) { return e; }
	int rc = n2g_link_on(&hw);                               /* wake vote, common_on, L1 DSP on, vote released */
	n2gd.last_err = rc;
	if (rc != N2G_OK) {
		printf("NORD2 GNSS: power on failed, step %d\n", rc);
		return EIO;
	}
	n2gd.hw = hw;
	n2gd.conn_hw_ver = hw.read32(hw.ctx, N2G_CONN_HW_VER);
	n2gd.powered = true;
	printf("NORD2 GNSS: powered (conn_infra %#x)\n", n2gd.conn_hw_ver);
	return 0;
}

static int
n2gd_open(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	int l = minor(dev);
	if (!n2gd.made || l < 0 || l >= N2GD_LINKS) { return ENXIO; }
	lck_mtx_lock(&n2gd_lock);
	int e = n2gd_power_on_locked();
	if (!e && !n2gd.link_up[l]) {                            /* 0352: rings + USRT in DMA mode, DSP ROM okay */
		uint8_t *va; uint32_t pa;
		e = n2gd_rings(l, &va, &pa);
		if (!e && n2g_dl_init(&n2gd.link[l], l, &n2gd.hw, va, pa) != N2G_OK) { e = EINVAL; }
		if (!e) {
			int rc = n2g_dl_usrt_on(&n2gd.link[l]);
			if (rc != N2G_OK) { printf("NORD2 GNSS: link %d usrt on failed %d (DSP ROM flag)\n", l, rc); e = EIO; n2gd.last_err = rc; }
		}
		if (!e) { n2gd.link_up[l] = true; printf("NORD2 GNSS: link %d up (rings %#x)\n", l, pa); n2gd_worker_kick(); }
	}
	if (!e) { n2gd.opens[l]++; }
	lck_mtx_unlock(&n2gd_lock);
	return e;
}

static int
n2gd_close(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	int l = minor(dev);
	if (!n2gd.made || l < 0 || l >= N2GD_LINKS) { return ENXIO; }
	lck_mtx_lock(&n2gd_lock);
	if (n2gd.opens[l]) { n2gd.opens[l]--; }
	/* power-off policy comes with milestone 2 (DSP sleep/stop states); the block stays on, as after mnld exits */
	lck_mtx_unlock(&n2gd_lock);
	return 0;
}

static int
n2gd_ioctl(dev_t dev, unsigned long cmd, caddr_t data, __unused int fflag, __unused struct proc *p)
{
	int l = minor(dev);
	if (!n2gd.made || l < 0 || l >= N2GD_LINKS) { return ENXIO; }
	if (data == NULL) { return EFAULT; }
	uint32_t v = 0;
	switch ((uint32_t)cmd) {
	case (uint32_t)N2GD_IOC(GPSDL_IOC_QUERY_STATUS):
		v = (n2gd.powered ? 1u : 0u) | (n2gd.link_up[l] ? 2u : 0u) | ((uint32_t)(n2gd.last_err & 0xff) << 8);
		break;
	case (uint32_t)N2GD_IOC(GPSDL_IOC_GPS_HWVER):
		v = n2gd.conn_hw_ver;
		break;
	case (uint32_t)N2GD_IOC(GPSDL_IOC_TAKE_GPS_WAKELOCK):
	case (uint32_t)N2GD_IOC(GPSDL_IOC_GIVE_GPS_WAKELOCK):
		v = 0;                                           /* no wakelocks on XNU */
		break;
	default:
		return ENOTSUP;                                  /* milestone 2+ */
	}
	*(uint32_t *)(void *)data = v;                           /* _IOWR: the kernel copies the 4 bytes in/out */
	return 0;
}

/* 0352: the worker polls every open link; readers/writers sleep on the link and are woken when it made progress */
static void
n2gd_worker_step(void)
{
	lck_mtx_lock(&n2gd_lock);
	for (int l = 0; l < N2GD_LINKS; l++) {
		if (!n2gd.link_up[l] || !n2gd.opens[l]) { continue; }
		if (n2g_dl_poll(&n2gd.link[l])) { wakeup(&n2gd.link[l]); }
	}
	lck_mtx_unlock(&n2gd_lock);
}

static int
n2gd_read(dev_t dev, struct uio *uio, int flag)
{
	int l = minor(dev), e = 0;
	if (!n2gd.made || l < 0 || l >= N2GD_LINKS || !n2gd.link_up[l]) { return ENODEV; }
	uint8_t buf[512];
	lck_mtx_lock(&n2gd_lock);
	while (n2g_dl_rx_avail(&n2gd.link[l]) == 0) {
		if (flag & IO_NDELAY) { lck_mtx_unlock(&n2gd_lock); return EAGAIN; }
		e = msleep(&n2gd.link[l], &n2gd_lock, PZERO | PCATCH, "gpsdl-rd", NULL);
		if (e) { lck_mtx_unlock(&n2gd_lock); return e; }
	}
	while (uio_resid(uio) > 0 && n2g_dl_rx_avail(&n2gd.link[l]) > 0) {
		size_t want = (size_t)uio_resid(uio) < sizeof buf ? (size_t)uio_resid(uio) : sizeof buf;
		size_t n = n2g_dl_read(&n2gd.link[l], buf, want);
		lck_mtx_unlock(&n2gd_lock);
		e = uiomove((caddr_t)buf, (int)n, uio);
		lck_mtx_lock(&n2gd_lock);
		if (e) { break; }
	}
	lck_mtx_unlock(&n2gd_lock);
	return e;
}

static int
n2gd_write(dev_t dev, struct uio *uio, __unused int flag)       /* always blocking (libmnl writes blocking) */
{
	int l = minor(dev), e = 0;
	if (!n2gd.made || l < 0 || l >= N2GD_LINKS || !n2gd.link_up[l]) { return ENODEV; }
	uint8_t buf[512];
	while (uio_resid(uio) > 0) {
		size_t want = (size_t)uio_resid(uio) < sizeof buf ? (size_t)uio_resid(uio) : sizeof buf;
		e = uiomove((caddr_t)buf, (int)want, uio);
		if (e) { return e; }
		size_t off = 0;
		lck_mtx_lock(&n2gd_lock);
		while (off < want) {
			off += n2g_dl_write(&n2gd.link[l], buf + off, want - off);
			if (off < want) {
				e = msleep(&n2gd.link[l], &n2gd_lock, PZERO | PCATCH, "gpsdl-wr", NULL);
				if (e) { lck_mtx_unlock(&n2gd_lock); return e; }
			}
		}
		lck_mtx_unlock(&n2gd_lock);
	}
	return 0;
}

#ifndef N2GD_HOST
static void
n2gd_worker(__unused void *arg, __unused wait_result_t w)
{
	for (;;) {
		n2gd_worker_step();
		bool any = (n2gd.opens[0] && n2gd.link_up[0]) || (n2gd.opens[1] && n2gd.link_up[1]);
		struct timespec ts = { 0, any ? N2GD_POLL_US * 1000L : 100L * 1000 * 1000 };   /* idle: 100 ms */
		(void)msleep(&n2gd.worker, NULL, PZERO, "gpsdl-poll", &ts);
	}
}
static void
n2gd_worker_kick(void)
{
	if (n2gd.worker) { return; }
	thread_t t;
	if (kernel_thread_start(n2gd_worker, NULL, &t) == KERN_SUCCESS) { thread_deallocate(t); n2gd.worker = true; }
	else { printf("NORD2 GNSS: worker thread start failed\n"); }
}
#endif

#ifdef N2GD_HOST
static const struct cdevsw n2gd_cdevsw = { .d_open = n2gd_open, .d_close = n2gd_close, .d_ioctl = n2gd_ioctl, .d_read = n2gd_read, .d_write = n2gd_write };
#else
static const struct cdevsw n2gd_cdevsw = {
	.d_open = n2gd_open, .d_close = n2gd_close,
	.d_read = n2gd_read, .d_write = n2gd_write, .d_ioctl = n2gd_ioctl,
	.d_stop = eno_stop, .d_reset = eno_reset, .d_ttys = NULL,
	.d_select = eno_select, .d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0,
};
#endif

/* Called from nord2_input_init() after the Wi-Fi owner exists (the integrator adds the call). Touches no register. */
static int nord2_gpsdl_start(void) __attribute__((unused));
static int
nord2_gpsdl_start(void)
{
	if (n2gd.made) { return 0; }
	int on = 0;
	/* nord2gnss=1, or the short alias n2gps=1 (the packed boot-args are at their 607-character limit) */
	n2gd.enabled = (PE_parse_boot_argn("nord2gnss", &on, sizeof(on)) && on == 1) ||
	    (PE_parse_boot_argn("n2gps", &on, sizeof(on)) && on == 1);
	n2gd_grp = lck_grp_alloc_init("nord2-gpsdl", LCK_GRP_ATTR_NULL);
	lck_mtx_init(&n2gd_lock, n2gd_grp, LCK_ATTR_NULL);
	int major = cdevsw_add(-1, &n2gd_cdevsw);
	if (major < 0) { printf("NORD2 GNSS: cdevsw_add failed (%d)\n", major); return EIO; }
	for (int l = 0; l < N2GD_LINKS; l++) {
		if (devfs_make_node(makedev(major, l), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0660, "gpsdl%d", l) == NULL) {
			printf("NORD2 GNSS: devfs_make_node gpsdl%d failed\n", l);
			(void)cdevsw_remove(major, &n2gd_cdevsw);
			return EIO;
		}
	}
	n2gd.major = major;
	n2gd.made = true;
	printf("NORD2 GNSS: /dev/gpsdl0,1 made (major %d), power path %s\n", major, n2gd.enabled ? "enabled (nord2gnss/n2gps=1)" : "disabled");
	return 0;
}

#endif /* NORD2_GPSDL_INC */
