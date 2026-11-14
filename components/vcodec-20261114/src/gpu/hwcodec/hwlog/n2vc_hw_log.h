/* SPDX-License-Identifier: MIT */
/*
 * n2vc_hw_log.h -- X01: log every call made through the frozen struct n2vc_hw.
 *
 * This is a decorator, not a back end. n2vc_hw_wrap_log() copies `inner` into
 * a private state, fills `out` with forwarding callbacks and returns 0. The
 * caller uses `out` exactly like `inner`; every result and out-parameter is
 * forwarded unchanged. The only side effect is a text log.
 *
 * Log line syntax: T02's recorder syntax (tests/hw/README.md) preceded by a
 * decimal microsecond timestamp and followed by the result, one line per
 * operation (one line per register op inside a batch):
 *
 *   123456 POWER VENC on -> 0
 *   123457 W VENC 0x0010 0x00000001 -> 0
 *   123458 W VENC 0x0010 0x00000003 mask 0x0000000f -> 0
 *   123459 R VENC 0x0010 0x00000001 -> 0
 *   123460 POLL VENC 0x0010 0x00000001 mask 0x00000001 -> ok
 *   123461 DELAY 100 us -> 0
 *   123462 IRQ wait 0 timeout_ms=100 -> 0 status=0x00000001
 *          (status=none when the caller passed a NULL status pointer)
 *   123463 IRQ clear 0 -> 0
 *   123464 DMA alloc handle=1 pa=0xda800000 bytes=4096 flags=0x0 -> 0
 *   123465 DMA free handle=1 -> 0
 *   123466 DMA rw handle=1 off=0x0000 bytes=16 W crc32=0x... -> 0
 *
 * Two values T02's own recorder prints are not observable through the frozen
 * callback and are therefore not logged: the read count of a POLL (only
 * ok/timeout is available) and the page-rounded size of a DMA allocation
 * (only the requested size is available). dma_rw logs CRC-32 (IEEE 802.3,
 * polynomial 0xedb88320, init/xorout 0xffffffff) of the host buffer, never the
 * data. A read whose backend call failed has no value and is logged with '?';
 * the op at index *done of a failed batch carries the error result. A NULL
 * out_done is forwarded as NULL; a failed batch then logs only its result
 * because the failing index is not observable.
 *
 * Buffering: stdio buffered; fflush+fsync+fclose on close(). Size: at most
 * N2HW_HWLOG_MAX_MB (default 16) mebibytes.
 * When the next line would exceed that, logging stops; close() writes one
 * final line
 *
 *   123467 HWLOG truncated limit_bytes=16777216 bytes=... dropped=...
 *
 * which is the last line of the file. The file can exceed the limit only by
 * that final line. Log I/O errors never change a forwarded return value.
 *
 * Threads: all log writes take an internal mutex. The forwarded call itself is
 * not serialised, so lines are in completion order. close() must not race with
 * a call, like every other n2vc_hw implementation.
 *
 * Failure of n2vc_hw_wrap_log: -EINVAL for a bad argument or a back end with a
 * missing mandatory callback; -ENOMEM; -errno from fopen(). `close` in `inner`
 * may be NULL; in that case the wrapper only closes the log.
 */
#ifndef N2VC_HW_LOG_H
#define N2VC_HW_LOG_H

#include <stdint.h>

#include "n2vc_hw.h"

#ifndef N2HW_HWLOG_MAX_MB
#define N2HW_HWLOG_MAX_MB 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wrap `inner` as `out`, logging to `path` (fopen "w", truncated per session).
 * `out` and `inner` may point to the same struct. Returns 0 or -errno.
 */
int n2vc_hw_wrap_log(struct n2vc_hw *out, const struct n2vc_hw *inner, const char *path);

/*
 * Test seam, not needed by vcodecd: use clock_us(arg) for log timestamps.
 * Call before n2vc_hw_wrap_log(); the choice is captured by each wrapper.
 * NULL restores CLOCK_MONOTONIC. This global write is not synchronised with
 * running wrappers; set it up before starting threads.
 */
void n2vc_hw_log_test_set_clock(uint64_t (*clock_us)(void *arg), void *arg);

#ifdef __cplusplus
}
#endif

#endif /* N2VC_HW_LOG_H */
