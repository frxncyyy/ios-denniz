/* SPDX-License-Identifier: MIT */
/*
 * nord2_mdp_api.h -- 0671: the MDP blit service between bsd/dev/nord2_mdp.inc (C, owns the hardware) and
 * iokit/Kernel/arm/sweet_m2scaler.inc (0672, C++, AppleM2ScalerCSCDriver + /dev/redwood-mdp surface blits).
 * Formats are N2MF_* of nord2_mdp_job.h (1 BGRA, 2 RGBA, 3 NV12, 4 NV21).
 */
#ifndef NORD2_MDP_API_H
#define NORD2_MDP_API_H
#include <stdint.h>
#ifdef N2MDP_HOST
#include <sys/ioctl.h>
#else
#include <sys/ioccom.h>
#endif

#define N2MDP_MAX_PAGES 16384u     /* 64 MiB per image */

struct n2mdp_pimg {
	uint32_t fmt, w, h;
	uint32_t pitch[2], off[2];      /* plane byte offsets from the first page */
	uint8_t  full_range, bt709;
	uint32_t npages;
	const uint64_t *pages;          /* physical address of each 4 KiB page, in order */
};

/* 0690: per-request statistics of nord2_mdp_blit_pages_st (all us): lock wait (pipe lock incl. the shared MDP lock), IOVA map,
 * compile, MDP busy (sum of tile GO -> done), whole run, tiles, first 4 tile times, slowest tile, WAIT polls, pipe (0 / 2),
 * overlap = the other pipe was running a job when this one started, rotation tile width used */
struct n2mdp_bstat {
	uint32_t lockwait_us, map_us, compile_us, hw_us, run_us, tiles, tile_us[4], tile_max_us, polls, pipe, overlap, tile_px;
	uint32_t scaled;                /* 0691: the request needed the scale lane (its failures do not stop the service) */
};

#ifdef __cplusplus
extern "C" {
#endif
/* capability bits (N2MDP_CAP_*) valid for this boot; 0 = do not use the MDP (stages not passed, killed) */
uint32_t nord2_mdp_caps(void);
/* rot: clockwise quarter turns, flip bit0 = horizontal mirror (before rotation).  0 or errno (ENODEV, ENOTSUP, EINVAL, ENOMEM,
 * ETIMEDOUT, EFAULT); the caller falls back to its own path on any error. */
int nord2_mdp_blit_pages(const struct n2mdp_pimg *src, const struct n2mdp_pimg *dst, uint32_t rot, uint32_t flip, uint32_t *hw_us);
/* 0690: the same with per-request statistics (st may be NULL); two calls can run at once (pipe 0 / pipe 2) */
int nord2_mdp_blit_pages_st(const struct n2mdp_pimg *src, const struct n2mdp_pimg *dst, uint32_t rot, uint32_t flip, struct n2mdp_bstat *st);
/* /dev/redwood-mdp RWMDP_BLIT_SURF goes through this hook, installed by the IOSurface side (0672) */
typedef int (*nord2_mdp_surface_blit_fn)(uint32_t src_id, uint32_t dst_id, uint32_t rot, uint32_t flip, uint32_t *hw_us);
void nord2_mdp_set_surface_hook(nord2_mdp_surface_blit_fn fn);
/* called once from the MDP thread after the stages (caps = nord2_mdp_caps()); installed by 0672 to register AppleM2ScalerCSCDriver */
typedef void (*nord2_mdp_publish_fn)(uint32_t caps);
void nord2_mdp_set_publish_hook(nord2_mdp_publish_fn fn);
#ifdef __cplusplus
}
#endif

/* /dev/redwood-mdp ioctls (group 'M') */
struct rwmdp_info {
	uint32_t version, stage, caps, s1, s2_pass, s2_fail, s3, jobs, job_fail, timeouts, last_us, max_us, rgb_swap, killed, probe_state, reserved;
};
struct rwmdp_blit_surf { uint32_t src_id, dst_id, rot, flip, hw_us, result; };
#define RWMDP_INFO      _IOR('M', 1, struct rwmdp_info)
#define RWMDP_BLIT_SURF _IOWR('M', 2, struct rwmdp_blit_surf)
#endif
