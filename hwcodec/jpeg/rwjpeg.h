/* SPDX-License-Identifier: MIT */
/*
 * rwjpeg.h -- client library of /dev/redwood-jpeg, the MT6893 hardware JPEG encoder (include/redwood_jpeg.h is the frozen ABI).
 *
 * What this library is for: any iOS user-space client (the VideoToolbox back end jpeg/n2jpeg_hw_vt.c, the rwjpeg_tool command
 * line tool, camera code) that wants a complete baseline JPEG file from NV12 or BGRA pixels.  It does not do the entropy
 * coding itself: the hardware block does.  Everything here is portable C11 (x86-64 Linux host tests, arm64 iOS).
 *
 * Failure model.  The hardware is optional.  Every encode returns
 *
 *   RWJPEG_OK             a complete JPEG file is in out[0..*bytes)
 *   RWJPEG_E_FALLBACK     the device is absent, or it answered with a hardware error: a distinct code the caller uses to
 *                         switch to the software JPEG encoder.  Never returned for a caller mistake.
 *   RWJPEG_E_NOSPC        *bytes was set to the size the call needs; grow the buffer and call again.
 *   RWJPEG_E_INVAL        the caller's arguments are wrong (odd size, too large, stride < width, ...): not a fallback.
 *   RWJPEG_E_NOMEM        allocation failed.
 *   RWJPEG_E_JPEG         a JPEG byte stream is malformed (marker surgery on garbage, or the kernel returned no EOI).
 *
 * rwjpeg_open() returns RWJPEG_E_FALLBACK when there is no device; that is not an error to log on a machine without the block.
 * After RWJPEG_E_FALLBACK from any call the handle is still open (the next call tries the device again) so a caller that keeps
 * one handle for the process lifetime recovers when the device appears.
 *
 * Threading.  A handle is not safe for concurrent calls: rwjpeg_encode_bgra keeps a conversion buffer in it.  The kernel
 * serialises jobs, so one handle per thread (or an external lock) is all that is needed.  close() waits for nothing: the
 * kernel's ioctl is synchronous.
 */
#ifndef RWJPEG_H
#define RWJPEG_H

#include <stddef.h>
#include <stdint.h>

#include "redwood_jpeg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RWJPEG_DEV_PATH "/dev/redwood-jpeg"

typedef struct rwjpeg rwjpeg;

/* ------------------------------------------------------------------ status codes */
enum {
	RWJPEG_OK            =  0,
	RWJPEG_E_INVAL       = -1,      /* caller bug: not a fallback */
	RWJPEG_E_FALLBACK    = -2,      /* device absent or hardware error: use the software encoder */
	RWJPEG_E_NOSPC       = -3,      /* output buffer too small; *bytes = what is needed */
	RWJPEG_E_NOMEM       = -4,
	RWJPEG_E_JPEG        = -5       /* malformed JPEG */
};

/* A short stable name for a status ("ok", "fallback", "too small", ...), for logs.  Never NULL. */
const char *rwjpeg_status_name(int status);

/* ------------------------------------------------------------------ device */
/* The ioctl hook: identical contract to ioctl(2) -- 0 on success, -1 with errno set.  rwjpeg_open() passes the real ioctl;
 * tests pass a function of their own and never open a device node.  `ctx` is copied into the handle. */
typedef int (*rwjpeg_ioctl_fn)(void *ctx, unsigned long request, void *arg);

/* Open the device.  `path` NULL means RWJPEG_DEV_PATH.  On success *out is a handle that rwjpeg_close() frees.
 * RWJPEG_E_FALLBACK: no device (ENOENT/ENODEV/ENXIO/ENOTTY/EACCES/EPERM) or the ABI version is not RWJ_ABI_VERSION; *out is NULL.
 * Any other failure is RWJPEG_E_INVAL (bad path argument) or RWJPEG_E_NOMEM. */
int  rwjpeg_open(rwjpeg **out, const char *path);

/* Test entry point: a handle whose ioctls go to fn(ctx, request, arg).  Same status codes; no file descriptor is opened. */
int  rwjpeg_open_ioctl(rwjpeg **out, rwjpeg_ioctl_fn fn, void *ctx);

void rwjpeg_close(rwjpeg *dev);

/* RWJ_INFO.  Returns RWJPEG_E_FALLBACK when the device cannot answer.  The struct is zeroed first on failure. */
int  rwjpeg_info(rwjpeg *dev, struct rwj_info *info);

/* 1 when the device answers RWJ_INFO with ABI version 1 and the encoder capability bit; 0 otherwise.  Used to decide whether
 * the VideoToolbox hardware object should be registered at all; opens and closes its own handle. */
int  rwjpeg_present(void);

/* ------------------------------------------------------------------ encode */
/* One NV12 (or NV21) picture.  y: height lines of y_stride bytes; c: height/2 lines of c_stride bytes (interleaved CbCr for
 * NV12, CrCb for NV21).  w, h even and 16..RWJ_MAX_*; w*h <= RWJ_MAX_PIXELS; quality 1..100.  `cap` must be at least
 * w*h/2 + 4096 (the kernel's minimum) -- otherwise RWJPEG_E_NOSPC with *bytes = that minimum.  On success *bytes is the JPEG
 * size after cutting at the last FF D9, and *hw_us the block's own time (0 when the ioctl is a test double).
 * `hw_us` may be NULL.  The pixels are copied by the kernel; y and c need not outlive the call. */
