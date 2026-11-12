/*
 * The cameras' frames made into pictures by the processor (see camera_isp.h): one pixel per 2x2
 * cell, the nearest cell for each pixel of the picture (previews), or every pixel by bilinear
 * demosaic (stills); per pixel, in this order: the black level off, lens shading (a grid of gains per
 * colour, when there is one), the white balance's and AE's gains, the colour matrix (identity until
 * one is set), sRGB's curve. Grey-world white balance; no noise reduction yet.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "camera_isp.h"

/* The black level comes with every frame (f->black, 10-bit steps); white is 1023 less it. */
#define LUT_WHITE       3836            /* white in the gamma table's steps */
static inline int
white_of(const struct redwood_camera_frame *f)
{
	int w = 1023 - (int)f->black;

	return w < 64 ? 64 : w;
}
#define ISP_CONTRAST    0.2             /* the share of the S-curve in the tone curve (0: plain sRGB) */

void
isp_init(struct isp *isp)
{
	memset(isp, 0, sizeof(*isp));
	isp->flicker_ns = 10000000;     /* 50 Hz mains: a hundred half-waves a second */
	isp->aim = isp->hidden_aim = 1;
	isp->wb[0] = isp->wb[1] = isp->wb[2] = 1;
	isp->digital = 1;
	/* a Sony phone sensor's gains in daylight and in a warm lamp's light, roughly (none of the three sensors' own are known: no calibration) */
	isp->wb_day[0] = 2.0f; isp->wb_day[1] = 1.7f;
	isp->wb_warm[0] = 1.3f; isp->wb_warm[1] = 2.9f;
	isp->wb_adapt = 0.8f;
	/* The colour matrix of a phone's sensor, not this one's (its vendor tuning is not read): the raw colours of a CMOS sensor with a
	 * Bayer filter are washed out, and the stock camera's matrix puts about 1.4 on the diagonal and takes the rest from the other two;
	 * rows sum to 1, so a grey stays grey. Camera.app's picture was flat with the identity matrix (run 575). */
	static const float default_ccm[9] = {
		1.42f, -0.28f, -0.14f,
		-0.20f, 1.38f, -0.18f,
		-0.04f, -0.36f, 1.40f,
	};
	isp_set_ccm(isp, default_ccm);
	for (int i = 0; i < 4096; i++) {
		double v = i >= LUT_WHITE ? 1 : (double)i / LUT_WHITE;

		v = v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1 / 2.4) - 0.055;
		/* a mild S-curve over sRGB's (smoothstep, a fifth of the way): the stock camera's contrast */
		v = v * (1 - ISP_CONTRAST) + (v * v * (3 - 2 * v)) * ISP_CONTRAST;
		isp->gamma[i] = (uint8_t)(v * 255 + 0.5);
	}
}

/* where red and blue are in a cell (0 top left, 1 top right, 2 bottom left, 3 bottom right) */
static void
cell_places(uint32_t bayer, int *r, int *b)
{
	static const int red[4] = { 0, 1, 2, 3 }, blue[4] = { 3, 2, 1, 0 };

	*r = red[bayer & 3];
	*b = blue[bayer & 3];
}

/* the four pixels of a group of five bytes: MIPI RAW10's (format 1) or the CAMSV DMA's little-endian 40-bit word (format 4) */
static inline void
group(const uint8_t *g, uint32_t format, int p[4])
{
	if (format == REDWOOD_CAMERA_FORMAT_LE40_RAW10) {
		uint64_t x = (uint64_t)g[0] | (uint64_t)g[1] << 8 | (uint64_t)g[2] << 16 | (uint64_t)g[3] << 24 | (uint64_t)g[4] << 32;

		p[0] = (int)(x & 0x3ff);
		p[1] = (int)(x >> 10 & 0x3ff);
		p[2] = (int)(x >> 20 & 0x3ff);
		p[3] = (int)(x >> 30 & 0x3ff);
	} else {
		p[0] = g[0] << 2 | (g[4] & 3);
		p[1] = g[1] << 2 | (g[4] >> 2 & 3);
		p[2] = g[2] << 2 | (g[4] >> 4 & 3);
		p[3] = g[3] << 2 | (g[4] >> 6 & 3);
	}
}

/* the four values of the cell at cx of the pair of lines from `top' (above black, 0 and up) */
static inline void
cell(const uint8_t *top, size_t stride, uint32_t cx, int v[4], int black, uint32_t format)
{
	const uint8_t *g0 = top + (size_t)(cx >> 1) * 5, *g1 = g0 + stride;
	unsigned o = (cx & 1) * 2;

	if (format == REDWOOD_CAMERA_FORMAT_LE40_RAW10) {
		int a[4], b[4];

		group(g0, format, a);
		group(g1, format, b);
		v[0] = a[o] - black;
		v[1] = a[o + 1] - black;
		v[2] = b[o] - black;
		v[3] = b[o + 1] - black;
	} else {
		v[0] = (g0[o] << 2 | (g0[4] >> (o * 2) & 3)) - black;
		v[1] = (g0[o + 1] << 2 | (g0[4] >> (o * 2 + 2) & 3)) - black;
		v[2] = (g1[o] << 2 | (g1[4] >> (o * 2) & 3)) - black;
		v[3] = (g1[o + 1] << 2 | (g1[4] >> (o * 2 + 2) & 3)) - black;
	}
	for (int i = 0; i < 4; i++) {
		v[i] = v[i] < 0 ? 0 : v[i];
	}
}

void
isp_measure(const struct redwood_camera_frame *f, const uint8_t *raw, struct isp_stats *s)
{
	isp_measure_window(f, raw, 0, 0, f->width, f->height, s);
}

void
isp_measure_window(const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, struct isp_stats *s)
{
	uint32_t cells_w = f->width / 2, cells_h = f->height / 2, x0 = wx / 2, y0 = wy / 2, x1 = (wx + ww) / 2, y1 = (wy + wh) / 2;
	double sum[3] = { 0, 0, 0 };
	int r, b;

	memset(s, 0, sizeof(*s));
	cell_places(f->bayer, &r, &b);
	/* every 4th cell of every 4th pair of lines; of a big frame every 8th of every 8th (49 000 cells of a 4096x3072 one: a quarter of the memory
	 * walked, which is what the time goes on -- run q10hc: 15 to 21 ms a frame at every 4th) */
	x1 = x1 > cells_w ? cells_w : x1;
	y1 = y1 > cells_h ? cells_h : y1;
	x0 = x0 < x1 ? x0 : 0;
	y0 = y0 < y1 ? y0 : 0;
	uint32_t step = x1 - x0 >= 1024 && y1 - y0 >= 768 ? 8 : x1 - x0 >= 256 && y1 - y0 >= 192 ? 4 : 2;

	step = cells_w < 256 ? 4 : step;                /* (the small frames of the tests: as before) */
	double fair[3] = { 0, 0, 0 }, grey[3] = { 0, 0, 0 };
	int fair_top = white_of(f) * 85 / 100;

	for (uint32_t cy = y0; cy < y1; cy += step) {
		const uint8_t *top = raw + (size_t)cy * 2 * f->stride;

		for (uint32_t cx = x0; cx < x1; cx += step) {
			int v[4], g = 0;

			cell(top, f->stride, cx, v, (int)f->black, f->format);
			for (int i = 0; i < 4; i++) {
				g += i != r && i != b ? v[i] : 0;
			}
			g /= 2;
			sum[0] += v[r];
			sum[1] += g;
			sum[2] += v[b];
			if (g >= 8 && v[r] < fair_top && g < fair_top && v[b] < fair_top) {
				fair[0] += v[r];
				fair[1] += g;
				fair[2] += v[b];
				s->fair_cells++;
				if (g >= v[r] && 10 * g <= 28 * v[r] && 10 * g >= 11 * v[b] && 10 * g <= 36 * v[b]) {
					grey[0] += v[r];
					grey[1] += g;
					grey[2] += v[b];
					s->grey_cells++;
				}
			}
			s->hist[g >> 4 > 63 ? 63 : g >> 4]++;
			s->cells++;
		}
	}
	for (int c = 0; c < 3; c++) {
		s->mean[c] = s->cells ? sum[c] / s->cells : 0;
		s->fair[c] = s->fair_cells ? fair[c] / s->fair_cells : 0;
		s->grey[c] = s->grey_cells ? grey[c] / s->grey_cells : 0;
	}
	for (int i = 63, above = 0; i >= 0; i--) {
		above += s->hist[i];
		if (above * 100 >= (int)s->cells) {
			s->bright = i * 16 + 8;
			break;
		}
	}
}

/*
 * Green's mean towards 15 % of white, unless that would put 1 % of the picture into white. The
 * exposure, longest first: the frame's lines at the shortest frame, then gain to 16x, then frames up
 * to max_frame_lines, then gain to the most; beyond that, digital gain (up to 4x).
 */
bool isp_ae_again(const struct isp *isp, struct redwood_camera_command *c);

