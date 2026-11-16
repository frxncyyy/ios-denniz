/*
 * y2s91 (thumbgrid) host test: no GPU, no phone. Built and run by thumbgrid-host-test.sh against the existing host Mesa (softpipe).
 *   A. rwa_texture_convert_ycbcr_exact on softpipe: a 375x667 picture out of a 376x668 source whose padding column and row hold
 *      garbage: every pixel is the conversion of ITS texel (no resampling, no padding), byte-identical to the old two-pass path
 *      (convert into 376x668, copy 375x667); the old single pass over the padded source resamples (the bug y2s63's copy avoided).
 *      Refusals; rwa_buffer_recycle / rwa_texture_recycle.
 *   B. rwa_tg_ubo_tail (taken verbatim out of air_metal.c by the script): after every fill the padded copy equals one that is fully
 *      re-zeroed (the old behaviour), over random (left, can_read) sequences.
 *   C. rw_thumbgrid_pure.h: the submit ring against a model (out-of-order completions), the size classes, the CPU reference against
 *      the per-pixel formula of rw_ycbcr_odd_note, the right-edge block with garbage in the luma padding, the compare classification,
 *      the check blocks inside the picture.
 * What it cannot show: Panfrost's two-plane sampling, Mali precision, the deferred dispatch on a real queue, the phone's pools.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "air_metal.h"
#include "rw_thumbgrid_pure.h"

int rwa_texture_convert_ycbcr_exact(struct rwa_texture *, struct rwa_texture *, int, int, char *, size_t);
size_t rwa_buffer_recycle(struct rwa_buffer *);
int rwa_texture_recycle(struct rwa_texture *);

/* B: the function under test, verbatim (ubo_tail.inc is cut out of air_metal.c by the script) */
#define MIN2(a, b) ((a) < (b) ? (a) : (b))
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define p_atomic_add(p, v) (*(p) += (v))
static uint64_t rwa_tg_ubo_zeroed, rwa_tg_ubo_saved;
#include "ubo_tail.inc"

static char why[512];
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAILED line %d: %s (%s)\n", __LINE__, #x, why); return 1; } } while (0)

static void
reference(int y, int cb, int cr, int matrix, int full, int out[3])      /* B, G, R (ycbcr_gpu_host_test.c) */
{
	static const double k[3][2] = { { 0.299, 0.114 }, { 0.2126, 0.0722 }, { 0.2627, 0.0593 } };
	const double kr = k[matrix][0], kb = k[matrix][1], kg = 1.0 - kr - kb;
	double Y = full ? y / 255.0 : (y < 16 ? 0.0 : (y - 16) / 219.0), U = (cb - 128) / (full ? 255.0 : 224.0), V = (cr - 128) / (full ? 255.0 : 224.0);
	double c[3] = { Y + 2.0 * (1.0 - kb) * U, Y - 2.0 * kb * (1.0 - kb) / kg * U - 2.0 * kr * (1.0 - kr) / kg * V, Y + 2.0 * (1.0 - kr) * V };

	for (int i = 0; i < 3; i++) {
		double v = floor(c[i] * 255.0 + 0.5);
		out[i] = v < 0 ? 0 : v > 255 ? 255 : (int)v;
	}
}

