/* SPDX-License-Identifier: MIT */
/*
 * t_fake.c -- T02 host test of tests/hw/n2vc_hw_fake.c.
 *
 * The expected values are literals from include/redwood_vcodec.h and from
 * K02's gpu/hwcodec/kern/VCODEC_HW.md (window sizes and the window -> power
 * block table), not from the fake's own tables.  Script files under
 * tests/hw/scripts/ are literal inputs; their expected effects are written
 * in the checks below.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "n2vc_hw.h"
#include "n2vc_hw_fake.h"

static int checks, fails;

#define CK(c) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
	} \
} while (0)

static int pw(struct n2vc_hw *h, unsigned block, int on)
{
	return h->power(h->ctx, block, on);
}

static int reg1(struct n2vc_hw *h, struct rwvc_reg_op *o, unsigned *done)
{
	return h->regs(h->ctx, o, 1, done);
}

static int rd(struct n2vc_hw *h, unsigned win, uint32_t off, uint32_t *v)
{
	struct rwvc_reg_op o = { RWVC_OP_READ, (uint8_t)win, 0, off, 0, 0 };
	unsigned done = 0;
	int rc = reg1(h, &o, &done);
	*v = o.value;
	return rc;
}

static int wr(struct n2vc_hw *h, unsigned win, uint32_t off, uint32_t v)
{
	struct rwvc_reg_op o = { RWVC_OP_WRITE, (uint8_t)win, 0, off, v, 0 };
	unsigned done = 0;
	return reg1(h, &o, &done);
}

static void msleep(unsigned ms)
{
	struct timespec ts = { ms / 1000u, (long)(ms % 1000u) * 1000000L };
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

static int file_line(const char *path, unsigned n, char *out, size_t cap)
{
	FILE *fp = fopen(path, "r");
	unsigned i = 0;
	int ok = 0;

	if (!fp)
		return 0;
	while (fgets(out, (int)cap, fp)) {
		size_t len = strlen(out);
		if (len && out[len - 1] == '\n')
			out[--len] = '\0';
		if (i == n) {
			ok = 1;
			break;
		}
		i++;
	}
	fclose(fp);
	return ok;
}

static int file_lines(const char *path)
{
	FILE *fp = fopen(path, "r");
	char buf[512];
	int n = 0;

	if (!fp)
		return -1;
	while (fgets(buf, sizeof buf, fp))
		n++;
	fclose(fp);
	return n;
}

/* K02 VCODEC_HW.md section 0, literal. */
static const uint32_t EXPECT_BYTES[RWVC_WIN_COUNT] = {
	0x2000, 0x2000, 0x1000, 0x1000, 0x1000, 0x1000,
	0x1000, 0x4000, 0x1000, 0x0400, 0x1000, 0x1000,
	0x30000
};
static const unsigned EXPECT_BLOCK[RWVC_WIN_COUNT] = {
	RWVC_BLK_VENC, RWVC_BLK_VENC_C1, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE,
	RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_CORE,
	RWVC_BLK_VDEC_SOC, RWVC_BLK_VDEC_SOC, RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_SOC,
	RWVC_BLK_VDEC_CORE
};

