/* SPDX-License-Identifier: MIT */
/*
 * n2avc_cli.c -- the command line driver of the WP03 H.264 encoder core; this is the program the oracle (WP10)
 * drives.  It reads raw YUV from a file, codes it with n2avc_* (include/n2avc.h) and writes the packets, the
 * encoder's reconstruction and a small JSON statistics file.
 *
 *   n2avc_cli --size WxH --frames N (--qp Q | --bitrate B) [--keyint K] [--threads T] [--intra-only]
 *             [--annexb] [--nv12] [--fps N/D] --in in.yuv --out out.264 [--recon recon.yuv] [--stats stats.json]
 *
 * --recon is written after every picture in the layout of the input (I420: all Y, then Cb, then Cr, cropped to
 * W x H), which is exactly what ffmpeg's decoder must reproduce byte for byte.
 */
#include "n2avc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *msg)
{
	if (msg)
		fprintf(stderr, "n2avc_cli: %s\n", msg);
	fprintf(stderr,
		"usage: n2avc_cli --size WxH --frames N (--qp Q | --bitrate B) [--keyint K] [--threads T]\n"
		"                 [--intra-only] [--annexb] [--nv12] [--fps N/D]\n"
		"                 --in in.yuv --out out.264 [--recon recon.yuv] [--stats stats.json]\n");
	exit(2);
}

static int parse_size(const char *s, unsigned *w, unsigned *h)
{
	char *end;
	unsigned long a, b;
	a = strtoul(s, &end, 10);
	if (end == s || (*end != 'x' && *end != 'X'))
		return -1;
	b = strtoul(end + 1, &end, 10);
	if (*end != '\0' || a == 0 || b == 0 || a > 65535 || b > 65535)
		return -1;
	*w = (unsigned)a;
	*h = (unsigned)b;
	return 0;
}

