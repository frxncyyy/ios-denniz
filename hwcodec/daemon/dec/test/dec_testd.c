/* SPDX-License-Identifier: MIT */
/*
 * dec_testd.c -- test-only standalone decoder daemon: n2vcd_srv (daemon/n2vcd_srv.c, the product
 * socket server) + the DEC05 decoder service + fake_ap.c.  Used by t_dec_e2e.c to drive the real
 * datagram path across a process boundary, including the fatal exit-70 behaviour.
 *
 * Not a product file: the product daemon is hw-ip's vcodecd, which links the same service with the
 * n2ap_vdec adapter instead of fake_ap.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fake_ap.h"
#include "n2vcd_dec_service.h"
#include "n2vcd_srv.h"

static volatile sig_atomic_t g_stop;

static void
on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static uint64_t
now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

struct dstate {
	struct n2vcd_srv *srv;
	int quiet;
};

static int
d_send(void *ctx, uint32_t to, const void *msg, uint32_t bytes)
{
	struct dstate *d = (struct dstate *)ctx;

	return n2vcd_srv_send(d->srv, to, msg, bytes);
}

static void
d_log(void *ctx, const char *line)
{
	struct dstate *d = (struct dstate *)ctx;

	if (!d->quiet) {
		fprintf(stderr, "DEC05D %s\n", line);
	}
}

static void
d_fatal(void *ctx, uint32_t code)
{
	struct dstate *d = (struct dstate *)ctx;

	if (!d->quiet) {
		fprintf(stderr, "DEC05D fatal guest stop code=%u, exiting 70\n", code);
	}
	_exit(70);
}

static void
usage(void)
{
	fprintf(stderr, "usage: dec_testd --socket PATH [--dma-dir DIR] [--idle-ms N] [--reorder N] "
	                "[--marker N] [--guest-stop] [--guest-code N] [--geometry WxH] [--quiet]\n");
}

int
main(int argc, char **argv)
{
	const char *sockpath = NULL;
	const char *dma_dir = NULL;
	uint32_t idle_ms = 0;
	struct fake_ap_cfg fcfg;
	struct dstate d;

	memset(&fcfg, 0, sizeof(fcfg));
	memset(&d, 0, sizeof(d));
	fcfg.marker = 0x5a;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
			sockpath = argv[++i];
		} else if (strcmp(argv[i], "--dma-dir") == 0 && i + 1 < argc) {
			dma_dir = argv[++i];
		} else if (strcmp(argv[i], "--idle-ms") == 0 && i + 1 < argc) {
			idle_ms = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (strcmp(argv[i], "--reorder") == 0 && i + 1 < argc) {
			fcfg.reorder = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (strcmp(argv[i], "--marker") == 0 && i + 1 < argc) {
			fcfg.marker = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (strcmp(argv[i], "--guest-stop") == 0) {
			fcfg.guest_stop = 1;
		} else if (strcmp(argv[i], "--guest-code") == 0 && i + 1 < argc) {
			fcfg.guest_code = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (strcmp(argv[i], "--geometry") == 0 && i + 1 < argc) {
			unsigned w = 0, h = 0;

			if (sscanf(argv[++i], "%ux%u", &w, &h) != 2 || w < 16u || h < 16u) {
				usage();
				return 2;
			}
			fcfg.geometry_override = 1;
			fcfg.pic_w = w;
			fcfg.crop_w = w;
			fcfg.buf_w = (w + 15u) & ~15u;
			fcfg.pic_h = h;
			fcfg.crop_h = h;
			fcfg.buf_h = (h + 31u) & ~31u;
			fcfg.dpb_frames = 7;
		} else if (strcmp(argv[i], "--quiet") == 0) {
			d.quiet = 1;
		} else if (strcmp(argv[i], "--help") == 0) {
			usage();
			return 0;
		} else {
			usage();
			return 2;
		}
	}
	if (!sockpath) {
		usage();
		return 2;
	}
	fcfg.dma_dir = dma_dir;

	struct fake_ap *fake = fake_ap_create(&fcfg);

	if (!fake) {
		fprintf(stderr, "DEC05D fake_ap_create failed\n");
		return 1;
	}
	char err[256] = { 0 };

	d.srv = n2vcd_srv_open_socket(sockpath, err, sizeof(err));
	if (!d.srv) {
		fprintf(stderr, "DEC05D socket: %s\n", err);
		fake_ap_destroy(fake);
		return 1;
	}
	struct n2vcd_dec_service_cfg scfg;

	memset(&scfg, 0, sizeof(scfg));
	scfg.send = d_send;
	scfg.ctx = &d;
	scfg.ap = fake_ap_ops();
	scfg.ap_ctx = fake;
	scfg.log = d_log;
	scfg.fatal = d_fatal;
	scfg.session_idle_ms = idle_ms;
	struct n2vcd_dec_service *svc = n2vcd_dec_service_create(&scfg);

	if (!svc) {
		fprintf(stderr, "DEC05D service_create failed\n");
		n2vcd_srv_close(d.srv);
		fake_ap_destroy(fake);
		return 1;
	}
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGPIPE, SIG_IGN);
	if (!d.quiet) {
		fprintf(stderr, "DEC05D listening %s idle_ms=%u reorder=%u\n", sockpath, idle_ms, fcfg.reorder);
	}
	uint8_t buf[N2VCD_DEC_MSG_MAX];
	int rc = 0;

	while (!g_stop) {
		uint32_t from = 0;
		int n = n2vcd_srv_recv(d.srv, buf, (uint32_t)sizeof(buf), &from, 100);

		if (n > 0) {
			(void)n2vcd_dec_service_handle(svc, from, buf, (uint32_t)n);
		} else if (n == 0) {
			n2vcd_dec_service_client_gone(svc, from);
		} else if (n != -EAGAIN) {
			rc = 1;
			break;
		}
		if (n2vcd_dec_service_tick(svc, now_ms()) != 0) {
			rc = 0;         /* the fatal callback already exited 70 */
			break;
		}
	}
	n2vcd_dec_service_destroy(svc);
	n2vcd_srv_close(d.srv);
	fake_ap_destroy(fake);
	if (!d.quiet) {
		fprintf(stderr, "DEC05D exit %d\n", rc);
	}
	return rc;
}
