/* SPDX-License-Identifier: MIT */
/*
 * t_fakeclient.c -- see t_fakeclient.h.  The playback is the vector's per-frame Annex B blob with the parameter sets
 * prepended on key frames, exactly what the vcodecd protocol promises (n2vcd_enc_frame_ok.header_bytes).  Every field the
 * encoder object passes is recorded; the fake never touches a real device or file after the vector is loaded.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1   /* nanosleep under -std=c11 */
#endif
#include "t_fakeclient.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_FAKE_MAX_OPEN 32
#define T_FAKE_MAX_SET 16

/* The fake connection object is opaque to the encoder object; this test defines it for P03's struct name. */
struct n2vcd_client {
	int magic;
};

struct t_fake_session {
	struct t_fake_client *owner;
	const struct t_vector *vec;
	uint32_t              next_frame;
	uint32_t              width, height;
	uint32_t              in_bytes;
	uint32_t              out_bytes;
};

struct t_fake_client {
	const struct t_vector *avc;
	const struct t_vector *hevc;
	struct n2hwenc_client  client;
	struct n2vcd_client   *conn;
	int                    fail_connect;
	int                    fail_open;
	int                    fail_frame;
	int                    no_params;
	int                    refuse_opens;        /* cam1-prod: refuse (E_UNSUPPORTED) this many OPENs, recording each */
	struct n2hwenc_params  refused[8];
	int                    refused_count;
	int                    busy_opens;          /* opus-cam: answer BUSY this many times */
	int                    restart_opens;       /* opus-cam: answer the daemon's restart-on-switch this many times */
	int                    fail_set;
	unsigned               delay_us;            /* every enc_frame sleeps this long */
	int                    delay_frame;         /* -1, or the one frame index that sleeps hang_us */
	unsigned               hang_us;
	uint64_t               y_hash, c_hash;      /* FNV-1a of the last picture's visible bytes */
	int                    connects;
	int                    disconnects;
	int                    opens;
	int                    closes;
	int                    sets;
	int                    frames;
	struct n2hwenc_params  open_params[T_FAKE_MAX_OPEN];
	struct t_fake_set      set_params[T_FAKE_MAX_SET];
	struct t_fake_frame_args last_frame;
	pthread_mutex_t        lock;
};

static void fake_lock(struct t_fake_client *f)
{
	pthread_mutex_lock(&f->lock);
}

static void fake_unlock(struct t_fake_client *f)
{
	pthread_mutex_unlock(&f->lock);
}

static int fc_connect(void *ctx, struct n2vcd_client **conn, const char *socket_path, const char *device_path)
{
	struct t_fake_client *f = (struct t_fake_client *)ctx;

	(void)socket_path;
	(void)device_path;
	fake_lock(f);
	f->connects++;
	if (f->fail_connect) {
		fake_unlock(f);
		return -2;
	}
	if (f->conn == NULL)
		f->conn = (struct n2vcd_client *)calloc(1, sizeof(*f->conn));
	if (f->conn == NULL) {
		fake_unlock(f);
		return -12;
	}
	f->conn->magic = 0x1234;
	*conn = f->conn;
	fake_unlock(f);
	return 0;
}

static int fc_enc_open(void *ctx, struct n2vcd_client *conn, const struct n2hwenc_params *p,
                       struct n2vcd_session **session, uint32_t *in_bytes, uint32_t *out_bytes)
{
	struct t_fake_client *f = (struct t_fake_client *)ctx;
	struct t_fake_session *s;
	uint32_t pixels = p->width * p->height;
	const struct t_vector *vec = p->codec == 0x31637661u ? f->avc : f->hevc;

	if (conn == NULL || conn->magic != 0x1234)
		return -22;
	if (vec == NULL)
		return -22;
	fake_lock(f);
	if (f->restart_opens > 0) {
		f->restart_opens--;
		fake_unlock(f);
		return N2HWENC_CLIENT_RESTART;
	}
	if (f->busy_opens > 0) {
		f->busy_opens--;
		fake_unlock(f);
		return N2HWENC_CLIENT_BUSY;
	}
	if (f->refuse_opens > 0) {
		f->refuse_opens--;
		if (f->refused_count < 8)
			f->refused[f->refused_count++] = *p;
		fake_unlock(f);
		return -EOPNOTSUPP;
	}
	if (f->fail_open || f->opens >= T_FAKE_MAX_OPEN) {
		fake_unlock(f);
		return -95;                         /* a refusal that is not retried (e.g. E_UNSUPPORTED) */
	}
	f->open_params[f->opens++] = *p;
	fake_unlock(f);
	s = (struct t_fake_session *)calloc(1, sizeof(*s));
	if (s == NULL)
		return -12;
	s->owner = f;
	s->vec = vec;
	s->width = p->width;
	s->height = p->height;
	s->in_bytes = pixels * 3 / 2 + 4096;
	s->out_bytes = 4u * 1024u * 1024u;      /* larger than any test access unit: the object must not need more */
	if (in_bytes != NULL)
		*in_bytes = s->in_bytes;
	if (out_bytes != NULL)
		*out_bytes = s->out_bytes;
	*session = (struct n2vcd_session *)s;
	return 0;
}

