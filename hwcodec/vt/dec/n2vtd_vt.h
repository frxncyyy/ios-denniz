/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_vt.h -- the decoder plug-in objects and their registration.
 *
 * Two factories with the frozen n2_VTVideoDecoderCreateInstance signature, one per codec, plus the plug-in entry
 * SLIMRegister() that VideoToolbox calls after dlopen("/System/Library/VideoDecoders/Slim.videodecoder").
 */
#ifndef N2VTD_VT_H
#define N2VTD_VT_H

#include <stdint.h>

#include "n2vt_dec_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Factories: createInstance for 'avc1' and 'hvc1'.  Both probe the daemon (short, cached) and return
 * N2_kVTVideoDecoderNotAvailableNowErr when it cannot be reached, which makes VideoToolbox try the next decoder
 * (Apple's H264SW / VCPHEVC).  A StartSession error is NOT retried by VideoToolbox, so nothing slow happens there. */
OSStatus n2vtd_vt_create_avc(uint32_t codecType, CFAllocatorRef allocator, VTVideoDecoderRef *instanceOut);
OSStatus n2vtd_vt_create_hevc(uint32_t codecType, CFAllocatorRef allocator, VTVideoDecoderRef *instanceOut);

/* 1 when the daemon answered within the probe budget (<= 250 ms); the answer is cached for 2 s per process. */
int n2vtd_vt_available(void);
void n2vtd_vt_availability_reset(void);

/* Register 'avc1' and (unless switched off) 'hvc1' with VideoToolbox, honouring the switch files.  Returns how many
 * registrations succeeded (0..2).  Called by SLIMRegister(). */
int n2vtd_vt_register_all(void);

/* Test/staging hook: switch directory (default /private/var/db) and the decision for one codec. */
void n2vtd_register_set_switch_dir(const char *dir);
int n2vtd_register_enabled(int codec_hevc);

/* The plug-in entry point (exported, no arguments), exactly as VTLoadVideoDecoders calls it. */
void SLIMRegister(void);

/* Counters for the host tests and (read-only) diagnostics; no printing from library code. */
struct n2vtd_stats {
	uint64_t sessions_started, sessions_failed, sessions_closed;
	uint64_t frames_in, frames_bad, tokens_pending_dropped, tokens_emitted, tokens_emitted_dropped;
	uint64_t pictures_out, pictures_orphan, pictures_unknown_format, copies_failed;
	uint64_t reconfigures, flushes, client_errors, probe_ok, probe_fail;
	int32_t last_osstatus;
	/* opus-robust: StartSession could not use the hardware and handed the session to VideoToolbox's next decoder
	 * (sw_fallbacks), or tried and that failed too (sw_fallback_failed); OPEN retries after BUSY/RESTART. */
	uint64_t sw_fallbacks, sw_fallback_failed, open_retries;
	/* dp1-fix: access units sent with the session's parameter sets prepended (first input after a configure, and sync
	 * samples without an SPS of their own). */
	uint64_t params_inband;
	/* dp4: HEVC sessions whose DEC_HEADER waited for the first IRAP (headers_deferred) and got it with a slice
	 * (headers_with_slice); non-IRAP samples dropped before it; software handovers inside decodeFrame (before any
	 * picture was emitted). */
	uint64_t headers_deferred, headers_with_slice, pre_irap_dropped, sw_handovers;
};
const struct n2vtd_stats *n2vtd_vt_stats(void);
void n2vtd_vt_stats_reset(void);

/* opus-robust (12 Nov 2026): software fallback inside the object.  VideoToolbox calls a decoder's StartSession once and
 * a failure there fails the whole decompression session (vt/dec/ABI.md section 5).  So when the hardware session
 * cannot be opened in StartSession (vcodecd gone since the factory's probe, BUSY or RESTART for longer than the retry
 * budget, OPEN/DEC_HEADER refused for this size/profile, 10-bit/4:2:2/interlaced parameter sets), the object asks
 * VideoToolbox for the next decoder of the same codec type (VTSelectAndCreateVideoDecoderInstance, while its own
 * factories refuse on this thread), replays the properties VideoToolbox set before StartSession, starts that decoder on
 * the SAME VTVideoDecoderSession, and from then on forwards every class slot to it.  The inner decoder emits the
 * frame tokens itself.  If no other decoder starts, StartSession returns the hardware error as before.
 * Retry budget for OPEN answered BUSY/RESTART (default 1000 ms; 0 = no retry).  Test hook. */
void n2vtd_vt_set_start_retry_ms(uint32_t ms);
/* 1 while this thread is inside the fallback's VTSelectAndCreateVideoDecoderInstance (the factories refuse then). */
int n2vtd_vt_fallback_in_progress(void);

#ifdef __cplusplus
}
#endif
#endif /* N2VTD_VT_H */
