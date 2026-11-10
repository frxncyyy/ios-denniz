/* SPDX-License-Identifier: MIT */
/*
 * t_cli.c -- test driver for the R2 deblocking suite.  The same shape as avc/test/core/n2avc_cli.c, but with the one flag the
 * shared CLI does not have yet: --deblock sets N2AVC_F_DEBLOCK.  It writes Annex-B packets and the encoder's reconstruction in
 * the layout the oracle compares: every picture, cropped to W x H, all Y then all Cb then all Cr.
 *
 *   t_cli --size WxH --frames N --qp Q [--keyint K] [--threads T] [--intra-only] [--deblock]
 *         --in in.yuv --out out.264 [--recon recon.yuv]
 */
#include "n2avc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *msg)
{
	if (msg)
		fprintf(stderr, "t_cli: %s\n", msg);
	fprintf(stderr, "usage: t_cli --size WxH --frames N --qp Q [--keyint K] [--threads T] [--intra-only] [--deblock]"
	                " --in in.yuv --out out.264 [--recon recon.yuv]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *in_path = NULL, *out_path = NULL, *recon_path = NULL;
	unsigned width = 0, height = 0, qp = 0, keyint = 0, threads = 0;
	long frames = 0;
	int intra_only = 0, deblock = 0;
	n2avc_config cfg;
	n2avc_enc *enc = NULL;
	FILE *fin = NULL, *fout = NULL, *frecon = NULL;
	uint8_t *frame = NULL;
	size_t frame_size, cw, ch;
	long f;
	int rc = 1;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (!strcmp(a, "--size")) {
			char *end;
			unsigned long w, h;
			if (!v)
				usage("bad --size");
			w = strtoul(v, &end, 10);
			if (end == v || (*end != 'x' && *end != 'X'))
				usage("bad --size");
			h = strtoul(end + 1, &end, 10);
			if (*end || !w || !h || w > 65535 || h > 65535)
				usage("bad --size");
			width = (unsigned)w;
			height = (unsigned)h;
			i++;
		} else if (!strcmp(a, "--frames")) {
			if (!v)
				usage("bad --frames");
			frames = strtol(v, NULL, 10);
			i++;
		} else if (!strcmp(a, "--qp")) {
			if (!v)
				usage("bad --qp");
			qp = (unsigned)strtoul(v, NULL, 10);
			i++;
		} else if (!strcmp(a, "--keyint")) {
			if (!v)
				usage("bad --keyint");
			keyint = (unsigned)strtoul(v, NULL, 10);
			i++;
		} else if (!strcmp(a, "--threads")) {
			if (!v)
				usage("bad --threads");
			threads = (unsigned)strtoul(v, NULL, 10);
			i++;
		} else if (!strcmp(a, "--intra-only")) {
			intra_only = 1;
		} else if (!strcmp(a, "--deblock")) {
			deblock = 1;
		} else if (!strcmp(a, "--in")) {
			in_path = v;
			if (!v)
				usage("bad --in");
			i++;
		} else if (!strcmp(a, "--out")) {
			out_path = v;
			if (!v)
				usage("bad --out");
			i++;
		} else if (!strcmp(a, "--recon")) {
			recon_path = v;
			if (!v)
				usage("bad --recon");
			i++;
		} else {
			usage("unknown argument");
		}
	}
	if (!width || !height || !frames || !qp || !in_path || !out_path)
		usage("missing --size, --frames, --qp, --in or --out");
	if ((width & 1) || (height & 1))
		usage("--size must be even");

	memset(&cfg, 0, sizeof cfg);
	cfg.size = sizeof cfg;
	cfg.width = width;
	cfg.height = height;
	cfg.fps_num = 30;
	cfg.fps_den = 1;
	cfg.qp = qp;
	cfg.keyint = keyint;
	cfg.threads = threads;
	cfg.flags = N2AVC_F_ANNEXB | (intra_only ? N2AVC_F_INTRA_ONLY : 0u) | (deblock ? N2AVC_F_DEBLOCK : 0u);

	frame_size = (size_t)width * (size_t)height * 3u / 2u;
	frame = malloc(frame_size);
	if (!frame) {
		fprintf(stderr, "t_cli: out of memory\n");
		goto done;
	}
	fin = fopen(in_path, "rb");
	fout = fopen(out_path, "wb");
	if (recon_path)
		frecon = fopen(recon_path, "wb");
	if (!fin || !fout || (recon_path && !frecon)) {
		fprintf(stderr, "t_cli: cannot open input/output: %s\n", strerror(errno));
		goto done;
	}
	if (n2avc_open(&cfg, &enc) != N2AVC_OK) {
		fprintf(stderr, "t_cli: n2avc_open failed\n");
		goto done;
	}
	cw = width / 2;
	ch = height / 2;
	for (f = 0; f < frames; f++) {
		n2avc_picture pic;
		n2avc_packet pkt;
		size_t got = fread(frame, 1, frame_size, fin);

		if (got != frame_size) {
			fprintf(stderr, "t_cli: short input at picture %ld\n", f);
			goto done;
		}
		memset(&pic, 0, sizeof pic);
		pic.format = N2AVC_FMT_I420;
		pic.plane[0] = frame;
		pic.stride[0] = width;
		pic.plane[1] = frame + (size_t)width * height;
		pic.stride[1] = (unsigned)cw;
		pic.plane[2] = frame + (size_t)width * height + cw * ch;
		pic.stride[2] = (unsigned)cw;
		pic.pts = f;
		if (n2avc_encode(enc, &pic, 0, &pkt) != N2AVC_OK) {
			fprintf(stderr, "t_cli: n2avc_encode failed at picture %ld\n", f);
			goto done;
		}
		if (fwrite(pkt.data, 1, pkt.size, fout) != pkt.size) {
			fprintf(stderr, "t_cli: write failed\n");
			goto done;
		}
		if (frecon) {
			const uint8_t *pl[3];
			uint32_t str[3];
			unsigned y;

			if (n2avc_reconstruction(enc, pl, str) != N2AVC_OK) {
				fprintf(stderr, "t_cli: n2avc_reconstruction failed\n");
				goto done;
			}
			for (y = 0; y < height; y++)
				fwrite(pl[0] + (size_t)y * str[0], 1, width, frecon);
			for (y = 0; y < ch; y++)
				fwrite(pl[1] + (size_t)y * str[1], 1, cw, frecon);
			for (y = 0; y < ch; y++)
				fwrite(pl[2] + (size_t)y * str[2], 1, cw, frecon);
		}
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
	return rc;
}
