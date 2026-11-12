/* SPDX-License-Identifier: MIT */
/*
 * n2a32_excl.c -- the LDREX/STREX monitor, emulated value-based on real host memory.  V02.
 *
 * The guest can run several threads (shims give each one its own struct n2a32_cpu) on one address space, so the
 * monitor state cannot live in the cpu alone and the store must be a real compare-and-swap rather than a
 * load/store pair.  n2a32_excl_mark() remembers the value the guest read; n2a32_excl_store() succeeds only if the
 * host memory still holds exactly that value, using __atomic_compare_exchange_n of the matching width.
 *
 * struct n2a32_cpu has only excl_addr/excl_valid, so the read value and width are kept in a small process-wide
 * table keyed by the cpu pointer under one mutex.  A cpu has at most one monitor, as the architecture does.
 */
#include "n2a32.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

struct monitor {
	struct n2a32_cpu *cpu;
	uint32_t addr;
	unsigned bytes;
	uint64_t value;
	int used;
};

static pthread_mutex_t mon_lock = PTHREAD_MUTEX_INITIALIZER;
static struct monitor mon_tab[64];

static struct monitor *mon_find(struct n2a32_cpu *c)
{
	unsigned i;

	for (i = 0; i < sizeof(mon_tab) / sizeof(mon_tab[0]); i++) {
		if (mon_tab[i].used && mon_tab[i].cpu == c)
			return &mon_tab[i];
	}
	return NULL;
}

static struct monitor *mon_claim(struct n2a32_cpu *c)
{
	struct monitor *m = mon_find(c);
	unsigned i;

	if (m)
		return m;
	for (i = 0; i < sizeof(mon_tab) / sizeof(mon_tab[0]); i++) {
		if (!mon_tab[i].used) {
			mon_tab[i].used = 1;
			mon_tab[i].cpu = c;
			mon_tab[i].addr = 0;
			mon_tab[i].bytes = 0;
			mon_tab[i].value = 0;
			return &mon_tab[i];
		}
	}
	/* Table full (more than 64 live cpus with a monitor): steal the first slot; only affects exotic hosts. */
	mon_tab[0].cpu = c;
	mon_tab[0].addr = 0;
	mon_tab[0].bytes = 0;
	mon_tab[0].value = 0;
	return &mon_tab[0];
}

void n2a32_excl_mark(struct n2a32_cpu *c, uint32_t addr, unsigned bytes, uint64_t value_read)
{
	struct monitor *m;

	if (!c || (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8)) {
		if (c)
			c->excl_valid = 0;
		return;
	}
	pthread_mutex_lock(&mon_lock);
	m = mon_claim(c);
	m->addr = addr;
	m->bytes = bytes;
	m->value = value_read;
	c->excl_addr = addr;
	c->excl_valid = bytes;      /* width, or 0 = no monitor */
	pthread_mutex_unlock(&mon_lock);
}

int n2a32_excl_store(struct n2a32_cpu *c, uint32_t addr, unsigned bytes, uint64_t value)
{
	struct monitor *m;
	uint8_t *p;
	int ok = 0;

	if (!c || !c->mem || !c->mem->base)
		return 1;
	pthread_mutex_lock(&mon_lock);
	m = mon_find(c);
	if (!m || c->excl_valid == 0 || c->excl_addr != addr || m->addr != addr || m->bytes != bytes ||
	    (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8)) {
		c->excl_valid = 0;
		pthread_mutex_unlock(&mon_lock);
		return 1;
	}
	if ((addr & (bytes - 1u)) != 0 || (uint64_t)addr + bytes > 0x100000000ull ||
	    n2a32_is_mmio(c->mem, addr)) {
		c->excl_valid = 0;
		m->used = 0;
		pthread_mutex_unlock(&mon_lock);
		return 1;
	}
	p = c->mem->base + addr;
	switch (bytes) {
	case 1: {
		uint8_t expected = (uint8_t)m->value;
		ok = __atomic_compare_exchange_n((uint8_t *)p, &expected, (uint8_t)value, 0,
						 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
		break;
	}
	case 2: {
		uint16_t expected = (uint16_t)m->value;
		ok = __atomic_compare_exchange_n((uint16_t *)p, &expected, (uint16_t)value, 0,
						 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
		break;
	}
	case 4: {
		uint32_t expected = (uint32_t)m->value;
		ok = __atomic_compare_exchange_n((uint32_t *)p, &expected, (uint32_t)value, 0,
						 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
		break;
	}
	default: {
		uint64_t expected = m->value;
		ok = __atomic_compare_exchange_n((uint64_t *)p, &expected, value, 0,
						 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
		break;
	}
	}
	c->excl_valid = 0;
	m->used = 0;
	pthread_mutex_unlock(&mon_lock);
	return ok ? 0 : 1;
}
