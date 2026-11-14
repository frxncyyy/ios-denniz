/*
 * (ae2) AE against a simulated sensor + pipeline, for the bright-light limit cycle of run lm6 (IMG_0065.MOV: a desk lamp, luma 48..133 in steps
 * every 3-4 frames, period 20-25 frames). Links against either camera_isp.c (the old AE of camera-userland-20261112-gpuauto2 or the new one):
 * only isp_init / isp_ae / isp->digital are used.
 *
 * The model:
 *   - a scene of 4096 cells: background, a bright window / wall, a lamp (60x the background); its light can step;
 *   - mains (50 Hz, 100 Hz in light) on the lamp-lit parts, m = 0.3, integrated per row over the exposure (rolling shutter, readout 30 ms);
 *   - the sensor: a command reaches the header HDR frames after it was given (the kernel writes it one register a turn) and the picture
 *     EFF frames after that (group hold: exposure and gain together); lines integer, gain 1/256;
 *   - the statistics AE gets are STATS frames old (the GPU's, a frame or two before); with PAIR=old they come with the CURRENT frame's header
 *     (rwcam_core.c of gpuauto2), with PAIR=new with their own frame's header and sequence (the ae2 core);
 *   - what is seen: the frame's level x the digital gain then, through sRGB (8 bits).
 * Prints per case: commands, up/down reversals, luma range and the largest frame-to-frame luma jump over the last 150 frames of each phase.
 * Exit code 1 when a case breaks the limits given with -check (the ae2 build's run_tests.sh uses it).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera_isp.h"

#define CELLS 4096
static double rad[CELLS];
static int lampish[CELLS];

static double
srgb8(double v)
{
	v = v < 0 ? 0 : v > 1 ? 1 : v;
	v = v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1 / 2.4) - 0.055;
	return v * 255;
}

/* the mains' share over an exposure of T seconds that starts at t0 */
static double
mains(double t0, double T, double m)
{
	double w = 2 * M_PI * 100;

	if (m == 0 || T <= 0) {
		return 1;
	}
	return 1 + m * (sin(w * (t0 + T)) - sin(w * t0)) / (w * T);
}

static void
scene(int kind)
{
	srand(7);
	for (int i = 0; i < CELLS; i++) {
		double u = (double)rand() / RAND_MAX;
		int row = i / 64;

		rad[i] = 0.6 + 0.8 * u;                         /* background */
		lampish[i] = kind != 1;                         /* (indoor: all lit by the mains) */
		if (kind == 0 && row >= 20 && row < 26 && i % 64 >= 28 && i % 64 < 40) {
			rad[i] = 60;                            /* the lamp: 0.75 % */
		}
		if (kind == 0 && row < 8) {
			rad[i] *= 3.5;                          /* a lit wall */
		}
		if (kind == 1 && row < 20) {
			rad[i] *= 5;                            /* outdoor: sky */
		}
	}
}

/* the levels of a frame taken with lines x gain at time t; light: the scene's scale; frame->line_ns */
static int wobble_cols = 8;       /* the lamp's width in cells this frame (the camera moved a little: 4..12 of 64, 0.4..1.6 % of the cells) */
static double wobble_gain = 1;

static void
measure(const struct redwood_camera_frame *f, double lines, double gain, double light, double t, double m, struct isp_stats *s)
{
	double sum = 0, white = 1023 - (double)f->black, T = lines * f->line_ns * 1e-9;

	memset(s, 0, sizeof(*s));
	for (int i = 0; i < CELLS; i++) {
		double t0 = t + (i / 64) * (0.030 / 64);        /* (row i/64 of 64 starts 30/64 ms after the one before) */
		double rr = rad[i];

		if (rr == 60 && (i % 64) - 28 >= wobble_cols) {
			rr = 1.0;                               /* (that part of the lamp is out of the picture now) */
		}
		double v = rr * light * wobble_gain * lines * gain * (lampish[i] ? mains(t0, T, m) : 1);

		v = v > white ? white : v;
		sum += v;
		s->hist[(int)v >> 4 > 63 ? 63 : (int)v >> 4]++;
	}
	s->cells = CELLS;
	s->mean[0] = s->mean[1] = s->mean[2] = sum / CELLS;
	for (int i = 63, above = 0; i >= 0; i--) {
		above += (int)s->hist[i];
		if (above * 100 >= CELLS) {
			s->bright = i * 16 + 8;
			break;
		}
	}
}

