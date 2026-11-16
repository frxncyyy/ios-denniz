/* y2s70: generated from camera-userland-20261112-hd60/src/rwcam_render.c (smooth, with_before): the CPU noise reduction, verbatim */
#define SMOOTH_BANDS 8
struct smooth_job {
	uint8_t *p, *edges;             /* edges: for each band the row above its first and the row under its last, as they were */
	size_t stride;
	uint32_t bytes, rows;
	unsigned step;
	int near;
};

static void
smooth_band(void *context, size_t band)
{
	const struct smooth_job *j = context;
	uint32_t a = (uint32_t)(j->rows * band / SMOOTH_BANDS), b = (uint32_t)(j->rows * (band + 1) / SMOOTH_BANDS), bytes = j->bytes;
	uint8_t *tmp = malloc(2 * (size_t)bytes), *above = tmp, *here = tmp != NULL ? tmp + bytes : NULL;
	const uint8_t *top = j->edges + (size_t)(2 * band) * bytes, *bottom = top + bytes;

	a = a < 1 ? 1 : a;
	b = b > j->rows - 1 ? j->rows - 1 : b;
	if (tmp == NULL || a >= b) {
		free(tmp);
		return;
	}
	memcpy(above, top, bytes);
	for (uint32_t y = a; y < b; y++) {
		uint8_t *row = j->p + (size_t)y * j->stride;
		const uint8_t *below = y + 1 == b ? bottom : row + j->stride;

		memcpy(here, row, bytes);
		for (uint32_t x = j->step; x + j->step < bytes; x++) {
			int v = (above[x] + below[x] + here[x - j->step] + here[x + j->step] + 4 * here[x] + 4) >> 3;

			if (j->near <= 0 || (v - here[x] < j->near && here[x] - v < j->near)) {
				row[x] = (uint8_t)v;
			}
		}
		uint8_t *t = above;

		above = here;
		here = t;
	}
	free(tmp);
}

static void
smooth(uint8_t *p, size_t stride, uint32_t bytes, uint32_t rows, unsigned step, uint8_t *tmp, int near)
{
	struct smooth_job j = { p, NULL, stride, bytes, rows, step, near };

	(void)tmp;
	if (rows < 3 || bytes < 3 * step || (j.edges = malloc(2 * SMOOTH_BANDS * (size_t)bytes)) == NULL) {
		return;
	}
	for (uint32_t k = 0; k < SMOOTH_BANDS; k++) {
		uint32_t a = rows * k / SMOOTH_BANDS, b = rows * (k + 1) / SMOOTH_BANDS;

		a = a < 1 ? 1 : a;
		b = b > rows - 1 ? rows - 1 : b;
		memcpy(j.edges + (size_t)(2 * k) * bytes, p + (size_t)(a - 1) * stride, bytes);
		memcpy(j.edges + (size_t)(2 * k + 1) * bytes, p + (size_t)b * stride, bytes);
	}
	if (R.apply != NULL) {
		R.apply(SMOOTH_BANDS, &j, smooth_band);
	} else {
		for (size_t k = 0; k < SMOOTH_BANDS; k++) {
			smooth_band(&j, k);
		}
	}
	free(j.edges);
}

/*
 * The picture and the one before it (run q10ia: "things that move drag a trail behind them, very annoying" -- each pixel by itself was drawn
 * 60 % of the way to the picture before wherever it differed from it by less than 26, and something moving through the dark differs by less).
 * By BLOCKS now: the brightness's mean difference from the picture before of each 8 x 8 block (every other pixel of it); the noise is what
 * that is in the calmer blocks; a block that differs by more than RWCAM_NR_MOVES times the noise has something moving in it and is left as it
 * is (and its eight neighbours, and the picture before forgotten there); when over RWCAM_NR_ALL of the blocks move (the camera does), all
 * are. In the blocks that are still, each pixel goes `keep' of 256 of the way to the picture before where it differs by less than `near'.
 *   near, keep: level 1 brightness 10, 90; colours 8, 102. Level 2 brightness 18, 128; colours 14, 140. `near' is at least three times the
 *   noise (three quarters of that for the colours), 40 at most: with what moves left out by the blocks it can be that wide.
 */
#define RWCAM_NR_BLOCK  8
#define RWCAM_NR_MOVES  1.7
#define RWCAM_NR_ALL    0.35
/* with_before()'s last step, the pixels, for the blocks of some of the rows of blocks: each block by itself, on the cores */
struct before_job {
	uint8_t *y, *uv, *by, *buv;
	const uint8_t *moves;
	size_t ys, uvs;
	uint32_t width, height, bw, bh, cw, moving;
	int near_y, keep_y, near_c, keep_c;
};
#define BEFORE_PIECES 8

