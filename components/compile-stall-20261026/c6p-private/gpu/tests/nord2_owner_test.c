#define _POSIX_C_SOURCE 200809L
#include "nord2_gpu_owner.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
static struct n2owner q;
static unsigned stop;
static pthread_t owner_id;
static uint64_t now_ms(void *p) { (void)p;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ull+t.tv_nsec/1000000; }
static void sleep_ms(void *p,unsigned ms) { (void)p;struct timespec t={ms/1000,(long)(ms%1000)*1000000};nanosleep(&t,0); }
static void *owner(void *p) { (void)p;owner_id=pthread_self();while(!__atomic_load_n(&stop,__ATOMIC_ACQUIRE)){n2owner_poll(&q);sleep_ms(0,1);}return 0; }
static int callback(void *p) { assert(pthread_equal(owner_id,pthread_self()));int*v=p;(*v)++;sleep_ms(0,2);return *v; }
int main(void) {
 struct n2owner_wait wait={now_ms,sleep_ms,0};int result=0,count=0;
 assert(!n2owner_call(&q,callback,&count,&wait,1,&result));
 n2owner_poll(&q);assert(!n2owner_call(&q,callback,&count,&wait,1,&result));assert(!count);
 pthread_t t;assert(!pthread_create(&t,0,owner,0));
 for(unsigned i=0;i<200;i++) {
  bool ok=n2owner_call(&q,callback,&count,&wait,10,&result);assert(ok);assert(result==count && count==(int)i+1);
 }
 __atomic_store_n(&stop,1,__ATOMIC_RELEASE);assert(!pthread_join(t,0));
 puts("owner: cancellation and 200 serial cross-thread callbacks PASS");return 0;
}