static void test_power_and_windows(const char *dir)
{
	unsigned w;

	(void)dir;
	for (w = 0; w < RWVC_WIN_COUNT; w++) {
		struct n2vc_hw h;
		struct rwvc_reg_op o;
		uint32_t v = 0;
		unsigned done = 99;
		char path[512];

		snprintf(path, sizeof path, "%s/win%u.rec", dir, w);
		CK(n2vc_hw_open_fake(&h, path) == 0);
		CK(h.ctx != NULL && h.power && h.regs && h.wait_irq && h.irq_clear);
		CK(h.dma_alloc && h.dma_free && h.dma_rw && h.close);

		/* Unpowered: every window of the block is EACCES and counted. */
		o.op = RWVC_OP_READ;
		o.window = (uint8_t)w;
		o.reserved = 0;
		o.offset = 0;
		o.value = 0;
		o.mask = 0;
		done = 99;
		CK(reg1(&h, &o, &done) == -EACCES);
		CK(done == 0);
		CK(n2vc_hw_fake_violations(&h) == 1);
		o.op = RWVC_OP_WRITE;
		o.value = 0x55;
		CK(reg1(&h, &o, &done) == -EACCES);
		CK(n2vc_hw_fake_violations(&h) == 2);

		/* Power exactly the block K02 names for this window. */
		CK(pw(&h, EXPECT_BLOCK[w], 1) == 0);
		CK(wr(&h, w, 0, 0x11110000u + w) == 0);
		CK(rd(&h, w, 0, &v) == 0);
		CK(v == 0x11110000u + w);
		CK(wr(&h, w, EXPECT_BYTES[w] - 4u, 0xa5a50000u + w) == 0);
		CK(rd(&h, w, EXPECT_BYTES[w] - 4u, &v) == 0);
		CK(v == 0xa5a50000u + w);

		/* One word past the window is EINVAL, not a violation. */
		o.op = RWVC_OP_READ;
		o.window = (uint8_t)w;
		o.offset = EXPECT_BYTES[w];
		CK(reg1(&h, &o, &done) == -EINVAL);
		o.offset = 2;
		CK(reg1(&h, &o, &done) == -EINVAL);
		CK(n2vc_hw_fake_violations(&h) == 2);

		CK(pw(&h, EXPECT_BLOCK[w], 0) == 0);
		CK(rd(&h, w, 0, &v) == -EACCES);
		CK(n2vc_hw_fake_violations(&h) == 3);
		h.close(h.ctx);

		/* The record names the window exactly as the header does. */
		{
			char line[512], want[64];
			CK(file_lines(path) >= 3);
			CK(file_line(path, 2, line, sizeof line));
			snprintf(want, sizeof want, "POWER %s on -> 0",
				 EXPECT_BLOCK[w] == RWVC_BLK_VENC ? "VENC" :
				 EXPECT_BLOCK[w] == RWVC_BLK_VENC_C1 ? "VENC_C1" :
				 EXPECT_BLOCK[w] == RWVC_BLK_VDEC_SOC ? "VDEC_SOC" : "VDEC_CORE");
			CK(strcmp(line, want) == 0);
		}
	}
}

static void test_power_refs(void)
{
	struct n2vc_hw h;
	uint32_t v;

	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	/* VDEC_CORE implies VDEC_SOC (hidden reference). */
	CK(pw(&h, RWVC_BLK_VDEC_CORE, 1) == 0);
	CK(rd(&h, RWVC_WIN_VDEC_SYS, 0, &v) == 0);
	CK(rd(&h, RWVC_WIN_VDEC_LAT_MISC, 0, &v) == 0);
	CK(pw(&h, RWVC_BLK_VDEC_CORE, 0) == 0);
	CK(rd(&h, RWVC_WIN_VDEC_SYS, 0, &v) == -EACCES);
	CK(rd(&h, RWVC_WIN_VDEC_LAT_MISC, 0, &v) == -EACCES);

	/* VDEC_SOC alone opens only its own windows. */
	CK(pw(&h, RWVC_BLK_VDEC_SOC, 1) == 0);
	CK(rd(&h, RWVC_WIN_VDEC_LAT_VLD, 0, &v) == 0);
	CK(rd(&h, RWVC_WIN_VDEC_BASE, 0, &v) == -EACCES);
	CK(pw(&h, RWVC_BLK_VDEC_SOC, 0) == 0);

	/* VENC_C1 implies VEN; user references are counted. */
	CK(pw(&h, RWVC_BLK_VENC_C1, 1) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0, &v) == 0);
	CK(pw(&h, RWVC_BLK_VENC_C1, 0) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0, &v) == -EACCES);

	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 0) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0, &v) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 0) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0, &v) == -EACCES);
	CK(pw(&h, RWVC_BLK_VENC, 0) == -EINVAL);
	CK(pw(&h, RWVC_BLK_COUNT, 1) == -EINVAL);
	h.close(h.ctx);
}