int  rwjpeg_encode_nv12(rwjpeg *dev, const void *y, const void *c,
                        uint32_t y_stride, uint32_t c_stride,
                        uint32_t w, uint32_t h, unsigned quality,
                        void *out, size_t cap, size_t *bytes, uint32_t *hw_us);

/* Convenience: NV21 (the other interleaved order) with the same rules. */
int  rwjpeg_encode_nv21(rwjpeg *dev, const void *y, const void *c,
                        uint32_t y_stride, uint32_t c_stride,
                        uint32_t w, uint32_t h, unsigned quality,
                        void *out, size_t cap, size_t *bytes, uint32_t *hw_us);

/* BGRA (little-endian byte order in memory, 4 bytes per pixel, alpha ignored) -> NV12 full-range BT.601, then one encode.
 * The conversion is integer only: Y = (19595 R + 38470 G + 7471 B + 2^15) >> 16, Cb = (32768 B - 11059 R - 21709 G + 2^15) >> 16
 * + 128, Cr = (32768 R - 27439 G - 5329 B + 2^15) >> 16 + 128, each chroma sample the rounded average of the 2x2 block.  These
 * are the JFIF (full-range BT.601) coefficients x 65536; the constants are fixed by that formula, not by the hardware.  Rows
 * beyond `stride` are not read; pixels beyond w/h are not read.  w and h must be even (the hardware refuses odd sizes). */
int  rwjpeg_encode_bgra(rwjpeg *dev, const void *bgra, uint32_t stride,
                        uint32_t w, uint32_t h, unsigned quality,
                        void *out, size_t cap, size_t *bytes, uint32_t *hw_us);

/* The conversion alone, exposed because it is the one piece of pixel arithmetic in this file and a test can check every
 * byte of it against literal pixels: w*h NV12 into nv12 (y plane first, then h/2 chroma lines of nv12 stride).  nv12_stride
 * must be >= w and even.  Returns RWJPEG_OK or RWJPEG_E_INVAL. */
int  rwjpeg_bgra_to_nv12(const void *bgra, uint32_t stride, uint32_t w, uint32_t h,
                         void *nv12, uint32_t nv12_stride);

/* The kernel's quality-percent -> hardware code mapping (redwood_jpeg.h), for logging and for tools; the kernel is
 * authoritative.  quality is clamped to 1..100. */
unsigned rwjpeg_quality_code(unsigned quality);

/* Size of the JPEG at the last FF D9 in [0, bytes): the kernel's documented padding cut.  Returns 0 when the buffer holds no
 * EOI (not a JPEG).  Do not call this on a stream that has already had an EXIF thumbnail inserted into it: the thumbnail has
 * its own EOI (rwjpeg_insert_segments does not trim, it only inserts). */
size_t rwjpeg_eoi_size(const void *jpeg, size_t bytes);

/* ------------------------------------------------------------------ marker surgery */
/*
 * One caller-supplied APPn segment to place directly after SOI.  `data` is the payload AFTER the two-byte length field (an
 * EXIF APP1 payload starts with "Exif\0\0"; an ICC APP2 payload with "ICC_PROFILE\0"), `size` its length (0..65533).  The
 * library writes FF, marker, length = size + 2, payload.  Only 0xE0..0xEF are accepted.
 */
struct rwjpeg_app_segment {
	uint8_t     marker;             /* 0xE0..0xEF */
	uint8_t     reserved[3];        /* zero */
	uint32_t    size;
	const void *data;
};

/*
 * Insert `n` segments directly after the SOI of the complete JPEG in jpeg[0..bytes), in the given order, in place.
 *
 *   - When one of the segments is an EXIF APP1 (marker 0xE1, payload starts "Exif\0\0"), the hardware's leading APP0/JFIF
 *     segment is dropped and the segments go where it was, directly after SOI: EXIF and JFIF both claim to be the first
 *     segment after SOI, and EXIF plus APP0 is invalid.
 *   - Without EXIF the hardware's APP0 stays the first segment and the supplied segments are inserted directly after it, so
 *     the file is still a valid JFIF + APPn file (XMP/ICC).  A hardware stream with no APP0 gets them directly after SOI.
 *   - Nothing else in the stream is touched: the hardware's DQT/SOF/DHT/SOS bytes are copied verbatim.
 *   - The result is written at jpeg[0..new_size); *out_bytes receives new_size on success and on RWJPEG_E_NOSPC, where it is
 *     the size the caller must provide.  The segments and the tail may overlap jpeg (the tail is moved with memmove).
 *
 * Returns RWJPEG_OK, RWJPEG_E_INVAL (not a JPEG -- no SOI --, unknown marker, size > 65533, n > 0 with segments NULL),
 * RWJPEG_E_NOSPC (cap too small; *out_bytes = needed) or RWJPEG_E_JPEG (SOI present but no EOI, or a marker chain that does
 * not parse).
 */
int  rwjpeg_insert_segments(void *jpeg, size_t bytes, size_t cap,
                            const struct rwjpeg_app_segment *segments, size_t n, size_t *out_bytes);

#ifdef __cplusplus
}
#endif
#endif /* RWJPEG_H */
