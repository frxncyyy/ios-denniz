/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_log.h -- the decoder plug-in's console channel.  VideoToolbox plug-ins must not fprintf() into a process's
 * stderr; the video work already has one channel (gpu/video/vt/n2vt_log.h, n2vt_say).  This wrapper formats one line
 * and forwards it there.  n2vt_say is a weak import: a link without gpu/video/vt/n2vt_log.c produces no output, and a
 * host test can define a strong n2vt_say to capture the lines.
 */
#ifndef N2VTD_LOG_H
#define N2VTD_LOG_H

#if defined(__cplusplus)
extern "C" {
#endif

void n2vtd_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#if defined(__cplusplus)
}
#endif
#endif /* N2VTD_LOG_H */
