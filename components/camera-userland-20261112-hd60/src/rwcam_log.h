/* Logging for the camera code: stderr, and /dev/console when /var/db/nord2-camera-console exists (kern.redwood.say is not in this kernel). */
#ifndef RWCAM_LOG_H
#define RWCAM_LOG_H
void rwcam_say(const char *format, ...) __attribute__((format(printf, 1, 2)));
/*
 * rwcam_say: a line of one shape (its numbers and addresses apart) three times, then counted (rwcam_loglimit.h), and all lines within a budget of bytes.
 * rwcam_say_keep: the periodic summaries -- not counted by shape, and the last to go without when the budget is used up.
 */
void rwcam_say_keep(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* one line per distinct (what, name) pair */
void rwcam_say_once(const char *what, const char *name, const char *detail);
/* where the dyld shared cache was slid to (0 if unknown); log addresses minus this are the unslid ones */
unsigned long redwood_cache_slide(void);
#endif
