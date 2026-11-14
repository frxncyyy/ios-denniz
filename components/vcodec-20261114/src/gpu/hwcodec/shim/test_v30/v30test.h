/* SPDX-License-Identifier: MIT */
/* v30test.h -- shared scaffolding of the V30 host tests (string/format/heap shims). */
#ifndef V30TEST_H
#define V30TEST_H

#include <stdint.h>
#include <stddef.h>

#include "n2a32.h"
#include "n2elf.h"

extern int t_pass, t_total;
extern struct n2a32_mem t_mem;

void t_checkf(int ok, const char *fmt, ...);
#define T_CHECK(cond) t_checkf((cond) != 0, "%s:%d: %s", __FILE__, __LINE__, #cond)

void t_init(void);                      /* reserve the guest address space, set up stack zones */
uint32_t t_alloc(uint32_t n);           /* guest bytes (zero filled, 16-aligned) */
uint32_t t_put(const char *s);          /* copy a C string into guest memory */
uint32_t t_putn(const void *p, uint32_t n);
struct n2a32_cpu *t_cpu(void);          /* the main thread's cpu (stack zone 0) */
struct n2a32_cpu *t_cpu_zone(unsigned zone, uint32_t *sp_out);
uint32_t t_call(const struct n2shim_entry *tab, const char *name, const uint32_t *args, int nargs,
		uint32_t *r1);
struct n2a32_cpu *t_last_cpu(void);    /* the cpu of the last t_call, without resetting it */
const char *t_gstr(uint32_t a, char *buf, size_t n);
const struct n2shim_entry *t_find(const struct n2shim_entry *tab, const char *name);

void test_string(void);
void test_format(void);
void test_fuzz(void);
void test_scanf(void);
void test_heap(void);
void test_qsort(void);
void test_mmap(void);

#endif
