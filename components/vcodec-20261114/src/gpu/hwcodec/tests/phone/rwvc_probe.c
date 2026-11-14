/* SPDX-License-Identifier: MIT */
/*
 * rwvc_probe.c -- T02 phone-side read-only probe of /dev/redwood-vcodec.
 *
 * This is the K-vcodec milestone tool: it never writes a codec register
 * except through RWVC_REGS reads, and it powers each block on and off around
 * the read so the kernel's power/clock/larb path is exercised once per block.
 * It prints N2VC lines to stdout (redirect them to a file on the phone).
 *
 *   rwvc_probe info  [dev]   RWVC_INFO only; no power, no register.
 *   rwvc_probe read  [dev]   power VENC on, read 0x00/0x04/0x5c/0x60/0x100/0x104, off.
 *   rwvc_probe all   [dev]   info, then each block on/read one word/off.
 *   rwvc_probe client [dev]  minor 1 only: RWVC_INFO, then the no-server /
 *                            EACCES answers of SEND/RECV/POWER/REGS.  Run it
 *                            only while vcodecd is NOT running.
 *
 * dev defaults to /dev/redwood-vcodec (minor 0, 0600 root).  Exit 0 = all
 * requested operations succeeded, 1 = an ioctl/power/read failed, 2 = usage
 * or open error.  Build for iOS with the same flags as the other hwcodec
 * tools; it only needs libSystem.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "redwood_vcodec.h"

#if defined(__APPLE__)
extern int ioctl(int, unsigned long, ...);
#else
#include <sys/ioctl.h>
#endif

static const char *const win_name[RWVC_WIN_COUNT] = {
	"VENC", "VENC_C1", "VDEC_SYS", "VDEC_BASE", "VDEC_VLD", "VDEC_MC",
	"VDEC_MV", "VDEC_MISC", "VDEC_LAT_MISC", "VDEC_LAT_VLD", "VDEC_RACING",
	"VDEC_SOC_GCON", "VDEC_ALL"
};

static const char *const blk_name[RWVC_BLK_COUNT] = {
	"VENC", "VENC_C1", "VDEC_SOC", "VDEC_CORE"
};

static int xioctl(int fd, unsigned long req, void *arg, const char *what)
{
	int r = ioctl(fd, req, arg);
	if (r != 0)
		printf("N2VC error %s: %s (%d)\n", what, strerror(errno), errno);
	return r;
}

static int info(int fd)
{
	struct rwvc_info i;

	memset(&i, 0, sizeof i);
	if (xioctl(fd, RWVC_INFO, &i, "RWVC_INFO") != 0)
		return -1;
	printf("N2VC info abi=%u powered=0x%x pool_pa=0x%llx pool_bytes=%u pool_free=%u "
	       "irq_mode=%u server=%u\n",
	       i.abi_version, i.powered_mask, (unsigned long long)i.pool_pa, i.pool_bytes,
	       i.pool_free, i.irq_mode, i.server_present);
	return i.abi_version == RWVC_ABI_VERSION ? 0 : -1;
}

static int power(int fd, unsigned block, int on)
{
	struct rwvc_power p;

	p.block = block;
	p.on = on ? 1u : 0u;
	if (xioctl(fd, RWVC_POWER, &p, on ? "RWVC_POWER on" : "RWVC_POWER off") != 0)
		return -1;
	printf("N2VC power %s %s -> 0\n", blk_name[block], on ? "on" : "off");
	return 0;
}

static int read_words(int fd, unsigned win, const uint32_t *offs, unsigned n)
{
	struct rwvc_reg_op ops[8];
	struct rwvc_reg_batch b;
	unsigned i;

	if (n > 8)
		return -1;
	memset(ops, 0, sizeof ops);
	for (i = 0; i < n; i++) {
		ops[i].op = RWVC_OP_READ;
		ops[i].window = (uint8_t)win;
		ops[i].offset = offs[i];
	}
	memset(&b, 0, sizeof b);
	b.ops = (uint64_t)(uintptr_t)ops;
	b.count = n;
	if (xioctl(fd, RWVC_REGS, &b, "RWVC_REGS") != 0)
		return -1;
	printf("N2VC regs %s done=%u/%u\n", win_name[win], b.out_done, b.count);
	for (i = 0; i < b.out_done && i < n; i++)
		printf("N2VC read %s 0x%04x 0x%08x\n", win_name[win], offs[i], ops[i].value);
	return b.out_done == n ? 0 : -1;
}

static int cmd_read(int fd)
{
	static const uint32_t offs[] = { 0x00, 0x04, 0x5c, 0x60, 0x100, 0x104 };
	int rc = 0;

	if (info(fd) != 0)
		return -1;
	if (power(fd, RWVC_BLK_VENC, 1) != 0)
		return -1;
	if (read_words(fd, RWVC_WIN_VENC, offs, sizeof offs / sizeof offs[0]) != 0)
		rc = -1;
	if (power(fd, RWVC_BLK_VENC, 0) != 0)
		rc = -1;
	return rc;
}

struct block_read {
	unsigned block;
	unsigned win;
	uint32_t off;
};

static int cmd_all(int fd)
{
	static const struct block_read br[] = {
		{ RWVC_BLK_VENC, RWVC_WIN_VENC, 0x00 },
		{ RWVC_BLK_VENC_C1, RWVC_WIN_VENC_C1, 0x00 },
		{ RWVC_BLK_VDEC_SOC, RWVC_WIN_VDEC_SOC_GCON, 0x00 },
		{ RWVC_BLK_VDEC_CORE, RWVC_WIN_VDEC_SYS, 0x00 },
	};
	unsigned i;
	int rc = 0;

	if (info(fd) != 0)
		return -1;
	for (i = 0; i < sizeof br / sizeof br[0]; i++) {
		if (power(fd, br[i].block, 1) != 0) {
			rc = -1;
			continue;
		}
		if (read_words(fd, br[i].win, &br[i].off, 1) != 0)
			rc = -1;
		if (power(fd, br[i].block, 0) != 0)
			rc = -1;
	}
	printf("N2VC all %s\n", rc == 0 ? "PASS" : "FAIL");
	return rc;
}

static int expect_errno(int r, int want, const char *what)
{
	if (r == 0) {
		printf("N2VC error %s: unexpectedly succeeded\n", what);
		return -1;
	}
	if (errno != want) {
		printf("N2VC error %s: errno %d (%s), wanted %d\n", what, errno, strerror(errno), want);
		return -1;
	}
	printf("N2VC client %s -> errno %d (%s)\n", what, errno, strerror(errno));
	return 0;
}

static int cmd_client(int fd)
{
	struct rwvc_info i;
	struct rwvc_msg m;
	struct rwvc_power p;
	struct rwvc_reg_op op;
	struct rwvc_reg_batch b;
	char buf[8] = { 0 };
	int rc = 0;

	memset(&i, 0, sizeof i);
	if (xioctl(fd, RWVC_INFO, &i, "RWVC_INFO client") != 0)
		return -1;
	printf("N2VC client info abi=%u server=%u irq_mode=%u\n", i.abi_version,
	       i.server_present, i.irq_mode);
	if (i.server_present) {
		printf("N2VC client SKIP relay checks: a server is present\n");
		return 0;
	}
	memset(&m, 0, sizeof m);
	m.user = (uint64_t)(uintptr_t)buf;
	m.bytes = sizeof buf;
	m.timeout_ms = 0;
	if (expect_errno(ioctl(fd, RWVC_MSG_SEND, &m), ENXIO, "RWVC_MSG_SEND no server") != 0)
		rc = -1;
	memset(&m, 0, sizeof m);
	m.user = (uint64_t)(uintptr_t)buf;
	m.bytes = sizeof buf;
	m.timeout_ms = 0;
	if (expect_errno(ioctl(fd, RWVC_MSG_RECV, &m), EAGAIN, "RWVC_MSG_RECV timeout 0") != 0)
		rc = -1;
	p.block = RWVC_BLK_VENC;
	p.on = 1;
	if (expect_errno(ioctl(fd, RWVC_POWER, &p), EACCES, "RWVC_POWER on minor 1") != 0)
		rc = -1;
	memset(&op, 0, sizeof op);
	op.op = RWVC_OP_READ;
	op.window = RWVC_WIN_VENC;
	memset(&b, 0, sizeof b);
	b.ops = (uint64_t)(uintptr_t)&op;
	b.count = 1;
	if (expect_errno(ioctl(fd, RWVC_REGS, &b), EACCES, "RWVC_REGS on minor 1") != 0)
		rc = -1;
	printf("N2VC client verdict %s\n", rc == 0 ? "PASS" : "FAIL");
	return rc;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/redwood-vcodec";
	const char *verb;
	int fd, rc;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s info|read|all|client [device]\n", argv[0]);
		return 2;
	}
	verb = argv[1];
	if (argc == 3)
		dev = argv[2];
	else if (strcmp(verb, "client") == 0)
		dev = "/dev/redwood-vcodec-client";
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		printf("N2VC error open %s: %s (%d)\n", dev, strerror(errno), errno);
		return 2;
	}
	if (strcmp(verb, "info") == 0)
		rc = info(fd);
	else if (strcmp(verb, "read") == 0)
		rc = cmd_read(fd);
	else if (strcmp(verb, "all") == 0)
		rc = cmd_all(fd);
	else if (strcmp(verb, "client") == 0)
		rc = cmd_client(fd);
	else {
		fprintf(stderr, "unknown verb %s\n", verb);
		close(fd);
		return 2;
	}
	close(fd);
	printf("N2VC verdict %s\n", rc == 0 ? "PASS" : "FAIL");
	return rc == 0 ? 0 : 1;
}