bool
isp_ae(struct isp *isp, const struct redwood_camera_frame *f, const struct isp_stats *s, struct redwood_camera_command *c)
{
	/* (a kernel that does not say its sensor's gain and frame limits cannot be asked for an exposure) */
	if (f->gain_unity == 0 || f->gain_max < f->gain_unity || f->frame_lines_min == 0 || f->margin >= f->frame_lines_min ||
	    f->exposure_lines == 0) {
		return false;
	}
	double now = (double)f->exposure_lines * f->gain / f->gain_unity * isp->digital, ratio, want;
	double target = 0.15 * white_of(f), mean = s->mean[1] * isp->digital > 0.5 ? s->mean[1] * isp->digital : 0.5;
	double bright = s->bright * isp->digital;
	uint32_t fast = f->frame_lines_min - f->margin, longest = isp->max_frame_lines ? isp->max_frame_lines : 2 * f->frame_lines_min;
	longest = f->frame_lines_max >= f->frame_lines_min && longest > f->frame_lines_max ? f->frame_lines_max : longest;
	uint32_t lines, gain, frame_lines = 0;
	double digital = 1;

	if (isp->asked && (f->exposure_lines != isp->asked_lines || f->gain != isp->asked_gain)) {
		/*
		 * The last change is not in the frames yet. That is two or three frames normally; but a kernel that started its session anew
		 * (run q10ha: after every "no frame for 3 s") has its sensor back at the exposure it starts with and will never show what was
		 * asked: waiting for ever left AE dead from the first restart on. So: asked again after 3 frames and after 6, and after 9
		 * forgotten -- AE goes on from what the frames do have.
		 */
		isp->asked_waits++;
		if (isp->asked_waits == 3 || isp->asked_waits == 6) {
			return isp_ae_again(isp, c);
		}
		if (isp->asked_waits < 9) {
			return false;
		}
		isp->settled = ISP_AE_SETTLE;   /* (forgotten: what the frames have is what there is) */
	} else if (isp->asked && isp->settled < ISP_AE_SETTLE) {
		/*
		 * Run q10ht: the exposure changed 40 to 110 times in 150 frames and the lights "flickered a lot". The kernel's header has the new
		 * exposure and gain as soon as it has written the sensor's registers (KERNEL_ABI_DUAL.md), and the sensor takes them a frame or
		 * two after that: the first frames that say the new exposure were taken with the old one, and AE, correcting again by them, went
		 * past the mark every time. Their levels are not believed until ISP_AE_SETTLE frames have had it.
		 */
		isp->settled++;
		isp->asked_waits = 0;
		return false;
	}
	isp->asked_waits = 0;
	/* (the measured levels are the sensor's: digital gain is applied to the picture only) */
	ratio = target / mean;
	if (bright > 0) {
		/*
		 * The brightest 1 % kept under white, by up to half of the exposure. When they ARE white (a lamp, a window: how far over nobody
		 * knows) that is the half at once. Until run q10hw it was a tenth less at a time, which the wide camera, shown from the start,
		 * walked all the way down, and the ultra-wide one -- at its target from the levels of the kernel's headers while hidden, and
		 * "where it should be" within an eighth when shown -- never began: the same scene twice as bright at 0.5x.
		 */
		if (bright >= 0.95 * white_of(f)) {
			ratio = target / mean * 0.5;
		} else if (bright * ratio > 0.9 * white_of(f)) {
			ratio = 0.9 * white_of(f) / bright > target / mean * 0.5 ? 0.9 * white_of(f) / bright : target / mean * 0.5;
		}
		isp->aim = (float)(ratio * mean / target < 0.5 ? 0.5 : ratio * mean / target > 1 ? 1 : ratio * mean / target);
	} else if (isp->hidden_aim >= 0.5f && isp->hidden_aim < 1) {
		ratio *= isp->hidden_aim;
	}
	ratio = ratio < 0.25 ? 0.25 : ratio > 4 ? 4 : ratio;
	/* (once it is where it should be it stays until the light is an eighth off; on its way there, until it is a twentieth off) */
	if (ratio > (isp->steady ? 0.88 : 0.95) && ratio < (isp->steady ? 1.13 : 1.05)) {
		isp->steady = true;
		return false;
	}
	isp->steady = false;
	want = now * pow(ratio, mean < 2 ? 0.7 : 0.85);
	{
		/*
		 * The exposure time first, the gain for the rest. A lamp on the mains is bright and dark a hundred times a second (50 Hz): an
		 * exposure that is not a whole number of those 10 ms catches another part of the wave in every frame (and, the sensor's shutter
		 * rolling, in every line: bands) -- the picture's level goes up and down and AE runs after it. So the time is a whole multiple of
		 * flicker_ns whenever what is wanted is at least one (under it, in strong light, it cannot be), and the gain makes up the rest.
		 */
		double period = isp->flicker_ns != 0 && f->line_ns != 0 ? (double)isp->flicker_ns / f->line_ns : 0, most = (double)f->gain_max / f->gain_unity;
		/*
		 * In the dark (run q10hz: "a great deal of noise ... I do not mind the viewfinder's frames a second going down"; the logs had
		 * 30 ms x 64 x 4): the time before the gain. One frame's time; then gain up to ISP_AE_GAIN_FIRST; then longer frames, as long as
		 * the caller lets them be (max_frame_lines: the frames a second go down); only then more gain, to ISP_AE_GAIN_MOST at the most (a
		 * sensor's last stops of gain are mostly noise), and a little digital gain (ISP_AE_DIGITAL_MOST). Darker than that is darker.
		 * Once the frames are long they stay so until a third less light than brought them there would do (no going to and fro).
		 */
		bool long_now = f->frame_lines > f->frame_lines_min + 8;
		double long_gain = long_now ? ISP_AE_GAIN_FIRST * 0.7 : ISP_AE_GAIN_FIRST, limit = fast, time, g;

		most = most > ISP_AE_GAIN_MOST ? ISP_AE_GAIN_MOST : most;
		long_gain = long_gain > most ? most : long_gain;

		/*
		 * Run q10hv: "it flickers only when I point it straight at the lamp: dark bands that move" -- 0.3 ms x 1.0 in that run's log. The
		 * lamp's own brightness (the top 1 % of the levels) took the exposure far under one period of the mains, where every line catches
		 * another part of the lamp's wave. So one period is the least there is (the lamp itself is then white, as it is to any camera),
		 * unless the WHOLE picture would be more than half way to white with it -- daylight, which does not flicker; and once under the
		 * period it stays there until the picture would be a third of the way to white at most (no going to and fro between the two).
		 */
		if (period >= 4 && want < period && f->exposure_lines != 0 && f->gain != 0) {
			double at_period = s->mean[1] * period * f->gain_unity / ((double)f->exposure_lines * f->gain);
			bool under_now = (double)f->exposure_lines + 2 < period;

			if (at_period <= (under_now ? 0.33 : 0.5) * white_of(f)) {
				want = period;
			}
		}
		if (want > (double)fast * long_gain && longest - f->margin > fast) {
			limit = want < (double)(longest - f->margin) ? want : (double)(longest - f->margin);
			limit = limit < fast ? fast : limit;
		}
		time = want < limit ? want : limit;
		if (period >= 4 && want >= period) {
			double n = (double)(uint64_t)(time / period);


			time = n >= 1 ? n * period : time;
		}
		lines = time < 4 ? 4 : (uint32_t)(time + 0.5);
		frame_lines = lines > fast ? lines + f->margin : 0;
		g = want / lines;
		g = g < 1 ? 1 : g;
		if (g > most) {
			digital = g / most;
			digital = digital > ISP_AE_DIGITAL_MOST ? ISP_AE_DIGITAL_MOST : digital;
			g = most;
		}
		gain = (uint32_t)(g * f->gain_unity + 0.5);
	}
	isp->digital = (float)digital;
	if (isp->asked && lines == isp->asked_lines && gain == isp->asked_gain && frame_lines == isp->asked_frame_lines) {
		if (f->exposure_lines == lines && f->gain == gain) {
			return false;           /* (nothing the sensor does not have already) */
		}
		isp->settled = 0;
		return isp_ae_again(isp, c);    /* (what was asked and never came: asked again) */
	}
	isp->asked_lines = lines;
	isp->asked_gain = gain;
	isp->asked_frame_lines = frame_lines;
	isp->asked = true;
	isp->settled = 0;
	memset(c, 0, sizeof(*c));
	c->command = REDWOOD_CAMERA_EXPOSURE;
	c->a = lines;
	c->b = gain;
	c->c = frame_lines;
	return true;
}

bool
isp_ae_again(const struct isp *isp, struct redwood_camera_command *c)
{
	if (!isp->asked) {
		return false;
	}
	memset(c, 0, sizeof(*c));
	c->command = REDWOOD_CAMERA_EXPOSURE;
	c->a = isp->asked_lines;
	c->b = isp->asked_gain;
	c->c = isp->asked_frame_lines;
	return true;
}

double
isp_sharpness(const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	uint32_t cells_w = f->width / 2, cells_h = f->height / 2, x0 = x / 2, y0 = y / 2, x1 = (x + w) / 2, y1 = (y + h) / 2, step = 1;
	double edges = 0, light = 0;
	uint64_t n = 0;

	x1 = x1 > cells_w ? cells_w : x1;
	y1 = y1 > cells_h ? cells_h : y1;
	if (cells_w < 8 || cells_h < 8 || x0 + 4 > x1 || y0 + 4 > y1) {
		return 0;
	}
	while ((uint64_t)((x1 - x0) / step) * ((y1 - y0) / step) > 40000) {
		step++;                         /* (rows and columns left out, never the neighbour a cell is compared with) */
	}
	for (uint32_t cy = y0; cy + 1 < y1; cy += step) {
		const uint8_t *top = raw + (size_t)cy * 2 * f->stride;
		int v[4];

		for (uint32_t cx = x0; cx + 1 < x1; cx += step) {
			int here, right, below;

			cell(top, f->stride, cx, v, (int)f->black, f->format);
			here = v[0] + v[1] + v[2] + v[3];
			cell(top, f->stride, cx + 1, v, (int)f->black, f->format);
			right = v[0] + v[1] + v[2] + v[3];
			cell(top + 2 * f->stride, f->stride, cx, v, (int)f->black, f->format);
			below = v[0] + v[1] + v[2] + v[3];
			edges += (double)(right - here) * (right - here) + (double)(below - here) * (below - here);
			light += here;
			n++;
		}
	}
	if (n == 0 || light < n * 16.0) {
		return 0;                       /* (too dark to tell) */
	}
	light /= (double)n;
	return edges / (double)n / (light * light) * 1e4;
}

void
isp_af_again(struct isp_af *af)
{
	af->again = true;
}

/* the lens told to go to `to' (it gets there ISP_AF_SLEW of the range a frame: isp_af) */
static void
af_go(struct isp_af *af, int64_t to, uint32_t focus_max)
{
	af->target = to < 0 ? 0 : to > (int64_t)focus_max ? focus_max : (uint32_t)to;
	if (af->target == af->pos) {
		af->wait = 1;
	}
}

/* a place of the search and its sharpness, kept with the two before it */
static void
af_note(struct isp_af *af, double sharpness)
{
	af->p[0] = af->p[1]; af->s[0] = af->s[1];
	af->p[1] = af->p[2]; af->s[1] = af->s[2];
	af->p[2] = af->pos; af->s[2] = sharpness;
	af->n++;
	if (sharpness > af->best) {
		af->best = sharpness;
		af->best_at = af->pos;
	}
}

