/*
 * The Nord 2's three cameras as iOS's capture stack wants cameras: ONE FigCaptureDevice (the object H10ISP.mediacapture makes for an
 * iPhone's ISP) with THREE FigCaptureStreams -- back wide (IMX766), back ultra-wide (IMX355), front (IMX615) -- made for mediaserverd's
 * BWFigCaptureDeviceVendor in place of Apple's create function. CMCapture then lists a source for every entry of the model's
 * AVCaptureSession.plist that has a stream of the matching port type (system/AVCaptureSession.plist adds the ultra-wide one).
 *
 * Based on HakiOS's userland/camera/camera_device.c (hakios-mt6768 commit 03383df, MIT, "The HakiOS authors"), which put Camera.app's
 * live preview on an MT6768 phone with one back camera. What changed here:
 *   - three streams, each with its own port type / unique ID / module info / formats, and the kernel's REDWOOD_CAMERA_SENSOR when the capture
 *     graph starts another one (rwcam_core.c: one sensor streams at a time, the reader thread follows the stream started last);
 *   - the ISP (camera_isp.c) takes the black level from each frame's header, the Bayer order too (no constants of one sensor);
 *   - no kern.redwood.say (rwcam_log.c), no CAM_TRACE / crash-handler / message-hook debugging code, no address of any cache (a method of
 *     CMCapture is found by its name; its address is not compared with an expected one);
 *   - no dependency on headers of Apple's SDK (rwcam_abi.h).
 *
 * Both objects are CoreMedia "derived objects" (CMDerivedObjectCreate): a table of functions for the base class (properties) and one for the
 * device or stream class, as H10ISP's are laid out in iOS 16.5 and, checked for this port by disassembling CMCapture of the iOS 16.7 cache
 * (REPORT.md section 3):
 *   device  derived table {version 3, processing_session_create, request_streams, relinquish_streams, pearl_calibration}
 *   stream  derived table {version 2, start, stop, add_attachments}
 *   session derived table {version 1, prepare, process, flush}
 * A stream's frames go to the capture graph through blocks it is given in its property "VideoOutputHandlers"
 * ({ output: { PixelBufferHandler: ^(CVPixelBufferRef, CMTime) } }), for the outputs "VideoOutputConfigurations" describes and
 * "VideoOutputsEnabled" switches on.
 */
#include "rwcam_abi.h"
#include "camera_isp.h"
#include "rwcam_core.h"
#include "rwcam_log.h"
#include "rwcam_render.h"
#include "rwcam_pair.h"
#include "rwcam_device.h"

/* ---- CoreMedia's derived objects, as CMCapture lays them out ------------------------------------------------ */

struct base_class {
	uint64_t        version;
	uint64_t        storage_size;
	Boolean         (*equal)(CMBaseObjectRef, CMBaseObjectRef);
	OSStatus        (*invalidate)(CMBaseObjectRef);
	void            (*finalize)(CMBaseObjectRef);
	CFStringRef     (*copy_debug_description)(CMBaseObjectRef);
	OSStatus        (*copy_property)(CMBaseObjectRef, CFStringRef, CFAllocatorRef, void *);
	OSStatus        (*set_property)(CMBaseObjectRef, CFStringRef, CFTypeRef);
	OSStatus        (*notification_barrier)(CMBaseObjectRef);
	const void *    protocols;
	const void *    reserved;
};
struct device_class {
	uint64_t        version;                /* 3 */
	void *          processing_session_create;
	OSStatus        (*request_streams)(CMBaseObjectRef, CFArrayRef, CFDictionaryRef);
	OSStatus        (*relinquish_streams)(CMBaseObjectRef, CFArrayRef, CFDictionaryRef);
	void *          pearl_calibration;
};
struct stream_class {
	uint64_t        version;                /* 2 */
	OSStatus        (*start)(CMBaseObjectRef);
	OSStatus        (*stop)(CMBaseObjectRef);
	OSStatus        (*add_attachments)(CMBaseObjectRef, CMSampleBufferRef, CFDictionaryRef);
};
struct vtable {
	const void *    reserved;
	const struct base_class *base;
	const void *    derived;
};

#define ERR_PARAM               (-12780)
#define ERR_NOT_AVAILABLE       (-12783)
#define ERR_UNSUPPORTED         (-12782)

static CMBaseClassID (*device_class_id)(void);          /* FigCaptureDeviceGetClassID, CMCapture */
static CMBaseClassID (*stream_class_id)(void);          /* FigCaptureStreamGetClassID */
static CMBaseClassID (*session_class_id)(void);         /* FigCaptureISPProcessingSessionGetClassID */
static CMBaseClassID (*group_class_id)(void);           /* FigCaptureSynchronizedStreamsGroupGetClassID */
/* by dlsym as well (in the cache's export tables; not in the hand-made stubs the bundle links) */
static void (*cm_set_attachment)(CFTypeRef target, CFStringRef key, CFTypeRef value, uint32_t mode);
static CFTypeRef (*cm_get_attachment)(CFTypeRef target, CFStringRef key, uint32_t *mode);
static void *(*cv_get_iosurface)(CVPixelBufferRef);
static CFTypeRef (*cf_data_create)(CFAllocatorRef, const uint8_t *, CFIndex);
static const void *(*cf_array_get)(CFArrayRef, CFIndex);
static CFTypeID (*cf_array_type)(void);
static CFTypeID (*pool_type_id)(void);                  /* CoreVideo's CVPixelBufferPoolGetTypeID (by dlsym: it is public, but no other image of the cache binds it, so the cache's tables do not list it) */

/* ---- strings and numbers ---------------------------------------------------------------------------------- */

static CFStringRef
cfs(const char *s)
{
	return CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8);
}

static CFNumberRef
number_i(int64_t v)
{
	return CFNumberCreate(NULL, kCFNumberSInt64Type, &v);
}

static CFNumberRef
number_f(double v)
{
	return CFNumberCreate(NULL, kCFNumberDoubleType, &v);
}

