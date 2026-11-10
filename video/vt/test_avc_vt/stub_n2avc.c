/* SPDX-License-Identifier: MIT */
/*
 * stub_n2avc.c -- the WP05 test double for include/n2avc.h (host tests only).
 *
 * One picture becomes one fake AVCC NAL: a 4-byte big-endian length of 16, then a 16-byte NAL whose payload carries the
 * picture number (NAL bytes 4..7, big-endian). The first byte is 0x65 (IDR) or 0x41 (non-IDR). The fixed SPS/PPS are the
 * ones named in WP05's TASK.md. Every call records what the glue passed to the real interface; wp05_stub is reset by the
 * test between cases.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "n2avc.h"

struct n2avc_enc {
	uint32_t width, height;
	uint32_t keyint;
	uint32_t pictures;
	uint8_t  packet[20];
};

struct wp05_stub_stats {
	int      opens;
	int      closes;
	int      reconfigures;
	int      fail_next;
	int      fail_open;
	n2avc_config last_config;
	uint32_t reconfigure_bitrate;
	uint32_t reconfigure_fps_num;
	uint32_t reconfigure_fps_den;
};

struct wp05_stub_stats wp05_stub;

/* The TSan test drives two encoders from two threads; the stub's recording is shared state and must be serialised. */
static pthread_mutex_t stub_lock = PTHREAD_MUTEX_INITIALIZER;

static const uint8_t stub_sps[6] = { 0x67, 0x42, 0xC0, 0x1E, 0xD9, 0x00 };
static const uint8_t stub_pps[4] = { 0x68, 0xCE, 0x3C, 0x80 };

int n2avc_open(const n2avc_config *config, n2avc_enc **out)
{
	struct n2avc_enc *enc;

	if (config == NULL || out == NULL || config->size != sizeof(*config) ||
	    config->width < 16 || config->height < 16)
		return N2AVC_E_ARGUMENT;
	pthread_mutex_lock(&stub_lock);
	if (wp05_stub.fail_open) {
		wp05_stub.fail_open = 0;
		pthread_mutex_unlock(&stub_lock);
		return N2AVC_E_MEMORY;
	}
	pthread_mutex_unlock(&stub_lock);
	enc = (struct n2avc_enc *)calloc(1, sizeof(*enc));
	if (enc == NULL)
		return N2AVC_E_MEMORY;
	enc->width = config->width;
	enc->height = config->height;
	enc->keyint = config->keyint == 0 ? 60 : config->keyint;
	pthread_mutex_lock(&stub_lock);
	wp05_stub.opens++;
	wp05_stub.last_config = *config;
	pthread_mutex_unlock(&stub_lock);
	*out = enc;
	return N2AVC_OK;
}

int n2avc_headers(n2avc_enc *enc, const uint8_t **sps, size_t *sps_size, const uint8_t **pps, size_t *pps_size)
{
	if (enc == NULL || sps == NULL || sps_size == NULL || pps == NULL || pps_size == NULL)
		return N2AVC_E_ARGUMENT;
	*sps = stub_sps;
	*sps_size = sizeof(stub_sps);
	*pps = stub_pps;
	*pps_size = sizeof(stub_pps);
	return N2AVC_OK;
}

int n2avc_encode(n2avc_enc *enc, const n2avc_picture *picture, int force_idr, n2avc_packet *out)
{
	uint32_t index;
	uint32_t keyframe;
	uint8_t *nal;

	if (enc == NULL || picture == NULL || out == NULL)
		return N2AVC_E_ARGUMENT;
	pthread_mutex_lock(&stub_lock);
	if (wp05_stub.fail_next) {
		wp05_stub.fail_next = 0;
		pthread_mutex_unlock(&stub_lock);
		return N2AVC_E_MEMORY;
	}
	pthread_mutex_unlock(&stub_lock);

	index = enc->pictures;
	keyframe = (force_idr != 0 || (index % enc->keyint) == 0) ? 1u : 0u;

	enc->packet[0] = 0x00;
	enc->packet[1] = 0x00;
	enc->packet[2] = 0x00;
	enc->packet[3] = 0x10;                 /* AVCC NAL length: 16 */
	nal = enc->packet + 4;
	memset(nal, 0, 16);
	nal[0] = keyframe ? 0x65 : 0x41;
	nal[4] = (uint8_t)(index >> 24);
	nal[5] = (uint8_t)(index >> 16);
	nal[6] = (uint8_t)(index >> 8);
	nal[7] = (uint8_t)index;

	out->data = enc->packet;
	out->size = sizeof(enc->packet);
	out->pts = picture->pts;
	out->keyframe = keyframe;
	out->qp = 26;
	out->frame_num = index;

	enc->pictures++;
	return N2AVC_OK;
}

int n2avc_reconfigure(n2avc_enc *enc, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den)
{
	if (enc == NULL)
		return N2AVC_E_ARGUMENT;
	pthread_mutex_lock(&stub_lock);
	wp05_stub.reconfigures++;
	wp05_stub.reconfigure_bitrate = bitrate;
	wp05_stub.reconfigure_fps_num = fps_num;
	wp05_stub.reconfigure_fps_den = fps_den;
	pthread_mutex_unlock(&stub_lock);
	return N2AVC_OK;
}

int n2avc_reconstruction(n2avc_enc *enc, const uint8_t *plane[3], uint32_t stride[3])
{
	uint32_t y;
	uint32_t chroma_width, chroma_height;

	if (enc == NULL || plane == NULL || stride == NULL)
		return N2AVC_E_ARGUMENT;
	if (plane[0] == NULL || plane[1] == NULL || plane[2] == NULL)
		return N2AVC_E_ARGUMENT;
	chroma_width = (enc->width + 1) / 2;
	chroma_height = (enc->height + 1) / 2;
	for (y = 0; y < enc->height; y++)
		memset((uint8_t *)plane[0] + (size_t)y * stride[0], 0x10, enc->width);
	for (y = 0; y < chroma_height; y++) {
		memset((uint8_t *)plane[1] + (size_t)y * stride[1], 0x80, chroma_width);
		memset((uint8_t *)plane[2] + (size_t)y * stride[2], 0x80, chroma_width);
	}
	return N2AVC_OK;
}

void n2avc_close(n2avc_enc *enc)
{
	if (enc == NULL)
		return;
	free(enc);
	pthread_mutex_lock(&stub_lock);
	wp05_stub.closes++;
	pthread_mutex_unlock(&stub_lock);
}