/* the search is over: to the top of the curve through three places (or to the best place seen) */
static void
af_finish(struct isp_af *af, const uint32_t *p, const double *s, uint32_t focus_max)
{
	double to = (double)af->best_at;

	if (p != NULL) {
		double x0 = p[0], x1 = p[1], x2 = p[2];
		double d = (x0 - x1) * (x0 - x2) * (x1 - x2);

		if (d != 0) {
			double a = (x2 * (s[1] - s[0]) + x1 * (s[0] - s[2]) + x0 * (s[2] - s[1])) / d;
			double b = (x2 * x2 * (s[0] - s[1]) + x1 * x1 * (s[2] - s[0]) + x0 * x0 * (s[1] - s[2])) / d;
			double lo = x0 < x2 ? x0 : x2, hi = x0 < x2 ? x2 : x0;

			lo = x1 < lo ? x1 : lo;
			hi = x1 > hi ? x1 : hi;
			if (a < 0) {
				to = -b / (2 * a);
				to = to < lo ? lo : to > hi ? hi : to;
			}
		}
	}
	af->state = 4;
	af_go(af, (int64_t)(to + 0.5), focus_max);
}

bool
isp_af(struct isp_af *af, double sharpness, uint32_t focus_max, uint32_t *move)
{
	uint32_t slew = focus_max / ISP_AF_SLEW > 0 ? focus_max / ISP_AF_SLEW : 1;

	if (focus_max == 0) {
		return false;
	}
	if (!af->pos_known) {
		af->pos = af->pos > focus_max ? focus_max : af->pos;
		af->target = af->pos;
		af->pos_known = true;
	}
	if (af->state == 5) {
		/* over the range: every frame counts (the mean of it and the one before), with the place its lens was at */
		double two = af->acc_n ? (af->acc + sharpness) / 2 : sharpness;

		af->acc = sharpness;
		af->acc_n = 1;
		sharpness = two;
		if (sharpness > af->best) {
			af->best = sharpness;
			/* (the mean of two frames: half a frame's way back from this one's place) */
			af->best_at = (uint32_t)((int64_t)(af->seen <= focus_max ? af->seen : af->pos) - af->dir * (int64_t)(slew * 3 / 4));
			af->best_at = af->best_at > focus_max ? (af->dir > 0 ? 0 : focus_max) : af->best_at;
		}
		if (af->best > 3 * af->s0 && sharpness < 0.5 * af->best) {
			af->state = 6;          /* (a clear peak, and past it) */
			af_go(af, af->best_at, focus_max);
		}
	}
	if (af->pos != af->target) {
		/* on its way: a little each frame, never a jump */
		/* (run q10ib: "very slow". Far to go: a 16th of the range a frame, a 21st over the range, where every frame is looked at; the last bit as before) */
		uint32_t left = af->pos < af->target ? af->target - af->pos : af->pos - af->target;
		uint32_t go = af->state == 5 ? slew * 3 / 2 : left > 3 * slew ? 2 * slew : slew;

		go = left < go ? left : go;

		af->pos = af->pos < af->target ? af->pos + go : af->pos - go;
		af->late = 0;
		*move = af->pos;
		return true;
	}
	if (af->state != 0 && af->seen != af->pos && af->late < 4) {
		/*
		 * Arrived: the first frame whose own lens position is this one is the one to look at (the kernel puts the position into the
		 * frame's header as it writes the actuator) -- not a number of frames guessed, which was two. Four frames without it: this one.
		 */
		af->late++;
		return false;
	}
	if (af->wait > 0) {
		af->wait--;
		return false;
	}
	switch (af->state) {
	case 0: {
		bool steady, far, lit;

		af->sm = af->sm > 0 ? 0.75 * af->sm + 0.25 * sharpness : sharpness;
		steady = af->before > 0 && af->sm > 0.92 * af->before && af->sm < 1.09 * af->before;
		af->calm = steady ? af->calm + 1 : 0;
		af->before = af->sm;
		if (af->fresh > 0) {
			/* (just focused: what the lens was left with is the mean of these frames, not the one it arrived in) */
			af->fresh--;
			af->held = 0.7 * af->held + 0.3 * sharpness;
			af->light_ref = af->light;
			return false;
		}
		if (!af->again && af->scans != 0) {
			if (af->hold) {
				return false;
			}
			far = af->held > 0 ? af->sm < 0.7 * af->held || af->sm > 1.6 * af->held : af->sm > 0;
			lit = af->light > 0 && af->light_ref > 0 && (af->light > 1.35 * af->light_ref || af->light < 0.74 * af->light_ref);
			af->off = far || lit ? af->off + 1 : 0;
			/* (this frame itself under 0.4 of what the lens was left with: counted five times -- a search after three such frames) */
			af->off += sharpness < 0.4 * af->held ? 4 : 0;
			af->calm += sharpness < 0.4 * af->held ? 3 : 0;
			/* (off for a third of a second, and still again -- or off for so long that waiting for stillness is over) */
			if (af->off < ISP_AF_OFF || (af->calm < 3 && af->off < 40)) {
				return false;
			}
		}
		/* (the running mean, unless this frame itself is far under what the lens was left with: then that is what there is) */
		sharpness = af->scans != 0 && !af->again && !(sharpness < 0.4 * af->held) ? af->sm : sharpness;
		af->tapped = af->tapped || (af->again && af->scans != 0 && !af->swept_again);
		af->again = false;
		af->off = 0;
		af->acc_n = 0;
		af->light_ref = af->light;
		if (af->state == 0 && !af->swept_again) {
			af->held_before = af->scans != 0 ? af->held : 0;
			af->swept = false;
		}
		af->swept_again = false;
		af->origin = af->pos;
		af->s0 = sharpness;
		af->step = focus_max / 40 > 0 ? focus_max / 40 : 1;
		if (af->held > 2 * sharpness && sharpness > 0) {
			af->step *= af->held > 4 * sharpness ? 4 : 2;   /* (far less sharp than it was: far to go) */
		}
		af->n = af->run = af->falls = 0;
		af->best = -1;
		af_note(af, sharpness);
		if (af->scans != 0 && af->held_before > 0 && sharpness < 0.3 * af->held_before && !af->swept) {
			/* (less than a third as sharp as it was: no small step will tell -- over the range at once) */
			af->state = 5;
			af->swept = true;
			af->dir = af->origin * 2 < focus_max ? 1 : -1;
			af->turned = false;
			af->best = af->s0;
			af->best_at = af->origin;
			af_go(af, af->dir > 0 ? focus_max : 0, focus_max);
		} else if (af->pos + af->step <= focus_max) {
			af->state = 1;
			af_go(af, (int64_t)af->pos + af->step, focus_max);
		} else {
			af->state = 2;
			af_go(af, (int64_t)af->pos - af->step, focus_max);
		}
		return false;
	}
	case 1:
	case 2: {
		if (af->acc_n == 0 && sharpness > 0.8 * af->s0 && sharpness < 1.25 * af->s0) {
			af->acc = sharpness;
			af->acc_n = 1;
			return false;
		}
		sharpness = af->acc_n ? (af->acc + sharpness) / 2 : sharpness;
		af->acc_n = 0;
		bool better = sharpness > af->s0 * 1.05 && sharpness > 0;

		af_note(af, sharpness);
		if (better) {
			af->dir = af->state == 1 ? 1 : -1;
			af->state = 3;
			af->run = 1;
			af_go(af, (int64_t)af->pos + af->dir * (int64_t)af->step, focus_max);
			return false;
		}
		if (af->state == 1 && af->origin >= af->step) {
			af->state = 2;
			af_go(af, (int64_t)af->origin - af->step, focus_max);
			return false;
		}
		/*
		 * Neither way better. And neither way clearly worse, with the picture far less sharp than the lens was left with (or never in
		 * focus yet, or a tap): the lens is too far off for a small step to tell -- over the range then, up from here and down, in
		 * twelfths of it, at the same gentle pace, and from the best place of that the small steps again.
		 */
		if (af->s0 > 0 && (af->scans == 0 || af->s0 < 0.7 * af->held ||
		    (af->tapped && af->s[2] > 0.985 * af->s0 && (af->n < 3 || af->s[1] > 0.985 * af->s0)))) {
			af->state = 5;
			af->swept = true;
			af->dir = af->origin * 2 < focus_max ? 1 : -1;
			af->turned = false;
			af->best = af->s0;
			af->best_at = af->origin;
			af_go(af, af->dir > 0 ? focus_max : 0, focus_max);
			return false;
		}
		/* the origin is the place, or near it (the curve through the three, when all three were looked at) */
		if (af->n == 3 && af->p[0] == af->origin && af->s0 > 0) {
			uint32_t p[3] = { af->p[2], af->p[0], af->p[1] };
			double s[3] = { af->s[2], af->s[0], af->s[1] };

			af->best_at = af->origin;
			af_finish(af, p, s, focus_max);
		} else {
			af->best_at = af->origin;
			af_finish(af, NULL, NULL, focus_max);
		}
		return false;
	}
	case 3: {
		if (af->acc_n == 0 && sharpness > 0.8 * af->s[2] && sharpness < 1.25 * af->s[2]) {
			af->acc = sharpness;
			af->acc_n = 1;
			return false;
		}
		sharpness = af->acc_n ? (af->acc + sharpness) / 2 : sharpness;
		af->acc_n = 0;
		double last = af->s[2];

		af_note(af, sharpness);
		if (sharpness > last) {
			af->run++;
			af->falls = 0;
			if (af->run >= 3 && af->step < focus_max / 8) {
				af->step *= 2;
				af->run = 1;
			}
		} else {
			af->falls++;
			af->run = 0;
		}
		if (sharpness < 0.92 * af->best || af->falls >= 2 || (af->dir > 0 ? af->pos >= focus_max : af->pos == 0)) {
			/* past the peak (or at the end of the range): the top of the curve through the last three places */
			if (af->n >= 3 && (af->best_at == af->p[1] || af->best_at == af->p[2] || af->best_at == af->p[0])) {
				af_finish(af, af->p, af->s, focus_max);
			} else {
				af_finish(af, NULL, NULL, focus_max);
			}
			return false;
		}
		af_go(af, (int64_t)af->pos + af->dir * (int64_t)af->step, focus_max);
		return false;
	}
	case 5:
		/* at an end of the range with no clear peak behind: the other end, if it has not been looked at; else the best place seen */
		if (!af->turned && af->origin != (af->dir > 0 ? 0 : focus_max)) {
			af->turned = true;
			af->dir = -af->dir;
			af_go(af, af->dir > 0 ? focus_max : 0, focus_max);
			return false;
		}
		af->state = 6;
		af_go(af, af->best_at, focus_max);
		return false;
	case 6:
		if (sharpness >= 0.85 * af->best) {
			af->state = 4;          /* (as sharp here as it was seen in passing: this is the place) */
			af->scans = af->scans == 0 ? 1 : af->scans;
			return false;
		}
		af->state = 0;
		af->again = true;
		af->tapped = false;
		af->scans = af->scans == 0 ? 1 : af->scans;     /* (the range has been looked over: not again for this) */
		af->swept_again = true;
		af->acc_n = 0;
		af->held = sharpness;
		return false;
	default:
		if (!af->swept && af->held_before > 0 && sharpness < 0.6 * af->held_before) {
			/*
			 * The small steps ended somewhere (a step that looked better by noise, and no more after it) and the picture is still far
			 * less sharp than it was: over the range after all, once.
			 */
			af->state = 5;
			af->swept = true;
			af->origin = af->pos;
			af->s0 = sharpness;
			af->dir = af->pos * 2 < focus_max ? 1 : -1;
			af->turned = false;
			af->best = sharpness;
			af->best_at = af->pos;
			af->acc_n = 0;
			af_go(af, af->dir > 0 ? focus_max : 0, focus_max);
			return false;
		}
		af->tapped = false;
		af->fresh = 6;
		af->sm = sharpness;
		af->light_ref = af->light;
		af->held = sharpness;
		af->before = sharpness;
		af->state = 0;
		af->scans++;
		af->off = af->calm = 0;
		af->wait = 6;                   /* (a breath before it is watched again) */
		return false;
	}
}

