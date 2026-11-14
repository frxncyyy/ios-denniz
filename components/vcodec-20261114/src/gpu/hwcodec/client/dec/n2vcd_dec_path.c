/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_dec_path.c -- path-based connect for the decoder client: P03's transports (the kernel relay
 * on the phone, SOCK_SEQPACKET on the host) created from a path.  Separate from n2vcd_dec.c so a
 * test binary that does not link P03's transport sources can still link the client.
 */
#define _POSIX_C_SOURCE 200809L

#include "n2vcd_dec.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef EPROTO
#define EPROTO 100
#endif

static void
set_err(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (!err || errlen == 0) {
		return;
	}
	va_start(ap, fmt);
	(void)vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

static int
path_status(int r)
{
	switch (r) {
	case -ENOMEM: return N2VCD_E_NOMEM;
	case -EINVAL: return N2VCD_E_ARGS;
	case -EBUSY: return N2VCD_E_BUSY;
	case -EOPNOTSUPP: return N2VCD_E_UNSUPPORTED;
	case -EPROTO: return N2VCD_E_VERSION;
	case -ENXIO:
	case -EPIPE:
	case -ECONNRESET: return N2VCD_E_SESSION;
	default: return N2VCD_E_SESSION;
	}
}

int
n2vcd_dec_connect_path(struct n2vcd_dec_client **out, const char *socket_path, const char *dev_path,
                       uint32_t timeout_ms, char *err, size_t errlen)
{
	struct n2vcd_transport t;
	int r;

	if (!out) {
		return N2VCD_E_ARGS;
	}
	*out = NULL;
	if (socket_path && dev_path) {
		set_err(err, errlen, "connect_path: socket and device are mutually exclusive");
		return N2VCD_E_ARGS;
	}
	memset(&t, 0, sizeof(t));
	if (socket_path) {
		r = n2vcd_transport_socket(&t, socket_path);
	} else {
		r = n2vcd_transport_relay(&t, dev_path);
	}
	if (r != 0) {
		set_err(err, errlen, "connect_path: transport: %d", r);
		return path_status(r);
	}
	r = n2vcd_dec_connect(out, &t, timeout_ms, err, errlen);
	if (r != N2VCD_E_OK) {
		if (t.close) {
			t.close(t.ctx);
		}
		return r;
	}
	return N2VCD_E_OK;
}