static CFMutableDictionaryRef
new_dict(void)
{
	return CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

/* d[key] = value; takes over the caller's reference to value */
static void
put(CFMutableDictionaryRef d, const char *key, CFTypeRef value)
{
	CFStringRef k = cfs(key);

	CFDictionarySetValue(d, k, value);
	CFRelease(k);
	CFRelease(value);
}

static int64_t
get_i(CFDictionaryRef d, const char *key, int64_t otherwise)
{
	CFStringRef k = cfs(key);
	CFTypeRef v = d ? CFDictionaryGetValue(d, k) : NULL;
	int64_t out = otherwise;

	CFRelease(k);
	if (v != NULL && CFGetTypeID(v) == CFNumberGetTypeID()) {
		CFNumberGetValue(v, kCFNumberSInt64Type, &out);
	} else if (v != NULL && CFGetTypeID(v) == CFBooleanGetTypeID()) {
		out = CFBooleanGetValue(v);
	}
	return out;
}

static bool
key_is(CFStringRef key, const char *name)
{
	char buffer[128];

	return key != NULL && CFGetTypeID(key) == CFStringGetTypeID() && CFStringGetCString(key, buffer, sizeof(buffer), kCFStringEncodingUTF8) &&
	    strcmp(buffer, name) == 0;
}

static void
key_name(CFStringRef key, char *out, size_t size)
{
	strlcpy(out, "?", size);
	if (key != NULL && CFGetTypeID(key) == CFStringGetTypeID()) {
		CFStringGetCString(key, out, (CFIndex)size, kCFStringEncodingUTF8);
	}
}

static void
describe(CFTypeRef value, char *text, size_t size)
{
	text[0] = 0;
	if (value != NULL) {
		CFStringRef d = CFCopyDescription(value);

		if (d != NULL) {
			CFStringGetCString(d, text, (CFIndex)size, kCFStringEncodingUTF8);
			CFRelease(d);
		}
		for (char *c = text; *c; c++) {
			*c = *c == '\n' ? ' ' : *c;
		}
	}
}

/*
 * Every property the capture graph asks of or sets on our objects that is not answered, with the value of a set: said every time (not once for
 * each key: run q10hl's loop was not explained by the log). The log's own limits (rwcam_loglimit.h: a line of one shape three times, then
 * counted) keep a loop from filling the console.
 */
static void
say_unknown(const char *what, CFStringRef key, CFTypeRef value)
{
	char name[128], text[300];

	key_name(key, name, sizeof(name));
	describe(value, text, sizeof(text));
	rwcam_say("%s %s%s%s", what, name, text[0] ? " = " : "", text);
}

/* ---- the cameras ------------------------------------------------------------------------------------------ */

struct fmt {
	int             width, height, fps;
	bool            binned;
};

struct cam_desc {
	const char      *port_symbol;           /* CMCaptureCore's kFigCapturePortType_... (dlsym) */
	const char      *unique_id;             /* the AVCaptureDevice unique ID of the model's AVCaptureSession.plist entry */
	const char      *name;
	int             sensor_id;              /* ModuleInfo "sensorID": the key of the entry in CameraSetup.plist (PortType... : "0x%04x") */
	double          focal_mm, f_number, pixel_um, fov_deg, max_zoom;
	bool            has_af, back;
	int             nformats;
	struct fmt      formats[3];
};

/*
 * The formats are the ones the model's AVCaptureSession.plist names (FigCaptureStreamFormats, matched by SensorWidth / SensorHeight /
 * MaxFrameRate / Binned / FES...), not the sensors' own sizes: the ISP (camera_isp.c) takes the middle of whatever the sensor sends in the
 * picture's proportions and scales it. Lens numbers are nominal (they feed the intrinsics only): IMX766 4096x3072 is a 2x2 binned 50 MP, ~2 um
 * pixels; IMX355 3264x2448, 1.4 um; IMX615 3280x2464.
 */
/*
 * The back cameras' third format, 4096x3072 (run user6): a one-picture photograph of 4032x3024 fails in GNR -- its pyramid's levels of
 * half floats fall 32 bytes off a multiple of 64 from the 1008x756 one on, which the Metal driver refuses -- and of 4096x3072 (the wide
 * sensor's own frame) every level falls on one. It is taken when the model's AVCaptureSession.plist names it (the `Photo' stream format's
 * SensorWidth/SensorHeight and the 12MP device format: tools/make_camera_plists.py); with the plist as it was, format 0 as before.
 * Format 1 is the films' (formats[1] everywhere a film is meant).
 */
static const struct cam_desc cams[RWCAM_CAMERAS] = {
	[RWCAM_BACK_WIDE] = {
		"kFigCapturePortType_BackFacingCamera", "com.apple.avfoundation.avcapturedevice.built-in_video:0", "Back Camera", 0x343,
		5.6, 1.9, 2.0, 71.0, 16.0, true, true, 3, { { 4032, 3024, 30, false }, { 4224, 2376, 60, false }, { 4096, 3072, 30, false } },
	},
	[RWCAM_BACK_ULTRAWIDE] = {
		"kFigCapturePortType_BackFacingSuperWideCamera", "com.apple.avfoundation.avcapturedevice.built-in_video:5", "Back Ultra Wide Camera",
		0x355, 1.6, 2.2, 1.4, 110.0, 8.0, false, true, 3, { { 4032, 3024, 30, false }, { 4224, 2376, 60, false }, { 4096, 3072, 30, false } },
	},
	[RWCAM_FRONT] = {
		"kFigCapturePortType_FrontFacingCamera", "com.apple.avfoundation.avcapturedevice.built-in_video:1", "Front Camera", 0x354,
		3.0, 2.2, 1.6, 82.0, 5.0, false, false, 2, { { 3088, 2320, 30, false }, { 3840, 2160, 30, false } },
	},
};

static CFStringRef port_type[RWCAM_CAMERAS];
static int mode60 = -1;                 /* (q10jm) the main camera's 60/s sensor mode: 1 hs_video, 2 custom2, 0 none (switch files; read once) */

/* ---- what the device and the stream say they have --------------------------------------------------------- */

struct prop {
	const char *name, *type, *access;
};

static const struct prop stream_props[] = {
	{ "PortType", "String", "ReadOnly" }, { "ModuleInfo", "Collection", "ReadOnly" }, { "UniqueID", "String", "ReadOnly" }, { "NonLocalizedName", "String", "ReadOnly" },
	{ "SupportedFormatsArray", "Collection", "ReadOnly" }, { "FocalLength", "Number", "ReadOnly" },
	{ "LensFNumber", "Number", "ReadOnly" }, { "PixelSize", "Number", "ReadOnly" },
	{ "MaxZoomFactor", "Number", "ReadOnly" }, { "SupportedColorSpaces", "Collection", "ReadOnly" },
	{ "HasAutoFocusPositionSensor", "Boolean", "ReadOnly" }, { "Clock", "Clock", "ReadOnly" },
	{ "FormatIndex", "Number", "ReadWrite" }, { "DecouplePrimaryScalerFromStreaming", "Boolean", "ReadWrite" }, { "MinimumFrameRate", "Number", "ReadWrite" }, { "MaximumFrameRate", "Number", "ReadWrite" },
	{ "MaximumAllowedFrameRate", "Number", "ReadWrite" }, { "VideoOutputConfigurations", "Collection", "ReadWrite" },
	{ "VideoOutputsEnabled", "Collection", "ReadWrite" }, { "VideoOutputHandlers", "Collection", "WriteOnly" },
	{ "DeferAdditionOfAttachments", "Boolean", "ReadWrite" }, { "TorchLevel", "Number", "ReadWrite" },
	{ "HighCurrentTorchEnabled", "Boolean", "ReadWrite" }, { "ColorSpace", "Number", "ReadWrite" }, { "ZoomFactor", "Number", "ReadWrite" },
	{ "ExposureBias", "Number", "ReadWrite" }, { "DeviceOrientation", "Number", "ReadWrite" }, { "FlashMode", "Number", "ReadWrite" },
	{ "StillImageCaptureNow", "Collection", "WriteOnly" }, { "LockAENow", "Boolean", "WriteOnly" }, { "UnlockAENow", "Boolean", "WriteOnly" },
	{ "LockAWBNow", "Boolean", "WriteOnly" }, { "UnlockAWBNow", "Boolean", "WriteOnly" }, { "MaxIntegrationTime", "Number", "ReadWrite" },
	{ "AEMaxGain", "Number", "ReadWrite" }, { "AEMinGain", "Number", "ReadWrite" }, { "FaceDetectionEnabled", "Boolean", "ReadWrite" },
	{ "VideoRecordingInProgress", "Boolean", "ReadWrite" }, { "MemoryPoolID", "Number", "ReadWrite" },
	{ "ChromaNoiseReductionEnabled", "Boolean", "ReadWrite" }, { "TemporalNoiseReductionEnabled", "Boolean", "ReadWrite" },
	{ "MultipleOutputSupportEnabled", "Boolean", "ReadWrite" }, { "VideoOutputsColorInfo", "Collection", "ReadWrite" },
	{ "RegionOfInterestForCameraControls", "Collection", "ReadWrite" }, { "AEMeteringMode", "Number", "ReadWrite" },
	{ "MaxFrameCoalescingDuration", "Number", "ReadWrite" }, { "LimitAERange", "Boolean", "ReadWrite" },
	/*
	 * Run q10hl (the dual-wide device asked for by Camera.app; every start of the session ended with -12787 and was made again, for ever):
	 * -[BWFigVideoCaptureStream initWithCaptureStream:attributes:sensorIDDictionary:synchronizedStreamsGroup:...] (0x1a207fd20), for a stream of
	 * a group with more than one active port type, reads this with the strict getProperty:error: (0x1a2080064) and gives up on any error
	 * (cbnz w0 at 0x1a2080070); BWFigCaptureStream answers -12787 by itself for a key that is not in the stream's SupportedPropertiesDictionary.
	 * It takes [MasterToSlaveFrameRateRatio] intValue of the answer. The whole dictionary, as -[BWFigVideoCaptureSynchronizedStreamsGroup
	 * _slaveConfigurationForStream:] (0x1a1f5e9ec) makes it and -setMasterStream:allStreams: sets it (on the stream when the group has no
	 * MasterConfiguration, in the group's MasterConfiguration when it has: ours has): StreamingOutputsEnabled (a boolean),
	 * MasterToSlaveFrameRateRatio (an int), FrameSkippingEnabled (a number), TimeMachineEnabled (a boolean).
	 */
	{ "SynchronizedStreamsSlaveConfiguration", "Collection", "ReadWrite" },
}, device_props[] = {
	{ "StreamArray", "Collection", "ReadOnly" }, { "Clock", "Clock", "ReadOnly" }, { "ClientIsActive", "Boolean", "ReadWrite" },
	{ "SupportedSynchronizedStreamsGroups", "Collection", "ReadOnly" }, { "MemoryPoolID", "Number", "ReadWrite" },
};

static CFDictionaryRef
supported(const struct prop *p, size_t n)
{
	CFMutableDictionaryRef all = new_dict();

	for (size_t i = 0; i < n; i++) {
		CFMutableDictionaryRef one = new_dict();

		put(one, "PropertyType", cfs(p[i].type));
		put(one, "ReadWriteStatus", cfs(p[i].access));
		put(all, p[i].name, one);
	}
	return all;
}

/*
 * Memory (run user3: the kernel refused surfaces, mediaserverd had 450 to 548 of them, 1.2 to 1.4 GB). The pictures are made in buffers of the
 * GRAPH's pools and given to the graph; how many of an output's it still holds is counted: each buffer gets an attachment, a one-byte CFData
 * whose bytes' deallocator is ours -- called when the buffer is let go of (back into its pool, or gone). Said in the stream's 5 s line.
 */
static void
buffer_back(void *ptr, void *info)
{
	(void)ptr;
	__atomic_sub_fetch((long *)info, 1, __ATOMIC_RELAXED);
}

static void
buffer_counted(CVPixelBufferRef pb, long *count, void **allocator)
{
	static CFAllocatorRef (*create)(CFAllocatorRef, void *);
	static CFTypeRef (*data_of)(CFAllocatorRef, const uint8_t *, CFIndex, CFAllocatorRef);
	static const uint8_t byte;

	if (create == NULL) {
		create = (CFAllocatorRef (*)(CFAllocatorRef, void *))dlsym(RTLD_DEFAULT, "CFAllocatorCreate");
		data_of = (CFTypeRef (*)(CFAllocatorRef, const uint8_t *, CFIndex, CFAllocatorRef))dlsym(RTLD_DEFAULT, "CFDataCreateWithBytesNoCopy");
	}
	if (create != NULL && data_of != NULL && *allocator == NULL) {
		/* (CFAllocatorContext: version, info, retain, release, copyDescription, allocate, reallocate, deallocate, preferredSize) */
		void *context[9] = { 0, count, 0, 0, 0, 0, 0, (void *)buffer_back, 0 };

		*allocator = (void *)create(NULL, context);
	}
	if (*allocator != NULL) {
		CFTypeRef d = data_of(NULL, &byte, 1, (CFAllocatorRef)*allocator);

		if (d != NULL) {
			CFStringRef k = cfs("RedwoodCounted");

			__atomic_add_fetch(count, 1, __ATOMIC_RELAXED);
			CVBufferSetAttachment(pb, k, d, 0 /* kCVAttachmentMode_ShouldNotPropagate */);
			CFRelease(k);
			CFRelease(d);
		}
	}
}

/* ---- the formats ------------------------------------------------------------------------------------------ */

#define PF_420F         0x34323066u     /* '420f' */
#define PF_420V         0x34323076u     /* '420v' */
#define PF_BGRA         0x42475241u     /* 'BGRA' */

static CFDictionaryRef
output_info(int width, int height)
{
	CFMutableDictionaryRef o = new_dict();
	CFNumberRef pf[3] = { number_i(PF_420F), number_i(PF_420V), number_i(PF_BGRA) };
	CFArrayRef pfs = CFArrayCreate(NULL, (const void **)pf, 3, &kCFTypeArrayCallBacks);

	for (int i = 0; i < 3; i++) {
		CFRelease(pf[i]);
	}
	put(o, "SupportedPixelFormats", pfs);
	put(o, "NativeWidth", number_i(width));
	put(o, "NativeHeight", number_i(height));
	put(o, "MaxDownscaleFactor", number_f(32));
	put(o, "MaxScaleFactor", number_f(1));
	put(o, "MinScaleFactor", number_f(1.0 / 32));
	put(o, "OutputWidthAlignmentRequirement", number_i(16));
	return o;
}

static CFDictionaryRef
format(const struct cam_desc *c, const struct fmt *fm)
{
	CFMutableDictionaryRef f = new_dict(), outputs = new_dict();
	CMVideoFormatDescriptionRef fd = NULL;
	int w = fm->width, h = fm->height;

	CMVideoFormatDescriptionCreate(NULL, PF_420F, w, h, NULL, &fd);
	if (fd != NULL) {
		put(f, "FormatDescription", fd);
	}
	put(f, "VideoMinFrameRate", number_f(2));
	put(f, "VideoMaxFrameRate", number_f(fm->fps));
	put(f, "VideoIsBinned", CFRetain(fm->binned ? kCFBooleanTrue : kCFBooleanFalse));
	put(f, "VideoBinningFactorHorizontal", number_i(fm->binned ? 2 : 1));
	put(f, "VideoBinningFactorVertical", number_i(fm->binned ? 2 : 1));
	put(f, "VideoFieldOfView", number_f(c->fov_deg));
	put(f, "VideoMinIntegrationTime", number_i(100));
	put(f, "VideoMaxIntegrationTime", number_i(1000000 / 15));
	put(f, "VideoRawBitDepth", number_i(10));
	put(f, "VideoMaxWidth", number_i(w));
	put(f, "VideoMaxHeight", number_i(h));
	put(f, "Experimental", CFRetain(kCFBooleanFalse));
	put(f, "SensorWidth", number_i(w));
	put(f, "SensorHeight", number_i(h));
	put(f, "ISOBase", number_f(32));
	put(f, "DefaultAEMaxGain", number_f(64));
	/* the sensor crop geometry the graph builder reads off a format (everything spans the whole sensor: no GDC crop) */
	put(f, "SensorCropWidth", number_i(w));
	put(f, "SensorCropHeight", number_i(h));
	put(f, "PreGDCSensorCropWidth", number_i(w));
	put(f, "PreGDCSensorCropHeight", number_i(h));
	put(f, "PostGDCCropWidth", number_i(w));
	put(f, "PostGDCCropHeight", number_i(h));
	put(f, "GeometricDistortionCorrectedNativeWidth", number_i(w));
	put(f, "GeometricDistortionCorrectedNativeHeight", number_i(h));
	put(outputs, "PrimaryScaler", output_info(w, h));
	put(outputs, "SecondaryScaler", output_info(w, h));
	put(outputs, "StillImage", output_info(w, h));
	if (fm == &c->formats[1]) {
		/* (the films' formats: a photograph's keeps the outputs it has had) */
		put(outputs, "IntermediateTap", output_info(w, h));
	}
	put(f, "SupportedOutputs", outputs);
	return f;
}

/* ---- the stream -------------------------------------------------------------------------------------------- */

struct output {
	const char *    name;
	CFDictionaryRef config;                 /* from VideoOutputConfigurations */
	bool            enabled;
	const void *    handler;                /* a block: ^(CVPixelBufferRef, CMTime) */
	CVPixelBufferPoolRef pool;
	int             pool_width, pool_height;
	uint32_t        pool_format;
	uint64_t        delivered;
	uint64_t        last_ms;                /* when the last picture was given (rwcam_now_ms) */
	uint64_t        skipped;                /* frames no picture was made of (StillImage: see STILL_MIN_MS) */
	unsigned        format_said;            /* (q10jn) the pool's buffers in another format than its attributes: said (twice at most) */
	uint64_t        real, made_up;          /* pictures of a frame of the sensor / of the test pattern's pacer */
	uint64_t        last_time;              /* the last picture's time (mach units): the times given only go forward */
	uint64_t        last_seq;               /* and its frame's number, for the even times (see deliver) */
	uint64_t        evened;                 /* pictures whose time was taken from the frame count, not the stamp */
	uint8_t         *small;                 /* the picture as the ISP makes it, smaller than asked (small_made) */
	long            out_there;              /* buffers of the graph's pool given and not yet let go of by the graph (buffer_counted) */
	void            *counter;               /* (the allocator whose deallocate counts them back) */
	size_t          small_room;
	int             asked_width, asked_height;      /* what the graph asked this output's pictures to be (its pool's, or its configuration's) */
	uint32_t        asked_format;
	const char      *why_not;               /* why the last frame gave no picture here (NULL: it gave one) */
	uint64_t        not_given;              /* frames since the last status line that gave none */
	uint64_t        given;                  /* pictures since the last status line */
	uint64_t        luma_sum, luma_n;       /* of the pictures since the last summary */
	uint64_t        luma_moved;             /* the sum of how far the mean luma went from each picture to the next (the lights' flicker, AE's hunting) */
	unsigned        luma_before;
	unsigned        luma_min, luma_max;
	uint64_t        black;                  /* pictures of this start that were black (mean luma under BLACK_LUMA) */
	struct {                                /* the summary every SAY_MS */
		uint64_t since, n, made, made_max, handler, handler_max, gap_max, last_ms;
		uint32_t gaps[4];
	} w;
};

/*
 * IntermediateTap (run q10ii, video mode): the film's node is given "PrimaryScaler decoupled" and "no SecondaryScaler", and then
 * -[BWMultiStreamCameraSourceNode _updateOutputIDMappingsForStreamingOutputs] (0x1a1f83ee4) has one output left for the film's pictures, the
 * IntermediateTap, and gives -12780 when the format has none (0x1a1f84218). It is an output like the PrimaryScaler here (LEAD: the one whose
 * pictures' times are evened and whose picture the SecondaryScaler's can be made from).
 */
#define OUTPUTS 4
#define LEAD(i) ((i) != 2)         /* (run q10ij: the film came from the SecondaryScaler, alone: every streaming output is one) */
static const char *const output_names[OUTPUTS] = { "PrimaryScaler", "SecondaryScaler", "StillImage", "IntermediateTap" };

struct stream {
	int             cam;                    /* RWCAM_... */
	CFArrayRef      formats;
	CFDictionaryRef supported;
	CFStringRef     name, unique_id;
	int             format_index;
	double          min_fps, max_fps;
	/* the PrimaryScaler's picture of the frame being given, for the SecondaryScaler's to be made from (twin_made); and its time */
	CVPixelBufferRef twin;
	int             twin_width, twin_height;
	uint32_t        twin_format;
	uint64_t        twin_seq, first_seq, first_time, twins;
	struct output   out[OUTPUTS];
	pthread_mutex_t lock;
	volatile bool   started;                /* Start (or a reservation) made this the camera that streams */
	volatile bool   delivering;             /* the graph has called Start: pictures may go to its handlers (before that its nodes are not all there) */
	int             torch;
	int64_t         max_integration_ms;     /* "MaxIntegrationTime": CMCapture 16.7's -[BWFigVideoCaptureStream maxEIT] reads it with a strict getProperty: and makes a CMTime of it with timescale 1000 */
	uint64_t        frames;
	volatile float  zoom;                   /* "ZoomFactor": the graph's zoom, done here (the middle 1/view_zoom() of the sensor's picture) */
	/* STILL CAPTURE (stream_still_now): a photograph was asked for with StillImageCaptureNow and its frames are being given */
	int             still_next;             /* the BracketedCaptureSequenceNumber of the next StillImage picture (1, 2, ...); 0: none asked */
	bool            still_pre;              /* the next one is the frame before the bracket (ProvidePreBracketedFrame) */
	bool            still_numbered;         /* the request is a bracket (it has ...BracketedCaptureParams): its pictures carry their number */
	int             still_most;             /* the last number given (the bracket's count when the request says it, else STILL_MOST) */
	uint64_t        still_until_ms, stills_asked, stills_given;
	volatile bool   zoom_of_group;          /* zoom is the dual-wide DEVICE's (set on the group): the wide camera shows all of its sensor at GROUP_WIDE_BASE_ZOOM */
	float           uw_ratio;               /* the ultra-wide stream: the two back lenses' ratio (rwcam_view_zoom; /var/db/nord2-camera-uw-ratio) */
	uint32_t        flip;                   /* ISP_FLIP_*: /var/db/nord2-camera-flip-{wide,ultra,front}-{h,v} */
	uint64_t        zooms;
	uint8_t         *last_y, *last_uv;      /* the last PrimaryScaler picture (4:2:0, rows of last_w bytes), for the test pattern's pacer */
	int             last_w, last_h;
	struct group    *group;                 /* the synchronized streams group this back stream is of (NULL: none; cleared when the group goes) */
	volatile bool   by_group;               /* started as one of the group's two: the group (rwcam_pair.c) says whose frames are pictures */
	struct rwcam_renderer render[OUTPUTS];  /* how each output's pictures are made (the GPU, the processor: rwcam_render.c) */
	uint64_t        attached;               /* sample buffers given metadata (stream_add_attachments) */
	CFTypeRef       slave_configuration;    /* SynchronizedStreamsSlaveConfiguration as last set (here or in the group's MasterConfiguration); NULL: none yet */
	uint64_t        status_ms, status_frames;       /* the status line every SAY_MS (sink_frame) */
	uint32_t        green;                  /* the last frame's mean green above black, of 1023 (the kernel's header; 0: not there) */
	bool            has_green;
};

/*
 * The synchronized streams group of the two back streams: what CMCapture makes its dual-wide device (AVCaptureDeviceTypeBuiltInDualWideCamera,
 * deviceType 8 in the model's AVCaptureSession.plist) of. A third kind of derived object, of FigCaptureSynchronizedStreamsGroupGetClassID(),
 * listed in the device's SupportedSynchronizedStreamsGroups. As CMCapture 16.7 uses it (disassembly; addresses of the cache before its slide):
 *   - -[BWFigCaptureSynchronizedStreamsGroup initWithFigCaptureSynchronizedStreamsGroup:captureDevice:] (0x1a1f99840) copies
 *     SupportedPropertiesDictionary, SynchronizationMaster (a stream object) and SupportedStreams (an array of stream objects), and takes the
 *     derived table's version over 1 to mean "has a stop function" (ours is 1: the streams are stopped one by one);
 *   - every other property goes through that dictionary first (a key not in it: -12787, not asked);
 *   - -[BWFigVideoCaptureSynchronizedStreamsGroup initWith...] (0x1a20332e8) sets ActiveStreams, reads Extrinsics (a dictionary of port type ->
 *     port type -> 48 bytes, a 3x4 matrix of floats; only "not supported", -12782, is forgiven), BaseZoomFactorsByPortType, and, when
 *     MasterConfiguration is supported, MasterConfigurationSupportedKeys;
 *   - -setMasterStream:allStreams: (0x1a1f3a9e0) then sets MasterConfiguration = { Master: stream, SlaveConfigurationByPortType: { port type:
 *     { StreamingOutputsEnabled, MasterToSlaveFrameRateRatio, FrameSkippingEnabled, TimeMachineEnabled } } } -- at the start and every time
 *     BWBravoStreamSelector recommends the other stream (the zoom crossing the wide stream's base zoom factor).
 * What the group then asks of the two sensors is rwcam_pair.c's.
 */
struct group {
	CMBaseObjectRef stream[2];              /* [0] wide, [1] ultra-wide (retained) */
	CFArrayRef      streams;
	CFDictionaryRef supported;
	CFTypeRef       master_configuration;   /* as last set */
	float           zoom;                   /* ZoomFactor as last set (the dual-wide device's) */
	int             last_sensor;            /* the camera whose frame the last picture was of, and when it was given (the change of sensor is said) */
	uint64_t        last_ms, changes;
	struct rwcam_pair pair;
	unsigned        said;
};
static const int group_cam[2] = { RWCAM_BACK_WIDE, RWCAM_BACK_ULTRAWIDE };
/* the zoom the dual-wide device has when the wide camera is at 1: what Camera.app shows as "1x" next to "0.5x" (rwcam_core.h) */
#define GROUP_WIDE_BASE_ZOOM    RWCAM_UW_SWITCH_ZOOM

/* the stream that streams for each camera (set by Start, cleared by Stop): what the reader thread's frames go to */
static struct stream *volatile active[RWCAM_CAMERAS];

static struct stream *
stream_of(CMBaseObjectRef o)
{
	return CMBaseObjectGetDerivedStorage(o);
}

/*
 * What the stream's pictures are cropped by for the graph's zoom: the zoom itself, but for the ultra-wide camera, whose zoom Camera.app takes to
 * reach the wide camera's field of view at 2.0 (rwcam_core.h).
 */
static bool meter_tapped[RWCAM_CAMERAS];        /* the exposure goes by a place the user chose (not by all that is seen) */

static float
zoom_view_at(const struct stream *s, float zoom, bool of_group, float *shift_x, float *shift_y)
{
	float v, x = 0, y = 0;

	if (of_group && s->cam == RWCAM_BACK_WIDE) {
		v = zoom / (float)GROUP_WIDE_BASE_ZOOM;
		v = v >= 1 ? v : 1;             /* (under the wide camera's base zoom the ultra-wide sensor is shown; all of the sensor is the most there is) */
	} else if (of_group && s->cam == RWCAM_BACK_ULTRAWIDE) {
		/* (the dual-wide device: the window that is the wide camera's picture at the switch, by what is known of the two lenses) */
		float ratio, dx, dy;

		rwcam_core_alignment(&ratio, &dx, &dy, NULL);
		rwcam_uw_view(zoom, ratio, dx, dy, &v, &x, &y);
	} else {
		v = rwcam_view_zoom(s->cam, zoom, s->uw_ratio);
	}
	if (shift_x != NULL) {
		*shift_x = x;
		*shift_y = y;
	}
	return v;
}

static float
zoom_view(const struct stream *s, float zoom, bool of_group)
{
	return zoom_view_at(s, zoom, of_group, NULL, NULL);
}

static float
view_zoom(const struct stream *s)
{
	return zoom_view(s, s->zoom, s->zoom_of_group);
}

/*
 * The zoom of a stream by itself is its own (1: all of its sensor). The zoom of the dual-wide device is one number for both cameras, set on the
 * GROUP when the group lists ZoomFactor (-[BWFigVideoCaptureDevice _setSynchronizedStreamsGroupProperty:value:], 0x1a1f46998: the group if it
 * has the property, else the device's captureStream, the wide one, alone -- run q10hs: 1.818 on the wide stream at Camera.app's "1x", taken
 * here for the wide camera's own zoom, and nothing on the ultra-wide one). An iPhone's camera divides it by each stream's base zoom factor
 * (BaseZoomFactorsByPortType: ultra-wide 1, wide GROUP_WIDE_BASE_ZOOM); so here.
 */
static void
stream_zoom(struct stream *s, float z, bool of_group)
{
	float most = (float)cams[s->cam].max_zoom * (of_group && s->cam == RWCAM_BACK_WIDE ? (float)GROUP_WIDE_BASE_ZOOM : 1);

	z = !(z >= 1) ? 1 : z > most ? most : z;
	if ((z != s->zoom || of_group != s->zoom_of_group) && (s->zooms++ < 6 || s->zooms % 200 == 0)) {
		rwcam_say("stream: zoom %.3f%s, the middle 1/%.3f of the sensor (%s; change %llu)", (double)z, of_group ? " of the dual-wide device" : "",
		    (double)zoom_view(s, z, of_group), cams[s->cam].name, (unsigned long long)s->zooms);
	}
	if (z != s->zoom || of_group != s->zoom_of_group) {
		float v = zoom_view(s, z, of_group);

		/* the sharpness is taken of the middle of what is seen */
		rwcam_core_focus_window(s->cam, 0.5f, 0.5f, 0.25f / v, 0.25f / v, false);
		if (of_group && !meter_tapped[s->cam]) {
			/*
			 * ... and the exposure of what is seen (the dual-wide device; run q10hy): the ultra-wide camera zoomed to the wide camera's
			 * field measures that field, as the wide camera does -- the same light on both sides of the change of camera.
			 */
			float x, y, w = zoom_view_at(s, z, of_group, &x, &y);

			rwcam_core_meter_window(s->cam, 0.5f + x, 0.5f + y, w > 1.02f ? 0.9f / w : 0, w > 1.02f ? 0.9f / w : 0);
		}
	}
	s->zoom_of_group = of_group;
	s->zoom = z;
}

/* the back lenses' ratio in thousandths, as text in /var/db/nord2-camera-uw-ratio (1001 to 1999; to set it right on the phone without a build) */
static float
uw_ratio_now(void)
{
	char text[16] = { 0 };
	int fd = open("/var/db/nord2-camera-uw-ratio", O_RDONLY), thousandths = 0;

	if (fd >= 0) {
		long n = read(fd, text, sizeof(text) - 1);

		close(fd);
		for (long i = 0; i < n && text[i] >= '0' && text[i] <= '9'; i++) {
			thousandths = thousandths * 10 + (text[i] - '0');
		}
	}
	return thousandths > 1000 && thousandths < 2000 ? (float)thousandths / 1000 : RWCAM_UW_RATIO;
}

/*
 * The two back lenses by hand: /var/db/nord2-camera-uw-ratio (above) and /var/db/nord2-camera-uw-offset, two whole numbers "dx dy" in
 * ten-thousandths of the ultra-wide picture (where the wide camera's axis is, off the middle; each -2000 to 2000). Either one there: what they
 * say is used and the pictures are not compared (rwcam_core.h). Neither: found by comparing.
 */
static void
align_markers(void)
{
	char text[32] = { 0 };
	int fd = open("/var/db/nord2-camera-uw-offset", O_RDONLY), v[2] = { 0, 0 }, k = 0;
	bool by_hand = access("/var/db/nord2-camera-uw-ratio", F_OK) == 0 || fd >= 0;

	if (fd >= 0) {
		long n = read(fd, text, sizeof(text) - 1);
		bool minus = false, digits = false;

		close(fd);
		for (long i = 0; i <= n && k < 2; i++) {
			char c = i < n ? text[i] : ' ';

			if (c == '-' && !digits) {
				minus = true;
			} else if (c >= '0' && c <= '9') {
				v[k] = v[k] * 10 + (c - '0');
				digits = true;
			} else if (digits) {
				v[k] = minus ? -v[k] : v[k];
				k++;
				minus = digits = false;
			}
		}
	}
	for (int i = 0; i < 2; i++) {
		v[i] = v[i] > 2000 ? 2000 : v[i] < -2000 ? -2000 : v[i];
	}
	rwcam_core_align_set(by_hand ? uw_ratio_now() : 0, (float)v[0] / 10000, (float)v[1] / 10000);
}

static int
output_index(CFStringRef name)
{
	for (int i = 0; i < OUTPUTS; i++) {
		if (key_is(name, output_names[i])) {
			return i;
		}
	}
	return -1;
}

static OSStatus
stream_copy_property(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef allocator, void *out)
{
	struct stream *s = stream_of(o);
	const struct cam_desc *c = &cams[s->cam];
	CFTypeRef value = NULL;
	char asked_key[160], asked_name[128];

	key_name(key, asked_name, sizeof(asked_name));
	snprintf(asked_key, sizeof(asked_key), "%s: %s", c->name, asked_name);
	rwcam_say_once("stream: read", asked_key, NULL);
	(void)allocator;
	if (key_is(key, "PortType")) {
		value = CFRetain(port_type[s->cam]);
	} else if (key_is(key, "ModuleInfo")) {
		/*
		 * +[FigCaptureCameraParameters sensorIDStringFromModuleInfo:] formats ["sensorID"] as "0x%04x"; that string and the port type find the sensor's
		 * entry in <model>/CameraSetup.plist. -[BWFigVideoCaptureStream init...] then needs all of ModuleLensID, ModuleActuatorID and
		 * ModuleIntegratorID (_GetFocalLengthParametersFromArrayUsingModuleInfo returns an error when one is missing) and, for a plist entry
		 * with a FocalLengthCharacterization, ids inside its lists: LensID [1], ActuatorID [1], IntegratorID [1, 2, 3, 5, 6] (back), LensID [1, 2, 5]
		 * (front). All 1 satisfies every entry of the iPhone X's file.
		 */
		CFMutableDictionaryRef info = new_dict();

		put(info, "sensorID", number_i(c->sensor_id));
		put(info, "ModuleLensID", number_i(1));
		put(info, "ModuleActuatorID", number_i(1));
		put(info, "ModuleIntegratorID", number_i(1));
		put(info, "ModuleFocusID", number_i(1));
		put(info, "ModuleIDSchemeVersion", number_i(1));
		value = info;
	} else if (key_is(key, "UniqueID") || key_is(key, "ModelID")) {
		value = CFRetain(s->unique_id);
	} else if (key_is(key, "NonLocalizedName") || key_is(key, "LocalizedName")) {
		value = CFRetain(s->name);
	} else if (key_is(key, "Manufacturer")) {
		value = cfs("Apple Inc.");
	} else if (key_is(key, "SupportedFormatsArray")) {
		value = CFRetain(s->formats);
	} else if (key_is(key, "FormatIndex")) {
		value = number_i(s->format_index);
	} else if (key_is(key, "MinimumFrameRate")) {
		value = number_f(s->min_fps);
	} else if (key_is(key, "MaximumFrameRate")) {
		value = number_f(s->max_fps);
	} else if (key_is(key, "Clock")) {
		value = CFRetain(CMClockGetHostTimeClock());
	} else if (key_is(key, "TorchLevel") || key_is(key, "ExposureBias")) {
		value = number_f(0);
	} else if (key_is(key, "VideoOutputsColorInfo")) {
		/* read back by the graph right after it configures the stream (not "if supported"): a failed read tears the graph down. One entry per output: none to say yet. */
		value = new_dict();
	} else if (key_is(key, "VideoOutputConfigurations") || key_is(key, "VideoOutputsEnabled")) {
		/* ReadWrite, but BWMultiStreamCameraSourceNode reads them back while it builds the graph (a failed read tears it down): what each output holds now */
		bool configs = key_is(key, "VideoOutputConfigurations");
		CFMutableDictionaryRef d = new_dict();

		pthread_mutex_lock(&s->lock);
		for (int i = 0; i < OUTPUTS; i++) {
			CFStringRef name = cfs(output_names[i]);

			if (configs) {
				if (s->out[i].config != NULL) {
					CFDictionarySetValue(d, name, s->out[i].config);
				}
			} else {
				CFDictionarySetValue(d, name, s->out[i].enabled ? kCFBooleanTrue : kCFBooleanFalse);
			}
			CFRelease(name);
		}
		pthread_mutex_unlock(&s->lock);
		value = d;
	} else if (key_is(key, "SupportedPropertiesDictionary")) {
		value = CFRetain(s->supported);
	} else if (key_is(key, "MaxIntegrationTime")) {
		value = number_i(s->max_integration_ms);
	} else if (key_is(key, "GeometricDistortionCorrectedRects")) {
		/* a strict copyProperty: in BWMultiStreamCameraSourceNode (depth filtering); no distortion correction here: no rectangles */
		value = new_dict();
	} else if (key_is(key, "FocalLength")) {
		value = number_f(c->focal_mm);
	} else if (key_is(key, "LensFNumber")) {
		value = number_f(c->f_number);
	} else if (key_is(key, "PixelSize")) {
		/* REQUIRED: -[BWFigVideoCaptureStream init...] does a strict getProperty: for it; any value in (0, 100) micrometres is accepted */
		value = number_f(c->pixel_um);
	} else if (key_is(key, "MaxZoomFactor")) {
		value = number_f(c->max_zoom);
	} else if (key_is(key, "ZoomFactor")) {
		value = number_f(s->zoom >= 1 ? s->zoom : 1);
	} else if (key_is(key, "SupportedColorSpaces")) {
		CFNumberRef srgb = number_i(0);

		value = CFArrayCreate(NULL, (const void **)&srgb, 1, &kCFTypeArrayCallBacks);
		CFRelease(srgb);
	} else if (key_is(key, "ColorSpace") || key_is(key, "DeviceOrientation") || key_is(key, "FlashMode")) {
		value = number_i(0);
	} else if (key_is(key, "HasAutoFocusPositionSensor") || key_is(key, "HighCurrentTorchEnabled") ||
	    key_is(key, "DeferAdditionOfAttachments")) {
		value = CFRetain(kCFBooleanFalse);
	} else if (key_is(key, "SynchronizedStreamsSlaveConfiguration")) {
		pthread_mutex_lock(&s->lock);
		if (s->slave_configuration != NULL) {
			value = CFRetain(s->slave_configuration);
		}
		pthread_mutex_unlock(&s->lock);
		if (value == NULL) {
			/* (before anything was set: a slave that gives no pictures, at the master's frame rate) */
			CFMutableDictionaryRef d = new_dict();

			put(d, "StreamingOutputsEnabled", CFRetain(kCFBooleanFalse));
			put(d, "MasterToSlaveFrameRateRatio", number_i(1));
			put(d, "FrameSkippingEnabled", number_i(0));
			put(d, "TimeMachineEnabled", CFRetain(kCFBooleanFalse));
			value = d;
		}
	} else {
		char why[200];

		snprintf(why, sizeof(why), "stream: %s: NO ANSWER (%d) for", c->name, (int)ERR_NOT_AVAILABLE);
		say_unknown(why, key, NULL);
		return ERR_NOT_AVAILABLE;
	}
	*(CFTypeRef *)out = value;
	return 0;
}

static void torch_set(struct stream *s, CFTypeRef value);
static void stream_still_now(struct stream *s, CFTypeRef options);
static void render_once(void);

static void
stream_slave_configuration(struct stream *s, CFTypeRef configuration)
{
	CFTypeRef old;

	pthread_mutex_lock(&s->lock);
	old = s->slave_configuration;
	s->slave_configuration = configuration != NULL ? CFRetain(configuration) : NULL;
	pthread_mutex_unlock(&s->lock);
	if (old != NULL) {
		CFRelease(old);
	}
}
static int no_metadata = -1;
static OSStatus stream_begin(CMBaseObjectRef o);
static OSStatus stream_stop(CMBaseObjectRef o);

static OSStatus
stream_set_property(CMBaseObjectRef o, CFStringRef key, CFTypeRef value)
{
	struct stream *s = stream_of(o);

	if (key_is(key, "FormatIndex")) {
		int64_t i = 0;

		if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID()) {
			CFNumberGetValue(value, kCFNumberSInt64Type, &i);
		}
		if (i < 0 || i >= CFArrayGetCount(s->formats)) {
			rwcam_say("stream: %s: set FormatIndex = %lld: no such format (%ld here), ANSWERED %d", cams[s->cam].name, (long long)i,
			    (long)CFArrayGetCount(s->formats), (int)ERR_PARAM);
			return ERR_PARAM;
		}
		s->format_index = (int)i;
		return 0;
	}
	if (key_is(key, "MinimumFrameRate") || key_is(key, "MaximumFrameRate")) {
		double fps = 0;

		if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID()) {
			CFNumberGetValue(value, kCFNumberDoubleType, &fps);
		}
		*(key_is(key, "MinimumFrameRate") ? &s->min_fps : &s->max_fps) = fps;
		/* a film's 30-30 (a photograph's viewfinder is 15-30): no longer frames in the dark while it is on */
		rwcam_core_even_rate(s->min_fps >= 24);
		/*
		 * (q10jm) a film at 60 (60-60) on the main camera: the sensor's 60/s mode -- 2048x1152 (hs_video, default) or 4096x2304 (custom2,
		 * /var/db/nord2-camera-60-custom2); anything slower: the 4096x3072 30/s one. /var/db/nord2-camera-no60: never.
		 */
		if (s->cam == RWCAM_BACK_WIDE) {
			if (mode60 < 0) {
				mode60 = access("/var/db/nord2-camera-no60", F_OK) == 0 ? 0 : access("/var/db/nord2-camera-60-custom2", F_OK) == 0 ? 2 : 1;
			}
			rwcam_core_set_mode(s->min_fps >= 50 ? (unsigned)mode60 : 0);
		}
		return 0;
	}
	if (key_is(key, "VideoOutputConfigurations") || key_is(key, "VideoOutputsEnabled") || key_is(key, "VideoOutputHandlers")) {
		bool configs = key_is(key, "VideoOutputConfigurations"), enabled = key_is(key, "VideoOutputsEnabled"), any_on = false;

		if (value == NULL || CFGetTypeID(value) != CFDictionaryGetTypeID()) {
			return 0;
		}
		pthread_mutex_lock(&s->lock);
		CFIndex n = CFDictionaryGetCount(value);
		const void **keys = calloc((unsigned long)n + 1, sizeof(void *)), **values = calloc((unsigned long)n + 1, sizeof(void *));

		CFDictionaryGetKeysAndValues(value, keys, values);
		for (CFIndex i = 0; i < n; i++) {
			int k = CFGetTypeID(keys[i]) == CFStringGetTypeID() ? output_index(keys[i]) : -1;
			struct output *out;

			if (k < 0) {
				say_unknown("stream: an output not here:", keys[i], NULL);
				continue;
			}
			out = &s->out[k];
			if (configs) {
				if (out->config != NULL) {
					CFRelease(out->config);
				}
				out->config = CFGetTypeID(values[i]) == CFDictionaryGetTypeID() ? CFRetain(values[i]) : NULL;
			} else if (enabled) {
				int64_t on = 1;

				if (CFGetTypeID(values[i]) == CFBooleanGetTypeID()) {
					on = CFBooleanGetValue(values[i]);
				} else if (CFGetTypeID(values[i]) == CFNumberGetTypeID()) {
					CFNumberGetValue(values[i], kCFNumberSInt64Type, &on);
				}
				out->enabled = on != 0;
			} else {
				CFStringRef hk = cfs("PixelBufferHandler");
				CFTypeRef h = CFGetTypeID(values[i]) == CFDictionaryGetTypeID() ? CFDictionaryGetValue(values[i], hk) : NULL;

				CFRelease(hk);
				if (out->handler != NULL) {
					CFRelease(out->handler);
				}
				out->handler = h != NULL ? CFRetain(h) : NULL;
			}
		}
		free(keys);
		free(values);
		if (enabled) {
			for (int i = 0; i < OUTPUTS; i++) {
				any_on = any_on || s->out[i].enabled;
			}
		}
		pthread_mutex_unlock(&s->lock);
		/*
		 * The graph switching an output on is the cue that it wants frames (HakiOS's proven preview delivered on `enabled` alone); the graph's Start
		 * (stream_start) works as well, and either one starts the sensor. All outputs off without a Start: let the sensor go. Not under s->lock: stopping
		 * waits for the reader thread, which takes s->lock to deliver.
		 */
		if (enabled && any_on && !s->started) {
			(void)stream_begin(o);
		} else if (enabled && !any_on && s->started && !s->delivering) {
			(void)stream_stop(o);
		}
		return 0;
	}
	if (key_is(key, "MaxIntegrationTime")) {
		int64_t ms = 0;

		if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID() && CFNumberGetValue(value, kCFNumberSInt64Type, &ms) && ms > 0) {
			s->max_integration_ms = ms;
		}
		return 0;
	}
	if (key_is(key, "TorchLevel")) {
		torch_set(s, value);
		return 0;
	}
	if (key_is(key, "StillImageCaptureNow")) {
		stream_still_now(s, value);
		return 0;
	}
	if (key_is(key, "ZoomFactor")) {
		/*
		 * The graph's zoom is the ISP's: -[BWMultiStreamCameraSourceNode _serviceZoomForPTS:] has BWZoomCommandHandler's value of each frame
		 * (pinch, the zoom buttons, their ramps) set here through -[BWFigVideoCaptureDevice setISPZoomFactor:totalZoomFactor:] ->
		 * _setNewISPZoomFactorOnStream, whenever the stream lists ZoomFactor among its properties. Until run q10he it was taken and not used.
		 */
		float z = 1;

		if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID()) {
			CFNumberGetValue(value, kCFNumberFloatType, &z);
		}
		stream_zoom(s, z, false);
		return 0;
	}
	if (key_is(key, "FocusNow") || key_is(key, "AutoFocusNow") || key_is(key, "ContinuousAutoFocusNow") || key_is(key, "ManualFocusNow") ||
	    key_is(key, "LockAFNow") || key_is(key, "AFWindowParams") || key_is(key, "AutoFocusHintNow") || key_is(key, "PeakTrackingFocusNow")) {
		/*
		 * CMCapture's focus (FigImageControl_Focus, _doFocusNow ...): only sent to a stream that lists these, which this one does only with
		 * /var/db/nord2-camera-focusprops (what else CMCapture then does -- its own autofocus processor reads the frames' metadata -- is not
		 * known). What comes is said in full, a window in thousandths (hStart, vStart, width, height, as RegionOfInterestForCameraControls
		 * has them) is taken as the place to focus on, and every ...Now looks for the sharpest position again.
		 */
		static unsigned said;
		char name[64], text[300];

		key_name(key, name, sizeof(name));
		describe(value, text, sizeof(text));
		if (said < 60) {
			said++;
			rwcam_say("stream: focus: %s = %s", name, text);
		}
		CFDictionaryRef d = value != NULL && CFGetTypeID(value) == CFDictionaryGetTypeID() ? value : NULL;
		int64_t x = get_i(d, "hStart", -1), y = get_i(d, "vStart", -1), w = get_i(d, "width", -1), h = get_i(d, "height", -1);
		bool now = !key_is(key, "AFWindowParams") && !key_is(key, "LockAFNow");

		if (x >= 0 && y >= 0 && w > 0 && h > 0 && w <= 1000 && h <= 1000) {
			float z = view_zoom(s);

			/* (thousandths of what is seen: the middle 1/zoom of the sensor) */
			rwcam_core_focus_window(s->cam, 0.5f + (((float)x + (float)w / 2) / 1000 - 0.5f) / z, 0.5f + (((float)y + (float)h / 2) / 1000 - 0.5f) / z,
			    (float)w / 1000 / z, (float)h / 1000 / z, now);
		} else if (now) {
			rwcam_core_focus_window(s->cam, 0.5f, 0.5f, 0.25f / view_zoom(s), 0.25f / view_zoom(s), true);
		}
		return 0;
	}
	if (key_is(key, "SynchronizedStreamsSlaveConfiguration")) {
		CFDictionaryRef d = value != NULL && CFGetTypeID(value) == CFDictionaryGetTypeID() ? value : NULL;
		struct group *g = s->group;
		char text[300];

		describe(value, text, sizeof(text));
		rwcam_say("stream: %s: set SynchronizedStreamsSlaveConfiguration = %s", cams[s->cam].name, text);
		if (d == NULL) {
			rwcam_say("stream: %s: set SynchronizedStreamsSlaveConfiguration: not a dictionary, ANSWERED %d", cams[s->cam].name, (int)ERR_PARAM);
			return ERR_PARAM;
		}
		stream_slave_configuration(s, d);
		if (g != NULL && s->cam != RWCAM_FRONT && rwcam_pair_master(&g->pair) != s->cam) {
			rwcam_pair_set_slave(&g->pair, get_i(d, "StreamingOutputsEnabled", 0) != 0, (unsigned)get_i(d, "MasterToSlaveFrameRateRatio", 1));
		}
		return 0;
	}
	{
		char what[200];

		snprintf(what, sizeof(what), "stream: %s: set (taken, not used)", cams[s->cam].name);
		say_unknown(what, key, value);
	}
	return 0;
}

