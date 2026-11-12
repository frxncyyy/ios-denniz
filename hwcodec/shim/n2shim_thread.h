/* SPDX-License-Identifier: MIT */
/*
 * n2shim_thread.h -- public side of n2shim_thread[] (package V32: guest threads and their synchronisation).
 *
 * n2shim_thread[] (declared in the frozen n2elf.h) implements the 50 pthread/sem/sched/signal imports of the
 * vendor set plus a few "extra" entry points the vendor code or the tests need (pthread_detach, cond_timedwait,
 * rwlock try/timed locks, sem try/timedwait, mutexattr type, ...).  Design notes are at the top of
 * n2shim_thread.c; the two things an integrator must know are:
 *
 *  1. A guest thread is a host thread with its own struct n2a32_cpu on the same struct n2a32_mem, started with
 *     n2a32_call().  The guest pthread_t is a table slot + 1, so 0 is never a valid thread id.  Threads created
 *     by the guest must be joined before the daemon destroys the n2a32_mem.
 *  2. Synchronisation objects (pthread_mutex_t/cond_t/rwlock_t/sem_t/pthread_once_t, 4..40 guest bytes) are
 *     host objects in a side table keyed by guest address, created on first use.  A never-initialised, all-zero
 *     object therefore behaves like PTHREAD_MUTEX_INITIALIZER / PTHREAD_COND_INITIALIZER, and *_destroy()
 *     removes the entry.  The guest bytes themselves are never read or written (they stay as the guest left
 *     them); copying such an object is not supported.
 *
 * Host-only: nothing in this package has run on the phone.  This is the shim's own header (not a frozen one);
 * the interface to the loader is n2shim_thread[] in include/n2elf.h and it does not change.
 */
#ifndef N2SHIM_THREAD_H
#define N2SHIM_THREAD_H

#include <stdint.h>

#include "n2a32.h"
#include "n2elf.h"

/* ---- first failure of a guest thread -------------------------------------------------------------------------
 * A guest thread started by pthread_create() that stops for a reason other than a normal return marks the
 * process as failed: its stop reason is recorded in a process-wide slot (first one wins), every other live
 * guest cpu gets stop_request set, and n2shim_thread_failure() reports the reason to the daemon.
 *
 * N2A32_STOP_REQUEST is not recorded: it means "this package asked the thread to stop" (failure propagation,
 * n2shim_thread_request_stop(), or the thread's own pthread_exit()), not that the guest code misbehaved.
 * A thread that calls pthread_exit() therefore does not mark a failure.  The main guest thread's abnormal stop
 * is not visible here (V32 does not own the run loop that called it) -- the daemon sees it as the return value
 * of its own n2a32_call(); n2shim_thread_note_stop() lets it record it in the same slot.
 */
int n2shim_thread_failure(void);                /* enum n2a32_stop of the first abnormal stop, 0 = none */
unsigned n2shim_thread_failure_thread(void);    /* guest pthread_t that failed, 0 = none */
int n2shim_thread_failure_detail(uint32_t *insn, uint32_t *addr);   /* stop_insn/stop_addr of that stop */
unsigned n2shim_thread_failure_count(void);     /* how many guest threads stopped abnormally */
void n2shim_thread_failure_clear(void);         /* tests only: forget the failure */

/* Record c->stop (if abnormal) in the same slot, exactly as a failing pthread_create() thread would.
 * The daemon calls this with the cpu of its own n2a32_call() when that call returns something other than
 * N2A32_STOP_RETURN.  Returns the recorded reason (0 = nothing recorded). */
int n2shim_thread_note_stop(struct n2a32_cpu *c);

/* Ask every live guest cpu to stop at its next instruction boundary (sets n2a32_cpu::stop_request).
 * Returns the number of cpus asked.  Guest threads woken by this stop with N2A32_STOP_REQUEST, which is not
 * a failure; use it for daemon shutdown. */
unsigned n2shim_thread_request_stop(void);

/* ---- introspection (tests and diagnostics) ------------------------------------------------------------------ */
unsigned n2shim_thread_live(void);          /* guest threads in RUNNING state (the initial thread counts once it used a shim) */
unsigned n2shim_thread_created(void);       /* successful pthread_create() calls */
unsigned n2shim_thread_objects(void);       /* live side-table objects (mutex/cond/rwlock/sem/once/attr) */
unsigned n2shim_thread_object_peak(void);
unsigned n2shim_thread_kind_clashes(void);  /* one guest address used for two object kinds without destroy */
unsigned n2shim_thread_nested_once(void);   /* pthread_once() re-entered on its own control (returns EDEADLK) */
unsigned n2shim_thread_stop_requests(void); /* cpus asked to stop because of a failure or request_stop() */
uint32_t n2shim_thread_self_id(void);       /* guest pthread_t of the calling host thread, 0 = never entered a shim */

#endif
