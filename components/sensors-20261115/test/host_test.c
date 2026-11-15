/* 0760 host test: the real n2scp_motion_out / n2scp_motion_target bodies (carved from b/bsd/dev/nord2_scp.inc) with stubs */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int n2scp_axflip, n2scp_motion = 2;
static volatile int32_t n2scp_ios[3][3];
static volatile uint32_t n2scp_out[3];
static int last_kind, last[3], want_ret[3];
static void sweet_hid_motion(int k, int x, int y, int z) { last_kind = k; last[0] = x; last[1] = y; last[2] = z; }
static int want_orient;
static int sweet_hid_motion_want(int k) { return k == 3 ? want_orient : want_ret[k]; }
static int orient_sent = -1, orient_sends;
static void sweet_hid_orientation(int o) { orient_sent = o; orient_sends++; }
static int n2scp_orient = 1, n2scp_orient_force = -1;
static volatile int n2scp_orient_now = -1;
static volatile uint32_t n2scp_orient_changes;
static int32_t n2scp_lp[3];
static int n2scp_lp_init, n2scp_orient_cand = -1, n2scp_orient_cnt;
static int n2scp_rate_now_acc = 50;
#include "carved.inc"
/* feed n samples of Android raw (mm/s^2) at 50 Hz */
static void pose(int ax, int ay, int az, int n) { for (int i = 0; i < n; i++) { n2scp_motion_out(0, ax, ay, az); } }
static double fx(int v) { return v / 65536.0; }
int main(void)
{
	/* accelerometer: face up on a table, Android z = +9.807 m/s^2 (9807) -> iOS z = -1 g */
	n2scp_motion_out(0, 0, 0, 9807);
	CHECK(last_kind == 0 && last[0] == 0 && fx(last[2]) < -0.999 && fx(last[2]) > -1.001, "acc z %f", fx(last[2]));
	/* upright portrait: Android y = +9.807 -> iOS y = -1 g; x right edge down: Android x = -9.807 -> iOS x = +1 g */
	n2scp_motion_out(0, -9807, 9807, 0);
	CHECK(fx(last[0]) > 0.999 && fx(last[1]) < -0.999, "acc x %f y %f", fx(last[0]), fx(last[1]));
	/* logged rest sample of q10j: 196 100 -9864 -> 0.020 0.010 -(-1.006) */
	n2scp_motion_out(0, 196, 100, -9864);
	CHECK(fx(last[2]) > 1.0 && fx(last[2]) < 1.01, "acc logged z %f", fx(last[2]));
	/* gyro 1 rad/s = 1000000 */
	n2scp_motion_out(1, 1000000, -500000, 0);
	CHECK(fx(last[0]) > 0.999 && fx(last[0]) < 1.001 && fx(last[1]) < -0.499 && fx(last[1]) > -0.501, "gyr %f %f", fx(last[0]), fx(last[1]));
	/* mag 45 uT = 4500 */
	n2scp_motion_out(2, 4500, -14855, 0);
	CHECK(fx(last[0]) > 44.99 && fx(last[0]) < 45.01 && fx(last[1]) < -148.5 && fx(last[1]) > -148.6, "mag %f %f", fx(last[0]), fx(last[1]));
	/* axflip bit 2 = accel z */
	n2scp_axflip = 4;
	n2scp_motion_out(0, 0, 0, 9807);
	CHECK(fx(last[2]) > 0.999, "flip z %f", fx(last[2]));
	n2scp_axflip = 1 << 3;
	n2scp_motion_out(1, 1000000, 0, 0);
	CHECK(fx(last[0]) < -0.999, "flip gyro x %f", fx(last[0]));
	n2scp_axflip = 0;
	CHECK(n2scp_out[0] == 4 && n2scp_out[1] == 2 && n2scp_out[2] == 1, "out counts %u %u %u", n2scp_out[0], n2scp_out[1], n2scp_out[2]);
	/* targets */
	int cases[][3] = { { 0, 0, 0 }, { 1, 5, 5 }, { 7, 10, 10 }, { 20, 25, 25 }, { 33, 50, 50 }, { 60, 100, 50 }, { 500, 100, 50 } };
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		want_ret[0] = want_ret[2] = cases[i][0];
		CHECK(n2scp_motion_target(0) == cases[i][1], "acc want %d -> %d", cases[i][0], n2scp_motion_target(0));
		CHECK(n2scp_motion_target(2) == cases[i][2], "mag want %d -> %d", cases[i][0], n2scp_motion_target(2));
	}
	n2scp_motion = 1;
	CHECK(n2scp_motion_target(0) == 50 && n2scp_motion_target(1) == 50 && n2scp_motion_target(2) == 25, "forced");
	n2scp_motion = 0; want_ret[0] = 50;
	CHECK(n2scp_motion_target(0) == 0, "off");
	/* 0761: accelerometer follows the orientation service's demand */
	n2scp_motion = 2; want_ret[0] = 0; want_orient = 25;
	CHECK(n2scp_motion_target(0) == 25, "acc for orientation %d", n2scp_motion_target(0));
	n2scp_orient = 0; CHECK(n2scp_motion_target(0) == 0, "orient off -> acc off"); n2scp_orient = 1;
	/* 0761: orientation detector, Android raw input (face up = +z 9807) */
	n2scp_lp_init = 0; n2scp_orient_now = -1; orient_sends = 0;
	pose(0, 0, 9807, 30);  CHECK(orient_sent == 5, "face up -> %d", orient_sent);
	pose(0, 9807, 0, 30);  CHECK(orient_sent == 1, "portrait -> %d", orient_sent);
	pose(0, -9807, 0, 30); CHECK(orient_sent == 2, "upside down -> %d", orient_sent);
	pose(9807, 0, 0, 30);  CHECK(orient_sent == 3, "Android x +g (left edge down, iOS x -1 g) -> landscape left %d", orient_sent);
	pose(-9807, 0, 0, 30); CHECK(orient_sent == 4, "landscape right -> %d", orient_sent);
	pose(0, 0, -9807, 30); CHECK(orient_sent == 6, "face down -> %d", orient_sent);
	int before = orient_sends;
	pose(-9807, 0, 0, 30); pose(-6900, 6900, 0, 60);   /* 45 deg between landscape right and portrait: ambiguous, keep */
	CHECK(orient_sent == 4 && orient_sends == before + 1, "ambiguous keeps landscape right (%d, sends %d)", orient_sent, orient_sends - before);
	before = orient_sends;
	pose(9807, 0, 0, 5);   /* 100 ms glitch: not enough to switch (hold 12 samples + low-pass) */
	pose(-9807, 0, 0, 30);
	CHECK(orient_sent == 4 && orient_sends == before, "short glitch ignored (%d)", orient_sent);
	n2scp_orient_force = 2; before = orient_sends; pose(0, 9807, 0, 30);
	CHECK(orient_sends == before, "forced: detector silent"); n2scp_orient_force = -1;
	printf(fails ? "SENSORS_HOST_TESTS_FAIL %d\n" : "SENSORS_HOST_TESTS_OK\n", fails);
	return fails != 0;
}
