/* The log's limits (src/rwcam_loglimit.c): a session restarted twice a second for five minutes, as in run q10hl, and what is still said then. */
#include <stdio.h>
#include <string.h>
#include "rwcam_loglimit.h"

static int failed;
#define CHECK(c, ...) do { if (!(c)) { failed++; printf("FAILED %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t said_bytes, said_lines;
static char last_note[128];

static bool
say(uint64_t now, bool keep, const char *format, unsigned long a, unsigned long b)
{
	char line[400], note[128];
	int n = snprintf(line, sizeof(line), format, a, b);
	bool go = rwcam_loglimit(line, (size_t)n + 1, now, keep, note, sizeof(note));

	if (go) {
		said_bytes += (size_t)n + 1 + strlen(note);
		said_lines++;
		strcpy(last_note, note);
	}
	return go;
}

int
main(void)
{
	uint64_t s, shape, budget, bytes;
	unsigned loop_said = 0, summaries = 0, others = 0;

	/* the shape: numbers and addresses do not count, words do */
	CHECK(rwcam_loglimit_shape("stream 0x1023824e0: frame 12 at 3.25 s") == rwcam_loglimit_shape("stream 0x105026c00: frame 99812 at 1234.5 s"), "shape of numbers");
	CHECK(rwcam_loglimit_shape("stream: read FocalLength") != rwcam_loglimit_shape("stream: read LensFNumber"), "shape of words");
	CHECK(rwcam_loglimit_shape("set X = 1") == rwcam_loglimit_shape("set X = 250"), "shape of a value");

	/* five minutes of the loop: every 500 ms the same 20 lines (other addresses each time), a summary every 5 s, and a new line now and then */
	rwcam_loglimit_reset();
	for (uint64_t t = 0; t < 300000; t += 500) {
		for (unsigned i = 0; i < 20; i++) {
			char format[200];

			snprintf(format, sizeof(format), "redwood: camera: log 0: <<<< FigCaptureSession >>>> captureSession_step%c: <%%#lx[%%lu][com.apple.camera]> "
			    "something that is said at every turn of the loop, about this long, with words in it", (int)('A' + i));
			loop_said += say(t + i, false, format, 0x10300000ul + t, t);
		}
		if (t % 5000 == 0) {
			bool go = say(t + 30, true, "redwood: camera: PrimaryScaler: 5.0 s: %lu pictures given, mean luma %lu, and the rest of a summary of this "
			    "length, which is what a run is read by, so that it has to be there every time whatever else is said", t / 33, t % 255);

			CHECK(go, "summary at %llu ms not said", (unsigned long long)t);
			summaries += go;
		}
		if (t % 7000 == 0) {
			char format[120];

			snprintf(format, sizeof(format), "redwood: camera: stream: set (not used) Key%c%c = %%lu (%%lu)", (int)('A' + t / 7000 % 26), (int)('a' + t / 7000 / 26));
			bool go = say(t + 40, false, format, t, t);

			CHECK(go, "a new line at %llu ms not said", (unsigned long long)t);
			others += go;
		}
	}
	rwcam_loglimit_totals(&s, &shape, &budget, &bytes);
	printf("   five minutes of a restart loop: %u of %u loop lines said, %u summaries, %u new lines; %llu bytes; %llu held back by shape, %llu by budget\n",
	    loop_said, 600 * 20, summaries, others, (unsigned long long)bytes, (unsigned long long)shape, (unsigned long long)budget);
	CHECK(bytes == said_bytes, "bytes counted %llu, said %llu", (unsigned long long)bytes, (unsigned long long)said_bytes);
	CHECK(bytes < 100000, "the loop took %llu bytes of the log", (unsigned long long)bytes);
	CHECK(loop_said >= 20 * 3 && loop_said <= 20 * 8, "loop lines said: %u", loop_said);
	CHECK(budget == 0, "budget used up");

	/* the three first, then one a window with the count */
	rwcam_loglimit_reset();
	said_lines = 0;
	for (uint64_t t = 0; t < 29000; t += 100) {
		(void)say(t, false, "device: streams given back (%lu) %lu", 2, t);
	}
	CHECK(said_lines == 3, "first window: %llu said", (unsigned long long)said_lines);
	CHECK(say(30000, false, "device: streams given back (%lu) %lu", 2, 1), "next window's line");
	CHECK(strstr(last_note, "+287 like it not said") != NULL, "note: '%s'", last_note);
	CHECK(!say(30100, false, "device: streams given back (%lu) %lu", 2, 1), "only one in the second window");
	CHECK(!say(89000, false, "device: streams given back (%lu) %lu", 2, 1), "the second window is 60 s");
	CHECK(say(90000, false, "device: streams given back (%lu) %lu", 2, 1), "third window's line");
	/* quiet for a whole window: as new */
	CHECK(!say(100000, false, "device: streams given back (%lu) %lu", 2, 1), "inside the third window");
	CHECK(say(210000, false, "device: streams given back (%lu) %lu", 2, 1) && strstr(last_note, "+1 like it") != NULL, "fourth window, note '%s'", last_note);
	CHECK(say(700000, false, "device: streams given back (%lu) %lu", 2, 1) && last_note[0] == 0, "after a quiet window");
	CHECK(say(700001, false, "device: streams given back (%lu) %lu", 2, 1) && say(700002, false, "device: streams given back (%lu) %lu", 2, 1) &&
	    !say(700003, false, "device: streams given back (%lu) %lu", 2, 1), "three again");

	/* the budget: lines all of different shapes, 2000 a second -- never more than the budget and its refill, the summaries last to go without */
	rwcam_loglimit_reset();
	said_bytes = 0;
	unsigned kept = 0, kept_said = 0;
	for (uint64_t t = 0; t < 60000; t++) {
		char format[200];

		for (unsigned k = 0; k < 2; k++) {
			unsigned long v = (unsigned long)(t * 2 + k);
			snprintf(format, sizeof(format), "a line %c%c%c%c%c of words only, each another shape, and long enough to count: lorem ipsum dolor sit amet %%lu %%lu",
			    (int)('a' + v % 26), (int)('a' + v / 26 % 26), (int)('a' + v / 676 % 26), (int)('a' + v / 17576 % 26), (int)('a' + v / 456976 % 26));
			(void)say(t, false, format, 1, 2);
		}
		if (t % 5000 == 4999) {
			kept++;
			kept_said += say(t, true, "summary %lu %lu: the numbers of five seconds, in a line of about two hundred bytes as the real ones are, more or less, "
			    "so that the reserve is drawn on at the rate it would be", t, t);
		}
	}
	rwcam_loglimit_totals(&s, &shape, &budget, &bytes);
	printf("   a minute of 2000 different lines a second: %llu bytes said (budget %d + %d a second), %llu lines held back; %u of %u summaries said\n",
	    (unsigned long long)bytes, RWCAM_LOG_BUDGET, RWCAM_LOG_REFILL, (unsigned long long)budget, kept_said, kept);
	CHECK(bytes <= RWCAM_LOG_BUDGET + 60 * RWCAM_LOG_REFILL, "over the budget: %llu", (unsigned long long)bytes);
	CHECK(kept_said == kept, "summaries held back: %u of %u said", kept_said, kept);
	CHECK(budget > 100000, "nothing held back by the budget");
	/* more shapes than there are places (run q10ht: 549 lines of one shape after the table was full): a line that repeats is still held back */
	rwcam_loglimit_reset();
	said_lines = 0;
	for (unsigned v = 0; v < 3 * RWCAM_LOG_SHAPES; v++) {
		char format[80];

		snprintf(format, sizeof(format), "s %c%c%c%c %%lu %%lu", (int)('a' + v % 26), (int)('a' + v / 26 % 26), (int)('a' + v / 676 % 26), (int)('a' + v / 17576 % 26));
		(void)say(1000 + v / 8, true, format, 1, 2);     /* (kept: no budget for these, they are here to fill the table) */
		(void)rwcam_loglimit(format, 1, 1000 + v / 8, false, last_note, sizeof(last_note));
	}
	said_lines = 0;
	for (int i = 0; i < 500; i++) {
		(void)say(4000 + (uint64_t)i * 33, false, "diag: PROPERTY ERROR -%lu: setProperty AFWindowParams of <stream %#lx>", 12787, 0x1000ul + (unsigned long)i);
	}
	CHECK(said_lines == 3, "a repeated line with the table full: %llu said", (unsigned long long)said_lines);
	printf(failed ? "t_log: %d FAILED\n" : "t_log: all passed\n", failed);
	return failed != 0;
}