/*
 * STILL CAPTURE, the stream's side (run q10hz: the shutter did nothing; the still image coordinator stayed in "Capturing" until the session ended).
 * -[BWFigVideoCaptureDevice _captureStillImageNowOnStreamWithOptions:captureStreamProperties:] (0x1a1f4a048) sets StillImageCaptureNow on the
 * stream with a dictionary of options, and the stream answers with pictures on its StillImage output: as many as the capture's
 * expectedFrameCaptureCount (-[BWFigVideoCaptureStream _prepareForStillImageCaptureWithStillImageSettings:fromCMTimeMachine:], 0x1a2081f38;
 * what comes with no capture pending, or after the last one, is discarded by -sourceNodeShouldDiscardStillImageSampleBuffer:..., 0x1a2081b7c).
 * In q10hz five did come and went through to BWGNRNode, which gave none of them on and said nothing: for every capture type but the single
 * picture it takes a frame only if its metadata's BracketedCaptureSequenceNumber is 1 ... the count it expects (-renderSampleBuffer:forInput:,
 * 0x1a1f18718: cmp w20, #1; b.lt -> return), and that number is the FIRMWARE's to give -- CMCapture adds it itself for the flash capture only
 * (-_addBracketecCaptureSequenceNumberToMetadataIfNeededForStillImageSampleBuffer:, 0x1a2081cb8). So: from the request on, every StillImage
 * picture is given at once (not one a second) and, when the request is a bracket (its options have AEBracketedCaptureParams,
 * UnifiedBracketedCaptureParams or TimeMachineBracketedCaptureParams), carries its number, 1, 2, 3 ...; with ProvidePreBracketedFrame the
 * first is the frame before the bracket instead (PreBracketingFrame, no number). A request for one picture gets pictures without a number. The frames are the sensor's ordinary ones, all of one exposure: there is no bracket of
 * exposures here, and what is fused is as many pictures of the same exposure.
 */
#define STILL_TAG               "RedwoodStillSequence"         /* on the picture's buffer: its number (-1: the frame before the bracket) */
#define STILL_ASKED_MS          6000
#define STILL_MOST              8

static bool
truth_in(CFTypeRef d, const char *name)
{
	CFStringRef k;
	CFTypeRef v;
	int64_t n = 0;

	if (d == NULL || CFGetTypeID(d) != CFDictionaryGetTypeID()) {
		return false;
	}
	k = cfs(name);
	v = CFDictionaryGetValue(d, k);
	CFRelease(k);
	if (v == NULL) {
		return false;
	}
	if (CFGetTypeID(v) == CFBooleanGetTypeID()) {
		return CFBooleanGetValue(v);
	}
	return CFGetTypeID(v) == CFNumberGetTypeID() && CFNumberGetValue(v, kCFNumberSInt64Type, &n) && n != 0;
}

static void
stream_still_now(struct stream *s, CFTypeRef options)
{
	/* (not among the bundle's CoreFoundation imports: asked for by name, as rwcam_diag.c does) */
	CFTypeID (*array_id)(void) = (CFTypeID (*)(void))dlsym(RTLD_DEFAULT, "CFArrayGetTypeID");
	CFTypeID array = array_id != NULL ? array_id() : (CFTypeID)-1;
	static const char *const brackets[3] = { "AEBracketedCaptureParams", "UnifiedBracketedCaptureParams", "TimeMachineBracketedCaptureParams" };
	char text[400] = "";
	size_t at = 0;
	int count = 0;
	bool numbered = false;

	/* the options' keys (run q10ia: their whole description is longer than any line here), an array's with its count */
	if (options != NULL && CFGetTypeID(options) == CFDictionaryGetTypeID()) {
		CFIndex n = CFDictionaryGetCount(options);
		const void **k = calloc((unsigned long)n + 1, sizeof(void *)), **v = calloc((unsigned long)n + 1, sizeof(void *));

		if (k != NULL && v != NULL) {
			CFDictionaryGetKeysAndValues(options, k, v);
			for (CFIndex i = 0; i < n && at + 80 < sizeof(text); i++) {
				char name[64] = "?";

				if (CFGetTypeID(k[i]) == CFStringGetTypeID()) {
					CFStringGetCString(k[i], name, sizeof(name), kCFStringEncodingUTF8);
				}
				at += (size_t)snprintf(text + at, sizeof(text) - at, CFGetTypeID(v[i]) == array ? "%s[%ld] " : "%s ", name,
				    CFGetTypeID(v[i]) == array ? (long)CFArrayGetCount(v[i]) : 0L);
			}
		}
		free(k);
		free(v);
	}
	for (int i = 0; i < 3; i++) {
		CFStringRef key = cfs(brackets[i]);
		CFTypeRef b = options != NULL && CFGetTypeID(options) == CFDictionaryGetTypeID() ? CFDictionaryGetValue(options, key) : NULL;

		CFRelease(key);
		if (b != NULL) {
			numbered = true;
			count = CFGetTypeID(b) == array && count == 0 ? (int)CFArrayGetCount(b) : count;
		}
	}
	/*
	 * The dual-wide device at "0.5x" (run user1: no photograph there): the ultra-wide stream's node has no still image output (rwcam_diag.c's
	 * still fix: this model's still pipeline has no input for it), so a photograph asked of it is the wide stream's to give -- and the wide
	 * stream's StillImage is made of whichever sensor is shown (frame_sink).
	 */
	if (s->group != NULL && !(s->out[2].handler != NULL && s->out[2].config != NULL && s->out[2].enabled)) {
		struct stream *o = stream_of(s->group->stream[s->cam != RWCAM_BACK_ULTRAWIDE]);

		if (o != s && o->out[2].handler != NULL && o->out[2].config != NULL && o->out[2].enabled) {
			rwcam_say_keep("stream: %s: StillImageCaptureNow: no still output here, the photograph is %s's", cams[s->cam].name, cams[o->cam].name);
			s = o;
		}
	}
	pthread_mutex_lock(&s->lock);
	s->still_numbered = numbered;
	s->still_pre = numbered && truth_in(options, "ProvidePreBracketedFrame");
	s->still_most = count > 0 && count < STILL_MOST ? count : numbered ? STILL_MOST : 2;
	s->still_next = 1;
	s->still_until_ms = rwcam_now_ms() + STILL_ASKED_MS;
	s->stills_asked++;
	pthread_mutex_unlock(&s->lock);
	rwcam_say_keep("stream: %s: StillImageCaptureNow %llu: StillImage pictures now given at once%s%s, %d of them at most; output %s; options: %s",
	    cams[s->cam].name, (unsigned long long)s->stills_asked, numbered ? ", numbered from 1 (a bracket)" : ", not numbered (one picture)",
	    s->still_pre ? " after one frame before the bracket" : "", s->still_most, s->out[2].handler == NULL ? "HAS NO HANDLER" :
	    s->out[2].config == NULL ? "HAS NO CONFIGURATION" : s->out[2].enabled ? "enabled" : "NOT enabled by the graph", text);
}

/* the flashlight: TorchLevel (0 to 1) as thousandths to /dev/redwood-torch where the kernel has one (not part of the camera ABI) */
static void
torch_set(struct stream *s, CFTypeRef value)
{
	float level = 0;
	uint32_t thousandths;

	if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID()) {
		CFNumberGetValue(value, kCFNumberFloatType, &level);
	}
	level = level < 0 ? 0 : level > 1 ? 1 : level;
	thousandths = (uint32_t)(level * 1000 + 0.5f);
	if (s->torch == -1) {
		s->torch = access("/dev/redwood-torch", F_OK) == 0 ? open("/dev/redwood-torch", O_WRONLY) : -2;
	}
	if (s->torch < 0 || write(s->torch, &thousandths, sizeof(thousandths)) != (long)sizeof(thousandths)) {
		rwcam_say("the flashlight at %u of 1000 NOT TAKEN (no /dev/redwood-torch)", thousandths);
	}
}

/* ---- the frames ---------------------------------------------------------------------------------------------- */

/* the buffer's planes for the processor (rwcam_render.c asks when it is the one to write; the caller unlocks what got mapped) */
static bool
map_pixel_buffer(struct rwcam_picture *pic)
{
	CVPixelBufferRef pb = pic->context;

	if (CVPixelBufferLockBaseAddress(pb, 0) != kCVReturnSuccess) {
		return false;
	}
	if (pic->format == PF_BGRA) {
		pic->plane[0] = CVPixelBufferGetBaseAddress(pb);
		pic->stride[0] = CVPixelBufferGetBytesPerRow(pb);
	} else {
		pic->plane[0] = CVPixelBufferGetBaseAddressOfPlane(pb, 0);
		pic->stride[0] = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
		pic->plane[1] = CVPixelBufferGetBaseAddressOfPlane(pb, 1);
		pic->stride[1] = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
	}
	return true;
}

static struct rwcam_picture
picture_of(CVPixelBufferRef pb, uint32_t pf, int width, int height)
{
	struct rwcam_picture pic;

	memset(&pic, 0, sizeof(pic));
	pic.format = pf;
	pic.width = (uint32_t)width;
	pic.height = (uint32_t)height;
	pic.iosurface = cv_get_iosurface != NULL ? cv_get_iosurface(pb) : NULL;
	pic.map = map_pixel_buffer;
	pic.context = pb;
	return pic;
}

static CVPixelBufferRef
output_buffer(struct output *out, int width, int height, uint32_t pf)
{
	CVPixelBufferRef pb = NULL;

	if (out->pool == NULL || out->pool_width != width || out->pool_height != height || out->pool_format != pf) {
		CFMutableDictionaryRef a = new_dict();
		CFNumberRef w = number_i(width), h = number_i(height), p = number_i(pf);
		CFMutableDictionaryRef io = new_dict();

		if (out->pool != NULL) {
			CVPixelBufferPoolRelease(out->pool);
			out->pool = NULL;
		}
		CFDictionarySetValue(a, kCVPixelBufferWidthKey, w);
		CFDictionarySetValue(a, kCVPixelBufferHeightKey, h);
		CFDictionarySetValue(a, kCVPixelBufferPixelFormatTypeKey, p);
		CFDictionarySetValue(a, kCVPixelBufferIOSurfacePropertiesKey, io);
		CFRelease(w);
		CFRelease(h);
		CFRelease(p);
		CFRelease(io);
		if (CVPixelBufferPoolCreate(NULL, NULL, a, &out->pool) != kCVReturnSuccess) {
			out->pool = NULL;
		}
		CFRelease(a);
		out->pool_width = width;
		out->pool_height = height;
		out->pool_format = pf;
		rwcam_say("%s: %dx%d %c%c%c%c buffers%s", out->name, width, height, (int)(pf >> 24), (int)(pf >> 16 & 255), (int)(pf >> 8 & 255),
		    (int)(pf & 255), out->pool ? "" : " CANNOT BE MADE");
	}
	if (out->pool == NULL || CVPixelBufferPoolCreatePixelBuffer(NULL, out->pool, &pb) != kCVReturnSuccess) {
		return NULL;
	}
	return pb;
}

