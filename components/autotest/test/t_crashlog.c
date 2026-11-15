/* Host test of bsd/dev/nord2_crashlog.inc (0301): the ring in the ramoops console zone, read back the way the stock kernel's
 * persistent_ram does (data[start..size) then data[0..start)), must be the newest bytes of the live log, through rewinds. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void *thread_t; typedef int wait_result_t; typedef uintptr_t vm_offset_t; typedef size_t vm_size_t;
typedef enum { KDP_EVENT_ENTER, KDP_EVENT_EXIT, KDP_EVENT_PANICLOG } kdp_event_t;
typedef void (*kdp_callout_fn_t)(void *, kdp_event_t);
#define KERN_SUCCESS 0
#define NSEC_PER_MSEC 1000000ull
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL 0
static uint8_t zone[0x1000 + 64], stream[1 << 16]; static uint32_t stream_size;
static vm_offset_t ml_io_map(vm_offset_t a, vm_size_t s) { (void)a; (void)s; return (vm_offset_t)zone; }
void nord2_log_stream_info(uint32_t info[2]) { info[0] = sizeof stream; info[1] = stream_size; }
uint32_t nord2_log_stream_read(uint32_t off, uint8_t *out, uint32_t max) { if (off >= stream_size) return 0; uint32_t n = stream_size - off < max ? stream_size - off : max; memcpy(out, stream + off, n); return n; }
static uint32_t nat_log_rewind(uint32_t expected) { if (expected != stream_size) return 5; stream_size = 0; return 1; }
static void clock_interval_to_deadline(unsigned a, uint64_t b, uint64_t *d) { (void)a; (void)b; *d = 0; }
static int assert_wait_deadline(void *c, int i, uint64_t d) { (void)c; (void)i; (void)d; return 0; }
static void thread_block(int c) { (void)c; }
static void thread_deallocate(thread_t t) { (void)t; }
static int kernel_thread_start(void (*f)(void *, wait_result_t), void *a, thread_t *t) { (void)f; (void)a; *t = NULL; return 0; }
static kdp_callout_fn_t dbg; static void kdp_register_callout(kdp_callout_fn_t f, void *a) { (void)a; dbg = f; }
#include "crashlog_small.inc"
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static uint8_t all[1 << 22]; static uint32_t total;
static void check(void) {
	uint32_t sig, start, size; memcpy(&sig, zone, 4); memcpy(&start, zone + 4, 4); memcpy(&size, zone + 8, 4);
	CHECK(sig == NCL_SIG && size <= NCL_CAP && start <= size);
	static uint8_t old[NCL_CAP]; memcpy(old, zone + 12 + start, size - start); memcpy(old + size - start, zone + 12, start);
	uint32_t want = total < NCL_CAP ? total : NCL_CAP; CHECK(size == want); CHECK(memcmp(old, all + total - want, want) == 0);
}
int main(void) {
	nord2_crashlog_start(); CHECK(ncl.hdr && dbg);
	uint32_t seed = 7;
	for (int round = 0; round < 8000; round++) {
		seed = seed * 1664525u + 1013904223u; uint32_t n = (seed >> 20) % 900;
		if (stream_size + n > sizeof stream) { ncl_copy(); CHECK(nat_log_rewind(stream_size) == 1); }
		for (uint32_t i = 0; i < n; i++) { uint8_t c = (uint8_t)(total * 31 + (total >> 8)); stream[stream_size++] = c; all[total++ % sizeof all] = c; if (total % sizeof all == 0) { total = 0; return puts("wrap of the reference: shorten the test"), 1; } }
		if (round % 3 == 0) { ncl_copy(); if (ncl.off == stream_size) check(); }
		if (round % 997 == 0) { dbg(NULL, KDP_EVENT_ENTER); check(); }
		if (round % 1500 == 7 && ncl.off == stream_size) { CHECK(nat_log_rewind(stream_size) == 1); }      /* a host rewinds after all was copied */
	}
	for (int i = 0; i < 400; i++) ncl_copy();
	check(); printf("t_crashlog OK (%u bytes through a %u byte ring)\n", total, NCL_CAP); return 0;
}
