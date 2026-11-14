/* SPDX-License-Identifier: MIT */
/*
 * t_hwlog.c -- X01 host test: the logging decorator against T02's fake
 * hardware, a literal expected log (run_tests.sh diffs it), a log2script
 * round trip, error forwarding, truncation and a threaded run (TSan).
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2vc_hw.h"
#include "n2vc_hw_fake.h"
#include "n2vc_hw_log.h"

static int g_pass, g_total, g_fail;

static void
ck(int cond, const char *fmt, ...)
{
	char msg[320];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	g_total++;
	if (cond) {
		g_pass++;
		printf("X01 CK ok: %s\n", msg);
	} else {
		g_fail++;
		printf("X01 CK FAIL: %s\n", msg);
	}
}

/* ---- deterministic clock ------------------------------------------------- */

struct clock_state {
	uint64_t next;
	uint64_t step;
};

static uint64_t
test_clock(void *arg)
{
	struct clock_state *c = arg;
	uint64_t v = c->next;

	c->next += c->step;
	return v;
}

/* ---- helpers ------------------------------------------------------------- */

static struct rwvc_reg_op
mkop(uint8_t op, unsigned win, uint32_t off, uint32_t value, uint32_t mask)
{
	struct rwvc_reg_op o;

	memset(&o, 0, sizeof o);
	o.op = op;
	o.window = (uint8_t)win;
	o.offset = off;
	o.value = value;
	o.mask = mask;
	return o;
}

static unsigned
count_lines(const char *path)
{
	FILE *f = fopen(path, "r");
	unsigned n = 0;
	int c, last = 0;

	if (f == NULL)
		return 0;
	while ((c = fgetc(f)) != EOF) {
		if (c == '\n')
			n++;
		last = c;
	}
	if (last != 0 && last != '\n')
		n++;
	(void)fclose(f);
	return n;
}

