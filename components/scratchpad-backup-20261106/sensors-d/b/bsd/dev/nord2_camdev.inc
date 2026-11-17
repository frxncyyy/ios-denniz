/*
 * nord2_camdev.inc - /dev/redwood-camera for the Nord 2: HakiOS' camera ABI (hakios-xnu-kernel, branch mtk,
 * xnu/files/bsd/sys/redwood_camera.h, RWFRAME1) and the session logic of its MediaTek skeleton
 * (xnu/files/bsd/dev/redwood_mtkcam_skel.c), with three sensors instead of one:
 *   sensor 1 = the wide (main) camera IMX766, 2 = the front IMX615, 3 = the ultra-wide IMX355
 *   (the numbers of the frame header's `sensor` field; REDWOOD_CAMERA_SENSOR switches, the next read starts it).
 * A session starts when something reads and ends when nothing has for 4 s. Frames are MIPI RAW10 (4 pixels in 5
 * bytes) as the DMA writes them (CAMSV PAK RAW10). A frame is done when the last 64 bytes of its buffer are no longer
 * the fill pattern (HakiOS' rule); the DMA is then pointed at the next free buffer.
 * Gain over the ABI is linear, 256 = 1x (gain_unity 256); the kernel turns it into the Sony code
 * (code = 1024 - 1024 * 256 / gain, the OnePlus drivers' gain2reg with BASEGAIN 64 -> 1024 scale).
 */
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/uio.h>

#define REDWOOD_CAMERA_MAGIC "RWFRAME1"
struct redwood_camera_frame {
	char magic[8]; uint32_t header_bytes, width, height, stride, format, bayer, black;
	uint64_t sequence, time;
	uint32_t exposure_lines, line_ns, gain, gain_unity, gain_max, frame_lines, frame_lines_min, frame_lines_max, margin, sensor;
	uint32_t reserved[6];
};
enum { REDWOOD_CAMERA_EXPOSURE = 1, REDWOOD_CAMERA_KEEP = 2, REDWOOD_CAMERA_SENSOR = 3 };
struct redwood_camera_command { uint32_t command, bytes, a, b, c, d; };

#define N2D_NBUF 3
#define N2D_FILL 0x5a
#define N2D_IDLE_MS 4000u

/* per sensor (index into n2c_sensors): ABI number, Bayer order and black level of its preview mode, line time */
struct n2d_info { uint32_t abi, bayer, black, line_ns, frame_lines, margin, gain_max; };
static const struct n2d_info n2d_info[N2C_NSENSORS] = {
	/* IMX766: 4096x3072 QBIN, Bayer B first (SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_B, HV mirror) = BGGR; 15616/1488 MHz */
	[N2C_IMX766] = { 1, 3, 64, 10495, 3176, 48, 64 * 256 },
	/* IMX615: 3280x2464, R first (RAW_4CELL_HW_BAYER_R, normal) = RGGB; 3768/597.6 MHz */
	[N2C_IMX615] = { 2, 0, 64, 6305, 2642, 48, 16 * 256 },
	/* IMX355: 3264x2448, B first (RAW_B, HV mirror) = BGGR; 3672/280.8 MHz */
	[N2C_IMX355] = { 3, 3, 64, 13077, 2548, 10, 16 * 256 },
	[N2C_GC02M1B] = { 0, 0, 64, 26095, 1276, 16, 12 * 256 },
};

static LCK_GRP_DECLARE(n2d_grp, "nord2-camera");
static LCK_MTX_DECLARE(n2d_lock, &n2d_grp);
enum { N2D_FREE, N2D_DMA, N2D_READY, N2D_READING };
static struct { int state; uint64_t seq, time; uint32_t exp, gain, frame; } n2d_slot[N2D_NBUF];
static vm_offset_t n2d_buf[N2D_NBUF];
static uint64_t n2d_pa[N2D_NBUF];
static uint32_t n2d_bufsz;
static unsigned n2d_sensor = N2C_IMX766, n2d_want_sensor = N2C_IMX766;
static int n2d_dma = -1;                       /* the buffer the DMA writes now */
static uint32_t n2d_opens;
static bool n2d_wanted, n2d_streaming, n2d_reading, n2d_broken, n2d_made;
static uint64_t n2d_seq, n2d_given, n2d_last_read_ms;
static struct { bool pending; uint32_t lines, gain, frame; } n2d_ask;
static uint32_t n2d_cur_exp = 1000, n2d_cur_gain = 256, n2d_cur_frame;
static uint8_t n2d_chunk[8 * 5120];

