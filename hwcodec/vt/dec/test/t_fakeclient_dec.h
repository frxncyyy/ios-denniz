/* SPDX-License-Identifier: MIT */
/*
 * t_fakeclient_dec.h -- a scripted decoder client for the DEC06 object test: the n2vtd_client seam implemented over a
 * per-connection fake daemon, so the object can be driven through StartSession / DecodeFrame / FinishDelayedFrames with
 * exact control over geometry, picture order, held frames and failures.
 */
#ifndef N2VTD_T_FAKECLIENT_DEC_H
#define N2VTD_T_FAKECLIENT_DEC_H

#include <stddef.h>
#include <stdint.h>

#include "n2vtd_client.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tfd_conn;

struct tfd_config {
	uint32_t width, height;      /* display */
	uint32_t stride, buf_h;      /* source geometry */
	uint32_t out_bytes;          /* one output buffer */
	uint32_t dpb;                /* buffers to register */
	uint32_t format;             /* N2VTD_FMT_MM21 / NV12 */
	uint32_t in_bytes;           /* OPEN_OK.in_buf_bytes */
	int connect_fail;            /* connect returns ABSENT */
	int open_fail;               /* dec_open returns HW */
	int header_fail;             /* dec_header returns PROTO */
	int input_fail;              /* dec_input returns HW */
	int auto_picture;            /* 1: each INPUT queues a picture with the input pts and the next index */
	uint32_t auto_index;         /* first index used by auto_picture */
	int vendor_rules;            /* dp1-fix: behave like the phone's vendor decoder (decint, 12 Nov 2026): an HEVC
	                              * DEC_HEADER without a slice NAL fails (E_HW), and INPUTs display nothing until one
	                              * of them carried an SPS in-band (INPUT still answers OK, as on the phone) */
};

/* Install the table (ctx points at the config, which must stay valid). */
void tfd_install(const struct tfd_config *config);

/* The connection the object opened (NULL before StartSession). */
struct tfd_conn *tfd_last_conn(void);

/* Queue a picture independently of INPUT (display-order tests). */
int tfd_push_picture(struct tfd_conn *c, uint64_t pts, uint32_t index);
/* Make the next INPUT answer changed=1 once (geometry change). */
void tfd_set_changed_next(struct tfd_conn *c, int on);
/* Enable/disable the automatic picture per INPUT. */
void tfd_set_auto(struct tfd_conn *c, int on);
/* Tell the next INPUT how many pictures are ready (overrides auto_picture when set). */
void tfd_set_ready(struct tfd_conn *c, int ready);
/* FLUSH answers this many ready pictures and moves the queued pictures there. */
void tfd_set_flush_ready(struct tfd_conn *c, uint32_t ready);

/* Inspection. */
uint32_t tfd_inputs(struct tfd_conn *c);
uint64_t tfd_last_pts(struct tfd_conn *c);
uint32_t tfd_last_au_bytes(struct tfd_conn *c);
const uint8_t *tfd_last_au(struct tfd_conn *c);
uint32_t tfd_registered(struct tfd_conn *c);
uint32_t tfd_released(struct tfd_conn *c);
uint32_t tfd_flushes(struct tfd_conn *c);
int tfd_closed(struct tfd_conn *c);
/* Snapshots taken when the connection was freed (safe after disconnect). */
int tfd_seen_closed(void);
uint32_t tfd_seen_registered(void);
uint32_t tfd_seen_released(void);
uint32_t tfd_seen_flushes(void);
uint32_t tfd_header_bytes(struct tfd_conn *c);
uint32_t tfd_open_format(struct tfd_conn *c);
uint32_t tfd_open_max_w(struct tfd_conn *c);
uint32_t tfd_open_max_h(struct tfd_conn *c);
/* dp1-fix: INPUTs that carried an SPS NAL / INPUTs dropped by vendor_rules. */
uint32_t tfd_inputs_with_sps(struct tfd_conn *c);
uint32_t tfd_inputs_dropped(struct tfd_conn *c);
/* Fill the pattern the fake writes for a delivered picture: MM21 of a width x height picture, Y[x,y] from pts. */
void tfd_pattern(uint64_t pts, uint32_t w, uint32_t h, uint8_t *y, size_t ys, uint8_t *c, size_t cs);

#ifdef __cplusplus
}
#endif
#endif
