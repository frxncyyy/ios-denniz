/* SPDX-License-Identifier: MIT */
/*
 * t_support.c -- see t_support.h.  Host only.  Compiled with -DN2FAKE_BUILD so that the fake runtime's object
 * allocator is visible here; that is how the two test-local HEVC format descriptions become real fake CF objects that
 * CFRelease()/CMSampleBufferCreateReady() can handle.
 */
#define N2FAKE_BUILD 1
#include "fake_apple.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t_support.h"

int t_checks;
int t_failures;

int t_report(const char *name)
{
	if (t_failures == 0) {
		printf("T_%s PASS %d\n", name, t_checks);
		return 0;
	}
	printf("T_%s FAIL %d of %d\n", name, t_failures, t_checks);
	return 1;
}

uint8_t *t_read_file(const char *path, uint32_t *bytes)
{
	FILE *f;
	long size;
	uint8_t *buf;

	if (bytes != NULL)
		*bytes = 0;
	f = fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "t_support: cannot open %s\n", path);
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	size = ftell(f);
	if (size < 0 || size > 32 * 1024 * 1024) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	buf = (uint8_t *)malloc((size_t)size + 1);
	if (buf == NULL) {
		fclose(f);
		return NULL;
	}
	if (size != 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	if (bytes != NULL)
		*bytes = (uint32_t)size;
	return buf;
}

static uint32_t rd_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int t_vector_load(const char *path, struct t_vector *v)
{
	uint8_t *buf;
	uint32_t bytes = 0, at = 0, i;
	uint32_t magic;

	memset(v, 0, sizeof(*v));
	buf = t_read_file(path, &bytes);
	if (buf == NULL)
		return -1;
	if (bytes < 24 || memcmp(buf, "N2HV", 4) != 0) {
		free(buf);
		return -1;
	}
	magic = rd_u32(buf + 4);
	if (magic != 1) {
		free(buf);
		return -1;
	}
	v->storage = buf;
	v->storage_bytes = bytes;
	v->codec = rd_u32(buf + 8);
	v->width = rd_u32(buf + 12);
	v->height = rd_u32(buf + 16);
	v->extradata_bytes = rd_u32(buf + 20);
	at = 24;
	if (at + v->extradata_bytes > bytes)
		goto bad;
	v->extradata = buf + at;
	at += v->extradata_bytes;
	if (at + 4 > bytes)
		goto bad;
	v->param_count = rd_u32(buf + at);
	at += 4;
	if (v->param_count > 64)
		goto bad;
	v->params = (struct t_param *)calloc(v->param_count ? v->param_count : 1, sizeof(*v->params));
	if (v->params == NULL)
		goto bad;
	for (i = 0; i < v->param_count; i++) {
		uint32_t n;
		if (at + 8 > bytes)
			goto bad;
		v->params[i].type = buf[at];
		n = rd_u32(buf + at + 4);
		at += 8;
		if (at + n > bytes)
			goto bad;
		v->params[i].data = buf + at;
		v->params[i].bytes = n;
		at += n;
	}
	if (at + 4 > bytes)
		goto bad;
	v->frame_count = rd_u32(buf + at);
	at += 4;
	if (v->frame_count > 10000)
		goto bad;
	v->frames = (struct t_frame *)calloc(v->frame_count ? v->frame_count : 1, sizeof(*v->frames));
	if (v->frames == NULL)
		goto bad;
	for (i = 0; i < v->frame_count; i++) {
		uint32_t n;
		if (at + 8 > bytes)
			goto bad;
		v->frames[i].key = rd_u32(buf + at);
		n = rd_u32(buf + at + 4);
		at += 8;
		if (at + n > bytes)
			goto bad;
		v->frames[i].annexb = buf + at;
		v->frames[i].annexb_bytes = n;
		at += n;
		if (at + 4 > bytes)
			goto bad;
		n = rd_u32(buf + at);
		at += 4;
		if (at + n > bytes)
			goto bad;
		v->frames[i].avcc = buf + at;
		v->frames[i].avcc_bytes = n;
		at += n;
	}
	if (at != bytes)
		goto bad;
	return 0;
bad:
	t_vector_free(v);
	return -1;
}