static int
first_line(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	int ok = 0;

	if (f == NULL)
		return 0;
	if (fgets(buf, (int)n, f) != NULL) {
		size_t l = strlen(buf);
		while (l != 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
			buf[--l] = '\0';
		ok = 1;
	}
	(void)fclose(f);
	return ok;
}

static int
last_line(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	char tmp[1024];
	int ok = 0;

	if (f == NULL)
		return 0;
	while (fgets(tmp, (int)sizeof tmp, f) != NULL) {
		size_t l = strlen(tmp);
		while (l != 0 && (tmp[l - 1] == '\n' || tmp[l - 1] == '\r'))
			tmp[--l] = '\0';
		(void)snprintf(buf, n, "%s", tmp);
		ok = 1;
	}
	(void)fclose(f);
	return ok;
}

static int
file_contains(const char *path, const char *needle)
{
	FILE *f = fopen(path, "r");
	char line[1024];
	int found = 0;

	if (f == NULL)
		return 0;
	while (fgets(line, (int)sizeof line, f) != NULL)
		if (strstr(line, needle) != NULL) {
			found = 1;
			break;
		}
	(void)fclose(f);
	return found;
}

static void
fill_pattern(uint8_t *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		p[i] = (uint8_t)(i * 37u + 11u);
}

/* Independent copy of IEEE 802.3 CRC-32 for the observation file. */
static uint32_t
test_crc32(const void *p, size_t n)
{
	const uint8_t *b = p;
	uint32_t c = 0xffffffffu;
	size_t i;
	unsigned k;

	for (i = 0; i < n; i++) {
		c ^= b[i];
		for (k = 0; k < 8; k++)
			c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
	}
	return ~c;
}

static void
obs(FILE *rf, const char *fmt, ...)
{
	va_list ap;

	if (rf == NULL)
		return;
	va_start(ap, fmt);
	(void)vfprintf(rf, fmt, ap);
	va_end(ap);
	(void)fputc('\n', rf);
}

/* ---- the literal op sequence -------------------------------------------- */

static int
run_sequence(const char *script_path, const char *log_path, const char *reads_path,
	     int mutate)
{
	struct clock_state clk = { 1000000u, 1000u };
	struct n2vc_hw hw, loghw;
	struct rwvc_reg_op a[10], b[4];
	unsigned done = 0;
	uint32_t h1 = 0, h2 = 0, pa1 = 0, pa2 = 0, status = 0;
	uint8_t *buf = NULL, *buf2 = NULL;
	FILE *rf = NULL;
	char line[1024];
	int rc;
	unsigned n;

	line[0] = '\0';
	n2vc_hw_log_test_set_clock(test_clock, &clk);
	rc = n2vc_hw_open_fake(&hw, NULL);
	ck(rc == 0, "open fake hardware");
	if (rc != 0)
		return 1;
	rc = n2vc_hw_fake_script(&hw, script_path);
	ck(rc == 0, "load script %s (%s)", script_path,
	   n2vc_hw_fake_last_error(&hw));
	if (rc != 0) {
		hw.close(hw.ctx);
		return 1;
	}
	ck(n2vc_hw_fake_set_poll_timeout(&hw, 5000) == 0, "poll timeout 5000 us");
	rc = n2vc_hw_wrap_log(&loghw, &hw, log_path);
	ck(rc == 0, "n2vc_hw_wrap_log returns 0");
	if (rc != 0) {
		hw.close(hw.ctx);
		return 1;
	}
	rf = fopen(reads_path, "w");
	ck(rf != NULL, "open observations %s", reads_path);
	if (rf == NULL) {
		loghw.close(loghw.ctx);
		return 1;
	}

	ck(loghw.power(loghw.ctx, RWVC_BLK_VENC, 1) == 0, "power VENC on");
	ck(loghw.power(loghw.ctx, RWVC_BLK_VENC_C1, 1) == 0, "power VENC_C1 on");

	a[0] = mkop(RWVC_OP_WRITE, RWVC_WIN_VENC, 0x0010, 0x00000001u, 0);
	a[1] = mkop(RWVC_OP_WRITE_MASK, RWVC_WIN_VENC, 0x0010, 0x00000003u, 0x0000000fu);
	a[2] = mkop(RWVC_OP_READ, RWVC_WIN_VENC, 0x0010, 0, 0);
	a[3] = mkop(RWVC_OP_READ, RWVC_WIN_VENC, 0x0020, 0, 0);
	a[4] = mkop(RWVC_OP_READ, RWVC_WIN_VENC, 0x0024, 0, 0);
	a[5] = mkop(RWVC_OP_READ, RWVC_WIN_VENC, 0x0028, 0, 0);
	a[6] = mkop(RWVC_OP_READ, RWVC_WIN_VENC, 0x0028, 0, 0);
	a[7] = mkop(RWVC_OP_DELAY_US, RWVC_WIN_VENC, 0, 100, 0);
	a[8] = mkop(RWVC_OP_POLL, RWVC_WIN_VENC, 0x0020, 0x12345678u, 0xffffffffu);
	a[9] = mkop(RWVC_OP_WRITE, RWVC_WIN_VENC, 0x0040,
		    mutate ? 0x0000000bu : 0x0000000au, 0);
	done = 999;
	rc = loghw.regs(loghw.ctx, a, 10, &done);
	ck(rc == 0 && done == 10, "batch A rc=%d done=%u", rc, done);
	ck(a[2].value == 0x00000003u, "masked write then read 0x0010 = 3");
	ck(a[3].value == 0x12345678u, "read 0x0020 = 0x12345678 (set rule)");
	ck(a[4].value == 0xdeadbeefu, "read 0x0024 = 0xdeadbeef (read rule)");
	ck(a[5].value == 0x11111111u, "read 0x0028 = 0x11111111 (once rule)");
	ck(a[6].value == 0x00000000u, "second read 0x0028 = array value 0");
	obs(rf, "R 0x0010 0x%08x", (unsigned)a[2].value);
	obs(rf, "R 0x0020 0x%08x", (unsigned)a[3].value);
	obs(rf, "R 0x0024 0x%08x", (unsigned)a[4].value);
	obs(rf, "R 0x0028 0x%08x", (unsigned)a[5].value);
	obs(rf, "R 0x0028 0x%08x", (unsigned)a[6].value);
	obs(rf, "BATCH 0 rc=%d done=%u", rc, done);

	b[0] = mkop(RWVC_OP_WRITE, RWVC_WIN_VENC, 0x0030, 0x00000001u, 0);
	b[1] = mkop(RWVC_OP_POLL, RWVC_WIN_VENC, 0x0034, 0x00000002u, 0x00000002u);
	b[2] = mkop(RWVC_OP_READ, RWVC_WIN_VENC, 0x0034, 0, 0);
	b[3] = mkop(RWVC_OP_POLL, RWVC_WIN_VENC, 0x0038, 0x00000001u, 0x00000001u);
	done = 999;
	rc = loghw.regs(loghw.ctx, b, 4, &done);
	ck(rc == -ETIMEDOUT && done == 3, "batch B timeout rc=%d done=%u", rc, done);
	ck(b[2].value == 0x00000002u, "read 0x0034 = 2 after set rule");
	obs(rf, "R 0x0034 0x%08x", (unsigned)b[2].value);
	obs(rf, "BATCH 1 rc=%d done=%u", rc, done);

	status = 0xdeadbeefu;
	rc = loghw.wait_irq(loghw.ctx, RWVC_IRQ_VENC, 100, &status);
	ck(rc == 0 && status == 0x00000001u, "wait_irq 0 rc=%d status=0x%08x", rc,
	   (unsigned)status);
	obs(rf, "IRQ 0 rc=%d status=0x%08x", rc, (unsigned)status);

	status = 0;
	rc = loghw.wait_irq(loghw.ctx, RWVC_IRQ_VDEC, 20, &status);
	ck(rc == -ETIMEDOUT && status == 0x00000010u,
	   "wait_irq 2 timeout rc=%d status=0x%08x", rc, (unsigned)status);
	obs(rf, "IRQ 2 rc=%d status=0x%08x", rc, (unsigned)status);

	ck(loghw.irq_clear(loghw.ctx, RWVC_IRQ_VENC) == 0, "irq_clear 0");
	status = 0;
	rc = loghw.wait_irq(loghw.ctx, RWVC_IRQ_VENC, 10, &status);
	ck(rc == -ETIMEDOUT && status == 0x00000001u,
	   "wait_irq 0 after clear timeout rc=%d status=0x%08x", rc,
	   (unsigned)status);
	obs(rf, "IRQ 0b rc=%d status=0x%08x", rc, (unsigned)status);

	rc = loghw.dma_alloc(loghw.ctx, 4096, 0, &h1, &pa1);
	ck(rc == 0 && h1 == 1 && pa1 == 0xda800000u,
	   "dma_alloc 4096 rc=%d handle=%u pa=0x%08x", rc, (unsigned)h1,
	   (unsigned)pa1);
	obs(rf, "DMA alloc %u 0x%08x", (unsigned)h1, (unsigned)pa1);
	rc = loghw.dma_alloc(loghw.ctx, 5000, RWVC_DMA_SHARED, &h2, &pa2);
	ck(rc == 0 && h2 == 2 && pa2 == 0xda801000u,
	   "dma_alloc 5000 shared rc=%d handle=%u pa=0x%08x", rc, (unsigned)h2,
	   (unsigned)pa2);
	obs(rf, "DMA alloc %u 0x%08x", (unsigned)h2, (unsigned)pa2);

	buf = malloc(4096);
	buf2 = malloc(4096);
	ck(buf != NULL && buf2 != NULL, "malloc DMA test buffers");
	if (buf != NULL && buf2 != NULL) {
		uint32_t c1, c2, c3;

		fill_pattern(buf, 4096);
		memset(buf2, 0, 4096);
		c1 = test_crc32(buf, 4096);
		c3 = test_crc32(buf, 64);
		ck(c1 == 0xfd7bb204u, "CRC-32 of the 4096-byte pattern");
		ck(test_crc32("123456789", 9) == 0xcbf43926u,
		   "CRC-32 anchor 123456789 = 0xcbf43926");
		rc = loghw.dma_rw(loghw.ctx, h1, 0, buf, 4096, 1);
		ck(rc == 0, "dma_rw h1 write 4096");
		rc = loghw.dma_rw(loghw.ctx, h1, 0, buf2, 4096, 0);
		c2 = test_crc32(buf2, 4096);
		ck(rc == 0 && memcmp(buf, buf2, 4096) == 0 && c2 == c1,
		   "dma_rw h1 read back 4096 with the same CRC");
		rc = loghw.dma_rw(loghw.ctx, h2, 0x100, buf, 64, 1);
		ck(rc == 0, "dma_rw h2 write 64 at 0x100");
		ck(c3 == 0xffbae609u, "CRC-32 of the first 64 pattern bytes");
		obs(rf, "DMA crc 0x%08x 0x%08x 0x%08x", (unsigned)c1,
		    (unsigned)c2, (unsigned)c3);
	} else {
		obs(rf, "DMA crc missing");
	}
	ck(loghw.dma_free(loghw.ctx, h1) == 0, "dma_free h1");
	ck(loghw.dma_free(loghw.ctx, h2) == 0, "dma_free h2");

	ck(loghw.power(loghw.ctx, RWVC_BLK_VENC_C1, 0) == 0, "power VENC_C1 off");
	ck(loghw.power(loghw.ctx, RWVC_BLK_VENC, 0) == 0, "power VENC off");
	ck(n2vc_hw_fake_violations(&hw) == 0, "fake violations 0");

	free(buf);
	free(buf2);
	(void)fclose(rf);
	rf = NULL;
	loghw.close(loghw.ctx);

	n = count_lines(log_path);
	ck(n == 29, "log has 29 lines (got %u)", n);
	ck(first_line(log_path, line, sizeof line) &&
	   strcmp(line, "1000000 POWER VENC on -> 0") == 0,
	   "first log line literal (got '%s')", line);
	ck(last_line(log_path, line, sizeof line) &&
	   strcmp(line, "1028000 POWER VENC off -> 0") == 0,
	   "last log line literal (got '%s')", line);
	return g_fail != 0;
}

/* ---- error forwarding ---------------------------------------------------- */

static void
test_errors(const char *dir)
{
	struct clock_state clk = { 1000000u, 1000u };
	struct n2vc_hw hw, out, bad, noc, lout;
	struct rwvc_reg_op o, one, ro;
	uint32_t status = 0, h = 0, pa = 0;
	unsigned done = 0;
	uint8_t buf[16];
	char p1[512], p2[512], p3[512];

	n2vc_hw_log_test_set_clock(test_clock, &clk);
	(void)snprintf(p1, sizeof p1, "%s/errors.log", dir);
	(void)snprintf(p2, sizeof p2, "%s/errors-close-null.log", dir);
	(void)snprintf(p3, sizeof p3, "%s/errors-alias.log", dir);

	ck(n2vc_hw_wrap_log(NULL, NULL, p1) == -EINVAL, "null out -> EINVAL");
	ck(n2vc_hw_open_fake(&hw, NULL) == 0, "open fake for error tests");
	ck(n2vc_hw_wrap_log(&out, NULL, p1) == -EINVAL, "null inner -> EINVAL");
	ck(n2vc_hw_wrap_log(&out, &hw, NULL) == -EINVAL, "null path -> EINVAL");
	ck(n2vc_hw_wrap_log(&out, &hw, "") == -EINVAL, "empty path -> EINVAL");
	ck(n2vc_hw_wrap_log(&out, &hw, "/nonexistent-n2hw-x01/e.log") == -ENOENT,
	   "missing directory -> -ENOENT");
	bad = hw;
	bad.regs = NULL;
	ck(n2vc_hw_wrap_log(&out, &bad, p1) == -EINVAL,
	   "inner without regs -> EINVAL");

	noc = hw;
	noc.close = NULL;
	ck(n2vc_hw_wrap_log(&lout, &noc, p2) == 0, "wrap with no inner close");
	ck(lout.power(lout.ctx, RWVC_BLK_VENC, 1) == 0, "no-close wrapper forwards");
	lout.close(lout.ctx);
	hw.close(hw.ctx);

	ck(file_contains(p2, "POWER VENC on -> 0"),
	   "no-close wrapper wrote its log");

	{
		struct n2vc_hw self;

		if (n2vc_hw_open_fake(&self, NULL) == 0) {
			ck(n2vc_hw_wrap_log(&self, &self, p3) == 0,
			   "out == inner is accepted");
			ck(self.power(self.ctx, RWVC_BLK_VENC, 1) == 0,
			   "aliased wrapper forwards");
			self.close(self.ctx);
			ck(file_contains(p3, "POWER VENC on -> 0"),
			   "aliased wrapper wrote its log");
		} else {
			ck(0, "open fake for alias test");
		}
	}
	{
		int orc = n2vc_hw_open_fake(&hw, NULL);
		ck(orc == 0, "open second fake");
		if (orc != 0)
			return;
	}
	ck(n2vc_hw_wrap_log(&out, &hw, p1) == 0, "wrap error log");
	ck(out.power(out.ctx, RWVC_BLK_VENC, 0) == -EINVAL,
	   "power off without ref -> EINVAL");
	o = mkop(RWVC_OP_WRITE, RWVC_WIN_VENC, 0x0010, 1, 0);
	done = 999;
	ck(out.regs(out.ctx, &o, 1, &done) == -EACCES && done == 0,
	   "unpowered write -> EACCES done=0");
	ro = mkop(RWVC_OP_READ, RWVC_WIN_VENC, 0x0010, 0, 0);
	done = 999;
	ck(out.regs(out.ctx, &ro, 1, &done) == -EACCES && done == 0,
	   "unpowered read -> EACCES done=0");
	ck(n2vc_hw_fake_violations(&hw) == 2, "fake counted two violations");
	done = 999;
	ck(out.regs(out.ctx, NULL, 0, &done) == 0 && done == 0,
	   "empty batch forwards");
	ck(out.regs(out.ctx, NULL, 0, NULL) == 0,
	   "empty batch with NULL out_done forwards");
	ck(out.wait_irq(out.ctx, RWVC_IRQ_VENC, 5, NULL) == -ETIMEDOUT,
	   "NULL status pointer forwards and times out");
	one = mkop(RWVC_OP_READ, RWVC_WIN_VENC, 0x0010, 0, 0);
	ck(out.regs(out.ctx, &one, RWVC_REG_MAX_OPS + 1, &done) == -EINVAL,
	   "oversized batch -> EINVAL");
	ck(out.wait_irq(out.ctx, 99, 10, &status) == -EINVAL,
	   "bad irq -> EINVAL");
	ck(out.irq_clear(out.ctx, 99) == -EINVAL, "bad irq clear -> EINVAL");
	ck(out.dma_alloc(out.ctx, RW_CLAIM_VCODEC_BYTES + 1, 0, &h, &pa) == -ENOMEM,
	   "oversized dma_alloc -> ENOMEM");
	ck(out.dma_free(out.ctx, 999) == -EINVAL, "unknown dma_free -> EINVAL");
	ck(out.dma_rw(out.ctx, 999, 0, buf, sizeof buf, 1) == -EINVAL,
	   "unknown dma_rw -> EINVAL");
	ck(out.power(out.ctx, RWVC_BLK_VENC, 1) == 0, "power VENC on");
	ck(out.regs(out.ctx, &o, 1, &done) == 0 && done == 1,
	   "powered write forwards");
	ck(out.power(out.ctx, RWVC_BLK_VENC, 0) == 0, "power VENC off");
	out.close(out.ctx);

	ck(file_contains(p1, "POWER VENC off -> -22"), "log has failed power");
	ck(file_contains(p1, "W VENC 0x0010 0x00000001 -> -13"),
	   "log has failed write with result");
	ck(file_contains(p1, "R VENC 0x0010 ? -> -13"),
	   "log has failed read with no value");
	ck(file_contains(p1, "REGS invalid count=2049 ops=set -> -22"),
	   "log has invalid batch marker");
	ck(file_contains(p1, "IRQ wait 99 timeout_ms=10 -> -22"),
	   "log has failed irq wait");
	ck(file_contains(p1, "IRQ wait 0 timeout_ms=5 -> -ETIMEDOUT status=none"),
	   "log records a NULL status as none");
	ck(file_contains(p1, "DMA alloc bytes=") &&
	   file_contains(p1, "flags=0x0 -> -12"), "log has failed dma_alloc");
	ck(file_contains(p1, "DMA free handle=999 -> -22"),
	   "log has failed dma_free");
	ck(file_contains(p1, "DMA rw handle=999") &&
	   !file_contains(p1, "DMA rw handle=999 off=0x0000 bytes=16 W crc32="),
	   "failed dma_rw has no checksum");
	ck(file_contains(p1, "POWER VENC on -> 0"), "log has successful power");
}

/* ---- truncation (built with -DN2HW_HWLOG_MAX_MB=0) ----------------------- */

static void
test_trunc(const char *path)
{
	struct clock_state clk = { 1000000u, 1000u };
	struct n2vc_hw hw, loghw;
	struct rwvc_reg_op o;
	unsigned done;
	char line[1024];
	unsigned n;

	n2vc_hw_log_test_set_clock(test_clock, &clk);
	ck(n2vc_hw_open_fake(&hw, NULL) == 0, "open fake for truncation");
	ck(n2vc_hw_wrap_log(&loghw, &hw, path) == 0, "wrap truncation log");
	ck(loghw.power(loghw.ctx, RWVC_BLK_VENC, 1) == 0, "power on");
	o = mkop(RWVC_OP_WRITE, RWVC_WIN_VENC, 0x0010, 1, 0);
	ck(loghw.regs(loghw.ctx, &o, 1, &done) == 0, "write 1");
	o = mkop(RWVC_OP_WRITE, RWVC_WIN_VENC, 0x0010, 2, 0);
	ck(loghw.regs(loghw.ctx, &o, 1, &done) == 0, "write 2");
	o = mkop(RWVC_OP_WRITE, RWVC_WIN_VENC, 0x0010, 3, 0);
	ck(loghw.regs(loghw.ctx, &o, 1, &done) == 0, "write 3");
	ck(loghw.power(loghw.ctx, RWVC_BLK_VENC, 0) == 0, "power off");
	loghw.close(loghw.ctx);

	n = count_lines(path);
	ck(n == 1, "truncated log has exactly one line (got %u)", n);
	ck(last_line(path, line, sizeof line) &&
	   strcmp(line,
		  "1000000 HWLOG truncated limit_bytes=0 bytes=0 dropped=5") == 0,
	   "truncation last line literal (got '%s')", line);
}

/* ---- threads (TSan) ------------------------------------------------------ */

struct thread_arg {
	struct n2vc_hw *hw;
	unsigned id;
	unsigned iters;
	int failed;
};

static void *
thread_fn(void *p)
{
	struct thread_arg *t = p;
	unsigned i;

	for (i = 0; i < t->iters; i++) {
		struct rwvc_reg_op ops[3];
		uint32_t h = 0, pa = 0;
		uint8_t buf[32], back[32];
		unsigned done = 0;

		if (t->hw->dma_alloc(t->hw->ctx, 4096, 0, &h, &pa) != 0) {
			t->failed = 1;
			break;
		}
		memset(buf, (int)(t->id * 16u + i), sizeof buf);
		if (t->hw->dma_rw(t->hw->ctx, h, 0, buf, sizeof buf, 1) != 0) {
			t->failed = 1;
			break;
		}
		memset(back, 0, sizeof back);
		if (t->hw->dma_rw(t->hw->ctx, h, 0, back, sizeof back, 0) != 0 ||
		    memcmp(buf, back, sizeof buf) != 0) {
			t->failed = 1;
			break;
		}
		if (t->hw->dma_free(t->hw->ctx, h) != 0) {
			t->failed = 1;
			break;
		}
		ops[0] = mkop(RWVC_OP_WRITE, RWVC_WIN_VENC,
			      0x0200u + t->id * 0x10u, i, 0);
		ops[1] = mkop(RWVC_OP_READ, RWVC_WIN_VENC,
			      0x0200u + t->id * 0x10u, 0, 0);
		ops[2] = mkop(RWVC_OP_DELAY_US, RWVC_WIN_VENC, 0, 1, 0);
		if (t->hw->regs(t->hw->ctx, ops, 3, &done) != 0 ||
		    done != 3 || ops[1].value != i) {
			t->failed = 1;
			break;
		}
	}
	return NULL;
}

static void
test_threads(const char *path)
{
	struct n2vc_hw hw, loghw;
	struct thread_arg t0, t1;
	pthread_t th0, th1;
	unsigned n;
	FILE *f;
	char line[1024];
	int bad_prefix = 0, bad_ts = 0;
	uint64_t prev = 0;

	{
		int orc = n2vc_hw_open_fake(&hw, NULL);
		ck(orc == 0, "open fake for threads");
		if (orc != 0)
			return;
	}
	ck(n2vc_hw_wrap_log(&loghw, &hw, path) == 0, "wrap threaded log");
	ck(loghw.power(loghw.ctx, RWVC_BLK_VENC, 1) == 0, "threads power on");

	t0.hw = &loghw;
	t0.id = 0;
	t0.iters = 50;
	t0.failed = 0;
	t1.hw = &loghw;
	t1.id = 1;
	t1.iters = 50;
	t1.failed = 0;
	{
		int c0 = pthread_create(&th0, NULL, thread_fn, &t0);
		int c1 = pthread_create(&th1, NULL, thread_fn, &t1);

		ck(c0 == 0, "create thread 0");
		ck(c1 == 0, "create thread 1");
		if (c0 == 0)
			ck(pthread_join(th0, NULL) == 0, "join thread 0");
		if (c1 == 0)
			ck(pthread_join(th1, NULL) == 0, "join thread 1");
	}
	ck(!t0.failed && !t1.failed, "both threads' operations succeeded");
	ck(n2vc_hw_fake_violations(&hw) == 0, "threads violations 0");
	ck(loghw.power(loghw.ctx, RWVC_BLK_VENC, 0) == 0, "threads power off");
	loghw.close(loghw.ctx);

	n = count_lines(path);
	ck(n == 702, "threaded log line count 702 (got %u)", n);
	f = fopen(path, "r");
	ck(f != NULL, "open threaded log");
	if (f != NULL) {
		while (fgets(line, (int)sizeof line, f) != NULL) {
			char *end = NULL;
			uint64_t ts;
			const char *rest;
			ts = strtoull(line, &end, 10);
			if (end == line || end == NULL || *end != ' ') {
				bad_ts++;
				continue;
			}
			if (ts < prev)
				bad_ts++;
			prev = ts;
			rest = end + 1;
			if (!(strncmp(rest, "POWER ", 6) == 0 ||
			      strncmp(rest, "W ", 2) == 0 ||
			      strncmp(rest, "R ", 2) == 0 ||
			      strncmp(rest, "DELAY ", 6) == 0 ||
			      strncmp(rest, "DMA ", 4) == 0))
				bad_prefix++;
		}
		(void)fclose(f);
	}
	ck(bad_ts == 0, "every line has a parseable, nondecreasing timestamp");
	ck(bad_prefix == 0, "every line starts with a known T02 op token");
}

/* ---- main ---------------------------------------------------------------- */

static void
usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s basic|replay|mutate SCRIPT LOG READS\n"
		"       %s errors DIR\n"
		"       %s threads LOG\n"
		"       %s trunc LOG\n",
		argv0, argv0, argv0, argv0);
}

int
main(int argc, char **argv)
{
	int rc = 2;

	if (argc < 2) {
		usage(argv[0]);
		return 2;
	}
	if ((strcmp(argv[1], "basic") == 0 || strcmp(argv[1], "replay") == 0 ||
	     strcmp(argv[1], "mutate") == 0) && argc == 5) {
		rc = run_sequence(argv[2], argv[3], argv[4],
				  strcmp(argv[1], "mutate") == 0);
	} else if (strcmp(argv[1], "errors") == 0 && argc == 3) {
		test_errors(argv[2]);
		rc = g_fail != 0;
	} else if (strcmp(argv[1], "threads") == 0 && argc == 3) {
		test_threads(argv[2]);
		rc = g_fail != 0;
	} else if (strcmp(argv[1], "trunc") == 0 && argc == 3) {
		test_trunc(argv[2]);
		rc = g_fail != 0;
	} else {
		usage(argv[0]);
		return 2;
	}
	printf("X01 C %s %d/%d\n", g_fail == 0 ? "PASS" : "FAIL", g_pass, g_total);
	return rc;
}