static uint64_t
n2d_ms(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / NSEC_PER_MSEC;
}

static uint32_t n2d_stride(unsigned s) { return n2c_sensors[s].w * 10 / 8; }

static bool
n2d_alloc(uint32_t bytes)
{
	if (bytes > N2V_BUF_BYTES - 4096) { return false; }
	if (n2d_buf[0]) { return true; }
	for (unsigned i = 0; i < N2D_NBUF; i++) {            /* the fixed claim below 4 GB (nord2_camsv.inc), 3 x 16 MiB */
		n2d_pa[i] = N2V_BUF_PA + (uint64_t)i * N2V_BUF_BYTES;
		n2d_buf[i] = ml_io_map_unmappable(n2d_pa[i], N2V_BUF_BYTES, VM_WIMG_DEFAULT);
		if (!n2d_buf[i]) { printf("NORD2 CAMERA: buffer %u cannot be mapped\n", i); return false; }
	}
	n2d_bufsz = N2V_BUF_BYTES;
	return true;
}

/* rails, the MDP and CAM domains, clocks, SMI: once, at the first session (n2c_t1's first half) */
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

static void
n2d_arm(unsigned b)                            /* the fill pattern at the end of buffer b, then the DMA's */
{
	uint32_t size = n2d_stride(n2d_sensor) * n2c_sensors[n2d_sensor].h;
	memset((void *)(n2d_buf[b] + size - 64), N2D_FILL, 64);
	n2v_inval(n2d_buf[b] + size - 64, 64);
	n2d_slot[b].state = N2D_DMA;
	n2d_dma = (int)b;
	n2v_enqueue(n2v_sv[N2V_SV], n2d_pa[b], size);
}

/* Sony exposure, gain and frame length (OnePlus write_shutter / set_gain: 0x0104 group hold) */
static void
n2d_set_exposure(uint32_t lines, uint32_t gain, uint32_t frame)
{
	const struct n2d_info *in = &n2d_info[n2d_sensor];
	if (gain < 256) { gain = 256; }
	if (gain > in->gain_max) { gain = in->gain_max; }
	if (!frame || frame < lines + in->margin) { frame = lines + in->margin; }
	if (frame < in->frame_lines) { frame = in->frame_lines; }
	if (frame > 0xffff) { frame = 0xffff; }
	if (lines > frame - in->margin) { lines = frame - in->margin; }
	if (lines < 1) { lines = 1; }
	uint32_t code = 1024 - (1024 * 256) / gain;
	n2c_s = &n2c_sensors[n2d_sensor];
	(void)n2c_s_wr(0x0104, 1);
	(void)n2c_s_wr(0x0340, (uint8_t)(frame >> 8)); (void)n2c_s_wr(0x0341, (uint8_t)frame);
	(void)n2c_s_wr(0x0202, (uint8_t)(lines >> 8)); (void)n2c_s_wr(0x0203, (uint8_t)lines);
	(void)n2c_s_wr(0x0204, (uint8_t)(code >> 8)); (void)n2c_s_wr(0x0205, (uint8_t)code);
	(void)n2c_s_wr(0x0104, 0);
	n2d_cur_exp = lines; n2d_cur_gain = gain; n2d_cur_frame = frame;
}

