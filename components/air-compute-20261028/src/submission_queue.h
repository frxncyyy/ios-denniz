/* Intrusive FIFO for native GPU submissions. Backend schedule() must enqueue
 * the callback on target; retain/release own a target reference. A null target
 * runs inline once earlier submissions finish. Operations are fixed before use.
 *
 * Each job's owner must keep this queue alive through run/dispose. dispose may
 * release the last queue reference only when no other jobs remain. The worker
 * never touches a completed job, or an empty queue, after dispose returns.
 * No allocations, shader execution, or completion-status policy live here.
 */
#ifndef TEGU_SUBMISSION_QUEUE_H
#define TEGU_SUBMISSION_QUEUE_H
#include <stddef.h>

struct tegu_submission_job {
    struct tegu_submission_job *next;
    void *target;
    void *context;
    void (*run)(struct tegu_submission_job *);
    void (*dispose)(struct tegu_submission_job *);
};
struct tegu_submission_ops {
    void (*retain)(void *);
    void (*release)(void *);
    void (*schedule)(void *, void *, void (*)(void *));
};
struct tegu_submission_queue {
    unsigned lock, active;
    void *target;
    struct tegu_submission_job *head, *tail;
    struct tegu_submission_ops ops;
};
static inline void tegu_submission_lock(struct tegu_submission_queue *q)
{
    while (__atomic_exchange_n(&q->lock, 1, __ATOMIC_ACQUIRE)) {}
}
static inline void tegu_submission_unlock(struct tegu_submission_queue *q)
{
    __atomic_store_n(&q->lock, 0, __ATOMIC_RELEASE);
}
/* Non-null targets require a complete backend. Failure leaves state unchanged.
 * Previously committed jobs retain their captured target across replacement. */
static inline int tegu_submission_set_target(struct tegu_submission_queue *q,
                                              void *target)
{
    if (target && (!q->ops.retain || !q->ops.release || !q->ops.schedule)) return 0;
    if (target) q->ops.retain(target);
    tegu_submission_lock(q);
    void *previous = q->target;
    q->target = target;
    tegu_submission_unlock(q);
    if (previous) q->ops.release(previous);
    return 1;
}
static void tegu_submission_drain(void *context);
static inline void tegu_submission_start(struct tegu_submission_queue *q,
                                         struct tegu_submission_job *job)
{
    if (job->target) q->ops.schedule(job->target, q, tegu_submission_drain);
    else tegu_submission_drain(q);
}
/* Caller retains the command/resources before enqueue and must never enqueue
 * the same job twice. Command commit's state transition enforces that contract. */
static inline void tegu_submission_enqueue(struct tegu_submission_queue *q,
                                           struct tegu_submission_job *job)
{
    tegu_submission_lock(q);
    job->next = NULL;
    job->target = q->target;
    if (job->target) q->ops.retain(job->target);
    if (q->tail) q->tail->next = job;
    else q->head = job;
    q->tail = job;
    unsigned start = !q->active;
    q->active = 1;
    tegu_submission_unlock(q);
    if (start) tegu_submission_start(q, job);
}
static void tegu_submission_drain(void *context)
{
    struct tegu_submission_queue *q = context;
    for (;;) {
        tegu_submission_lock(q);
        struct tegu_submission_job *job = q->head;
        tegu_submission_unlock(q);
        job->run(job);
        tegu_submission_lock(q);
        struct tegu_submission_job *next = job->next;
        q->head = next;
        if (!next) { q->tail = NULL; q->active = 0; }
        tegu_submission_unlock(q);
        if (job->target) q->ops.release(job->target);
        job->dispose(job);
        if (!next) return;
        /* Next job owns the queue lifetime. Iterate null-target jobs rather
         * than recursing when a large backlog follows a target reset. */
        if (next->target) { tegu_submission_start(q, next); return; }
    }
}
#endif
