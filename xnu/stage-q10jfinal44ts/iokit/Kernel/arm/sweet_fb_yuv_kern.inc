/*
 * 0490 (yuvovl-20261113): kernel side of sweet_fb_yuv.h, included once by SweetFramebuffer.cpp (NORD2 only, after the 0396
 * sysctls).  kern.n2fbyuv (RW, default 0 = off: YUV surfaces are refused by the planner as before) and kern.n2fbyuvstat (RO).
 * No boot-arg (the UI command line is full).  Counters are the present thread's; the sysctls only read them.
 */
static uint32_t sweet_fb_yuv_mode;      /* kern.n2fbyuv */
static struct {
	uint64_t frames, layers;            /* HW frames with a YUV layer / YUV layers in them */
	uint64_t seen_off, seen_biplanar, seen_bad;   /* planner inputs refused: packed 4:2:2 while off / 4:2:0 two planes / odd layout */
	uint32_t last_con, last_fcc, logs;
} sweet_fb_yuv_st;

static int
sweet_fb_yuv_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_yuv_mode, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || !sweet_fb_yuv_mode_valid((uint32_t)v)) { return EINVAL; }
	__atomic_store_n(&sweet_fb_yuv_mode, (uint32_t)v, __ATOMIC_RELAXED);
	IOLog("NORD2 FB YUV: kern.n2fbyuv %#x (packed 4:2:2 overlay layers %s, matrix %u (0 = auto), byte swap %u, uyvy/yuyv exchanged %u)\n", (uint32_t)v,
	    (v & SWEET_FB_YUV_MODE_ON) ? "on" : "off", SWEET_FB_YUV_MODE_MTX((uint32_t)v), (v & SWEET_FB_YUV_MODE_SWAP) ? 1u : 0u,
	    (v & SWEET_FB_YUV_MODE_XCHG) ? 1u : 0u);
	return 0;
}
static int
sweet_fb_yuv_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[256];
	snprintf(buf, sizeof(buf), "mode=%#x frames=%llu layers=%llu seen_off=%llu seen_biplanar=%llu seen_bad=%llu last_con=%08x last_fcc=%08x",
	    __atomic_load_n(&sweet_fb_yuv_mode, __ATOMIC_RELAXED), sweet_fb_yuv_st.frames, sweet_fb_yuv_st.layers, sweet_fb_yuv_st.seen_off,
	    sweet_fb_yuv_st.seen_biplanar, sweet_fb_yuv_st.seen_bad, sweet_fb_yuv_st.last_con, sweet_fb_yuv_st.last_fcc);
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
SYSCTL_PROC(_kern, OID_AUTO, n2fbyuv, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_yuv_sysctl, "I",
    "0490 packed 4:2:2 overlay layers: bit 0 on, bits 7..4 matrix code (0 auto), bit 8 byte swap, bit 9 uyvy/yuyv exchanged");
SYSCTL_PROC(_kern, OID_AUTO, n2fbyuvstat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, sweet_fb_yuv_sysctl_stat, "A",
    "0490 YUV overlay layer counters");

/* hwInputs: the L_CON format bits of a surface that is not BGRA, 0 = still not an overlay layer (the planner says "format") */
static inline uint32_t
sweet_fb_yuv_layer_con(const SweetSurface *s)
{
	uint32_t mode = __atomic_load_n(&sweet_fb_yuv_mode, __ATOMIC_RELAXED);
	switch (sweet_fb_yuv_classify(s->pixelFormat, s->planeCount, s->bytesPerElement, s->elementWidth, s->elementHeight)) {
	case SWEET_FB_YUV_UYVY:
	case SWEET_FB_YUV_YUYV:
		if (!(mode & SWEET_FB_YUV_MODE_ON)) { sweet_fb_yuv_st.seen_off++; }
		break;
	case SWEET_FB_YUV_BIPLANAR:
		sweet_fb_yuv_st.seen_biplanar++;
		return 0u;
	case SWEET_FB_YUV_BAD:
		sweet_fb_yuv_st.seen_bad++;
		return 0u;
	default:
		return 0u;
	}
	return sweet_fb_yuv_con(mode, s->pixelFormat, s->planeCount, s->bytesPerElement, s->elementWidth, s->elementHeight, s->width, s->height);
}

/* a planned and applied HW frame: counts its YUV layers; the first 8 and every change of the format bits are logged */
static inline void
sweet_fb_yuv_note(const struct sweet_fb_hw_in *in, unsigned n, const uint32_t *ids, SweetSurface *const *ss)
{
	unsigned y = 0;
	for (unsigned i = 0; i < n; i++) {
		if (in[i].format_ok <= 1u) { continue; }
		y++;
		sweet_fb_yuv_st.layers++;
		if (sweet_fb_yuv_st.logs < 8 || in[i].format_ok != sweet_fb_yuv_st.last_con) {
			sweet_fb_yuv_st.logs++;
			IOLog("NORD2 FB YUV: layer %u id %u fcc %08x %ux%u bpr %u -> CON format %08x (cfmt %u mtx %u swap %u) src %d,%d %dx%d at %d,%d"
			    " (layers=%llu frames=%llu)\n", i, ids[i], ss[i]->pixelFormat, ss[i]->width, ss[i]->height, in[i].pitch, in[i].format_ok,
			    (in[i].format_ok >> 12) & 0xfu, (in[i].format_ok >> 16) & 0xfu, (in[i].format_ok >> 24) & 1u, in[i].sx, in[i].sy, in[i].w, in[i].h,
			    in[i].dx, in[i].dy, sweet_fb_yuv_st.layers, sweet_fb_yuv_st.frames + 1u);
		}
		sweet_fb_yuv_st.last_con = in[i].format_ok; sweet_fb_yuv_st.last_fcc = ss[i]->pixelFormat;
	}
	if (y != 0) { sweet_fb_yuv_st.frames++; }
}
