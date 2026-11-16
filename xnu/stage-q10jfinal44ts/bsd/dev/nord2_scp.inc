/*
 * 0284 NORD2 SCP: the MT6893 sensor hub (MediaTek "tinysys" SCP, two RISC-V cores) and the sensors behind it.
 *
 * On the Nord 2 every sensor sits on the SCP's own buses, not the AP's (OnePlus dtbo sensor_devinfo: light/proximity
 * TCS3701 0x39 or STK32600 0x48 on SCP I2C0 with EINT 4, magnetometer AK09918 0x0c or MMC5603 0x30 on SCP I2C0,
 * accelerometer/gyroscope on SCP SPI2). Android's kernel never drives the chips: LK loads the SCP image into its TCM,
 * the kernel lets core 0 run and talks to the firmware's sensor hub. This file does the same, from scratch, after the
 * public descriptions of the interfaces (MediaTek scp v02 / mtk-mbox / sensorHub v1 headers in the OnePlus mt6893 OSS
 * kernel; the bring-up order follows HakiOS' MT6768 driver, MIT, github.com/oosexgang/hakios-mt6768
 * kernel/bsd/dev/redwood_scp.c):
 *
 *   SCP regions (device tree scp@10700000): TCM 0x10500000 (1.5 MB; region info at +4, written by LK), cfg
 *   0x10724000, core0 0x10730000 (+0 SW_RSTN_CLR = run, +4 SW_RSTN_SET = hold, +0x40.. GPR0..7, GPR1/2 = the AP DRAM
 *   the firmware may use, +0x70 status, +0x80 PC), core1 0x10740000. Clock: topckgen CLK_CFG_0 scp_sel bits 16-18
 *   (0 = clk26m), gate bit 23, CLK_CFG_UPDATE bit 2.
 *   Mailboxes 0..4 at 0x107fb000 + 0x1000*n: 64 32-bit slots, +0x100 "set" (AP -> SCP pin bits, a bit stays set until
 *   the SCP has taken that pin), +0x10c "clr" (SCP -> AP pending pin bits; writing them back takes them). Pins (scp
 *   v02 mt6885 tables, offsets as mbox_setup_pin_table() lays them out with 64-bit slots): mbox1 recv SCP_READY_0 slot
 *   20 bit 10; mbox2 send MPOOL_0 slot 0 bit 0 (34 slots), recv MPOOL_0 slot 34 bit 17 (30 slots). The "legacy" IPIs
 *   ride on MPOOL: words {id, length in bytes, payload}; ids IPI_CHRE 1, IPI_CHREX 2, IPI_SENSOR 3.
 *   Waking it for a send: infracfg 0x10001b14 INFRA_IRQ_SET = 0xA0 | 1, until it takes it, then INFRA_IRQ_CLEAR.
 *   Sensor hub: SCP_SENSOR_HUB_REQ {type, action, 0, 0, data[]} on IPI_SENSOR; SET_CONFIG (4) gives the DRAM FIFO
 *   (struct sensorFIFO {rp, wp, FIFOSize, 0} then 44-byte data_unit_t records); sensors are enabled with nanohub
 *   ConfigCmd packets on IPI_CHRE (sync 0x31, seq, CMD_COMMS_WRITE 0x1091, length, data, CRC32), each IPI_CHRE from
 *   the SCP acknowledged with IPI_CHREX carrying its length. Android sensor types: accelerometer 1, magnetic 2,
 *   gyroscope 4, light 5, proximity 8 (data_unit_t: x/y/z at +12/+16/+20; light at +12; proximity steps +12,
 *   oneshot +16: 0 near, 1 far).
 *
 * The SCP's DRAM (its MPU window and the sensor FIFO) is N2SCP_DRAM_PHYS: below XNU's window, outside every fixed
 * carve-out of the live device tree, where Android's kernel itself places reserve-memory-scp_share (0x320000, 16 MB
 * aligned, 0x50000000-0x90000000): see the scp-probe-20261108 notes.
 *
 * Everything is in one kernel thread started with the input thread; it never blocks the boot. Every step is logged
 * ("NORD2 SCP: ..."). No storage is written. Default on; boot-arg nord2scp=0 turns it off (the command line is full,
 * so the default is compiled in), nord2scp=1 only reads (step 1).
 */
#ifndef N2SCP_DRAM_PHYS
#define N2SCP_DRAM_PHYS     0x89000000u         /* Android (rescue, 8 Nov): scp_reserve_memory_ioremap 0x89000000+0x320000 */
#endif
#define N2SCP_DRAM_SIZE     0x320000u
#define N2SCP_FIFO_PHYS_TEST 0x9fef0000u      /* q10eq: map-only test inside LK's SCP-reserved 0x9fa00000+0x500000 */
#ifndef N2SCP_START_DELAY_S
#define N2SCP_START_DELAY_S 20u
#define N2SCP_MAP_DELAY_S   90u
#endif
#define N2SCP_FIFO_OFF      0x4c000u            /* page-aligned, inside SENS_MEM_ID (0x4a700..0x14a700) */
#define N2SCP_FIFO_SIZE     0x4000u             /* 16 KB (372 records): XNU's I/O map is small, bigger maps exhausted it and the next driver to map panicked (q10ee/q10el/q10em) */

#define N2SCP_TCM           0x10500000u
#define N2SCP_CFG           0x10724000u
#define N2SCP_CORE0         0x10730000u
#define N2SCP_CORE1         0x10740000u
#define N2SCP_MBOX(n)       (0x107fb000u + 0x1000u * (n))
#define N2SCP_INFRA         0x10001000u
#define N2SCP_TOPCK         0x10000000u

#define N2SCP_IPI_CHRE      1u
#define N2SCP_IPI_CHREX     2u
#define N2SCP_IPI_SENSOR    3u

#define N2SCP_REC           44u
enum { N2S_ACCEL = 1, N2S_MAG = 2, N2S_GYRO = 4, N2S_LIGHT = 5, N2S_PROX = 8 };   /* nanohub ConfigCmd types (Android) */
/* FIFO records carry the MTK handle: the Android type - 1 (q10et: accelerometer 0, magnetometer 1, gyroscope 3, proximity 7) */
enum { N2H_ACCEL = 0, N2H_MAG = 1, N2H_GYRO = 3, N2H_LIGHT = 4, N2H_PROX = 7 };
static volatile uint32_t n2scp_count[16];
static volatile uint32_t n2scp_rsp[12], n2scp_rsp_seq;     /* 0762: last IPI_SENSOR reply (action != notify) */
static int n2scp_lightpoll = 1;                 /* 0762: 1 = poll the light raw value when the hub never pushes light */
static volatile int32_t n2scp_lightpolled = -1;
static volatile uint32_t n2scp_lightpolls;

static vm_offset_t n2scp_tcm, n2scp_cfg, n2scp_c0, n2scp_c1, n2scp_infra, n2scp_topck, n2scp_dram;
static vm_offset_t n2scp_mb[5];
static uint32_t n2scp_seq = 1;
static volatile int n2scp_prox = -1;            /* last proximity: 0 near, 1 far, -1 none */
static volatile int32_t n2scp_lux = -1;
static volatile int32_t n2scp_acc[3], n2scp_gyr[3], n2scp_magv[3];
static volatile uint32_t n2scp_state, n2scp_records, n2scp_msgs;

/* VCORE/SSHUB PMIC registers, read once by the input thread (the PMIF's only user) before its polling loop */
static const uint16_t n2scp_pmic_addr[7] = { 0x1588, 0x1594, 0x15a6, 0x15aa, 0x15b4, 0x01ac, 0x1f28 };
static uint16_t n2scp_pmic_val[7];
static volatile int n2scp_pmic_done;      /* 0 not yet, 1 read, -1 failed */
static void
n2scp_pmic_snapshot(void)
{
	int ok = 1;
	for (int i = 0; i < 7; i++) { if (!ni_pmic_read(n2scp_pmic_addr[i], &n2scp_pmic_val[i])) { ok = 0; } }
	n2scp_pmic_done = ok ? 1 : -1;
}

/*
 * VIBR at 3.0 V: Android's mtk_nanohub turns this LDO on with the proximity sensor (infrared_set_power: the TCS3701's IR
 * LED), regulator "vdd" of odm:mtk_nanohub_info = vibr; without it the proximity value never changed (q10ev, q10ew).
 * MT6359P: VIBR_ANA_CON0 0x1fae VOSEL bits 8-11 = 11 (3.0 V, regulator_codegen vibr_idx[7]), LDO_VIBR_CON0 0x1d52 EN bit 0.
 * Done by the input thread (the PMIF's only user) when the SCP thread asks: n2scp_vibr_want 1 on, 0 off, -1 nothing.
 */
