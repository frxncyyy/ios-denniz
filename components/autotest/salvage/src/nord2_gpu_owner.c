/* SPDX-License-Identifier: MIT */
#include "nord2_gpu_owner.h"
enum { IDLE, PREPARING, QUEUED, RUNNING, DONE };
static bool change(unsigned *p, unsigned old, unsigned next)
{
        return __atomic_compare_exchange_n(p, &old, next, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
bool n2owner_ready(const struct n2owner *q)
{
        return q && __atomic_load_n(&q->ready, __ATOMIC_ACQUIRE);
}
void n2owner_poll(struct n2owner *q)
{
        if (!q) return;
        __atomic_store_n(&q->ready, 1, __ATOMIC_RELEASE);
        if (!change(&q->state, QUEUED, RUNNING)) return;
        q->result = q->fn(q->arg);
        __atomic_store_n(&q->state, DONE, __ATOMIC_RELEASE);
}
bool n2owner_call(struct n2owner *q, int (*fn)(void *), void *arg,
                  const struct n2owner_wait *w, unsigned queue_ms, int *result)
{
        if (!q || !fn || !w || !w->now_ms || !w->sleep_ms || !result ||
            !n2owner_ready(q) || !change(&q->state, IDLE, PREPARING)) return false;
        q->fn = fn;
        q->arg = arg;
        uint64_t started = w->now_ms(w->ctx);
        __atomic_store_n(&q->state, QUEUED, __ATOMIC_RELEASE);
        for (;;) {
                if (__atomic_load_n(&q->state, __ATOMIC_ACQUIRE) == DONE) {
                        *result = q->result;
                        q->fn = 0;
                        q->arg = 0;
                        __atomic_store_n(&q->state, IDLE, __ATOMIC_RELEASE);
                        return true;
                }
                if (w->now_ms(w->ctx) - started >= queue_ms &&
                    change(&q->state, QUEUED, PREPARING)) {
                        q->fn = 0;
                        q->arg = 0;
                        __atomic_store_n(&q->state, IDLE, __ATOMIC_RELEASE);
                        return false;
                }
                w->sleep_ms(w->ctx, 1);
        }
}
