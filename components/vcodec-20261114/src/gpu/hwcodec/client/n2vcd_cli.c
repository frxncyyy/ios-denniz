/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_cli.c -- command-line client of vcodecd; the phone test tool.
 *
 *   n2vcd_cli enc in.yuv WxH fps bitrate out.h264 [--socket PATH | --dev PATH]
 *   n2vcd_cli info [--socket PATH | --dev PATH]
 *
 * in.yuv is raw NV12 (W*H luma, then (W/2)*(H/2) interleaved UV; 1.5*W*H bytes per frame, back to back).  The
 * output is Annex B.  stdout carries one line per frame and one summary line (see usage); diagnostics go to stderr
 * with the prefix "N2VCD CLI error: ".
 *
 * Exit codes: 0 the whole input was encoded and the output is a complete Annex B stream; 1 library/daemon/hardware
 * failure; 2 usage or input-file problem.
 */
#define _POSIX_C_SOURCE 200809L

#include "n2vcd_client.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(FILE *out)
{
	fprintf(out,
	        "usage: n2vcd_cli enc in.yuv WxH fps bitrate out.h264 [--socket PATH | --dev PATH]\n"
	        "       n2vcd_cli info [--socket PATH | --dev PATH]\n"
	        "\n"
	        "  in.yuv   raw NV12 frames, no header: W*H luma then (W/2)*(H/2) interleaved UV\n"
	        "  WxH      picture size, e.g. 640x480\n"
	        "  fps      \"30\" or \"30000/1001\"\n"
	        "  bitrate  bits per second; k/K = 1000 and M = 1000000 are accepted\n"
	        "  --socket PATH  SOCK_SEQPACKET daemon (host tests); buffers in PATH.buf\n"
	        "  --dev PATH     kernel relay device (default /dev/redwood-vcodec-client)\n"
	        "\n"
	        "stdout per frame:  N2VCD CLI frame N bytes=B key=K hw_us=U\n"
	        "stdout at the end: N2VCD CLI done frames=N bytes=B key=K avg_hw_us=U\n");
}

static int
fail(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "N2VCD CLI error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	return 1;
}

static int
input_fail(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "N2VCD CLI error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	return 2;
}

static int
usage_fail(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "N2VCD CLI error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	usage(stderr);
	return 2;
}

static int
parse_size(const char *s, uint32_t *w, uint32_t *h)
{
	char *e = NULL;
	unsigned long a = strtoul(s, &e, 10);
	if (e == s || (*e != 'x' && *e != 'X') || a == 0 || a > 65535) { return -1; }
	char *e2 = NULL;
	unsigned long b = strtoul(e + 1, &e2, 10);
	if (e2 == e + 1 || *e2 != '\0' || b == 0 || b > 65535) { return -1; }
	*w = (uint32_t)a;
	*h = (uint32_t)b;
	return 0;
}

static int
parse_fps(const char *s, uint32_t *num, uint32_t *den)
{
	char *e = NULL;
	unsigned long a = strtoul(s, &e, 10);
	if (e == s || a == 0 || a > 1000000) { return -1; }
	unsigned long b = 1;
	if (*e == '/') {
		char *e2 = NULL;
		b = strtoul(e + 1, &e2, 10);
		if (e2 == e + 1 || *e2 != '\0' || b == 0 || b > 1000000) { return -1; }
	} else if (*e != '\0') {
		return -1;
	}
	*num = (uint32_t)a;
	*den = (uint32_t)b;
	return 0;
}

static int
parse_bitrate(const char *s, uint32_t *bps)
{
	char *e = NULL;
	unsigned long v = strtoul(s, &e, 10);
	if (e == s) { return -1; }
	unsigned long long vv = (unsigned long long)v;
	if (*e == 'k' || *e == 'K') { vv *= 1000u; e++; }
	else if (*e == 'M') { vv *= 1000000u; e++; }
	if (*e != '\0' || vv == 0 || vv > 400000000ull) { return -1; }
	*bps = (uint32_t)vv;
	return 0;
}