void t_vector_free(struct t_vector *v)
{
	free(v->params);
	free(v->frames);
	free(v->storage);
	memset(v, 0, sizeof(*v));
}

uint32_t t_vector_ps(const struct t_vector *v, struct n2hwenc_ps *out, uint32_t capacity)
{
	uint32_t i, n = 0;

	for (i = 0; i < v->param_count && n < capacity; i++) {
		out[n].type = v->params[i].type;
		out[n].data = v->params[i].data;
		out[n].bytes = v->params[i].bytes;
		n++;
	}
	return n;
}

/* ------------------------------------------------------------------ HEVC format descriptions (test-local) */

#define T_HEVC_MAX_PARAMS 8

struct t_hevc_fd {
	struct n2fake_base base;
	uint32_t           magic;
	int                nal_length;
	size_t             count;
	uint8_t           *bytes[T_HEVC_MAX_PARAMS];
	size_t             sizes[T_HEVC_MAX_PARAMS];
};

static void t_hevc_fd_destroy(struct n2fake_base *base)
{
	struct t_hevc_fd *fd = (struct t_hevc_fd *)base;
	size_t i;

	for (i = 0; i < fd->count && i < T_HEVC_MAX_PARAMS; i++)
		free(fd->bytes[i]);
}

OSStatus CMVideoFormatDescriptionCreateFromHEVCParameterSets(CFAllocatorRef allocator, size_t parameterSetCount,
                                                             const uint8_t *const *parameterSetPointers,
                                                             const size_t *parameterSetSizes,
                                                             int NALUnitHeaderLength,
                                                             CFDictionaryRef extensions,
                                                             CMFormatDescriptionRef *formatDescriptionOut)
{
	struct t_hevc_fd *fd;
	size_t i;

	(void)allocator;
	(void)extensions;
	if (formatDescriptionOut == NULL)
		return -12712;
	if (parameterSetCount < 1 || parameterSetCount > T_HEVC_MAX_PARAMS ||
	    parameterSetPointers == NULL || parameterSetSizes == NULL)
		return -12712;
	if (NALUnitHeaderLength != 1 && NALUnitHeaderLength != 2 && NALUnitHeaderLength != 4)
		return -12712;
	/* A fake CM object (not N2FAKE_TYPE_FORMATDESC: that type's built-in destructor assumes the fake's own layout). */
	fd = (struct t_hevc_fd *)n2fake_object_alloc(sizeof(*fd), N2FAKE_TYPE_CMOBJECT, 1);
	fd->base.destroy_extra = t_hevc_fd_destroy;
	fd->magic = 0x48564d46u;
	fd->nal_length = NALUnitHeaderLength;
	fd->count = parameterSetCount;
	for (i = 0; i < parameterSetCount; i++) {
		if (parameterSetPointers[i] == NULL || parameterSetSizes[i] == 0) {
			CFRelease((CFTypeRef)fd);
			return -12712;
		}
		fd->bytes[i] = (uint8_t *)malloc(parameterSetSizes[i]);
		if (fd->bytes[i] == NULL) {
			CFRelease((CFTypeRef)fd);
			return -12712;
		}
		memcpy(fd->bytes[i], parameterSetPointers[i], parameterSetSizes[i]);
		fd->sizes[i] = parameterSetSizes[i];
	}
	*formatDescriptionOut = (CMFormatDescriptionRef)fd;
	return 0;
}

OSStatus CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(CMFormatDescriptionRef fmt, size_t index,
                                                            const uint8_t **ps, size_t *size, size_t *count,
                                                            int *nalUnitHeaderLength)
{
	struct n2fake_base *base = n2fake_object_check((CFTypeRef)fmt, N2FAKE_TYPE_CMOBJECT,
	                                               "CMVideoFormatDescriptionGetHEVCParameterSetAtIndex");
	struct t_hevc_fd *fd = (struct t_hevc_fd *)base;

	if (fd->magic != 0x48564d46u || index >= fd->count)
		return -12712;
	if (ps != NULL)
		*ps = fd->bytes[index];
	if (size != NULL)
		*size = fd->sizes[index];
	if (count != NULL)
		*count = fd->count;
	if (nalUnitHeaderLength != NULL)
		*nalUnitHeaderLength = fd->nal_length;
	return 0;
}