static volatile int n2scp_vibr_want = -1, n2scp_vibr_done = -1;
static bool
n2scp_pmic_write(uint16_t addr, uint16_t mask, uint16_t value)
{
	uint16_t cur = 0, want;
	if ((addr != 0x1d52 || mask != 0x0001) && (addr != 0x1fae || mask != 0x0f00)) { return false; }   /* nothing else */
	if (!ni_pmic_read(addr, &cur)) { return false; }
	want = (uint16_t)((cur & ~mask) | (value & mask));
	if (want == cur) { return true; }
	if (!nw_wait(ni_pwrap, 0xca8, 0x800e, 0x8000)) { return false; }
	ni_pwrap_owned = true;
	ni_wr(ni_pwrap, 0xc84, want);
	ni_wr(ni_pwrap, 0xc80, (1u << 29) | addr);
	if (!nw_wait(ni_pwrap, 0xca8, 0x800e, 0x8000)) { return false; }
	return ni_pmic_read(addr, &cur) && cur == want;
}
static void
n2scp_pmic_service(void)
{
	int w = n2scp_vibr_want;
	uint16_t con = 0, mon = 0, ana = 0;
	if (w < 0) { return; }
	n2scp_vibr_want = -1;
	bool ok = w ? (n2scp_pmic_write(0x1fae, 0x0f00, 11u << 8) && n2scp_pmic_write(0x1d52, 0x0001, 1)) :
	    n2scp_pmic_write(0x1d52, 0x0001, 0);
	(void)ni_pmic_read(0x1d52, &con); (void)ni_pmic_read(0x1d56, &mon); (void)ni_pmic_read(0x1fae, &ana);
	printf("NORD2 SCP: VIBR (proximity IR LED) %s: %s - CON0 %#x MON %#x ANA_CON0 %#x\n", w ? "on 3.0 V" : "off", ok ? "ok" : "FAILED", con, mon, ana);
	n2scp_vibr_done = ok ? w : -2;
}

extern void sweet_hid_proximity(int near);
extern void sweet_hid_ambient(int lux);
extern void sweet_hid_motion(int kind, int x, int y, int z);
/*
 * 0760 (sensors-20261115): the motion sensors and the light sensor reach iOS.
 *   - sweet_hid_motion() now takes iOS units as IOFixed (16.16), converted here from the hub's (Android sensor 2.0 gains of
 *     mtk_nanohub.c: accelerometer 1000 = 1 m/s^2, gyroscope 1000000 = 1 rad/s, magnetometer 100 = 1 uT). iOS (CoreMotion /
 *     locationd CLAccelerometer: kIOHIDEventTypeAccelerometer fields X/Y/Z read as floats) wants g with the opposite sign of
 *     Android (lying face up: Android z = +9.81 m/s^2, iOS z = -1 g); gyroscope rad/s and magnetometer uT keep Android's signs.
 *     kern.n2scp_axflip negates single axes on top of that (bits 0-2 accel x/y/z, 3-5 gyro, 6-8 compass), for the pose test.
 *   - accelerometer / gyroscope / compass are enabled on demand: kern.n2scp_motion 2 (default) follows what iOS asks of each
 *     HID service (its "ReportInterval" property; a service nobody has configured streams while it has a client), 1 = always
 *     on at 50/50/25 Hz (0531's old mode), 0 = off.  The hub is re-told only when a rate changes.
 *   - light: mtk_nanohub never marks light as on-change (only its rate field from the HAL's sampling period), and the
 *     on-change enable of 0284 never produced a single light record; it is now enabled at kern.n2scp_lightrate (Hz * 1024,
 *     default 5 Hz; 0 = 0284's on-change request).  Proximity is not touched (same calls, same order, same rate).
 *   - kern.n2scp_alslcd = N sends OPLUS' "LCD info" request once (sensor 4, action 117, brightness N, dc mode 0), what Android's
 *     sensor_devinfo does for its light algorithm; default never sent.
 *   - kern.n2scp_raw: one line with the last raw and converted samples, rates, counters (the ssh diagnostic).
 *   Logs "NORD2 SCP: ..." as before; no new MMIO mapping, no new register, no new PMIC write.
 */
extern int sweet_hid_motion_want(int kind);     /* SweetHID.cpp: Hz iOS wants from that service (0 = off); 3 = orientation (0761) */
extern void sweet_hid_orientation(int o);       /* 0761: UIDeviceOrientation 0..6 to the 0x20/0x8A service */
extern void sweet_hid_sensor_status(char *buf, int len);
static int n2scp_axflip;                        /* 0760: kern.n2scp_axflip */
static volatile int32_t n2scp_ios[3][3];        /* 0760: last value handed to iOS per kind, IOFixed */
static volatile uint32_t n2scp_out[3];          /* 0760: samples handed to iOS per kind */
/*
 * 0761: device orientation from the accelerometer, the job iPhone10,3's always-on processor does (SweetHID.cpp 0761 comment).
 * Low-pass (1/4) of the iOS-unit gravity vector in milli-g; a candidate must hold for ~250 ms (n2scp_orient_hold samples) before it
 * is sent; face up / down when |z| > 850 mg, landscape when |x| > 550 mg and |x| > |y| + 150, portrait likewise on y;
 * UIDeviceOrientation numbering: 1 portrait (y -1 g), 2 upside down (y +1 g), 3 landscape left (x -1 g), 4 landscape right
 * (x +1 g), 5 face up (z -1 g), 6 face down (z +1 g).  kern.n2scp_orient 0 = no orientation events; kern.n2scp_orient_force
 * 0..6 = that orientation, held (test of the iOS side without moving the phone), -1 = the detector.
 */
static int n2scp_orient = 1, n2scp_orient_force = -1;
static int n2scp_rate_now_acc;                  /* 0761: copy of n2scp_rate_now[0] for the hold time */
static volatile int n2scp_orient_now = -1;
static volatile uint32_t n2scp_orient_changes;
static int32_t n2scp_lp[3];
static int n2scp_lp_init, n2scp_orient_cand = -1, n2scp_orient_cnt, n2scp_orient_forced = -1;
static int
n2scp_orient_classify(const int32_t g[3], int cur)
{
	int32_t ax = g[0] < 0 ? -g[0] : g[0], ay = g[1] < 0 ? -g[1] : g[1], az = g[2] < 0 ? -g[2] : g[2];
	if (az > 850) { return g[2] < 0 ? 5 : 6; }
	if (ax > 550 && ax > ay + 150) { return g[0] < 0 ? 3 : 4; }
	if (ay > 550 && ay > ax + 150) { return g[1] < 0 ? 1 : 2; }
	return cur;                             /* ambiguous: keep */
}
static void
n2scp_orient_set(int o, const char *why)
{
	static const char *const names[7] = { "unknown", "portrait", "upside-down", "landscape-left", "landscape-right", "face-up", "face-down" };
	if (o == n2scp_orient_now || o < 0 || o > 6) { return; }
	n2scp_orient_now = o;
	n2scp_orient_changes++;
	if (n2scp_orient_changes <= 300 || (n2scp_orient_changes % 100) == 0) {
		printf("NORD2 SCP: device orientation %s (%s, lp %d %d %d mg, change %u)\n", names[o], why, n2scp_lp[0], n2scp_lp[1], n2scp_lp[2],
		    n2scp_orient_changes);
	}
	sweet_hid_orientation(o);
}
static void
n2scp_orient_feed(const int32_t ios_fixed[3], int hz)
{
	int32_t g[3];
	for (int i = 0; i < 3; i++) { g[i] = (int32_t)((int64_t)ios_fixed[i] * 1000 / 65536); }
	if (!n2scp_lp_init) { for (int i = 0; i < 3; i++) { n2scp_lp[i] = g[i]; } n2scp_lp_init = 1; }
	for (int i = 0; i < 3; i++) { n2scp_lp[i] += (g[i] - n2scp_lp[i]) / 4; }
	if (!n2scp_orient || n2scp_orient_force >= 0) { return; }
	int c = n2scp_orient_classify(n2scp_lp, n2scp_orient_now);
	if (c != n2scp_orient_cand) { n2scp_orient_cand = c; n2scp_orient_cnt = 0; }
	int hold = hz > 0 ? hz / 4 : 3;
	if (hold < 3) { hold = 3; }
	if (++n2scp_orient_cnt >= hold && c >= 0) { n2scp_orient_set(c, "accelerometer"); }
}

static void
n2scp_motion_out(int kind, int32_t a, int32_t b, int32_t c)
{
	int64_t v[3] = { a, b, c };
	for (int i = 0; i < 3; i++) {
		if (kind == 0) { v[i] = -v[i] * 6553600 / 980665; }      /* 1/1000 m/s^2 -> g, iOS sign */
		else if (kind == 1) { v[i] = v[i] * 65536 / 1000000; }  /* 1e-6 rad/s -> rad/s */
		else { v[i] = v[i] * 65536 / 100; }                     /* 1/100 uT -> uT */
		if (n2scp_axflip & (1 << (kind * 3 + i))) { v[i] = -v[i]; }
		n2scp_ios[kind][i] = (int32_t)v[i];
	}
	n2scp_out[kind]++;
	sweet_hid_motion(kind, (int)v[0], (int)v[1], (int)v[2]);
	if (kind == 0) {                /* 0761 */
		int32_t f[3] = { (int32_t)v[0], (int32_t)v[1], (int32_t)v[2] };
		n2scp_orient_feed(f, n2scp_rate_now_acc);
	}
}

