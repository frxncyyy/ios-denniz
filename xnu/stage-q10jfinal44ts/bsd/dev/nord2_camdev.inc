/*
 * nord2_camdev.inc - /dev/redwood-camera for the Nord 2: HakiOS' camera ABI (hakios-xnu-kernel, branch mtk,
 * xnu/files/bsd/sys/redwood_camera.h, RWFRAME1) and the session logic of its MediaTek skeleton
 * (xnu/files/bsd/dev/redwood_mtkcam_skel.c), with three sensors instead of one:
 *   sensor 1 = the wide (main) camera IMX766, 2 = the front IMX615, 3 = the ultra-wide IMX355
 *   (the numbers of the frame header's `sensor` field; REDWOOD_CAMERA_SENSOR switches, the next read starts it).
 * A session starts when something reads and ends when nothing has for 4 s. Frames are MIPI RAW10 (4 pixels in 5
 * bytes) as the DMA writes them (CAMSV PAK RAW10; since 0289 handed out as they are, format 4). A frame is done when the last 64 bytes of its buffer are no longer
 * the fill pattern (HakiOS' rule); the DMA is then pointed at the next free buffer.
 * 0292: TWO instances, /dev/redwood-camera (node 1, default sensor 1) and /dev/redwood-camera2 (node 2, default sensor 3),
 * each with its own session thread, receiver (SENINF context and mux), cam mux, CAMSV, buffers and exposure; they can stream
 * two different sensors at the same time. The ABI is gpu/build/camera-userland-20261109/KERNEL_ABI_DUAL.md.
 * Gain over the ABI is linear, 256 = 1x (gain_unity 256); the kernel turns it into each sensor's own code (0293: n2d_info).
 */
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/uio.h>

#define REDWOOD_CAMERA_MAGIC "RWFRAME1"
struct redwood_camera_frame {
	char magic[8]; uint32_t header_bytes, width, height, stride, format, bayer, black;
	uint64_t sequence, time;
	uint32_t exposure_lines, line_ns, gain, gain_unity, gain_max, frame_lines, frame_lines_min, frame_lines_max, margin, sensor;
	uint32_t focus, focus_max;      /* 0290: the lens's position as last given to the actuator; 0 to focus_max (0: the lens is not moved) */
	uint32_t stat_samples, stat_green, stat_red, stat_blue;   /* 0292: 8x8 Bayer cells over the frame: their number, mean green/red/blue above black (10 bits) */
};
enum { REDWOOD_CAMERA_EXPOSURE = 1, REDWOOD_CAMERA_KEEP = 2, REDWOOD_CAMERA_SENSOR = 3, REDWOOD_CAMERA_FOCUS = 4, REDWOOD_CAMERA_MODE = 5 };
struct redwood_camera_command { uint32_t command, bytes, a, b, c, d; };

/*
 * 0289: four buffers (was three): one the DMA writes, one more waiting in the hardware's queue (as the stock HAL always has: it
 * enqueues a second buffer 19 ms after the enable and then one per frame), the newest finished one and the one being read.
 * The claim below 4 GB is 0xD0000000..0xD4000000 (nord2_camsv.inc: nothing carved out in 0xC0000000..0xDFE70000).
 */
#define N2D_NBUF 4
#define N2D_QUEUED 2                            /* buffers kept in the hardware's queue, the one being written included */
#define N2D_FILL 0x5a
#define N2D_IDLE_MS 4000u

/* per sensor (index into n2c_sensors): ABI number, Bayer order and black level of its preview mode, line time */
/*
 * 0293: the analog gain code is NOT the same for the three Sony sensors (their OnePlus drivers' gain2reg, gain in 1/64):
 *   IMX766  16384 - 16384 * 64 / gain   (gain = 16384 / (16384 - code), up to 64x)
 *   IMX615   4096 -  1024 * 64 / gain   (gain =  1024 / ( 4096 - code), up to 64x; code 3072 is 1x)
 *   IMX355   1024 -  1024 * 64 / gain   (gain =  1024 / ( 1024 - code), up to 16x)
 * Until 0293 all three were given the IMX355's code: the IMX766 then had 1.0x to 1.07x whatever was asked, and the IMX615
 * 0.29x to 0.33x (codes far below its 1x) - AE asked for more and more gain and nothing came (q10hm: the front camera black in
 * a dim room at "16x"; q10hf: its green did not follow the gain asked). code = gain_full - gain_num * 256 / gain (gain in 1/256).
 */
struct n2d_info { uint32_t abi, bayer, black, line_ns, frame_lines, margin, gain_max, gain_full, gain_num; };
static const struct n2d_info n2d_info[N2C_NSENSORS] = {
	/* IMX766: 4096x3072 QBIN, Bayer B first (SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_B, HV mirror) = BGGR; 15616/1488 MHz */
	[N2C_IMX766] = { 1, 3, 64, 10495, 3176, 48, 64 * 256, 16384, 16384 },
	/* IMX615: 3280x2464, R first (RAW_4CELL_HW_BAYER_R, normal) = RGGB; 3768/597.6 MHz.
	 * 0290: the mode's own frame is 2642 lines = 60 frames a second; nothing wants 60 and they double everybody's work, so the
	 * shortest frame given out is twice that, 5284 lines = 30 a second (n2d_set_exposure() writes it before the stream starts). */
	[N2C_IMX615] = { 2, 0, 64, 6305, 2 * 2642, 48, 64 * 256, 4096, 1024 },      /* 0293: max_gain 4096 / 64 = 64x in its driver */
	/* IMX355: 3264x2448, B first (RAW_B, HV mirror) = BGGR; 3672/280.8 MHz */
	[N2C_IMX355] = { 3, 3, 64, 13077, 2548, 10, 16 * 256, 1024, 1024 },
	[N2C_GC02M1B] = { 0, 0, 64, 26095, 1276, 16, 12 * 256, 1024, 1024 },
};

static LCK_GRP_DECLARE(n2d_grp, "nord2-camera");
static LCK_MTX_DECLARE(n2d_lock, &n2d_grp);    /* the instances' state */
/*
 * 0292: one lock for what the two session threads share in the hardware: the bit-banged I2C (one bus at a time: n2c_b, n2c_s),
 * the sensors' power sequences, and the read-modify-write registers of SENINF's top and cam mux. Taken per I2C transaction
 * (per register of a table), so one sensor's 2 s of tables do not hold up the other's exposure.
 */
static LCK_MTX_DECLARE(n2d_hw, &n2d_grp);
/*
 * 0294: the lock is not fair, and one instance loading a sensor's tables took it again right after every release: the other
 * instance's session thread, wanting it for its exposure, stood still for hundreds of ms, its queue ran dry and it then called
 * that a stall (q10hp: every "stall" of node 2 is at the IMX766's start on node 1; FBC enqueued = done). So: who wants the lock
 * says so, and the table loop lets him in between two registers; nothing but a power sequence or a receiver's setup holds it
 * for more than one I2C transaction any more.
 */
static volatile int32_t n2d_hw_waiters;
static void
n2d_hw_get(void)
{
	(void)__sync_add_and_fetch(&n2d_hw_waiters, 1);
	lck_mtx_lock(&n2d_hw);
	(void)__sync_sub_and_fetch(&n2d_hw_waiters, 1);
}
static void
n2d_hw_let_in(void)                             /* called WITHOUT the lock */
{
	for (unsigned i = 0; i < 50 && n2d_hw_waiters > 0; i++) { n2c_sleep(1); }
}
enum { N2D_FREE, N2D_DMA, N2D_READY, N2D_READING };