void
isp_awb(struct isp *isp, const struct isp_stats *s)
{
	/*
	 * Grey world over the cells that are neither clipped nor near black. With the clipped ones in it (run q10hf, the front camera at strong
	 * white lamps: every channel of a lamp is at the sensor's white, so they count as "as much red and blue as green") red's and blue's
	 * gains were pulled down, under 1, and the lamps themselves -- R, G and B all at white -- came out green.
	 */
	/* the cells that could be grey, if there are some (3 % of the picture); else all the fair ones, believed half (see below) */
	bool greys = !isp->wb_free && s->grey_cells * 33 >= s->cells;
	const double *m = greys ? s->grey : s->fair_cells * 20 >= s->cells ? s->fair : NULL;

	if (m == NULL || m[0] < 2 || m[2] < 2 || m[1] < 2) {
		return;
	}
	float r = (float)(m[1] / m[0]), b = (float)(m[1] / m[2]);

	r = r < 0.5f ? 0.5f : r > 4 ? 4 : r;
	b = b < 0.5f ? 0.5f : b > 4 ? 4 : b;
	if (!isp->wb_free && isp->wb_day[0] > 0 && isp->wb_day[1] > 0 && isp->wb_warm[0] > 0 && isp->wb_warm[1] > 0) {
		/*
		 * Not any gains that make the picture's mean grey (run q10hg: "yellow lamps do not exist, they are white and greenish" -- a room
		 * in warm light, or a picture full of one colour, was made grey): the gains of a real light. Those lie, in (log red gain, log blue
		 * gain), near the line from a warm lamp's to daylight's (wb_warm, wb_day). The grey-world gains are split into where along that
		 * line they are -- the light's warmth, kept within the line's ends (a little beyond) and, on the warm side of daylight, followed
		 * only wb_adapt of the way, so that warm light stays a little warm as cameras leave it -- and how far off the line (green or
		 * magenta: no light, the scene's own colour or this sensor not being the one the line was drawn for), of which half is kept.
		 */
		float dx = (float)log((double)isp->wb_day[0]), dy = (float)log((double)isp->wb_day[1]), ex = dx - (float)log((double)isp->wb_warm[0]), ey = dy - (float)log((double)isp->wb_warm[1]);
		float len = (float)pow((double)(ex * ex + ey * ey), 0.5);

		if (len > 0.05f) {
			ex /= len;
			ey /= len;
			float ux = (float)log((double)r) - dx, uy = (float)log((double)b) - dy, along = ux * ex + uy * ey, off = -ux * ey + uy * ex;

			along = along < -len - 0.05f ? -len - 0.05f : along > 0.3f ? 0.3f : along;
			along = along < 0 ? along * (isp->wb_adapt > 0 ? isp->wb_adapt : 1) : along;
			off = off * 0.5f;
			if (!greys) {
				/* nothing in the picture that could be grey (a yellow wall, a face close up): halfway from daylight only */
				along *= 0.5f;
				off *= 0.5f;
			}
			off = off < -0.2f ? -0.2f : off > 0.2f ? 0.2f : off;
			r = (float)exp((double)dx + along * ex - off * ey);
			b = (float)exp((double)dy + along * ey + off * ex);
		}
	}
	isp->wb[0] = isp->wb[0] * 0.8f + r * 0.2f;
	isp->wb[2] = isp->wb[2] * 0.8f + b * 0.2f;
}

/* the part of the sensor's cells the picture shows: its middle, in the picture's proportions */
static void
crop(const struct redwood_camera_frame *f, uint32_t width, uint32_t height, uint32_t *x0, uint32_t *y0, uint32_t *cw, uint32_t *ch)
{
	uint32_t w = f->width / 2, h = f->height / 2;

	if ((uint64_t)width * h > (uint64_t)height * w) {
		*cw = w;
		*ch = (uint32_t)((uint64_t)w * height / width);
	} else {
		*ch = h;
		*cw = (uint32_t)((uint64_t)h * width / height);
	}
	*x0 = (w - *cw) / 2;
	*y0 = (h - *ch) / 2;
}

void
isp_set_ccm(struct isp *isp, const float m[9])
{
	for (int i = 0; i < 9; i++) {
		isp->ccm[i / 3][i % 3] = m[i];
	}
}

/*
 * The colour matrix with the white balance's and AE's gains folded in, in the gamma table's steps
 * (per 10-bit step above black): out[c] = sum over j of m[c][j] * value[j] (each value lens-shaded).
 */
struct pipe {
	float           gain[3];        /* white balance (the least of the three 1) and digital gain */
	float           white;          /* the sensor's white above black: what a channel is clipped to after its gain */
	float           m[3][3];        /* the colour matrix, to the gamma table's steps */
	int32_t         gq[3];          /* gain in 1/256ths (green's halved: it is given the SUM of a cell's two greens) */
	int32_t         q[3][3];        /* m in 1/4096ths */
	int32_t         wq, knee;       /* white, and the level of the sensor's own values from which a pixel loses its colour (85 % of white) */
	int32_t         mid_x, mid_y, vq;       /* lens falloff: the frame's middle (pixels), the gain at the corners less 1 in 1/256ths ... */
	uint64_t        r2k;            /* ... and 2^32 over the squared distance of a corner */
	bool            identity;
};

static void
pipe_init(const struct isp *isp, const struct redwood_camera_frame *f, struct pipe *p)
{
	float least = isp->wb[0] < isp->wb[1] ? isp->wb[0] : isp->wb[1];

	least = isp->wb[2] < least ? isp->wb[2] : least;
	least = least > 0.05f ? least : 0.05f;
	p->identity = true;
	p->white = (float)white_of(f);
	p->wq = white_of(f);
	p->knee = p->wq * 85 / 100;
	p->mid_x = (int32_t)f->width / 2;
	p->mid_y = (int32_t)f->height / 2;
	p->vq = (int32_t)((isp->falloff > 0 ? isp->falloff > 2 ? 2 : isp->falloff : 0) * 256.0f + 0.5f);
	p->r2k = ((uint64_t)1 << 32) / ((uint64_t)p->mid_x * (uint64_t)p->mid_x + (uint64_t)p->mid_y * (uint64_t)p->mid_y + 1);
	for (int c = 0; c < 3; c++) {
		/* no gain under 1: a channel at the sensor's white stays at white, and white stays white */
		p->gain[c] = isp->wb[c] / least * isp->digital;
		p->gain[c] = p->gain[c] > 64 ? 64 : p->gain[c];
		p->gq[c] = (int32_t)(p->gain[c] * (c == 1 ? 128.0f : 256.0f) + 0.5f);
		for (int j = 0; j < 3; j++) {
			p->m[c][j] = isp->ccm[c][j] * (LUT_WHITE / (float)white_of(f));
			p->identity = p->identity && (c == j ? isp->ccm[c][j] == 1 : isp->ccm[c][j] == 0);
			float q = p->m[c][j] * 4096.0f;

			p->q[c][j] = (int32_t)(q >= 0 ? q + 0.5f : q - 0.5f);
		}
	}
}

/*
 * The highlights. Each channel is clipped to the sensor's white after its gain (a colour matrix has negative terms: an unclipped red and
 * blue of a burnt-out lamp took green down -- "the burnt zones are purple", run q10hg). And a pixel one of whose channels the SENSOR had
 * near its white (`most', the largest of its three values as they came, from 85 % of white up) is drawn towards its brightest channel, all
 * the way at white: what the sensor clipped has no known colour and is white, not the complement of the clipped channel (a lamp with red
 * clipped and green not: green-yellow). A bright colour the sensor did NOT clip keeps its colour (a yellow lamp's glow stays yellow).
 */
static inline void
highlights_q(const struct pipe *p, int32_t v[3], int32_t most)
{
	for (int c = 0; c < 3; c++) {
		v[c] = v[c] > p->wq ? p->wq : v[c];
	}
	if (most <= p->knee) {
		return;
	}
	int32_t top = v[0] > v[1] ? v[0] : v[1], t = (most > p->wq ? p->wq : most) - p->knee, span = p->wq - p->knee;

	top = v[2] > top ? v[2] : top;
	for (int c = 0; c < 3; c++) {
		v[c] += (top - v[c]) * t / (span > 0 ? span : 1);
	}
}

/* the lens's falloff at a pixel of the frame: the gain that undoes it, in 1/256ths (1 in the middle, 1 + falloff at the corners, by the square
 * of the distance) */
