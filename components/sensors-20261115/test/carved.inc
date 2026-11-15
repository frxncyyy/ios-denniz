
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

