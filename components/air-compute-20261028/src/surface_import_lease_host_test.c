#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include "TeguSurfaceGPUImportCandidate.h"

#define LIMIT 100u
static _Atomic uint64_t gpu_wired;
static pthread_mutex_t surface_lock = PTHREAD_MUTEX_INITIALIZER;
struct surface { uint32_t refs; uint64_t charged; uint64_t bytes; int custom; };

static int reserve_gpu(uint64_t bytes)
{
    uint64_t current=atomic_load_explicit(&gpu_wired,memory_order_relaxed);
    for (;;) {
        if (!bytes || current>LIMIT || bytes>LIMIT-current) return 0;
        if (atomic_compare_exchange_weak_explicit(&gpu_wired,&current,current+bytes,
            memory_order_acq_rel,memory_order_relaxed)) return 1;
    }
}
static void unreserve_gpu(uint64_t bytes)
{
    if (bytes) {
        uint64_t old=atomic_fetch_sub_explicit(&gpu_wired,bytes,memory_order_acq_rel);
        assert(old>=bytes);
    }
}
static int pin(struct surface *s)
{
    uint32_t next_refs=0; uint64_t next_charge=0,reserve=0;
    pthread_mutex_lock(&surface_lock);
    int ok=tegu_surface_gpu_import_lease_acquire(s->refs,s->charged,s->bytes,
        s->custom,&next_refs,&next_charge,&reserve);
    if (ok && reserve && !reserve_gpu(reserve)) ok=0;
    if (ok) { s->refs=next_refs; s->charged=next_charge; }
    pthread_mutex_unlock(&surface_lock);
    return ok;
}
static int import_after_validation(struct surface *s, int layout_ok, int all_pages_ok)
{
    if (!layout_ok || !all_pages_ok) return 0;
    return pin(s);
}
static void unpin(struct surface *s)
{
    uint32_t next_refs=0; uint64_t release=0;
    pthread_mutex_lock(&surface_lock);
    assert(tegu_surface_gpu_import_lease_release(s->refs,s->charged,
        &next_refs,&release));
    s->refs=next_refs;
    if (release) s->charged=0;
    unreserve_gpu(release);
    pthread_mutex_unlock(&surface_lock);
}
struct thread_arg { struct surface *s; unsigned loops; };
static void *worker(void *opaque)
{
    struct thread_arg *a=opaque;
    for (unsigned i=0;i<a->loops;i++) { assert(pin(a->s)); unpin(a->s); }
    return NULL;
}
struct race_arg { struct surface *s; int accepted; };
static void *race_pin(void *opaque)
{
    struct race_arg *a=opaque;
    a->accepted=pin(a->s);
    return NULL;
}
int main(void)
{
    struct surface shared={.bytes=60};
    assert(pin(&shared)); assert(atomic_load(&gpu_wired)==60);
    assert(pin(&shared)); assert(shared.refs==2 && shared.charged==60);
    unpin(&shared); assert(atomic_load(&gpu_wired)==60);
    /* Imported handle close does not release a backing's retained lease. */
    assert(pin(&shared)); /* simulated mapped/quarantined backing */
    unpin(&shared);       /* handle close */
    assert(shared.refs==1 && atomic_load(&gpu_wired)==60);
    unpin(&shared);       /* GPU known idle / backing disposal */
    assert(shared.refs==0 && shared.charged==0 && atomic_load(&gpu_wired)==0);

    /* Failed layout/page validation must precede lease acquisition. */
    struct surface rejected={.bytes=40};
    assert(!import_after_validation(&rejected,1,0));
    assert(!import_after_validation(&rejected,0,1));
    assert(rejected.refs==0 && rejected.charged==0 && atomic_load(&gpu_wired)==0);

    uint32_t next_refs; uint64_t next_charge,reserve;
    assert(!tegu_surface_gpu_import_lease_acquire(UINT32_MAX,0,16,0,
        &next_refs,&next_charge,&reserve));
    assert(!tegu_surface_gpu_import_lease_acquire(1,32,16,0,
        &next_refs,&next_charge,&reserve));
    assert(!tegu_surface_gpu_import_lease_release(0,0,&next_refs,&reserve));

    /* Custom allocation is charged once by its existing descriptor path;
     * importing it pins lifetime but never double-charges the subset. */
    assert(reserve_gpu(80));
    struct surface custom={.bytes=80,.custom=1};
    assert(pin(&custom)); assert(pin(&custom));
    assert(atomic_load(&gpu_wired)==80 && custom.charged==0);
    unpin(&custom); unpin(&custom);
    assert(atomic_load(&gpu_wired)==80);
    unreserve_gpu(80); /* custom descriptor's last release */

    /* Over-quota first import fails without a lease, ref leak, or residual
     * charge; a later valid import can still succeed. */
    assert(reserve_gpu(70));
    struct surface too_big={.bytes=40};
    assert(!pin(&too_big));
    assert(too_big.refs==0 && too_big.charged==0 && atomic_load(&gpu_wired)==70);
    unreserve_gpu(70);
    assert(pin(&too_big)); unpin(&too_big);
    assert(atomic_load(&gpu_wired)==0);

    /* Same-Surface import/release racing across clients deduplicates one
     * allocation and returns every ref/charge to zero. */
    struct surface concurrent={.bytes=64};
    enum { THREADS=8 };
    pthread_t threads[THREADS]; struct thread_arg ta={&concurrent,2000};
    for (unsigned i=0;i<THREADS;i++) assert(!pthread_create(&threads[i],NULL,worker,&ta));
    for (unsigned i=0;i<THREADS;i++) assert(!pthread_join(threads[i],NULL));
    assert(concurrent.refs==0 && concurrent.charged==0 && atomic_load(&gpu_wired)==0);

    /* Distinct first-import race at the limit: only one may reserve 60 bytes. */
    struct surface a={.bytes=60}, b={.bytes=60};
    struct race_arg ra={&a,0}, rb={&b,0}; pthread_t t1,t2;
    assert(!pthread_create(&t1,NULL,race_pin,&ra));
    assert(!pthread_create(&t2,NULL,race_pin,&rb));
    assert(!pthread_join(t1,NULL)); assert(!pthread_join(t2,NULL));
    assert(ra.accepted+rb.accepted==1 && atomic_load(&gpu_wired)==60);
    if (ra.accepted) unpin(&a); else unpin(&b);
    assert(atomic_load(&gpu_wired)==0);
    puts("PASS ordinary IOSurface import lease dedup, quota refusal, concurrency, and quarantine lifetime");
}