static inline int32_t
shade_at(const struct pipe *p, int32_t px, int32_t py)
{
	int64_t dx = px - p->mid_x, dy = py - p->mid_y;

	return 256 + (int32_t)((((uint64_t)(dx * dx + dy * dy) * p->r2k) >> 16) * (uint64_t)p->vq >> 16);
}

/* pixel() in whole numbers (no lens shading table): r and b a cell's red and blue, g2 the sum of its two greens, all above black */
static inline void
pixel_q(const struct isp *isp, const struct pipe *p, int32_t r, int32_t g2, int32_t b, int32_t shade, uint8_t *rgb)
{
	int32_t most = r > b ? r : b;
	int32_t v[3] = { (r * ((p->gq[0] * shade) >> 8) + 128) >> 8, (g2 * ((p->gq[1] * shade) >> 8) + 128) >> 8, (b * ((p->gq[2] * shade) >> 8) + 128) >> 8 };

	most = g2 / 2 > most ? g2 / 2 : most;
	highlights_q(p, v, most);
	for (int c = 0; c < 3; c++) {
		int32_t o = (p->q[c][0] * v[0] + p->q[c][1] * v[1] + p->q[c][2] * v[2] + 2048) >> 12;

		rgb[c] = isp->gamma[o < 0 ? 0 : o > 4095 ? 4095 : o];
	}
}

static inline uint8_t
curve(const struct isp *isp, float v)
{
	int i = (int)(v + 0.5f);

	return isp->gamma[i < 0 ? 0 : i > 4095 ? 4095 : i];
}

static inline void
pixel(const struct isp *isp, const struct pipe *p, float r, float g, float b, uint8_t *rgb)
{
	int32_t v[3] = { (int32_t)(r * p->gain[0] + 0.5f), (int32_t)(g * p->gain[1] + 0.5f), (int32_t)(b * p->gain[2] + 0.5f) };
	float most = r > g ? r : g;

	highlights_q(p, v, (int32_t)(b > most ? b : most));
	if (p->identity) {
		rgb[0] = curve(isp, (float)v[0] * p->m[0][0]);
		rgb[1] = curve(isp, (float)v[1] * p->m[1][1]);
		rgb[2] = curve(isp, (float)v[2] * p->m[2][2]);
		return;
	}
	rgb[0] = curve(isp, p->m[0][0] * (float)v[0] + p->m[0][1] * (float)v[1] + p->m[0][2] * (float)v[2]);
	rgb[1] = curve(isp, p->m[1][0] * (float)v[0] + p->m[1][1] * (float)v[1] + p->m[1][2] * (float)v[2]);
	rgb[2] = curve(isp, p->m[2][0] * (float)v[0] + p->m[2][1] * (float)v[1] + p->m[2][2] * (float)v[2]);
}

/* the lens shading's gains at (u, v), 0 to 1 across the sensor */
static inline void
lsc_at(const struct isp *isp, float u, float v, float g[3])
{
	float gx = u * (LSC_W - 1), gy = v * (LSC_H - 1);
	int x = (int)gx, y = (int)gy;

	x = x < 0 ? 0 : x > LSC_W - 2 ? LSC_W - 2 : x;
	y = y < 0 ? 0 : y > LSC_H - 2 ? LSC_H - 2 : y;
	float fx = gx - x, fy = gy - y;

	for (int c = 0; c < 3; c++) {
		const float (*t)[LSC_W] = isp->lsc.gain[c];

		g[c] = (t[y][x] * (1 - fx) + t[y][x + 1] * fx) * (1 - fy) + (t[y + 1][x] * (1 - fx) + t[y + 1][x + 1] * fx) * fy;
	}
}

/* the pixels x and x + 1 of a line, as they are (not above black) */
static inline void
pair(const uint8_t *line, uint32_t x, uint32_t format, int out[2])
{
	int p[4];

	group(line + (size_t)(x >> 2) * 5, format, p);
	out[0] = p[x & 3];
	if ((x & 3) != 3) {
		out[1] = p[(x & 3) + 1];
	} else {
		group(line + (size_t)((x >> 2) + 1) * 5, format, p);
		out[1] = p[0];
	}
}

/*
 * One line of a zoomed (or flipped) picture: the window is the middle 1/zoom of what the picture shows unzoomed, and each of the picture's
 * pixels is the 2x2 pixels of the sensor at its place in the window -- ANY 2x2 pixels, not only a whole cell: every 2x2 of a Bayer sensor has
 * one red, one blue and two greens, so the picture is made at the sensor's own pitch (a 1504 wide picture of a 4096 wide sensor: its own
 * pixels up to 2.7x; beyond that the same pixels are given more than once).
 */
static void
render_line_window(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t width, uint32_t height,
    uint32_t row, uint8_t *rgb)
{
	uint32_t x0, y0, cw, ch;
	int r, b;
	struct pipe p;
	float zoom = isp->zoom < 1 ? 1 : isp->zoom > 32 ? 32 : isp->zoom, gains[3] = { 1, 1, 1 };

	if (f->width < 4 || f->height < 4) {
		return;
	}
	crop(f, width, height, &x0, &y0, &cw, &ch);
	cell_places(f->bayer, &r, &b);
	pipe_init(isp, f, &p);
	/* the window, in pixels of the sensor with 16 bits of fraction */
	uint64_t full_w = (uint64_t)cw << 17, full_h = (uint64_t)ch << 17;
	uint64_t zw = (uint64_t)((double)full_w / zoom), zh = (uint64_t)((double)full_h / zoom);
	uint64_t left = ((uint64_t)x0 << 17) + (full_w - zw) / 2, top = ((uint64_t)y0 << 17) + (full_h - zh) / 2;

	if (isp->shift_x != 0 || isp->shift_y != 0) {
		/* (the window moved, and kept inside what the picture shows unzoomed) */
		int64_t dx = (int64_t)((double)isp->shift_x * (double)full_w), dy = (int64_t)((double)isp->shift_y * (double)full_h);
		int64_t room_x = (int64_t)(full_w - zw) / 2, room_y = (int64_t)(full_h - zh) / 2;

		dx = dx > room_x ? room_x : dx < -room_x ? -room_x : dx;
		dy = dy > room_y ? room_y : dy < -room_y ? -room_y : dy;
		left = (uint64_t)((int64_t)left + dx);
		top = (uint64_t)((int64_t)top + dy);
	}
	uint32_t from = isp->flip & ISP_FLIP_V ? height - 1 - row : row;
	uint32_t py = (uint32_t)((top + (uint64_t)from * zh / height) >> 16);
	int64_t step = (int64_t)(zw / width), at = (int64_t)left;

	py = py > f->height - 2 ? f->height - 2 : py;
	if (isp->flip & ISP_FLIP_H) {
		at += step * (int64_t)(width - 1);
		step = -step;
	}
	const uint8_t *l0 = raw + (size_t)py * f->stride, *l1 = l0 + f->stride;

	for (uint32_t x = 0; x < width; x++, at += step) {
		uint32_t px = (uint32_t)(at >> 16);
		int a[2], c[2], v[4], g;

		px = px > f->width - 2 ? f->width - 2 : px;
		pair(l0, px, f->format, a);
		pair(l1, px, f->format, c);
		/* v[] by the place in a cell (0 top left ... 3 bottom right) of each of the four */
		unsigned place = (py & 1) * 2 + (px & 1);

		v[place] = a[0] - (int)f->black;
		v[place ^ 1] = a[1] - (int)f->black;
		v[place ^ 2] = c[0] - (int)f->black;
		v[place ^ 3] = c[1] - (int)f->black;
		for (int j = 0; j < 4; j++) {
			v[j] = v[j] < 0 ? 0 : v[j];
		}
		g = v[0] + v[1] + v[2] + v[3] - v[r] - v[b];
		if (!isp->lsc.on) {
			pixel_q(isp, &p, v[r], g, v[b], p.vq ? shade_at(&p, (int32_t)px, (int32_t)py) : 256, rgb + 3 * x);
			continue;
		}
		lsc_at(isp, (float)px / (f->width - 1), (float)py / (f->height - 1), gains);
		pixel(isp, &p, v[r] * gains[0], g * 0.5f * gains[1], v[b] * gains[2], rgb + 3 * x);
	}
}

/* one line of the picture as 8-bit R, G, B */
static void
render_line(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t width, uint32_t height,
    uint32_t row, uint8_t *rgb)
{
	uint32_t x0, y0, cw, ch, cells_w = f->width / 2, cells_h = f->height / 2;
	int r, b;
	struct pipe p;

	if (isp->zoom > 1.0005f || isp->flip != 0 || isp->shift_x != 0 || isp->shift_y != 0) {
		render_line_window(isp, f, raw, width, height, row, rgb);
		return;
	}
	crop(f, width, height, &x0, &y0, &cw, &ch);
	cell_places(f->bayer, &r, &b);
	pipe_init(isp, f, &p);
	uint32_t cy = y0 + (uint32_t)((uint64_t)row * ch / height);
	const uint8_t *top = raw + (size_t)cy * 2 * f->stride;
	uint64_t step = ((uint64_t)cw << 16) / width, at = (uint64_t)x0 << 16;
	float v_pos = cells_h > 1 ? (float)cy / (cells_h - 1) : 0, gains[3] = { 1, 1, 1 };

	for (uint32_t x = 0; x < width; x++, at += step) {
		uint32_t cx = (uint32_t)(at >> 16);
		int v[4], g = 0;

		cell(top, f->stride, cx, v, (int)f->black, f->format);
		for (int j = 0; j < 4; j++) {
			g += j != r && j != b ? v[j] : 0;
		}
		if (!isp->lsc.on) {
			pixel_q(isp, &p, v[r], g, v[b], p.vq ? shade_at(&p, (int32_t)(2 * cx + 1), (int32_t)(2 * cy + 1)) : 256, rgb + 3 * x);
			continue;
		}
		lsc_at(isp, cells_w > 1 ? (float)cx / (cells_w - 1) : 0, v_pos, gains);
		pixel(isp, &p, v[r] * gains[0], g * 0.5f * gains[1], v[b] * gains[2], rgb + 3 * x);
	}
}