/*
 * The second pipe. Stock Android, IMX766 streaming (cam-android-regs-20261109/imx766/sen_4.log): TOP_MUX_CTRL_0 0x03020404
 * (mux1 and mux2 from SENINF5), CAM_MUX_EN 0xe061 and CAM_MUX_CTRL_1 0x07010104: cam mux 5 and 6 take SENINF mux2 (its PDAF,
 * DT 0x30/0x31) and feed the two CAMSV the HAL drives (camsv_hal_writes_ordered.txt). So mux2 -> cam mux 5 -> the CAMSV at
 * 0x1a092000 is a path the stock system uses; cam mux 4 / 0x1a091000 was never seen in use. The three numbers are here to change.
 */
#define N2D_NINST 2
#define N2D_B_MUX SENINF_MUX2
#define N2D_B_CAMMUX 5
#define N2D_B_SV 2u
#define N2D_B_BUF_PA (N2V_BUF_PA + (uint64_t)N2D_NBUF * N2V_BUF_BYTES)       /* 0xD4000000..0xD8000000 */

struct n2d_slot { int state; uint64_t seq, time; uint32_t exp, gain, frame, focus, stat_n, stat_g, stat_r, stat_b; };
struct n2d_inst {
	unsigned idx;                           /* 0, 1 */
	const char *node;
	int mux, cammux; unsigned sv;           /* SENINF mux, cam mux, CAMSV (0x1a090000 + 0x1000 * sv) */
	uint64_t buf_pa;                        /* N2D_NBUF x 16 MiB from here */
	struct seninf_ctx sctx; bool sctx_ok;
	struct n2d_slot slot[N2D_NBUF];
	vm_offset_t buf[N2D_NBUF];
	uint64_t pa[N2D_NBUF];
	unsigned sensor, want_sensor;
	int dma;                                /* the buffer the DMA writes now: q[0] */
	int q[N2D_NBUF]; unsigned qn;           /* the buffers given to the hardware, in its order */
	unsigned failed;                        /* sessions that ended for want of frames, in a row */
	uint32_t opens;
	bool wanted, streaming, reading, taken_said;
	uint64_t seq, given, last_read_ms;
	struct { bool pending; uint32_t lines, gain, frame; } ask;
	uint32_t cur_exp, cur_gain, cur_frame;
	uint64_t enq_ok_us;                     /* no buffer is given to the hardware before this (n2d_top_up) */
	unsigned exp_said;                      /* exposures said in this session (0293) */
	uint32_t w_frame, w_lines, w_code;      /* 0294: what the sensor's registers hold (~0: not written yet) */
	struct { unsigned n, i; uint16_t reg[8]; uint8_t val[8]; uint32_t lines, gain, frame, code; } xw;   /* an exposure being written */
	uint64_t hw_ticks_fallback;             /* frames stamped with the time they were seen (no hardware stamp) */
	struct { bool pending, ever; uint32_t pos; } focus_ask;
	unsigned mode, want_mode;               /* 0340: the sensor mode streaming / asked (n2d_imx766_modes) */
};
static struct n2d_inst n2d_i[N2D_NINST] = {
	{ .idx = 0, .node = "redwood-camera", .mux = SENINF_MUX1, .cammux = N2V_CAMMUX, .sv = N2V_SV, .buf_pa = N2V_BUF_PA,
	  .sensor = N2C_IMX766, .want_sensor = N2C_IMX766, .dma = -1, .cur_exp = 1000, .cur_gain = 256 },
	{ .idx = 1, .node = "redwood-camera2", .mux = N2D_B_MUX, .cammux = N2D_B_CAMMUX, .sv = N2D_B_SV, .buf_pa = N2D_B_BUF_PA,
	  .sensor = N2C_IMX355, .want_sensor = N2C_IMX355, .dma = -1, .cur_exp = 1000, .cur_gain = 256 },
};
static bool n2d_made;

/*
 * 0290: the main camera's lens. The stock kernel's driver for this phone (OnePlus kernel source,
 * drivers/misc/mediatek/lens/main/common/ak7377af/AK7377AF.c; lens_list.h: "add for 20817 sensor af"; 20817 is the project of
 * the vendor's imx766_mipi_raw20817) is the AK7377AF: I2C address 0x18 >> 1 = 0x0c on the sensor's bus (i2c2), supply FAN53870
 * LDO6 2.8 V (AK7377AF_DoExtLdo; on since n2c_fan_setup). Its whole protocol there:
 *   initAF      reg 0x02 = 0x00 (active; "00:active mode, 10:Standby mode, x1:Sleep mode"), 10 ms
 *   moveAF(p)   p 0..1023 from the HAL, DAC = p * 4 (12 bits): reg 0x00 = DAC >> 4, reg 0x01 = (DAC & 0xf) << 4
 *   Release     reg 0x02 = 0x20 (standby), 20 ms
 * No identification register is read by that driver, so the chip is taken as an AK7377 when it acknowledges at 0x0c and takes
 * the activation; its registers 0x00-0x0f are printed at every power-on. The ABI's position is the HAL's 10 bits.
 * One actuator: it belongs to whichever instance streams the IMX766.
 */
#define N2D_AF_DEV 0x0c
#define N2D_FOCUS_MAX 1023u
static bool n2d_af_ok;                         /* the actuator is active and takes positions */
static uint32_t n2d_focus_cur;                 /* the position last written */
static unsigned n2d_focus_said;

static uint64_t
n2d_ms(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / NSEC_PER_MSEC;
}
static uint64_t
n2d_us(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / NSEC_PER_USEC;
}

/*
 * 0340 (cam60-20261112): the main camera's sensor modes. REDWOOD_CAMERA_MODE a = 0 (the 4096x3072 30/s one of n2c_sensors, default),
 * 1 (hs_video: QBIN V2H2 2048x1152; the vendor's 120/s frame given out twice as long, 60/s) or 2 (custom2: QBIN 4096x2304, 60/s), from the
 * OnePlus driver the other two tables came from (nord2_cam_tables.h). Like REDWOOD_CAMERA_SENSOR: the session ends, the next read starts the
 * sensor in the mode asked (the frame header says the size, line time and shortest frame). Only the IMX766 has modes; other sensors run 0.
 */
struct n2d_mode { const char *name; const uint16_t *t; unsigned n; uint32_t w, h, line_ns, frame_lines; int64_t pixel_rate; };
#define N2D_NMODES 3
static const struct n2d_mode n2d_imx766_modes[N2D_NMODES] = {
	[1] = { "hs_video 2048x1152 60/s", N2C_TAB(n2c_imx766_hsvideo), 2048, 1152, 6777, 2 * 1228, 517030000 },
	[2] = { "custom2 4096x2304 60/s", N2C_TAB(n2c_imx766_custom2), 4096, 2304, 6835, 2438, 894170000 },
};
static const struct n2d_mode *
n2d_mode_of(const struct n2d_inst *in)
{
	return in->sensor == N2C_IMX766 && in->mode > 0 && in->mode < N2D_NMODES ? &n2d_imx766_modes[in->mode] : NULL;
}
static uint32_t n2d_w(const struct n2d_inst *in) { const struct n2d_mode *m = n2d_mode_of(in); return m ? m->w : n2c_sensors[in->sensor].w; }
static uint32_t n2d_h(const struct n2d_inst *in) { const struct n2d_mode *m = n2d_mode_of(in); return m ? m->h : n2c_sensors[in->sensor].h; }
static uint32_t n2d_line_ns(const struct n2d_inst *in) { const struct n2d_mode *m = n2d_mode_of(in); return m ? m->line_ns : n2d_info[in->sensor].line_ns; }
static uint32_t n2d_flines(const struct n2d_inst *in) { const struct n2d_mode *m = n2d_mode_of(in); return m ? m->frame_lines : n2d_info[in->sensor].frame_lines; }
static uint32_t n2d_stride_i(const struct n2d_inst *in) { return n2d_w(in) * 10 / 8; }
static unsigned n2d_want_mode(const struct n2d_inst *in, unsigned s) { return s == N2C_IMX766 && in->want_mode < N2D_NMODES ? in->want_mode : 0; }
static vm_offset_t n2d_svb(const struct n2d_inst *in) { return n2v_sv[in->sv]; }
static struct n2d_inst *n2d_other(struct n2d_inst *in) { return &n2d_i[1 - in->idx]; }

