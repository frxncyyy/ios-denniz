/*
 * A fake kernel with the two nodes of KERNEL_ABI_DUAL.md, for the host tests (t_dual.c, t_pair.c): /dev/redwood-camera (by itself sensor 1) and
 * /dev/redwood-camera2 (sensor 3), each with its own session, exposure and frame counter; a sensor the other node has is EBUSY (for
 * REDWOOD_CAMERA_SENSOR and for a read that would start it); a read of the header's size copies no frame; the header carries the frame's levels,
 * which follow the exposure asked for two frames earlier (a scene of a fixed brightness for each sensor).
 */
/* ---- the fake kernel: two nodes ---- */
static const int fw[4] = { 0, 64, 80, 48 }, fh[4] = { 0, 48, 60, 36 };       /* sensor 1 wide, 2 front, 3 ultra-wide */
static const double scene[4] = { 0, 0.004, 0.02, 0.0005 };                   /* green above black per line of exposure at gain 1 */
static pthread_mutex_t klock = PTHREAD_MUTEX_INITIALIZER;
static struct knode {
	int opens;                      /* descriptors open */
	int sensor;                     /* the sensor asked for */
	bool running;                   /* its session has started (a read has) */
	uint64_t seq;
	uint32_t lines, gain, pending_lines[3], pending_gain[3];
	uint64_t header_reads, whole_reads, exposures, sensor_cmds, busy_given;
} kn[2] = { { .sensor = 1 }, { .sensor = 3 } };
static atomic_int node2_there = 1, node2_dead, no_levels, busy_first;
static atomic_int total_opens, total_closes;

static void session_reset(struct knode *k) { k->lines = 100; k->gain = 256; for (int i = 0; i < 3; i++) { k->pending_lines[i] = 100; k->pending_gain[i] = 256; } }

static int fake_open_node(int node)
{
	if (node == 1 && !node2_there) { errno = ENOENT; return -1; }
	pthread_mutex_lock(&klock);
	if (kn[node].opens++ == 0) { kn[node].running = false; session_reset(&kn[node]); }
	pthread_mutex_unlock(&klock);
	atomic_fetch_add(&total_opens, 1);
	return 10 + node;
}
static int fake_open(void) { return fake_open_node(0); }
static void fake_close(int fd)
{
	pthread_mutex_lock(&klock);
	struct knode *k = &kn[fd - 10];

	if (--k->opens == 0) { k->running = false; k->sensor = fd - 10 == 0 ? 1 : 3; }  /* (closed by all: back to its own sensor) */
	pthread_mutex_unlock(&klock);
	atomic_fetch_add(&total_closes, 1);
}

static ssize_t fake_read(int fd, void *buf, size_t n)
{
	struct knode *k = &kn[fd - 10], *o = &kn[1 - (fd - 10)];
	struct redwood_camera_frame h;

	usleep(700);                    /* a frame's time */
	pthread_mutex_lock(&klock);
	if (fd - 10 == 1 && node2_dead) { pthread_mutex_unlock(&klock); errno = EAGAIN; return -1; }
	if (!k->running && o->opens > 0 && o->running && o->sensor == k->sensor) {
		k->busy_given++;
		pthread_mutex_unlock(&klock);
		errno = EBUSY;
		return -1;
	}
	if (!k->running) { k->running = true; session_reset(k); }
	int s = k->sensor;
	size_t stride = (size_t)fw[s] * 5 / 4, total = sizeof(h) + stride * fh[s];

	/* the exposure asked for is in the frames two frames later */
	k->lines = k->pending_lines[0]; k->gain = k->pending_gain[0];
	k->pending_lines[0] = k->pending_lines[1]; k->pending_gain[0] = k->pending_gain[1];
	k->pending_lines[1] = k->pending_lines[2]; k->pending_gain[1] = k->pending_gain[2];
	double green = scene[s] * k->lines * k->gain / 256.0;

	green = green > 959 ? 959 : green;
	memset(&h, 0, sizeof(h));
	memcpy(h.magic, REDWOOD_CAMERA_MAGIC, 8);
	h.header_bytes = sizeof(h); h.width = fw[s]; h.height = fh[s]; h.stride = (uint32_t)stride; h.format = 1; h.bayer = 0; h.black = 64;
	h.sequence = ++k->seq; h.exposure_lines = k->lines; h.line_ns = 10000; h.gain = k->gain; h.gain_unity = 256; h.gain_max = 4096;
	h.frame_lines = 3300; h.frame_lines_min = 3300; h.frame_lines_max = 6600; h.margin = 8; h.sensor = (uint32_t)s;
	h.time = k->seq * 33000000ull;
	if (!no_levels) { h.stat_samples = 64; h.stat_green = (uint32_t)green; h.stat_red = (uint32_t)(green * 0.5); h.stat_blue = (uint32_t)(green * 0.6); }
	if (n == sizeof(h)) {
		k->header_reads++;
		pthread_mutex_unlock(&klock);
		memcpy(buf, &h, sizeof(h));
		return (ssize_t)sizeof(h);
	}
	k->whole_reads++;
	pthread_mutex_unlock(&klock);
	size_t m = n < total ? n : total;
	uint8_t *tmp = calloc(1, total);

	memcpy(tmp, &h, sizeof(h));
	/* every pixel at the level (MIPI RAW10: the high bytes; grey, so that the white balance has something) */
	{
		unsigned v = (unsigned)green + 64;

		for (size_t i = 0; i < stride * (size_t)fh[s]; i++) { tmp[sizeof(h) + i] = i % 5 == 4 ? 0 : (uint8_t)(v >> 2); }
	}
	memcpy(buf, tmp, m);
	free(tmp);
	return (ssize_t)m;
}

static ssize_t fake_write(int fd, const void *buf, size_t n)
{
	const struct redwood_camera_command *c = buf;
	struct knode *k = &kn[fd - 10], *o = &kn[1 - (fd - 10)];

	if (n != sizeof(*c)) { errno = EINVAL; return -1; }
	pthread_mutex_lock(&klock);
	if (c->command == REDWOOD_CAMERA_SENSOR) {
		if (c->a < 1 || c->a > 3) { pthread_mutex_unlock(&klock); errno = EINVAL; return -1; }
		if ((o->opens > 0 && o->sensor == (int)c->a) || busy_first > 0) {
			if (busy_first > 0) { atomic_fetch_sub(&busy_first, 1); }
			k->busy_given++;
			pthread_mutex_unlock(&klock);
			errno = EBUSY;
			return -1;
		}
		k->sensor = (int)c->a; k->running = false; k->sensor_cmds++;
	} else if (c->command == REDWOOD_CAMERA_EXPOSURE) {
		k->pending_lines[2] = c->a; k->pending_gain[2] = c->b; k->exposures++;
	}
	pthread_mutex_unlock(&klock);
	return (ssize_t)n;
}

