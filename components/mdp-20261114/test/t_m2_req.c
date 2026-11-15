/* 0685 host test: sweet_m2_req.h decoder on the 16 mediaserverd requests of q10jmdp14 (fixtures/) + synthetic refusals. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../piece-0685/patched-sources-0685/iokit/Kernel/arm/sweet_m2_req.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
int main(int argc, char **argv)
{
	FILE *f = fopen(argc > 1 ? argv[1] : "fixtures/m2-requests-magnifier-q10jmdp14.txt", "r");
	if (!f) { printf("FAIL no fixture\n"); return 1; }
	char line[512]; uint8_t buf[0x180]; int nreq = 0; uint32_t len = 0; int have = 0;
	struct sweet_m2_req r;
	for (;;) {
		char *l = fgets(line, sizeof line, f);
		int start = l && strstr(l, "request #");
		if ((start || !l) && have) {
			memset(&r, 0, sizeof r);
			int e = sweet_m2_decode(buf, 0x170, &r);
			nreq++;
			CHECK(e == SWEET_M2_OK, "req %d decode %s", nreq, sweet_m2_reason[e]);
			CHECK(r.rot == 1 && r.flip == 0 && r.sx == 0 && r.sy == 0 && r.dx == 0 && r.dy == 0, "req %d rot/origin", nreq);
			CHECK((r.sw == 2048 && r.sh == 1536 && r.dw == 1536 && r.dh == 2048) || (r.sw == 1504 && r.sh == 1128 && r.dw == 1128 && r.dh == 1504),
			    "req %d rects %u %u -> %u %u", nreq, r.sw, r.sh, r.dw, r.dh);
			printf("req %2d: src %#x dst %#x flags %#llx rot %u src %ux%u+%u+%u dst %ux%u+%u+%u\n", nreq, r.src, r.dst,
			    (unsigned long long)r.flags, r.rot, r.sw, r.sh, r.sx, r.sy, r.dw, r.dh, r.dx, r.dy);
			have = 0;
		}
		if (!l) { break; }
		if (start) { memset(buf, 0, sizeof buf); len = 0; have = 1; continue; }
		unsigned off, w[8];
		if (have && sscanf(l, " +%x %x %x %x %x %x %x %x %x", &off, &w[0], &w[1], &w[2], &w[3], &w[4], &w[5], &w[6], &w[7]) == 9 && off + 32 <= sizeof buf) {
			for (int k = 0; k < 8; k++) { for (int b = 0; b < 4; b++) { buf[off + 4 * k + b] = (uint8_t)(w[k] >> (8 * b)); } }
			len = off + 32;
		}
	}
	(void)len;
	CHECK(nreq == 16, "%d requests in the fixture", nreq);
	/* synthetic: async, custom filter, fractional, options, bad transform, whole-surface */
	uint8_t q[0x170];
	memset(q, 0, sizeof q); q[8] = 1;                                CHECK(sweet_m2_decode(q, sizeof q, &r) == SWEET_M2_R_ASYNC, "async");
	memset(q, 0, sizeof q); q[8] = 1; q[0x21] = 0x04;                CHECK(sweet_m2_decode(q, sizeof q, &r) == SWEET_M2_OK, "async + SkipAsyncCallback");
	memset(q, 0, sizeof q); q[0x20] = 0x40;                          CHECK(sweet_m2_decode(q, sizeof q, &r) == SWEET_M2_R_FLAGS, "custom filter");
	memset(q, 0, sizeof q); q[0x20] = 0x0c;                          CHECK(sweet_m2_decode(q, sizeof q, &r) == SWEET_M2_R_TRANSFORM, "rot bits 0xc");
	memset(q, 0, sizeof q); q[0x38] = 0x80; q[0x3a] = 0x10; q[0x42] = 0x10; CHECK(sweet_m2_decode(q, sizeof q, &r) == SWEET_M2_R_FRACT, "fractional");
	memset(q, 0, sizeof q); q[0x100] = 1;                            CHECK(sweet_m2_decode(q, sizeof q, &r) == SWEET_M2_R_OPTIONS, "options");
	memset(q, 0, sizeof q); q[0x20] = 0x08;                          CHECK(sweet_m2_decode(q, sizeof q, &r) == SWEET_M2_OK && r.rot == 2 && r.sw == 0, "whole-surface rot180");
	memset(q, 0, sizeof q); q[0x3a] = 0x10;                          CHECK(sweet_m2_decode(q, sizeof q, &r) == SWEET_M2_R_RECT, "half rectangle");
	memset(q, 0, sizeof q);                                          CHECK(sweet_m2_decode(q, 0x40, &r) == SWEET_M2_R_SHORT, "short");
	printf(fails ? "T_M2_REQ FAILED %d\n" : "T_M2_REQ ALL OK\n", fails);
	return fails != 0;
}