static bool
n2d_start(unsigned s)
{
	const struct n2c_sensor *se = &n2c_sensors[s];
	uint32_t stride = n2d_stride(s), size = stride * se->h;
	if (se->port < 0) { printf("NORD2 CAMERA: %s has no receiver port yet\n", se->name); return false; }
	if (!n2d_platform() || !n2d_alloc(5120 * 3072 + 4096)) { return false; }
	n2c_s = se;
	n2d_sensor = s;
	if (!n2c_power(s, true)) { (void)n2c_power(s, false); return false; }
	n2c_bus_init(se->bus);
	int hi = n2c_s_rd(se->idreg_hi), lo = n2c_s_rd(se->idreg_lo);
	if (hi != (se->id >> 8) || lo != (se->id & 0xff)) {
		printf("NORD2 CAMERA: %s chip id %#x %#x: not there\n", se->name, hi, lo);
		(void)n2c_power(s, false);
		return false;
	}
	(void)n2c_s_table(se->init, se->init_n);
	(void)n2c_s_wr(0x0101, se->mirror);
	(void)n2c_s_table(se->mode, se->mode_n);
	n2d_set_exposure(n2d_info[s].frame_lines / 2, 2 * 256, 0);
	for (unsigned k = 0; k < N2D_NBUF; k++) { memset((void *)(n2d_buf[k] + size - 64), N2D_FILL, 64); n2v_inval(n2d_buf[k] + size - 64, 64); n2d_slot[k].state = N2D_FREE; }
	/* the receiver and CAMSV armed on buffer 0 first, then the stream */
	n2v_xoff = s == N2C_IMX615 ? 8 : 0;
	n2s_rx(se->port, se->cphy, se->lanes, se->pixel_rate);
	n2v_cammux(se->w, se->h);
	n2d_slot[0].state = N2D_DMA; n2d_dma = 0;
	n2v_camsv_cfg(n2v_sv[N2V_SV], se->w, se->h, stride, n2d_pa[0]);
	n2v_camsv_go(n2v_sv[N2V_SV]);
	(void)n2c_s_wr(0x0100, 1);
	n2d_sensor = s;
	printf("NORD2 CAMERA: %s streaming %ux%u RAW10\n", se->name, se->w, se->h);
	return true;
}

static void
n2d_stop(unsigned s)
{
	n2v_camsv_stop(n2v_sv[N2V_SV]);
	n2c_s = &n2c_sensors[s];
	(void)n2c_s_wr(0x0100, 0);
	n2c_sleep(40);
	(void)n2c_power(s, false);
	n2d_dma = -1;
}

/* a session: one sensor until nobody reads */
static bool
n2d_session(unsigned s)
{
	uint32_t size = n2d_stride(s) * n2c_sensors[s].h;
	uint64_t last_frame = n2d_ms();
	if (!n2d_start(s)) { return false; }
	lck_mtx_lock(&n2d_lock);
	n2d_given = 0; n2d_streaming = true; n2d_last_read_ms = n2d_ms();
	lck_mtx_unlock(&n2d_lock);
	for (;;) {
		bool stop = false;
		int b = n2d_dma;
		if (b >= 0) {
			n2v_inval(n2d_buf[b] + size - 64, 64);
			const volatile uint8_t *tail = (const volatile uint8_t *)(n2d_buf[b] + size - 64);
			bool done = false;
			for (unsigned k = 0; k < 64; k++) { if (tail[k] != N2D_FILL) { done = true; break; } }
			if (done) {
				lck_mtx_lock(&n2d_lock);
				for (unsigned i = 0; i < N2D_NBUF; i++) { if (n2d_slot[i].state == N2D_READY) { n2d_slot[i].state = N2D_FREE; } }
				n2d_slot[b].state = N2D_READY;
				n2d_slot[b].seq = ++n2d_seq;
				n2d_slot[b].time = mach_absolute_time();
				n2d_slot[b].exp = n2d_cur_exp; n2d_slot[b].gain = n2d_cur_gain; n2d_slot[b].frame = n2d_cur_frame;
				int next = -1;
				for (unsigned i = 1; i <= N2D_NBUF; i++) { unsigned c = ((unsigned)b + i) % N2D_NBUF; if (n2d_slot[c].state == N2D_FREE) { next = (int)c; break; } }
				wakeup(&n2d_seq);
				lck_mtx_unlock(&n2d_lock);
				if (next >= 0) { n2d_arm((unsigned)next); } else { n2d_dma = -1; }
				last_frame = n2d_ms();
			}
		} else {
			lck_mtx_lock(&n2d_lock);
			for (unsigned i = 0; i < N2D_NBUF; i++) { if (n2d_slot[i].state == N2D_FREE) { lck_mtx_unlock(&n2d_lock); n2d_arm(i); lck_mtx_lock(&n2d_lock); break; } }
			lck_mtx_unlock(&n2d_lock);
		}
		lck_mtx_lock(&n2d_lock);
		if (n2d_ask.pending) {
			uint32_t l = n2d_ask.lines, g = n2d_ask.gain, f = n2d_ask.frame;
			n2d_ask.pending = false;
			lck_mtx_unlock(&n2d_lock);
			n2d_set_exposure(l, g, f);
			lck_mtx_lock(&n2d_lock);
		}
		if (n2d_opens == 0 || n2d_ms() - n2d_last_read_ms > N2D_IDLE_MS || n2d_want_sensor != s) {
			stop = true;
		} else if (n2d_ms() - last_frame > 3000u) {
			printf("NORD2 CAMERA: no frame for 3 s\n");
			stop = true;
		}
		lck_mtx_unlock(&n2d_lock);
		if (stop) { break; }
		n2c_sleep(3);
	}
	lck_mtx_lock(&n2d_lock);
	n2d_streaming = false;
	lck_mtx_unlock(&n2d_lock);
	n2d_stop(s);
	printf("NORD2 CAMERA: session of %s stopped after %llu frames\n", n2c_sensors[s].name, n2d_seq);
	return n2d_ms() - last_frame <= 3000u;
}