/* is sensor s the other node's (it is open and wants it, or streams it)? n2d_lock held */
static bool
n2d_sensor_taken(struct n2d_inst *in, unsigned s)
{
	struct n2d_inst *o = n2d_other(in);
	return (o->streaming && o->sensor == s) || (o->opens != 0 && o->want_sensor == s);
}

static uint64_t
n2d_frame_us(const struct n2d_inst *in)
{
	return ((uint64_t)(in->cur_frame ? in->cur_frame : n2d_flines(in)) * n2d_line_ns(in)) / 1000u;
}

static bool
n2d_alloc(struct n2d_inst *in, uint32_t bytes)
{
	if (bytes > N2V_BUF_BYTES - 4096) { return false; }
	if (in->buf[0]) { return true; }
	for (unsigned i = 0; i < N2D_NBUF; i++) {            /* a fixed claim below 4 GB (nord2_camsv.inc), N2D_NBUF x 16 MiB an instance */
		in->pa[i] = in->buf_pa + (uint64_t)i * N2V_BUF_BYTES;
		in->buf[i] = ml_io_map_unmappable(in->pa[i], N2V_BUF_BYTES, VM_WIMG_DEFAULT);
		if (!in->buf[i]) { printf("NORD2 CAMERA: %s: buffer %u cannot be mapped\n", in->node, i); in->buf[0] = 0; return false; }
	}
	printf("NORD2 CAMERA: %s: %u buffers of %u bytes at pa %#llx..%#llx\n", in->node, N2D_NBUF, N2V_BUF_BYTES,
	    (unsigned long long)in->buf_pa, (unsigned long long)(in->buf_pa + (uint64_t)N2D_NBUF * N2V_BUF_BYTES));
	return true;
}

/* rails, the MDP and CAM domains, clocks, SMI: once, at the first session (n2c_t1's first half). n2d_hw held */
static bool n2d_plat_ok, n2d_plat_tried;
static bool
n2d_platform(void)
{
	if (n2d_plat_tried) { return n2d_plat_ok; }
	n2d_plat_tried = true;
	n2c_top = ml_io_map_unmappable(0x10000000, 0x1000, VM_WIMG_IO);
	if (!n2c_lvl(29)) { n2c_out(29, true); }
	n2c_out(133, true);
	n2c_sleep(5);
	(void)n2c_fan_setup();
	if (!n2c_vcamio(1)) { printf("NORD2 CAMERA: VCAMIO FAILED\n"); return false; }
	n2c_sleep(2);
	n2c_seninf_ok = n2s_init();
	n2v_map();
	n2d_plat_ok = n2c_seninf_ok;
	return n2d_plat_ok;
}

/* ---- the sensors' I2C, one transaction at a time under n2d_hw ---- */
static bool
n2d_swr(const struct n2c_sensor *se, uint16_t reg, uint8_t v)
{
	n2d_hw_get();
	n2c_s = se;
	bool ok = n2c_s_wr(reg, v);
	lck_mtx_unlock(&n2d_hw);
	return ok;
}
static int
n2d_srd(const struct n2c_sensor *se, uint16_t reg)
{
	n2d_hw_get();
	n2c_s = se;
	int v = n2c_s_rd(reg);
	lck_mtx_unlock(&n2d_hw);
	return v;
}
static unsigned
n2d_stable(const struct n2c_sensor *se, const uint16_t *t, unsigned words)
{
	unsigned nak = 0;
	for (unsigned i = 0; i + 1 < words; i += 2) {
		if (words > 600 && (i / 2) % 32 == 0) { printf("NORD2 CAM: %s table write %u: [%#06x] <- %#04x\n", se->name, i / 2, t[i], t[i + 1]); }
		if (!n2d_swr(se, t[i], (uint8_t)t[i + 1])) { nak++; }
		n2d_hw_let_in();
	}
	return nak;
}

static void
n2d_arm(struct n2d_inst *in, unsigned b)       /* the fill pattern at the end of buffer b, then the DMA's */
{
	uint32_t size = n2d_stride_i(in) * n2d_h(in);
	memset((void *)(in->buf[b] + size - 64), N2D_FILL, 64);
	n2v_inval(in->buf[b] + size - 64, 64);
	in->slot[b].state = N2D_DMA;
	in->q[in->qn++] = (int)b;
	in->dma = in->q[0];
	n2v_enqueue(n2d_svb(in), in->pa[b], size);
}

/* a free buffer, the one after `after' first */
static int
n2d_free_slot(struct n2d_inst *in, unsigned after)
{
	for (unsigned i = 1; i <= N2D_NBUF; i++) {
		unsigned c = (after + i) % N2D_NBUF;
		if (in->slot[c].state == N2D_FREE) { return (int)c; }
	}
	return -1;
}

/*
 * keep N2D_QUEUED buffers with the hardware while there are free ones.
 * 0290: never two enqueues one right after the other while frames flow. q10hf, front camera at 60 frames a second: after
 * 15 s this thread fell two frames behind once, found both queued buffers done and gave the hardware two more within
 * microseconds; from then on the first of every such pair never got a frame ("buffer N of the queue was passed over"), which
 * left the queue empty again at the next frame: two more at once, and so on - every other frame lost, 30 a second for good.
 * The stock HAL never does that (one enqueue per frame, 19 ms or more apart). So: after an enqueue into an EMPTY queue the
 * next one waits a whole frame time and 2 ms (the first is the DMA's by then); otherwise half a frame time, 8 ms at least.
 */
static void
n2d_top_up(struct n2d_inst *in, unsigned after)
{
	while (in->qn < N2D_QUEUED) {
		if (n2d_us() < in->enq_ok_us) { break; }
		bool empty = in->qn == 0;
		uint64_t frame_us = n2d_frame_us(in), half = frame_us / 2 < 8000 ? 8000 : frame_us / 2;
		lck_mtx_lock(&n2d_lock);
		int c = n2d_free_slot(in, after);
		if (c >= 0) { in->slot[c].state = N2D_DMA; }      /* taken under the lock; n2d_arm() fills and enqueues it */
		lck_mtx_unlock(&n2d_lock);
		if (c < 0) { break; }
		n2d_arm(in, (unsigned)c);
		in->enq_ok_us = n2d_us() + (empty ? frame_us + 2000 : half);
		after = (unsigned)c;
	}
}

