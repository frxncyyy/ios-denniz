/*
 * How much the camera code says: the kernel's console keeps the first 4 MB after the start and a session restarted in a loop filled them in two
 * minutes (run q10hl: 833 KB of it this bundle's, 989 times the same five lines of CMCapture's log). Every line goes through here first:
 *   - lines of one shape (the line with its numbers and addresses taken out) are said RWCAM_LOG_FIRST times; after that one line of that shape for
 *     each window of time, with the count of those not said, the window doubling (30 s, 60 s, ... 10 min) while they keep coming; a shape not seen
 *     for a whole window starts again;
 *   - all lines together draw on a budget of bytes (RWCAM_LOG_BUDGET at first, RWCAM_LOG_REFILL more each second, never more than the first):
 *     750 KB at most in the first five minutes, whatever happens.
 * A line marked `keep' (the summaries every few seconds, which are the same shape with other numbers and are what a run is read by) is not
 * counted by shape and may use the budget's last RWCAM_LOG_RESERVE bytes, which other lines may not.
 * No Apple types, no clock of its own: built unchanged into the bundle (rwcam_log.c) and the host test (test/t_log.c). Not locked: the caller's.
 */
#ifndef RWCAM_LOGLIMIT_H
#define RWCAM_LOGLIMIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RWCAM_LOG_FIRST         3
#define RWCAM_LOG_WINDOW_MS     30000u
#define RWCAM_LOG_WINDOW_MAX_MS 600000u
#define RWCAM_LOG_BUDGET        300000
#define RWCAM_LOG_REFILL        1500            /* bytes a second */
#define RWCAM_LOG_RESERVE       48000
#define RWCAM_LOG_SHAPES        4096            /* (run q10ht: 512 were full after the start, and what came after was held back by the budget alone) */

/*
 * true: say the line, followed by note (empty, or " [+N like it not said]" / " [N lines over the log's budget not said]"); false: not said (counted).
 * bytes: the line's length as it would be written.
 */
bool rwcam_loglimit(const char *line, size_t bytes, uint64_t now_ms, bool keep, char *note, size_t note_size);
void rwcam_loglimit_totals(uint64_t *said, uint64_t *by_shape, uint64_t *by_budget, uint64_t *bytes);
void rwcam_loglimit_reset(void);
uint32_t rwcam_loglimit_shape(const char *line);

#endif