int main(int argc, char **argv)
{
	const char *in_path = NULL, *out_path = NULL, *recon_path = NULL, *stats_path = NULL;
	unsigned width = 0, height = 0;
	long frames = 0;
	unsigned qp = 0, bitrate = 0, keyint = 0, threads = 0;
	unsigned fps_num = 30, fps_den = 1;
	int deblock = 0, intra_only = 0, annexb = 0, nv12 = 0;
	n2avc_config cfg;
	n2avc_enc *enc = NULL;
	FILE *fin = NULL, *fout = NULL, *frecon = NULL, *fstats = NULL;
	uint8_t *frame = NULL;
	size_t frame_size, cw, ch;
	int rc = 1;
	long f;
	int first_stats = 1;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (!strcmp(a, "--size")) { if (!v || parse_size(v, &width, &height)) usage("bad --size"); i++; }
		else if (!strcmp(a, "--frames")) { if (!v) usage("bad --frames"); frames = strtol(v, NULL, 10); i++; }
		else if (!strcmp(a, "--qp")) { if (!v) usage("bad --qp"); qp = (unsigned)strtoul(v, NULL, 10); i++; }
		else if (!strcmp(a, "--bitrate")) { if (!v) usage("bad --bitrate"); bitrate = (unsigned)strtoul(v, NULL, 10); i++; }
		else if (!strcmp(a, "--keyint")) { if (!v) usage("bad --keyint"); keyint = (unsigned)strtoul(v, NULL, 10); i++; }
		else if (!strcmp(a, "--threads")) { if (!v) usage("bad --threads"); threads = (unsigned)strtoul(v, NULL, 10); i++; }
		else if (!strcmp(a, "--fps")) {
			char *end;
			if (!v) usage("bad --fps");
			fps_num = (unsigned)strtoul(v, &end, 10);
			if (*end == '/') fps_den = (unsigned)strtoul(end + 1, &end, 10);
			if (*end || fps_den == 0 || fps_num == 0) usage("bad --fps");
			i++;
		}
		else if (!strcmp(a, "--intra-only")) intra_only = 1;
		else if (!strcmp(a, "--deblock")) deblock = 1;         /* (lead) N2AVC_F_DEBLOCK */
		else if (!strcmp(a, "--annexb")) annexb = 1;
		else if (!strcmp(a, "--nv12")) nv12 = 1;
		else if (!strcmp(a, "--in")) { in_path = v; if (!v) usage("bad --in"); i++; }
		else if (!strcmp(a, "--out")) { out_path = v; if (!v) usage("bad --out"); i++; }
		else if (!strcmp(a, "--recon")) { recon_path = v; if (!v) usage("bad --recon"); i++; }
		else if (!strcmp(a, "--stats")) { stats_path = v; if (!v) usage("bad --stats"); i++; }
		else usage("unknown argument");
	}
	if (!width || !frames || !in_path || !out_path)
		usage("missing --size, --frames, --in or --out");
	if ((width & 1) || (height & 1))
		usage("--size must be even");
	if (frames < 0)
		usage("--frames must be positive");

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = width;
	cfg.height = height;
	cfg.fps_num = fps_num;
	cfg.fps_den = fps_den;
	cfg.bitrate = bitrate;
	cfg.qp = qp;
	cfg.keyint = keyint;
	cfg.threads = threads;
	cfg.flags = (annexb ? N2AVC_F_ANNEXB : 0u) | (intra_only ? N2AVC_F_INTRA_ONLY : 0u) | (deblock ? N2AVC_F_DEBLOCK : 0u);
	if (getenv("N2AVC_FULLRANGE"))
		cfg.flags |= N2AVC_F_FULLRANGE;

	frame_size = (size_t)width * (size_t)height * 3u / 2u;

	fin = fopen(in_path, "rb");
	if (!fin) {
		fprintf(stderr, "n2avc_cli: cannot open %s: %s\n", in_path, strerror(errno));
		goto done;
	}
	fout = fopen(out_path, "wb");
	if (!fout) {
		fprintf(stderr, "n2avc_cli: cannot open %s: %s\n", out_path, strerror(errno));
		goto done;
	}
	if (recon_path) {
		frecon = fopen(recon_path, "wb");
		if (!frecon) {
			fprintf(stderr, "n2avc_cli: cannot open %s: %s\n", recon_path, strerror(errno));
			goto done;
		}
	}
	if (stats_path) {
		fstats = fopen(stats_path, "w");
		if (!fstats) {
			fprintf(stderr, "n2avc_cli: cannot open %s: %s\n", stats_path, strerror(errno));
			goto done;
		}
		fprintf(fstats, "[");
	}

	frame = (uint8_t *)malloc(frame_size);
	if (!frame) {
		fprintf(stderr, "n2avc_cli: out of memory\n");
		goto done;
	}
	if (n2avc_open(&cfg, &enc) != N2AVC_OK) {
		fprintf(stderr, "n2avc_cli: n2avc_open failed (size %ux%u)\n", width, height);
		goto done;
	}

	cw = width / 2;
	ch = height / 2;
	for (f = 0; f < frames; f++) {
		n2avc_picture pic;
		n2avc_packet pkt;
		size_t got = fread(frame, 1, frame_size, fin);
		int st;

		if (got == 0 && f > 0)
			break;
		if (got != frame_size) {
			fprintf(stderr, "n2avc_cli: input has %ld complete pictures (%zu bytes short)\n", f, frame_size - got);
			goto done;
		}
		memset(&pic, 0, sizeof pic);
		pic.format = nv12 ? N2AVC_FMT_NV12 : N2AVC_FMT_I420;
		pic.plane[0] = frame;
		pic.stride[0] = width;
		if (nv12) {
			pic.plane[1] = frame + (size_t)width * height;
			pic.stride[1] = width;
		} else {
			pic.plane[1] = frame + (size_t)width * height;
			pic.stride[1] = cw;
			pic.plane[2] = frame + (size_t)width * height + cw * ch;
			pic.stride[2] = cw;
		}
		pic.pts = f;
		st = n2avc_encode(enc, &pic, 0, &pkt);
		if (st != N2AVC_OK) {
			fprintf(stderr, "n2avc_cli: n2avc_encode failed at picture %ld: %d\n", f, st);
			goto done;
		}
		if (fwrite(pkt.data, 1, pkt.size, fout) != pkt.size) {
			fprintf(stderr, "n2avc_cli: write to %s failed\n", out_path);
			goto done;
		}
		if (frecon) {
			const uint8_t *pl[3];
			uint32_t str[3];
			unsigned y;
			if (n2avc_reconstruction(enc, pl, str) != N2AVC_OK) {
				fprintf(stderr, "n2avc_cli: n2avc_reconstruction failed\n");
				goto done;
			}
			for (y = 0; y < height; y++)
				fwrite(pl[0] + (size_t)y * str[0], 1, width, frecon);
			for (y = 0; y < ch; y++)
				fwrite(pl[1] + (size_t)y * str[1], 1, cw, frecon);
			for (y = 0; y < ch; y++)
				fwrite(pl[2] + (size_t)y * str[2], 1, cw, frecon);
		}
		if (fstats) {
			fprintf(fstats, "%s{\"picture\":%ld,\"bytes\":%zu,\"qp\":%u,\"keyframe\":%u,\"frame_num\":%u}",
				first_stats ? "" : ",", f, pkt.size, pkt.qp, pkt.keyframe, pkt.frame_num);
			first_stats = 0;
		}
	}
	if (fstats) {
		fprintf(fstats, "]\n");
		fclose(fstats);
		fstats = NULL;
	}
	rc = 0;

done:
	if (enc)
		n2avc_close(enc);
	free(frame);
	if (fin)
		fclose(fin);
	if (fout && fclose(fout) != 0)
		rc = 1;
	if (frecon)
		fclose(frecon);
	if (fstats)
		fclose(fstats);
	return rc;
}