struct result { int commands, reversals, bad; double range, jump; };

#define NF 900
/*
 * hdr, eff: the sensor's delays; sd: the statistics' age (frames); old_pair: statistics with the current header; kind: 0 lamp room, 1 outdoor;
 * fps 30 or 60. Phases of 300 frames: the light as it is, x6 (a lamp switched on / the camera to the window), /6 again.
 */
static struct result
run(int hdr, int eff, int sd, bool old_pair, int kind, int fps, bool wobble, bool verbose)
{
	struct redwood_camera_frame f;
	struct isp isp;
	struct isp_stats st, ring[8];
	struct redwood_camera_command c;
	uint32_t hl[8], hg[8];                  /* the header's lines/gain per frame (ring) */
	double ql[16], qg[16];                  /* what the sensor has: [0] in the frame now; the newest command in [15] */
	double el[8], eg[8];                    /* the frame's real exposure per frame (ring) */
	double luma[NF];
	int cmd_frame[NF], dir[NF];
	struct result r = { 0, 0, 0, 0, 0 };
	double m = kind != 1 ? 0.3 : 0, base = kind == 0 ? 0.0006 : kind == 2 ? 0.012 : 0.03;

	memset(&f, 0, sizeof(f));
	f.width = 2048; f.height = 1152; f.black = 64; f.gain_unity = 256; f.gain_max = 16 * 256;
	f.frame_lines_min = fps == 60 ? 1588 : 3176; f.margin = 48; f.line_ns = 10494; f.frame_lines_max = 4 * 3176;
	f.frame_lines = f.frame_lines_min;
	isp_init(&isp);
	scene(kind == 2 ? 0 : kind);
	for (int i = 0; i < 16; i++) { ql[i] = 1500; qg[i] = 2; }
	for (int n = 0; n < NF; n++) {
		double light = n < 300 ? base : n < 600 ? base * 6 : base, t = n * (f.frame_lines_min * 10494e-9);
		int lag = hdr + eff;

		/* the frame now: real exposure ql[0]; its header says what was written HDR frames after the command, i.e. the queue's [eff] */
		wobble_cols = wobble ? 4 + (int)(4 + 4 * sin(2 * M_PI * n / (2.0 * fps))) : 8;
		wobble_gain = wobble ? 1 + 0.015 * ((double)rand() / RAND_MAX - 0.5) * 2 + 0.06 * sin(2 * M_PI * n / (1.3 * fps)) : 1;
		el[n % 8] = ql[0]; eg[n % 8] = qg[0];
		hl[n % 8] = (uint32_t)ql[eff]; hg[n % 8] = (uint32_t)(qg[eff] * 256 + 0.5);
		measure(&f, ql[0], qg[0], light, t, m, &ring[n % 8]);
		/* the picture of this frame, with the digital gain as AE left it on the frame before */
		luma[n] = srgb8(ring[n % 8].mean[1] * isp.digital / 959.0);
		/* AE: the statistics of frame n - sd */
		int k = n - sd < 0 ? 0 : n - sd;
		st = ring[k % 8];
		f.sequence = (uint64_t)(old_pair ? n : k) + 1;
		f.exposure_lines = old_pair ? hl[n % 8] : hl[k % 8];
		f.gain = old_pair ? hg[n % 8] : hg[k % 8];
		cmd_frame[n] = 0; dir[n] = 0;
		double before = (double)f.exposure_lines * f.gain;
		if (isp_ae(&isp, &f, &st, &c)) {
			double after = (double)c.a * c.b;

			cmd_frame[n] = 1;
			dir[n] = after > before * 1.01 ? 1 : after < before * 0.99 ? -1 : 0;
			ql[15] = c.a; qg[15] = c.b / 256.0;
			if (getenv("TRACE") && kind == atoi(getenv("TRACE")) && wobble == (getenv("TW") != NULL) && hdr == 2 && eff == 0 && sd == 2 && fps == 30) printf("     n %d cmd %u lines %.2fx dig %.2f aim %.2f mean %.0f hold %u\n", n, c.a, c.b / 256.0, isp.digital, isp.aim, st.mean[1], 0u);
			f.frame_lines = c.c ? c.c : f.frame_lines_min;
		}
		/* the delays: the newest command moves a place a frame until it is hdr + eff frames old */
		for (int i = 0; i < 15; i++) {
			if (i >= lag) { ql[i] = ql[15]; qg[i] = qg[15]; } else { ql[i] = ql[i + 1]; qg[i] = qg[i + 1]; }
		}
		(void)el; (void)eg;
	}
	/* the last 150 frames of each phase */
	for (int p = 0; p < 3; p++) {
		double lo = 1e9, hi = 0, jump = 0;
		int last = 0;

		for (int n = p * 300 + (wobble ? 40 : 90); n < p * 300 + 300; n++) {
			lo = luma[n] < lo ? luma[n] : lo;
			hi = luma[n] > hi ? luma[n] : hi;
			jump = fabs(luma[n] - luma[n - 1]) > jump ? fabs(luma[n] - luma[n - 1]) : jump;
			r.commands += cmd_frame[n];
			if (dir[n]) {
				r.reversals += last && dir[n] != last;
				last = dir[n];
			}
		}
		r.range = hi - lo > r.range ? hi - lo : r.range;
		r.jump = jump > r.jump ? jump : r.jump;
	}
	if (verbose) {
		printf("     luma 0..120:");
		for (int n = 0; n < 120; n += 2) printf(" %.0f", luma[n]);
		printf("\n     luma 300..360:");
		for (int n = 300; n < 360; n++) printf(" %.0f", luma[n]);
		printf("\n");
	}
	return r;
}

