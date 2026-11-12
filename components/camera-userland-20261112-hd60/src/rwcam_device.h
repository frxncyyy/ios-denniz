#ifndef RWCAM_DEVICE_H
#define RWCAM_DEVICE_H
#include "rwcam_abi.h"
/* the create function BWFigCaptureDeviceVendor is given (signature of H10ISPCaptureDeviceCreate) */
OSStatus redwood_camera_device_create(CFAllocatorRef allocator, CFDictionaryRef options, CMBaseObjectRef *out);
/*
 * A tap in the viewfinder, from mediaserverd's -[BWFigVideoCaptureDevice setFocusModeAutoWithRect:...] / setExposureModeAutoWithRect: (rwcam_diag.c):
 * the rectangle in parts (0 to 1) of the picture the stream gives; what: RWCAM_POINT_*.
 */
enum { RWCAM_POINT_FOCUS = 1, RWCAM_POINT_EXPOSURE = 2, RWCAM_POINT_EXPOSURE_LOCK = 3 };
void rwcam_device_point(int what, double x, double y, double w, double h, bool continuous);
#endif
