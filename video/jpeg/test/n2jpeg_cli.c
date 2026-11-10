/* SPDX-License-Identifier: MIT */
/*
 * n2jpeg_cli -- encode one raw NV12 frame to a JPEG file; the command-line front end of the host tests and oracle.
 *
 * usage: n2jpeg_cli --size WxH --in in.nv12 --out out.jpg [--quality Q] [--restart MCUS] [--offset BYTES]
 *        [--app MARKER:FILE ...]   e.g. --app 225:exif.bin   (marker in decimal, 224..239 or 254)
 * exit:  0 ok, 1 usage/IO error, 2 encoder error.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2jpeg.h"

static void
usage(void)
{
	fprintf(stderr, "usage: n2jpeg_cli --size WxH --in in.nv12 --out out.jpg [--quality Q] [--restart MCUS] "
	                "[--offset BYTES] [--app MARKER:FILE ...]\n");
}

static int
parse_size(const char *text, int *width, int *height)
{
	char *end = NULL;
	long w = strtol(text, &end, 10);

	if (end == text || (*end != 'x' && *end != 'X'))
		return -1;
	{
		long h = strtol(end + 1, &end, 10);

		if (*end != '\0' || h <= 0 || h > 65534)
			return -1;
		*width = (int)w;
		*height = (int)h;
	}
	return 0;
}

static unsigned char *
read_file(const char *path, size_t *size_out)
{
	FILE *f = fopen(path, "rb");
	unsigned char *data;
	long size;

	if (f == NULL) {
		fprintf(stderr, "n2jpeg_cli: cannot open %s: %s\n", path, strerror(errno));
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fprintf(stderr, "n2jpeg_cli: cannot size %s\n", path);
		fclose(f);
		return NULL;
	}
	data = (unsigned char *)malloc((size_t)size ? (size_t)size : 1);
	if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "n2jpeg_cli: cannot read %s\n", path);
		free(data);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*size_out = (size_t)size;
	return data;
}

int
main(int argc, char **argv)
{
	const char *in_path = NULL, *out_path = NULL;
	int width = 0, height = 0, quality = 90;
	long restart = 0, offset = 0;
	n2jpeg_segment segments[16];
	unsigned char *segment_data[16];
	unsigned segment_count = 0;
	unsigned char *frame = NULL;
	size_t frame_size = 0;
	n2jpeg_config config;
	n2jpeg_picture picture;
	n2jpeg_enc *enc = NULL;
	unsigned char *out = NULL;
	size_t out_size = 0;
	int i, status = 1;

	memset(segments, 0, sizeof(segments));
	memset(segment_data, 0, sizeof(segment_data));

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			if (parse_size(argv[++i], &width, &height) != 0) {
				usage();
				goto done;
			}
		} else if (strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
			in_path = argv[++i];
		} else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
			out_path = argv[++i];
		} else if (strcmp(argv[i], "--quality") == 0 && i + 1 < argc) {
			quality = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--restart") == 0 && i + 1 < argc) {
			restart = atol(argv[++i]);
		} else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
			offset = atol(argv[++i]);
		} else if (strcmp(argv[i], "--app") == 0 && i + 1 < argc && segment_count < 16) {
			char *arg = argv[++i];
			char *colon = strchr(arg, ':');
			long marker;
			size_t size;

			if (colon == NULL) {
				usage();
				goto done;
			}
			*colon = '\0';
			marker = strtol(arg, NULL, 10);
			if (marker < 224 || marker > 254) {
				fprintf(stderr, "n2jpeg_cli: marker %ld is not APPn/COM\n", marker);
				goto done;
			}
			segment_data[segment_count] = read_file(colon + 1, &size);
			if (segment_data[segment_count] == NULL)
				goto done;
			segments[segment_count].marker = (uint8_t)marker;
			segments[segment_count].data = segment_data[segment_count];
			segments[segment_count].size = size;
			segment_count++;
		} else {
			usage();
			goto done;
		}
	}

	if (in_path == NULL || out_path == NULL || width <= 0 || height <= 0) {
		usage();
		goto done;
	}

	frame = read_file(in_path, &frame_size);
	if (frame == NULL)
		goto done;
	if (frame_size < (size_t)width * (size_t)height * 3u / 2u) {
		fprintf(stderr, "n2jpeg_cli: %s is %zu bytes, %dx%d NV12 needs %zu\n",
		        in_path, frame_size, width, height, (size_t)width * (size_t)height * 3u / 2u);
		goto done;
	}

	memset(&config, 0, sizeof(config));
	config.size = (uint32_t)sizeof(config);
	config.width = width;
	config.height = height;
	config.quality = quality;
	config.restart_interval = (uint32_t)restart;
	config.segments = segment_count ? segments : NULL;
	config.segment_count = segment_count;

	if (n2jpeg_open(&config, &enc) != N2JPEG_OK) {
		fprintf(stderr, "n2jpeg_cli: n2jpeg_open failed\n");
		goto done;
	}

	memset(&picture, 0, sizeof(picture));
	picture.size = (uint32_t)sizeof(picture);
	picture.y = frame + offset;
	picture.y_stride = (uint32_t)width;
	picture.uv = frame + offset + (size_t)width * (size_t)height;
	picture.uv_stride = (uint32_t)width;

	out_size = n2jpeg_bound(enc);
	out = (unsigned char *)malloc(out_size);
	if (out == NULL) {
		fprintf(stderr, "n2jpeg_cli: out of memory (%zu bytes)\n", out_size);
		goto done;
	}
	if (n2jpeg_encode(enc, &picture, out, out_size, &out_size) != N2JPEG_OK) {
		fprintf(stderr, "n2jpeg_cli: n2jpeg_encode failed\n");
		goto done;
	}
	{
		FILE *f = fopen(out_path, "wb");

		if (f == NULL) {
			fprintf(stderr, "n2jpeg_cli: cannot create %s: %s\n", out_path, strerror(errno));
			goto done;
		}
		if (fwrite(out, 1, out_size, f) != out_size) {
			fprintf(stderr, "n2jpeg_cli: short write to %s\n", out_path);
			fclose(f);
			goto done;
		}
		fclose(f);
	}
	printf("n2jpeg_cli: %dx%d quality=%d restart=%ld -> %s (%zu bytes)\n",
	       width, height, quality, restart, out_path, out_size);
	status = 0;

done:
	n2jpeg_close(enc);
	free(out);
	free(frame);
	for (i = 0; i < 16; i++)
		free(segment_data[i]);
	return status;
}