/*
 * Two lines of 8-bit R, G, B as NV12 (BT.601's matrix, which CoreVideo's 420f and 420v and
 * tools/camraw.py take): each pixel's Y (y1 NULL: the second line has none, the picture's last of an
 * odd height), each 2x2's Cb and Cr from the sum of its four. The coefficients in 1/65536ths, video
 * range's scaled to its 219 steps of luma (16 to 235) and 224 of chroma (16 to 240); every value
 * rounded to the nearest step, so that the primaries come back within one step of 0 and 255 (red's and
 * blue's cannot be exact: no 8-bit Y, Cb, Cr is both).
 */
static void
nv12_pair(const uint8_t *rgb0, const uint8_t *rgb1, uint32_t width, uint8_t *y0, uint8_t *y1, uint8_t *uv, bool video_range)
{
	static const int32_t full[9] = { 19595, 38470, 7471, -11058, -21710, 32768, 32768, -27439, -5329 };
	static const int32_t video[9] = { 16829, 33039, 6416, -9714, -19070, 28784, 28784, -24103, -4681 };
	const int32_t *k = video_range ? video : full;
	int32_t oy = (video_range ? 16 << 16 : 0) + (1 << 15);

	for (int l = 0; l < 2; l++) {
		const uint8_t *p = l ? rgb1 : rgb0;
		uint8_t *out = l ? y1 : y0;

		for (uint32_t x = 0; out != NULL && x < width; x++, p += 3) {
			out[x] = (uint8_t)((k[0] * p[0] + k[1] * p[1] + k[2] * p[2] + oy) >> 16);
		}
	}
	for (uint32_t x = 0; x < width; x += 2) {
		uint32_t x1 = x + 1 < width ? x + 1 : x;
		int32_t R = rgb0[3 * x] + rgb0[3 * x1] + rgb1[3 * x] + rgb1[3 * x1];
		int32_t G = rgb0[3 * x + 1] + rgb0[3 * x1 + 1] + rgb1[3 * x + 1] + rgb1[3 * x1 + 1];
		int32_t B = rgb0[3 * x + 2] + rgb0[3 * x1 + 2] + rgb1[3 * x + 2] + rgb1[3 * x1 + 2];
		/*
		 * (the sums are four pixels': 18 bits off, with 128 added and just under half a step to round;
		 * a tie goes down, so that chroma's -127.5, a full colour's, comes out 0 as its +127.5 does 255)
		 */
		int32_t cb = (k[3] * R + k[4] * G + k[5] * B + (128 << 18) + (1 << 17) - 1) >> 18;
		int32_t cr = (k[6] * R + k[7] * G + k[8] * B + (128 << 18) + (1 << 17) - 1) >> 18;

		uv[x] = (uint8_t)(cb < 0 ? 0 : cb > 255 ? 255 : cb);
		uv[x + 1] = (uint8_t)(cr < 0 ? 0 : cr > 255 ? 255 : cr);
	}
}

#ifdef ISP_LINES
/*
 * The same picture in passes over a whole line (run user6: a film's pictures took all eight cores and the encoder got 21 a second): what
 * render_line / render_line_window and nv12_pair do pixel by pixel is done here as (1) the sensor's values of every pixel of the line taken
 * out into arrays, (2) gains, clipping and the colour matrix over those arrays, (3) the tone table, (4) the Y, Cb, Cr of two lines -- (2)
 * and (4) are plain loops over arrays of numbers, which the compiler makes four or eight at a time of (NEON on the phone, SSE where the
 * tests run: the same source, the same numbers). Byte for byte the pixel-by-pixel picture (t_isp compares them); a pixel whose sensor value
 * is near white goes through pixel_q itself. Not with a lens shading table (lsc.on): then as before. isp_reference: as before, always.
 * Only with -DISP_LINES (the tests'): on the PC it is no faster than pixel by pixel (48 against 49 ms for 1280x720 on one thread -- the time
 * is in taking the values out of the frame, pass (1), not in the arithmetic), and the package has no room for what is not known to help.
 */
int isp_reference;

struct line_values { int32_t *r, *g, *b, *shade, *most; uint16_t *index[3]; uint8_t *rgb[3]; };

static void
line_take(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t width, uint32_t height, uint32_t row,
    const struct pipe *p, const struct line_values *o)
{
	int r, b;
	uint32_t x0, y0, cw, ch;

	crop(f, width, height, &x0, &y0, &cw, &ch);
	cell_places(f->bayer, &r, &b);
	if (isp->zoom > 1.0005f || isp->flip != 0 || isp->shift_x != 0 || isp->shift_y != 0) {
		/* (render_line_window's places) */
		float zoom = isp->zoom < 1 ? 1 : isp->zoom > 32 ? 32 : isp->zoom;
		uint64_t full_w = (uint64_t)cw << 17, full_h = (uint64_t)ch << 17;
		uint64_t zw = (uint64_t)((double)full_w / zoom), zh = (uint64_t)((double)full_h / zoom);
		uint64_t left = ((uint64_t)x0 << 17) + (full_w - zw) / 2, top = ((uint64_t)y0 << 17) + (full_h - zh) / 2;

		if (isp->shift_x != 0 || isp->shift_y != 0) {
			int64_t dx = (int64_t)((double)isp->shift_x * (double)full_w), dy = (int64_t)((double)isp->shift_y * (double)full_h);
			int64_t room_x = (int64_t)(full_w - zw) / 2, room_y = (int64_t)(full_h - zh) / 2;

			dx = dx > room_x ? room_x : dx < -room_x ? -room_x : dx;
			dy = dy > room_y ? room_y : dy < -room_y ? -room_y : dy;
			left = (uint64_t)((int64_t)left + dx);
			top = (uint64_t)((int64_t)top + dy);
		}
		uint32_t from = isp->flip & ISP_FLIP_V ? height - 1 - row : row;
		uint32_t py = (uint32_t)((top + (uint64_t)from * zh / height) >> 16);
		int64_t step = (int64_t)(zw / width), at = (int64_t)left;

		py = py > f->height - 2 ? f->height - 2 : py;
		if (isp->flip & ISP_FLIP_H) {
			at += step * (int64_t)(width - 1);
			step = -step;
		}
		const uint8_t *l0 = raw + (size_t)py * f->stride, *l1 = l0 + f->stride;

		for (uint32_t x = 0; x < width; x++, at += step) {
			uint32_t px = (uint32_t)(at >> 16);
			int a[2], c[2], v[4];

			px = px > f->width - 2 ? f->width - 2 : px;
			pair(l0, px, f->format, a);
			pair(l1, px, f->format, c);
			unsigned place = (py & 1) * 2 + (px & 1);

			v[place] = a[0] - (int)f->black;
			v[place ^ 1] = a[1] - (int)f->black;
			v[place ^ 2] = c[0] - (int)f->black;
			v[place ^ 3] = c[1] - (int)f->black;
			for (int j = 0; j < 4; j++) {
				v[j] = v[j] < 0 ? 0 : v[j];
			}
			o->r[x] = v[r];
			o->b[x] = v[b];
			o->g[x] = v[0] + v[1] + v[2] + v[3] - v[r] - v[b];
			o->shade[x] = p->vq ? shade_at(p, (int32_t)px, (int32_t)py) : 256;
		}
	} else {
		/* (render_line's) */
		uint32_t cy = y0 + (uint32_t)((uint64_t)row * ch / height);
		const uint8_t *top = raw + (size_t)cy * 2 * f->stride;
		uint64_t step = ((uint64_t)cw << 16) / width, at = (uint64_t)x0 << 16;

		for (uint32_t x = 0; x < width; x++, at += step) {
			uint32_t cx = (uint32_t)(at >> 16);
			int v[4];

			cell(top, f->stride, cx, v, (int)f->black, f->format);
			o->r[x] = v[r];
			o->b[x] = v[b];
			o->g[x] = v[0] + v[1] + v[2] + v[3] - v[r] - v[b];
			o->shade[x] = p->vq ? shade_at(p, (int32_t)(2 * cx + 1), (int32_t)(2 * cy + 1)) : 256;
		}
	}
}

static void
line_colours(const struct isp *isp, const struct pipe *p, uint32_t width, const struct line_values *o)
{
	const int32_t g0 = p->gq[0], g1 = p->gq[1], g2 = p->gq[2], wq = p->wq, knee = p->knee;
	const int32_t *restrict R = o->r, *restrict G = o->g, *restrict B = o->b, *restrict S = o->shade;
	int32_t *restrict M = o->most;

	for (int c = 0; c < 3; c++) {
		const int32_t q0 = p->q[c][0], q1 = p->q[c][1], q2 = p->q[c][2];
		uint16_t *restrict index = o->index[c];

		for (uint32_t x = 0; x < width; x++) {
			int32_t v0 = (R[x] * ((g0 * S[x]) >> 8) + 128) >> 8, v1 = (G[x] * ((g1 * S[x]) >> 8) + 128) >> 8;
			int32_t v2 = (B[x] * ((g2 * S[x]) >> 8) + 128) >> 8, t;

			v0 = v0 > wq ? wq : v0;
			v1 = v1 > wq ? wq : v1;
			v2 = v2 > wq ? wq : v2;
			t = (q0 * v0 + q1 * v1 + q2 * v2 + 2048) >> 12;
			t = t < 0 ? 0 : t > 4095 ? 4095 : t;
			index[x] = (uint16_t)t;
		}
	}
	for (uint32_t x = 0; x < width; x++) {
		int32_t most = R[x] > B[x] ? R[x] : B[x];

		M[x] = G[x] / 2 > most ? G[x] / 2 : most;
	}
	for (uint32_t x = 0; x < width; x++) {
		if (M[x] > knee) {
			uint8_t rgb[3];         /* (near the sensor's white: the highlights' way, pixel_q's own) */

			pixel_q(isp, p, R[x], G[x], B[x], S[x], rgb);
			o->rgb[0][x] = rgb[0];
			o->rgb[1][x] = rgb[1];
			o->rgb[2][x] = rgb[2];
		} else {
			o->rgb[0][x] = isp->gamma[o->index[0][x]];
			o->rgb[1][x] = isp->gamma[o->index[1][x]];
			o->rgb[2][x] = isp->gamma[o->index[2][x]];
		}
	}
}

