#ifndef __APPLE__
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "rwcam_loglimit.h"

struct shape {
	uint32_t hash;                  /* 0: free */
	uint32_t count;                 /* said in this window */
	uint32_t allowed;               /* may be said in this window */
	uint32_t window_ms;
	uint64_t since;
	uint64_t unsaid;
};

static struct {
	struct shape shapes[RWCAM_LOG_SHAPES];
	unsigned used;
	bool started;
	int64_t budget;
	uint64_t budget_at;
	uint64_t said, by_shape, by_budget, bytes, budget_unsaid;
} L;

static bool
is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool
is_hex(char c)
{
	return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* the line without its numbers: runs of digits (with a point or a sign among them) and 0x... addresses count as one '#' */
uint32_t
rwcam_loglimit_shape(const char *line)
{
	uint32_t h = 2166136261u;

	for (const char *p = line; *p; ) {
		char c = *p;

		if (c == '0' && (p[1] == 'x' || p[1] == 'X') && is_hex(p[2])) {
			for (p += 2; is_hex(*p); p++) {
			}
			c = '#';
		} else if (is_digit(c)) {
			while (is_digit(*p) || ((*p == '.' || *p == ',') && is_digit(p[1]))) {
				p++;
			}
			c = '#';
		} else {
			p++;
		}
		h = (h ^ (uint8_t)c) * 16777619u;
	}
	return h != 0 ? h : 1;
}

static struct shape *
shape_of(uint32_t hash)
{
	struct shape *oldest = NULL;

	/* (a place among sixteen: its own, a free one, or -- all sixteen taken by others -- the one not seen for the longest, which starts anew) */
	for (unsigned i = 0, at = hash % RWCAM_LOG_SHAPES; i < 16; i++, at = (at + 1) % RWCAM_LOG_SHAPES) {
		struct shape *s = &L.shapes[at];

		if (s->hash == hash) {
			return s;
		}
		if (s->hash == 0) {
			L.used++;
			s->hash = hash;
			return s;
		}
		oldest = oldest == NULL || s->since < oldest->since ? s : oldest;
	}
	memset(oldest, 0, sizeof(*oldest));
	oldest->hash = hash;
	return oldest;
}

bool
rwcam_loglimit(const char *line, size_t bytes, uint64_t now_ms, bool keep, char *note, size_t note_size)
{
	struct shape *s = NULL;
	uint64_t unsaid = 0;
	int n = 0;

	if (note_size != 0) {
		note[0] = 0;
	}
	if (!L.started) {
		L.started = true;
		L.budget = RWCAM_LOG_BUDGET;
		L.budget_at = now_ms;
	}
	if (now_ms > L.budget_at) {
		uint64_t more = (now_ms - L.budget_at) * RWCAM_LOG_REFILL / 1000;

		if (more != 0) {
			L.budget = L.budget + (int64_t)more > RWCAM_LOG_BUDGET ? RWCAM_LOG_BUDGET : L.budget + (int64_t)more;
			L.budget_at = now_ms;
		}
	}
	if (!keep && (s = shape_of(rwcam_loglimit_shape(line))) != NULL) {
		if (s->allowed == 0) {
			s->allowed = RWCAM_LOG_FIRST;
			s->window_ms = RWCAM_LOG_WINDOW_MS;
			s->since = now_ms;
		} else if (now_ms - s->since >= s->window_ms) {
			if (s->unsaid != 0) {
				/* they keep coming: one line for the next window, twice as long */
				s->allowed = 1;
				s->window_ms = s->window_ms * 2 > RWCAM_LOG_WINDOW_MAX_MS ? RWCAM_LOG_WINDOW_MAX_MS : s->window_ms * 2;
			} else {
				s->allowed = RWCAM_LOG_FIRST;
				s->window_ms = RWCAM_LOG_WINDOW_MS;
			}
			s->since = now_ms;
			s->count = 0;
		}
		if (s->count >= s->allowed) {
			s->unsaid++;
			L.by_shape++;
			return false;
		}
	}
	if (L.budget < (int64_t)bytes + (keep ? 0 : RWCAM_LOG_RESERVE)) {
		L.by_budget++;
		L.budget_unsaid++;
		return false;
	}
	if (s != NULL) {
		s->count++;
		unsaid = s->unsaid;
		s->unsaid = 0;
	}
	if (unsaid != 0 && note_size != 0) {
		n = snprintf(note, note_size, " [+%llu like it not said]", (unsigned long long)unsaid);
	}
	if (L.budget_unsaid != 0 && n >= 0 && (size_t)n < note_size) {
		n += snprintf(note + n, note_size - (size_t)n, " [%llu lines over the log's budget not said]", (unsigned long long)L.budget_unsaid);
		L.budget_unsaid = 0;
	}
	bytes += n > 0 ? (size_t)n : 0;
	L.budget -= (int64_t)bytes;
	L.bytes += bytes;
	L.said++;
	return true;
}

void
rwcam_loglimit_totals(uint64_t *said, uint64_t *by_shape, uint64_t *by_budget, uint64_t *bytes)
{
	*said = L.said;
	*by_shape = L.by_shape;
	*by_budget = L.by_budget;
	*bytes = L.bytes;
}

void
rwcam_loglimit_reset(void)
{
	memset(&L, 0, sizeof(L));
}
