/*
 * n2vk_dispatch.c -- n2vk_use() and the routing of the three kernels of include/n2vk.h.
 *
 * The rule, from the header:  a NULL member of the installed backend means "the reference
 * for this kernel"; a backend call that answers N2VK_E_GPU or N2VK_E_UNSUPPORTED is
 * answered by the reference instead; and three N2VK_E_GPU in a row keep that one kernel on
 * the reference until n2vk_use() is called again.  The three kernels have their own
 * counters and their own disabled flag.
 *
 * Thread safety: one process-global spin lock guards the backend and the counters.  A
 * backend call itself is made OUTSIDE the lock (a motion search takes milliseconds; a
 * switch must not wait for it), against a snapshot of the function pointer and ctx.  A
 * result is credited to the counter only when the same backend member is still installed.
 *
 * The lock is a C11 atomic_flag rather than a pthread mutex so that this file needs no
 * library beyond the C atomics, which clang turns into ldxr/stxr on arm64.
 *
 * SPDX-License-Identifier: MIT
 */
#include "n2vk.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { K_ME16 = 0, K_HALF = 1, K_FDCT = 2, K_COUNT = 3 };

static atomic_flag lock_flag = ATOMIC_FLAG_INIT;

static struct {
	int have;
	n2vk_backend backend;
	unsigned strikes[K_COUNT];
	int disabled[K_COUNT];
} g;

/* n2vk_backend_name's answer, copied under the lock. */
static char g_name[64] = "reference";

static void
lock(void)
{
	while (atomic_flag_test_and_set_explicit(&lock_flag, memory_order_acquire))
		;
}

static void
unlock(void)
{
	atomic_flag_clear_explicit(&lock_flag, memory_order_release);
}

void
n2vk_use(const n2vk_backend *backend)
{
	lock();
	memset(&g.backend, 0, sizeof(g.backend));
	g.have = 0;
	for (int k = 0; k < K_COUNT; k++) {
		g.strikes[k] = 0;
		g.disabled[k] = 0;
	}
	if (backend != NULL && backend->size >= sizeof(*backend)) {
		g.backend = *backend;
		g.have = 1;
		snprintf(g_name, sizeof(g_name), "%s", backend->name != NULL ? backend->name : "backend");
	} else {
		snprintf(g_name, sizeof(g_name), "reference");
	}
	unlock();
}

const char *
n2vk_backend_name(void)
{
	static _Thread_local char answer[64];

	lock();
	memcpy(answer, g_name, sizeof(answer));
	unlock();
	return answer;
}

/* The member of `b' that is this kernel's, so that a result can be credited only when the
 * same one is still installed. */
static int
member_is(int kernel, const n2vk_backend *a, const n2vk_backend *b)
{
	switch (kernel) {
	case K_ME16:
		return a->me16 == b->me16 && a->ctx == b->ctx;
	case K_HALF:
		return a->half == b->half && a->ctx == b->ctx;
	default:
		return a->fdct8_quant == b->fdct8_quant && a->ctx == b->ctx;
	}
}

/* What one result does to the kernel's strike counter.  Called after the backend
 * returned, with the snapshot it was called through. */
static void
settle(int kernel, const n2vk_backend *called, int rc)
{
	lock();
	if (g.have && member_is(kernel, &g.backend, called)) {
		if (rc == N2VK_E_GPU) {
			if (++g.strikes[kernel] >= 3)
				g.disabled[kernel] = 1;
		} else {
			/* success, or E_UNSUPPORTED: not a GPU failure, and either breaks a
			 * run of them ("three ... in a row") */
			g.strikes[kernel] = 0;
		}
	}
	unlock();
}

/* A snapshot of one member: may the backend be asked for this kernel at all? */
static int
want(int kernel, n2vk_backend *out)
{
	int use = 0;

	lock();
	if (g.have && !g.disabled[kernel]) {
		const int present = kernel == K_ME16 ? g.backend.me16 != NULL
		                  : kernel == K_HALF ? g.backend.half != NULL
		                                     : g.backend.fdct8_quant != NULL;

		if (present) {
			*out = g.backend;
			use = 1;
		}
	}
	unlock();
	return use;
}

int
n2vk_me16(const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad)
{
	n2vk_backend b;

	if (want(K_ME16, &b)) {
		const int rc = b.me16(b.ctx, a, mvx, mvy, sad);

		if (rc == N2VK_OK)
			return N2VK_OK;
		if (rc == N2VK_E_GPU || rc == N2VK_E_UNSUPPORTED)
			settle(K_ME16, &b, rc);
		else
			return rc;
	}
	return n2vk_ref_me16(a, mvx, mvy, sad);
}

int
n2vk_half(const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride, uint32_t width, uint32_t height)
{
	n2vk_backend b;

	if (want(K_HALF, &b)) {
		const int rc = b.half(b.ctx, src, src_stride, dst, dst_stride, width, height);

		if (rc == N2VK_OK)
			return N2VK_OK;
		if (rc == N2VK_E_GPU || rc == N2VK_E_UNSUPPORTED)
			settle(K_HALF, &b, rc);
		else
			return rc;
	}
	return n2vk_ref_half(src, src_stride, dst, dst_stride, width, height);
}

int
n2vk_fdct8_quant(const uint8_t *src, uint32_t src_stride, uint32_t width, uint32_t height, const uint16_t qtable[64],
                 int16_t *coef)
{
	n2vk_backend b;

	if (want(K_FDCT, &b)) {
		const int rc = b.fdct8_quant(b.ctx, src, src_stride, width, height, qtable, coef);

		if (rc == N2VK_OK)
			return N2VK_OK;
		if (rc == N2VK_E_GPU || rc == N2VK_E_UNSUPPORTED)
			settle(K_FDCT, &b, rc);
		else
			return rc;
	}
	return n2vk_ref_fdct8_quant(src, src_stride, width, height, qtable, coef);
}