/*
 * Diagnostics of run q10ha (the viewfinder black although BWImageQueueSinkNode said "Did display first frame"): every frame the kernel gave was black
 * itself (green 2 to 3 of 959 above black at the exposure the sensor starts with, and AE never got further), so a black viewfinder said nothing about
 * the way from here to the screen. Two things for the next run:
 *   - the mean luma of the pictures given is said (the first SAY_PICTURES of an output, then every SAY_PICTURE_EVERY-th), with the time the picture
 *     took to make and the time the graph's handler took;
 *   - a PrimaryScaler picture that is black (mean luma under MARK_LUMA) gets a band of eight grey bars across its middle and a white square above it
 *     that moves one place with every picture: seen in Camera.app, the pictures reach the screen and the square says how often; not seen, they do
 *     not. Nothing is drawn into a picture that has something in it. /var/db/nord2-camera-nomark switches the drawing off.
 */
#define SAY_PICTURES            10
#define SAY_PICTURE_EVERY       150
#define MARK_LUMA               12
#define ZOOM_OF_PICTURE         "RedwoodZoomOfPicture"
#define BLACK_LUMA              4       /* a picture whose mean luma is under this is black */
#define NOT_BLACK_GREEN         24      /* a frame whose mean green above black (of 1023, the kernel's header) is this or more is not */
/*
 * StillImage is 4032x3024 from the same frame: seven times the preview's pixels, made on the reader's thread before it can read the next frame (run
 * q10ha: BWIrisStagingNode counted 4.86 frames a second in a run of frames). The graph takes it all the time (it is the zero-shutter-lag source) but
 * nothing shows it: one at most every STILL_MIN_MS. /var/db/nord2-camera-still-all gives every frame as before.
 */
#define STILL_MIN_MS            1000
#define SAY_MS                  5000
#define PREVIEW_PIECES          24      /* (small pieces: the cores are not equally fast, and a picture is done when its slowest piece is) */
#define STILL_PIECES            12
#define RW_QOS_UTILITY          0x11
static int no_mark = -1, still_all = -1, still_sync = -1;

/*
 * StillImage off the reader's thread (run q10hc: 95 to 227 ms each on it, every 600 to 790 ms -- the sensor's frames of that time not read, the
 * viewfinder's longest gaps: 180 to 210 ms about 1.6 times a second). The frame is copied (16 MB) and the picture made by a thread of its own, at
 * utility priority, and given to the graph's handler from there; one at a time, at most one every STILL_MIN_MS. /var/db/nord2-camera-still-sync
 * makes them on the reader's thread as before.
 */
static struct {
	int             busy;
	uint8_t         *copy;
	size_t          room;
	struct isp      isp;
	CVPixelBufferRef pb;
	const void      *handler;
	uint32_t        pf;
	int             width, height;
	const char      *name;
	uint64_t        made, ms, ms_max, dropped;
	struct rwcam_renderer render;           /* (the still thread's own: a GPU context is one thread's at a time) */
	pthread_t       thread;
	volatile bool   on_thread;
} still = { .render = { .name = "StillImage" } };

#define PIECES_THREADS          4
static int pieces_all = -1;
struct pieces_job {
	size_t          n, next;
	void            *context;
	void            (*work)(void *, size_t);
};

static void
pieces_thread(void *context, size_t thread)
{
	struct pieces_job *j = context;

	(void)thread;
	for (;;) {
		size_t piece = __atomic_fetch_add(&j->next, 1, __ATOMIC_RELAXED);

		if (piece >= j->n) {
			return;
		}
		j->work(j->context, piece);
	}
}

/* rwcam_render.c's pieces on the cores: the viewfinder's at once, a still's (made on the still thread) when there is time */
static void
apply_pieces(size_t n, void *context, void (*work)(void *, size_t))
{
	bool of_still = still.on_thread && pthread_equal(pthread_self(), still.thread);
	struct pieces_job j = { n, 0, context, work };

	/*
	 * No more than PIECES_THREADS threads at the pieces (run user6: on all eight cores the pictures left the graph's encoder 21 a second of
	 * a film's 30, and the cores 56 % busy): each takes the next piece until none is left. /var/db/nord2-camera-allcores: one a piece.
	 */
	if (pieces_all < 0) {
		pieces_all = access("/var/db/nord2-camera-allcores", F_OK) == 0;
	}
	if (pieces_all == 1) {
		dispatch_apply_f(n, dispatch_get_global_queue(of_still ? RW_QOS_UTILITY : RW_QOS_USER_INTERACTIVE, 0), context, work);
		return;
	}
	dispatch_apply_f(n < PIECES_THREADS ? n : PIECES_THREADS, dispatch_get_global_queue(of_still ? RW_QOS_UTILITY : RW_QOS_USER_INTERACTIVE, 0),
	    &j, pieces_thread);
}
/*
 * The test pattern (run q10hb: good pictures, 11 of 11 taken by BWImageQueueSinkNode and reported displayed, the screen black). Only when
 * /var/db/nord2-camera-testpattern exists (it was on by itself in the builds for runs q10hb and q10hc). Every PrimaryScaler picture gets, over its first third, sixteen black and white bars that move half a
 * bar with each picture, and under them a strip of eight greys; the rest stays the camera's picture (grey before the first frame). And the pictures
 * do not wait for the sensor: a pacer gives one every 1/TEST_FPS s -- the last picture again, bars moved on -- whenever the sensor has not, and
 * every PrimaryScaler picture's time is the time it is given at. So the viewfinder gets an even, well-timed flow whatever the kernel's DMA does:
 * moving bars on the screen = the way from here to the screen works.
 */
#define TEST_FPS                15
static int test_pattern = -1;
extern uint64_t mach_absolute_time(void);
static pthread_mutex_t pace_lock = PTHREAD_MUTEX_INITIALIZER;   /* held by the pacer while it uses a stream, and by whoever takes a stream out of active[] */
static bool pacer_running;
static pthread_t pacer_thread;

static void
pattern(struct stream *s, CVPixelBufferRef pb, int width, int height, uint64_t count, bool made_up)
{
	uint8_t *y = CVPixelBufferGetBaseAddressOfPlane(pb, 0), *uv = CVPixelBufferGetBaseAddressOfPlane(pb, 1);
	size_t ys = CVPixelBufferGetBytesPerRowOfPlane(pb, 0), uvs = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
	int cw = (width + 1) & ~1, ch = (height + 1) / 2;

	if (y == NULL || uv == NULL || width < 64 || height < 64 || ys < (size_t)width || uvs < (size_t)cw) {
		return;
	}
	if (!made_up) {
		/* a picture of the sensor: kept for the pacer */
		if (s->last_y == NULL || s->last_w != width || s->last_h != height) {
			free(s->last_y);
			free(s->last_uv);
			s->last_y = malloc((size_t)width * (size_t)height);
			s->last_uv = malloc((size_t)cw * (size_t)ch);
			s->last_w = width;
			s->last_h = height;
		}
		for (int r = 0; s->last_y != NULL && r < height; r++) {
			memcpy(s->last_y + (size_t)r * (size_t)width, y + (size_t)r * ys, (size_t)width);
		}
		for (int r = 0; s->last_uv != NULL && r < ch; r++) {
			memcpy(s->last_uv + (size_t)r * (size_t)cw, uv + (size_t)r * uvs, (size_t)cw);
		}
	} else if (s->last_y != NULL && s->last_uv != NULL && s->last_w == width && s->last_h == height) {
		for (int r = 0; r < height; r++) {
			memcpy(y + (size_t)r * ys, s->last_y + (size_t)r * (size_t)width, (size_t)width);
		}
		for (int r = 0; r < ch; r++) {
			memcpy(uv + (size_t)r * uvs, s->last_uv + (size_t)r * (size_t)cw, (size_t)cw);
		}
	} else {
		for (int r = 0; r < height; r++) {
			memset(y + (size_t)r * ys, 110, (size_t)width);
		}
		for (int r = 0; r < ch; r++) {
			memset(uv + (size_t)r * uvs, 128, (size_t)cw);
		}
	}
	int bars = height / 3, greys = height / 16, shift = (int)(count % 32) * width / 32;

	for (int r = 0; r < bars + greys; r++) {
		uint8_t *row = y + (size_t)r * ys;

		for (int c = 0; c < width; c++) {
			row[c] = r < bars ? ((((c + shift) % width) * 16 / width) & 1 ? 235 : 16) : (uint8_t)(30 + 29 * (c * 8 / width));
		}
	}
	for (int r = 0; r < (bars + greys + 1) / 2 && r < ch; r++) {
		memset(uv + (size_t)r * uvs, 128, (size_t)cw);
	}
}

static unsigned
mean_luma(CVPixelBufferRef pb, uint32_t pf, int width, int height)
{
	const uint8_t *y = pf == PF_BGRA ? CVPixelBufferGetBaseAddress(pb) : CVPixelBufferGetBaseAddressOfPlane(pb, 0);
	size_t stride = pf == PF_BGRA ? CVPixelBufferGetBytesPerRow(pb) : CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
	uint64_t sum = 0, n = 0;

	if (y == NULL || width < 64 || height < 64) {
		return 0;
	}
	for (int r = height / 64; r < height; r += height / 32) {
		for (int c = width / 64; c < width; c += width / 32) {
			sum += pf == PF_BGRA ? y[(size_t)r * stride + 4 * (size_t)c + 1] : y[(size_t)r * stride + (size_t)c];
			n++;
		}
	}
	return n ? (unsigned)(sum / n) : 0;
}

/* the bars and the square (luma only: 4:2:0 pictures) */
static void
mark(CVPixelBufferRef pb, int width, int height, uint64_t count)
{
	uint8_t *y = CVPixelBufferGetBaseAddressOfPlane(pb, 0);
	size_t stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
	int side = height / 8, places = side > 0 ? width / side : 0;

	if (y == NULL || side < 8 || places < 2 || stride < (size_t)width) {
		return;
	}
	for (int r = height * 7 / 16; r < height * 9 / 16; r++) {
		for (int c = 0; c < width; c++) {
			y[(size_t)r * stride + (size_t)c] = (uint8_t)(40 + 27 * (c * 8 / width));
		}
	}
	int x0 = (int)(count % (uint64_t)places) * side;

	for (int r = height * 4 / 16; r < height * 4 / 16 + side; r++) {
		memset(y + (size_t)r * stride + (size_t)x0, 235, (size_t)side);
	}
}