/* nv12_pair, the colours in rows of their own */
static void
nv12_rows(const struct line_values *a, const struct line_values *b, uint32_t width, uint8_t *y0, uint8_t *y1, uint8_t *uv, bool video_range)
{
	static const int32_t full[9] = { 19595, 38470, 7471, -11058, -21710, 32768, 32768, -27439, -5329 };
	static const int32_t video[9] = { 16829, 33039, 6416, -9714, -19070, 28784, 28784, -24103, -4681 };
	const int32_t *k = video_range ? video : full;
	const int32_t k0 = k[0], k1 = k[1], k2 = k[2], oy = (video_range ? 16 << 16 : 0) + (1 << 15);

	for (int l = 0; l < 2; l++) {
		const uint8_t *restrict r = (l ? b : a)->rgb[0], *restrict g = (l ? b : a)->rgb[1], *restrict bl = (l ? b : a)->rgb[2];
		uint8_t *restrict out = l ? y1 : y0;

		for (uint32_t x = 0; out != NULL && x < width; x++) {
			out[x] = (uint8_t)((k0 * r[x] + k1 * g[x] + k2 * bl[x] + oy) >> 16);
		}
	}
	for (uint32_t x = 0; x < width; x += 2) {
		uint32_t x1 = x + 1 < width ? x + 1 : x;
		int32_t R = a->rgb[0][x] + a->rgb[0][x1] + b->rgb[0][x] + b->rgb[0][x1];
		int32_t G = a->rgb[1][x] + a->rgb[1][x1] + b->rgb[1][x] + b->rgb[1][x1];
		int32_t B = a->rgb[2][x] + a->rgb[2][x1] + b->rgb[2][x] + b->rgb[2][x1];
		int32_t cb = (k[3] * R + k[4] * G + k[5] * B + (128 << 18) + (1 << 17) - 1) >> 18;
		int32_t cr = (k[6] * R + k[7] * G + k[8] * B + (128 << 18) + (1 << 17) - 1) >> 18;

		uv[x] = (uint8_t)(cb < 0 ? 0 : cb > 255 ? 255 : cb);
		uv[x + 1] = (uint8_t)(cr < 0 ? 0 : cr > 255 ? 255 : cr);
	}
}

static bool
render_rows(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *y, size_t y_stride,
    uint8_t *uv, size_t uv_stride, uint32_t width, uint32_t height, uint32_t first_row, uint32_t end_row, bool video_range)
{
	/* for each of the two lines: five rows of int32, three of uint16, three of bytes */
	size_t each = ((size_t)width * (5 * sizeof(int32_t) + 3 * sizeof(uint16_t) + 3) + 64 + 15) & ~(size_t)15;
	uint8_t *room = malloc(2 * each);
	struct line_values v[2];
	struct pipe p;

	if (room == NULL || f->width < 4 || f->height < 4) {
		free(room);
		return false;
	}
	for (int l = 0; l < 2; l++) {
		int32_t *n = (int32_t *)(void *)(room + l * each);

		v[l].r = n;
		v[l].g = n + width;
		v[l].b = n + 2 * width;
		v[l].shade = n + 3 * width;
		v[l].most = n + 4 * width;
		for (int c = 0; c < 3; c++) {
			v[l].index[c] = (uint16_t *)(void *)(n + 5 * width) + c * width;
			v[l].rgb[c] = (uint8_t *)(n + 5 * width) + 3 * sizeof(uint16_t) * width + c * width;
		}
	}
	pipe_init(isp, f, &p);
	for (uint32_t row = first_row & ~1u; row < end_row && row < height; row += 2) {
		bool second = row + 1 < height;

		for (int l = 0; l < 2; l++) {
			line_take(isp, f, raw, width, height, l && second ? row + 1 : row, &p, &v[l]);
			line_colours(isp, &p, width, &v[l]);
		}
		nv12_rows(&v[0], &v[1], width, y + (size_t)row * y_stride, second ? y + (size_t)(row + 1) * y_stride : NULL,
		    uv + (size_t)(row / 2) * uv_stride, video_range);
	}
	free(room);
	return true;
}

#endif

void
isp_render_nv12(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *y, size_t y_stride,
    uint8_t *uv, size_t uv_stride, uint32_t width, uint32_t height, uint32_t first_row, uint32_t end_row, bool video_range)
{
	uint8_t line[2][3 * 8192];

	if (width > 8192) {
		return;
	}
#ifdef ISP_LINES
	if (!isp->lsc.on && !isp_reference && width >= 2 &&
	    render_rows(isp, f, raw, y, y_stride, uv, uv_stride, width, height, first_row, end_row, video_range)) {
		return;
	}
#endif
	for (uint32_t row = first_row & ~1u; row < end_row && row < height; row += 2) {
		bool second = row + 1 < height;

		render_line(isp, f, raw, width, height, row, line[0]);
		render_line(isp, f, raw, width, height, second ? row + 1 : row, line[1]);
		nv12_pair(line[0], line[1], width, y + (size_t)row * y_stride, second ? y + (size_t)(row + 1) * y_stride : NULL,
		    uv + (size_t)(row / 2) * uv_stride, video_range);
	}
}

void
isp_render_bgra(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *out, size_t stride,
    uint32_t width, uint32_t height, uint32_t first_row, uint32_t end_row)
{
	uint8_t line[3 * 8192];

	if (width > 8192) {
		return;
	}
	for (uint32_t row = first_row; row < end_row && row < height; row++) {
		uint8_t *o = out + (size_t)row * stride;

		render_line(isp, f, raw, width, height, row, line);
		for (uint32_t x = 0; x < width; x++) {
			o[4 * x] = line[3 * x + 2];
			o[4 * x + 1] = line[3 * x + 1];
			o[4 * x + 2] = line[3 * x];
			o[4 * x + 3] = 255;
		}
	}
}

/* ---- every pixel: bilinear demosaic -------------------------------------------------------------------- */

/* the colour of each site of a 2x2 cell (0 R, 1 G, 2 B), position y * 2 + x */
static void
cfa(uint32_t bayer, int colour[4])
{
	int r, b;

	cell_places(bayer, &r, &b);
	for (int i = 0; i < 4; i++) {
		colour[i] = i == r ? 0 : i == b ? 2 : 1;
	}
}

/* line y of the frame as values above black (y clamped into the frame) */
static void
unpack_line(const struct redwood_camera_frame *f, const uint8_t *raw, int y, int *out)
{
	y = y < 0 ? 0 : y >= (int)f->height ? (int)f->height - 1 : y;
	const uint8_t *line = raw + (size_t)y * f->stride;

	for (uint32_t x = 0; x + 3 < f->width + 3 && x < f->width; x += 4) {
		const uint8_t *g = line + (x >> 2) * 5;

		int p[4];

		group(g, f->format, p);
		for (int i = 0; i < 4 && x + i < f->width; i++) {
			int v = p[i] - (int)f->black;

			out[x + i] = v < 0 ? 0 : v;
		}
	}
}

/* one line (y) at full size as 8-bit R, G, B, from the lines y - 1, y, y + 1 (l[0..2]) */
static void
full_line(const struct isp *isp, const struct redwood_camera_frame *f, const int *const l[3], uint32_t y, const int colour[4],
    const struct pipe *p, uint8_t *rgb)
{
	int w = (int)f->width;
	float v_pos = f->height > 1 ? (float)y / (f->height - 1) : 0, gains[3] = { 1, 1, 1 };

	for (int x = 0; x < w; x++) {
		float sum[3] = { 0, 0, 0 };
		int n[3] = { 0, 0, 0 };

		/* each colour: its own value where the site has it, else the mean of its sites in the 3x3 around */
		for (int dy = -1; dy <= 1; dy++) {
			const int *line = l[dy + 1];
			int yy = (int)y + dy;

			yy = yy < 0 ? 0 : yy >= (int)f->height ? (int)f->height - 1 : yy;
			for (int dx = -1; dx <= 1; dx++) {
				int xx = x + dx < 0 ? 0 : x + dx >= w ? w - 1 : x + dx;
				int c = colour[(yy & 1) * 2 + (xx & 1)];

				if (dx == 0 && dy == 0) {
					sum[c] = (float)line[xx] * 64;          /* (its own value wins over the neighbours') */
					n[c] = 64;
				} else if (n[c] < 64) {
					sum[c] += (float)line[xx];
					n[c]++;
				}
			}
		}
		if (isp->lsc.on) {
			lsc_at(isp, w > 1 ? (float)x / (w - 1) : 0, v_pos, gains);
		}
		pixel(isp, p, sum[0] / (n[0] ? n[0] : 1) * gains[0], sum[1] / (n[1] ? n[1] : 1) * gains[1],
		    sum[2] / (n[2] ? n[2] : 1) * gains[2], rgb + 3 * x);
	}
}

void
isp_render_full_nv12(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *y, size_t y_stride,
    uint8_t *uv, size_t uv_stride, uint32_t first_row, uint32_t end_row, bool video_range)
{
	uint32_t width = f->width;
	int colour[4];
	struct pipe p;
	int *lines[4];
	uint8_t *rgb[2];

	/* (buffers of this call's own: no thread-local storage in a bundle that is loaded into somebody else's process) */
	for (int i = 0; i < 4; i++) {
		lines[i] = malloc(sizeof(int) * width);
	}
	for (int i = 0; i < 2; i++) {
		rgb[i] = malloc(3 * (size_t)width);
	}
	cfa(f->bayer, colour);
	pipe_init(isp, f, &p);
	for (uint32_t row = first_row & ~1u; row < end_row && row < f->height; row += 2) {
		for (int i = 0; i < 4; i++) {
			unpack_line(f, raw, (int)row - 1 + i, lines[i]);
		}
		const int *const above[3] = { lines[0], lines[1], lines[2] }, *const below[3] = { lines[1], lines[2], lines[3] };

		bool second = row + 1 < f->height;

		full_line(isp, f, above, row, colour, &p, rgb[0]);
		full_line(isp, f, below, second ? row + 1 : row, colour, &p, rgb[1]);
		nv12_pair(rgb[0], rgb[1], width, y + (size_t)row * y_stride, second ? y + (size_t)(row + 1) * y_stride : NULL,
		    uv + (size_t)(row / 2) * uv_stride, video_range);
	}
	for (int i = 0; i < 4; i++) {
		free(lines[i]);
	}
	for (int i = 0; i < 2; i++) {
		free(rgb[i]);
	}
}

/* ---- lens shading from a flat field --------------------------------------------------------------------- */