static int
annexb_start(const uint8_t *p, uint32_t n)
{
	if (n >= 4u && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) { return 1; }
	if (n >= 3u && p[0] == 0 && p[1] == 0 && p[2] == 1) { return 1; }
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
		usage(argc < 2 ? stderr : stdout);
		return argc < 2 ? 2 : 0;
	}
	const char *cmd = argv[1];
	int is_enc = strcmp(cmd, "enc") == 0;
	int is_info = strcmp(cmd, "info") == 0;
	if (!is_enc && !is_info) { return usage_fail("unknown command '%s'", cmd); }

	const char *sock = NULL;
	const char *dev = NULL;
	const char *pos[5];
	int npos = 0;
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--socket") == 0) {
			if (i + 1 >= argc) { return usage_fail("--socket needs a path"); }
			if (dev) { return usage_fail("--socket and --dev are mutually exclusive"); }
			sock = argv[++i];
		} else if (strcmp(argv[i], "--dev") == 0) {
			if (i + 1 >= argc) { return usage_fail("--dev needs a path"); }
			if (sock) { return usage_fail("--socket and --dev are mutually exclusive"); }
			dev = argv[++i];
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			usage(stdout);
			return 0;
		} else if (argv[i][0] == '-' && argv[i][1] != '\0') {
			return usage_fail("unknown option '%s'", argv[i]);
		} else {
			if (npos >= 5) { return usage_fail("too many arguments"); }
			pos[npos++] = argv[i];
		}
	}

	/* Validate the command line before touching the daemon, so a usage error is always exit 2. */
	const char *in_path = NULL;
	const char *out_path = NULL;
	uint32_t width = 0, height = 0, fps_num = 0, fps_den = 0, bitrate = 0;
	if (is_enc) {
		if (npos != 5) { return usage_fail("enc needs in.yuv WxH fps bitrate out.h264"); }
		in_path = pos[0];
		out_path = pos[4];
		if (parse_size(pos[1], &width, &height) != 0) {
			return usage_fail("bad picture size '%s' (want WxH)", pos[1]);
		}
		if (parse_fps(pos[2], &fps_num, &fps_den) != 0) {
			return usage_fail("bad frame rate '%s' (want N or N/D)", pos[2]);
		}
		if (parse_bitrate(pos[3], &bitrate) != 0) {
			return usage_fail("bad bitrate '%s' (bits per second, optional k/M)", pos[3]);
		}
		if ((width & 1u) || (height & 1u)) {
			return usage_fail("NV12 needs even width and height");
		}
	} else if (npos != 0) {
		return usage_fail("info takes no arguments");
	}

	struct n2vcd_transport t;
	memset(&t, 0, sizeof(t));
	int tr;
	if (sock) {
		tr = n2vcd_transport_socket(&t, sock);
	} else {
		tr = n2vcd_transport_relay(&t, dev);
	}
	if (tr != 0) {
		return fail("cannot open %s transport: %s", sock ? "socket" : "relay", strerror(-tr));
	}

	char errbuf[256];
	struct n2vcd_client *c = NULL;
	int cr = n2vcd_connect(&c, &t, 0, errbuf, sizeof(errbuf));
	if (cr != N2VCD_E_OK) {
		if (t.close) { t.close(t.ctx); }        /* connect failed: the caller still owns the transport */
		return fail("connect: %s", errbuf);
	}

	struct n2vcd_caps caps;
	(void)n2vcd_info(c, &caps);
	char enc[64];
	int en = 0;
	enc[0] = '\0';
	if (caps.enc_codecs & 1u) { en += snprintf(enc + en, sizeof(enc) - (size_t)en, "avc1"); }
	if (caps.enc_codecs & 2u) { en += snprintf(enc + en, sizeof(enc) - (size_t)en, "%shvc1", en ? "," : ""); }
	if (en == 0) { (void)snprintf(enc, sizeof(enc), "none"); }
	char dec[64];
	int dn = 0;
	dec[0] = '\0';
	if (caps.dec_codecs & 1u) { dn += snprintf(dec + dn, sizeof(dec) - (size_t)dn, "avc1"); }
	if (caps.dec_codecs & 2u) { dn += snprintf(dec + dn, sizeof(dec) - (size_t)dn, "%shvc1", dn ? "," : ""); }
	if (dn == 0) { (void)snprintf(dec, sizeof(dec), "none"); }

	if (is_info) {
		printf("N2VCD CLI info version=%u enc=%s dec=%s max=%ux%u\n", caps.version, enc, dec,
		       caps.max_width, caps.max_height);
		n2vcd_disconnect(c);
		return 0;
	}

	struct n2vcd_enc_params p;
	memset(&p, 0, sizeof(p));
	p.codec = N2VCD_CODEC_H264;
	p.encoder = 1;
	p.width = width;
	p.height = height;
	p.fps_num = fps_num;
	p.fps_den = fps_den;
	p.bitrate = bitrate;

	struct n2vcd_session *s = NULL;
	int r = n2vcd_enc_open(c, &p, &s, errbuf, sizeof(errbuf));
	if (r != N2VCD_E_OK) {
		n2vcd_disconnect(c);
		return fail("open %ux%u: %s", width, height, errbuf);
	}

	FILE *in = fopen(in_path, "rb");
	if (!in) {
		int e = errno;
		(void)n2vcd_close(s);
		n2vcd_disconnect(c);
		return input_fail("cannot open %s: %s", in_path, strerror(e));
	}
	FILE *out = fopen(out_path, "wb");
	if (!out) {
		int e = errno;
		(void)fclose(in);
		(void)n2vcd_close(s);
		n2vcd_disconnect(c);
		return fail("cannot create %s: %s", out_path, strerror(e));
	}

	uint64_t frame_bytes64 = (uint64_t)width * height * 3u / 2u;
	if (frame_bytes64 == 0 || frame_bytes64 > 0x7fffffffu) {
		(void)fclose(out);
		(void)fclose(in);
		(void)n2vcd_close(s);
		n2vcd_disconnect(c);
		return usage_fail("picture too large for one frame buffer");
	}
	uint32_t frame_bytes = (uint32_t)frame_bytes64;
	uint8_t *frame = (uint8_t *)malloc(frame_bytes);
	uint32_t out_cap = n2vcd_session_out_bytes(s);
	uint8_t *aubuf = (uint8_t *)malloc(out_cap ? out_cap : 1u);
	if (!frame || !aubuf) {
		free(frame);
		free(aubuf);
		(void)fclose(out);
		(void)fclose(in);
		(void)n2vcd_close(s);
		n2vcd_disconnect(c);
		return fail("out of memory");
	}

	uint64_t total = 0;
	uint64_t hw_total = 0;
	uint32_t frames = 0;
	uint32_t keys = 0;
	uint8_t last_byte = 0;
	int have_au = 0;
	int status = 0;

	for (;;) {
		size_t got = fread(frame, 1, frame_bytes, in);
		if (got == 0) {
			if (ferror(in)) { status = input_fail("read %s: %s", in_path, strerror(errno)); }
			break;
		}
		if (got != frame_bytes) {
			status = input_fail("%s: partial frame %u (%zu of %u bytes)", in_path, frames, got, frame_bytes);
			break;
		}
		uint64_t pts = (uint64_t)frames * 1000000ull * fps_den / fps_num;
		struct n2vcd_frame_info fi;
		memset(&fi, 0, sizeof(fi));
		r = n2vcd_enc_frame_ex(s, frame, frame + (size_t)width * height, width, width, pts, 0,
		                       aubuf, out_cap, &fi);
		uint32_t bytes = fi.bytes;
		int key = fi.key_frame ? 1 : 0;
		if (r != N2VCD_E_OK) {
			status = fail("frame %u: %s (detail %u, errno %u)", frames, n2vcd_strstatus(r),
			              n2vcd_last_detail(c), n2vcd_last_errno(c));
			break;
		}
		if (bytes == 0 || !annexb_start(aubuf, bytes) || aubuf[bytes - 1u] == 0) {
			status = fail("frame %u: access unit is not a complete Annex B unit (%u bytes)", frames, bytes);
			break;
		}
		if (fwrite(aubuf, 1, bytes, out) != bytes) {
			status = fail("write %s: %s", out_path, strerror(errno));
			break;
		}
		last_byte = aubuf[bytes - 1u];
		have_au = 1;
		total += bytes;
		hw_total += fi.hw_us;
		frames++;
		if (key) { keys++; }
		printf("N2VCD CLI frame %u bytes=%u key=%d hw_us=%u\n", frames - 1u, bytes, key, fi.hw_us);
	}
	if (status == 0 && frames == 0) {
		status = input_fail("%s: no complete NV12 frame of %u bytes", in_path, frame_bytes);
	}
	if (status == 0 && (!have_au || last_byte == 0)) {
		status = fail("output is not a complete Annex B stream");
	}

	int close_status = n2vcd_close(s);
	if (close_status != N2VCD_E_OK && status == 0) {
		status = fail("close: %s", n2vcd_strstatus(close_status));
	}
	n2vcd_disconnect(c);
	(void)fclose(out);
	(void)fclose(in);
	free(frame);
	free(aubuf);

	if (status != 0) { return status; }
	printf("N2VCD CLI done frames=%u bytes=%llu key=%u avg_hw_us=%llu\n", frames,
	       (unsigned long long)total, keys, frames ? (unsigned long long)(hw_total / frames) : 0ull);
	return 0;
}
