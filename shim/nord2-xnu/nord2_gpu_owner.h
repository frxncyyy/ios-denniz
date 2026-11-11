/* Serialize GPU rail transactions with the existing PWRAP owner thread.
 * SPDX-License-Identifier: MIT */
#ifndef NORD2_GPU_OWNER_H
#define NORD2_GPU_OWNER_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct n2owner {
        unsigned state, ready;
        int (*fn)(void *);
        void *arg;
        int result;
};
struct n2owner_wait {
        uint64_t (*now_ms)(void *);
        void (*sleep_ms)(void *, unsigned);
        void *ctx;
};
/* Exactly one owner thread calls poll. The callback executes on that thread.
 * A request can be cancelled before execution; once executing, the caller
 * retains its argument until completion even past the queue deadline. */
void n2owner_poll(struct n2owner *q);
bool n2owner_ready(const struct n2owner *q);
bool n2owner_call(struct n2owner *q, int (*fn)(void *), void *arg,
                  const struct n2owner_wait *wait, unsigned queue_ms,
                  int *result);
#ifdef __cplusplus
}
#endif
#endif