static int fc_enc_frame(void *ctx, struct n2vcd_session *session, const uint8_t *y, const uint8_t *cbcr,
                        uint32_t luma_stride, uint32_t chroma_stride, uint64_t pts, int force_key,
                        uint8_t *out, uint32_t out_capacity, uint32_t *out_bytes, int *key)
{
	struct t_fake_client *f = (struct t_fake_client *)ctx;
	struct t_fake_session *s = (struct t_fake_session *)session;
	const struct t_frame *frame;
	uint32_t at = 0, i;

	if (s == NULL || s->owner != f)
		return -22;
	{
		unsigned sleep_us;
		fake_lock(f);
		sleep_us = f->delay_us;
		if (f->delay_frame >= 0 && (uint32_t)f->delay_frame == s->next_frame)
			sleep_us = f->hang_us;
		fake_unlock(f);
		if (sleep_us != 0) {
			struct timespec ts = { (time_t)(sleep_us / 1000000u), (long)(sleep_us % 1000000u) * 1000L };
			nanosleep(&ts, NULL);
		}
	}
	fake_lock(f);
	{
		/* What the daemon would read: width x height luma, width x height/2 interleaved chroma. */
		uint64_t hy = 1469598103934665603ull, hc = 1469598103934665603ull;
		uint32_t w = s->width, h = s->height, r, c;
		for (r = 0; r < h; r++)
			for (c = 0; c < w; c++)
				hy = (hy ^ y[(size_t)r * luma_stride + c]) * 1099511628211ull;
		for (r = 0; r < h / 2; r++)
			for (c = 0; c < w; c++)
				hc = (hc ^ cbcr[(size_t)r * chroma_stride + c]) * 1099511628211ull;
		f->y_hash = hy;
		f->c_hash = hc;
	}
	f->last_frame.y = y;
	f->last_frame.cbcr = cbcr;
	f->last_frame.luma_stride = luma_stride;
	f->last_frame.chroma_stride = chroma_stride;
	f->last_frame.pts = pts;
	f->last_frame.force_key = force_key;
	f->frames++;
	if (f->fail_frame >= 0 && (uint32_t)f->fail_frame == s->next_frame) {
		fake_unlock(f);
		return -5;                          /* -EIO: the daemon tears the session down */
	}
	if (s->next_frame >= s->vec->frame_count) {
		fake_unlock(f);
		return -5;
	}
	frame = &s->vec->frames[s->next_frame];
	if (frame->key && !f->no_params) {
		for (i = 0; i < s->vec->param_count; i++) {
			static const uint8_t sc[4] = { 0, 0, 0, 1 };
			if (at + 4 + s->vec->params[i].bytes > out_capacity) {
				fake_unlock(f);
				return -28;
			}
			memcpy(out + at, sc, 4);
			at += 4;
			memcpy(out + at, s->vec->params[i].data, s->vec->params[i].bytes);
			at += s->vec->params[i].bytes;
		}
	}
	if (at + frame->annexb_bytes > out_capacity) {
		fake_unlock(f);
		return -28;
	}
	memcpy(out + at, frame->annexb, frame->annexb_bytes);
	at += frame->annexb_bytes;
	if (out_bytes != NULL)
		*out_bytes = at;
	if (key != NULL)
		*key = frame->key;
	s->next_frame++;
	fake_unlock(f);
	return 0;
}

static int fc_enc_set(void *ctx, struct n2vcd_session *session, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den,
                      int force_key)
{
	struct t_fake_client *f = (struct t_fake_client *)ctx;

	(void)session;
	fake_lock(f);
	if (f->fail_set) {
		f->sets++;
		fake_unlock(f);
		return -22;
	}
	if (f->sets < T_FAKE_MAX_SET) {
		f->set_params[f->sets].bitrate = bitrate;
		f->set_params[f->sets].fps_num = fps_num;
		f->set_params[f->sets].fps_den = fps_den;
		f->set_params[f->sets].force_key = force_key;
	}
	f->sets++;
	fake_unlock(f);
	return 0;
}

static int fc_close_session(void *ctx, struct n2vcd_session *session)
{
	struct t_fake_client *f = (struct t_fake_client *)ctx;

	(void)session;
	free(session);
	fake_lock(f);
	f->closes++;
	fake_unlock(f);
	return 0;
}

static int fc_disconnect(void *ctx, struct n2vcd_client *conn)
{
	struct t_fake_client *f = (struct t_fake_client *)ctx;

	(void)conn;
	fake_lock(f);
	f->disconnects++;
	fake_unlock(f);
	return 0;
}

struct t_fake_client *t_fake_new(const struct t_vector *avc, const struct t_vector *hevc)
{
	struct t_fake_client *f = (struct t_fake_client *)calloc(1, sizeof(*f));