static void test_record_exact(const char *dir)
{
	struct n2vc_hw h;
	uint32_t v;
	char path[512], line[512];
	const char *expect[] = {
		"POWER VENC on -> 0",
		"W VENC 0x0010 0x00000001",
		"R VENC 0x0010 0x00000001",
		"POWER VENC off -> 0",
		"R VENC 0x0010 EACCES",
	};
	unsigned i;

	snprintf(path, sizeof path, "%s/record.rec", dir);
	CK(n2vc_hw_open_fake(&h, path) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(wr(&h, RWVC_WIN_VENC, 0x10, 1) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0x10, &v) == 0 && v == 1);
	CK(pw(&h, RWVC_BLK_VENC, 0) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0x10, &v) == -EACCES);
	h.close(h.ctx);
	CK(file_lines(path) == 5);
	for (i = 0; i < 5; i++) {
		CK(file_line(path, i, line, sizeof line));
		CK(strcmp(line, expect[i]) == 0);
		if (strcmp(line, expect[i]) != 0)
			fprintf(stderr, "  line %u: got '%s' want '%s'\n", i, line, expect[i]);
	}

	/* Masked write and script read/action lines. */
	snprintf(path, sizeof path, "%s/record2.rec", dir);
	CK(n2vc_hw_open_fake(&h, path) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(wr(&h, RWVC_WIN_VENC, 0x10, 0x0f) == 0);
	{
		struct rwvc_reg_op o = { RWVC_OP_WRITE_MASK, RWVC_WIN_VENC, 0, 0x10, 0xf0, 0xf0 };
		unsigned done = 0;
		CK(reg1(&h, &o, &done) == 0);
	}
	CK(rd(&h, RWVC_WIN_VENC, 0x10, &v) == 0 && v == 0xff);
	h.close(h.ctx);
	CK(file_line(path, 2, line, sizeof line));
	CK(strcmp(line, "W VENC 0x0010 0x000000f0 mask 0x000000f0") == 0);
}

static void test_script_basic(const char *dir)
{
	struct n2vc_hw h;
	uint32_t v;
	char path[512];

	snprintf(path, sizeof path, "%s/basic.txt", dir);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(n2vc_hw_fake_script(&h, path) == 0);
	CK(n2vc_hw_fake_last_error(&h)[0] == '\0');
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0x20, &v) == 0 && v == 0x12345678u);
	CK(wr(&h, RWVC_WIN_VENC, 0x14, 0) == 0);
	CK(wr(&h, RWVC_WIN_VENC, 0x10, 1) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0x14, &v) == 0 && v == 2);
	CK(wr(&h, RWVC_WIN_VENC, 0x14, 0) == 0);
	CK(wr(&h, RWVC_WIN_VENC, 0x10, 0x11) == 0);   /* (0x11 & 0xf) == 1 */
	CK(rd(&h, RWVC_WIN_VENC, 0x14, &v) == 0 && v == 2);
	CK(wr(&h, RWVC_WIN_VENC, 0x14, 0) == 0);
	CK(wr(&h, RWVC_WIN_VENC, 0x10, 2) == 0);      /* no match */
	CK(rd(&h, RWVC_WIN_VENC, 0x14, &v) == 0 && v == 0);
	{
		struct rwvc_reg_op o = { RWVC_OP_WRITE_MASK, RWVC_WIN_VENC, 0, 0x10, 0x21, 0x0f };
		unsigned done = 0;
		CK(reg1(&h, &o, &done) == 0);           /* trigger value is 0x21 & 0x0f == 1 */
	}
	CK(rd(&h, RWVC_WIN_VENC, 0x14, &v) == 0 && v == 2);
	{
		struct n2vc_hw_fake_stats st;
		n2vc_hw_fake_stats(&h, &st);
		CK(st.script_actions == 3);
		CK(st.violations == 0);
	}
	h.close(h.ctx);

	/* A malformed script is rejected and keeps the previous rules. */
	snprintf(path, sizeof path, "%s/bad.txt", dir);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(n2vc_hw_fake_script(&h, path) == -EINVAL);
	CK(strstr(n2vc_hw_fake_last_error(&h), "read WIN") != NULL);
	h.close(h.ctx);

	/* One-shot read rule. */
	snprintf(path, sizeof path, "%s/readonce.txt", dir);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(n2vc_hw_fake_script(&h, path) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0x24, &v) == 0 && v == 0xdeadbeefu);
	CK(wr(&h, RWVC_WIN_VENC, 0x24, 5) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0x24, &v) == 0 && v == 5);
	h.close(h.ctx);
}

