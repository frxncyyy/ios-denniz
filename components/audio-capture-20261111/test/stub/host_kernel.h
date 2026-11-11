#ifndef HOST_KERNEL_H
#define HOST_KERNEL_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uintptr_t vm_offset_t; typedef uintptr_t vm_size_t; typedef void *event_t;
struct sysctl_oid; struct sysctl_req { void *newptr; };
static inline int sysctl_handle_int(struct sysctl_oid *o, void *a, int b, struct sysctl_req *r) { (void)o;(void)a;(void)b;(void)r; return 0; }
#define LCK_GRP_DECLARE(n, s) int n __attribute__((unused))
#define LCK_MTX_DECLARE(n, g) int n __attribute__((unused))
#define lck_mtx_lock(x) ((void)0)
#define lck_mtx_unlock(x) ((void)0)
#define NSEC_PER_MSEC 1000000ull
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL 0
uint64_t mach_absolute_time(void);
void absolutetime_to_nanoseconds(uint64_t, uint64_t *);
void clock_interval_to_deadline(uint32_t, uint32_t, uint64_t *);
int assert_wait_deadline(event_t, int, uint64_t);
int thread_block(int);
void delay(int);
vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
#define __unused __attribute__((unused))
#endif