static int
n2d_open(__unused dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	if (n2d_broken) { return EIO; }
	lck_mtx_lock(&n2d_lock); n2d_opens++; lck_mtx_unlock(&n2d_lock);
	return 0;
}

static int
n2d_close(__unused dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	lck_mtx_lock(&n2d_lock); if (n2d_opens) { n2d_opens--; } lck_mtx_unlock(&n2d_lock);
	return 0;
}

static int
n2d_read(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	struct redwood_camera_frame f;
	uint64_t since = n2d_ms();
	int b, error = 0;
	lck_mtx_lock(&n2d_lock);
	if (n2d_reading) { lck_mtx_unlock(&n2d_lock); return EBUSY; }
	n2d_reading = true;
	n2d_last_read_ms = n2d_ms();
	for (;;) {
		if (n2d_broken) { error = EIO; break; }
		b = -1;
		for (int i = 0; i < N2D_NBUF; i++) {
			if (n2d_slot[i].state == N2D_READY && n2d_slot[i].seq > n2d_given && (b < 0 || n2d_slot[i].seq > n2d_slot[b].seq)) { b = i; }
		}
		if (b >= 0 && n2d_streaming && n2d_sensor == n2d_want_sensor) { break; }
		if (!n2d_streaming && !n2d_wanted) { n2d_wanted = true; wakeup(&n2d_wanted); }
		if (n2d_ms() - since > (n2d_streaming ? 3000u : 20000u)) { error = EAGAIN; break; }
		struct timespec ts = { 0, 50 * NSEC_PER_MSEC };
		error = msleep(&n2d_seq, &n2d_lock, PZERO | PCATCH, "nord2-camera", &ts);
		if (error == EWOULDBLOCK) { error = 0; }
		if (error) { break; }
		n2d_last_read_ms = n2d_ms();
	}
	if (error) { n2d_reading = false; lck_mtx_unlock(&n2d_lock); return error; }
	unsigned s = n2d_sensor;
	const struct n2c_sensor *se = &n2c_sensors[s];
	uint32_t stride = n2d_stride(s);
	n2d_slot[b].state = N2D_READING;
	n2d_given = n2d_slot[b].seq;
	bzero(&f, sizeof(f));
	memcpy(f.magic, REDWOOD_CAMERA_MAGIC, 8);
	f.header_bytes = sizeof(f);
	f.width = se->w; f.height = se->h; f.stride = stride; f.format = 1;
	f.bayer = n2d_info[s].bayer; f.black = n2d_info[s].black;
	f.sequence = n2d_slot[b].seq; f.time = n2d_slot[b].time;
	f.exposure_lines = n2d_slot[b].exp; f.line_ns = n2d_info[s].line_ns;
	f.gain = n2d_slot[b].gain; f.gain_unity = 256; f.gain_max = n2d_info[s].gain_max;
	f.frame_lines = n2d_slot[b].frame; f.frame_lines_min = n2d_info[s].frame_lines; f.frame_lines_max = 0xffff;
	f.margin = n2d_info[s].margin; f.sensor = n2d_info[s].abi;
	lck_mtx_unlock(&n2d_lock);
	error = uiomove((caddr_t)&f, (int)MIN(sizeof(f), (size_t)uio_resid(uio)), uio);
	for (uint32_t y = 0; y < se->h && !error && uio_resid(uio) > 0; y += 8) {
		uint32_t n = MIN(8u, se->h - y);
		n2v_inval(n2d_buf[b] + y * stride, n * stride);
		/* the DMA packs 4 pixels as one little-endian 40-bit word (PAK mode 2); the ABI is MIPI RAW10: 4 high bytes, then the low bits */
		const uint8_t *src = (const uint8_t *)(n2d_buf[b] + y * stride);
		for (uint32_t k = 0; k + 5 <= n * stride; k += 5) {
			uint64_t x = (uint64_t)src[k] | (uint64_t)src[k + 1] << 8 | (uint64_t)src[k + 2] << 16 | (uint64_t)src[k + 3] << 24 | (uint64_t)src[k + 4] << 32;
			uint32_t p0 = (uint32_t)x & 0x3ff, p1 = (uint32_t)(x >> 10) & 0x3ff, p2 = (uint32_t)(x >> 20) & 0x3ff, p3 = (uint32_t)(x >> 30) & 0x3ff;
			n2d_chunk[k] = (uint8_t)(p0 >> 2); n2d_chunk[k + 1] = (uint8_t)(p1 >> 2); n2d_chunk[k + 2] = (uint8_t)(p2 >> 2); n2d_chunk[k + 3] = (uint8_t)(p3 >> 2);
			n2d_chunk[k + 4] = (uint8_t)((p0 & 3) | (p1 & 3) << 2 | (p2 & 3) << 4 | (p3 & 3) << 6);
		}
		error = uiomove((caddr_t)n2d_chunk, (int)MIN((user_ssize_t)(n * stride), uio_resid(uio)), uio);
	}
	lck_mtx_lock(&n2d_lock);
	n2d_slot[b].state = N2D_FREE;
	n2d_reading = false;
	n2d_last_read_ms = n2d_ms();
	lck_mtx_unlock(&n2d_lock);
	return error;
}