static int n2scp_chan;
static void
n2scp_sleep(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&n2scp_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static inline uint32_t n2scp_rd(vm_offset_t b, uint32_t o) { return *(volatile uint32_t *)(b + o); }
static inline void n2scp_wr(vm_offset_t b, uint32_t o, uint32_t v) { *(volatile uint32_t *)(b + o) = v; __asm__ volatile("dsb sy" ::: "memory"); }

/* -------- step 1: what LK left (read only) -------- */
/* 0410: every SCP io_submap map logged BEFORE (with a 200 ms drain: an exhausted io_submap panics inside the call, invisible on the
 * polled console) and after, so the console names the map that ran out. */
static vm_offset_t
n2scp_iomap(vm_offset_t pa, vm_size_t sz, const char *what)
{
	printf("NORD2 SCP: IOMAP %s pa %#lx +%#lx: mapping (io_submap)\n", what, (unsigned long)pa, (unsigned long)sz);
	n2scp_sleep(200);
	vm_offset_t va = ml_io_map(pa, sz);
	printf("NORD2 SCP: IOMAP %s -> %#lx%s\n", what, (unsigned long)va, va ? "" : " FAILED");
	n2scp_sleep(200);
	return va;
}

static bool
n2scp_map(void)
{
	n2scp_tcm = n2scp_iomap(N2SCP_TCM, 0x1000, "TCM");
	n2scp_cfg = n2scp_iomap(N2SCP_CFG, 0x1000, "CFG");
	n2scp_c0 = n2scp_iomap(N2SCP_CORE0, 0x1000, "CORE0");
	n2scp_c1 = n2scp_iomap(N2SCP_CORE1, 0x1000, "CORE1");
	/* XNU's I/O map (8 MB) is nearly full on this kernel: every map is 16 KB pages, so neighbours share one map */
	n2scp_topck = n2scp_iomap(N2SCP_TOPCK, 0x2000, "TOPCK+INFRA");
	n2scp_infra = n2scp_topck ? n2scp_topck + 0x1000 : 0;
	n2scp_mb[0] = n2scp_iomap(N2SCP_MBOX(0), 0x5000, "MBOX0-4");
	for (int i = 1; i < 5; i++) { n2scp_mb[i] = n2scp_mb[0] ? n2scp_mb[0] + 0x1000u * (uint32_t)i : 0; }
	return n2scp_tcm && n2scp_cfg && n2scp_c0 && n2scp_c1 && n2scp_infra && n2scp_topck && n2scp_mb[1] && n2scp_mb[2];
}

static bool
n2scp_step1(void)
{
	uint32_t ri[17], pc0, pc1;

	for (int i = 0; i < 17; i++) { ri[i] = n2scp_rd(n2scp_tcm, 4 + 4 * i); }
	pc0 = n2scp_rd(n2scp_c0, 0x80);
	n2scp_sleep(20);
	pc1 = n2scp_rd(n2scp_c0, 0x80);
	printf("NORD2 SCP: step 1: core0 status %#x PC %#x/%#x LR %#x rstn %#x | core1 status %#x PC %#x | clk_cfg_0 %#x\n",
	    n2scp_rd(n2scp_c0, 0x70), pc0, pc1, n2scp_rd(n2scp_c0, 0x84), n2scp_rd(n2scp_c0, 0x4),
	    n2scp_rd(n2scp_c1, 0x70), n2scp_rd(n2scp_c1, 0x80), n2scp_rd(n2scp_topck, 0x10));
	printf("NORD2 SCP: step 1: GPR %#x %#x %#x %#x %#x %#x %#x %#x | TCM[0] %#x %#x\n",
	    n2scp_rd(n2scp_c0, 0x40), n2scp_rd(n2scp_c0, 0x44), n2scp_rd(n2scp_c0, 0x48), n2scp_rd(n2scp_c0, 0x4c),
	    n2scp_rd(n2scp_c0, 0x50), n2scp_rd(n2scp_c0, 0x54), n2scp_rd(n2scp_c0, 0x58), n2scp_rd(n2scp_c0, 0x5c),
	    n2scp_rd(n2scp_tcm, 0), n2scp_rd(n2scp_tcm, 0x800));
	printf("NORD2 SCP: step 1: region info loader %#x+%#x fw %#x+%#x dram %#x+%#x backup %#x size %u uart %u ctx %#x "
	    "scpctl %#x regdump %#x+%#x op %u pcb %u proj %u sensor_data %#x\n", ri[0], ri[1], ri[2], ri[3], ri[4], ri[5],
	    ri[6], ri[7], ri[8], ri[9], ri[10], ri[11], ri[12], ri[13], ri[14], ri[15], ri[16]);
	printf("NORD2 SCP: step 1: mbox set/clr %#x/%#x %#x/%#x %#x/%#x %#x/%#x %#x/%#x\n",
	    n2scp_rd(n2scp_mb[0], 0x100), n2scp_rd(n2scp_mb[0], 0x10c), n2scp_rd(n2scp_mb[1], 0x100), n2scp_rd(n2scp_mb[1], 0x10c),
	    n2scp_rd(n2scp_mb[2], 0x100), n2scp_rd(n2scp_mb[2], 0x10c), n2scp_rd(n2scp_mb[3], 0x100), n2scp_rd(n2scp_mb[3], 0x10c),
	    n2scp_rd(n2scp_mb[4], 0x100), n2scp_rd(n2scp_mb[4], 0x10c));
	/* a loaded image: the region info says where LK put it */
	return ri[2] != 0 && ri[3] != 0 && ri[3] < 0x10000000u;
}

/* -------- IPI -------- */
static bool
n2scp_awake(int unlock)
{
	bool ok = false;
	n2scp_wr(n2scp_infra, 0xb14, 0xA0u | (1u << (unlock ? 1 : 0)));
	for (int n = 0; n < 20000; n++) {
		uint32_t v = n2scp_rd(n2scp_infra, 0xb14);
		if ((v & 0xE0) != 0xA0) { break; }
		if (!(v & (1u << (unlock ? 1 : 0)))) { ok = true; break; }
		delay(10);
	}
	n2scp_wr(n2scp_infra, 0xb18, 0xA0u | (1u << (unlock ? 1 : 0)));
	return ok;
}

/* one legacy IPI: {id, length, payload} into mbox2 MPOOL_0 (slot 0, pin 0); whether the SCP took it */
static bool
n2scp_send(uint32_t id, const void *payload, uint32_t len)
{
	const uint8_t *p = payload;
	bool taken = false;

	if (len > (34 - 2) * 4) { return false; }
	(void)n2scp_awake(0);
	for (int n = 0; n < 10000 && (n2scp_rd(n2scp_mb[2], 0x100) & 1); n++) { delay(10); }
	if (!(n2scp_rd(n2scp_mb[2], 0x100) & 1)) {
		n2scp_wr(n2scp_mb[2], 0, id);
		n2scp_wr(n2scp_mb[2], 4, len);
		for (uint32_t i = 0; i < len; i += 4) {
			uint32_t w = 0;
			for (uint32_t k = 0; k < 4 && i + k < len; k++) { w |= (uint32_t)p[i + k] << (8 * k); }
			n2scp_wr(n2scp_mb[2], 8 + i, w);
		}
		n2scp_wr(n2scp_mb[2], 0x100, 1);
		for (int n = 0; n < 100000; n++) {
			if (!(n2scp_rd(n2scp_mb[2], 0x100) & 1)) { taken = true; break; }
			delay(10);
		}
	}
	(void)n2scp_awake(1);
	return taken;
}

static const uint32_t n2scp_crc_tab[16] = {
	0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005,
	0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61, 0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD,
};
static uint32_t
n2scp_crc(const uint8_t *b, uint32_t len)
{
	uint32_t crc = ~0u;
	for (uint32_t i = 0; i < len; i += 4) {
		uint32_t w = 0;
		for (uint32_t k = 0; k < 4 && i + k < len; k++) { w |= (uint32_t)b[i + k] << (8 * k); }
		crc ^= w;
		for (int n = 0; n < 8; n++) { crc = (crc << 4) ^ n2scp_crc_tab[crc >> 28]; }
	}
	return crc;
}

/* nanohub ConfigCmd: enable/disable `type` at `hz` (rate = Hz * 1024; 0xFFFFFF01 on-change) */
static bool
n2scp_config(uint8_t type, int enable, uint32_t rate)
{
	uint8_t pkt[10 + 20 + 4];
	uint32_t seq = n2scp_seq++, crc;

	memset(pkt, 0, sizeof(pkt));
	pkt[0] = 0x31;
	memcpy(pkt + 1, &seq, 4);
	pkt[5] = 0x91; pkt[6] = 0x10;
	pkt[9] = 20;
	pkt[10] = 0x00; pkt[11] = 0x03;            /* EVT_NO_SENSOR_CONFIG_EVENT 0x300 */
	memcpy(pkt + 10 + 12, &rate, 4);
	pkt[10 + 16] = type;
	pkt[10 + 17] = (uint8_t)(enable < 0 ? 0 : enable);    /* 0762: 0 disable, 1 enable, 2 flush (nanohub CONFIG_CMD_*) */
	crc = n2scp_crc(pkt, 30);
	memcpy(pkt + 30, &crc, 4);
	bool ok = n2scp_send(N2SCP_IPI_CHRE, pkt, sizeof(pkt));
	printf("NORD2 SCP: config sensor %u %s rate %u: %s\n", type, enable == 0 ? "off" : enable == 1 ? "on" : "cmd 2 (flush)", rate,
	    ok ? "taken" : "NOT TAKEN");
	return ok;
}

/*
 * OPLUS calibration, as sensor_devinfo sends it right after the hub is up (ConfigCmd cmd 3 CONFIG_CMD_CFG_DATA, 12 int32
 * of data): without it the TCS3701 reported proximity once and never again, and light not at all (q10ev). Values of
 * this phone, read by Android's sensor_devinfo from the OPLUS custom partition (rescue dmesg, 8 Nov):
 * acc[0, 0, 0], gyro[0, 0, 0], ps[0, 0, 0,0,0,0], als factor 959.
 */
static bool
n2scp_cfg_data(uint8_t type, const int32_t *vals, int n)
{
	uint8_t pkt[10 + 20 + 48 + 4];
	uint32_t seq = n2scp_seq++, crc;
	int32_t data[12];

	memset(data, 0, sizeof(data));
	for (int i = 0; i < n && i < 12; i++) { data[i] = vals[i]; }
	memset(pkt, 0, sizeof(pkt));
	pkt[0] = 0x31;
	memcpy(pkt + 1, &seq, 4);
	pkt[5] = 0x91; pkt[6] = 0x10;
	pkt[9] = 20 + 48;
	pkt[10] = 0x00; pkt[11] = 0x03;
	pkt[10 + 16] = type;
	pkt[10 + 17] = 3;                          /* CONFIG_CMD_CFG_DATA */
	memcpy(pkt + 30, data, 48);
	crc = n2scp_crc(pkt, 78);
	memcpy(pkt + 78, &crc, 4);
	bool ok = n2scp_send(N2SCP_IPI_CHRE, pkt, sizeof(pkt));
	printf("NORD2 SCP: calibration for sensor %u (%d %d %d ...): %s\n", type, data[0], data[1], data[2], ok ? "taken" : "NOT TAKEN");
	return ok;
}

/* the SCP's messages: mbox1 (ready, errors) and mbox2 MPOOL (sensor hub, nanohub); `say` logs them */
static int
n2scp_take(bool say)
{
	int got = 0;
	uint32_t p1 = n2scp_rd(n2scp_mb[1], 0x10c), p2 = n2scp_rd(n2scp_mb[2], 0x10c);

	if (p1) {
		if (say) {
			printf("NORD2 SCP: mbox1 pins %#x: ready slot %#x | error info %#x %#x %#x %#x\n", p1, n2scp_rd(n2scp_mb[1], 20 * 4),
			    n2scp_rd(n2scp_mb[1], 10 * 4), n2scp_rd(n2scp_mb[1], 11 * 4), n2scp_rd(n2scp_mb[1], 12 * 4), n2scp_rd(n2scp_mb[1], 13 * 4));
		}
		if (p1 & (1u << 10)) { n2scp_state |= 1; }
		n2scp_wr(n2scp_mb[1], 0x10c, p1);
		got++;
	}
	{       /* mbox3: core 1 (logger wakeup bit 15, error info from bit 10, ...): said and taken */
		uint32_t p3 = n2scp_rd(n2scp_mb[3], 0x10c);
		if (p3) {
			static unsigned said3;
			if (say || said3 < 20) {
				said3++;
				printf("NORD2 SCP: mbox3 pins %#x: slot10 %#x %#x %#x %#x | slot24 %#x %#x | slot30 %#x %#x %#x %#x %#x\n", p3,
				    n2scp_rd(n2scp_mb[3], 40), n2scp_rd(n2scp_mb[3], 44), n2scp_rd(n2scp_mb[3], 48), n2scp_rd(n2scp_mb[3], 52),
				    n2scp_rd(n2scp_mb[3], 96), n2scp_rd(n2scp_mb[3], 100), n2scp_rd(n2scp_mb[3], 120), n2scp_rd(n2scp_mb[3], 124),
				    n2scp_rd(n2scp_mb[3], 128), n2scp_rd(n2scp_mb[3], 132), n2scp_rd(n2scp_mb[3], 136));
			}
			n2scp_wr(n2scp_mb[3], 0x10c, p3);
			got++;
		}
	}
	if (p2 & (1u << 17)) {
		uint32_t id = n2scp_rd(n2scp_mb[2], 34 * 4), len = n2scp_rd(n2scp_mb[2], 35 * 4), d0 = n2scp_rd(n2scp_mb[2], 36 * 4);
		n2scp_msgs++;
		static unsigned said_sensor_msg;     /* 0761: hub replies on IPI_SENSOR (e.g. to SET_LCD_INFO) after the first 40 */
		if (id == N2SCP_IPI_SENSOR && ((d0 >> 8) & 0xff) != 6) {   /* 0762: a reply to one of our requests: keep and log all 12 words */
			for (int i = 0; i < 12; i++) { n2scp_rsp[i] = n2scp_rd(n2scp_mb[2], (36u + (uint32_t)i) * 4u); }
			n2scp_rsp_seq++;
			static unsigned said_rsp;
			if (said_rsp++ < 300) {
				printf("NORD2 SCP: reply sensor %u action %u err %u: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", d0 & 0xff,
				    (d0 >> 8) & 0xff, (d0 >> 16) & 0xff, n2scp_rsp[1], n2scp_rsp[2], n2scp_rsp[3], n2scp_rsp[4], n2scp_rsp[5], n2scp_rsp[6],
				    n2scp_rsp[7], n2scp_rsp[8], n2scp_rsp[9], n2scp_rsp[10], n2scp_rsp[11]);
			}
		} else if (id == N2SCP_IPI_SENSOR && (d0 & 0xff) != 0 && (d0 & 0xff) != 7) {   /* 0762: a push for another sensor (light = 4) */
			static unsigned said_push;
			if (said_push++ < 40) { printf("NORD2 SCP: push notify for sensor %u event %u wp %#x\n", d0 & 0xff, (d0 >> 16) & 0xff, n2scp_rd(n2scp_mb[2], 38 * 4)); }
		}
		if (say || n2scp_msgs <= 40 || (id == N2SCP_IPI_SENSOR && said_sensor_msg++ < 60)) {
			printf("NORD2 SCP: msg %u id %u len %u: %08x %08x %08x %08x %08x %08x\n", n2scp_msgs, id, len, d0,
			    n2scp_rd(n2scp_mb[2], 37 * 4), n2scp_rd(n2scp_mb[2], 38 * 4), n2scp_rd(n2scp_mb[2], 39 * 4),
			    n2scp_rd(n2scp_mb[2], 40 * 4), n2scp_rd(n2scp_mb[2], 41 * 4));
		}
		/* sensor hub: byte 1 the action, byte 2 the notify event (SCP_INIT_DONE 0) */
		if (id == N2SCP_IPI_SENSOR && ((d0 >> 8) & 0xff) == 6 && ((d0 >> 16) & 0xff) == 0) { n2scp_state |= 2; }
		n2scp_wr(n2scp_mb[2], 0x10c, 1u << 17);
		if (id == N2SCP_IPI_CHRE) { (void)n2scp_send(N2SCP_IPI_CHREX, &len, 4); }
		got++;
	} else if (p2) {
		if (say) { printf("NORD2 SCP: mbox2 unexpected pins %#x\n", p2); }
		n2scp_wr(n2scp_mb[2], 0x10c, p2);
	}
	return got;
}

static void
n2scp_wait(uint32_t ms, uint32_t want)
{
	for (uint32_t t = 0; t < ms && (n2scp_state & want) != want; t += 10) {
		if (!n2scp_take(true)) { n2scp_sleep(10); }
	}
}

/* -------- the FIFO -------- */
static void
n2scp_record(const volatile uint8_t *r)
{
	uint8_t type = r[0];
	if (type < 16) { n2scp_count[type]++; }
	int32_t a = *(const volatile int32_t *)(r + 12), b = *(const volatile int32_t *)(r + 16), c = *(const volatile int32_t *)(r + 20);

	n2scp_records++;
	switch (type) {
	case N2H_PROX: {
		{
			const volatile uint32_t *w = (const volatile uint32_t *)(r + 12);
			static unsigned said7;
			if (said7++ < 200) {
				printf("NORD2 SCP: prox raw #%u: %d %d %d %d %d %d %d %d\n", said7, (int)w[0], (int)w[1], (int)w[2], (int)w[3], (int)w[4],
				    (int)w[5], (int)w[6], (int)w[7]);
			}
		}
		/* "near" reaches iOS only once the sensor has shown it can say "far": without OPLUS' calibration a sensor stuck at
		 * "near" would leave the touch screen ignored for every call (the q10eb..q10ed freeze) */
		static int seen_far;
		int far = b == 0;       /* q10ey: field 1 is a counter, field 2 the state: 1 covered (near), 0 clear (far) */
		if (far) { seen_far = 1; }
		if (far != n2scp_prox) {
			printf("NORD2 SCP: proximity %s (fields %d %d)%s\n", far ? "far" : "NEAR", a, b, (!far && !seen_far) ? " - not passed on (never far yet)" : "");
			n2scp_prox = far;
			if (far || seen_far) { sweet_hid_proximity(!far); }
		}
		break;
	}
	case N2H_LIGHT:
		if (n2scp_count[4] == 1) { printf("NORD2 SCP: light first sample lux %d (fifo record, fields %d %d)\n", a, b, c); }   /* 0762 */
		if (n2scp_lux < 0 || a > n2scp_lux * 2 || a < n2scp_lux / 2) { printf("NORD2 SCP: light %d lux\n", a); }
		n2scp_lux = a;
		sweet_hid_ambient(a);
		break;
	case N2H_ACCEL: n2scp_acc[0] = a; n2scp_acc[1] = b; n2scp_acc[2] = c; n2scp_motion_out(0, a, b, c); break;
	case N2H_GYRO: n2scp_gyr[0] = a; n2scp_gyr[1] = b; n2scp_gyr[2] = c; n2scp_motion_out(1, a, b, c); break;
	case N2H_MAG: n2scp_magv[0] = a; n2scp_magv[1] = b; n2scp_magv[2] = c; n2scp_motion_out(2, a, b, c); break;
	default: {
		static unsigned said_other;     /* 0761: records of types we do not route (e.g. an OPLUS light type) */
		if (said_other++ < 40) {
			printf("NORD2 SCP: record of type %u (not routed) #%u: %d %d %d\n", type, said_other, a, b, c);
		}
		break;
	}
	}
	if (n2scp_records <= 16 || (n2scp_records % 2000) == 0) {
		printf("NORD2 SCP: record %u type %u: %d %d %d\n", n2scp_records, type, a, b, c);
	}
}

static void
n2scp_drain(void)
{
	volatile uint32_t *h = (volatile uint32_t *)n2scp_dram + N2SCP_FIFO_OFF / 4;
	uint32_t size = h[2], rp = h[0], wp = h[1];
	const volatile uint8_t *data = (const volatile uint8_t *)(h + 4);

	if (size == 0 || size > N2SCP_FIFO_SIZE - 16 || rp >= size || wp >= size) { return; }
	while (rp != wp) {
		n2scp_record(data + rp);
		rp += N2SCP_REC;
		if (rp + N2SCP_REC > size) { rp = 0; }
	}
	h[0] = rp;
}

/* -------- the thread -------- */
#ifndef N2SCP_DEFAULT_MODE
#define N2SCP_DEFAULT_MODE  2
#endif
static int n2scp_mode = N2SCP_DEFAULT_MODE;     /* 0 off, 1 read only (default since q10ee: the started SCP stalled the AP at boot), 2 all */

/*
 * 0531 (heat-20261114): the motion sensors are no longer enabled by default.  SweetHID publishes only the proximity service
 * (N2_SENSOR_MASK 1: all five made backboardd crash, q10ef), so sweet_hid_motion() dropped every accelerometer / gyroscope /
 * magnetometer record, yet the hub sampled them at 50 / 50 / 25 Hz and this thread woke every 20 ms to drain them: 156 records/s
 * and 50 mailbox messages/s for the whole uptime, display off included (heat1.log: "records 253166 msgs 80989" in 1620 s).
 * Now only proximity and light (both on-change) are enabled and the thread polls every N2SCP_IDLE_POLL_MS.
 * kern.n2scp_motion = 1 (or boot-arg n2scpm=1, or n2gdv +128) is the old behaviour (the three sensors on, 20 ms polling) and can be
 * switched at run time: the loop sends the nanohub enable / disable ConfigCmd when the value changes.
 */
#include <sys/sysctl.h>
#define N2SCP_IDLE_POLL_MS  100u
#ifndef N2SCP_MOTION_DEFAULT
#define N2SCP_MOTION_DEFAULT 2                  /* 0760: on demand */
#endif
static int n2scp_motion = N2SCP_MOTION_DEFAULT; /* 0 off; 1 always on (0531's old mode); 2 (0760 default) what iOS asks for */
SYSCTL_INT(_kern, OID_AUTO, n2scp_motion, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_motion, 0,
    "NORD2 SCP: accelerometer/gyroscope/magnetometer: 0 off, 1 always on (50/50/25 Hz), 2 on demand (iOS ReportInterval)");
SYSCTL_INT(_kern, OID_AUTO, n2scp_axflip, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_axflip, 0,
    "NORD2 SCP 0760: negate axes handed to iOS: bits 0-2 accel x/y/z, 3-5 gyro, 6-8 compass");
static int n2scp_lightrate = 5 * 1024;          /* 0760: Hz * 1024; 0 = 0284's on-change request */
SYSCTL_INT(_kern, OID_AUTO, n2scp_lightrate, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_lightrate, 0,
    "NORD2 SCP 0760: light sensor rate (Hz*1024, 0 = on-change as 0284); re-sent to the hub on change");
static int n2scp_alslcd;                        /* 0760: > 0 = send the OPLUS LCD info (brightness) once */
SYSCTL_INT(_kern, OID_AUTO, n2scp_alslcd, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_alslcd, 0,
    "NORD2 SCP 0760: write a brightness (1..2047) to send OPLUS SET_LCD_INFO for the light sensor once");
static int n2scp_rate_now[3];                   /* 0760: Hz the hub was last told per kind (acc, gyro, mag) */
SYSCTL_INT(_kern, OID_AUTO, n2scp_orient, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_orient, 0,
    "NORD2 SCP 0761: 1 = device orientation events from the accelerometer (0x20/0x8A service), 0 = none");
SYSCTL_INT(_kern, OID_AUTO, n2scp_orient_force, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_orient_force, 0,
    "NORD2 SCP 0761: -1 = detector; 0..6 = send this UIDeviceOrientation (1 portrait 2 upside down 3 landscape left 4 right 5 up 6 down)");
static int n2scp_lighttype = N2S_LIGHT;         /* 0761: ConfigCmd sensor type used for light */
SYSCTL_INT(_kern, OID_AUTO, n2scp_lighttype, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_lighttype, 0,
    "NORD2 SCP 0761: sensor type of the light enable (5 Android light; re-sent on change, the old type is disabled)");
static int n2scp_alslcd_auto = 1023;            /* 0761: brightness sent with SET_LCD_INFO while light is silent (0 = never) */
SYSCTL_INT(_kern, OID_AUTO, n2scp_alslcd_auto, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_alslcd_auto, 0,
    "NORD2 SCP 0761: brightness for OPLUS SET_LCD_INFO sent after the light enable and every 20 s (6 times) while no light record came; 0 = never");
static int
n2scp_sysctl_raw SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[1100], hid[300];
	hid[0] = 0;
	sweet_hid_sensor_status(hid, (int)sizeof(hid));
	/* integers only: g/rad/s/uT as value*1000 (milli units) */
	int64_t m[3][3];
	for (int k = 0; k < 3; k++) { for (int i = 0; i < 3; i++) { m[k][i] = (int64_t)n2scp_ios[k][i] * 1000 / 65536; } }
	int n = snprintf(buf, sizeof(buf),
	    "state %#x mode %d | acc raw %d %d %d (mm/s2) ios_mg %lld %lld %lld | gyr raw %d %d %d (urad/s) ios_mrad %lld %lld %lld | "
	    "mag raw %d %d %d (10nT) ios_muT %lld %lld %lld | lux %d prox %d | hz acc %d gyr %d mag %d light %d | counts acc %u gyr %u mag %u "
	    "light %u prox %u out %u %u %u | records %u msgs %u | axflip %#x | orient %d (changes %u, mode %d force %d) lighttype %d | lightpoll %d polled %d (%u) rsp#%u %08x %08x %08x | %s",
	    n2scp_state, n2scp_motion, n2scp_acc[0], n2scp_acc[1], n2scp_acc[2], m[0][0], m[0][1], m[0][2],
	    n2scp_gyr[0], n2scp_gyr[1], n2scp_gyr[2], m[1][0], m[1][1], m[1][2], n2scp_magv[0], n2scp_magv[1], n2scp_magv[2],
	    m[2][0], m[2][1], m[2][2], n2scp_lux, n2scp_prox, n2scp_rate_now[0], n2scp_rate_now[1], n2scp_rate_now[2],
	    n2scp_lightrate, n2scp_count[0], n2scp_count[3], n2scp_count[1], n2scp_count[4], n2scp_count[7], n2scp_out[0], n2scp_out[1],
	    n2scp_out[2], n2scp_records, n2scp_msgs, n2scp_axflip, n2scp_orient_now, n2scp_orient_changes, n2scp_orient, n2scp_orient_force,
	    n2scp_lighttype, n2scp_lightpoll, n2scp_lightpolled, n2scp_lightpolls, n2scp_rsp_seq, n2scp_rsp[0], n2scp_rsp[1], n2scp_rsp[2], hid);
	if (n < 0) { n = 0; }
	if (n >= (int)sizeof(buf)) { n = (int)sizeof(buf) - 1; }
	return SYSCTL_OUT(req, buf, (size_t)n + 1);
}
SYSCTL_PROC(_kern, OID_AUTO, n2scp_raw, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, n2scp_sysctl_raw, "A",
    "NORD2 SCP 0760: last raw and iOS-unit samples, rates, counters, HID service state");

