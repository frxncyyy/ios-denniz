#include <math.h>
#include <string.h>
#include "rwcam_align.h"

#define GW 32
#define GH 24

static float
sample(const uint8_t *p, float x, float y)
{
	/* x, y: 0 to 1 over the picture; bilinear */
	float fx = x * (RWCAM_ALIGN_W - 1), fy = y * (RWCAM_ALIGN_H - 1);
	int ix, iy;

	fx = fx < 0 ? 0 : fx > RWCAM_ALIGN_W - 1.001f ? RWCAM_ALIGN_W - 1.001f : fx;
	fy = fy < 0 ? 0 : fy > RWCAM_ALIGN_H - 1.001f ? RWCAM_ALIGN_H - 1.001f : fy;
	ix = (int)fx;
	iy = (int)fy;
	fx -= (float)ix;
	fy -= (float)iy;
	const uint8_t *q = p + iy * RWCAM_ALIGN_W + ix;

	return (q[0] * (1 - fx) + q[1] * fx) * (1 - fy) + (q[RWCAM_ALIGN_W] * (1 - fx) + q[RWCAM_ALIGN_W + 1] * fx) * fy;
}

struct ref {
	float v[GW * GH];
	float sum, sq;
};

static float
score_at(const struct ref *w, const uint8_t *ultra, float ratio, float dx, float dy)
{
	double su = 0, suu = 0, swu = 0;
	const float n = GW * GH;

	/* (the window has to be inside the ultra-wide picture) */
	if (0.5f + dx - 0.5f / ratio < 0 || 0.5f + dx + 0.5f / ratio > 1 || 0.5f + dy - 0.5f / ratio < 0 || 0.5f + dy + 0.5f / ratio > 1) {
		return -2;
	}
	for (int gy = 0; gy < GH; gy++) {
		float yw = ((float)gy + 0.5f) / GH, yu = 0.5f + dy + (yw - 0.5f) / ratio;

		for (int gx = 0; gx < GW; gx++) {
			float xw = ((float)gx + 0.5f) / GW, u = sample(ultra, 0.5f + dx + (xw - 0.5f) / ratio, yu);

			su += u;
			suu += (double)u * u;
			swu += (double)w->v[gy * GW + gx] * u;
		}
	}
	double vw = w->sq - (double)w->sum * w->sum / n, vu = suu - su * su / n, c = swu - (double)w->sum * su / n;

	if (vw < n * 4 || vu < n * 4) {
		return -2;                      /* (a picture with nothing in it: no place is better than another) */
	}
	return (float)(c / __builtin_sqrt(vw * vu));
}

void
rwcam_align(const uint8_t *wide, const uint8_t *ultra, struct rwcam_alignment *a)
{
	static const float r0 = 1.2f, r1 = 2.3f, d = 0.15f;
	struct ref w;
	float best = -2, br = 0, bx = 0, by = 0, second = -2;

	memset(a, 0, sizeof(*a));
	w.sum = w.sq = 0;
	for (int gy = 0; gy < GH; gy++) {
		for (int gx = 0; gx < GW; gx++) {
			float v = sample(wide, ((float)gx + 0.5f) / GW, ((float)gy + 0.5f) / GH);

			w.v[gy * GW + gx] = v;
			w.sum += v;
			w.sq += v * v;
		}
	}
	for (float r = r0; r <= r1 + 0.001f; r += 0.05f) {
		for (float y = -d; y <= d + 0.001f; y += 0.03f) {
			for (float x = -d; x <= d + 0.001f; x += 0.03f) {
				float s = score_at(&w, ultra, r, x, y);

				if (s > best) {
					best = s;
					br = r;
					bx = x;
					by = y;
				}
			}
		}
	}
	if (best < -1) {
		return;
	}
	/* the best of the places that are not the best one's neighbours */
	for (float r = r0; r <= r1 + 0.001f; r += 0.05f) {
		for (float y = -d; y <= d + 0.001f; y += 0.03f) {
			for (float x = -d; x <= d + 0.001f; x += 0.03f) {
				if (__builtin_fabsf(r - br) > 0.16f || __builtin_fabsf(x - bx) > 0.07f || __builtin_fabsf(y - by) > 0.07f) {
					float s = score_at(&w, ultra, r, x, y);

					second = s > second ? s : second;
				}
			}
		}
	}
	/* more finely around it, twice */
	for (int pass = 0; pass < 2; pass++) {
		float sr = pass ? 0.004f : 0.0125f, sd = pass ? 0.002f : 0.0075f, cr = br, cx = bx, cy = by;

		for (int i = -4; i <= 4; i++) {
			for (int j = -4; j <= 4; j++) {
				for (int k = -4; k <= 4; k++) {
					float s = score_at(&w, ultra, cr + (float)i * sr, cx + (float)j * sd, cy + (float)k * sd);

					if (s > best) {
						best = s;
						br = cr + (float)i * sr;
						bx = cx + (float)j * sd;
						by = cy + (float)k * sd;
					}
				}
			}
		}
	}
	a->ratio = br;
	a->dx = bx;
	a->dy = by;
	a->score = best;
	a->second = second;
	a->ok = best >= 0.6f && best - second >= 0.06f;
}

static float
middle(float *v, unsigned n)
{
	for (unsigned i = 1; i < n; i++) {
		float x = v[i];
		unsigned j = i;

		for (; j > 0 && v[j - 1] > x; j--) {
			v[j] = v[j - 1];
		}
		v[j] = x;
	}
	return n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

bool
rwcam_align_add(struct rwcam_align_filter *f, const struct rwcam_alignment *found, struct rwcam_alignment *now)
{
	float r[RWCAM_ALIGN_KEPT], x[RWCAM_ALIGN_KEPT], y[RWCAM_ALIGN_KEPT];
	unsigned agree = 0;

	if (found != NULL && found->ok) {
		f->kept[f->next] = *found;
		f->next = (f->next + 1) % RWCAM_ALIGN_KEPT;
		f->n = f->n < RWCAM_ALIGN_KEPT ? f->n + 1 : f->n;
	}
	if (f->n < 2) {
		return false;
	}
	for (unsigned i = 0; i < f->n; i++) {
		r[i] = f->kept[i].ratio;
		x[i] = f->kept[i].dx;
		y[i] = f->kept[i].dy;
	}
	memset(now, 0, sizeof(*now));
	now->ratio = middle(r, f->n);
	now->dx = middle(x, f->n);
	now->dy = middle(y, f->n);
	for (unsigned i = 0; i < f->n; i++) {
		agree += __builtin_fabsf(f->kept[i].ratio - now->ratio) < 0.04f && __builtin_fabsf(f->kept[i].dx - now->dx) < 0.02f && __builtin_fabsf(f->kept[i].dy - now->dy) < 0.02f;
		now->score += f->kept[i].score / (float)f->n;
	}
	now->ok = agree * 2 > f->n;
	return now->ok;
}