static int
test_library(void)
{
	enum { W = 375, H = 667, PW = 376, PH = 668 };
	static unsigned char src_px[PH][PW][4], exact_px[H][W][4], old_px[H][W][4], one_px[H][W][4];
	const unsigned origin[3] = { 0, 0, 0 }, psize[3] = { PW, PH, 1 }, size[3] = { W, H, 1 };

	setenv("RWA_SOFTWARE", "1", 1);
	struct rwa_device *device = rwa_device_open(why, sizeof(why));
	CHECK(device != NULL);
	struct rwa_texture_shape s = { .pixel_format = 70, .type = 2, .width = PW, .height = PH, .depth = 1, .array_length = 1,
				       .mip_levels = 1, .sample_count = 1, .usage = 1 };
	struct rwa_texture *src = rwa_texture_create(device, &s, why, sizeof(why));
	CHECK(src != NULL);
	s.pixel_format = 80;
	s.usage = 1 | 4;
	struct rwa_texture *tmp = rwa_texture_create(device, &s, why, sizeof(why));     /* the old 376x668 picture */
	CHECK(tmp != NULL);
	s.width = W;
	s.height = H;
	struct rwa_texture *dst = rwa_texture_create(device, &s, why, sizeof(why));
	CHECK(dst != NULL);
	s.width = W + 3;
	struct rwa_texture *wide = rwa_texture_create(device, &s, why, sizeof(why));
	CHECK(wide != NULL);

	srand(91);
	for (int y = 0; y < PH; y++) {
		for (int x = 0; x < PW; x++) {
			unsigned char *p = src_px[y][x];

			if (x == PW - 1 || y == PH - 1) {                       /* the padding: garbage that must never show */
				p[0] = 255; p[1] = 0; p[2] = 255;
			} else {
				p[0] = (unsigned char)rand(); p[1] = (unsigned char)rand(); p[2] = (unsigned char)rand();
			}
			p[3] = 0;
		}
	}
	CHECK(rwa_texture_write(src, 0, 0, origin, psize, src_px, PW * 4, why, sizeof(why)) == 0);

	/* refusals: a source smaller than the destination, or more than one texel larger */
	CHECK(rwa_texture_convert_ycbcr_exact(wide, src, 0, 1, why, sizeof(why)) == -1 && strstr(why, "exact"));
	CHECK(rwa_texture_convert_ycbcr_exact(tmp, dst, 0, 1, why, sizeof(why)) == -1);
	CHECK(rwa_texture_convert_ycbcr_exact(NULL, src, 0, 1, why, sizeof(why)) == -1);

	int worst = 0;

	for (int matrix = 0; matrix < 3; matrix++) {
		for (int full = 0; full < 2; full++) {
			why[0] = 0;
			CHECK(rwa_texture_convert_ycbcr_exact(dst, src, matrix, full, why, sizeof(why)) == 0);
			CHECK(rwa_finish(device, why, sizeof(why)) == 0);
			CHECK(rwa_texture_read(dst, 0, 0, origin, size, exact_px, W * 4, why, sizeof(why)) == 0);
			for (int y = 0; y < H; y++) {
				for (int x = 0; x < W; x++) {
					int want[3];

					reference(src_px[y][x][0], src_px[y][x][1], src_px[y][x][2], matrix, full, want);
					for (int c = 0; c < 3; c++) {
						int d = abs((int)exact_px[y][x][c] - want[c]);

						if (d > 1) {
							fprintf(stderr, "FAILED exact matrix %d full %d at (%d,%d) ch %d: %d want %d\n", matrix, full, x, y, c,
								exact_px[y][x][c], want[c]);
							return 1;
						}
						worst = d > worst ? d : worst;
					}
				}
			}
			/* the old two passes give the same bytes */
			CHECK(rwa_texture_convert_ycbcr(tmp, src, matrix, full, why, sizeof(why)) == 0);
			CHECK(rwa_texture_copy(dst, 0, 0, origin, tmp, 0, 0, origin, size, why, sizeof(why)) == 0);
			CHECK(rwa_finish(device, why, sizeof(why)) == 0);
			CHECK(rwa_texture_read(dst, 0, 0, origin, size, old_px, W * 4, why, sizeof(why)) == 0);
			CHECK(memcmp(old_px, exact_px, sizeof(old_px)) == 0);
		}
	}
	/* the old ONE pass over the padded source resamples 376 texels onto 375 pixels: most pixels are not their texel */
	CHECK(rwa_texture_convert_ycbcr(dst, src, 0, 1, why, sizeof(why)) == 0);
	CHECK(rwa_finish(device, why, sizeof(why)) == 0);
	CHECK(rwa_texture_read(dst, 0, 0, origin, size, one_px, W * 4, why, sizeof(why)) == 0);
	unsigned long off = 0;

	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			int want[3];

			reference(src_px[y][x][0], src_px[y][x][1], src_px[y][x][2], 0, 1, want);
			off += abs((int)one_px[y][x][0] - want[0]) > 1 || abs((int)one_px[y][x][1] - want[1]) > 1 ||
			       abs((int)one_px[y][x][2] - want[2]) > 1;
		}
	}
	CHECK(off > (unsigned long)W * H / 4);
	printf("A: exact pass == texel conversion (worst %d level), == old padded+copy byte for byte; old single pass over the padding: %lu of "
	       "%d pixels off\n", worst, off, W * H);

	/* recycle hooks */
	struct rwa_buffer *b = rwa_buffer_create(device, 65536, why, sizeof(why));
	CHECK(b != NULL);
	CHECK(rwa_buffer_recycle(b) == 65536);
	CHECK(rwa_buffer_map(b) != NULL);
	rwa_buffer_destroy(b);
	CHECK(rwa_texture_recycle(dst) == 0);
	CHECK(rwa_texture_recycle(NULL) == -1);
	rwa_texture_destroy(src);
	rwa_texture_destroy(tmp);
	rwa_texture_destroy(dst);
	rwa_texture_destroy(wide);
	rwa_device_close(device);
	printf("A: refusals and recycle hooks ok\n");
	return 0;
}