/* 0760: OPLUS SET_LCD_INFO {sensor 4 (light), action 117, data brightness << 16 | dc mode} on IPI_SENSOR, as sensor_devinfo does */
static void
n2scp_lcd_info(uint32_t brightness)
{
	uint32_t req[8] = { (117u << 8) | 4u, (brightness & 0xffffu) << 16, 0, 0, 0, 0, 0, 0 };
	printf("NORD2 SCP: light LCD info brightness %u: %s\n", brightness, n2scp_send(N2SCP_IPI_SENSOR, req, sizeof(req)) ? "taken" : "NOT TAKEN");
}

/* 0762: SCP_SENSOR_HUB_SET_CUST (action 5) {sensor, 5, -, -, cust action} and wait <= 100 ms for its reply; true = a reply came */
static bool
n2scp_cust(uint32_t sensor, uint32_t cust)
{
	uint32_t req[8] = { (5u << 8) | (sensor & 0xff), cust, 0, 0, 0, 0, 0, 0 };
	uint32_t seq0 = n2scp_rsp_seq;
	if (!n2scp_send(N2SCP_IPI_SENSOR, req, sizeof(req))) { return false; }
	for (int i = 0; i < 20 && n2scp_rsp_seq == seq0; i++) {
		if (!n2scp_take(false)) { n2scp_sleep(5); }
	}
	return n2scp_rsp_seq != seq0 && (n2scp_rsp[0] & 0xff) == (sensor & 0xff) && ((n2scp_rsp[0] >> 8) & 0xff) == 5;
}
static void
n2scp_cust_info(uint32_t sensor)
{
	if (!n2scp_cust(sensor, 12)) {          /* CUST_ACTION_GET_SENSOR_INFO */
		printf("NORD2 SCP: sensor %u info: no reply\n", sensor);
		return;
	}
	char name[17];
	for (int i = 0; i < 16; i++) { name[i] = (char)((n2scp_rsp[2 + i / 4] >> (8 * (i % 4))) & 0xff); }
	name[16] = 0;
	for (int i = 0; i < 16; i++) { if (name[i] && (name[i] < 0x20 || name[i] > 0x7e)) { name[i] = '?'; } }
	printf("NORD2 SCP: sensor %u info: err %u name \"%s\"\n", sensor, (n2scp_rsp[0] >> 16) & 0xff, name);
}
/* 0762: light raw data request (CUST_ACTION_GET_RAW_DATA 7); -1 = no / failed reply */
static int64_t
n2scp_light_raw(void)
{
	if (!n2scp_cust(4, 7) || ((n2scp_rsp[0] >> 16) & 0xff) != 0 || n2scp_rsp[1] != 7) { return -1; }
	return (int64_t)n2scp_rsp[2];
}
SYSCTL_INT(_kern, OID_AUTO, n2scp_lightpoll, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_lightpoll, 0,
    "NORD2 SCP 0762: 1 = if no light record arrives, poll CUST GET_RAW_DATA for light once a second and hand the value to iOS as lux; 0 = never");
