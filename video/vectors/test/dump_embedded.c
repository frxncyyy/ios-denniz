/* SPDX-License-Identifier: MIT */
/*
 * dump_embedded.c -- write the data of embedded_vectors.h to disk for the
 * independent check in check_vectors.sh.
 *
 * The header is included twice on purpose: everything in it is static const
 * and it has an include guard, so that must compile with -Wall -Wextra.
 *
 * Output in argv[1]:
 *   h264.264   Annex B: SPS, PPS, then every access unit (AVCC lengths removed)
 *   hevc.265   Annex B: VPS, SPS, PPS, then every access unit
 *   jpeg.jpg, heic.heic
 *   info.txt   frame counts, sizes, dimensions, golden luma md5 strings
 */
#include "embedded_vectors.h"
#include "embedded_vectors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t start_code[4] = { 0x00, 0x00, 0x00, 0x01 };

static int put(FILE *f, const void *data, size_t n)
{
	if (n != 0 && fwrite(data, 1, n, f) != n)
		return -1;
	return 0;
}

static int put_file(const char *dir, const char *name, const uint8_t *data, size_t n)
{
	char path[1024];
	FILE *f;
	int rc;
	if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path)
		return -1;
	f = fopen(path, "wb");
	if (f == NULL) {
		perror(path);
		return -1;
	}
	rc = put(f, data, n);
	if (fclose(f) != 0)
		rc = -1;
	return rc;
}

static int put_annexb(const char *dir, const char *name,
		      const uint8_t *const *params, const unsigned *param_sizes,
		      unsigned param_count,
		      const struct n2vec_frame *frames, unsigned frame_count)
{
	char path[1024];
	FILE *f;
	unsigned i;
	if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path)
		return -1;
	f = fopen(path, "wb");
	if (f == NULL) {
		perror(path);
		return -1;
	}
	for (i = 0; i < param_count; i++) {
		if (put(f, start_code, sizeof start_code) != 0 ||
		    put(f, params[i], param_sizes[i]) != 0) {
			fclose(f);
			return -1;
		}
	}
	for (i = 0; i < frame_count; i++) {
		const uint8_t *p = frames[i].data;
		unsigned left = frames[i].size;
		while (left != 0) {
			uint32_t n;
			if (left < 4) {
				fprintf(stderr, "%s: truncated AVCC length\n", name);
				fclose(f);
				return -1;
			}
			n = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
			    ((uint32_t)p[2] << 8) | (uint32_t)p[3];
			p += 4;
			left -= 4;
			if (n == 0 || n > left) {
				fprintf(stderr, "%s: bad AVCC NAL size\n", name);
				fclose(f);
				return -1;
			}
			if (put(f, start_code, sizeof start_code) != 0 ||
			    put(f, p, n) != 0) {
				fclose(f);
				return -1;
			}
			p += n;
			left -= n;
		}
	}
	if (fclose(f) != 0)
		return -1;
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir;
	const uint8_t *h264_params[2];
	unsigned h264_param_sizes[2];
	const uint8_t *hevc_params[3];
	unsigned hevc_param_sizes[3];
	char info[512];
	unsigned i, au_bytes = 0;
	int n;

	if (argc != 2) {
		fprintf(stderr, "usage: %s OUTDIR\n", argv[0]);
		return 2;
	}
	dir = argv[1];

	h264_params[0] = n2vec_h264_sps;
	h264_params[1] = n2vec_h264_pps;
	h264_param_sizes[0] = n2vec_h264_sps_size;
	h264_param_sizes[1] = n2vec_h264_pps_size;
	hevc_params[0] = n2vec_hevc_vps;
	hevc_params[1] = n2vec_hevc_sps;
	hevc_params[2] = n2vec_hevc_pps;
	hevc_param_sizes[0] = n2vec_hevc_vps_size;
	hevc_param_sizes[1] = n2vec_hevc_sps_size;
	hevc_param_sizes[2] = n2vec_hevc_pps_size;

	if (put_annexb(dir, "h264.264", h264_params, h264_param_sizes, 2,
		       n2vec_h264_frames, n2vec_h264_frame_count) != 0 ||
	    put_annexb(dir, "hevc.265", hevc_params, hevc_param_sizes, 3,
		       n2vec_hevc_frames, n2vec_hevc_frame_count) != 0 ||
	    put_file(dir, "jpeg.jpg", n2vec_jpeg, n2vec_jpeg_size) != 0 ||
	    put_file(dir, "heic.heic", n2vec_heic, n2vec_heic_size) != 0)
		return 1;

	for (i = 0; i < n2vec_h264_frame_count; i++)
		au_bytes += n2vec_h264_frames[i].size;
	for (i = 0; i < n2vec_hevc_frame_count; i++)
		au_bytes += n2vec_hevc_frames[i].size;

	n = snprintf(info, sizeof info,
		     "count=%u\n"
		     "streams=%u\nimages=%u\n"
		     "h264_codec=%08x\nh264_ps=%u\nh264_nal=%u\n"
		     "hevc_codec=%08x\nhevc_ps=%u\nhevc_nal=%u\n"
		     "h264_width=%u\nh264_height=%u\nh264_frames=%u\n"
		     "h264_keyframe0=%u\nh264_md5=%s\n"
		     "hevc_width=%u\nhevc_height=%u\nhevc_frames=%u\n"
		     "hevc_keyframe0=%u\nhevc_md5=%s\n"
		     "jpeg_width=%u\njpeg_height=%u\njpeg_size=%u\njpeg_md5=%s\n"
		     "heic_width=%u\nheic_height=%u\nheic_size=%u\nheic_md5=%s\n"
		     "data_bytes=%u\n",
		     n2vec_count(),
		     n2vec_stream_count(), n2vec_image_count(),
		     n2vec_streams[0].codec, n2vec_streams[0].ps_count,
		     n2vec_streams[0].nal_length_size,
		     n2vec_streams[1].codec, n2vec_streams[1].ps_count,
		     n2vec_streams[1].nal_length_size,
		     N2VEC_H264_WIDTH, N2VEC_H264_HEIGHT, n2vec_h264_frame_count,
		     n2vec_h264_frames[0].keyframe, n2vec_h264_luma_md5_first_frame,
		     N2VEC_HEVC_WIDTH, N2VEC_HEVC_HEIGHT, n2vec_hevc_frame_count,
		     n2vec_hevc_frames[0].keyframe, n2vec_hevc_luma_md5_first_frame,
		     N2VEC_JPEG_WIDTH, N2VEC_JPEG_HEIGHT, n2vec_jpeg_size,
		     n2vec_jpeg_luma_md5_first_frame,
		     N2VEC_HEIC_WIDTH, N2VEC_HEIC_HEIGHT, n2vec_heic_size,
		     n2vec_heic_luma_md5_first_frame,
		     au_bytes + n2vec_h264_sps_size + n2vec_h264_pps_size +
		     n2vec_hevc_vps_size + n2vec_hevc_sps_size + n2vec_hevc_pps_size +
		     n2vec_jpeg_size + n2vec_heic_size);
	if (n < 0 || n >= (int)sizeof info)
		return 1;
	if (put_file(dir, "info.txt", (const uint8_t *)info, (size_t)n) != 0)
		return 1;
	return 0;
}