static void test_script_default(const char *dir)
{
	struct n2vc_hw h;
	uint32_t v;
	char path[512];

	snprintf(path, sizeof path, "%s/defaults.txt", dir);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(n2vc_hw_fake_script(&h, path) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0x08, &v) == 0 && v == 0xdeadbeefu);
	h.close(h.ctx);
}

static void test_script_env(const char *dir)
{
	struct n2vc_hw h;
	uint32_t v;
	char path[512];

	snprintf(path, sizeof path, "%s/basic.txt", dir);
	CK(setenv("N2VC_FAKE_SCRIPT", path, 1) == 0);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(rd(&h, RWVC_WIN_VENC, 0x20, &v) == 0 && v == 0x12345678u);
	h.close(h.ctx);
	CK(unsetenv("N2VC_FAKE_SCRIPT") == 0);
}

static void test_poll(const char *dir)
{
	struct n2vc_hw h;
	struct rwvc_reg_op o;
	unsigned done;
	char path[512];
	uint64_t t0, t1;
	struct timespec ts;

	snprintf(path, sizeof path, "%s/poll.txt", dir);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(n2vc_hw_fake_script(&h, path) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(wr(&h, RWVC_WIN_VENC, 0x10, 1) == 0);
	o.op = RWVC_OP_POLL;
	o.window = RWVC_WIN_VENC;
	o.offset = 0x18;
	o.value = 4;
	o.mask = 4;
	done = 99;
	CK(reg1(&h, &o, &done) == 0);
	CK(done == 1);

	/* No rule sets 0x1c: one read with timeout 0 fails at once. */
	CK(n2vc_hw_fake_set_poll_timeout(&h, 0) == 0);
	CK(n2vc_hw_fake_set_poll_timeout(&h, 1000001) == -EINVAL);
	o.offset = 0x1c;
	o.value = 1;
	o.mask = 1;
	done = 99;
	CK(reg1(&h, &o, &done) == -ETIMEDOUT);
	CK(done == 0);
	CK(n2vc_hw_fake_violations(&h) == 0);

	/* 20 ms timeout really waits (bounded, not an unbounded loop). */
	CK(n2vc_hw_fake_set_poll_timeout(&h, 20000) == 0);
	clock_gettime(CLOCK_MONOTONIC, &ts);
	t0 = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
	CK(reg1(&h, &o, &done) == -ETIMEDOUT);
	clock_gettime(CLOCK_MONOTONIC, &ts);
	t1 = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
	CK(t1 - t0 >= 15000u);

	/* DELAY_US bound. */
	o.op = RWVC_OP_DELAY_US;
	o.value = 1001;
	done = 99;
	CK(reg1(&h, &o, &done) == -EINVAL);
	o.value = 100;
	CK(reg1(&h, &o, &done) == 0 && done == 1);
	h.close(h.ctx);
}

static void test_irq(const char *dir)
{
	struct n2vc_hw h;
	uint32_t status = 0;
	char path[512];
	uint64_t t0, t1;
	struct timespec ts;

	snprintf(path, sizeof path, "%s/irq.txt", dir);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(n2vc_hw_fake_script(&h, path) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(h.wait_irq(h.ctx, 0, 0, &status) == -ETIMEDOUT);
	CK(status == 1);
	CK(wr(&h, RWVC_WIN_VENC, 0x30, 1) == 0);
	CK(h.wait_irq(h.ctx, 0, 10, &status) == 0);
	CK(status == 1);
	CK(h.wait_irq(h.ctx, 0, 0, &status) == -ETIMEDOUT);
	clock_gettime(CLOCK_MONOTONIC, &ts);
	t0 = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
	CK(h.wait_irq(h.ctx, 1, 200, &status) == 0);
	clock_gettime(CLOCK_MONOTONIC, &ts);
	t1 = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
	CK(t1 - t0 >= 15000u);
	CK(h.irq_clear(h.ctx, 1) == 0);
	CK(h.wait_irq(h.ctx, 1, 0, &status) == -ETIMEDOUT);
	CK(wr(&h, RWVC_WIN_VENC, 0x30, 2) == 0);
	CK(h.wait_irq(h.ctx, 2, 10, &status) == 0);
	CK(status == 0x10);
	CK(h.wait_irq(h.ctx, 9, 0, &status) == -EINVAL);
	CK(h.irq_clear(h.ctx, 9) == -EINVAL);
	h.close(h.ctx);
}

struct poll_thread_arg {
	struct n2vc_hw *h;
	int rc;
	unsigned done;
};

static void *poll_thread(void *p)
{
	struct poll_thread_arg *a = p;
	struct rwvc_reg_op o = { RWVC_OP_POLL, RWVC_WIN_VENC, 0, 0x1c, 1, 1 };
	a->done = 0;
	a->rc = a->h->regs(a->h->ctx, &o, 1, &a->done);
	return NULL;
}

struct irq_thread_arg {
	struct n2vc_hw *h;
	int rc;
	uint32_t status;
};

static void *irq_thread(void *p)
{
	struct irq_thread_arg *a = p;
	a->rc = a->h->wait_irq(a->h->ctx, 2, 500, &a->status);
	return NULL;
}

static void test_poll_thread(const char *dir)
{
	struct n2vc_hw h;
	struct poll_thread_arg a;
	pthread_t th;

	(void)dir;
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	CK(n2vc_hw_fake_set_poll_timeout(&h, 1000000) == 0);
	memset(&a, 0, sizeof a);
	a.h = &h;
	a.rc = -1;
	CK(pthread_create(&th, NULL, poll_thread, &a) == 0);
	msleep(20);
	CK(wr(&h, RWVC_WIN_VENC, 0x1c, 1) == 0);
	CK(pthread_join(th, NULL) == 0);
	CK(a.rc == 0);
	CK(a.done == 1);
	h.close(h.ctx);
}

static void test_irq_thread(const char *dir)
{
	struct n2vc_hw h;
	struct irq_thread_arg a;
	pthread_t th;
	char path[512];

	snprintf(path, sizeof path, "%s/irq.txt", dir);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(n2vc_hw_fake_script(&h, path) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	memset(&a, 0, sizeof a);
	a.h = &h;
	a.rc = -1;
	CK(pthread_create(&th, NULL, irq_thread, &a) == 0);
	msleep(20);
	CK(wr(&h, RWVC_WIN_VENC, 0x30, 2) == 0);
	CK(pthread_join(th, NULL) == 0);
	CK(a.rc == 0);
	CK(a.status == 0x10);
	h.close(h.ctx);
}

static void test_dma(void)
{
	struct n2vc_hw h;
	uint32_t handles[100], pas[100], bytes[100];
	uint8_t buf[64], got[64];
	void *host = NULL;
	uint32_t hbytes = 0;
	uint32_t handle = 0, pa = 0, i, j;

	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(h.dma_alloc(h.ctx, 0, 0, &handle, &pa) == -EINVAL);
	CK(h.dma_alloc(h.ctx, RW_CLAIM_VCODEC_BYTES + 1u, 0, &handle, &pa) == -ENOMEM);
	CK(h.dma_alloc(h.ctx, 4096, 0, &handle, &pa) == 0);
	CK(handle != 0);
	CK(pa >= RW_CLAIM_VCODEC_PA);
	CK(pa + 4096u <= RW_CLAIM_VCODEC_PA + RW_CLAIM_VCODEC_BYTES);
	CK((pa & 4095u) == 0);
	CK(n2vc_hw_fake_dma_host(&h, handle, &host, &hbytes) == 0);
	CK(hbytes == 4096);
	CK(host != NULL);
	for (i = 0; i < sizeof buf; i++) {
		buf[i] = (uint8_t)(i * 7u + 3u);
		CK(((uint8_t *)host)[i] == 0);
	}
	CK(h.dma_rw(h.ctx, handle, 16, buf, sizeof buf, 1) == 0);
	memset(got, 0, sizeof got);
	CK(h.dma_rw(h.ctx, handle, 16, got, sizeof got, 0) == 0);
	CK(memcmp(buf, got, sizeof buf) == 0);
	CK(h.dma_rw(h.ctx, handle, 4090, buf, 16, 1) == -EINVAL);
	CK(h.dma_free(h.ctx, handle) == 0);
	CK(h.dma_free(h.ctx, handle) == -EINVAL);
	CK(h.dma_rw(h.ctx, handle, 0, buf, 4, 1) == -EINVAL);

	/* 100 live buffers: inside the claim, page-aligned, no overlap. */
	for (i = 0; i < 100; i++) {
		uint32_t want = (i * 37u) % 8192u + 1u;
		CK(h.dma_alloc(h.ctx, want, i & 1u, &handles[i], &pas[i]) == 0);
		CK(n2vc_hw_fake_dma_host(&h, handles[i], &host, &bytes[i]) == 0);
		CK(bytes[i] >= want);
		CK((bytes[i] & 4095u) == 0);
		CK(pas[i] >= RW_CLAIM_VCODEC_PA);
		CK(pas[i] + bytes[i] <= RW_CLAIM_VCODEC_PA + RW_CLAIM_VCODEC_BYTES);
		for (j = 0; j < i; j++)
			CK(!(pas[i] < pas[j] + bytes[j] && pas[j] < pas[i] + bytes[i]));
	}
	for (i = 0; i < 100; i++)
		CK(h.dma_free(h.ctx, handles[i]) == 0);
	CK(h.dma_alloc(h.ctx, 4096, 0, &handle, &pa) == 0);
	CK(pa == RW_CLAIM_VCODEC_PA);
	CK(h.dma_free(h.ctx, handle) == 0);
	{
		struct n2vc_hw_fake_stats st;
		n2vc_hw_fake_stats(&h, &st);
		CK(st.dma_allocs == 102);
		CK(st.dma_frees == 102);
	}
	h.close(h.ctx);
}

static void test_chain(const char *dir)
{
	struct n2vc_hw h;
	char path[512];
	unsigned before;

	snprintf(path, sizeof path, "%s/chain.txt", dir);
	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(n2vc_hw_fake_script(&h, path) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	before = n2vc_hw_fake_violations(&h);
	CK(wr(&h, RWVC_WIN_VENC, 0x40, 1) == -ELOOP);
	CK(n2vc_hw_fake_violations(&h) == before + 1);
	h.close(h.ctx);
}

static void test_batch_stop(void)
{
	struct n2vc_hw h;
	struct rwvc_reg_op ops[3];
	unsigned done = 99;

	CK(n2vc_hw_open_fake(&h, NULL) == 0);
	CK(pw(&h, RWVC_BLK_VENC, 1) == 0);
	ops[0].op = RWVC_OP_WRITE;
	ops[0].window = RWVC_WIN_VENC;
	ops[0].reserved = 0;
	ops[0].offset = 0;
	ops[0].value = 7;
	ops[0].mask = 0;
	ops[1].op = RWVC_OP_READ;
	ops[1].window = RWVC_WIN_VENC_C1;      /* unpowered */
	ops[1].reserved = 0;
	ops[1].offset = 0;
	ops[1].value = 0;
	ops[1].mask = 0;
	ops[2].op = RWVC_OP_WRITE;
	ops[2].window = RWVC_WIN_VENC;
	ops[2].reserved = 0;
	ops[2].offset = 4;
	ops[2].value = 9;
	ops[2].mask = 0;
	CK(h.regs(h.ctx, ops, 3, &done) == -EACCES);
	CK(done == 1);
	CK(ops[0].value == 7);
	CK(n2vc_hw_fake_violations(&h) == 1);
	/* The op after the failure did not run. */
	CK(rd(&h, RWVC_WIN_VENC, 4, &ops[0].value) == 0 && ops[0].value == 0);
	h.close(h.ctx);
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s SCRIPT_DIR WORK_DIR\n", argv[0]);
		return 2;
	}
	test_power_and_windows(argv[2]);
	test_power_refs();
	test_record_exact(argv[2]);
	test_script_basic(argv[1]);
	test_script_default(argv[1]);
	test_script_env(argv[1]);
	test_poll(argv[1]);
	test_irq(argv[1]);
	test_poll_thread(argv[1]);
	test_irq_thread(argv[1]);
	test_dma();
	test_chain(argv[1]);
	test_batch_stop();
	printf("N2TEST T02-hw %s %d/%d\n", fails ? "FAIL" : "PASS", checks - fails, checks);
	return fails ? 1 : 0;
}
