#ifndef NORD2_CONFIGURED_CLOCK_H
#define NORD2_CONFIGURED_CLOCK_H
#include <stdint.h>
#define N2CLK_WORDS 10u
#define N2CLK_PASSES 3u
/* Register configuration telemetry only, never execution-frequency measurement. */
enum n2clk_status { N2CLK_OK, N2CLK_READ_ERROR, N2CLK_UNSTABLE,
 N2CLK_NON_ARMPLL, N2CLK_UNKNOWN_DIVIDER, N2CLK_UNKNOWN_POSTDIV, N2CLK_ZERO_PCW,
 N2CLK_BAD_ARGUMENT };
struct n2clk_decoded {
 uint32_t con1,divider_word,pcw,postdiv_code,divider_code,mux;
 uint64_t configured_khz; /* zero unless status=OK */
 enum n2clk_status status;
};
struct n2clk_capture {
 uint32_t words[N2CLK_PASSES][N2CLK_WORDS],valid[N2CLK_PASSES];
 int read_result[N2CLK_PASSES][N2CLK_WORDS];
 unsigned attempted;
 enum n2clk_status status;
 struct n2clk_decoded pairs[5];
};
extern const uint64_t n2clk_read_allowlist[N2CLK_WORDS];
/* Caller supplies only a read callback: no write/enable/clock-vote callback.
 * Return0 and initialize *value on success; nonzero stops capture, no retry.
 * A callback/MMIO hang cannot be bounded by this module. */
typedef int (*n2clk_read_fn)(void *,uint64_t,uint32_t *);
enum n2clk_status n2clk_decode(uint32_t,uint32_t,struct n2clk_decoded *);
enum n2clk_status n2clk_collect(n2clk_read_fn,void *,struct n2clk_capture *);
#endif