/* CAMSV configured and enabled on a free buffer, a second one behind it: the start of a session, and after a stall */
static void
n2d_camsv_begin(struct n2d_inst *in, bool frames_flow)
{
	unsigned s = in->sensor;
	uint32_t stride = n2d_stride_i(in), size = stride * n2d_h(in);
	lck_mtx_lock(&n2d_lock);
	for (unsigned k = 0; k < N2D_NBUF; k++) { if (in->slot[k].state == N2D_DMA) { in->slot[k].state = N2D_FREE; } }
	int b = n2d_free_slot(in, N2D_NBUF - 1);
	if (b >= 0) { in->slot[b].state = N2D_DMA; }
	lck_mtx_unlock(&n2d_lock);
	in->qn = 0; in->dma = -1;
	if (b < 0) { return; }                         /* (cannot be: at most two are ready or being read) */
	memset((void *)(in->buf[b] + size - 64), N2D_FILL, 64); n2v_inval(in->buf[b] + size - 64, 64);
	in->q[in->qn++] = b; in->dma = b;
	n2v_camsv_cfg(n2d_svb(in), s == N2C_IMX615 ? 8 : 0, n2d_w(in), n2d_h(in), stride, in->pa[b]);
	n2v_camsv_go(n2d_svb(in));
	/* the second buffer at once before the sensor streams (as every session so far); a frame time later when frames already flow */
	in->enq_ok_us = frames_flow ? n2d_us() + n2d_frame_us(in) + 2000 : 0;
	n2d_top_up(in, (unsigned)b);
}

/*
 * Sony exposure, gain and frame length (OnePlus write_shutter / set_gain: 0x0104 group hold).
 * 0294: only the registers that change are written, and the session loop writes ONE of them per turn (n2d_exposure_step): the
 * bit-banged I2C takes about 3 ms a register, eight in a row kept the loop from looking at its buffers for most of a frame.
 */
static void
n2d_exposure_plan(struct n2d_inst *in, uint32_t lines, uint32_t gain, uint32_t frame)
{
	const struct n2d_info *info = &n2d_info[in->sensor];
	unsigned n = 0;
	if (gain < 256) { gain = 256; }
	if (gain > info->gain_max) { gain = info->gain_max; }
	if (!frame || frame < lines + info->margin) { frame = lines + info->margin; }
	if (frame < n2d_flines(in)) { frame = n2d_flines(in); }
	if (frame > 0xffff) { frame = 0xffff; }
	if (lines > frame - info->margin) { lines = frame - info->margin; }
	if (lines < 1) { lines = 1; }
	uint32_t code = info->gain_full - (info->gain_num * 256) / gain;
	if (in->exp_said < 4) {
		in->exp_said++;
		printf("NORD2 CAMERA: %s: %s exposure %u lines, frame %u lines, gain %u/256 = code %u (0x0204/5 <- %#06x)\n", in->node,
		    n2c_sensors[in->sensor].name, lines, frame, gain, code, code);
	}
	in->xw.lines = lines; in->xw.gain = gain; in->xw.frame = frame; in->xw.code = code;
#define N2D_XW(r, v) do { in->xw.reg[n] = (r); in->xw.val[n] = (uint8_t)(v); n++; } while (0)
	if (frame != in->w_frame || lines != in->w_lines || code != in->w_code) {
		N2D_XW(0x0104, 1);
		if (frame != in->w_frame) { N2D_XW(0x0340, frame >> 8); N2D_XW(0x0341, frame); }
		if (lines != in->w_lines) { N2D_XW(0x0202, lines >> 8); N2D_XW(0x0203, lines); }
		if (code != in->w_code) { N2D_XW(0x0204, code >> 8); N2D_XW(0x0205, code); }
		N2D_XW(0x0104, 0);
	}
#undef N2D_XW
	in->xw.n = n; in->xw.i = 0;
	if (n == 0) { in->cur_exp = lines; in->cur_gain = gain; in->cur_frame = frame; }
}
/* one register of the exposure being written; true while there is more */
static bool
n2d_exposure_step(struct n2d_inst *in)
{
	if (in->xw.i >= in->xw.n) { return false; }
	(void)n2d_swr(&n2c_sensors[in->sensor], in->xw.reg[in->xw.i], in->xw.val[in->xw.i]);
	if (++in->xw.i >= in->xw.n) {
		in->w_frame = in->xw.frame; in->w_lines = in->xw.lines; in->w_code = in->xw.code;
		in->cur_exp = in->xw.lines; in->cur_gain = in->xw.gain; in->cur_frame = in->xw.frame;
		in->xw.n = in->xw.i = 0;
		return false;
	}
	return true;
}
static void
n2d_set_exposure(struct n2d_inst *in, uint32_t lines, uint32_t gain, uint32_t frame)   /* all of it now: before the stream */
{
	n2d_exposure_plan(in, lines, gain, frame);
	while (n2d_exposure_step(in)) { }
}

static bool
n2d_af_move(uint32_t pos)
{
	uint32_t dac = (pos > N2D_FOCUS_MAX ? N2D_FOCUS_MAX : pos) * 4;
	n2d_hw_get();
	n2c_bus_init(&n2c_i2c2);
	bool ok = n2c_wr(N2D_AF_DEV, 0x00, 1, (uint8_t)(dac >> 4)) && n2c_wr(N2D_AF_DEV, 0x01, 1, (uint8_t)((dac & 0xf) << 4));
	lck_mtx_unlock(&n2d_hw);
	if (ok) { n2d_focus_cur = dac / 4; }
	if (!ok || n2d_focus_said < 12) {
		n2d_focus_said++;
		printf("NORD2 CAMERA: lens to %u of %u (DAC %u): %s\n", dac / 4, N2D_FOCUS_MAX, dac, ok ? "written" : "NOT ACKNOWLEDGED");
	}
	return ok;
}