static void *
still_thread(void *unused)
{
	const struct redwood_camera_frame *f = (const void *)still.copy;
	uint64_t t0 = rwcam_now_ms(), t1;
	struct rwcam_picture pic = picture_of(still.pb, still.pf, still.width, still.height);

	(void)unused;
	pthread_setname_np("redwood-camstill");
	still.thread = pthread_self();
	still.on_thread = true;
	(void)rwcam_render(&still.render, &still.isp, f, still.copy + f->header_bytes, &pic, STILL_PIECES);
	still.on_thread = false;
	if (!pic.mapped) {
		(void)CVPixelBufferLockBaseAddress(still.pb, 0);        /* (after the GPU wrote the planes: for whoever goes by the surface's seed) */
	}
	CVPixelBufferUnlockBaseAddress(still.pb, 0);
	CVBufferSetAttachment(still.pb, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_601_4, kCVAttachmentMode_ShouldPropagate);
	CVBufferSetAttachment(still.pb, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
	CVBufferSetAttachment(still.pb, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_709_2,
	    kCVAttachmentMode_ShouldPropagate);
	rw_call_pixel_handler(still.handler, still.pb, CMClockMakeHostTimeFromSystemUnits(f->time));
	t1 = rwcam_now_ms();
	CVPixelBufferRelease(still.pb);
	CFRelease(still.handler);
	still.made++;
	still.ms += t1 - t0;
	still.ms_max = t1 - t0 > still.ms_max ? t1 - t0 : still.ms_max;
	if (still.made <= 2) {
		rwcam_say("%s: picture %llu (frame %llu) made off the reader's thread in %llu ms", still.name, (unsigned long long)still.made,
		    (unsigned long long)f->sequence, (unsigned long long)(t1 - t0));
	}
	__atomic_store_n(&still.busy, 0, __ATOMIC_RELEASE);
	return NULL;
}

/* takes over pb; false (pb given back) when a still is being made or the thread cannot be */
static bool
still_start(struct output *out, CVPixelBufferRef pb, uint32_t pf, int width, int height, const struct isp *isp, const struct redwood_camera_frame *f)
{
	size_t bytes = (size_t)f->header_bytes + (size_t)f->stride * f->height;
	pthread_t t;
	int idle = 0;

	if (!__atomic_compare_exchange_n(&still.busy, &idle, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		still.dropped++;
		CVPixelBufferRelease(pb);
		return false;
	}
	if (still.room < bytes) {
		free(still.copy);
		still.copy = malloc(bytes);
		still.room = still.copy != NULL ? bytes : 0;
	}
	if (still.copy == NULL) {
		CVPixelBufferRelease(pb);
		__atomic_store_n(&still.busy, 0, __ATOMIC_RELEASE);
		return false;
	}
	memcpy(still.copy, f, bytes);
	still.isp = *isp;
	still.pb = pb;
	still.handler = CFRetain(out->handler);
	still.pf = pf;
	still.width = width;
	still.height = height;
	still.name = out->name;
	if (pthread_create(&t, NULL, still_thread, NULL) != 0) {
		CFRelease(still.handler);
		CVPixelBufferRelease(pb);
		__atomic_store_n(&still.busy, 0, __ATOMIC_RELEASE);
		return false;
	}
	pthread_detach(t);
	return true;
}

/*
 * A film (run q10ig: video mode): the graph has two scalers on, the film's pictures (1920x1080) and the viewfinder's. The second one of a frame
 * is not made from the sensor's frame again: it is the first one's, made smaller (isp_shrink_420), when it is no larger, has its proportions (to
 * a hundredth) and both are 4:2:0 -- every output of a stream shows the same view. /var/db/nord2-camera-notwin: each makes its own.
 */
#define TWIN_PIECES             16
#define HELD_MOST               12      /* (a film's pictures with the graph: 0.4 s of them) */
static int no_twin = -1;
struct twin_job {
	const uint8_t   *from[2];
	size_t          from_stride[2], to_stride[2];
	uint8_t         *to[2];
	uint32_t        from_width, from_height, width, height;
	int             range;
};

static void
twin_piece(void *context, size_t piece)
{
	const struct twin_job *j = context;
	uint32_t r0 = (uint32_t)(j->height * piece / TWIN_PIECES) & ~1u, r1 = (uint32_t)(j->height * (piece + 1) / TWIN_PIECES) & ~1u;

	isp_shrink_420(j->from, j->from_stride, j->from_width, j->from_height, j->to, j->to_stride, j->width, j->height, r0,
	    piece + 1 == TWIN_PIECES ? j->height : r1, j->range);
}

static bool
twin_made(struct stream *s, struct rwcam_picture *pic, const struct redwood_camera_frame *f)
{
	CVPixelBufferRef from = s->twin;
	struct twin_job j;
	int64_t off;

	if (from == NULL || s->twin_seq != f->sequence || pic->format == PF_BGRA || (int)pic->width > s->twin_width ||
	    (int)pic->height > s->twin_height) {
		return false;
	}
	off = (int64_t)pic->width * s->twin_height - (int64_t)pic->height * s->twin_width;
	off = off < 0 ? -off : off;
	if (off * 100 > (int64_t)pic->width * s->twin_height || CVPixelBufferLockBaseAddress(from, 1 /* read only */) != kCVReturnSuccess) {
		return false;
	}
	if (!pic->mapped && !(pic->mapped = pic->map(pic))) {
		CVPixelBufferUnlockBaseAddress(from, 1);
		return false;
	}
	for (int i = 0; i < 2; i++) {
		j.from[i] = CVPixelBufferGetBaseAddressOfPlane(from, (size_t)i);
		j.from_stride[i] = CVPixelBufferGetBytesPerRowOfPlane(from, (size_t)i);
		j.to[i] = pic->plane[i];
		j.to_stride[i] = pic->stride[i];
	}
	j.from_width = (uint32_t)s->twin_width;
	j.from_height = (uint32_t)s->twin_height;
	j.width = pic->width;
	j.height = pic->height;
	j.range = s->twin_format == pic->format ? 0 : pic->format == PF_420F ? 1 : 2;
	if (j.from[0] != NULL && j.from[1] != NULL && j.to[0] != NULL && j.to[1] != NULL) {
		apply_pieces(TWIN_PIECES, &j, twin_piece);
		s->twins++;
	}
	CVPixelBufferUnlockBaseAddress(from, 1);
	return j.from[0] != NULL && j.from[1] != NULL && j.to[0] != NULL && j.to[1] != NULL;
}

/*
 * Heat (run user1: 3 to 5 of the 8 cores busy while the camera shows, most of it the ISP's passes over the sensor's frame, 20 ms on all cores
 * for a 1504x1128 picture and 55 to 75 with the noise taken out). The streaming pictures are made SMALLER than asked -- 3/5 a side for a
 * photograph's viewfinder (902x676: the screen is 1080 wide), 2/3 for a film (1280x720 for 1920x1080) -- ISP and noise both, and then made
 * the size asked by isp_shrink_420 (which makes larger too). Not the photographs, not when the GPU makes the pictures.
 * /var/db/nord2-camera-fullsize: as asked.
 */
static int small_on = -1;

static int
small_made(struct stream *s, int index, struct output *out, const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw,
    struct rwcam_picture *pic)
{
	struct rwcam_picture sp;
	struct twin_job j;
	int by, num = s->format_index != 1 ? 3 : 2, den = s->format_index != 1 ? 5 : 3;
	uint32_t w = (pic->width * (uint32_t)num / (uint32_t)den) & ~1u, h = (pic->height * (uint32_t)num / (uint32_t)den) & ~1u;
	size_t room = (size_t)w * h * 3 / 2;

	if (small_on != 1 || pic->format == PF_BGRA || w < 320 || h < 240) {
		return -2;
	}
	if (out->small_room != room) {
		free(out->small);
		out->small = malloc(room);
		out->small_room = out->small != NULL ? room : 0;
	}
	if (out->small == NULL || (!pic->mapped && !(pic->mapped = pic->map(pic))) || pic->plane[0] == NULL || pic->plane[1] == NULL) {
		return -2;
	}
	memset(&sp, 0, sizeof(sp));
	sp.format = pic->format;
	sp.width = w;
	sp.height = h;
	sp.plane[0] = out->small;
	sp.plane[1] = out->small + (size_t)w * h;
	sp.stride[0] = sp.stride[1] = w;
	sp.mapped = true;
	if ((by = rwcam_render(&s->render[index], isp, f, raw, &sp, PREVIEW_PIECES)) < 0) {
		return -2;
	}
	for (int i = 0; i < 2; i++) {
		j.from[i] = sp.plane[i];
		j.from_stride[i] = w;
		j.to[i] = pic->plane[i];
		j.to_stride[i] = pic->stride[i];
	}
	j.from_width = w;
	j.from_height = h;
	j.width = pic->width;
	j.height = pic->height;
	j.range = 0;
	apply_pieces(TWIN_PIECES, &j, twin_piece);
	return by;
}

static void
deliver(struct stream *s, const struct stream *of, int index, struct output *out, const struct isp *isp, const struct redwood_camera_frame *f,
    const uint8_t *raw)
{
	/* s: the stream whose output the picture is given to; of: the stream of the SENSOR the frame is of (the same, but for the dual-wide device) */
	int width = (int)get_i(out->config, "Width", 0), height = (int)get_i(out->config, "Height", 0);
	uint32_t pf = (uint32_t)get_i(out->config, "PixelFormat", PF_420F);
	CVPixelBufferRef pb = NULL;
	CFStringRef pk = cfs("BufferPool");
	CFTypeRef gp = out->config != NULL ? CFDictionaryGetValue(out->config, pk) : NULL;

	CFRelease(pk);
	bool made_up = f == NULL, test = index == 0 && test_pattern == 1;
	struct isp view;                /* the camera's ISP with this stream's zoom and flip */
	float zoom_now = 1;

	out->asked_width = width;
	out->asked_height = height;
	out->asked_format = pf;
	out->why_not = NULL;

	if (!made_up) {
		view = *isp;
		zoom_now = of->zoom;
		view.zoom = zoom_view_at(of, zoom_now, of->zoom_of_group, &view.shift_x, &view.shift_y);
		view.flip = of->flip;
		isp = &view;
	}

	/*
	 * The graph gives each output a buffer pool of its own (VideoOutputConfigurations' "BufferPool", with "RetainedBufferCount"): the pictures are made
	 * in buffers from that pool, whose attributes say their size and format. The older shape of the configuration (Width, Height, PixelFormat) is the
	 * fallback.
	 */
	if (gp != NULL && pool_type_id != NULL && CFGetTypeID(gp) == pool_type_id()) {
		CFDictionaryRef attrs = CVPixelBufferPoolGetPixelBufferAttributes((CVPixelBufferPoolRef)gp);

		width = (int)get_i(attrs, "Width", 0);
		height = (int)get_i(attrs, "Height", 0);
		pf = (uint32_t)get_i(attrs, "PixelFormatType", PF_420F);
		out->asked_width = width;
		out->asked_height = height;
		out->asked_format = pf;
		if (pf != PF_420F && pf != PF_420V && pf != PF_BGRA) {
			/* (a compressed or 10-bit format negotiated with a sink: not one the ISP here can write; nothing is written into such a buffer) */
			char text[64];

			snprintf(text, sizeof(text), "%c%c%c%c (%#x)", (int)(pf >> 24), (int)(pf >> 16 & 255), (int)(pf >> 8 & 255), (int)(pf & 255), pf);
			rwcam_say_once("stream: the graph's buffer pool has a pixel format not made here, NO PICTURES for", out->name, text);
			out->why_not = "the pool's pixel format is not made here";
			return;
		}
		if (width <= 0 || height <= 0) {
			out->why_not = "the pool's attributes have no size";
			return;
		}
		if (CVPixelBufferPoolCreatePixelBuffer(NULL, (CVPixelBufferPoolRef)gp, &pb) != kCVReturnSuccess) {
			out->why_not = "the graph's pool gave no buffer (all of them out: the graph does not give them back?)";
			return;
		}
		buffer_counted(pb, &out->out_there, &out->counter);
		/*
		 * (q10jn) the buffer's own pixel format: run cam10 -- the pool's attributes gave no single PixelFormatType (so '420f' was assumed)
		 * while its buffers were '420v' (video range): the GPU refused every one (E_SURFACE: "the render was asked for '420f'") and the
		 * processor wrote full-range levels into video-range buffers.
		 */
		{
			uint32_t own = CVPixelBufferGetPixelFormatType(pb);   /* (CoreVideo is a weak library: no CoreVideo, no camera at all) */

			if ((own == PF_420F || own == PF_420V || own == PF_BGRA) && own != pf) {
				if (out->format_said++ < 2) {
					rwcam_say("%s: the pool's buffers are %c%c%c%c, its attributes said %c%c%c%c: made as the buffers are", out->name,
					    (int)(own >> 24), (int)(own >> 16 & 255), (int)(own >> 8 & 255), (int)(own & 255), (int)(pf >> 24),
					    (int)(pf >> 16 & 255), (int)(pf >> 8 & 255), (int)(pf & 255));
				}
				pf = own;
				out->asked_format = pf;
			}
		}
		if (out->delivered == 0) {
			rwcam_say("%s: the graph's own buffer pool: %dx%d %c%c%c%c", out->name, width, height, (int)(pf >> 24), (int)(pf >> 16 & 255),
			    (int)(pf >> 8 & 255), (int)(pf & 255));
		}
	} else {
		if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
			out->why_not = "the output's configuration has no size (and no pool)";
			return;
		}
		pf = pf == PF_420V || pf == PF_BGRA ? pf : PF_420F;
		if ((pb = output_buffer(out, width, height, pf)) == NULL) {
			out->why_not = "no buffer of our own could be made";
			return;
		}
	}
	uint64_t t0 = rwcam_now_ms(), t1, t2;
	unsigned luma;
	bool marked = false;

	/* a photograph's frame (stream_still_now): its number goes with the buffer, for stream_add_attachments to put into the metadata */
	bool still_tagged = false;

	if (index == 2 && !made_up && s->still_next != 0) {
		CFStringRef k = cfs(STILL_TAG);
		/* (-1: the frame before a bracket; 0: a photograph of one picture, no number -- run user4: with no tag at all the front camera's had no
		 * metadata, and -[BWStillImageCoordinatorNode renderSampleBuffer:forInput:]'s block, 0x1a1f1c7b0, used its nil PortType as a key:
		 * NSInvalidArgumentException, mediaserverd gone at every shutter of the front camera) */
		CFNumberRef n = number_i(s->still_pre ? -1 : s->still_numbered ? s->still_next : 0);

		CVBufferSetAttachment(pb, k, n, 0 /* kCVAttachmentMode_ShouldNotPropagate */);
		CFRelease(n);
		CFRelease(k);
		still_tagged = true;
	}
	if (index == 2 && !made_up && still_sync == 0) {
		if (still_start(out, pb, pf, width, height, isp, f)) {
			if (out->delivered++ == 0) {
				rwcam_say("%s: the first picture given (%dx%d)", out->name, width, height);
			}
			out->last_ms = t0;
			if (still_tagged) {
				rwcam_say_keep("%s: picture %d of the photograph asked for%s (frame %llu, %dx%d)", out->name, s->still_next, s->still_pre ?
				    ": the frame before the bracket" : s->still_numbered ? ", numbered" : ", not numbered", (unsigned long long)f->sequence,
				    width, height);
				if (s->still_pre) {
					s->still_pre = false;
				} else {
					s->still_next++;
				}
				s->stills_given++;
			}
		} else {
			out->skipped++;                 /* (the one before is still being made: this frame's number goes to the next) */
		}
		return;
	}
	struct rwcam_picture pic = picture_of(pb, pf, width, height);
	int by = -1;

	bool twinned = !made_up && index == 1 && twin_made(s, &pic, f);

	if (twinned) {
		by = RWCAM_BACKEND_CPU;
	} else if (!made_up && LEAD(index) && !test && (by = small_made(s, index, out, isp, f, raw, &pic)) >= 0) {
		/* (made smaller and then the size asked) */
	} else if (!made_up) {
		if ((by = rwcam_render(&s->render[index], isp, f, raw, &pic, LEAD(index) ? PREVIEW_PIECES : 8)) < 0) {
			if (pic.mapped) {
				CVPixelBufferUnlockBaseAddress(pb, 0);
			}
			CVPixelBufferRelease(pb);
			out->why_not = "neither the GPU nor the processor could make the picture (the buffer's memory could not be had)";
			return;
		}
	} else if (!test || pf == PF_BGRA) {
		CVPixelBufferRelease(pb);
		return;
	}
	if (!pic.mapped) {
		/* (the GPU wrote the planes, or the pacer's pattern is about to: locked for what reads and draws below, and for the surface's seed) */
		pic.mapped = map_pixel_buffer(&pic);
	}
	luma = pic.mapped ? mean_luma(pb, pf, width, height) : 0;
	/*
	 * A picture by the GPU that is black while the sensor's own levels (the kernel's header: mean green above black, of 1023) say the frame is
	 * not: the GPU did not write it, or wrote nothing. The GPU is off for this output from here on and this picture is made again by the
	 * processor. (The comparisons of rwcam_render.c find a wrong picture; this finds a black one at once, on every picture.)
	 */
	if (by == RWCAM_BACKEND_GPU && pic.mapped && luma < BLACK_LUMA && f->stat_samples != 0 && f->stat_green >= NOT_BLACK_GREEN) {
		char why[160];

		snprintf(why, sizeof(why), "picture %llu of %s is black, mean luma %u of 255, of a frame whose mean green is %u of 1023",
		    (unsigned long long)out->delivered + 1, cams[s->cam].name, luma, (unsigned)f->stat_green);
		rwcam_render_distrust(&s->render[index], why);
		by = rwcam_render(&s->render[index], isp, f, raw, &pic, LEAD(index) ? PREVIEW_PIECES : 8);
		luma = mean_luma(pb, pf, width, height);
		rwcam_say_keep("%s: that picture made again by the processor: mean luma %u of 255", out->name, luma);
	}
	if (!made_up) {
		out->luma_moved += out->luma_n == 0 ? 0 : luma > out->luma_before ? luma - out->luma_before : out->luma_before - luma;
		out->luma_before = luma;
		out->luma_sum += luma;
		out->luma_n++;
		out->luma_min = out->luma_n == 1 || luma < out->luma_min ? luma : out->luma_min;
		out->luma_max = out->luma_n == 1 || luma > out->luma_max ? luma : out->luma_max;
		out->black += luma < BLACK_LUMA;
		out->given++;
	}
	if (test && pf != PF_BGRA) {
		pattern(s, pb, width, height, out->delivered, made_up);
	} else if (LEAD(index) && pf != PF_BGRA && luma < MARK_LUMA && no_mark == 0) {
		mark(pb, width, height, out->delivered);
		marked = true;
	}
	if (pic.mapped) {
		CVPixelBufferUnlockBaseAddress(pb, 0);
	}
	t1 = rwcam_now_ms();
	CVBufferSetAttachment(pb, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_601_4, kCVAttachmentMode_ShouldPropagate);
	CVBufferSetAttachment(pb, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
	CVBufferSetAttachment(pb, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_709_2,
	    kCVAttachmentMode_ShouldPropagate);

	/* the picture's time: the kernel's stamp of the frame (when its last line was in memory), never one that does not go forward */
	uint64_t when = test || made_up ? mach_absolute_time() : f->time;

	/*
	 * The kernel stamps a frame when its 3 ms poll sees it done, so frames 33.3 ms apart are stamped 22 to 42 ms apart (run q10hc), and the image
	 * queue shows pictures by these times. The sensor's frames are exactly frame_lines x line_ns apart: the time given is the last one plus that
	 * for each frame since, pulled an eighth of the way to the stamp (so it follows the kernel's clock), and the stamp itself after a gap, a change
	 * of frame length it does not explain, or more than a frame off.
	 */
	if (LEAD(index) && !test && !made_up && f->line_ns != 0 && f->frame_lines != 0 && out->last_time != 0 && f->sequence > out->last_seq &&
	    f->sequence - out->last_seq <= 8) {
		uint64_t period = rwcam_ns_units((uint64_t)f->frame_lines * f->line_ns);
		uint64_t even = out->last_time + (f->sequence - out->last_seq) * period;
		int64_t off = (int64_t)(when - even);

		if (period != 0 && off > -(int64_t)period && off < (int64_t)period) {
			when = (uint64_t)((int64_t)even + off / 8);
			out->evened++;
		}
	}
	if (LEAD(index) && !made_up) {
		out->last_seq = f->sequence;
	}
	if (index == 1 && !made_up && s->first_seq == f->sequence && s->first_time > out->last_time) {
		when = s->first_time;           /* (the film's picture of this frame and the viewfinder's: one time) */
	}
	when = when <= out->last_time ? out->last_time + 1 : when;
	out->last_time = when;
	if (LEAD(index) && !made_up) {
		s->first_seq = f->sequence;
		s->first_time = when;
		if (no_twin == 0 && pf != PF_BGRA && s->twin == NULL) {
			s->twin = (CVPixelBufferRef)CFRetain(pb);
			s->twin_width = width;
			s->twin_height = height;
			s->twin_format = pf;
			s->twin_seq = f->sequence;
		}
	}

	CMTime pts = CMClockMakeHostTimeFromSystemUnits(when);

	if (!made_up && s->by_group) {
		/* (the zoom this picture was made with, for stream_add_attachments: the graph asks for another one while it handles this one) */
		CFStringRef k = cfs(ZOOM_OF_PICTURE);
		CFNumberRef z = number_f((double)(zoom_now / (s->cam == RWCAM_BACK_WIDE ? (float)GROUP_WIDE_BASE_ZOOM : 1)));

		CVBufferSetAttachment(pb, k, z, 0 /* kCVAttachmentMode_ShouldNotPropagate */);
		CFRelease(z);
		CFRelease(k);
	}
	if (still_tagged) {             /* (StillImage made on this thread: /var/db/nord2-camera-still-sync) */
		if (s->still_pre) {
			s->still_pre = false;
		} else {
			s->still_next++;
		}
		s->stills_given++;
	}
	rw_call_pixel_handler(out->handler, pb, pts);
	t2 = rwcam_now_ms();
	CVPixelBufferRelease(pb);
	if (out->delivered++ == 0) {
		rwcam_say("%s: the first picture given (%dx%d)", out->name, width, height);
	}
	uint64_t kind = made_up ? ++out->made_up : ++out->real;

	/* the summary of the pictures given (not the pacer's) */
	if (!made_up) {
		uint64_t gap = out->w.last_ms ? t2 - out->w.last_ms : 0;

		out->w.since = out->w.since ? out->w.since : t0;
		out->w.n++;
		out->w.made += t1 - t0;
		out->w.made_max = t1 - t0 > out->w.made_max ? t1 - t0 : out->w.made_max;
		out->w.handler += t2 - t1;
		out->w.handler_max = t2 - t1 > out->w.handler_max ? t2 - t1 : out->w.handler_max;
		out->w.gap_max = gap > out->w.gap_max ? gap : out->w.gap_max;
		if (out->w.last_ms) {
			out->w.gaps[gap <= 40 ? 0 : gap <= 70 ? 1 : gap <= 100 ? 2 : 3]++;
		}
		out->w.last_ms = t2;
		if (t2 - out->w.since >= SAY_MS) {
			double secs = (double)(t2 - out->w.since) / 1000.0;

			rwcam_say_keep("%s of %s: %.1f s: %llu pictures given (%.1f a second), %dx%d %c%c%c%c, made in %.1f ms (longest %llu), the graph's handler "
			    "%.1f ms (longest %llu); between them up to 40/70/100/more ms: %u/%u/%u/%u (longest %llu); mean luma %llu of 255 (%u to %u, %.1f from one picture "
			    "to the next; %llu black pictures since the start); the frame's green %u of 1023, %.1f ms x %.2f; StillImage off-thread: %llu made, %.0f ms each (longest %llu), "
			    "%llu not made (busy); %llu even times", out->name, cams[s->cam].name, secs, (unsigned long long)out->w.n, (double)out->w.n / secs,
			    width, height, (int)(pf >> 24), (int)(pf >> 16 & 255), (int)(pf >> 8 & 255), (int)(pf & 255),
			    (double)out->w.made / (double)out->w.n, (unsigned long long)out->w.made_max, (double)out->w.handler / (double)out->w.n,
			    (unsigned long long)out->w.handler_max, out->w.gaps[0], out->w.gaps[1], out->w.gaps[2], out->w.gaps[3],
			    (unsigned long long)out->w.gap_max, (unsigned long long)(out->luma_n ? out->luma_sum / out->luma_n : 0), out->luma_min,
			    out->luma_max, out->luma_n > 1 ? (double)out->luma_moved / (double)(out->luma_n - 1) : 0.0, (unsigned long long)out->black,
			    (unsigned)(f->stat_samples ? f->stat_green : 0), (double)f->exposure_lines * f->line_ns / 1e6,
			    f->gain_unity ? (double)f->gain / f->gain_unity : 0.0,
			    (unsigned long long)still.made, still.made ? (double)still.ms / (double)still.made : 0.0,
			    (unsigned long long)still.ms_max, (unsigned long long)still.dropped, (unsigned long long)out->evened);
			out->luma_sum = out->luma_n = out->luma_moved = 0;
			{
				char how[400];

				rwcam_render_summary(&s->render[index], how, sizeof(how));
				rwcam_say_keep("%s of %s: these pictures were made by: %s", out->name, cams[s->cam].name, how);
			}
			memset(&out->w, 0, sizeof(out->w));
			out->w.last_ms = t2;
		}
	}
	if (kind <= (made_up ? 4u : 3u) || (test && out->delivered % (SAY_PICTURE_EVERY * 4) == 0)) {
		rwcam_say_keep("%s of %s: picture %llu (%s %llu), %dx%d by %s: made in %llu ms, the graph's handler took %llu ms, %llu ms after the one before; "
		    "mean luma %u of 255%s; time %.4f s; %llu frames without one", out->name, cams[s->cam].name, (unsigned long long)out->delivered, made_up ? "TEST PATTERN, no frame, pacer's" : test ?
		    "TEST PATTERN over frame" : "frame", (unsigned long long)(made_up ? out->made_up : f->sequence), width, height,
		    twinned ? "the processor, from the PrimaryScaler's" : by == RWCAM_BACKEND_GPU ? "the GPU" : by == RWCAM_BACKEND_CPU ? "the processor" : "the pacer",
		    (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1), (unsigned long long)(out->last_ms ? t2 - out->last_ms : 0), luma,
		    marked ? " -- BLACK: bars and the moving square drawn into it" : "", pts.timescale ? (double)pts.value / pts.timescale : 0.0,
		    (unsigned long long)out->skipped);
	}
	out->last_ms = t2;
}

/* the test pattern's pacer: a PrimaryScaler picture every 1/TEST_FPS s for the stream that streams, when the sensor has given none */
static void *
pacer(void *unused)
{
	unsigned idle = 0;

	(void)unused;
	pthread_setname_np("redwood-campace");
	for (;;) {
		bool any = false;

		usleep(1000000 / TEST_FPS);
		pthread_mutex_lock(&pace_lock);
		for (int c = 0; c < RWCAM_CAMERAS; c++) {
			struct stream *s = active[c];

			if (s == NULL) {
				continue;
			}
			any = true;
			if (test_pattern != 1) {
				continue;
			}
			pthread_mutex_lock(&s->lock);
			struct output *out = &s->out[0];

			if (out->handler != NULL && out->config != NULL && (out->enabled || s->delivering) &&
			    rwcam_now_ms() - out->last_ms + 5 >= 1000 / TEST_FPS) {
				deliver(s, s, 0, out, NULL, NULL, NULL);
			}
			pthread_mutex_unlock(&s->lock);
		}
		idle = any ? 0 : idle + 1;
		if (idle > 10 * TEST_FPS) {
			pacer_running = false;          /* (no stream for 10 s: the next start makes another) */
			pthread_mutex_unlock(&pace_lock);
			break;
		}
		pthread_mutex_unlock(&pace_lock);
	}
	return NULL;
}

/* a stream is taken out of active[] (or put in): not while the pacer is inside one */
static void
pace_hold(bool hold)
{
	if (pacer_running && pthread_equal(pthread_self(), pacer_thread)) {
		return;                         /* (the graph stopping the stream from inside the pacer's own call of its handler) */
	}
	if (hold) {
		pthread_mutex_lock(&pace_lock);
	} else {
		pthread_mutex_unlock(&pace_lock);
	}
}

void
rwcam_device_point(int what, double x, double y, double w, double h, bool continuous)
{
	static unsigned said;
	int cam = rwcam_core_owner();
	struct stream *s;
	bool whole = !(x >= 0 && x <= 1 && y >= 0 && y <= 1 && w > 0 && w <= 1 && h > 0 && h <= 1);       /* (CGRectNull: no place in particular) */
	float z, cx, cy, ww, wh;
	uint32_t flip;

	if (cam < 0 || cam >= RWCAM_CAMERAS) {
		return;
	}
	pace_hold(true);
	s = active[cam];
	z = s != NULL ? view_zoom(s) : 1;
	flip = s != NULL ? s->flip : 0;
	pace_hold(false);
	/* from the picture (the middle 1/zoom of the sensor, turned as the stream turns it) to the sensor's frame */
	cx = whole ? 0.5f : 0.5f + ((float)(x + w / 2) - 0.5f) / z;
	cy = whole ? 0.5f : 0.5f + ((float)(y + h / 2) - 0.5f) / z;
	ww = (whole ? 0.25f : (float)w) / z;
	wh = (whole ? 0.25f : (float)h) / z;
	cx = flip & ISP_FLIP_H ? 1 - cx : cx;
	cy = flip & ISP_FLIP_V ? 1 - cy : cy;
	if (what == RWCAM_POINT_FOCUS) {
		rwcam_core_focus_hold(cam, !continuous);
		rwcam_core_focus_window(cam, cx, cy, ww, wh, true);
	} else if (what == RWCAM_POINT_EXPOSURE) {
		meter_tapped[cam] = !whole;
		rwcam_core_meter_window(cam, cx, cy, whole ? 0 : ww, whole ? 0 : wh);
	} else {
		rwcam_core_exposure_lock(cam, true);
	}
	if (said < 40) {
		said++;
		rwcam_say("stream: %s (%s): picture %.3f,%.3f %.3fx%.3f%s -> sensor %.3f,%.3f %.3fx%.3f (zoom %.2f, flip %u)",
		    what == RWCAM_POINT_FOCUS ? (continuous ? "focus here, then follow" : "focus here and stay") : what == RWCAM_POINT_EXPOSURE ?
		    "exposure by this" : "exposure locked", cams[cam].name, x, y, w, h, whole ? " (none)" : "", (double)cx, (double)cy, (double)ww, (double)wh,
		    (double)z, flip);
	}
}

/*
 * A stream that streams, every SAY_MS, whether it gives pictures or not (run q10hl: the front camera's session running, the viewfinder black,
 * and nothing in the log to tell a black picture from no picture): the frames it got, what the frame is (levels, exposure), and for each output
 * what the graph asked for, how many pictures it was given and why not when it was given none. Called with the stream's lock held.
 */
static void
stream_status(struct stream *s, const struct redwood_camera_frame *f)
{
	uint64_t now = rwcam_now_ms();
	char text[OUTPUTS][150];

	if (s->status_ms == 0) {
		s->status_ms = now;
		s->status_frames = s->frames;
		return;
	}
	if (now - s->status_ms < SAY_MS) {
		return;
	}
	for (int i = 0; i < OUTPUTS; i++) {
		struct output *out = &s->out[i];
		uint32_t pf = out->asked_format;

		if (out->handler == NULL && out->config == NULL) {
			snprintf(text[i], sizeof(text[i]), "not asked for");
		} else {
			snprintf(text[i], sizeof(text[i]), "%dx%d %c%c%c%c, %llu pictures (%llu since the start), by %s%s%.70s", out->asked_width, out->asked_height,
			    pf ? (int)(pf >> 24) : '-', pf ? (int)(pf >> 16 & 255) : '-', pf ? (int)(pf >> 8 & 255) : '-', pf ? (int)(pf & 255) : '-',
			    (unsigned long long)out->given, (unsigned long long)out->delivered, s->render[i].made[RWCAM_BACKEND_GPU] != 0 &&
			    !s->render[i].gpu_off ? "the GPU" : "the processor", out->given == 0 && out->why_not != NULL ? "; NONE: " : "",
			    out->given == 0 && out->why_not != NULL ? out->why_not : "");
		}
		out->given = out->not_given = 0;
	}
	rwcam_say_keep("stream %s: %.1f s: %llu frames (frame %llu: %ux%u, green %u of 1023%s, %u of %u lines x %.2f); format %d, zoom %.2f (view %.2f; %llu changes in all), flip %u, %s%s; "
	    "PrimaryScaler: %s; SecondaryScaler: %s; StillImage: %s", cams[s->cam].name, (double)(now - s->status_ms) / 1000.0,
	    (unsigned long long)(s->frames - s->status_frames), (unsigned long long)f->sequence, f->width, f->height, (unsigned)f->stat_green,
	    f->stat_samples ? "" : " (no levels in the header)", (unsigned)f->exposure_lines, (unsigned)f->frame_lines,
	    f->gain_unity ? (double)f->gain / (double)f->gain_unity : 0.0, s->format_index, (double)s->zoom,
	    (double)view_zoom(s), (unsigned long long)s->zooms, (unsigned)s->flip, s->delivering ? "started by the graph" : "NOT started by the graph (no pictures given)",
	    s->by_group ? ", one of the group's two" : "", text[s->out[3].given ? 3 : 0], text[1], text[2]);
	rwcam_say_keep("stream %s: buffers the graph holds: PrimaryScaler %ld (%ld MiB), SecondaryScaler %ld, StillImage %ld (%ld MiB), IntermediateTap %ld",
	    cams[s->cam].name, s->out[0].out_there, s->out[0].out_there * s->out[0].asked_width * s->out[0].asked_height * 3 / 2 >> 20, s->out[1].out_there,
	    s->out[2].out_there, s->out[2].out_there * s->out[2].asked_width * s->out[2].asked_height * 3 / 2 >> 20, s->out[3].out_there);
	s->status_ms = now;
	s->status_frames = s->frames;
}

/* the reader thread's good frame of camera `cam` */
static void
sink_frame(void *context, int cam, const struct redwood_camera_frame *f, const uint8_t *raw, const struct isp *isp)
{
	struct stream *of = active[cam], *s = of;

	(void)context;
	if (of == NULL && cam != RWCAM_FRONT) {
		/* (a sensor of the pair whose own stream is not started: the group's, if the other one's stream is) */
		struct stream *other = active[cam == RWCAM_BACK_WIDE ? RWCAM_BACK_ULTRAWIDE : RWCAM_BACK_WIDE];

		if (other != NULL && other->by_group && other->group != NULL) {
			of = stream_of(other->group->stream[cam == RWCAM_BACK_ULTRAWIDE]);
		}
	}
	if (of == NULL) {
		return;
	}
	s = of;
	if (of->group != NULL && (of->by_group || active[cam] == NULL)) {
		/*
		 * The dual-wide device: the sensor shown is chosen by the zoom (rwcam_pair.h) and its pictures are the MASTER stream's, whichever of
		 * the two that is; the view is this sensor's (zoom_view_at).
		 */
		int to = rwcam_pair_route(&of->group->pair, cam);

		if (to < 0) {
			return;                 /* (the pair's other sensor: nobody's picture now) */
		}
		s = stream_of(of->group->stream[to == RWCAM_BACK_ULTRAWIDE]);
		if (!s->started) {
			return;
		}
		{
			struct group *g = of->group;
			uint64_t now = rwcam_now_ms();

			if (to == rwcam_pair_master(&g->pair)) {
				if (g->last_ms != 0 && g->last_sensor != cam && g->changes++ < 60) {
					float ratio, dx, dy;
					unsigned found;

					rwcam_core_alignment(&ratio, &dx, &dy, &found);
					rwcam_say_keep("group: the sensor shown changed, %s -> %s, at zoom %.3f: %llu ms from the last picture of the one to this first "
					    "of the other (a frame is 33); this frame %.1f ms x %.2f, green %u of 1023; the lenses: 1/%.3f, %.4f, %.4f (%s, of %u)",
					    cams[g->last_sensor].name, cams[cam].name, (double)g->zoom, (unsigned long long)(now - g->last_ms),
					    (double)f->exposure_lines * f->line_ns / 1e6, f->gain_unity ? (double)f->gain / f->gain_unity : 0.0,
					    (unsigned)f->stat_green, (double)ratio, (double)dx, (double)dy, found ? "found" : "nominal or by hand", found);
				}
				g->last_sensor = cam;
				g->last_ms = now;
			}
		}
	}
	s->frames++;
	s->has_green = f->stat_samples != 0;
	s->green = f->stat_green;
	pthread_mutex_lock(&s->lock);
	for (int i = 0; i < OUTPUTS; i++) {
		struct output *out = &s->out[i];

		if (out->handler != NULL && out->config != NULL && (out->enabled || (i == 0 && s->delivering))) {
			if (i == 2 && s->still_next != 0 && (rwcam_now_ms() > s->still_until_ms || s->still_next > s->still_most)) {
				s->still_next = 0;              /* (the photograph's frames have been given, or nobody took them) */
				s->still_pre = false;
			}
			if (i == 2 && still_all == 0 && s->still_next == 0 && out->delivered != 0 && rwcam_now_ms() - out->last_ms < STILL_MIN_MS) {
				out->skipped++;
				continue;
			}
			/*
			 * A film (run user5): the graph took the film's pictures slower than they were made (832 encoded of 1325 given in 66 s; its
			 * zoomer and compressor) and kept the rest waiting -- 21, 89, 161 ... 355 buffers of 3 MiB, the viewfinder (made from the same
			 * pictures) seconds behind, then no surface left in the whole system and the pictures stopped. No more than HELD_MOST of a
			 * film's pictures are with the graph at once: a frame that would be one more is not made.
			 */
			if (LEAD(i) && s->format_index == 1 && __atomic_load_n(&out->out_there, __ATOMIC_RELAXED) >= HELD_MOST) {
				out->skipped++;
				out->why_not = "the graph holds as many of a film's pictures as it may";
				out->not_given++;
				continue;
			}
			deliver(s, of, i, out, isp, f, raw);
			out->not_given += out->why_not != NULL;
		} else {
			out->why_not = out->handler == NULL ? "the graph gave no handler" : out->config == NULL ? "the graph gave no configuration" :
			    "not enabled by the graph";
			out->not_given++;
		}
	}
	if (s->twin != NULL) {
		CFRelease(s->twin);             /* (held for this frame's outputs only: it is the graph's pool's) */
		s->twin = NULL;
	}
	stream_status(s, f);
	pthread_mutex_unlock(&s->lock);
	if (of->group != NULL) {
		/* a photograph asked of the pair's OTHER stream (the pictures go to the master's): its StillImage, of this frame -- the sensor shown */
		struct stream *o = stream_of(of->group->stream[s->cam != RWCAM_BACK_ULTRAWIDE]);
		struct output *out = &o->out[2];

		if (o != s && o->still_next != 0) {
			pthread_mutex_lock(&o->lock);         /* (s's is not held any more: never two at once) */
			if (rwcam_now_ms() > o->still_until_ms || o->still_next > o->still_most) {
				o->still_next = 0;
				o->still_pre = false;
			} else if (out->handler != NULL && out->config != NULL && out->enabled) {
				deliver(o, of, 2, out, isp, f, raw);
			}
			pthread_mutex_unlock(&o->lock);
		}
	}
}

static void
sink_stopped(void *context, int cam, const char *why)
{
	(void)context;
	rwcam_say("camera %d (%s): the reader gave up: %s", cam, cams[cam].name, why);
}

/* ---- the stream's Start / Stop ------------------------------------------------------------------------------- */

static OSStatus
stream_begin(CMBaseObjectRef o)
{
	struct stream *s = stream_of(o);

	if (s->started) {
		return 0;
	}
	s->started = true;
	s->frames = 0;
	no_mark = access("/var/db/nord2-camera-mark", F_OK) != 0;       /* (the bars over a BLACK picture: only when asked for now) */
	still_all = access("/var/db/nord2-camera-still-all", F_OK) == 0;
	still_sync = access("/var/db/nord2-camera-still-sync", F_OK) == 0;
	no_twin = access("/var/db/nord2-camera-notwin", F_OK) == 0;
	small_on = access("/var/db/nord2-camera-fullsize", F_OK) != 0 && access("/var/db/nord2-camera-gpu-isp", F_OK) != 0;
	{
		/* a sensor mounted the other way round than the graph takes it to be (not known for the front and ultra-wide ones before they are seen) */
		static const char *const names[RWCAM_CAMERAS] = { [RWCAM_BACK_WIDE] = "wide", [RWCAM_BACK_ULTRAWIDE] = "ultra", [RWCAM_FRONT] = "front" };
		char path[96];

		/*
		 * The front camera's pictures are turned half round here (run q10hf: "upside down" in Camera.app). The phone's front sensor sits half
		 * a turn from where an iPhone's does (Android gives the Nord 2's back camera the orientation 90 and its front one 270, an iPhone's
		 * graph turns both the same way and mirrors the front one's preview itself), so it is a turn, both flips, not a mirror. The markers
		 * exchange a flip each (for a kernel that turns the sensor itself with its register 0x0101: both markers).
		 */
		s->flip = s->cam == RWCAM_FRONT ? ISP_FLIP_H | ISP_FLIP_V : 0;
		snprintf(path, sizeof(path), "/var/db/nord2-camera-flip-%s-h", names[s->cam]);
		s->flip ^= access(path, F_OK) == 0 ? ISP_FLIP_H : 0;
		snprintf(path, sizeof(path), "/var/db/nord2-camera-flip-%s-v", names[s->cam]);
		s->flip ^= access(path, F_OK) == 0 ? ISP_FLIP_V : 0;
		s->zoom = s->zoom >= 1 ? s->zoom : 1;
		s->uw_ratio = s->cam == RWCAM_BACK_ULTRAWIDE ? uw_ratio_now() : 1;
		if (s->cam != RWCAM_FRONT) {
			align_markers();
		}
		rwcam_core_focus_window(s->cam, 0.5f, 0.5f, 0.25f / view_zoom(s), 0.25f / view_zoom(s), false);
	}
	test_pattern = access("/var/db/nord2-camera-testpattern", F_OK) == 0;
	pthread_mutex_lock(&s->lock);
	for (int i = 0; i < OUTPUTS; i++) {
		s->out[i].delivered = 0;        /* (each start says its first pictures) */
		s->out[i].skipped = 0;
		s->out[i].last_ms = 0;
		s->out[i].real = s->out[i].made_up = 0;
		s->out[i].last_time = s->out[i].last_seq = s->out[i].evened = 0;
		s->out[i].given = s->out[i].not_given = s->out[i].luma_sum = s->out[i].luma_n = s->out[i].black = 0;
		s->out[i].why_not = NULL;
		memset(&s->out[i].w, 0, sizeof(s->out[i].w));
	}
	s->status_ms = 0;
	pthread_mutex_unlock(&s->lock);
	render_once();
	/*
	 * This stream takes the camera over: a stream that had it (the other lens, the graph of Camera.app we are leaving) is not fed any more.
	 * But one of the two streams of a synchronized streams group whose ActiveStreams are both (the dual-wide device) leaves the group's other
	 * stream where it is: the two run together, and the group says which one's frames are pictures.
	 */
	struct group *g = s->group;
	bool grouped = g != NULL && s->cam != RWCAM_FRONT && rwcam_pair_wanted(&g->pair);

	pace_hold(true);
	if (active[s->cam] != s) {
		active[s->cam] = s;
	}
	for (int c = 0; c < RWCAM_CAMERAS; c++) {
		if (c != s->cam && !(grouped && active[c] != NULL && active[c]->group == g && active[c]->by_group)) {
			active[c] = NULL;
		}
	}
	s->by_group = grouped;
	if (grouped != s->zoom_of_group) {
		/* (the zoom kept from the other use of this stream means something else: all of the sensor until the graph says) */
		s->zoom_of_group = grouped;
		s->zoom = grouped && s->cam == RWCAM_BACK_WIDE ? (float)GROUP_WIDE_BASE_ZOOM : 1;
	}
	if (test_pattern == 1 && !pacer_running) {
		pacer_running = true;
		if (pthread_create(&pacer_thread, NULL, pacer, NULL) == 0) {
			pthread_detach(pacer_thread);
		} else {
			pacer_running = false;
		}
	}
	pace_hold(false);
	rwcam_say("stream: start (%s, format %d%s%s%s)", cams[s->cam].name, s->format_index, no_mark ? ", black pictures not marked" : "",
	    still_all ? ", StillImage from every frame" : "", test_pattern ? ", TEST PATTERN on PrimaryScaler at 15 a second (/var/db/nord2-camera-testpattern)" : "");
	if (s->cam == RWCAM_BACK_ULTRAWIDE) {
		rwcam_say("stream: the ultra-wide camera's zoom: lens ratio %.3f (wide / ultra-wide), so zoom %.1f is the middle 1/%.3f of its sensor "
		    "(the wide camera's view); zoom now %.3f = 1/%.3f", (double)s->uw_ratio, (double)RWCAM_UW_SWITCH_ZOOM,
		    (double)rwcam_view_zoom(s->cam, RWCAM_UW_SWITCH_ZOOM, s->uw_ratio), (double)s->zoom, (double)view_zoom(s));
	}
	if (!(grouped ? rwcam_pair_set_started(&g->pair, s->cam, true) : rwcam_core_start(s->cam))) {
		s->started = false;
		s->by_group = false;
		rwcam_say("stream: %s: Start ANSWERED %d: the reader could not be started (%s)", cams[s->cam].name, (int)ERR_UNSUPPORTED,
		    grouped ? "as one of the group's two" : "alone");
		return ERR_UNSUPPORTED;
	}
	return 0;
}

static OSStatus
stream_start(CMBaseObjectRef o)
{
	stream_of(o)->delivering = true;
	return stream_begin(o);
}

static OSStatus
stream_stop(CMBaseObjectRef o)
{
	struct stream *s = stream_of(o);

	s->delivering = false;
	if (!s->started) {
		return 0;
	}
	s->started = false;
	if (active[s->cam] == s) {
		pace_hold(true);
		active[s->cam] = NULL;
		pace_hold(false);
		for (int i = 0; i < 300 && __atomic_load_n(&still.busy, __ATOMIC_ACQUIRE); i++) {
			usleep(5000);                   /* (a StillImage being made: given to the graph before its stream is said to have stopped) */
		}
		rwcam_say("stream: stop (%s; PrimaryScaler pictures: %llu of frames, %llu of the pacer; %llu sample buffers given metadata)",
		    cams[s->cam].name, (unsigned long long)s->out[0].real, (unsigned long long)s->out[0].made_up, (unsigned long long)s->attached);
		if (!s->by_group) {
			rwcam_core_stop(s->cam);        /* (does nothing if another camera has taken the sensor over) */
		}
	}
	if (s->by_group) {
		struct group *g = s->group;

		s->by_group = false;
		if (g != NULL) {
			(void)rwcam_pair_set_started(&g->pair, s->cam, false);  /* (the last of the two: the sensors go down) */
		}
	}
	pthread_mutex_lock(&s->lock);
	for (int i = 0; i < OUTPUTS; i++) {
		rwcam_render_forget(&s->render[i]);     /* (the GPU's kept surfaces of this stream's pools) */
	}
	pthread_mutex_unlock(&s->lock);
	return 0;
}

/*
 * The metadata of a frame, for a stream that runs as one of a synchronized streams group's two. CMCapture 16.7 (disassembly):
 *   - -[BWFigVideoCaptureDevice captureStream:willEmitVideoSampleBuffer:drivesCameraControls:] (0x1a2190454) takes a sample buffer whose
 *     metadata says SynchronizedStreamsIsMaster = false as none of its business; with the key absent (as all of this bundle's frames were
 *     until now: they have no metadata dictionary at all) or true it updates its state from it;
 *   - -[BWFrameStatisticsByPortType updateWithFrameMetadata:updateFocusDistance:] (0x1a1f412d0) takes the frame's own statistics from the
 *     dictionary (found by its PortType) AND those of the other streams from SynchronizedStreamsSlaveMetadataByPortType, a dictionary of port
 *     type -> a metadata dictionary of the same keys: the slave's statistics travel in the MASTER's frames, the slave gives no sample buffer;
 *   - BWBravoStreamSelector (-recommendMasterStream... 0x1a1ff68a0) changes down to the ultra-wide stream as soon as the zoom is under the wide
 *     one's base zoom factor, whatever the statistics; UP to the wide stream only when -_exposureAndFocusStableForSwitchOverTo: (0x1a1ff7d30)
 *     finds AEStable in the statistics of the stream it changes to (and the focus not moving: AFStatus 4, or no focus step).
 * So: the master's frames carry both cameras' exposure state (rwcam_core_status: the hidden camera's comes from its frames' headers), and that
 * is all the slave ever gives. A stream outside a group gets no metadata, as before.
 */
static CFMutableDictionaryRef
metadata_of(int cam, int sensor)
{
	/* cam: the stream's (its port type); sensor: the camera whose frames these are (the same, but for the dual-wide device: rwcam_pair.h) */
	struct rwcam_status st;
	CFMutableDictionaryRef m;

	rwcam_core_status(sensor, &st);
	if (st.frames == 0 || port_type[cam] == NULL) {
		return NULL;
	}
	m = new_dict();
	put(m, "PortType", CFRetain(port_type[cam]));
	/*
	 * The still-image noise reduction finds its tuning by this and by nothing else: -[GNRProcessor processSampleBuffer:] (0x1f205d340) takes
	 * its table's entry for the metadata's SensorID -- a whole number: the table is keyed by strtoul of the "0x%04x" string of ModuleInfo
	 * (_get_tuning_for_sensor_id, 0x1f20682b8) -- and gives up (-1, no photograph) when there is none. The stream's own number (cam, as
	 * the ModuleInfo that CMCapture chose the tuning with), also for the other back camera's frames given as this stream's.
	 */
	put(m, "SensorID", number_i(cams[cam].sensor_id));
	/*
	 * Numbers, not booleans: CMCapture's C code takes these with CFNumberGetValue (AEStable in _processBuffer, 0x1a1f0f670) and
	 * FigCFDictionaryGetInt32IfPresent (AFStatus, AEStable, AELimitsReached, AWBStable in FigImageControl_Focus, 0x1a1f31708..4c); its
	 * Objective-C code takes them with intValue / boolValue, which a number answers as well.
	 */
	put(m, "AEStable", number_i(st.ae_stable ? 1 : 0));
	put(m, "AELocked", number_i(0));
	put(m, "AELimitsReached", number_i(0));
	/* (only their ratio is looked at: target over average) */
	put(m, "AETarget", number_i((int64_t)(st.target * 1024)));
	put(m, "AEAverage", number_i(st.level * 1024 >= 1 ? (int64_t)(st.level * 1024) : 1));
	put(m, "ExposureTime", number_f((double)st.exposure_ns / 1e9));
	put(m, "AGC", number_i((int64_t)(st.gain * st.digital * 256)));
	put(m, "CurrentFrameRate", number_f(st.fps));
	put(m, "AFStatus", number_i(4));
	put(m, "AFStable", number_i(1));
	put(m, "AWBStable", number_i(1));
	return m;
}

static OSStatus
stream_add_attachments(CMBaseObjectRef o, CMSampleBufferRef sbuf, CFDictionaryRef options)
{
	struct stream *s = stream_of(o);
	struct group *g = s->group;
	CFMutableDictionaryRef m, other;
	CFStringRef key;
	CFTypeRef had;
	int master, slave, shown, hidden;

	(void)options;
	if ((!s->by_group || g == NULL) && sbuf != NULL && cm_set_attachment != NULL && cm_get_attachment != NULL && no_metadata != 1) {
		/*
		 * A stream by itself (the front camera) has no metadata on its pictures (see below) -- but a photograph's frame must have: the still
		 * image nodes return at once without a MetadataDictionary (BWGNRNode, 0x1a1f1837c) and take the port type and the frame's number
		 * from it. On the sample buffer and on its image buffer, as for the group's.
		 */
		CFTypeRef (*image_of)(CMSampleBufferRef) = (CFTypeRef (*)(CMSampleBufferRef))dlsym(RTLD_DEFAULT, "CMSampleBufferGetImageBuffer");
		CFTypeRef image = image_of != NULL ? image_of(sbuf) : NULL;
		CFStringRef tk = cfs(STILL_TAG);
		CFTypeRef tag = image != NULL ? cm_get_attachment(image, tk, NULL) : NULL;
		int64_t number = 0;

		CFRelease(tk);
		if (tag != NULL && CFGetTypeID(tag) == CFNumberGetTypeID() && CFNumberGetValue(tag, kCFNumberSInt64Type, &number) &&
		    (m = metadata_of(s->cam, s->cam)) != NULL) {
			key = cfs("MetadataDictionary");
			if (number > 0) {
				put(m, "BracketedCaptureSequenceNumber", number_i(number));
			} else if (number < 0) {
				put(m, "PreBracketingFrame", number_i(1));
			}
			put(m, "ZoomFactor", number_f((double)(s->zoom >= 1 ? s->zoom : 1)));
			cm_set_attachment(sbuf, key, m, kCVAttachmentMode_ShouldPropagate);
			cm_set_attachment(image, key, m, kCVAttachmentMode_ShouldPropagate);
			CFRelease(m);
			CFRelease(key);
		}
		return 0;
	}
	if (!s->by_group || g == NULL || sbuf == NULL || cm_set_attachment == NULL || no_metadata == 1) {
		return 0;
	}
	master = rwcam_pair_master(&g->pair);
	slave = master == RWCAM_BACK_WIDE ? RWCAM_BACK_ULTRAWIDE : RWCAM_BACK_WIDE;
	shown = rwcam_pair_shown(&g->pair);
	hidden = shown == RWCAM_BACK_WIDE ? RWCAM_BACK_ULTRAWIDE : RWCAM_BACK_WIDE;
	if ((m = metadata_of(s->cam, s->cam == master ? shown : hidden)) == NULL) {
		return 0;
	}
	key = cfs("MetadataDictionary");
	/* (what the graph may have put there already stays, under what is said here) */
	had = cm_get_attachment != NULL ? cm_get_attachment(sbuf, key, NULL) : NULL;
	if (had != NULL && CFGetTypeID(had) == CFDictionaryGetTypeID()) {
		CFIndex n = CFDictionaryGetCount(had);
		const void **k = calloc((unsigned long)n + 1, sizeof(void *)), **v = calloc((unsigned long)n + 1, sizeof(void *));

		if (k != NULL && v != NULL) {
			CFDictionaryGetKeysAndValues(had, k, v);
			for (CFIndex i = 0; i < n; i++) {
				if (CFDictionaryGetValue(m, k[i]) == NULL) {
					CFDictionarySetValue(m, k[i], v[i]);
				}
			}
		}
		free(k);
		free(v);
	}
	put(m, "SynchronizedStreamsIsMaster", number_i(s->cam == master ? 1 : 0));
	if (s->cam == master && (other = metadata_of(slave, hidden)) != NULL) {
		CFMutableDictionaryRef by_port = new_dict();

		CFDictionarySetValue(by_port, port_type[slave], other);
		CFRelease(other);
		put(m, "SynchronizedStreamsSlaveMetadataByPortType", by_port);
	}
	cm_set_attachment(sbuf, key, m, kCVAttachmentMode_ShouldPropagate);
	/*
	 * And on the sample buffer's IMAGE BUFFER, the same dictionary (run q10hp: mediaserverd's SIGSEGV a few frames after the pair started).
	 * A sample buffer with a MetadataDictionary is what _processBuffer (0x1a1f0e5e0) runs FigImageControl on; FigImageControl_Focus
	 * (0x1a1f30a04) takes the dictionary from the IMAGE BUFFER (0x1a1f30a70..94), makes an empty one when there is none (0x1a1f30ac8) -- and
	 * then asks the one that was not there for its PortType (0x1a1f30cfc: CFDictionaryGetValue of NULL). The camera of an iPhone has it on
	 * both. A stream outside the group has no metadata on either, and FigImageControl does not run for it (as in every run before).
	 */
	{
		static CFTypeRef (*image_of)(CMSampleBufferRef);
		CFTypeRef image;

		if (image_of == NULL) {
			image_of = (CFTypeRef (*)(CMSampleBufferRef))dlsym(RTLD_DEFAULT, "CMSampleBufferGetImageBuffer");
		}
		image = image_of != NULL ? image_of(sbuf) : NULL;
		/*
		 * ZoomFactor: the zoom the ISP gave THIS picture, by the stream's own measure. -[BWMultiStreamCameraSourceNode
		 * _ispAppliedZoomFactorFromSampleBufferMetadataDictionary:outputIndex:] (0x1a1ef5b8c) multiplies it by the stream's base zoom factor,
		 * BWZoomCommandHandler finds the request it was (-updateAppliedZoomFactorForDelayedISPAppliedZoomFactor:), and what the picture
		 * lacks of the zoom wanted for it is written into the metadata as FinalCropRect (-_updateZoomForOutputIndex:..., 0x1a215d7d0) for
		 * the 'Preview Scaler' to cut out and enlarge. Run q10ht: without this key every picture lacked ALL of the zoom, the scaler was
		 * asked to enlarge (which VideoToolbox cannot do here: -12914) and the viewfinder stood still at every zoom but 1x and 0.5x.
		 */
		{
			CFStringRef zk = cfs(ZOOM_OF_PICTURE);
			CFTypeRef z = image != NULL && cm_get_attachment != NULL ? cm_get_attachment(image, zk, NULL) : NULL;

			/*
			 * (camera-userland-20261112-zoom, EXPERIMENT, off unless /var/db/nord2-camera-zoom-label-set) the zoom last SET, not the one
			 * the picture was made with: then the picture never "lacks" zoom for BWZoomCommandHandler, FinalCropRect stays the whole
			 * picture while the fingers move, and the Preview Scaler / Movie File Zoomer keep one crop (VideoToolbox sets a transfer up
			 * anew for each new crop: runs user4/user5, 55 to 120 ms compute commits in mediaserverd only while the zoom moved). The
			 * price: the picture shows the zoom of up to a frame or two before its label (a lag of 33-66 ms of the zoom, no jump).
			 * INFERRED: whether the handler wants the newest request or a delayed one is not read from CMCapture; the phone says.
			 */
			static int label_set = -1;

			if (label_set < 0) {
				label_set = access("/var/db/nord2-camera-zoom-label-set", F_OK) == 0;
				rwcam_say("stream: pictures' ZoomFactor: %s", label_set ? "the zoom last set (/var/db/nord2-camera-zoom-label-set)" :
				    "the zoom each picture was made with");
			}
			if (label_set) {
				put(m, "ZoomFactor", number_f((double)(s->zoom / (s->cam == RWCAM_BACK_WIDE ? (float)GROUP_WIDE_BASE_ZOOM : 1))));
			} else if (z != NULL && CFGetTypeID(z) == CFNumberGetTypeID()) {
				put(m, "ZoomFactor", CFRetain(z));
			} else {
				put(m, "ZoomFactor", number_f((double)(s->zoom / (s->cam == RWCAM_BACK_WIDE ? (float)GROUP_WIDE_BASE_ZOOM : 1))));
			}
			CFRelease(zk);
		}
		{
			/* a photograph's frame (stream_still_now): its number in the bracket, or that it is the frame before it */
			CFStringRef tk = cfs(STILL_TAG);
			CFTypeRef tag = image != NULL && cm_get_attachment != NULL ? cm_get_attachment(image, tk, NULL) : NULL;
			int64_t number = 0;

			if (tag != NULL && CFGetTypeID(tag) == CFNumberGetTypeID() && CFNumberGetValue(tag, kCFNumberSInt64Type, &number)) {
				if (number > 0) {
					put(m, "BracketedCaptureSequenceNumber", number_i(number));
				} else if (number < 0) {
					put(m, "PreBracketingFrame", number_i(1));
				}
			}
			CFRelease(tk);
		}
		if (image != NULL) {
			cm_set_attachment(image, key, m, kCVAttachmentMode_ShouldPropagate);
		} else if (s->attached < 3) {
			rwcam_say("stream: %s: the sample buffer has no image buffer (or no CMSampleBufferGetImageBuffer here): the metadata is on the sample "
			    "buffer only", cams[s->cam].name);
		}
	}
	if (s->attached++ < 3) {
		char text[300];

		describe(m, text, sizeof(text));
		rwcam_say("stream: %s: metadata for a sample buffer (%s of the group): %s", cams[s->cam].name, s->cam == master ? "master" : "SLAVE", text);
	}
	CFRelease(m);
	CFRelease(key);
	return 0;
}

static OSStatus
stream_invalidate(CMBaseObjectRef o)
{
	return stream_stop(o);
}

static void
stream_finalize(CMBaseObjectRef o)
{
	struct stream *s = stream_of(o);

	(void)stream_stop(o);
	for (int i = 0; i < OUTPUTS; i++) {
		if (s->out[i].config) {
			CFRelease(s->out[i].config);
		}
		if (s->out[i].handler) {
			CFRelease(s->out[i].handler);
		}
		if (s->out[i].pool) {
			CVPixelBufferPoolRelease(s->out[i].pool);
		}
		free(s->out[i].small);
	}
	for (int i = 0; i < OUTPUTS; i++) {
		rwcam_render_end(&s->render[i]);
	}
	free(s->last_y);
	free(s->last_uv);
	stream_slave_configuration(s, NULL);
	if (s->formats) {
		CFRelease(s->formats);
	}
	if (s->supported) {
		CFRelease(s->supported);
	}
	if (s->name) {
		CFRelease(s->name);
	}
	if (s->unique_id) {
		CFRelease(s->unique_id);
	}
	if (s->torch >= 0) {
		close(s->torch);
	}
}

static CFStringRef
stream_description(CMBaseObjectRef o)
{
	char text[96];

	snprintf(text, sizeof(text), "<RedwoodCaptureStream: %s>", cams[stream_of(o)->cam].name);
	return cfs(text);
}

static const struct base_class stream_base = {
	1, sizeof(struct stream), NULL, stream_invalidate, stream_finalize, stream_description, stream_copy_property, stream_set_property,
	NULL, NULL, NULL
};
static const struct stream_class stream_derived = { 2, stream_start, stream_stop, stream_add_attachments };
static const struct vtable stream_vtable = { NULL, &stream_base, &stream_derived };

static CMBaseObjectRef
stream_create(CFAllocatorRef allocator, int cam)
{
	CMBaseObjectRef o = NULL;
	struct stream *s;
	const struct cam_desc *c = &cams[cam];

	if (CMDerivedObjectCreate(allocator, &stream_vtable, stream_class_id(), &o) != 0 || o == NULL) {
		rwcam_say("the stream of %s cannot be made", c->name);
		return NULL;
	}
	s = stream_of(o);
	memset(s, 0, sizeof(*s));
	pthread_mutex_init(&s->lock, NULL);
	s->cam = cam;
	s->torch = -1;
	s->max_integration_ms = 66;
	s->min_fps = 2;
	s->max_fps = 30;
	s->zoom = 1;
	s->uw_ratio = cam == RWCAM_BACK_ULTRAWIDE ? RWCAM_UW_RATIO : 1;
	for (int i = 0; i < OUTPUTS; i++) {
		static const char *const render_names[RWCAM_CAMERAS][OUTPUTS] = {
			[RWCAM_BACK_WIDE] = { "wide PrimaryScaler", "wide SecondaryScaler", "wide StillImage", "wide film" },
			[RWCAM_BACK_ULTRAWIDE] = { "ultra-wide PrimaryScaler", "ultra-wide SecondaryScaler", "ultra-wide StillImage", "ultra-wide film" },
			[RWCAM_FRONT] = { "front PrimaryScaler", "front SecondaryScaler", "front StillImage", "front film" },
		};

		s->out[i].name = output_names[i];
		s->render[i].name = render_names[cam][i];
		s->render[i].zero_copy = i == 0;        /* (q10jn) the viewfinder's/film's pictures: rendered where the reader read the frame, when on */
		s->render[i].check_max = -1;
	}
	s->name = cfs(c->name);
	s->unique_id = cfs(c->unique_id);
	{
		CFDictionaryRef f[3];

		for (int i = 0; i < c->nformats; i++) {
			f[i] = format(c, &c->formats[i]);
		}
		s->formats = CFArrayCreate(NULL, (const void **)f, c->nformats, &kCFTypeArrayCallBacks);
		for (int i = 0; i < c->nformats; i++) {
			CFRelease(f[i]);
		}
	}
	s->supported = supported(stream_props, sizeof(stream_props) / sizeof(stream_props[0]));
	if (c->has_af && access("/var/db/nord2-camera-focusprops", F_OK) == 0) {
		static const struct prop focus_props[] = {
			{ "FocusNow", "Collection", "WriteOnly" }, { "AutoFocusNow", "Collection", "WriteOnly" },
			{ "ContinuousAutoFocusNow", "Collection", "WriteOnly" }, { "ManualFocusNow", "Collection", "WriteOnly" },
			{ "LockAFNow", "Boolean", "WriteOnly" }, { "AFWindowParams", "Collection", "WriteOnly" },
			{ "AutoFocusHintNow", "Collection", "WriteOnly" },
		};
		CFDictionaryRef parts[2] = { s->supported, supported(focus_props, sizeof(focus_props) / sizeof(focus_props[0])) };
		CFMutableDictionaryRef all = new_dict();

		for (int part = 0; part < 2; part++) {
			CFIndex n = CFDictionaryGetCount(parts[part]);
			const void **k = calloc((unsigned long)n + 1, sizeof(void *)), **v = calloc((unsigned long)n + 1, sizeof(void *));

			CFDictionaryGetKeysAndValues(parts[part], k, v);
			for (CFIndex i = 0; i < n; i++) {
				CFDictionarySetValue(all, k[i], v[i]);
			}
			free(k);
			free(v);
			CFRelease(parts[part]);
		}
		s->supported = all;
		rwcam_say("stream: %s lists CMCapture's focus properties (/var/db/nord2-camera-focusprops)", c->name);
	}
	return o;
}

/* ---- the synchronized streams group --------------------------------------------------------------------------- */

static const struct prop group_props[] = {
	{ "SupportedStreams", "Collection", "ReadOnly" }, { "SynchronizationMaster", "Object", "ReadOnly" }, { "ActiveStreams", "Collection", "ReadWrite" },
	{ "MasterConfiguration", "Collection", "ReadWrite" }, { "MasterConfigurationSupportedKeys", "Collection", "ReadOnly" },
	{ "BaseZoomFactorsByPortType", "Collection", "ReadOnly" }, { "Extrinsics", "Collection", "ReadOnly" },
	{ "ZoomFactor", "Number", "ReadWrite" },        /* (the device's zoom, for both cameras: stream_zoom) */
};

static struct group *
group_of(CMBaseObjectRef o)
{
	return CMBaseObjectGetDerivedStorage(o);
}

static int
group_slot(const struct group *g, CFTypeRef stream)
{
	return stream == NULL ? -1 : stream == (CFTypeRef)g->stream[0] ? 0 : stream == (CFTypeRef)g->stream[1] ? 1 : -1;
}

/*
 * The group's streams that are started already come under the group when its ActiveStreams become both (CMCapture may start a stream before it
 * sets the group up), and go back to being single streams -- one of them, the reader has one node then -- when they stop being both.
 */
static void
group_follow(struct group *g)
{
	bool wanted = rwcam_pair_wanted(&g->pair);
	struct stream *alone = NULL;

	for (int i = 0; i < 2; i++) {
		struct stream *s = stream_of(g->stream[i]);

		if (wanted && s->started && !s->by_group) {
			pace_hold(true);
			active[s->cam] = s;
			active[RWCAM_FRONT] = NULL;
			pace_hold(false);
			s->by_group = true;
			(void)rwcam_pair_set_started(&g->pair, s->cam, true);
		} else if (!wanted && s->by_group) {
			s->by_group = false;
			(void)rwcam_pair_set_started(&g->pair, s->cam, false);
			alone = s->started && (alone == NULL || s->cam == rwcam_pair_master(&g->pair)) ? s : alone;
		}
	}
	if (alone != NULL) {
		pace_hold(true);
		for (int c = 0; c < RWCAM_CAMERAS; c++) {
			active[c] = c == alone->cam ? alone : NULL;
		}
		pace_hold(false);
		(void)rwcam_core_start(alone->cam);
	}
}

static OSStatus
group_copy_property(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef allocator, void *out)
{
	struct group *g = group_of(o);
	CFTypeRef value = NULL;
	char name[128];

	(void)allocator;
	key_name(key, name, sizeof(name));
	rwcam_say_once("group: read", name, NULL);
	if (key_is(key, "SupportedPropertiesDictionary")) {
		value = CFRetain(g->supported);
	} else if (key_is(key, "SupportedStreams")) {
		value = CFRetain(g->streams);
	} else if (key_is(key, "SynchronizationMaster")) {
		value = CFRetain(g->stream[0]);
	} else if (key_is(key, "ActiveStreams")) {
		CFTypeRef list[2];
		int n = 0;

		pthread_mutex_lock(&g->pair.lock);
		for (int i = 0; i < 2; i++) {
			if (g->pair.active[i]) {
				list[n++] = g->stream[i];
			}
		}
		pthread_mutex_unlock(&g->pair.lock);
		value = CFArrayCreate(NULL, (const void **)list, n, &kCFTypeArrayCallBacks);
	} else if (key_is(key, "MasterConfiguration")) {
		if (g->master_configuration != NULL) {
			value = CFRetain(g->master_configuration);
		} else {
			CFMutableDictionaryRef d = new_dict();
			CFStringRef k = cfs("Master");

			CFDictionarySetValue(d, k, g->stream[rwcam_pair_master(&g->pair) == RWCAM_BACK_ULTRAWIDE ? 1 : 0]);
			CFRelease(k);
			value = d;
		}
	} else if (key_is(key, "ZoomFactor")) {
		value = number_f(g->zoom >= 1 ? g->zoom : 1);
	} else if (key_is(key, "MasterConfigurationSupportedKeys")) {
		CFStringRef k[5] = { cfs("Master"), cfs("SlaveConfigurationByPortType"), cfs("AutoFocusControlMaster"), cfs("AutoExposureControlMaster"),
			cfs("AutoWhiteBalanceControlMaster") };

		value = CFArrayCreate(NULL, (const void **)k, 5, &kCFTypeArrayCallBacks);
		for (int i = 0; i < 5; i++) {
			CFRelease(k[i]);
		}
	} else if (key_is(key, "BaseZoomFactorsByPortType")) {
		/* the zoom of the device at which each stream shows all of its sensor: the ultra-wide one at 1, the wide one at Camera.app's "1x" */
		CFMutableDictionaryRef d = new_dict();
		CFNumberRef one = number_f(1), wide = number_f(GROUP_WIDE_BASE_ZOOM);

		CFDictionarySetValue(d, port_type[RWCAM_BACK_ULTRAWIDE], one);
		CFDictionarySetValue(d, port_type[RWCAM_BACK_WIDE], wide);
		CFRelease(one);
		CFRelease(wide);
		value = d;
	} else if (key_is(key, "Extrinsics")) {
		/*
		 * Where each camera is, seen from the other: 3x4 floats, rotation and translation. Not known for this phone: no rotation, no
		 * distance (the two pictures are taken to have the same middle; what CMCapture makes of it at the switch-over is no shift).
		 */
		static const float same[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
		CFMutableDictionaryRef d = new_dict();

		if (cf_data_create == NULL) {
			CFRelease(d);
			rwcam_say("group: Extrinsics ANSWERED %d (no CFDataCreate here)", (int)ERR_UNSUPPORTED);
			return ERR_UNSUPPORTED;
		}
		for (int i = 0; i < 2; i++) {
			CFMutableDictionaryRef to = new_dict();
			CFTypeRef data = cf_data_create(NULL, (const uint8_t *)same, (CFIndex)sizeof(same));

			CFDictionarySetValue(to, port_type[group_cam[1 - i]], data);
			CFRelease(data);
			CFDictionarySetValue(d, port_type[group_cam[i]], to);
			CFRelease(to);
		}
		value = d;
	} else {
		say_unknown("group: NO ANSWER (-12783) for", key, NULL);
		return ERR_NOT_AVAILABLE;
	}
	*(CFTypeRef *)out = value;
	return 0;
}

static OSStatus
group_set_property(CMBaseObjectRef o, CFStringRef key, CFTypeRef value)
{
	struct group *g = group_of(o);

	{
		char name[64], text[300];

		key_name(key, name, sizeof(name));
		describe(value, text, sizeof(text));
		rwcam_say("group: set %s = %s", name, text);
	}
	if (key_is(key, "ZoomFactor")) {
		float z = 1;

		if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID()) {
			CFNumberGetValue(value, kCFNumberFloatType, &z);
		}
		g->zoom = z >= 1 ? z : 1;
		for (int i = 0; i < 2; i++) {
			stream_zoom(stream_of(g->stream[i]), g->zoom, true);
		}
		rwcam_pair_set_zoom(&g->pair, g->zoom);
		return 0;
	}
	if (key_is(key, "ActiveStreams")) {
		bool on[2] = { false, false };

		if (value != NULL && cf_array_type != NULL && cf_array_get != NULL && CFGetTypeID(value) == cf_array_type()) {
			for (CFIndex i = 0; i < CFArrayGetCount(value); i++) {
				int k = group_slot(g, cf_array_get(value, i));

				if (k >= 0) {
					on[k] = true;
				}
			}
		}
		rwcam_pair_set_active(&g->pair, on[0], on[1]);
		group_follow(g);
		return 0;
	}
	if (key_is(key, "MasterConfiguration")) {
		CFDictionaryRef d = value != NULL && CFGetTypeID(value) == CFDictionaryGetTypeID() ? value : NULL;
		CFStringRef mk = cfs("Master"), sk = cfs("SlaveConfigurationByPortType");
		CFTypeRef master = d != NULL ? CFDictionaryGetValue(d, mk) : NULL, slaves = d != NULL ? CFDictionaryGetValue(d, sk) : NULL;
		int k = group_slot(g, master);

		CFRelease(mk);
		CFRelease(sk);
		if (d == NULL) {
			rwcam_say("group: set MasterConfiguration: not a dictionary, ANSWERED %d", (int)ERR_PARAM);
			return ERR_PARAM;
		}
		if (master != NULL && k >= 0) {
			/* (a dictionary with the control masters only -- -setCameraControlsMasterStream: -- has no Master: nothing changes) */
			if (g->master_configuration != NULL) {
				CFRelease(g->master_configuration);
			}
			g->master_configuration = CFRetain(value);
			if (slaves != NULL && CFGetTypeID(slaves) == CFDictionaryGetTypeID() && port_type[group_cam[1 - k]] != NULL) {
				CFDictionaryRef sc = CFDictionaryGetValue(slaves, port_type[group_cam[1 - k]]);

				if (sc != NULL && CFGetTypeID(sc) == CFDictionaryGetTypeID()) {
					stream_slave_configuration(stream_of(g->stream[1 - k]), sc);
					rwcam_pair_set_slave(&g->pair, get_i(sc, "StreamingOutputsEnabled", 0) != 0, (unsigned)get_i(sc, "MasterToSlaveFrameRateRatio", 1));
				}
			}
			rwcam_pair_set_master(&g->pair, group_cam[k]);
		}
		return 0;
	}
	say_unknown("group: set (taken, not used)", key, value);
	return 0;
}

static OSStatus
group_invalidate(CMBaseObjectRef o)
{
	(void)o;
	return 0;
}

static void
group_finalize(CMBaseObjectRef o)
{
	struct group *g = group_of(o);

	rwcam_pair_set_active(&g->pair, false, false);
	for (int i = 0; i < 2; i++) {
		if (g->stream[i] != NULL) {
			struct stream *s = stream_of(g->stream[i]);

			if (s->by_group) {
				s->by_group = false;
				(void)rwcam_pair_set_started(&g->pair, s->cam, false);
			}
			s->group = NULL;
			CFRelease(g->stream[i]);
		}
	}
	if (g->streams) {
		CFRelease(g->streams);
	}
	if (g->supported) {
		CFRelease(g->supported);
	}
	if (g->master_configuration) {
		CFRelease(g->master_configuration);
	}
}

static CFStringRef
group_description(CMBaseObjectRef o)
{
	(void)o;
	return cfs("<RedwoodCaptureSynchronizedStreamsGroup: the Nord 2's back wide and back ultra-wide cameras>");
}

struct group_class {
	uint64_t        version;                /* 1: no stop function (2 and over: one at +8, which CMCapture then calls instead of the streams' own) */
	void *          stop;
};
static const struct base_class group_base = {
	1, sizeof(struct group), NULL, group_invalidate, group_finalize, group_description, group_copy_property, group_set_property,
	NULL, NULL, NULL
};
static const struct group_class group_derived = { 1, NULL };
static const struct vtable group_vtable = { NULL, &group_base, &group_derived };

/* the two back streams' group, or NULL (and said why not) */
static CMBaseObjectRef
group_create(CFAllocatorRef allocator, CMBaseObjectRef wide, CMBaseObjectRef ultra)
{
	CMBaseObjectRef o = NULL;
	struct group *g;

	if (group_class_id == NULL || cf_array_get == NULL || cf_array_type == NULL || cf_data_create == NULL) {
		rwcam_say("group: CMCapture's FigCaptureSynchronizedStreamsGroupGetClassID (or CoreFoundation's array and data calls) not found: no dual-wide device");
		return NULL;
	}
	if (CMDerivedObjectCreate(allocator, &group_vtable, group_class_id(), &o) != 0 || o == NULL) {
		rwcam_say("group: cannot be made");
		return NULL;
	}
	g = group_of(o);
	memset(g, 0, sizeof(*g));
	rwcam_pair_init(&g->pair);
	g->stream[0] = (CMBaseObjectRef)CFRetain(wide);
	g->stream[1] = (CMBaseObjectRef)CFRetain(ultra);
	g->streams = CFArrayCreate(NULL, (const void **)g->stream, 2, &kCFTypeArrayCallBacks);
	g->supported = supported(group_props, sizeof(group_props) / sizeof(group_props[0]));
	stream_of(wide)->group = g;
	stream_of(ultra)->group = g;
	return o;
}

/* ---- the device ------------------------------------------------------------------------------------------- */

struct device {
	CFArrayRef      streams;
	CFDictionaryRef supported;
	CMBaseObjectRef group;                  /* the two back streams' synchronized streams group, when both sensors can run together */
};

static OSStatus
device_copy_property(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef allocator, void *out)
{
	struct device *d = CMBaseObjectGetDerivedStorage(o);
	CFTypeRef value = NULL;
	char asked_name[128];

	key_name(key, asked_name, sizeof(asked_name));
	rwcam_say_once("device: read", asked_name, NULL);
	(void)allocator;
	if (key_is(key, "StreamArray")) {
		value = CFRetain(d->streams);
	} else if (key_is(key, "Clock")) {
		value = CFRetain(CMClockGetHostTimeClock());
	} else if (key_is(key, "SupportedPropertiesDictionary")) {
		value = CFRetain(d->supported);
	} else if (key_is(key, "SupportedSynchronizedStreamsGroups")) {
		/*
		 * The group of the two back streams (CMCapture's dual-wide device is made of it), when the kernel has the second node; none
		 * otherwise: then the cameras are three independent devices, one sensor at a time, and a dual-wide entry of the model's
		 * AVCaptureSession.plist gives no source ("Failed to open streams for device").
		 */
		value = CFArrayCreate(NULL, (const void **)&d->group, d->group != NULL ? 1 : 0, &kCFTypeArrayCallBacks);
	} else {
		say_unknown("device: NO ANSWER (-12783) for", key, NULL);
		return ERR_NOT_AVAILABLE;
	}
	*(CFTypeRef *)out = value;
	return 0;
}

static OSStatus
device_set_property(CMBaseObjectRef o, CFStringRef key, CFTypeRef value)
{
	(void)o;
	say_unknown("device: set (not used)", key, value);
	return 0;
}

/*
 * Requesting a stream is the graph's reservation of it (the vendor does it at -copyStreams...): it does not start a sensor here -- the vendor reserves
 * the default stream when it prewarms and AVFoundation may reserve several while it lists devices, and with three sensors on one bus that would
 * switch cameras for nothing. HakiOS's version (one camera) started its frames at the reservation. Here the sensor starts at the graph's Start or
 * when it switches an output on (stream_set_property), both of which mean it wants frames.
 */
static unsigned requests, relinquishes;

static OSStatus
device_request_streams(CMBaseObjectRef o, CFArrayRef streams, CFDictionaryRef options)
{
	(void)o;
	(void)options;
	if (requests++ < 60) {
		rwcam_say("device: streams asked for (%ld)", streams ? (long)CFArrayGetCount(streams) : 0L);
	}
	return 0;
}

static OSStatus
device_relinquish_streams(CMBaseObjectRef o, CFArrayRef streams, CFDictionaryRef options)
{
	(void)o;
	(void)options;
	if (relinquishes++ < 60) {
		rwcam_say("device: streams given back (%ld)", streams ? (long)CFArrayGetCount(streams) : 0L);
	}
	return 0;
}

static OSStatus
device_invalidate(CMBaseObjectRef o)
{
	(void)o;
	return 0;
}

static void
device_finalize(CMBaseObjectRef o)
{
	struct device *d = CMBaseObjectGetDerivedStorage(o);

	if (d->group) {
		CFRelease(d->group);
	}
	if (d->streams) {
		CFRelease(d->streams);
	}
	if (d->supported) {
		CFRelease(d->supported);
	}
}

static CFStringRef
device_description(CMBaseObjectRef o)
{
	(void)o;
	return cfs("<RedwoodCaptureDevice: the Nord 2's back wide, back ultra-wide and front cameras>");
}

static const struct base_class device_base = {
	1, sizeof(struct device), NULL, device_invalidate, device_finalize, device_description, device_copy_property, device_set_property,
	NULL, NULL, NULL
};

/*
 * The processing session (FigCaptureISPProcessingSession): the capture graph asks the device for one (slot 1 of the device's class: device, allocator,
 * a configuration dictionary, where to put it) and keeps asking until it is given one (without it the graph's ISP-processing nodes are not made).
 * A session is a derived object of class FigCaptureISPProcessingSessionGetClassID whose derived table is { version, prepare(session, handler),
 * process(session, buffer, refcon, outputDescriptors, numOutputDescriptors, parameters), flush(session) } (BWFigCaptureISPProcessingSession's methods call these
 * at +0x8, +0x10, +0x18) and whose base class answers its properties. Here: it says what it is asked and takes everything; the pictures come through the
 * streams, as before.
 */
struct session {
	CFDictionaryRef config;
	void            *handler;
};

static CFDictionaryRef
session_supported(void)
{
	/* (the keys are the plain strings of kFigCaptureISPProcessingSessionProperty_*, which CMCapture does not export all of) */
	static const struct prop props[] = {
		{ "SupportedOutputs", "Collection", "ReadOnly" },
		{ "InputPixelBufferAttributes", "Collection", "ReadOnly" },
		{ "OutputPixelBufferAttributes", "Collection", "ReadOnly" },
		{ "DeferAdditionOfAttachments", "Boolean", "ReadWrite" },
		{ "DefaultProcessingParameters", "Collection", "ReadOnly" },
		{ "OutputHandler", "Collection", "WriteOnly" },
	};

	return supported(props, sizeof(props) / sizeof(props[0]));
}

static OSStatus
session_copy_property(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef allocator, void *out)
{
	CFTypeRef value = NULL;

	(void)o;
	(void)allocator;
	if (key_is(key, "SupportedPropertiesDictionary")) {
		value = session_supported();
	} else if (key_is(key, "SupportedOutputs")) {
		CFStringRef ids[2] = { cfs("PrimaryScaler"), cfs("SecondaryScaler") };

		value = CFArrayCreate(NULL, (const void **)ids, 2, &kCFTypeArrayCallBacks);
		CFRelease(ids[0]);
		CFRelease(ids[1]);
	} else if (key_is(key, "InputPixelBufferAttributes") || key_is(key, "OutputPixelBufferAttributes") || key_is(key, "DefaultProcessingParameters")) {
		value = new_dict();
	} else if (key_is(key, "DeferAdditionOfAttachments")) {
		value = CFRetain(kCFBooleanFalse);
	} else {
		return ERR_NOT_AVAILABLE;
	}
	*(CFTypeRef *)out = value;
	return 0;
}

static OSStatus
session_set_property(CMBaseObjectRef o, CFStringRef key, CFTypeRef value)
{
	(void)o;
	say_unknown("session: set", key, value);
	return 0;
}

static OSStatus
session_invalidate(CMBaseObjectRef o)
{
	(void)o;
	return 0;
}

static void
session_finalize(CMBaseObjectRef o)
{
	struct session *s = CMBaseObjectGetDerivedStorage(o);

	if (s->config) {
		CFRelease(s->config);
	}
}

static CFStringRef
session_description(CMBaseObjectRef o)
{
	(void)o;
	return cfs("<RedwoodISPProcessingSession>");
}

/*
 * Prepare: the caller's block is `void (^)(int status)` (its signature in CMCapture is "v12@?0i8"), called when the session is ready.
 * -[BWInferenceEngine _prepareProcessingSessionIfNecessary] (16.7: 0x1a1f709ac unslid) makes a semaphore, calls prepareWithHandler: with a block that
 * signals it and, when Prepare returned 0, waits on it FOR EVER. That wait is on the queue that prepares the graph's deferred nodes
 * (-[BWGraph startDeferredNodePrepareIfNeededWithCompletionHandler:] -> -[BWInferenceNode prepareForCurrentConfigurationToBecomeLive]): with a
 * handler that was kept and never called (as here until run q10ha, and in HakiOS's) the deferred nodes are never prepared, the sinks behind them
 * (still image, movie file, metadata, inference, filter thumbnails) never become active, the graph's start never completes, and the session's
 * stop -- which first waits for that start (captureSession_stopGraph -> -[BWGraph _waitForStartOrCommitToComplete]) -- ends after 10 s in CMCapture's
 * "Timed out waiting for capture graph start to complete" trap (mediaserverd sig 5, run q10ha), or in MediaSafetyNet's abort when that comes first.
 * There is nothing to prepare here: the handler is called at once, with 0. (The block is on its caller's stack: it is not kept.)
 */
static OSStatus
session_prepare(CMBaseObjectRef o, void *handler)
{
	struct session *s = CMBaseObjectGetDerivedStorage(o);
	static unsigned said;

	s->handler = NULL;
	if (said < 8) {
		said++;
		rwcam_say("session: prepare (handler %p): ready at once", handler);
	}
	if (handler != NULL && ((const struct rw_block *)handler)->invoke != NULL) {
		((void (*)(const void *, int))((const struct rw_block *)handler)->invoke)(handler, 0);
	}
	return 0;
}

static OSStatus
session_process(CMBaseObjectRef o, void *buffer, void *refcon, void *descriptors, int count, void *parameters)
{
	(void)o;
	(void)buffer;
	(void)refcon;
	(void)descriptors;
	(void)count;
	(void)parameters;
	rwcam_say_once("session: process called", "", "(not supported)");
	return ERR_UNSUPPORTED;
}

static OSStatus
session_flush(CMBaseObjectRef o)
{
	(void)o;
	return 0;
}

struct session_class {
	uint64_t        version;
	OSStatus        (*prepare)(CMBaseObjectRef, void *);
	OSStatus        (*process)(CMBaseObjectRef, void *, void *, void *, int, void *);
	OSStatus        (*flush)(CMBaseObjectRef);
};

static const struct base_class session_base = {
	1, sizeof(struct session), NULL, session_invalidate, session_finalize, session_description, session_copy_property, session_set_property,
	NULL, NULL, NULL
};
static const struct session_class session_derived = { 1, session_prepare, session_process, session_flush };
static const struct vtable session_vtable = { NULL, &session_base, &session_derived };

static OSStatus
device_processing_session_create(CMBaseObjectRef device, CFAllocatorRef allocator, CFDictionaryRef config, CMBaseObjectRef *out)
{
	CMBaseObjectRef o = NULL;

	(void)device;
	if (session_class_id == NULL || out == NULL || CMDerivedObjectCreate(allocator, &session_vtable, session_class_id(), &o) != 0 || o == NULL) {
		rwcam_say("processing_session_create: cannot be made");
		return ERR_UNSUPPORTED;
	}
	memset(CMBaseObjectGetDerivedStorage(o), 0, sizeof(struct session));
	if (config != NULL) {
		((struct session *)CMBaseObjectGetDerivedStorage(o))->config = CFRetain(config);
	}
	*out = o;
	return 0;
}

static OSStatus
device_pearl_calibration(void *a, void *b, void *c, void *d)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	return ERR_UNSUPPORTED;
}

static const struct device_class device_derived = { 3, (void *)device_processing_session_create, device_request_streams, device_relinquish_streams, device_pearl_calibration };
static const struct vtable device_vtable = { NULL, &device_base, &device_derived };

/* ---- the vendor's create function (as H10ISPCaptureDeviceCreate is called) ------------------------------------------ */

static bool resolved, usable;

/*
 * How the pictures are made, decided at the first stream start (not when mediaserverd loads the bundle: the graphics library is 10 MB of text).
 * By the processor, unless /var/db/nord2-camera-gpu-isp exists: then by the GPU when /System/Library/Extensions/Nord2Metal.bundle/
 * libredwoodair.dylib has rwa_isp_* (GPU_ISP_API.md), and by the processor whenever the GPU does not.
 * The GPU was the default for one run (q10hl), in which the front camera's viewfinder was black with its session running and the log had been
 * filled before it could say why. The GPU's way has never been seen to make a picture on this phone, and what watches it (rwcam_render.c's
 * comparisons with the processor's picture, the black picture found in deliver) sees a wrong or an empty picture but not a driver that hangs
 * or takes mediaserverd down: until a run with the marker has shown the GPU's pictures to be right, a picture by the processor comes first.
 * /var/db/nord2-camera-cpu-isp: the processor only, whatever else is there (as before).
 */
static void
render_once(void)
{
	static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;
	static bool done;
	struct rwa_isp_calls calls;
	void *(*dl_open)(const char *, int);
	void *library = NULL;
	int have = 0;
	bool cpu_only, gpu_asked;

	pthread_mutex_lock(&once);
	if (done) {
		pthread_mutex_unlock(&once);
		return;
	}
	done = true;
	no_metadata = access("/var/db/nord2-camera-nometadata", F_OK) == 0;
	cpu_only = access("/var/db/nord2-camera-cpu-isp", F_OK) == 0;
	gpu_asked = access("/var/db/nord2-camera-gpu-isp", F_OK) == 0;
	memset(&calls, 0, sizeof(calls));
	dl_open = (void *(*)(const char *, int))dlsym(RTLD_DEFAULT, "dlopen");
	if (gpu_asked && !cpu_only && dl_open != NULL && cv_get_iosurface != NULL) {
		library = dl_open(RWA_ISP_LIBRARY, 2 /* RTLD_NOW */);
		RWA_ISP_LOAD(&calls, dlsym, library, 1, have);   /* (version 2 also cleans the noise: rwcam_render.c asks) */
	}
	rwcam_render_setup(have ? &calls : NULL, apply_pieces, rwcam_now_ms);
	/*
	 * (q10jn) /var/db/nord2-camera-zerocopy (with gpu-isp): the readers read their frames into the GPU's memory (rwa_isp_frame_memory) and
	 * the viewfinder's/film's pictures are rendered from there: no 15.7 MB upload a frame (run cam10: the stills' "prepare" 17-39 ms). Off by
	 * default: the processor then reads the same memory for exposure, focus and its own pictures, and how fast it does so is for the phone to say.
	 */
	if (have && access("/var/db/nord2-camera-zerocopy", F_OK) == 0) {
		rwcam_render_zero_copy(true);
		rwcam_core_frame_memory(rwcam_render_frame_memory);
		rwcam_say_keep("render: zero copy on (/var/db/nord2-camera-zerocopy): the frames are read into the GPU's memory");
	}
	rwcam_say_keep("render: pictures are made by %s", have ? "the GPU (/var/db/nord2-camera-gpu-isp; rwa_isp of libredwoodair.dylib), by the processor "
	    "whenever it does not; its first pictures and one every 2 s are compared with the processor's" : cpu_only ?
	    "the processor (/var/db/nord2-camera-cpu-isp)" : !gpu_asked ? "the processor (the GPU is asked only when /var/db/nord2-camera-gpu-isp exists)" :
	    library == NULL ? "the processor (libredwoodair.dylib cannot be opened, or no dlopen / CVPixelBufferGetIOSurface)" :
	    "the processor (libredwoodair.dylib has no rwa_isp_* of version 1: older than y2s39)");
	pthread_mutex_unlock(&once);
}

static bool
resolve(void)
{
	static const char *const sym[RWCAM_CAMERAS] = {
		"kFigCapturePortType_BackFacingCamera", "kFigCapturePortType_BackFacingSuperWideCamera", "kFigCapturePortType_FrontFacingCamera",
	};

	device_class_id = dlsym(RTLD_DEFAULT, "FigCaptureDeviceGetClassID");
	stream_class_id = dlsym(RTLD_DEFAULT, "FigCaptureStreamGetClassID");
	session_class_id = dlsym(RTLD_DEFAULT, "FigCaptureISPProcessingSessionGetClassID");
	pool_type_id = dlsym(RTLD_DEFAULT, "CVPixelBufferPoolGetTypeID");
	group_class_id = dlsym(RTLD_DEFAULT, "FigCaptureSynchronizedStreamsGroupGetClassID");
	cm_set_attachment = dlsym(RTLD_DEFAULT, "CMSetAttachment");
	cm_get_attachment = dlsym(RTLD_DEFAULT, "CMGetAttachment");
	cv_get_iosurface = dlsym(RTLD_DEFAULT, "CVPixelBufferGetIOSurface");
	cf_data_create = dlsym(RTLD_DEFAULT, "CFDataCreate");
	cf_array_get = dlsym(RTLD_DEFAULT, "CFArrayGetValueAtIndex");
	cf_array_type = dlsym(RTLD_DEFAULT, "CFArrayGetTypeID");
	for (int i = 0; i < RWCAM_CAMERAS; i++) {
		CFStringRef *p = dlsym(RTLD_DEFAULT, sym[i]);

		port_type[i] = p != NULL ? *p : NULL;
		if (port_type[i] == NULL) {
			rwcam_say("%s is not there: no stream for %s", sym[i], cams[i].name);
		}
	}
	if (device_class_id == NULL || stream_class_id == NULL || port_type[RWCAM_BACK_WIDE] == NULL || port_type[RWCAM_FRONT] == NULL) {
		rwcam_say("CMCapture's FigCaptureDevice/StreamGetClassID or the wide and front port types are not there: no camera");
		return false;
	}
	return true;
}

OSStatus
redwood_camera_device_create(CFAllocatorRef allocator, CFDictionaryRef options, CMBaseObjectRef *out)
{
	static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;
	CMBaseObjectRef device = NULL, streams[RWCAM_CAMERAS];
	CFTypeRef list[RWCAM_CAMERAS];
	struct device *d;
	int n = 0;
	bool back_only;

	(void)options;
	pthread_mutex_lock(&once);
	if (!resolved) {
		resolved = true;
		usable = resolve();
		if (usable) {
			struct rwcam_sink sink = { sink_frame, sink_stopped, NULL };

			rwcam_core_init(NULL, &sink);
		}
	}
	pthread_mutex_unlock(&once);
	if (!usable || out == NULL) {
		return ERR_UNSUPPORTED;
	}
	/* A/B of run q10gy: with /var/db/nord2-camera-backonly the device is HakiOS's (one stream, the back wide camera, with the stock D22 plists) */
	back_only = access("/var/db/nord2-camera-backonly", F_OK) == 0;
	for (int i = 0; i < RWCAM_CAMERAS; i++) {
		streams[i] = NULL;
		if (back_only && i != RWCAM_BACK_WIDE) {
			continue;
		}
		if (port_type[i] != NULL && (streams[i] = stream_create(allocator, i)) != NULL) {
			list[n++] = streams[i];
		}
	}
	if (n == 0 || CMDerivedObjectCreate(allocator, &device_vtable, device_class_id(), &device) != 0 || device == NULL) {
		for (int i = 0; i < RWCAM_CAMERAS; i++) {
			if (streams[i] != NULL) {
				CFRelease(streams[i]);
			}
		}
		rwcam_say("the device cannot be made");
		return ERR_UNSUPPORTED;
	}
	d = CMBaseObjectGetDerivedStorage(device);
	memset(d, 0, sizeof(*d));
	d->streams = CFArrayCreate(NULL, (const void **)list, n, &kCFTypeArrayCallBacks);
	d->supported = supported(device_props, sizeof(device_props) / sizeof(device_props[0]));
	/* the two back cameras together (the dual-wide device): only with the kernel's second node; /var/db/nord2-camera-nodual: never */
	if (streams[RWCAM_BACK_WIDE] != NULL && streams[RWCAM_BACK_ULTRAWIDE] != NULL) {
		bool nodual = access("/var/db/nord2-camera-nodual", F_OK) == 0, node2 = rwcam_core_pair_possible();

		if (!nodual && node2) {
			d->group = group_create(allocator, streams[RWCAM_BACK_WIDE], streams[RWCAM_BACK_ULTRAWIDE]);
		}
		rwcam_say("the two back cameras together (synchronized streams group, for the dual-wide device): %s", d->group != NULL ? "YES" : nodual ?
		    "no (/var/db/nord2-camera-nodual)" : !node2 ? "no (" REDWOOD_CAMERA_DEVICE2 " is not there: one sensor at a time)" : "no (the group cannot be made)");
	}
	for (int i = 0; i < RWCAM_CAMERAS; i++) {
		if (streams[i] != NULL) {
			CFRelease(streams[i]);
		}
	}
	*out = device;
	rwcam_say("the device made: %d streams (%s)", n, back_only ? "back wide only: /var/db/nord2-camera-backonly" : "back wide, back ultra-wide, front");
	return 0;
}