static int n2scp_summary_s = 30;                /* 0762: one summary line every N s (0 = none) */
SYSCTL_INT(_kern, OID_AUTO, n2scp_summary_s, CTLFLAG_RW | CTLFLAG_LOCKED, &n2scp_summary_s, 0,
    "NORD2 SCP 0762: seconds between the 'sensors summary' log lines (0 = off)");

/* 0760: the rate iOS wants per kind, clamped to what the 0531 mode did (acc/gyro 50, mag 25 when forced) */
static int
n2scp_motion_target(int kind)
{
	static const int forced[3] = { 50, 50, 25 };
	int mode = n2scp_motion;
	if (mode == 1) { return forced[kind]; }
	if (mode != 2) { return 0; }
	int hz = sweet_hid_motion_want(kind);
	if (kind == 0 && n2scp_orient) {        /* 0761: the orientation detector needs the accelerometer while its service is wanted */
		int ho = sweet_hid_motion_want(3);
		if (ho > hz) { hz = ho; }
	}
	if (hz <= 0) { return 0; }
	/* a few steps only, so small changes of the asked interval do not re-send the enable */
	hz = hz <= 5 ? 5 : hz <= 10 ? 10 : hz <= 25 ? 25 : hz <= 50 ? 50 : 100;
	if (kind == 2 && hz > 50) { hz = 50; }   /* MMC5603 */
	return hz;
}