/* after the sensor's power-on (AFVDD is up with it): what the actuator's registers say, then active */
static void
n2d_af_start(struct n2d_inst *in)
{
	uint8_t r[16], mode = 0xff;
	unsigned acks = 0;
	n2d_af_ok = false;
	for (unsigned i = 0; i < 16; i++) {
		r[i] = 0xee;
		n2d_hw_get();
		n2c_bus_init(&n2c_i2c2);
		if (n2c_rd(N2D_AF_DEV, (uint16_t)i, 1, &r[i], 1)) { acks++; }
		lck_mtx_unlock(&n2d_hw);
		n2d_hw_let_in();
	}
	n2d_hw_get();
	n2c_bus_init(&n2c_i2c2);
	bool ok = acks != 0 && n2c_wr(N2D_AF_DEV, 0x02, 1, 0x00);
	lck_mtx_unlock(&n2d_hw);
	printf("NORD2 CAMERA: lens actuator, i2c2 0x0c: %u of 16 registers read, 00-0f = %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
	    "(ee: not read; the stock driver here is AK7377AF: 02 mode, 00/01 position)\n", acks, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
	    r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
	if (acks == 0) { printf("NORD2 CAMERA: lens actuator does not answer: the lens is not moved (focus_max 0)\n"); return; }
	if (!ok) { printf("NORD2 CAMERA: lens actuator did not take the activation (02 <- 00): the lens is not moved (focus_max 0)\n"); return; }
	n2c_sleep(10);
	n2d_hw_get();
	n2c_bus_init(&n2c_i2c2);
	(void)n2c_rd(N2D_AF_DEV, 0x02, 1, &mode, 1);
	lck_mtx_unlock(&n2d_hw);
	n2d_af_ok = true;
	n2d_focus_cur = (((uint32_t)r[0] << 4) | (r[1] >> 4)) / 4;
	printf("NORD2 CAMERA: lens actuator active (02 reads %02x), position registers say %u of %u%s\n", mode, n2d_focus_cur, N2D_FOCUS_MAX,
	    in->focus_ask.ever ? "; the position asked before is given again" : "");
	if (in->focus_ask.ever) { (void)n2d_af_move(in->focus_ask.pos); }
}

static void
n2d_af_stop(void)
{
	if (!n2d_af_ok) { return; }
	n2d_af_ok = false;
	n2d_hw_get();
	n2c_bus_init(&n2c_i2c2);
	(void)n2c_wr(N2D_AF_DEV, 0x02, 1, 0x20);       /* standby (AK7377AF_Release) */
	lck_mtx_unlock(&n2d_hw);
	n2c_sleep(20);
}

static bool
n2d_start(struct n2d_inst *in, unsigned s)
{
	const struct n2c_sensor *se = &n2c_sensors[s];
	in->sensor = s;
	in->mode = n2d_want_mode(in, s);        /* 0340 */
	const struct n2d_mode *md = n2d_mode_of(in);
	uint32_t size = n2d_stride_i(in) * n2d_h(in);
	if (se->port < 0) { printf("NORD2 CAMERA: %s has no receiver port yet\n", se->name); return false; }
	n2d_hw_get();
	bool ok = n2d_platform();
	if (ok && !in->sctx_ok) { in->sctx = n2s_ctx; in->sctx_ok = true; }      /* the register bases; the rest is n2s_rx_x()'s */
	lck_mtx_unlock(&n2d_hw);
	if (!ok || !n2d_alloc(in, 5120 * 3072 + 4096)) { return false; }
	in->sensor = s;
	in->exp_said = 0;
	in->w_frame = in->w_lines = in->w_code = ~0u; in->xw.n = in->xw.i = 0;
	if (s == N2C_IMX766) { n2d_af_ok = false; }
	n2d_hw_get();
	ok = n2c_power(s, true);
	if (!ok) { (void)n2c_power(s, false); }
	lck_mtx_unlock(&n2d_hw);
	if (!ok) { return false; }
	n2d_hw_get(); n2c_bus_init(se->bus); lck_mtx_unlock(&n2d_hw);
	int hi = n2d_srd(se, se->idreg_hi), lo = n2d_srd(se, se->idreg_lo);
	if (hi != (se->id >> 8) || lo != (se->id & 0xff)) {
		printf("NORD2 CAMERA: %s chip id %#x %#x: not there\n", se->name, hi, lo);
		n2d_hw_get(); (void)n2c_power(s, false); lck_mtx_unlock(&n2d_hw);
		return false;
	}
	(void)n2d_stable(se, se->init, se->init_n);
	(void)n2d_swr(se, 0x0101, se->mirror);
	(void)n2d_stable(se, md ? md->t : se->mode, md ? md->n : se->mode_n);
	if (md) { printf("NORD2 CAMERA: %s: %s mode %u: %s (%ux%u, line %u ns, frame %u lines)\n", in->node, se->name, in->mode, md->name, md->w, md->h, md->line_ns, md->frame_lines); }
	n2d_set_exposure(in, n2d_flines(in) / 2, 2 * 256, 0);
	if (s == N2C_IMX766) { n2d_af_start(in); }
	lck_mtx_lock(&n2d_lock);
	for (unsigned k = 0; k < N2D_NBUF; k++) { in->slot[k].state = N2D_FREE; }
	lck_mtx_unlock(&n2d_lock);
	for (unsigned k = 0; k < N2D_NBUF; k++) { memset((void *)(in->buf[k] + size - 64), N2D_FILL, 64); n2v_inval(in->buf[k] + size - 64, 64); }
	/* the receiver and the cam mux (shared registers: under n2d_hw), CAMSV armed on a buffer, then the stream */
	n2d_hw_get();
	n2s_rx_x(&in->sctx, in->mux, se->port, se->cphy, se->lanes, md ? md->pixel_rate : se->pixel_rate);
	n2v_cammux_x(&in->sctx, in->mux, in->cammux, n2d_w(in), n2d_h(in));
	printf("NORD2 CAMERA: %s: pipe: %s, CSI port %d %s %d %s -> SENINF%d -> mux%d -> cam mux %d -> CAMSV%u (%#x) -> pa %#llx; top mux %#x %#x, cam mux src %#x %#x en %#x\n",
	    in->node, se->name, se->port, se->cphy ? "C-PHY" : "D-PHY", se->lanes, se->cphy ? "trios" : "lanes", in->sctx.seninfIdx + 1, in->mux + 1,
	    in->cammux, in->sv, 0x1a090000u + 0x1000u * in->sv, (unsigned long long)in->buf_pa, ni_rd(n2s_if, 0x10), ni_rd(n2s_if, 0x14),
	    ni_rd(n2s_if, 0x400), ni_rd(n2s_if, 0x404), ni_rd(n2s_if, 0x410));
	lck_mtx_unlock(&n2d_hw);
	n2d_camsv_begin(in, false);
	(void)n2d_swr(se, 0x0100, 1);
	printf("NORD2 CAMERA: %s: %s streaming %ux%u RAW10\n", in->node, se->name, n2d_w(in), n2d_h(in));
	return true;
}

static void
n2d_stop(struct n2d_inst *in, unsigned s)
{
	n2v_camsv_stop(n2d_svb(in));
	(void)n2d_swr(&n2c_sensors[s], 0x0100, 0);
	n2c_sleep(40);
	if (s == N2C_IMX766) { n2d_af_stop(); }
	n2d_hw_get();
	mtk_cam_seninf_disable_cammux(&in->sctx, in->cammux);
	(void)n2c_power(s, false);
	lck_mtx_unlock(&n2d_hw);
	in->dma = -1; in->qn = 0;
}

/* how long without a frame is a stall: three frame times and 50 ms, 150 ms at least (the frame length follows the exposure) */
static uint32_t
n2d_stall_ms(const struct n2d_inst *in)
{
	uint64_t ms = 3 * (n2d_frame_us(in) / 1000u) + 50;
	return (uint32_t)(ms < 150 ? 150 : ms);
}

/*
 * 0294: when a frame began, by the hardware. The first four bytes of the 64 the DMA writes after a frame (0x804) are a 32-bit
 * count at 13 MHz, latched about one read-out before the frame's end is seen (q10hm/q10hp: +433120 a frame of 33.3 ms on every
 * CAMSV, and equal to the low 32 bits of mach_absolute_time() less 33.0 to 34.3 ms): the system's own 13 MHz counter. So the
 * frame's `time` is mach_absolute_time() of that latch, the same clock for both instances, instead of when this loop's 3 ms
 * polling noticed the frame. Used only when mach_absolute_time() counts 13 MHz and the count is less than 2 s old; else the
 * time the frame was seen, as before. N2D_HW_TIME 0 turns it off.
 */
#define N2D_HW_TIME 1
static uint64_t
n2d_frame_time(struct n2d_inst *in, int b, uint32_t size, uint32_t *ticks_out)
{
	uint64_t now = mach_absolute_time(), ns = 0;
	const volatile uint8_t *h = (const volatile uint8_t *)(in->buf[b] + size);
	n2v_inval(in->buf[b] + size, 64);
	uint32_t ticks = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
	*ticks_out = ticks;
	absolutetime_to_nanoseconds(13000000ULL, &ns);
	uint32_t age = (uint32_t)now - ticks;
	if (N2D_HW_TIME && ns > 990000000ULL && ns < 1010000000ULL && age != 0 && age < 2u * 13000000u) { return now - age; }
	in->hw_ticks_fallback++;
	return now;
}

static bool
n2d_tail_done(const struct n2d_inst *in, int b, uint32_t size)
{
	n2v_inval(in->buf[b] + size - 64, 64);
	const volatile uint8_t *tail = (const volatile uint8_t *)(in->buf[b] + size - 64);
	for (unsigned k = 0; k < 64; k++) { if (tail[k] != N2D_FILL) { return true; } }
	return false;
}

/*
 * 0292: what a finished frame looks like, for whoever reads only headers (the stream that is not on screen): 8x8 Bayer cells
 * spread over the frame (centres at (2i + 1) / 16 of the width and height), the mean of each colour above black, 10 bits.
 * The DMA's RAW10: four pixels in one little-endian 40-bit word. 128 cache lines are looked at, not the frame.
 */
static void
n2d_stats(const struct n2d_inst *in, int b, struct n2d_slot *sl)
{
	unsigned s = in->sensor;
	uint32_t w = n2d_w(in), h = n2d_h(in), stride = n2d_stride_i(in), black = n2d_info[s].black, bayer = n2d_info[s].bayer;
	uint32_t sum[3] = { 0, 0, 0 };      /* r, g (two a cell), b */
	for (unsigned gy = 0; gy < 8; gy++) {
		for (unsigned gx = 0; gx < 8; gx++) {
			uint32_t x = (((2 * gx + 1) * w) / 16) & ~3u, y = (((2 * gy + 1) * h) / 16) & ~1u, v[4];
			for (unsigned row = 0; row < 2; row++) {
				vm_offset_t a = in->buf[b] + (vm_offset_t)(y + row) * stride + (x / 4) * 5;
				n2v_inval(a, 5);
				const volatile uint8_t *g = (const volatile uint8_t *)a;
				uint32_t lo = (uint32_t)g[0] | (uint32_t)g[1] << 8 | (uint32_t)g[2] << 16;
				v[row * 2] = lo & 0x3ff; v[row * 2 + 1] = (lo >> 10) & 0x3ff;
			}
			for (unsigned k = 0; k < 4; k++) { v[k] = v[k] > black ? v[k] - black : 0; }
			switch (bayer & 3) {
			case 0: sum[0] += v[0]; sum[1] += v[1] + v[2]; sum[2] += v[3]; break;      /* RGGB */
			case 1: sum[1] += v[0] + v[3]; sum[0] += v[1]; sum[2] += v[2]; break;      /* GRBG */
			case 2: sum[1] += v[0] + v[3]; sum[2] += v[1]; sum[0] += v[2]; break;      /* GBRG */
			default: sum[2] += v[0]; sum[1] += v[1] + v[2]; sum[0] += v[3]; break;     /* BGGR */
			}
		}
	}
	sl->stat_n = 64; sl->stat_r = sum[0] / 64; sl->stat_g = sum[1] / 128; sl->stat_b = sum[2] / 64;
}

/*
 * a session: one sensor on one instance until nobody reads it.
 * 0289: (a) the hardware's queue is kept N2D_QUEUED deep, so a frame's end no longer races the next frame's start for a buffer;
 * (b) a stall says what CAMSV and the receiver look like; (c) and is answered by configuring and enabling CAMSV again on a free
 * buffer, the sensor left streaming; (d) the session's own frame count is said. 0292: all of it per instance.
 */
static bool
n2d_session(struct n2d_inst *in, unsigned s)
{
	uint64_t last_frame = n2d_ms(), seq0 = in->seq;
	unsigned rearms = 0, rearms_now = 0, dropped_head = 0;
	bool stall_said = false, first = true;
	if (!n2d_start(in, s)) { return false; }
	uint32_t size = n2d_stride_i(in) * n2d_h(in);   /* 0340: of the mode n2d_start() chose */
	lck_mtx_lock(&n2d_lock);
	in->given = 0; in->streaming = true; in->last_read_ms = n2d_ms();
	lck_mtx_unlock(&n2d_lock);
	last_frame = n2d_ms();
	uint64_t watch = last_frame;                   /* the last frame, or the last time CAMSV was started again */
	for (;;) {
		bool stop = false;
		uint64_t looked = n2d_ms();                /* 0294: when this turn looked at its buffers */
		int b = in->qn ? in->q[0] : -1;
		if (b >= 0) {
			bool done = n2d_tail_done(in, b, size);
			if (!done && in->qn > 1 && n2d_tail_done(in, in->q[1], size)) {
				/* the hardware went past the head of the queue without writing it: the next one has an end and this one has not */
				int b2 = in->q[1];
				if (dropped_head++ < 4 || (dropped_head & 63) == 0) {
					printf("NORD2 CAMERA: %s: buffer %d of the queue was passed over, buffer %d has the frame (#%u of this session, frame %llu of it)\n",
					    in->node, b, b2, dropped_head, in->seq - seq0 + 1);
				}
				lck_mtx_lock(&n2d_lock);
				in->slot[b].state = N2D_FREE;
				lck_mtx_unlock(&n2d_lock);
				for (unsigned k = 1; k < in->qn; k++) { in->q[k - 1] = in->q[k]; }
				in->qn--;
				b = b2; done = true;
			}
			if (done) {
				struct n2d_slot st = { 0 };
				uint32_t hw_ticks = 0;
				uint64_t seen = mach_absolute_time(), stamp = n2d_frame_time(in, b, size, &hw_ticks);
				n2d_stats(in, b, &st);
				lck_mtx_lock(&n2d_lock);
				for (unsigned i = 0; i < N2D_NBUF; i++) { if (in->slot[i].state == N2D_READY) { in->slot[i].state = N2D_FREE; } }
				in->slot[b].state = N2D_READY;
				in->slot[b].seq = ++in->seq;
				in->slot[b].time = stamp;
				in->slot[b].exp = in->cur_exp; in->slot[b].gain = in->cur_gain; in->slot[b].frame = in->cur_frame;
				in->slot[b].focus = n2d_focus_cur;
				in->slot[b].stat_n = st.stat_n; in->slot[b].stat_g = st.stat_g; in->slot[b].stat_r = st.stat_r; in->slot[b].stat_b = st.stat_b;
				wakeup(&in->seq);
				lck_mtx_unlock(&n2d_lock);
				if (in->seq - seq0 <= 3) {
					/* 0290: the 64 bytes the DMA writes after a frame (0x804; the TG's time stamp is on, 0x130 bit 16): said for the
					 * first three frames of a session, to see whether a hardware time is in there */
					const volatile uint8_t *h = (const volatile uint8_t *)(in->buf[b] + size);
					char hex[64 * 2 + 1];
					n2v_inval(in->buf[b] + size, 64);
					for (unsigned k = 0; k < 64; k++) { snprintf(hex + k * 2, 3, "%02x", h[k]); }
					uint64_t age_ns = 0;
					absolutetime_to_nanoseconds(seen - stamp, &age_ns);
					printf("NORD2 CAMERA: %s: frame %llu of the session, seen done at %llu us, stamped %llu us before that (hardware count %#x, %llu not stamped by it) (green %u red %u blue %u): DMA header %s\n",
					    in->node, in->seq - seq0, n2d_us(), age_ns / 1000, hw_ticks, in->hw_ticks_fallback, st.stat_g, st.stat_r, st.stat_b, hex);
				}
				for (unsigned k = 1; k < in->qn; k++) { in->q[k - 1] = in->q[k]; }
				in->qn--;
				in->dma = in->qn ? in->q[0] : -1;
				n2d_top_up(in, (unsigned)b);
				last_frame = watch = n2d_ms();
				first = false; rearms_now = 0;
			}
		}
		if (in->qn < N2D_QUEUED) { n2d_top_up(in, in->qn ? (unsigned)in->q[in->qn - 1] : 0); }
		/* 0294: the exposure asked, one register a turn (the newest ask waits for the one being written) */
		if (!n2d_exposure_step(in)) {
			lck_mtx_lock(&n2d_lock);
			bool pend = in->ask.pending;
			uint32_t l = in->ask.lines, g = in->ask.gain, f = in->ask.frame;
			in->ask.pending = false;
			lck_mtx_unlock(&n2d_lock);
			if (pend) { n2d_exposure_plan(in, l, g, f); (void)n2d_exposure_step(in); }
		}
		lck_mtx_lock(&n2d_lock);
		if (in->focus_ask.pending) {
			uint32_t pos = in->focus_ask.pos;
			in->focus_ask.pending = false;
			lck_mtx_unlock(&n2d_lock);
			if (s == N2C_IMX766 && n2d_af_ok) { (void)n2d_af_move(pos); }
			lck_mtx_lock(&n2d_lock);
		}
		if (in->opens == 0 || n2d_ms() - in->last_read_ms > N2D_IDLE_MS || in->want_sensor != s || n2d_want_mode(in, s) != in->mode) {
			stop = true;
		} else if (n2d_ms() - last_frame > 3000u) {
			printf("NORD2 CAMERA: %s: no frame for 3 s\n", in->node);
			stop = true;
		}
		lck_mtx_unlock(&n2d_lock);
		if (stop) { break; }
		/* the sensor's first frame may take longer: 500 ms for it */
		uint32_t limit = first ? 500u : n2d_stall_ms(in);
		/* 0294: not when this thread itself was kept waiting since it looked (the lock, the I2C): the next turn looks first */
		if (n2d_ms() - watch > limit && n2d_ms() - looked <= 10) {
			vm_offset_t sv = n2d_svb(in);
			if (!stall_said || rearms_now >= 3) {
				printf("NORD2 CAMERA: %s: stall: no frame for %llu ms (limit %u) after %llu of this session; queue %u [%d %d], exposure %u lines, frame %u lines, gain %u\n",
				    in->node, n2d_ms() - last_frame, limit, in->seq - seq0, in->qn, in->qn > 0 ? in->q[0] : -1, in->qn > 1 ? in->q[1] : -1,
				    in->cur_exp, in->cur_frame, in->cur_gain);
				n2v_status("stall", sv, in->sv);
				uint32_t inter0 = ni_rd(sv, 0x16c);
				n2c_sleep(40);
				n2v_status("stall +40 ms", sv, in->sv);
				printf("NORD2 CAMERA: %s: stall: TG frame count %s in 40 ms (%#x -> %#x): %s\n", in->node, ni_rd(sv, 0x16c) != inter0 ? "moved" : "did NOT move",
				    inter0, ni_rd(sv, 0x16c), ni_rd(sv, 0x16c) != inter0 ? "frames reach this CAMSV, its DMA/FBC is what stopped" : "no frames reach this CAMSV (sensor, receiver, mux or cam mux)");
				n2d_hw_get();
				n2s_report_x(&in->sctx, in->mux, in->node);
				lck_mtx_unlock(&n2d_hw);
				stall_said = true; rearms_now = 0;
			}
			n2v_camsv_stop(sv);
			n2d_camsv_begin(in, true);
			rearms++; rearms_now++;
			if (rearms <= 8 || (rearms & 15) == 0) {
				printf("NORD2 CAMERA: %s: CAMSV%u started again on buffer %d (#%u of this session), the sensor left streaming\n", in->node, in->sv, in->dma, rearms);
			}
			watch = n2d_ms();
		}
		n2c_sleep(3);
	}
	lck_mtx_lock(&n2d_lock);
	in->streaming = false;
	lck_mtx_unlock(&n2d_lock);
	n2d_stop(in, s);
	printf("NORD2 CAMERA: %s: session of %s stopped after %llu frames (%llu since boot), CAMSV started again %u times\n", in->node, n2c_sensors[s].name,
	    in->seq - seq0, in->seq, rearms);
	return n2d_ms() - last_frame <= 3000u;
}

static struct n2d_inst *
n2d_of(dev_t dev)
{
	unsigned m = (unsigned)minor(dev);
	return m < N2D_NINST ? &n2d_i[m] : NULL;
}

static int
n2d_open(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	struct n2d_inst *in = n2d_of(dev);
	if (in == NULL) { return ENXIO; }
	lck_mtx_lock(&n2d_lock);
	if (in->opens++ == 0) { in->failed = 0; in->taken_said = false; }      /* a new opener starts with a clean count */
	lck_mtx_unlock(&n2d_lock);
	return 0;
}

static int
n2d_close(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	struct n2d_inst *in = n2d_of(dev);
	if (in == NULL) { return ENXIO; }
	lck_mtx_lock(&n2d_lock); if (in->opens) { in->opens--; } lck_mtx_unlock(&n2d_lock);
	return 0;
}

static int
n2d_read(dev_t dev, struct uio *uio, __unused int ioflag)
{
	struct n2d_inst *in = n2d_of(dev);
	struct redwood_camera_frame f;
	uint64_t since = n2d_ms();
	int b, error = 0;
	if (in == NULL) { return ENXIO; }
	lck_mtx_lock(&n2d_lock);
	if (in->reading) { lck_mtx_unlock(&n2d_lock); return EBUSY; }
	in->reading = true;
	in->last_read_ms = n2d_ms();
	for (;;) {
		b = -1;
		for (int i = 0; i < N2D_NBUF; i++) {
			if (in->slot[i].state == N2D_READY && in->slot[i].seq > in->given && (b < 0 || in->slot[i].seq > in->slot[b].seq)) { b = i; }
		}
		if (b >= 0 && in->streaming && in->sensor == in->want_sensor) { break; }
		if (!in->streaming) {
			struct n2d_inst *o = n2d_other(in);
			/* 0292: the other node streams this very sensor: this session cannot start */
			if (o->streaming && o->sensor == in->want_sensor) { error = EBUSY; break; }
			if (!in->wanted) { in->wanted = true; wakeup(&in->wanted); }
		}
		if (n2d_ms() - since > (in->streaming ? 3000u : 20000u)) { error = EAGAIN; break; }
		struct timespec ts = { 0, 50 * NSEC_PER_MSEC };
		error = msleep(&in->seq, &n2d_lock, PZERO | PCATCH, "nord2-camera", &ts);
		if (error == EWOULDBLOCK) { error = 0; }
		if (error) { break; }
		in->last_read_ms = n2d_ms();
	}
	if (error) { in->reading = false; lck_mtx_unlock(&n2d_lock); return error; }
	unsigned s = in->sensor;
	uint32_t stride = n2d_stride_i(in), fw = n2d_w(in), fh = n2d_h(in);
	in->slot[b].state = N2D_READING;
	in->given = in->slot[b].seq;
	bzero(&f, sizeof(f));
	memcpy(f.magic, REDWOOD_CAMERA_MAGIC, 8);
	f.header_bytes = sizeof(f);
	f.width = fw; f.height = fh; f.stride = stride; f.format = 4;
	f.bayer = n2d_info[s].bayer; f.black = n2d_info[s].black;
	f.sequence = in->slot[b].seq; f.time = in->slot[b].time;
	f.exposure_lines = in->slot[b].exp; f.line_ns = n2d_line_ns(in);
	f.gain = in->slot[b].gain; f.gain_unity = 256; f.gain_max = n2d_info[s].gain_max;
	f.frame_lines = in->slot[b].frame; f.frame_lines_min = n2d_flines(in); f.frame_lines_max = 0xffff;
	f.margin = n2d_info[s].margin; f.sensor = n2d_info[s].abi;
	f.focus = in->slot[b].focus; f.focus_max = (s == N2C_IMX766 && n2d_af_ok) ? N2D_FOCUS_MAX : 0;
	if (!f.focus_max) { f.focus = 0; }
	f.stat_samples = in->slot[b].stat_n; f.stat_green = in->slot[b].stat_g; f.stat_red = in->slot[b].stat_r; f.stat_blue = in->slot[b].stat_b;
	lck_mtx_unlock(&n2d_lock);
	error = uiomove((caddr_t)&f, (int)MIN(sizeof(f), (size_t)uio_resid(uio)), uio);
	/*
	 * 0289: format 4 (REDWOOD_CAMERA_FORMAT_LE40_RAW10): the DMA's buffer as it is, four pixels in one little-endian 40-bit word,
	 * same stride and height; the bundle unpacks it (camera_isp.c group()). No repacking here, 1 MiB at a time.
	 * A read of the header alone stops here: nothing of the frame is touched (0292: the stream that is not on screen).
	 */
	if (!error && uio_resid(uio) > 0) {
		uint32_t total = stride * fh;
		n2v_inval(in->buf[b], total);
		for (uint32_t off = 0; off < total && !error && uio_resid(uio) > 0; ) {
			uint32_t n = MIN(total - off, 1u << 20);
			if ((user_ssize_t)n > uio_resid(uio)) { n = (uint32_t)uio_resid(uio); }
			error = uiomove((caddr_t)(in->buf[b] + off), (int)n, uio);
			off += n;
		}
	}
	lck_mtx_lock(&n2d_lock);
	in->slot[b].state = N2D_FREE;
	in->reading = false;
	in->last_read_ms = n2d_ms();
	lck_mtx_unlock(&n2d_lock);
	return error;
}

static int
n2d_write(dev_t dev, struct uio *uio, __unused int ioflag)
{
	struct n2d_inst *in = n2d_of(dev);
	struct redwood_camera_command c = { 0, 0, 0, 0, 0, 0 };
	if (in == NULL) { return ENXIO; }
	if (uio_resid(uio) < (user_ssize_t)sizeof(c) || uiomove((caddr_t)&c, sizeof(c), uio) != 0) { return EINVAL; }
	switch (c.command) {
	case REDWOOD_CAMERA_EXPOSURE:
		lck_mtx_lock(&n2d_lock); in->ask.lines = c.a; in->ask.gain = c.b; in->ask.frame = c.c; in->ask.pending = true; lck_mtx_unlock(&n2d_lock);
		return 0;
	case REDWOOD_CAMERA_SENSOR:
		for (unsigned s = 0; s < N2C_NSENSORS; s++) {
			if (n2d_info[s].abi == c.a && c.a != 0) {
				lck_mtx_lock(&n2d_lock);
				bool taken = n2d_sensor_taken(in, s);
				if (!taken) { in->want_sensor = s; }
				lck_mtx_unlock(&n2d_lock);
				printf("NORD2 CAMERA: %s: sensor %u asked (%s)%s\n", in->node, c.a, n2c_sensors[s].name, taken ? ": REFUSED, the other node has it (EBUSY)" : "");
				return taken ? EBUSY : 0;
			}
		}
		return EINVAL;
	case REDWOOD_CAMERA_FOCUS:
		lck_mtx_lock(&n2d_lock);
		in->focus_ask.pos = c.a > N2D_FOCUS_MAX ? N2D_FOCUS_MAX : c.a; in->focus_ask.pending = true; in->focus_ask.ever = true;
		lck_mtx_unlock(&n2d_lock);
		return 0;
	case REDWOOD_CAMERA_MODE: {                 /* 0340 */
		if (c.a >= N2D_NMODES) { return EINVAL; }
		lck_mtx_lock(&n2d_lock);
		bool changed = in->want_mode != c.a;
		in->want_mode = c.a;
		lck_mtx_unlock(&n2d_lock);
		if (changed) { printf("NORD2 CAMERA: %s: mode %u asked (%s)\n", in->node, c.a, c.a ? n2d_imx766_modes[c.a].name : "4096x3072 30/s"); }
		return 0;
	}
	case REDWOOD_CAMERA_KEEP:
		return ENOSPC;
	default:
		return EINVAL;
	}
}

static const struct cdevsw n2d_cdevsw = {
	.d_open = n2d_open, .d_close = n2d_close, .d_read = n2d_read, .d_write = n2d_write,
	.d_ioctl = eno_ioctl, .d_stop = eno_stop, .d_reset = eno_reset, .d_select = eno_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0
};

/* an instance's sessions, for ever */
static void
n2d_serve(struct n2d_inst *in)
{
	while (n2d_made) {
		struct timespec ts = { 1, 0 };
		lck_mtx_lock(&n2d_lock);
		while (!in->wanted) { (void)msleep(&in->wanted, &n2d_lock, PZERO, "nord2-camera-idle", &ts); }
		in->wanted = false;
		unsigned s = in->want_sensor;
		bool any = in->opens != 0;
		struct n2d_inst *o = n2d_other(in);
		bool taken = o->streaming && o->sensor == s;
		if (taken && !in->taken_said) {
			in->taken_said = true;
			printf("NORD2 CAMERA: %s: %s is streaming on %s: no session here (its reads get EBUSY)\n", in->node, n2c_sensors[s].name, o->node);
		}
		lck_mtx_unlock(&n2d_lock);
		if (!any || taken) { continue; }
		if (!n2d_session(in, s)) {
			/* 0289: no giving up for good: say it and slow down; n2d_open() clears the count */
			lck_mtx_lock(&n2d_lock);
			unsigned failed = ++in->failed;
			lck_mtx_unlock(&n2d_lock);
			if (failed >= 5) {
				if (failed == 5 || failed % 20 == 0) { printf("NORD2 CAMERA: %s: %u sessions in a row without frames: each next one waits 2 s\n", in->node, failed); }
				n2c_sleep(2000);
			}
		} else {
			lck_mtx_lock(&n2d_lock); in->failed = 0; lck_mtx_unlock(&n2d_lock);
		}
	}
}

static void
n2d_thread_b(void *arg, wait_result_t wr __unused)
{
	n2d_serve((struct n2d_inst *)arg);
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

static void
n2d_thread(void *arg __unused, wait_result_t wr __unused)
{
	int major = cdevsw_add(-1, &n2d_cdevsw);
	bool a = major >= 0 && devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "%s", n2d_i[0].node) != NULL;
	bool b = a && devfs_make_node(makedev(major, 1), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "%s", n2d_i[1].node) != NULL;
	n2d_made = a;
	printf("NORD2 CAMERA: /dev/redwood-camera %s, /dev/redwood-camera2 %s (sensors: 1 wide IMX766, 2 front IMX615, 3 ultra-wide IMX355; "
	    "node 1: mux%d, cam mux %d, CAMSV%u; node 2: mux%d, cam mux %d, CAMSV%u)\n", a ? "made" : "CANNOT BE MADE", b ? "made" : "CANNOT BE MADE",
	    n2d_i[0].mux + 1, n2d_i[0].cammux, n2d_i[0].sv, n2d_i[1].mux + 1, n2d_i[1].cammux, n2d_i[1].sv);
	if (b) {
		thread_t t;
		if (kernel_thread_start(n2d_thread_b, &n2d_i[1], &t) == KERN_SUCCESS) { thread_deallocate(t); }
	}
	n2d_serve(&n2d_i[0]);
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}
