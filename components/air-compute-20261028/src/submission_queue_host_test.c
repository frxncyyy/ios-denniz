#include "submission_queue.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

struct target { unsigned refs, number, scheduled; };
static void *pending_context;
static void (*pending_callback)(void *);
static struct target *pending_target, *running_target;
static pthread_mutex_t backend_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned completed, disposed, last[2], executed;
struct job {
    struct tegu_submission_job base;
    unsigned producer, sequence, target_number;
    int failed;
};
static void retain_target(void *p) {
    struct target *t=p;
    assert(__atomic_fetch_add(&t->refs,1,__ATOMIC_RELAXED)>0);
}
static void release_target(void *p) {
    struct target *t=p;
    assert(__atomic_fetch_sub(&t->refs,1,__ATOMIC_RELAXED)>0);
}
static void schedule(void *target,void *context,void (*callback)(void *)) {
    pthread_mutex_lock(&backend_lock);
    assert(!pending_callback);
    pending_target=target;pending_context=context;pending_callback=callback;
    ((struct target *)target)->scheduled++;
    pthread_mutex_unlock(&backend_lock);
}
static int pump(void) {
    pthread_mutex_lock(&backend_lock);
    void (*callback)(void *)=pending_callback;
    void *context=pending_context;
    running_target=pending_target;pending_callback=NULL;
    pthread_mutex_unlock(&backend_lock);
    if(!callback)return 0;
    callback(context);running_target=NULL;return 1;
}
static void run(struct tegu_submission_job *base) {
    struct job *job=(void *)base;
    assert(!executed++);
    assert(job->sequence==last[job->producer]+1);
    last[job->producer]=job->sequence;
    if(job->target_number) {
        assert(running_target&&running_target->number==job->target_number);
        assert(running_target->refs);
    }
    /* A failed GPU operation still retires the job and allows later work. */
    if(job->failed)assert(job->sequence==2);
    completed++;executed--;
}
static void dispose(struct tegu_submission_job *base) { disposed++;free(base); }
static void enqueue(struct tegu_submission_queue *q,unsigned producer,unsigned seq,unsigned target) {
    struct job *job=calloc(1,sizeof(*job));assert(job);
    job->base.run=run;job->base.dispose=dispose;
    job->producer=producer;job->sequence=seq;job->target_number=target;
    job->failed=seq==2;
    tegu_submission_enqueue(q,&job->base);
}
struct producer { struct tegu_submission_queue *q;unsigned index; };
static void *produce(void *arg) {
    struct producer *p=arg;
    for(unsigned i=1;i<=1000;i++)enqueue(p->q,p->index,i,3);
    return NULL;
}
static void final_run(struct tegu_submission_job *job) { (void)job; }
struct final_job {struct tegu_submission_job base;struct tegu_submission_queue *q;};
static void final_dispose(struct tegu_submission_job *job) {
    free(((struct final_job *)job)->q);free(job);
}
int main(void) {
    struct tegu_submission_queue q={.ops={retain_target,release_target,schedule}};
    struct target a={1,1,0},b={1,2,0};
    assert(tegu_submission_set_target(&q,&a));
    enqueue(&q,0,1,1);enqueue(&q,0,2,1);
    assert(completed==0&&disposed==0);
    assert(tegu_submission_set_target(&q,&b));
    release_target(&a); /* queued jobs now own all remaining old-target refs */
    enqueue(&q,0,3,2);
    assert(tegu_submission_set_target(&q,NULL));release_target(&b);
    for(unsigned i=4;i<=10003;i++)enqueue(&q,0,i,0);
    assert(completed==0&&q.active);
    while(pump()){}
    assert(completed==10003&&disposed==completed&&!q.active&&!q.head&&!q.tail);
    assert(!a.refs&&!b.refs&&a.scheduled==2&&b.scheduled==1);
    enqueue(&q,0,10004,0); /* default target completes inline */
    assert(completed==10004&&disposed==completed);
    struct tegu_submission_queue unavailable={0};
    assert(!tegu_submission_set_target(&unavailable,&a)&&!unavailable.target);
    /* Concurrent producers preserve each producer's order without losing jobs. */
    struct target c={1,3,0};last[0]=last[1]=0;
    assert(tegu_submission_set_target(&q,&c));
    pthread_t threads[2];struct producer producers[2]={{&q,0},{&q,1}};
    for(unsigned i=0;i<2;i++)assert(!pthread_create(&threads[i],NULL,produce,&producers[i]));
    for(unsigned i=0;i<2;i++)assert(!pthread_join(threads[i],NULL));
    assert(tegu_submission_set_target(&q,NULL));release_target(&c);
    while(pump()){}
    assert(last[0]==1000&&last[1]==1000&&completed==12004&&disposed==completed);
    assert(!c.refs&&c.scheduled==2000&&!q.active);
    /* The last job may destroy its queue: sanitizer catches any later access. */
    struct tegu_submission_queue *final=calloc(1,sizeof(*final));
    struct final_job *job=calloc(1,sizeof(*job));assert(final&&job);
    job->q=final;job->base.run=final_run;job->base.dispose=final_dispose;
    tegu_submission_enqueue(final,&job->base);
    puts("PASS submission FIFO, target replacement, retention, concurrent producers, teardown");
}