int
main(int argc, char **argv)
{
	bool check = argc > 1 && strcmp(argv[1], "-check") == 0;
	bool verbose = getenv("VERBOSE") != NULL;
	int fails = 0, cases = 0;
	const char *pair = getenv("PAIR");
	bool old_pair = pair && strcmp(pair, "old") == 0;

	printf("t_aesim: statistics paired with %s header\n", old_pair ? "the CURRENT frame's (gpuauto2 core)" : "their own frame's (ae2 core)");
	printf("  kind fps hdr eff stats | commands reversals luma-range max-jump (frames 90..300 of each of 3 phases, 40..300 with the wobble: the lamp in and out of the top 1 %%, the light +-6 %%)\n");
	for (int kind = 0; kind <= 2; kind++) {
		for (int wob = 0; wob <= 1; wob++)
		for (int fps = 30; fps <= 60; fps += 30) {
			for (int hdr = 1; hdr <= 2; hdr++) {
				for (int eff = 0; eff <= 2; eff++) {
					for (int sd = 0; sd <= 2; sd++) {
						struct result r = run(hdr, eff, sd, old_pair, kind, fps, wob, verbose && hdr == 1 && eff == 1 && sd == 2 && fps == 30 && wob);
						/* steady: no limit cycle (at most a couple of commands and one reversal per settled phase), no visible jumps */
						/* (with the wobble the light itself moves +-6 %: AE may follow it in small steps, but no big steps to and fro) */
						bool bad = wob ? r.jump > 8 || r.range > 30 : r.commands > 6 || r.reversals > 3 || r.range > 12 || r.jump > 8;

						cases++;
						fails += bad;
						if (bad || verbose || (hdr == 1 && eff == 1)) {
							printf("  %s%s %2d   %d   %d   %d   | %4d %4d %6.1f %6.1f%s\n", kind == 1 ? "out " : kind == 2 ? "LAMP" : "lamp", wob ? "~" : " ", fps, hdr, eff, sd,
							    r.commands, r.reversals, r.range, r.jump, bad ? "   LIMIT CYCLE / JUMPS" : "");
						}
					}
				}
			}
		}
	}
	printf("t_aesim: %d of %d cases with a limit cycle or visible jumps\n", fails, cases);
	return check && fails ? 1 : 0;
}
