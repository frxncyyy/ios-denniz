/* SPDX-License-Identifier: MIT */
/*
 * t_fakeclient.h -- a canned vcodecd client for the host test of vt/n2hwenc_vt.c.  It plays back the access units of a
 * vector file (mp4_extract.py output) and records what the encoder object asked for, so every open/enc_set/frame call can
 * be checked against literals.
 */
#ifndef T_FAKECLIENT_H
#define T_FAKECLIENT_H

#include <stdint.h>

#include "n2hwenc_client.h"
#include "t_support.h"

struct n2vcd_client;

struct t_fake_set {
	uint32_t bitrate, fps_num, fps_den;
	int      force_key;
};

struct t_fake_client *t_fake_new(const struct t_vector *avc, const struct t_vector *hevc);
void t_fake_free(struct t_fake_client *f);
void t_fake_install(struct t_fake_client *f);
void t_fake_uninstall(void);

void t_fake_fail_connect(struct t_fake_client *f, int on);
void t_fake_fail_open(struct t_fake_client *f, int on);
void t_fake_fail_frame(struct t_fake_client *f, int frame_index);
void t_fake_no_params(struct t_fake_client *f, int on);
/* opus-cam additions */
void t_fake_busy_open(struct t_fake_client *f, int times);
/* cam1-prod: the next `times` OPENs are refused with -EOPNOTSUPP and their parameters recorded. */
void t_fake_refuse_open(struct t_fake_client *f, int times);
int t_fake_refused(const struct t_fake_client *f);
const struct n2hwenc_params *t_fake_refused_params(const struct t_fake_client *f, int index);
void t_fake_restart_open(struct t_fake_client *f, int times);
void t_fake_fail_set(struct t_fake_client *f, int on);
void t_fake_delay(struct t_fake_client *f, unsigned us);
void t_fake_hang_frame(struct t_fake_client *f, int frame_index, unsigned us);
void t_fake_hashes(struct t_fake_client *f, uint64_t *y, uint64_t *c);

int t_fake_connects(const struct t_fake_client *f);
int t_fake_disconnects(const struct t_fake_client *f);
int t_fake_opens(const struct t_fake_client *f);
int t_fake_closes(const struct t_fake_client *f);
int t_fake_sets(const struct t_fake_client *f);
int t_fake_frames(const struct t_fake_client *f);
const struct n2hwenc_params *t_fake_open_params(const struct t_fake_client *f, int index);
const struct t_fake_set *t_fake_set_params(const struct t_fake_client *f, int index);

/* What the client saw on the last enc_frame (for stride/pts/force-key checks). */
struct t_fake_frame_args {
	const uint8_t *y, *cbcr;
	uint32_t luma_stride, chroma_stride;
	uint64_t pts;
	int force_key;
};
const struct t_fake_frame_args *t_fake_last_frame(const struct t_fake_client *f);

#endif /* T_FAKECLIENT_H */