static int
test_ubo_tail(void)
{
	enum { CAP = 65536 };
	static char a[CAP], ref[CAP], src[CAP];
	size_t dirty = 0;
	unsigned long steps = 0;

	memset(a, 0, sizeof(a));
	srand(7);
	for (int i = 0; i < 200000; i++) {
		size_t can_read = (size_t)(rand() % 4 == 0 ? 16 + rand() % 512 : 1 + rand() % CAP);
		size_t left = (size_t)(rand() % (int)can_read);

		for (size_t k = 0; k < left; k++) {
			src[k] = (char)(rand() | 1);    /* never zero: a missed clear shows */
		}
		memcpy(a, src, left);
		rwa_tg_ubo_tail(a, left, can_read, &dirty);
		memcpy(ref, src, left);
		memset(ref + left, 0, can_read - left);
		if (memcmp(a, ref, can_read) != 0) {
			fprintf(stderr, "FAILED ubo tail step %d: left %zu can_read %zu\n", i, left, can_read);
			return 1;
		}
		/* and the invariant: nothing past `dirty` is non-zero */
		for (size_t k = dirty; k < CAP; k += 61) {
			if (a[k] != 0) {
				fprintf(stderr, "FAILED ubo tail invariant step %d at %zu (dirty %zu)\n", i, k, dirty);
				return 1;
			}
		}
		steps++;
	}
	printf("B: ubo tail == full re-zero over %lu random fills; zeroed %llu KiB, saved %llu KiB (%.1f%% saved)\n", steps,
	       (unsigned long long)(rwa_tg_ubo_zeroed >> 10), (unsigned long long)(rwa_tg_ubo_saved >> 10),
	       100.0 * rwa_tg_ubo_saved / (double)(rwa_tg_ubo_saved + rwa_tg_ubo_zeroed));
	return 0;
}