static int
n2d_write(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	struct redwood_camera_command c = { 0, 0, 0, 0, 0, 0 };
	if (uio_resid(uio) < (user_ssize_t)sizeof(c) || uiomove((caddr_t)&c, sizeof(c), uio) != 0) { return EINVAL; }
	switch (c.command) {
	case REDWOOD_CAMERA_EXPOSURE:
		lck_mtx_lock(&n2d_lock); n2d_ask.lines = c.a; n2d_ask.gain = c.b; n2d_ask.frame = c.c; n2d_ask.pending = true; lck_mtx_unlock(&n2d_lock);
		return 0;
	case REDWOOD_CAMERA_SENSOR:
		for (unsigned s = 0; s < N2C_NSENSORS; s++) {
			if (n2d_info[s].abi == c.a && c.a != 0) {
				lck_mtx_lock(&n2d_lock); n2d_want_sensor = s; lck_mtx_unlock(&n2d_lock);
				printf("NORD2 CAMERA: sensor %u asked (%s)\n", c.a, n2c_sensors[s].name);
				return 0;
			}
		}
		return EINVAL;
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

static void
n2d_thread(void *arg __unused, wait_result_t wr __unused)
{
	unsigned failed = 0;
	int major = cdevsw_add(-1, &n2d_cdevsw);
	n2d_made = major >= 0 && devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "redwood-camera") != NULL;
	printf("NORD2 CAMERA: /dev/redwood-camera %s (sensors: 1 wide IMX766, 2 front IMX615, 3 ultra-wide IMX355)\n", n2d_made ? "made" : "CANNOT BE MADE");
	while (n2d_made && !n2d_broken) {
		struct timespec ts = { 1, 0 };
		lck_mtx_lock(&n2d_lock);
		while (!n2d_wanted) { (void)msleep(&n2d_wanted, &n2d_lock, PZERO, "nord2-camera-idle", &ts); }
		n2d_wanted = false;
		unsigned s = n2d_want_sensor;
		bool any = n2d_opens != 0;
		lck_mtx_unlock(&n2d_lock);
		if (!any) { continue; }
		if (!n2d_session(s)) {
			if (++failed >= 5) { printf("NORD2 CAMERA: five sessions failed: given up\n"); n2d_broken = true; }
		} else {
			failed = 0;
		}
	}
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}