static void
n2scp_thread(void *arg __unused, wait_result_t res __unused)
{
	/* after the Wi-Fi bring-up has taken its (large) I/O maps: q10en..q10eq died at ~30-50 s when ours came first */
	printf("NORD2 SCP: mapping in %u s\n", N2SCP_MAP_DELAY_S);
	n2scp_sleep(N2SCP_MAP_DELAY_S * 1000);
	if (!n2scp_map()) { printf("NORD2 SCP: map failed\n"); goto out; }
	/* the FIFO window now, while the I/O map has room; n2scp_dram points N2SCP_FIFO_OFF before it so offsets stay those of the region */
	{
		vm_offset_t fifo = n2scp_iomap(N2SCP_DRAM_PHYS + N2SCP_FIFO_OFF, N2SCP_FIFO_SIZE, "FIFO"); (void)N2SCP_FIFO_PHYS_TEST;
		n2scp_dram = fifo ? fifo - N2SCP_FIFO_OFF : 0;
		printf("NORD2 SCP: FIFO (test location) %#x+%#x mapped %s\n", N2SCP_FIFO_PHYS_TEST, N2SCP_FIFO_SIZE, fifo ? "ok" : "FAILED");
	}
	bool loaded = n2scp_step1();
	for (int i = 0; i < 9000 && !n2scp_pmic_done; i++) { n2scp_sleep(10); }
	printf("NORD2 SCP: step 1: PMIC %s: VGPU11 CON0 %#x OP_EN %#x DBG0(vosel out) %#x SSHUB_CON0 %#x (en %u vosel %#x) ELR0(AP vosel) %#x "
	    "PMRC_CON1 %#x (sshub mode %u) VSRAM_OTHERS_SSHUB %#x\n", n2scp_pmic_done > 0 ? "read" : "NOT read", n2scp_pmic_val[0],
	    n2scp_pmic_val[1], n2scp_pmic_val[2], n2scp_pmic_val[3], n2scp_pmic_val[3] & 1, (n2scp_pmic_val[3] >> 4) & 0x7f, n2scp_pmic_val[4],
	    n2scp_pmic_val[5], (n2scp_pmic_val[5] >> 2) & 1, n2scp_pmic_val[6]);
	if (n2scp_mode < 2 || !loaded || N2SCP_DRAM_PHYS == 0) {
		printf("NORD2 SCP: stop after step 1 (mode %d, image %s, DRAM %#x)\n", n2scp_mode, loaded ? "loaded" : "NOT loaded",
		    N2SCP_DRAM_PHYS);
		goto out;
	}
	if (n2scp_rd(n2scp_c0, 0x80) != 0 && (n2scp_rd(n2scp_c0, 0x70) & 1) == 0) {
		printf("NORD2 SCP: core0 already runs: not restarted\n");
	} else {
		/* after userland is up (the q10ee stall came seconds after the release, during the boot) */
		printf("NORD2 SCP: step 2 in %u s\n", N2SCP_START_DELAY_S);
		for (unsigned t = 0; t < N2SCP_START_DELAY_S; t += 10) {
			n2scp_sleep(10000);
			printf("NORD2 SCP: waiting, %u s to go\n", N2SCP_START_DELAY_S - t - 10);
		}
		printf("NORD2 SCP: 2-: mapping the DRAM %#x+%#x next\n", N2SCP_DRAM_PHYS, N2SCP_DRAM_SIZE); n2scp_sleep(3000);
		if (!n2scp_dram) { printf("NORD2 SCP: FIFO map failed\n"); goto out; }
		/* the FIFO header first (the hub writes records after it) */
		volatile uint32_t *h = (volatile uint32_t *)n2scp_dram + N2SCP_FIFO_OFF / 4;
		printf("NORD2 SCP: 2a: DRAM mapped, writing the FIFO header\n"); n2scp_sleep(3000);
		h[0] = 0; h[1] = 0; h[2] = (N2SCP_FIFO_SIZE - 16) / N2SCP_REC * N2SCP_REC; h[3] = 0;
		__asm__ volatile("dsb sy" ::: "memory");
		printf("NORD2 SCP: 2b: FIFO header written (%#x), the clock next\n", h[2]); n2scp_sleep(3000);
		/* clock: scp_sel on clk26m, ungated */
		uint32_t before = n2scp_rd(n2scp_topck, 0x10);
		n2scp_wr(n2scp_topck, 0x18, (7u << 16) | (1u << 23));
		n2scp_wr(n2scp_topck, 0x04, 1u << 2);
		delay(100);
		printf("NORD2 SCP: 2c: clock %#x -> %#x, GPR1/2 next\n", before, n2scp_rd(n2scp_topck, 0x10)); n2scp_sleep(3000);
		/* the AP DRAM it may use (MPU window), then core 0 runs */
		n2scp_wr(n2scp_c0, 0x44, N2SCP_DRAM_PHYS);
		n2scp_wr(n2scp_c0, 0x48, N2SCP_DRAM_SIZE);
		printf("NORD2 SCP: 2d: GPR1/2 %#x %#x, releasing core 0 now\n", n2scp_rd(n2scp_c0, 0x44), n2scp_rd(n2scp_c0, 0x48)); n2scp_sleep(3000);
		n2scp_wr(n2scp_c0, 0x0, 1);
		{       /* the first 50 ms, sampled each ms, said in one line */
			uint32_t pcs[10];
			for (int k = 0; k < 10; k++) { pcs[k] = n2scp_rd(n2scp_c0, 0x80); delay(5000); }
			printf("NORD2 SCP: 2e: released; PC %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x\n", pcs[0], pcs[1], pcs[2], pcs[3], pcs[4], pcs[5],
			    pcs[6], pcs[7], pcs[8], pcs[9]);
		}
		printf("NORD2 SCP: step 2: clk_cfg_0 %#x -> %#x, DRAM %#x+%#x, core0 released (rstn %#x)\n", before,
		    n2scp_rd(n2scp_topck, 0x10), N2SCP_DRAM_PHYS, N2SCP_DRAM_SIZE, n2scp_rd(n2scp_c0, 0x0));
	}
	/* heartbeat: what the SCP does, every 200 ms for 6 s (the AP's last line says when it stopped) */
	for (int hb = 0; hb < 30; hb++) {
		(void)n2scp_take(true);
		printf("NORD2 SCP: hb %d: c0 st %#x pc %#x lr %#x | c1 st %#x pc %#x | gpr %#x %#x %#x %#x %#x %#x | to_spm %#x | mbox1 %#x mbox2 %#x mbox3 %#x\n",
		    hb, n2scp_rd(n2scp_c0, 0x70), n2scp_rd(n2scp_c0, 0x80), n2scp_rd(n2scp_c0, 0x84), n2scp_rd(n2scp_c1, 0x70), n2scp_rd(n2scp_c1, 0x80),
		    n2scp_rd(n2scp_c0, 0x40), n2scp_rd(n2scp_c0, 0x4c), n2scp_rd(n2scp_c0, 0x50), n2scp_rd(n2scp_c0, 0x54), n2scp_rd(n2scp_c0, 0x58),
		    n2scp_rd(n2scp_c0, 0x5c), n2scp_rd(n2scp_cfg, 0x94), n2scp_rd(n2scp_mb[1], 0x10c), n2scp_rd(n2scp_mb[2], 0x10c),
		    n2scp_rd(n2scp_mb[3], 0x10c));
		n2scp_sleep(hb < 10 ? 50 : 200);
	}
	n2scp_wait(20000, 1);
	printf("NORD2 SCP: step 3: %s (core0 status %#x PC %#x, GPR4 freq %u)\n", (n2scp_state & 1) ? "READY" : "no ready message",
	    n2scp_rd(n2scp_c0, 0x70), n2scp_rd(n2scp_c0, 0x80), n2scp_rd(n2scp_c0, 0x50));
	if (!(n2scp_state & 3)) { goto out; }      /* neither ready nor the hub's SCP_INIT_DONE */
	n2scp_wait(5000, 2);
	printf("NORD2 SCP: step 4: sensor hub %s\n", (n2scp_state & 2) ? "SCP_INIT_DONE" : "silent (continuing)");
	{
		uint32_t req[8] = { 4u << 8, N2SCP_DRAM_PHYS + N2SCP_FIFO_OFF, N2SCP_FIFO_SIZE, 0, 0, 0, 0, 0 };   /* SET_CONFIG */
		printf("NORD2 SCP: SET_CONFIG FIFO %#x: %s\n", req[1], n2scp_send(N2SCP_IPI_SENSOR, req, sizeof(req)) ? "taken" : "NOT TAKEN");
		n2scp_wait(500, 0);
		for (int i = 0; i < 50; i++) { (void)n2scp_take(true); n2scp_sleep(10); }
	}
	{
		static const int32_t acc[6] = { 0, 0, 0 }, gyr[6] = { 0, 0, 0 }, ps[6] = { 0, 0, 0, 0, 0, 0 }, als[1] = { 959 };
		(void)n2scp_cfg_data(N2S_ACCEL, acc, 6); for (int i = 0; i < 10; i++) { (void)n2scp_take(true); n2scp_sleep(10); }
		(void)n2scp_cfg_data(N2S_GYRO, gyr, 6); for (int i = 0; i < 10; i++) { (void)n2scp_take(true); n2scp_sleep(10); }
		(void)n2scp_cfg_data(N2S_PROX, ps, 6); for (int i = 0; i < 10; i++) { (void)n2scp_take(true); n2scp_sleep(10); }
		(void)n2scp_cfg_data(N2S_LIGHT, als, 1); for (int i = 0; i < 10; i++) { (void)n2scp_take(true); n2scp_sleep(10); }
	}
	n2scp_vibr_want = 1;
	for (int i = 0; i < 100 && n2scp_vibr_done < 0; i++) { n2scp_sleep(10); }
	(void)n2scp_config(N2S_PROX, 1, 0xFFFFFF01u);
	int light_rate = n2scp_lightrate;       /* 0760: what the hub was last told for light */
	int light_type = n2scp_lighttype;       /* 0761 */
	(void)n2scp_config((uint8_t)light_type, 1, light_rate > 0 ? (uint32_t)light_rate : 0xFFFFFF01u);
	uint32_t lcd_tries = 0, lcd_next_ms = 1000;     /* 0761: SET_LCD_INFO 1 s after the enable, then every 20 s while light is silent */
	n2scp_state |= 4;
	printf("NORD2 SCP: step 5: proximity and light (rate %d) enabled; motion sensors mode %d (kern.n2scp_motion: 0 off, 1 on, 2 on demand)\n",
	    light_rate, n2scp_motion);
	static const uint8_t mtype[3] = { N2S_ACCEL, N2S_GYRO, N2S_MAG };
	static const char *const mname[3] = { "accelerometer", "gyroscope", "magnetometer" };
	for (uint32_t ms = 0, say = 0;;) {
		int motion = 0;
		for (int k = 0; k < 3; k++) {   /* 0760: enable / disable / new rate only on a change (nothing is sent while nobody asks) */
			int hz = n2scp_motion_target(k);
			if (hz != n2scp_rate_now[k]) {
				(void)n2scp_config(mtype[k], hz > 0, (uint32_t)(hz > 0 ? hz : n2scp_rate_now[k]) * 1024u);
				static uint32_t said_rate;
				if (said_rate++ < 200 || (said_rate % 100) == 0) {
					printf("NORD2 SCP: %s %d Hz -> %d Hz (mode %d, change %u)\n", mname[k], n2scp_rate_now[k], hz, n2scp_motion, said_rate);
				}
				n2scp_rate_now[k] = hz;
			}
			motion |= hz > 0;
		}
		if ((n2scp_lightrate != light_rate && n2scp_lightrate >= 0) || (n2scp_lighttype != light_type && n2scp_lighttype > 0 &&
		    n2scp_lighttype < 128 && n2scp_lighttype != N2S_PROX)) {   /* 0760 rate, 0761 type */
			if (n2scp_lighttype != light_type && n2scp_lighttype > 0 && n2scp_lighttype < 128 && n2scp_lighttype != N2S_PROX) {
				(void)n2scp_config((uint8_t)light_type, 0, light_rate > 0 ? (uint32_t)light_rate : 0xFFFFFF01u);
				light_type = n2scp_lighttype;
			}
			light_rate = n2scp_lightrate >= 0 ? n2scp_lightrate : light_rate;
			(void)n2scp_config((uint8_t)light_type, 1, light_rate > 0 ? (uint32_t)light_rate : 0xFFFFFF01u);
			lcd_tries = 0; lcd_next_ms = ms + 1000;
		}
		if (n2scp_count[4] == 0 && n2scp_alslcd_auto > 0 && lcd_tries < 6 && (int32_t)(ms - lcd_next_ms) >= 0) {   /* 0761 */
			lcd_tries++;
			lcd_next_ms = ms + 20000;
			printf("NORD2 SCP: light still silent: OPLUS LCD info, try %u of 6\n", lcd_tries);
			n2scp_lcd_info((uint32_t)n2scp_alslcd_auto);
		}
		if (n2scp_orient_force != n2scp_orient_forced) {        /* 0761: forced orientation for the iOS-side test */
			n2scp_orient_forced = n2scp_orient_force;
			if (n2scp_orient_force >= 0 && n2scp_orient_force <= 6) {
				n2scp_orient_set(n2scp_orient_force, "kern.n2scp_orient_force");
			}
		}
		n2scp_rate_now_acc = n2scp_rate_now[0];
		{       /* 0762: what the hub says it has (Android's init_sensorlist_info) and one light raw read, 5 s after step 5 */
			static int asked;
			if (!asked && ms >= 5000) {
				asked = 1;
				static const uint32_t ids[6] = { 0, 1, 3, 4, 5, 7 };
				for (int i = 0; i < 6; i++) { n2scp_cust_info(ids[i]); }
				printf("NORD2 SCP: light raw read: %lld (-1 = no reply / error)\n", (long long)n2scp_light_raw());
			}
		}
		{       /* 0762: light still silent after 30 s: re-enable ladder (once each, 20 s apart): disable+enable, rate 0, on-change, flush */
			static uint32_t ladder_next = 30000;
			static int ladder;
			if (n2scp_count[4] == 0 && ladder < 4 && ms >= ladder_next) {
				ladder_next = ms + 20000;
				uint32_t rate = ladder == 1 ? 0u : ladder == 2 ? 0xFFFFFF01u : (light_rate > 0 ? (uint32_t)light_rate : 0xFFFFFF01u);
				printf("NORD2 SCP: light silent: ladder step %d (%s)\n", ladder + 1, ladder == 0 ? "disable + enable" : ladder == 1 ? "rate 0" :
				    ladder == 2 ? "on-change" : "flush");
				if (ladder == 0) { (void)n2scp_config((uint8_t)light_type, 0, rate); }
				(void)n2scp_config((uint8_t)light_type, ladder == 3 ? 2 : 1, rate);
				ladder++;
			}
		}
		if (n2scp_lightpoll && n2scp_count[4] == 0 && ms >= 25000) {     /* 0762: polled fallback, 1 Hz */
			static uint32_t poll_next, poll_fail;
			if (ms >= poll_next && poll_fail < 10) {
				poll_next = ms + 1000;
				int64_t raw = n2scp_light_raw();
				if (raw < 0) {
					poll_fail++;
					if (poll_fail == 10) { printf("NORD2 SCP: light poll: 10 failed reads, stopped\n"); }
				} else {
					poll_fail = 0;
					n2scp_lightpolls++;
					if (n2scp_lightpolled < 0) { printf("NORD2 SCP: light first sample lux %lld (polled raw value)\n", (long long)raw); }
					n2scp_lightpolled = (int32_t)raw;
					n2scp_lux = (int32_t)raw;
					sweet_hid_ambient((int)raw);
				}
			}
		}
		if (n2scp_summary_s > 0) {      /* 0762: everything in one line */
			static uint32_t sum_next = 15000;
			if (ms >= sum_next) {
				static const char *const on[7] = { "unknown", "portrait", "upside-down", "landscape-left", "landscape-right", "face-up", "face-down" };
				sum_next = ms + (uint32_t)n2scp_summary_s * 1000u;
				int o = n2scp_orient_now;
				printf("NORD2 SCP: sensors summary t=%us | acc %dHz %d %d %d mg (%u) | gyr %dHz %d %d %d mrad/s (%u) | mag %dHz %d %d %d uT (%u) | "
				    "light %d lux (fifo %u polled %u) | prox %s (%u) | orient %s (%u changes)\n", ms / 1000,
				    n2scp_rate_now[0], (int)((int64_t)n2scp_ios[0][0] * 1000 / 65536), (int)((int64_t)n2scp_ios[0][1] * 1000 / 65536),
				    (int)((int64_t)n2scp_ios[0][2] * 1000 / 65536), n2scp_count[0],
				    n2scp_rate_now[1], (int)((int64_t)n2scp_ios[1][0] * 1000 / 65536), (int)((int64_t)n2scp_ios[1][1] * 1000 / 65536),
				    (int)((int64_t)n2scp_ios[1][2] * 1000 / 65536), n2scp_count[3],
				    n2scp_rate_now[2], (int)(n2scp_ios[2][0] / 65536), (int)(n2scp_ios[2][1] / 65536), (int)(n2scp_ios[2][2] / 65536), n2scp_count[1],
				    n2scp_lux, n2scp_count[4], n2scp_lightpolls, n2scp_prox < 0 ? "none" : n2scp_prox ? "far" : "NEAR", n2scp_count[7],
				    (o >= 0 && o <= 6) ? on[o] : "none", n2scp_orient_changes);
			}
		}
		{       /* 0761: the current orientation again every 10 s (fresh sensor time), for a locationd that started after the change */
			static uint32_t orient_next;
			if (n2scp_orient_now >= 0 && n2scp_orient && (int32_t)(ms - orient_next) >= 0) {
				orient_next = ms + 10000;
				sweet_hid_orientation(n2scp_orient_now);
			}
		}
		if (n2scp_alslcd > 0) {         /* 0760: one request per write */
			uint32_t v = (uint32_t)n2scp_alslcd;
			n2scp_alslcd = 0;
			n2scp_lcd_info(v);
		}
		while (n2scp_take(false)) { }
		n2scp_drain();
		if ((int32_t)(ms - say) >= 0) { /* the report keeps its 10 s period */
			say = ms + 10000;
			volatile uint32_t *h = (volatile uint32_t *)n2scp_dram + N2SCP_FIFO_OFF / 4;
			printf("NORD2 SCP: counts acc %u mag %u gyr %u light %u prox %u | to iOS %u %u %u | hz %d %d %d light %d\n", n2scp_count[0], n2scp_count[1],
			    n2scp_count[3], n2scp_count[4], n2scp_count[7], n2scp_out[0], n2scp_out[1], n2scp_out[2], n2scp_rate_now[0], n2scp_rate_now[1],
			    n2scp_rate_now[2], light_rate);
			printf("NORD2 SCP: t=%us records %u msgs %u fifo rp %u wp %u | prox %d lux %d acc %d %d %d gyr %d %d %d mag %d %d %d | core0 %#x PC %#x\n",
			    ms / 1000, n2scp_records, n2scp_msgs, h[0], h[1], n2scp_prox, n2scp_lux, n2scp_acc[0], n2scp_acc[1], n2scp_acc[2],
			    n2scp_gyr[0], n2scp_gyr[1], n2scp_gyr[2], n2scp_magv[0], n2scp_magv[1], n2scp_magv[2],
			    n2scp_rd(n2scp_c0, 0x70), n2scp_rd(n2scp_c0, 0x80));
		}
		uint32_t step = motion ? 20u : N2SCP_IDLE_POLL_MS;
		n2scp_sleep(step);
		ms += step;
	}
out:
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

static void
nord2_scp_start(void)
{
	thread_t thread;
	int gdv = 0;
	(void)PE_parse_boot_argn("nord2scp", &n2scp_mode, sizeof(n2scp_mode));
	if (!PE_parse_boot_argn("n2scpm", &n2scp_motion, sizeof(n2scp_motion)) && PE_parse_boot_argn("n2gdv", &gdv, sizeof(gdv)) && (gdv & 128)) {
		n2scp_motion = 1;               /* 0531: the kill switch that fits the boot line */
	}
	if (n2scp_mode <= 0) { printf("NORD2 SCP: off (nord2scp=0)\n"); return; }
	if (kernel_thread_start(n2scp_thread, NULL, &thread) == KERN_SUCCESS) { thread_deallocate(thread); }
}