	if (f == NULL)
		return NULL;
	f->avc = avc;
	f->hevc = hevc;
	f->fail_frame = -1;
	f->delay_frame = -1;
	if (pthread_mutex_init(&f->lock, NULL) != 0) {
		free(f);
		return NULL;
	}
	f->client.ctx = f;
	f->client.connect = fc_connect;
	f->client.enc_open = fc_enc_open;
	f->client.enc_frame = fc_enc_frame;
	f->client.enc_set = fc_enc_set;
	f->client.close_session = fc_close_session;
	f->client.disconnect = fc_disconnect;
	return f;
}

void t_fake_free(struct t_fake_client *f)
{
	if (f == NULL)
		return;
	free(f->conn);
	pthread_mutex_destroy(&f->lock);
	free(f);
}

void t_fake_install(struct t_fake_client *f)
{
	if (n2hwenc_client_install(&f->client) != 0) {
		fprintf(stderr, "t_fakeclient: install failed\n");
		abort();
	}
}

void t_fake_uninstall(void)
{
	(void)n2hwenc_client_install(NULL);
}

void t_fake_fail_connect(struct t_fake_client *f, int on) { f->fail_connect = on; }
void t_fake_fail_open(struct t_fake_client *f, int on) { f->fail_open = on; }
void t_fake_fail_frame(struct t_fake_client *f, int frame_index) { f->fail_frame = frame_index; }
void t_fake_no_params(struct t_fake_client *f, int on) { f->no_params = on; }
void t_fake_refuse_open(struct t_fake_client *f, int times)
{
	fake_lock(f);
	f->refuse_opens = times;
	f->refused_count = 0;
	fake_unlock(f);
}
int t_fake_refused(const struct t_fake_client *f) { return f->refused_count; }
const struct n2hwenc_params *t_fake_refused_params(const struct t_fake_client *f, int index)
{
	return index >= 0 && index < f->refused_count ? &f->refused[index] : NULL;
}
void t_fake_busy_open(struct t_fake_client *f, int times) { fake_lock(f); f->busy_opens = times; fake_unlock(f); }
void t_fake_restart_open(struct t_fake_client *f, int times) { fake_lock(f); f->restart_opens = times; fake_unlock(f); }
void t_fake_fail_set(struct t_fake_client *f, int on) { fake_lock(f); f->fail_set = on; fake_unlock(f); }
void t_fake_delay(struct t_fake_client *f, unsigned us) { fake_lock(f); f->delay_us = us; fake_unlock(f); }
void t_fake_hang_frame(struct t_fake_client *f, int frame_index, unsigned us)
{
	fake_lock(f);
	f->delay_frame = frame_index;
	f->hang_us = us;
	fake_unlock(f);
}
void t_fake_hashes(struct t_fake_client *f, uint64_t *y, uint64_t *c)
{
	fake_lock(f);
	*y = f->y_hash;
	*c = f->c_hash;
	fake_unlock(f);
}

int t_fake_connects(const struct t_fake_client *f)
{
	int v;
	pthread_mutex_lock((pthread_mutex_t *)&f->lock);
	v = f->connects;
	pthread_mutex_unlock((pthread_mutex_t *)&f->lock);
	return v;
}
int t_fake_disconnects(const struct t_fake_client *f)
{
	int v;
	pthread_mutex_lock((pthread_mutex_t *)&f->lock);
	v = f->disconnects;
	pthread_mutex_unlock((pthread_mutex_t *)&f->lock);
	return v;
}
int t_fake_opens(const struct t_fake_client *f)
{
	int v;
	pthread_mutex_lock((pthread_mutex_t *)&f->lock);
	v = f->opens;
	pthread_mutex_unlock((pthread_mutex_t *)&f->lock);
	return v;
}
int t_fake_closes(const struct t_fake_client *f)
{
	int v;
	pthread_mutex_lock((pthread_mutex_t *)&f->lock);
	v = f->closes;
	pthread_mutex_unlock((pthread_mutex_t *)&f->lock);
	return v;
}
int t_fake_sets(const struct t_fake_client *f)
{
	int v;
	pthread_mutex_lock((pthread_mutex_t *)&f->lock);
	v = f->sets;
	pthread_mutex_unlock((pthread_mutex_t *)&f->lock);
	return v;
}
int t_fake_frames(const struct t_fake_client *f)
{
	int v;
	pthread_mutex_lock((pthread_mutex_t *)&f->lock);
	v = f->frames;
	pthread_mutex_unlock((pthread_mutex_t *)&f->lock);
	return v;
}

const struct n2hwenc_params *t_fake_open_params(const struct t_fake_client *f, int index)
{
	if (index < 0 || index >= f->opens || index >= T_FAKE_MAX_OPEN)
		return NULL;
	return &f->open_params[index];
}

const struct t_fake_set *t_fake_set_params(const struct t_fake_client *f, int index)
{
	if (index < 0 || index >= f->sets || index >= T_FAKE_MAX_SET)
		return NULL;
	return &f->set_params[index];
}

const struct t_fake_frame_args *t_fake_last_frame(const struct t_fake_client *f)
{
	return &f->last_frame;
}
