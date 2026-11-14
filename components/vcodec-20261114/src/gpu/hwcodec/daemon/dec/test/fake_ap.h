/* SPDX-License-Identifier: MIT */
/*
 * fake_ap.h -- test-only AP side of DEC05: the n2vcd_dec_ap_ops of a VDEC session, modelled on
 * dec1/vdec_ref.c's synchronous session (one output buffer per START, list_disp in display order,
 * list_free when the reference is dropped) and, when a bitstream is available, parsing its SPS
 * through DEC03's n2vld model (tests/hw/vdec/n2vld.c) instead of a private parser.
 *
 * It is a model, not the vendor: the picture flow (reorder delay, flush drain) is scripted so the
 * protocol's 0..n pictures per input, display order, release and resolution change can be
 * exercised without pixels.  With `dma_dir` set (the end-to-end daemon) it reads the access unit
 * from the client's file-backed DMA buffer and writes a deterministic MM21 pattern into each
 * output buffer, which proves the client's buffer plumbing: the daemon itself copies nothing.
 */
#ifndef FAKE_AP_H
#define FAKE_AP_H

#include <stddef.h>
#include <stdint.h>

#include "n2vcd_dec_service.h"

struct fake_ap;

struct fake_ap_geometry {
	uint32_t width, height, stride, buf_h, dpb_frames;
};

struct fake_ap_cfg {
	uint32_t geometry_override;      /* 1: use pic_w/... below instead of parsing the SPS */
	uint32_t pic_w, pic_h;
	uint32_t crop_w, crop_h;
	uint32_t buf_w, buf_h;
	uint32_t dpb_frames;
	uint32_t reorder;                /* pictures held before display (0 = immediate) */
	int swap;                        /* emit pairs in B-frame order (2nd before 1st) */
	const char *dma_dir;             /* NULL = in-process (no buffer I/O) */
	uint32_t marker;                 /* pattern seed written into pictures */
	int guest_stop;                  /* report a guest stop on every check */
	uint32_t guest_code;
	uint32_t changed_after;          /* 1-based input that reports a geometry change, 0 = never */
	uint32_t changed_w, changed_h, changed_buf_h;
};

struct fake_ap *fake_ap_create(const struct fake_ap_cfg *cfg);
void fake_ap_destroy(struct fake_ap *f);
const struct n2vcd_dec_ap_ops *fake_ap_ops(void);

/* Inject an error into the next call of one operation (0 open, 1 header, 2 add, 3 input, 4 picture,
 * 5 release, 6 free_buffers, 7 flush).  A negative errno is returned once. */
void fake_ap_fail_next(struct fake_ap *f, int op, int err);

/* Parse the first SPS of an Annex B access unit through n2vld.  1 = parsed, 0 = no SPS/unsupported. */
int fake_ap_parse_sps(struct fake_ap *f, const void *au, size_t bytes, struct fake_ap_geometry *g);

/* Unit-test form: install the geometry a parse produced, so op_header() answers from the model
 * without a DMA file to read. */
void fake_ap_set_geometry(struct fake_ap *f, const struct fake_ap_geometry *g);

/* Turn the guest-stop report on after a session exists (the service checks it before any request). */
void fake_ap_set_guest_stop(struct fake_ap *f, uint32_t code);

/* Counters and last-call observations. */
struct fake_ap_stats {
	uint32_t opens, closes, headers, adds, inputs, pictures, releases, frees, flushes;
	uint32_t pool_max, outstanding_max;
	uint32_t last_in_bytes;
	uint64_t last_pts;
	uint32_t last_flags;
	uint32_t last_error_map;
	uint32_t pictures_in_flight;
};
void fake_ap_stats(const struct fake_ap *f, struct fake_ap_stats *out);
uint32_t fake_ap_pool_count(const struct fake_ap *f);

/* E2E buffer I/O checks (both only meaningful with dma_dir). */
int fake_ap_read_input(const struct fake_ap *f, uint32_t handle, uint32_t bytes, uint8_t *out);
int fake_ap_check_picture(const struct fake_ap *f, uint32_t handle, uint32_t y_bytes, uint64_t pts,
                          uint32_t index);

#endif /* FAKE_AP_H */