static void
before_piece(void *context, size_t piece)
{
	const struct before_job *j = context;
	uint8_t *y = j->y, *uv = j->uv, *by = j->by, *buv = j->buv;
	const uint8_t *moves = j->moves;
	size_t ys = j->ys, uvs = j->uvs;
	uint32_t width = j->width, height = j->height, bw = j->bw, bh = j->bh, cw = j->cw, moving = j->moving, n = bw * bh;
	int near_y = j->near_y, keep_y = j->keep_y, near_c = j->near_c, keep_c = j->keep_c;

	for (uint32_t b = (uint32_t)(bh * piece / BEFORE_PIECES) * bw; b < (uint32_t)(bh * (piece + 1) / BEFORE_PIECES) * bw; b++) {
		uint32_t bx = b % bw, byy = b / bw, x0 = bx * RWCAM_NR_BLOCK, y0 = byy * RWCAM_NR_BLOCK;
		bool still = (double)moving <= RWCAM_NR_ALL * n;

		for (int dy = -1; still && dy <= 1; dy++) {
			for (int dx = -1; still && dx <= 1; dx++) {
				int64_t nx = (int64_t)bx + dx, ny = (int64_t)byy + dy;

				still = nx < 0 || ny < 0 || nx >= bw || ny >= bh || !(moves[ny * bw + nx] & 1);
			}
		}
		for (uint32_t yy = y0; yy < y0 + RWCAM_NR_BLOCK && yy < height; yy++) {
			uint8_t *row = y + (size_t)yy * ys, *before = by + (size_t)yy * width;

			for (uint32_t x = x0; x < x0 + RWCAM_NR_BLOCK && x < width; x++) {
				int d = (int)row[x] - (int)before[x], ad = d < 0 ? -d : d;
				int v = still && ad < near_y ? (int)row[x] - ((d * keep_y) >> 8) : (int)row[x];

				row[x] = (uint8_t)v;
				before[x] = (uint8_t)v;
			}
		}
		for (uint32_t yy = y0 / 2; yy < (y0 + RWCAM_NR_BLOCK) / 2 && yy < (height + 1) / 2; yy++) {
			uint8_t *row = uv + (size_t)yy * uvs, *before = buv + (size_t)yy * cw;

			for (uint32_t x = x0; x < x0 + RWCAM_NR_BLOCK && x < cw; x++) {
				int d = (int)row[x] - (int)before[x], ad = d < 0 ? -d : d;
				int v = still && ad < near_c ? (int)row[x] - ((d * keep_c) >> 8) : (int)row[x];

				row[x] = (uint8_t)v;
				before[x] = (uint8_t)v;
			}
		}
	}
}

static void
with_before(uint8_t *y, size_t ys, uint8_t *uv, size_t uvs, uint32_t width, uint32_t height, uint8_t *by, uint8_t *buv, int level)
{
	uint32_t bw = (width + RWCAM_NR_BLOCK - 1) / RWCAM_NR_BLOCK, bh = (height + RWCAM_NR_BLOCK - 1) / RWCAM_NR_BLOCK, cw = (width + 1) & ~1u;
	uint32_t n = bw * bh, moving = 0;
	uint8_t *diff = malloc(2 * (size_t)n), *moves = diff != NULL ? diff + n : NULL;
	uint64_t all = 0, calm = 0, calm_n = 0;
	int near_y = level == 2 ? 18 : 10, keep_y = level == 2 ? 128 : 90, near_c = level == 2 ? 14 : 8, keep_c = level == 2 ? 140 : 102, limit;

	if (diff == NULL) {
		return;
	}
	for (uint32_t b = 0; b < n; b++) {
		uint32_t x0 = b % bw * RWCAM_NR_BLOCK, y0 = b / bw * RWCAM_NR_BLOCK, sum = 0, count = 0;

		for (uint32_t yy = y0; yy < y0 + RWCAM_NR_BLOCK && yy < height; yy += 2) {
			const uint8_t *row = y + (size_t)yy * ys, *before = by + (size_t)yy * width;

			for (uint32_t x = x0; x < x0 + RWCAM_NR_BLOCK && x < width; x += 2) {
				int d = (int)row[x] - (int)before[x];

				sum += (uint32_t)(d < 0 ? -d : d);
				count++;
			}
		}
		sum = count ? sum * 4 / count : 0;      /* (in quarters of a level) */
		diff[b] = (uint8_t)(sum > 255 ? 255 : sum);
		all += diff[b];
	}
	/* the noise: the mean of the blocks that differ by less than twice the mean of all */
	for (uint32_t b = 0; b < n; b++) {
		if ((uint64_t)diff[b] * n <= 2 * all) {
			calm += diff[b];
			calm_n++;
		}
	}
	limit = (int)(RWCAM_NR_MOVES * (calm_n ? (double)calm / (double)calm_n : 0) + 0.5);
	limit = limit < 10 ? 10 : limit > 48 ? 48 : limit;      /* (2.5 to 12 levels) */
	{
		/* (in a block that is still, what a pixel differs by is noise up to three times the blocks' own: at least `near') */
		int noise3 = (int)(3.0 * (calm_n ? (double)calm / (double)calm_n : 0) / 4 + 0.5);

		noise3 = noise3 > 40 ? 40 : noise3;
		near_y = noise3 > near_y ? noise3 : near_y;
		near_c = noise3 * 3 / 4 > near_c ? noise3 * 3 / 4 : near_c;
	}
	for (uint32_t b = 0; b < n; b++) {
		moves[b] = diff[b] > limit;
		moving += moves[b];
	}
	{
		struct before_job j = { y, uv, by, buv, moves, ys, uvs, width, height, bw, bh, cw, moving, near_y, keep_y, near_c, keep_c };

		if (R.apply != NULL) {
			R.apply(BEFORE_PIECES, &j, before_piece);
		} else {
			for (size_t k = 0; k < BEFORE_PIECES; k++) {
				before_piece(&j, k);
			}
		}
	}
	free(diff);
}