bool
isp_lsc_from_flat(struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, float *min_ratio)
{
	uint32_t cells_w = f->width / 2, cells_h = f->height / 2, half_w = cells_w / (LSC_W - 1) / 2, half_h = cells_h / (LSC_H - 1) / 2;
	float level[3][LSC_H][LSC_W], top[3] = { 0, 0, 0 };
	uint32_t clipped = 0, seen = 0;
	int r, b;

	cell_places(f->bayer, &r, &b);
	for (int gy = 0; gy < LSC_H; gy++) {
		for (int gx = 0; gx < LSC_W; gx++) {
			int cx0 = (int)((uint64_t)gx * (cells_w - 1) / (LSC_W - 1)) - (int)half_w;
			int cy0 = (int)((uint64_t)gy * (cells_h - 1) / (LSC_H - 1)) - (int)half_h;
			double sum[3] = { 0, 0, 0 };
			uint32_t n = 0;

			for (int cy = cy0; cy <= cy0 + 2 * (int)half_h; cy += 2) {
				for (int cx = cx0; cx <= cx0 + 2 * (int)half_w; cx += 2) {
					int v[4];

					if (cx < 0 || cy < 0 || cx >= (int)cells_w || cy >= (int)cells_h) {
						continue;
					}
					cell(raw + (size_t)cy * 2 * f->stride, f->stride, (uint32_t)cx, v, (int)f->black, f->format);
					sum[0] += v[r];
					sum[2] += v[b];
					for (int j = 0; j < 4; j++) {
						sum[1] += j != r && j != b ? v[j] / 2.0 : 0;
						clipped += v[j] > white_of(f) - 30;
					}
					seen += 4;
					n++;
				}
			}
			for (int c = 0; c < 3; c++) {
				level[c][gy][gx] = n ? (float)(sum[c] / n) : 0;
				top[c] = level[c][gy][gx] > top[c] ? level[c][gy][gx] : top[c];
			}
		}
	}
	/* bright enough to tell shading from noise, and not clipped anywhere that counts */
	if (top[1] < 100 || clipped * 1000 > seen) {
		return false;
	}
	float darkest = 1;

	for (int c = 0; c < 3; c++) {
		for (int gy = 0; gy < LSC_H; gy++) {
			for (int gx = 0; gx < LSC_W; gx++) {
				float l = level[c][gy][gx] > 1 ? level[c][gy][gx] : 1;

				isp->lsc.gain[c][gy][gx] = top[c] / l;
				if (c == 1 && l / top[c] < darkest) {
					darkest = l / top[c];
				}
			}
		}
	}
	isp->lsc.on = true;
	if (min_ratio != NULL) {
		*min_ratio = darkest;
	}
	return true;
}

/* one plane of isp_shrink_420: n bytes a point (1 luma, 2 chroma), rows r0 to r1 of the smaller one */
static void
shrink_plane(const uint8_t *from, size_t fs, uint32_t fw, uint32_t fh, uint8_t *to, size_t ts, uint32_t w, uint32_t h, uint32_t r0, uint32_t r1,
    unsigned n, const uint8_t *table)
{
	/* 16.16: a point of the smaller picture is step points of the larger one wide; taps samples a side, each between two points */
	uint32_t sx = (uint32_t)(((uint64_t)fw << 16) / w), sy = (uint32_t)(((uint64_t)fh << 16) / h);
	unsigned tx = sx > 0x20000 ? 2 : 1, ty = sy > 0x20000 ? 2 : 1;

	if (tx == 1 && ty == 1) {
		/*
		 * Larger, or up to twice smaller: one sample a point. Each column's place and weight are found once; a line of the picture given is
		 * weighed along itself once (two are kept: the next line of the new picture mostly needs one of them again) and then the two lines
		 * are weighed against each other, which is a loop the compiler makes many at a time of.
		 */
		uint32_t *place = malloc((size_t)w * sizeof(*place));
		uint16_t *line[2] = { malloc((size_t)w * n * sizeof(uint16_t)), malloc((size_t)w * n * sizeof(uint16_t)) };
		int64_t have[2] = { -1, -1 };

		for (uint32_t x = 0; place != NULL && x < w; x++) {
			int64_t at = (int64_t)x * sx + sx / 2 - 0x8000;
			uint32_t x0;

			at = at < 0 ? 0 : at;
			x0 = (uint32_t)(at >> 16);
			x0 = x0 > fw - 1 ? fw - 1 : x0;
			place[x] = x0 * n << 8 | (x0 == fw - 1 ? 0 : (uint32_t)(at & 0xffff) >> 8);
		}
		for (uint32_t y = r0; place != NULL && line[0] != NULL && line[1] != NULL && y < r1 && y < h; y++) {
			int64_t at = (int64_t)y * sy + sy / 2 - 0x8000;
			uint32_t y0, wy, want[2];
			uint8_t *out = to + (size_t)y * ts;

			at = at < 0 ? 0 : at;
			y0 = (uint32_t)(at >> 16);
			y0 = y0 > fh - 1 ? fh - 1 : y0;
			wy = y0 == fh - 1 ? 0 : (uint32_t)(at & 0xffff) >> 8;
			want[0] = y0;
			want[1] = y0 == fh - 1 ? y0 : y0 + 1;
			if (have[1] == want[0] || have[0] == want[1]) {
				uint16_t *t = line[0];

				line[0] = line[1];
				line[1] = t;
				at = have[0];
				have[0] = have[1];
				have[1] = at;
			}
			for (int k = 0; k < 2; k++) {
				const uint8_t *a = from + (size_t)want[k] * fs;
				uint16_t *l = line[k];

				if (have[k] == want[k] || (k == 1 && wy == 0)) {
					continue;
				}
				have[k] = want[k];
				for (uint32_t x = 0, i = 0; x < w; x++) {
					uint32_t o = place[x] >> 8, wx = place[x] & 255, step = wx ? n : 0;

					for (unsigned c = 0; c < n; c++, i++, o++) {
						l[i] = (uint16_t)(a[o] * (256 - wx) + a[o + step] * wx);
					}
				}
			}
			const uint16_t *u = line[0], *v = wy ? line[1] : line[0];

			if (table == NULL) {
				for (uint32_t i = 0; i < w * n; i++) {
					out[i] = (uint8_t)((((u[i] * (256 - wy) + v[i] * wy) >> 8) + 128) >> 8);
				}
			} else {
				for (uint32_t i = 0; i < w * n; i++) {
					out[i] = table[(((u[i] * (256 - wy) + v[i] * wy) >> 8) + 128) >> 8];
				}
			}
		}
		free(place);
		free(line[0]);
		free(line[1]);
		if (place != NULL) {
			return;         /* (no memory for a line: the picture keeps what it had) */
		}
	}
	for (uint32_t y = r0; y < r1 && y < h; y++) {
		uint8_t *out = to + (size_t)y * ts;
		const uint8_t *row[2][2];
		uint32_t wy[2];

		for (unsigned t = 0; t < ty; t++) {
			/* the sample's place: the point's middle (one tap), or a quarter of the point to each side of it (two) */
			int64_t at = (int64_t)y * sy + (ty == 2 ? (int64_t)sy * (1 + 2 * t) / 4 : sy / 2) - 0x8000;
			uint32_t y0;

			at = at < 0 ? 0 : at;
			y0 = (uint32_t)(at >> 16);
			y0 = y0 > fh - 1 ? fh - 1 : y0;
			wy[t] = y0 == fh - 1 ? 0 : (uint32_t)(at & 0xffff) >> 8;
			row[t][0] = from + (size_t)y0 * fs;
			row[t][1] = from + (size_t)(y0 == fh - 1 ? y0 : y0 + 1) * fs;
		}
		for (uint32_t x = 0; x < w; x++) {
			uint32_t sum[2] = { 0, 0 };

			for (unsigned u = 0; u < tx; u++) {
				int64_t at = (int64_t)x * sx + (tx == 2 ? (int64_t)sx * (1 + 2 * u) / 4 : sx / 2) - 0x8000;
				uint32_t x0, x1, wx;

				at = at < 0 ? 0 : at;
				x0 = (uint32_t)(at >> 16);
				x0 = x0 > fw - 1 ? fw - 1 : x0;
				x1 = x0 == fw - 1 ? x0 : x0 + 1;
				wx = x0 == fw - 1 ? 0 : (uint32_t)(at & 0xffff) >> 8;
				for (unsigned t = 0; t < ty; t++) {
					for (unsigned c = 0; c < n; c++) {
						uint32_t a = row[t][0][x0 * n + c] * (256 - wx) + row[t][0][x1 * n + c] * wx;
						uint32_t b = row[t][1][x0 * n + c] * (256 - wx) + row[t][1][x1 * n + c] * wx;

						sum[c] += (a * (256 - wy[t]) + b * wy[t]) >> 8;
					}
				}
			}
			for (unsigned c = 0; c < n; c++) {
				uint32_t v = (sum[c] / (tx * ty) + 128) >> 8;

				v = v > 255 ? 255 : v;
				out[x * n + c] = table != NULL ? table[v] : (uint8_t)v;
			}
		}
	}
}

void
isp_shrink_420(const uint8_t *const from[2], const size_t from_stride[2], uint32_t from_width, uint32_t from_height, uint8_t *const to[2],
    const size_t to_stride[2], uint32_t width, uint32_t height, uint32_t row0, uint32_t row1, int range)
{
	uint8_t luma[256], chroma[256];

	if (width < 2 || height < 2 || from_width < 2 || from_height < 2) {
		return;
	}
	for (int i = 0; i < 256 && range != 0; i++) {
		int l = range == 1 ? ((i - 16) * 255 + 109) / 219 : 16 + (i * 219 + 127) / 255;
		int c = range == 1 ? 128 + ((i - 128) * 255) / 224 : 128 + ((i - 128) * 224) / 255;

		luma[i] = (uint8_t)(l < 0 ? 0 : l > 255 ? 255 : l);
		chroma[i] = (uint8_t)(c < 0 ? 0 : c > 255 ? 255 : c);
	}
	shrink_plane(from[0], from_stride[0], from_width, from_height, to[0], to_stride[0], width, height, row0, row1, 1, range ? luma : NULL);
	shrink_plane(from[1], from_stride[1], from_width / 2, from_height / 2, to[1], to_stride[1], width / 2, height / 2, row0 / 2, (row1 + 1) / 2, 2,
	    range ? chroma : NULL);
}