static int
test_pure(void)
{
	/* submit ring vs model */
	static struct rw_tg_seq q;
	static uint8_t back[200000];
	uint64_t outq[64];
	int nout = 0;

	rw_tg_seq_init(&q);
	srand(3);
	for (int i = 0; i < 150000; i++) {
		int op = rand() % 3;

		if (op == 0 && nout < 64) {
			uint64_t s = rw_tg_seq_submit(&q);

			CHECK(s != 0);
			outq[nout++] = s;
		} else if (op == 1 && nout > 0) {
			int j = rand() % nout;

			rw_tg_seq_done(&q, outq[j]);
			back[outq[j]] = 1;
			outq[j] = outq[--nout];
		} else {
			/* a mark now: safe exactly when every number below it is back */
			uint64_t m = rw_tg_seq_mark(&q);
			int want = 1;

			for (int j = 0; j < nout; j++) {
				want &= outq[j] >= m;
			}
			CHECK(rw_tg_seq_safe(&q, m) == want);
			if (nout > 0) {
				uint64_t o = outq[rand() % nout];

				CHECK(!rw_tg_seq_safe(&q, o + 1));     /* something let go after `o` was submitted waits for it */
			}
		}
	}
	/* a full window: untracked submits block every mark until they come back */
	rw_tg_seq_init(&q);
	for (unsigned i = 0; i < RW_TG_SEQ_RING; i++) {
		CHECK(rw_tg_seq_submit(&q) == i + 1);
	}
	CHECK(rw_tg_seq_submit(&q) == 0);
	for (unsigned i = 0; i < RW_TG_SEQ_RING; i++) {
		rw_tg_seq_done(&q, i + 1);
	}
	CHECK(!rw_tg_seq_safe(&q, rw_tg_seq_mark(&q)));
	rw_tg_seq_done(&q, 0);
	CHECK(rw_tg_seq_safe(&q, rw_tg_seq_mark(&q)));
	printf("C: submit ring == model over 150000 steps, untracked submits block\n");

	/* classes */
	size_t prev = 0;

	for (size_t len = 1; len < (40u << 20); len += len < 70000 ? 1 + len / 64 : 1 + len / 7) {
		size_t c = rw_tg_class(len);

		CHECK(c >= len && c >= prev && rw_tg_class(c) == c);
		CHECK(len <= 4096 || c <= len * 2);
		CHECK(len <= 65536 || c - len < 65536 + (len > (1u << 20) ? (1u << 20) : 0));
		prev = c;
	}
	printf("C: size classes monotonic, fixed points, bounded waste\n");

	/* reference vs the per-pixel formula, constant chroma (bilinear of a constant is the constant) */
	enum { W = 375, H = 667, LROW = 384, CW = 188, CH = 334, CROW = 384 };
	static uint8_t luma[LROW * 668], chroma[CROW * CH], out[16 * 16 * 4], out2[16 * 16 * 4];

	for (int m = 0; m < 3; m++) {
		for (int full = 0; full < 2; full++) {
			int cb = 60 + 40 * m, cr = 200 - 30 * full;

			for (int i = 0; i < LROW * 668; i++) luma[i] = (uint8_t)rand();
			for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) { chroma[y * CROW + 2 * x] = (uint8_t)cb; chroma[y * CROW + 2 * x + 1] = (uint8_t)cr; }
			for (int k = 0; k < 50; k++) {
				unsigned bx, by, bw, bh;
				int edge;

				rw_tg_check_block((unsigned long)k, W, H, &bx, &by, &bw, &bh, &edge);
				CHECK(bx + bw <= W && by + bh <= H && bw == 16 && bh == 16);
				rw_tg_ref_bgra(luma, LROW, chroma, CROW, W, H, m, full, bx, by, bw, bh, out);
				for (unsigned j = 0; j < bh; j++) {
					for (unsigned i = 0; i < bw; i++) {
						int want[3];

						reference(luma[(by + j) * LROW + bx + i], cb, cr, m, full, want);
						for (int c = 0; c < 3; c++) {
							CHECK(abs((int)out[(j * bw + i) * 4 + c] - want[c]) <= 1);
						}
					}
				}
			}
		}
	}
	/* the right edge of a 375-wide picture: the luma padding column 375 and anything past chroma column 187 are never read */
	for (int i = 0; i < LROW * 668; i++) luma[i] = (uint8_t)rand();
	for (int i = 0; i < CROW * CH; i++) chroma[i] = (uint8_t)rand();
	rw_tg_ref_bgra(luma, LROW, chroma, CROW, W, H, 0, 1, W - 16, H - 16, 16, 16, out);
	for (int y = 0; y < 668; y++) for (int x = W; x < LROW; x++) luma[y * LROW + x] ^= 0xff;
	for (int y = 0; y < CH; y++) for (int x = 2 * CW; x < CROW; x++) chroma[y * CROW + x] ^= 0xff;
	for (int x = 0; x < LROW; x++) luma[667 * LROW + x] ^= 0xff;
	rw_tg_ref_bgra(luma, LROW, chroma, CROW, W, H, 0, 1, W - 16, H - 16, 16, 16, out2);
	CHECK(memcmp(out, out2, sizeof(out)) == 0);
	printf("C: reference == formula (constant chroma, 3 matrices x 2 ranges, 50 blocks each); edge block blind to the padding\n");

	/* compare classification */
	struct rw_tg_cmp c;

	memcpy(out2, out, sizeof(out));
	rw_tg_compare(out, 64, out2, 64, 16, 16, 10, 48, &c);
	CHECK(!rw_tg_is_mismatch(&c) && c.maxdiff == 0);
	out2[4 * 7] ^= 8;                                       /* a filter-precision difference: fine */
	rw_tg_compare(out, 64, out2, 64, 16, 16, 10, 48, &c);
	CHECK(!rw_tg_is_mismatch(&c));
	for (int i = 0; i < 16; i++) {                          /* a black right column: a thin edge line */
		uint8_t *p = out2 + (i * 16 + 15) * 4;
		p[0] = p[1] = p[2] = (uint8_t)(out[(i * 16 + 15) * 4] > 128 ? 0 : 255);
	}
	rw_tg_compare(out, 64, out2, 64, 16, 16, 10, 48, &c);
	CHECK(rw_tg_is_mismatch(&c) && c.gross > 0);
	printf("C: compare: identical/precision ok, edge line = gross mismatch\n");
	return 0;
}

int
main(void)
{
	if (test_pure() || test_ubo_tail() || test_library()) {
		return 1;
	}
	printf("rw_thumbgrid_host_test: ALL OK\n");
	return 0;
}
