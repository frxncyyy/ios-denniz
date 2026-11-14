/*
 * Diagnostics only (run q10gn: the device is made at every Camera launch and then nothing asks for a stream). Nothing here changes what CMCapture
 * does: after the capture servers are up it says what sources CMCapture made of our device and what the camera capability questions answer, and
 * three methods are wrapped to say that they were called. Every CoreFoundation / other entry point not in rwcam_abi.h is found with dlsym.
 */
#include "rwcam_abi.h"
#include "rwcam_log.h"
#include "rwcam_device.h"

void rwcam_diag_after_servers(void);
void rwcam_diag_hooks(void);
static CFTypeRef ask(id object, const char *selector);

static const void *
array_at(CFTypeRef array, CFIndex i)
{
	const void *(*at)(CFTypeRef, CFIndex) = (const void *(*)(CFTypeRef, CFIndex))dlsym(RTLD_DEFAULT, "CFArrayGetValueAtIndex");

	return at != NULL ? at(array, i) : NULL;
}

static CFStringRef
cfstr_static(const char *text)
{
	return CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
}

static void
say_long(const char *what, CFTypeRef value, unsigned limit)
{
	char text[6000];
	CFStringRef d = value != NULL ? CFCopyDescription(value) : NULL;
	unsigned long n;

	text[0] = 0;
	if (d != NULL) {
		CFStringGetCString(d, text, (CFIndex)sizeof(text), kCFStringEncodingUTF8);
		CFRelease(d);
	} else {
		strlcpy(text, "(null)", sizeof(text));
	}
	for (char *c = text; *c; c++) {
		if (*c == '\n' || *c == '\t') {
			*c = ' ';
		}
	}
	for (n = 0; text[n]; n++) {
	}
	if (n > limit) {
		n = limit;
	}
	for (unsigned long at = 0; at < n; at += 300) {
		char piece[304];
		unsigned long len = n - at > 300 ? 300 : n - at;

		memcpy(piece, text + at, len);
		piece[len] = 0;
		rwcam_say("diag: %s%s %s", what, at ? " +" : ":", piece);
	}
}

static void
say_source(const char *label, CFTypeRef src)
{
	CFTypeID (*array_id)(void) = (CFTypeID (*)(void))dlsym(RTLD_DEFAULT, "CFArrayGetTypeID");

	if (src == NULL || CFGetTypeID(src) != CFDictionaryGetTypeID()) {
		say_long(label, src, 600);
		return;
	}
	{
		CFIndex n = CFDictionaryGetCount((CFDictionaryRef)src);
		const void *keys[64], *values[64];

		if (n > 64) {
			rwcam_say("diag: %s: %ld keys", label, (long)n);
			return;
		}
		CFDictionaryGetKeysAndValues((CFDictionaryRef)src, keys, values);
		for (CFIndex i = 0; i < n; i++) {
			char key[100] = "?", what[240];

			if (CFGetTypeID(keys[i]) == CFStringGetTypeID()) {
				CFStringGetCString(keys[i], key, sizeof(key), kCFStringEncodingUTF8);
			}
			snprintf(what, sizeof(what), "%s.%s", label, key);
			if (values[i] != NULL && array_id != NULL && CFGetTypeID(values[i]) == array_id()) {
				rwcam_say("diag: %s: array of %ld", what, (long)CFArrayGetCount(values[i]));
			} else if (values[i] != NULL && CFGetTypeID(values[i]) == CFDictionaryGetTypeID() && CFDictionaryGetCount(values[i]) > 12) {
				rwcam_say("diag: %s: dictionary of %ld", what, (long)CFDictionaryGetCount(values[i]));
				if (key[0] == 'A' && key[1] == 't') {
					say_long(what, values[i], 5400);
				}
			} else {
				say_long(what, values[i], 900);
			}
		}
	}
}

/*
 * run q10gs: TCC grants the Camera its camera and the entitlement is seen, yet the Camera makes a session with no source. What an AVFoundation
 * client sees is asked here, from this process, the way the Camera asks (AVCaptureDevice, over XPC to the capture source server in this same
 * process: on a thread of its own, some seconds after the servers are up).
 */
/* (the client probe of runs q10g*, /var/db/nord2-camera-clientprobe -- an AVCaptureSession made in this process, the sources' reply -- was
 * here until run user1: taken out for room) */
/*
 * A PHOTOGRAPH AS HEIC CANNOT BE ENCODED HERE. BWPhotoEncoderNode encodes 'hvc1' through VTCompressionSessionCreate (CMPhoto 0x1a5dad298)
 * and this phone has neither Apple's hardware encoder nor a software one (/System/Library/VideoCodecs is empty, no VCPHEVCRegisterEncoder in
 * the shared cache); a failed encode is the node's error, with no JPEG instead. JPEG needs no hardware: CMPhoto goes to its software encoder
 * when AppleJPEGDriver does not open (0x1a5d6a990). What makes a client ask for HEIC is the source's attributes: HEVC, HEIF and PrefersHEVC,
 * set for every source by the platform alone (_csu_createVideoCaptureSourceInfoDictionary, 0x1a20b6e2c); -[AVCaptureFigVideoDevice
 * isHEIFSupported] (0x19f095d80) reads them and AVCapturePhotoOutput then offers 'hvc1' beside 'jpeg'. Here the three are taken out of a
 * source's attributes where that dictionary can be changed (said either way). Not while /var/db/nord2-camera-keephevc exists.
 */
static void
source_without_hevc(const char *label, CFTypeRef src)
{
	static const char *const keys[3] = { "HEVC", "HEIF", "PrefersHEVC" };
	void (*remove_value)(CFTypeRef, const void *) = (void (*)(CFTypeRef, const void *))dlsym(RTLD_DEFAULT, "CFDictionaryRemoveValue");
	const char *(*class_name)(CFTypeRef) = (const char *(*)(CFTypeRef))dlsym(RTLD_DEFAULT, "object_getClassName");
	Boolean (*cf_mutable)(CFTypeRef) = (Boolean (*)(CFTypeRef))dlsym(RTLD_DEFAULT, "_CFDictionaryIsMutable");
	CFStringRef k = cfstr_static("Attributes");
	CFTypeRef attributes = src != NULL && CFGetTypeID(src) == CFDictionaryGetTypeID() ? CFDictionaryGetValue((CFDictionaryRef)src, k) : NULL;
	const char *cls = attributes != NULL && class_name != NULL ? class_name(attributes) : NULL;
	int had = 0, taken = 0;

	CFRelease(k);
	if (attributes == NULL || CFGetTypeID(attributes) != CFDictionaryGetTypeID() || cls == NULL || access("/var/db/nord2-camera-keephevc", F_OK) == 0) {
		return;
	}
	/* (a dictionary of the mutable class, or CoreFoundation's own that says it is: nothing else is touched) */
	bool can = remove_value != NULL && (strcmp(cls, "__NSDictionaryM") == 0 ||
	    (strcmp(cls, "__NSCFDictionary") == 0 && cf_mutable != NULL && cf_mutable(attributes)));
	for (unsigned i = 0; i < 3; i++) {
		CFStringRef key = cfstr_static(keys[i]);

		if (CFDictionaryGetValue((CFDictionaryRef)attributes, key) != NULL) {
			had++;
			if (can) {
				remove_value(attributes, key);
				taken++;
			}
		}
		CFRelease(key);
	}
	rwcam_say("diag: still: %s: %d of HEVC/HEIF/PrefersHEVC in its attributes (%s), %d taken out: photographs as %s", label, had, cls, taken,
	    had == taken ? "JPEG" : "HEIC STILL OFFERED (it cannot be encoded here)");
}

void
rwcam_diag_after_servers(void)
{
	CFTypeID (*array_id)(void) = (CFTypeID (*)(void))dlsym(RTLD_DEFAULT, "CFArrayGetTypeID");
	Class cls = objc_getClass("FigCaptureSourceBackings");
	Boolean (*mg)(CFStringRef) = (Boolean (*)(CFStringRef))dlsym(RTLD_DEFAULT, "MGGetBoolAnswer");
	Boolean (*avg)(CFStringRef) = (Boolean (*)(CFStringRef))dlsym(RTLD_DEFAULT, "AVGestaltGetBoolAnswer");
	static const char *const mg_keys[] = { "RearFacingCameraCapability", "FrontFacingCameraCapability", "RearFacingCameraSuperWideCameraCapability",
		"rear-facing-camera", "front-facing-camera", "still-camera", "video-camera", "CameraCapability", "RearFacingTelephotoCameraCapability" };
	static const char *const avg_keys[] = { "AVGQRearFacingCameraCapability", "AVGQFrontFacingCameraCapability", "AVGQRearFacingCameraSuperWideCameraCapability" };

	if (cls != NULL) {
		id backings = ((id (*)(Class, SEL))objc_msgSend)(cls, sel_registerName("sharedCaptureSourceBackings"));
		CFTypeRef infos = backings ? (CFTypeRef)((id (*)(id, SEL))objc_msgSend)(backings, sel_registerName("sourceInfoDictionaries")) : NULL;

		if (infos == NULL) {
			rwcam_say("diag: sources: backings %s, sourceInfoDictionaries nil", backings ? "there" : "nil");
		} else if (array_id != NULL && CFGetTypeID(infos) == array_id()) {
			CFIndex n = CFArrayGetCount(infos);

			rwcam_say("diag: sources: array of %ld", (long)n);
			for (CFIndex i = 0; i < n && i < 8; i++) {
				char label[24];

				snprintf(label, sizeof(label), "source %ld", (long)i);
				say_source(label, array_at(infos, i));
				source_without_hevc(label, array_at(infos, i));
			}
		} else if (CFGetTypeID(infos) == CFDictionaryGetTypeID()) {
			CFIndex n = CFDictionaryGetCount(infos);
			const void *keys[16], *values[16];

			rwcam_say("diag: sources: dictionary of %ld", (long)n);
			if (n <= 16) {
				CFDictionaryGetKeysAndValues(infos, keys, values);
				for (CFIndex i = 0; i < n; i++) {
					char label[24];

					snprintf(label, sizeof(label), "source %ld", (long)i);
					say_long(label, keys[i], 200);
					say_source(label, values[i]);
					source_without_hevc(label, values[i]);
				}
			}
		} else {
			say_long("sources (other type)", infos, 1500);
		}
	} else {
		rwcam_say("diag: no FigCaptureSourceBackings class");
	}
	for (unsigned i = 0; mg != NULL && i < sizeof(mg_keys) / sizeof(mg_keys[0]); i++) {
		CFStringRef k = CFStringCreateWithCString(NULL, mg_keys[i], kCFStringEncodingUTF8);

		rwcam_say("diag: MGGetBoolAnswer(%s) = %d", mg_keys[i], (int)mg(k));
		CFRelease(k);
	}
	for (unsigned i = 0; i < sizeof(avg_keys) / sizeof(avg_keys[0]); i++) {
		CFStringRef *k = (CFStringRef *)dlsym(RTLD_DEFAULT, avg_keys[i]);

		if (avg != NULL && k != NULL && *k != NULL) {
			rwcam_say("diag: AVGestaltGetBoolAnswer(%s) = %d", avg_keys[i], (int)avg(*k));
		} else {
			rwcam_say("diag: %s: %s", avg_keys[i], avg == NULL ? "no AVGestaltGetBoolAnswer here" : "no such constant here");
		}
	}
}

/* ---- wrapped methods: said, then the stock code ---------------------------------------------------------------------------------------- */
static IMP stock_config_init, stock_copy_streams, stock_copy_device;
static CFTypeRef ask(id object, const char *selector);
static volatile int log_on;

/*
 * (camprev, run perf1a) Magnifier: Photo preset, a video data output with no size of its own (automaticallyConfiguresOutputBufferDimensions
 * NO, MagnifierSupport 0x200f1ced8) gets the format's whole 4096x3072; _csp_videoCaptureDimensionsFromConnectionConfigurations
 * (0x1a1fcb14c) makes the stream's video capture output (our PrimaryScaler) as big as the largest video data output (or the preview's
 * size), so every frame was a 4096x3072 ISP picture on the GPU (~39 ms) plus VideoToolbox's 3072x4096 rotation for that output
 * (VTMTSComputeFunction 48x128 groups, ~40 ms) on a GPU without preemption: the whole UI at 10-14 fps. A video data output of a 4:3
 * picture of more than 6 MP is made half as wide and half as high here, in the session configuration as mediaserverd decodes it
 * (FigVideoCaptureConnectionConfiguration's outputWidth/outputHeight, plain ivars read by nonRotatedOutputDimensions too): the
 * PrimaryScaler follows (2048x1536). Photographs are the StillImage output's (their own 4096x3072 pool): not touched; nor films (16:9),
 * nor a movie file output. /var/db/nord2-camera-fullvdo: as asked. /var/db/nord2-camera-vdo-quarter: a quarter as wide instead.
 */
/* (host-tested: test/t_vdo.c) a 4:3 picture (either way round, within 1 %) of more than 6 MP -> 1/div as wide (a multiple of 16) and as
 * high in the same proportion (even); 0: left as it is */
int
rwcam_vdo_target(int w, int h, int div, int *nw, int *nh)
{
	long lo = w < h ? w : h, hi = w < h ? h : w;

	if (div < 2 || lo <= 0 || (long)w * h <= 6000000L || hi * 300 - lo * 400 > lo * 4 || lo * 400 - hi * 300 > lo * 4) {
		return 0;
	}
	*nw = w / div / 16 * 16;
	*nh = (int)((long)*nw * h / w) & ~1;
	return *nw >= 16 && *nh >= 16;
}

static int vdo_shrink_mode = -1;

static void
vdo_shrink(id config)
{
	Class vcc = objc_getClass("FigVideoCaptureConnectionConfiguration"), vdo = objc_getClass("FigCaptureVideoDataSinkConfiguration");
	SEL kind = sel_registerName("isKindOfClass:");
	id all;
	long n;

	if (vdo_shrink_mode < 0) {
		vdo_shrink_mode = access("/var/db/nord2-camera-fullvdo", F_OK) == 0 ? 0 : access("/var/db/nord2-camera-vdo-quarter", F_OK) == 0 ? 4 : 2;
	}
	if (vdo_shrink_mode == 0 || config == NULL || vcc == NULL || vdo == NULL) {
		return;
	}
	all = (id)ask(config, "connectionConfigurations");
	n = all != NULL ? ((long (*)(id, SEL))objc_msgSend)(all, sel_registerName("count")) : 0;
	for (long i = 0; i < n && i < 32; i++) {
		id c = ((id (*)(id, SEL, unsigned long))objc_msgSend)(all, sel_registerName("objectAtIndex:"), (unsigned long)i);
		id sink = c != NULL && ((bool (*)(id, SEL, Class))objc_msgSend)(c, kind, vcc) ? (id)ask(c, "sinkConfiguration") : NULL;
		int w, h, nw, nh;

		if (sink == NULL || !((bool (*)(id, SEL, Class))objc_msgSend)(sink, kind, vdo)) {
			continue;
		}
		w = ((int (*)(id, SEL))objc_msgSend)(c, sel_registerName("outputWidth"));
		h = ((int (*)(id, SEL))objc_msgSend)(c, sel_registerName("outputHeight"));
		if (!rwcam_vdo_target(w, h, vdo_shrink_mode, &nw, &nh)) {
			continue;
		}
		((void (*)(id, SEL, int))objc_msgSend)(c, sel_registerName("setOutputWidth:"), nw);
		((void (*)(id, SEL, int))objc_msgSend)(c, sel_registerName("setOutputHeight:"), nh);
		rwcam_say("preview: a video data output of %dx%d made %dx%d (1/%d; the stream's PrimaryScaler follows; photographs keep their own size; "
		    "/var/db/nord2-camera-fullvdo: as asked)", w, h, nw, nh, vdo_shrink_mode);
	}
}

static id
config_init(id self, SEL sel, id encoding)
{
	id made = ((id (*)(id, SEL, id))stock_config_init)(self, sel, encoding);
	static int said;

	vdo_shrink(made);
	log_on = 1;
	if (said++ < 12) {
		say_long("a client committed a session configuration (FigCaptureSessionConfiguration initWithXPCEncoding:)", (CFTypeRef)made, 1500);
		say_long("  its sourceConfigurations", ask(made, "sourceConfigurations"), 1500);
		say_long("  its connectionConfigurations", ask(made, "connectionConfigurations"), 1500);
	}
	return made;
}

static id
copy_streams(id self, SEL sel, id positions, id types, id device, void *error)
{
	id streams = ((id (*)(id, SEL, id, id, id, void *))stock_copy_streams)(self, sel, positions, types, device, error);
	static int said;

	if (said++ < 20) {
		say_long("vendor copyStreamsWithPositions", (CFTypeRef)positions, 300);
		say_long("vendor copyStreams deviceTypes", (CFTypeRef)types, 300);
		say_long("vendor copyStreams gives", (CFTypeRef)streams, 600);
	}
	return streams;
}

static id
copy_device(id self, SEL sel, uintptr_t client, uintptr_t inform, uintptr_t error)
{
	id device = ((id (*)(id, SEL, uintptr_t, uintptr_t, uintptr_t))stock_copy_device)(self, sel, client, inform, error);
	static int said;

	if (said >= 1) {
		log_on = 1;             /* the second device is the prewarm of a Camera launch: CMCapture's log from here */
	}
	if (said++ < 20) {
		rwcam_say("diag: vendor copyDeviceForClient:%ld gives %s", (long)client, device ? "a device" : "nil");
	}
	return device;
}

/* run q10go: the client commits configurations and the server never asks the vendor for a device: who holds the session back? */
typedef uintptr_t (*generic_imp)(id, SEL, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static IMP stock_parsed;

static CFTypeRef
ask(id object, const char *selector)
{
	return object != NULL ? (CFTypeRef)((id (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector)) : NULL;
}

static uintptr_t
parsed_init(id self, SEL sel, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
	uintptr_t made = ((generic_imp)stock_parsed)(self, sel, a, b, c, d);
	static int said;

	if (said++ < 20) {
		rwcam_say("diag: FigCaptureSessionParsedConfiguration %s for the caller at %#lx (unslid)", made ? "made" : "nil",
		    (unsigned long)__builtin_return_address(0) - redwood_cache_slide());
		say_long("  its parsedCameraSourceConfigurations", ask((id)made, "parsedCameraSourceConfigurations"), 900);
	}
	return made;
}

/* (runs q10go to q10gr, the session that would not start: the client monitor's state changes, RunningBoard's mapping, the display layout, the
 * probe of the client as tccd sees it and the audio interruptions were said here until run q10ig -- taken out for room, the cause long found) */

/*
 * CMCapture's own log (os_log) on the console: libsystem_trace's os_log_set_hook calls a block with every message of the process once its
 * arguments are flattened; os_log_copy_message_string makes the text. Said from the first session configuration a client commits, only the
 * Fig/CMCapture ones (their formats begin "<<<< ") and those of a camera/coremedia subsystem, and no more than LOG_LINES of them.
 */
#define LOG_LINES 40000        /* (what is repeated is held back by the log itself: rwcam_loglimit.h) */
struct log_block { void *isa; int32_t flags, reserved; void *invoke; const void *descriptor; };
static const unsigned long log_block_descriptor[2] = { 0, sizeof(struct log_block) };
static struct log_block log_block;
static struct log_block *previous_log_hook;
static volatile int log_lines;
static char *(*log_copy)(void *);

static bool
has(const char *text, const char *word)
{
	if (text == NULL) {
		return false;
	}
	for (; *text; text++) {
		unsigned long i = 0;

		while (word[i] && text[i] == word[i]) {
			i++;
		}
		if (word[i] == 0) {
			return true;
		}
	}
	return false;
}

static void
log_hook(struct log_block *block, uint8_t type, void *msg)
{
	const char *format = *(const char **)((char *)msg + 0x58);
	const char *subsystem = *(const char **)((char *)msg + 0x80);

	(void)block;
	if ((log_on || has(format, "sources with types") || has(format, "FigCaptureSource ")) && log_lines < LOG_LINES && format != NULL && !(format[0] == 'm' && format[1] == '1' && format[2] == '1') &&
	    ((format[0] == '<' && format[1] == '<' && format[2] == '<' && format[3] == '<') || has(subsystem, "camera") || has(subsystem, "coremedia") || has(subsystem, "cmio"))) {
		char *text = log_copy(msg);

		log_lines++;
		if (text != NULL) {
			for (char *c = text; *c; c++) {
				if (*c == '\n') {
					*c = ' ';
				}
			}
			rwcam_say("log %u: %.380s", (unsigned)type, text);
			free(text);
		}
	}
	if (previous_log_hook != NULL) {
		((void (*)(struct log_block *, uint8_t, void *))previous_log_hook->invoke)(previous_log_hook, type, msg);
	}
}

static void
log_hook_install(void)
{
	void *(*set_hook)(uint8_t, void *) = (void *(*)(uint8_t, void *))dlsym(RTLD_DEFAULT, "os_log_set_hook");
	void *global_block = dlsym(RTLD_DEFAULT, "_NSConcreteGlobalBlock");

	log_copy = (char *(*)(void *))dlsym(RTLD_DEFAULT, "os_log_copy_message_string");
	if (set_hook == NULL || global_block == NULL || log_copy == NULL) {
		rwcam_say("diag: no os_log hook here (set_hook %d, global block %d, copy %d)", set_hook != NULL, global_block != NULL, log_copy != NULL);
		return;
	}
	log_block.isa = global_block;
	log_block.flags = 1 << 28;
	log_block.invoke = (void *)log_hook;
	log_block.descriptor = log_block_descriptor;
	previous_log_hook = set_hook(1, &log_block);
	rwcam_say("diag: os_log hook installed (previous %s); said from the first committed session configuration", previous_log_hook ? "kept" : "none");
}

static void
wrap(const char *class_name, const char *selector, IMP ours, IMP *stock)
{
	Class cls = objc_getClass(class_name);
	Method m = cls != NULL ? class_getInstanceMethod(cls, sel_registerName(selector)) : NULL;

	if (m == NULL) {
		rwcam_say("diag: -[%s %s] not there", class_name, selector);
		return;
	}
	*stock = method_getImplementation(m);
	method_setImplementation(m, ours);
	rwcam_say("diag: -[%s %s] (%s) wrapped", class_name, selector, method_getTypeEncoding(m));
}

/*
 * run q10h8: with the authorization fixed the session builds its graph and mediaserverd aborts on an uncaught Objective-C exception
 * (std::terminate from a dispatch queue). Every exception thrown in this process is said here, with who threw it (frame pointers walked,
 * addresses of the shared cache without its slide), before the stock handling goes on.
 */
static id (*previous_preprocessor)(id);

static id
exception_said(id exception)
{
	static int said;

	if (said++ < 12) {
		unsigned long slide = redwood_cache_slide();
		void **frame = (void **)__builtin_frame_address(0);
		char line[360];
		int n = 0;

		say_long("EXCEPTION name", ask(exception, "name"), 200);
		say_long("EXCEPTION reason", ask(exception, "reason"), 900);
		for (int i = 0; i < 26 && frame != NULL && ((unsigned long)frame & 7) == 0; i++) {
			unsigned long lr = (unsigned long)frame[1] & 0x0000007fffffffffUL;
			void **next = (void **)frame[0];

			if (lr == 0) {
				break;
			}
			n += snprintf(line + n, sizeof(line) - (unsigned long)n, " %#lx", lr >= 0x180000000UL + slide ? lr - slide : lr);
			if (n > 300) {
				rwcam_say("diag: EXCEPTION thrown from%s", line);
				n = 0;
			}
			if (next <= frame) {
				break;
			}
			frame = next;
		}
		if (n > 0) {
			rwcam_say("diag: EXCEPTION thrown from%s", line);
		}
	}
	return previous_preprocessor != NULL ? previous_preprocessor(exception) : exception;
}

static void
exception_hook_install(void)
{
	id (*(*set)(id (*)(id)))(id) = (id (*(*)(id (*)(id)))(id))dlsym(RTLD_DEFAULT, "objc_setExceptionPreprocessor");

	if (set == NULL) {
		rwcam_say("diag: no objc_setExceptionPreprocessor here");
		return;
	}
	previous_preprocessor = set(exception_said);
	rwcam_say("diag: exceptions thrown in this process are said (previous preprocessor %s)", previous_preprocessor ? "kept" : "none");
}

/*
 * A tap in the viewfinder. Camera.app's focus / exposure point (AVCaptureDevice setFocusPointOfInterest: + setFocusMode:, the same for exposure,
 * AE/AF lock) arrives in mediaserverd at -[BWFigVideoCaptureDevice setFocusModeAutoWithRect:restrictToRect:continuous:smooth:rangeRestrictionNear:
 * rangeRestrictionFar:isFocusRectInOverscanSpace:] (i72@0:8{CGRect}16B48B52B56f60f64B68), setContinuousAutoFocusRect: and
 * setExposureModeAutoWithRect: (i48@0:8{CGRect}16), setExposureModeLocked (i16@0:8). From there CMCapture's own 3A (the sample buffer
 * processor of FigSampleBufferProcessorCreateForAutofocus: FigImageControl_Focus / _Exposure) would send the stream FocusNow, AFWindowParams ...
 * and follow the ISP's answer in every frame's metadata (AFStatus, FocusPeakSumArray, CurrentFocusPosition ...) -- metadata of Apple's ISP
 * firmware that this stream does not give: FigImageControl_Focus leaves at its first look without FocusPeakSumArray, so nothing ever reached
 * the stream. The focus and exposure here are the bundle's own (rwcam_core.c): the methods are wrapped, the rectangle is given to them, and the
 * stock method runs as before. /var/db/nord2-camera-notap: not wrapped.
 */
struct point_rect { double x, y, w, h; };
static IMP stock_focus_rect, stock_caf_rect, stock_exposure_rect, stock_exposure_locked;

static int
focus_rect(id self, SEL _cmd, struct point_rect r, bool restrict_to, bool continuous, bool smooth, float near, float far, bool overscan)
{
	rwcam_device_point(RWCAM_POINT_FOCUS, r.x, r.y, r.w, r.h, continuous);
	return ((int (*)(id, SEL, struct point_rect, bool, bool, bool, float, float, bool))stock_focus_rect)(self, _cmd, r, restrict_to, continuous, smooth,
	    near, far, overscan);
}

static int
caf_rect(id self, SEL _cmd, struct point_rect r)
{
	rwcam_device_point(RWCAM_POINT_FOCUS, r.x, r.y, r.w, r.h, true);
	return ((int (*)(id, SEL, struct point_rect))stock_caf_rect)(self, _cmd, r);
}

static int
exposure_rect(id self, SEL _cmd, struct point_rect r)
{
	rwcam_device_point(RWCAM_POINT_EXPOSURE, r.x, r.y, r.w, r.h, true);
	return ((int (*)(id, SEL, struct point_rect))stock_exposure_rect)(self, _cmd, r);
}

static int
exposure_locked(id self, SEL _cmd)
{
	rwcam_device_point(RWCAM_POINT_EXPOSURE_LOCK, 0, 0, 0, 0, false);
	return ((int (*)(id, SEL))stock_exposure_locked)(self, _cmd);
}

/*
 * Run q10hk (the first with Camera.app told it is an iPhone 11): mediaserverd threw "Can't add a nil node" from -[BWGraph addNode:error:] <-
 * -[FigCaptureStillImageSinkPipeline _buildStillImageSinkPipelineWithConfiguration:...] + 0x7ac (0x1a1f23750 of the cache before its slide) and
 * aborted, in two of three starts of the (front camera's) session.
 *   - The node is the BWSISNode that the pipeline makes only when its configuration's noiseReductionAndFusionScheme is not 2 (0x1a1f23498:
 *     cmp w0, #2; cset w19, ne ... and w8, w19, w23; cbz w8 -> no such node); with 2 it makes a BWGNRNode instead, as in every run before.
 *   - -[BWSISNode _unpackSISOptions] (0x1a20f1d64) gives up -- the init gives nil -- when the sensor's tuning dictionary has no
 *     NoiseModelParameters and the scheme is 0. The iPhone X's CameraSetup.plist has that key for no sensor: every camera of its
 *     AVCaptureSession.plist says stillImageNoiseReductionAndFusionScheme 2, its still pipeline never has a BWSISNode.
 *   - The scheme the configuration gets (captureSession_buildGraphWithConfiguration, 0x1a1f143d0 .. 0x1a1f14718) is read from the CLIENT's
 *     FigCaptureSource: FigCaptureSourceGetStillImageNoiseReductionAndFusionScheme copies its AttributesDictionary and gives 0 when that fails.
 *     All five sources had 2 there in that run's log.
 *   - In all three starts Camera.app itself crashed (in the graphics library's shader compiler: the iPhone 11's viewfinder uses Metal) one to
 *     twenty log lines after the device was brought up, that is WHILE mediaserverd was building the graph; in the two that threw, the
 *     client's end ("is now Terminated", state "Unknown" / "Backgrounded") is in the log before the exception; in the third the build got
 *     past the still pipeline and the session was stopped in the ordinary way.
 * So (the last step not seen, taken from the above): the client died, its source was invalidated, the scheme read 0, and a graph for a
 * client that is no more took mediaserverd down with it. Here: every scheme given to a still pipeline configuration is said, one under 2 is
 * made 2 (this model's, the one its tuning data is for: the graph of a dead client is then built and torn down as any other), and what a
 * BWSISNode is made of and whether it came out nil is said, which shows at the next run whether that was it.
 * /var/db/nord2-camera-noschemefix: said only, nothing changed.
 */
static IMP stock_set_scheme, stock_sis_init;
static int scheme_fix = -1;

static void
set_scheme(id self, SEL _cmd, int scheme)
{
	static unsigned said;
	int given = scheme;

	if (scheme_fix < 0) {
		scheme_fix = access("/var/db/nord2-camera-noschemefix", F_OK) != 0;
	}
	if (scheme < 2 && scheme_fix == 1) {
		scheme = 2;
	}
	if (said < 30) {
		said++;
		rwcam_say("diag: still pipeline configuration %p: noiseReductionAndFusionScheme %d%s", (void *)self, given, given == scheme ? "" :
		    given < 2 ? " -> made 2 (the iPhone X's: a BWSISNode cannot be made of its tuning data)" : "");
	}
	((void (*)(id, SEL, int))stock_set_scheme)(self, _cmd, scheme);
}

static bool
has_key(id dictionary, const char *key)
{
	CFStringRef k;
	bool there;

	if (dictionary == NULL || CFGetTypeID((CFTypeRef)dictionary) != CFDictionaryGetTypeID()) {
		return false;
	}
	k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
	there = CFDictionaryGetValue((CFDictionaryRef)dictionary, k) != NULL;
	CFRelease(k);
	return there;
}

static id
sis_init(id self, SEL _cmd, id tuning, id sensor, int scheme)
{
	id node = ((id (*)(id, SEL, id, id, int))stock_sis_init)(self, _cmd, tuning, sensor, scheme);

	rwcam_say("diag: BWSISNode made with fusion scheme %d, tuning dictionary %s (StillImageStabilization %d, NoiseModelParameters %d), sensor "
	    "dictionary %s (StillImageStabilization %d, NoiseModelParameters %d): %s", scheme, tuning ? "there" : "NIL", has_key(tuning,
	    "StillImageStabilization"), has_key(tuning, "NoiseModelParameters"), sensor ? "there" : "NIL", has_key(sensor, "StillImageStabilization"),
	    has_key(sensor, "NoiseModelParameters"), node ? "a node" : "NIL (the graph would throw \"Can't add a nil node\")");
	return node;
}

/*
 * Run q10hl: the session of the dual-wide device ended with -12787 at every start, and nothing in the log said which property: CMCapture's own
 * wrappers (BWFigCaptureStream, BWFigCaptureSynchronizedStreamsGroup, BWFigCaptureDevice) answer -12787 by themselves for a key that is not in
 * the object's SupportedPropertiesDictionary, without asking the object. The strict calls of the three (getProperty:error:, copyProperty:error:,
 * setProperty:value: -- the ...IfSupported ones do not come through these) are wrapped: every one that ends in an error is said, with the object,
 * the key, the error and, for a set, the value. Nothing is changed. /var/db/nord2-camera-nopropdiag: not wrapped.
 */
#define BW_CLASSES 3
static const char *const bw_class[BW_CLASSES] = { "BWFigCaptureStream", "BWFigCaptureSynchronizedStreamsGroup", "BWFigCaptureDevice" };
static IMP stock_bw_get[BW_CLASSES], stock_bw_copy[BW_CLASSES], stock_bw_set[BW_CLASSES];

static void
bw_failed(id self, const char *what, id key, int error, id value, bool with_value)
{
	char who[100], name[100], text[200];
	CFStringRef d = CFCopyDescription((CFTypeRef)self);

	who[0] = name[0] = text[0] = 0;
	if (d != NULL) {
		CFStringGetCString(d, who, (CFIndex)sizeof(who), kCFStringEncodingUTF8);
		CFRelease(d);
	}
	if (key != NULL && CFGetTypeID((CFTypeRef)key) == CFStringGetTypeID()) {
		CFStringGetCString((CFStringRef)key, name, (CFIndex)sizeof(name), kCFStringEncodingUTF8);
	}
	if (with_value && value != NULL && (d = CFCopyDescription((CFTypeRef)value)) != NULL) {
		CFStringGetCString(d, text, (CFIndex)sizeof(text), kCFStringEncodingUTF8);
		CFRelease(d);
	}
	for (char *c = text; *c; c++) {
		if (*c == '\n' || *c == '\t') {
			*c = ' ';
		}
	}
	rwcam_say("diag: PROPERTY ERROR %d%s: %s %s of %.90s%s%.180s", error, error == -12787 ? " (the key is not in the object's SupportedPropertiesDictionary)" :
	    error == -12783 || error == -12780 || error == -12782 ? " (our object's answer)" : "", what, name[0] ? name : "(no key)", who,
	    with_value ? " = " : "", with_value ? (value != NULL ? text : "(null)") : "");
}

#define BW_WRAPPERS(i) \
static id bw_get_##i(id self, SEL _cmd, id key, int *error) \
{ \
	int e = 0; \
	id r = ((id (*)(id, SEL, id, int *))stock_bw_get[i])(self, _cmd, key, &e); \
	if (error != NULL) { *error = e; } \
	if (e != 0) { bw_failed(self, "getProperty", key, e, NULL, false); } \
	return r; \
} \
static id bw_copy_##i(id self, SEL _cmd, id key, int *error) \
{ \
	int e = 0; \
	id r = ((id (*)(id, SEL, id, int *))stock_bw_copy[i])(self, _cmd, key, &e); \
	if (error != NULL) { *error = e; } \
	if (e != 0) { bw_failed(self, "copyProperty", key, e, NULL, false); } \
	return r; \
} \
static int bw_set_##i(id self, SEL _cmd, id key, id value) \
{ \
	int e = ((int (*)(id, SEL, id, id))stock_bw_set[i])(self, _cmd, key, value); \
	if (e != 0) { bw_failed(self, "setProperty", key, e, value, true); } \
	return e; \
}
BW_WRAPPERS(0)
BW_WRAPPERS(1)
BW_WRAPPERS(2)
static const IMP bw_ours[BW_CLASSES][3] = {
	{ (IMP)bw_get_0, (IMP)bw_copy_0, (IMP)bw_set_0 }, { (IMP)bw_get_1, (IMP)bw_copy_1, (IMP)bw_set_1 }, { (IMP)bw_get_2, (IMP)bw_copy_2, (IMP)bw_set_2 },
};

/* a method of the class itself only (one inherited is another class's, wrapped there or not at all), and of the encoding expected */
static void
wrap_own(const char *class_name, const char *selector, const char *encoding_start, IMP ours, IMP *stock)
{
	Class cls = objc_getClass(class_name);
	Method m = cls != NULL ? class_getInstanceMethod(cls, sel_registerName(selector)) : NULL;
	Class (*superclass_of)(Class) = (Class (*)(Class))dlsym(RTLD_DEFAULT, "class_getSuperclass");
	Class super = cls != NULL && superclass_of != NULL ? superclass_of(cls) : NULL;
	const char *encoding = m != NULL ? method_getTypeEncoding(m) : NULL;
	bool as_expected = encoding != NULL && (encoding_start[0] == 0 || encoding[0] == encoding_start[0] || (encoding_start[0] == '@' && encoding[0] == '^'));

	if (m == NULL || superclass_of == NULL || (super != NULL && class_getInstanceMethod(super, sel_registerName(selector)) == m) || !as_expected) {
		rwcam_say("diag: -[%s %s] not wrapped (%s%s)", class_name, selector, m == NULL ? "not there" : !as_expected ?
		    "another return type than expected: " : "inherited, or no class_getSuperclass", m != NULL && !as_expected && encoding != NULL ? encoding : "");
		return;
	}
	*stock = method_getImplementation(m);
	method_setImplementation(m, ours);
	rwcam_say("diag: -[%s %s] (%s) wrapped", class_name, selector, encoding);
}

/*
 * Run q10hm: with the dual-wide device (wide + ultra-wide) mediaserverd threw "Input can't be nil" at every start of the session, from
 * -[BWGraph connectOutput:toInput:pipelineStage:] called by -[FigCaptureStillImageSinkPipeline _buildStillImageSinkPipelineWithConfiguration:...]
 * (0x1a1f22fa4) at 0x1a1f23b8c. That builder is the one captureSession_buildGraphWithConfiguration takes for every still image scheme but 3 and 4
 * (0x1a1f14464: scheme - 3 > 1; schemes 3 and 4 get FigCaptureStillImageUnifiedBracketingSinkPipeline, which needs tuning data this model's
 * files do not have). It knows three port types only: the device's captureStream's, its bravoTelephotoCaptureStream's and its
 * pearlInfraredCaptureStream's -- the list its coordinator nodes are made with (0x1a1f23324, 0x1a1f239b0..ec) -- and then connects EVERY entry
 * of sourceOutputsByPortType to [coordinator inputForPortType:] (0x1a1f23b64): the ultra-wide stream's still image output has no such input.
 * No iPhone has an ultra-wide camera with this pipeline. Two things here, so that the dual-wide device's graph is the wide camera's as far as
 * photographs go:
 *   - a BWMultiStreamCameraSourceNode made for a stream that is none of the device's three gets no still image output: the next
 *     -[BWMultiStreamCameraSourceNodeConfiguration setStillImageOutputEnabled:YES] on the thread that made the node (the configuration is made
 *     and filled by _csp_configureMultiStreamCameraNode right after the node, 0x1a1f604c4 then 0x1a1f60d38) is made NO, as it is for the infrared
 *     camera's node;
 *   - whatever of sourceOutputsByPortType is not one of the three port types is taken out before the pipeline is built (nothing to take out
 *     when the first worked; an output left without a connection is passed over by -[BWGraph _resolveFormats:], 0x1a1f8bbd4).
 * A photograph taken while the ultra-wide camera is the one shown has no still image output to come from: not expected to work yet.
 * /var/db/nord2-camera-nostillfix: nothing of this.
 */
static IMP stock_node_init, stock_set_still, stock_still_init, stock_is_bravo, stock_connect;
static void text_of(CFTypeRef value, char *text, unsigned long size);
static pthread_t still_build_thread;
static volatile int still_building;
static int still_fix = -1;
static pthread_mutex_t still_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t still_thread;
static bool still_pending;

static bool
responds(id object, const char *selector)
{
	static Class (*class_of)(id);

	if (class_of == NULL) {
		class_of = (Class (*)(id))dlsym(RTLD_DEFAULT, "object_getClass");
	}
	return object != NULL && class_of != NULL && class_getInstanceMethod(class_of(object), sel_registerName(selector)) != NULL;
}

static bool
still_fix_on(void)
{
	if (still_fix < 0) {
		still_fix = access("/var/db/nord2-camera-nostillfix", F_OK) != 0;
	}
	return still_fix == 1;
}

static const char *const still_streams[3] = { "captureStream", "bravoTelephotoCaptureStream", "pearlInfraredCaptureStream" };

static id
node_init(id self, SEL _cmd, id device, id stream)
{
	id node = ((id (*)(id, SEL, id, id))stock_node_init)(self, _cmd, device, stream);
	bool without = false;

	if (still_fix_on() && node != NULL && stream != NULL && responds(device, still_streams[0]) && responds(device, still_streams[1]) &&
	    responds(device, still_streams[2])) {
		without = true;
		for (int i = 0; i < 3; i++) {
			without = without && (id)ask(device, still_streams[i]) != stream;
		}
	}
	pthread_mutex_lock(&still_lock);
	still_pending = without;
	still_thread = pthread_self();
	pthread_mutex_unlock(&still_lock);
	if (without) {
		say_long("still fix: a source node for a stream the still image pipeline of this model has no input for; it gets no still image output",
		    (CFTypeRef)stream, 200);
	}
	return node;
}

static void
set_still(id self, SEL _cmd, bool enabled)
{
	bool mine = false;

	if (enabled) {
		pthread_mutex_lock(&still_lock);
		if (still_pending && pthread_equal(still_thread, pthread_self())) {
			mine = true;
			still_pending = false;
		}
		pthread_mutex_unlock(&still_lock);
	}
	if (mine) {
		rwcam_say("diag: still fix: setStillImageOutputEnabled:YES for that node's configuration (%p) made NO", (void *)self);
		enabled = false;
	}
	((void (*)(id, SEL, bool))stock_set_still)(self, _cmd, enabled);
}

static id
still_init(id self, SEL _cmd, id configuration, id device, id outputs, id delegate, id scheduler, id graph, id name)
{
	if (still_fix_on() && outputs != NULL && CFGetTypeID((CFTypeRef)outputs) == CFDictionaryGetTypeID() && responds(device, still_streams[0]) &&
	    responds(device, still_streams[1]) && responds(device, still_streams[2])) {
		CFMutableDictionaryRef kept = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

		for (int i = 0; i < 3; i++) {
			id stream = (id)ask(device, still_streams[i]);
			CFTypeRef port = stream != NULL && responds(stream, "portType") ? ask(stream, "portType") : NULL;
			const void *output = port != NULL ? CFDictionaryGetValue((CFDictionaryRef)outputs, port) : NULL;

			if (output != NULL) {
				CFDictionarySetValue(kept, port, output);
			}
		}
		if (CFDictionaryGetCount(kept) != 0 && CFDictionaryGetCount(kept) < CFDictionaryGetCount((CFDictionaryRef)outputs)) {
			say_long("still fix: the still image pipeline is given the outputs of the port types it has inputs for only; all there were",
			    (CFTypeRef)outputs, 500);
			outputs = ((id (*)(id, SEL))objc_msgSend)((id)kept, sel_registerName("autorelease"));
		} else {
			CFRelease(kept);
		}
	}
	/*
	 * Run q10ho: with the ultra-wide stream's output out of the way the builder went on to its BWCrossoverNode (0x1a1f2404c), which it makes
	 * when [captureDevice isBravoVariant] (0x1a1f230c4, kept at [sp, #0xfc]; deviceType 4, 8 or 9) and neither constituent photos nor depth
	 * are asked for, and whose second input it gives [router bravoTelephotoOutput] (0x1a1f240d8): nil without a telephoto stream, "Output
	 * can't be nil". Every bravo branch of this builder means wide + telephoto ([sp, #0xfc] is read at 0x1a1f23264, 235e4, 23e30, 24028, 24534,
	 * 252c4, 25508, 257b0). While it runs -- on this thread, and only then -- the device answers NO to isBravoVariant: with that and the one
	 * port type in sourceOutputsByPortType the builder does exactly what it does for the back wide camera by itself.
	 */
	id made;

	pthread_mutex_lock(&still_lock);
	still_build_thread = pthread_self();
	still_building = still_fix_on() ? 1 : 0;
	pthread_mutex_unlock(&still_lock);
	made = ((id (*)(id, SEL, id, id, id, id, id, id, id))stock_still_init)(self, _cmd, configuration, device, outputs, delegate, scheduler, graph, name);
	pthread_mutex_lock(&still_lock);
	still_building = 0;
	pthread_mutex_unlock(&still_lock);
	rwcam_say("diag: still fix: the still image pipeline (%s) was %s", "FigCaptureStillImageSinkPipeline", made != NULL ? "built" : "NOT built (nil)");
	return made;
}

static bool
is_bravo(id self, SEL _cmd)
{
	bool stock = ((bool (*)(id, SEL))stock_is_bravo)(self, _cmd), mine = false;

	if (stock && still_building) {
		pthread_mutex_lock(&still_lock);
		mine = still_building && pthread_equal(still_build_thread, pthread_self());
		pthread_mutex_unlock(&still_lock);
	}
	if (mine) {
		rwcam_say("diag: still fix: isBravoVariant answered NO while the still image pipeline is built (the device's own answer: YES)");
		return false;
	}
	return stock;
}

/*
 * -[BWGraph connectOutput:toInput:pipelineStage:] throws when either end is nil, which takes mediaserverd down, which Camera.app answers by
 * asking again: one such connection costs a run to find (q10hk, q10hm, q10ho). In this build a connection with a nil end is said -- who asked
 * for it (the return address, of the cache before its slide), and the node and name of the end that is there -- and passed over: YES is
 * returned and the graph is built without it. A graph with a connection missing is not a working one (what is behind it gets nothing), but
 * every such connection of a start is in one log. /var/db/nord2-camera-nilconnect-throw: not wrapped, the exception as before.
 */
static bool
graph_connect(id self, SEL _cmd, id output, id input, id stage)
{
	if (output == NULL || input == NULL) {
		static const char *(*class_name)(id);
		id there = output != NULL ? output : input, node = there != NULL && responds(there, "node") ? (id)ask(there, "node") : NULL;
		char node_name[64], end_name[48];

		if (class_name == NULL) {
			class_name = (const char *(*)(id))dlsym(RTLD_DEFAULT, "object_getClassName");
		}
		text_of(node != NULL && responds(node, "name") ? ask(node, "name") : NULL, node_name, sizeof(node_name));
		text_of(there != NULL && responds(there, "name") ? ask(there, "name") : NULL, end_name, sizeof(end_name));
		rwcam_say_keep("diag: A CONNECTION WITH A NIL %s%s, asked for from %#lx (unslid): PASSED OVER, the graph is built without it. The other end: %s "
		    "'%s' of node %s '%s'", output == NULL ? "OUTPUT" : "", input == NULL ? (output == NULL ? " AND INPUT" : "INPUT") : "",
		    (unsigned long)__builtin_return_address(0) - redwood_cache_slide(), there == NULL ? "none" : output != NULL ? "output" : "input", end_name,
		    node != NULL && class_name != NULL ? class_name(node) : "?", node_name);
		return true;
	}
	return ((bool (*)(id, SEL, id, id, id))stock_connect)(self, _cmd, output, input, stage);
}

/*
 * Run q10ho, the black viewfinder: BWPreviewStitcherNode made a dropped sample ("StitchingFailure", 0x1a201f8d0) of every picture, 84 of 84,
 * and everything after it had nothing. The preview pipeline has that node when its configuration's overCaptureEnabled (+0x74) or
 * primaryCaptureRectModificationEnabled (+0x75) is set (-[FigCapturePreviewSinkPipeline _buildVideoPreviewSinkPipeline:...], 0x1a1f9b0c0..cc);
 * the second is the client's own -[FigCaptureVideoPreviewSinkConfiguration primaryCaptureRectModificationEnabled] (0x1a1f13c0c), which
 * Camera.app sets when it takes the phone for an iPhone 11 ("PrimaryCaptureRectModification:1" in its configurations; the iPhone X's app does
 * not, and in run q10hf the viewfinder had no such node and showed pictures). Without stitching the node still makes every picture anew
 * (-_newStitchedSampleBufferFromWide:..., 0x1a2022208: a buffer of its output's pool, VTPixelRotationSessionRotateSubImage into it) and
 * gives nil -- the dropped sample -- when the pool has no buffer or the rotation fails; which of the two is not known. Here the client's
 * flag reads NO in mediaserverd: no stitcher, the preview pipeline of run q10hf. (The first is _cs_shouldEnableOverCapture, which is the
 * client's spatialOverCaptureEnabled for the dual-wide device: 0 in these runs.) /var/db/nord2-camera-keepstitcher: the flag as the client set it.
 */
static IMP stock_rect_modification, stock_moment_capture;

/*
 * Run q10hz, the photograph that is never saved: the stream's StillImage pictures reached BWGNRNode (the still pipeline's noise reduction and
 * fusion, a Metal processor: GNR.videoprocessor) and nothing came out of it, with no error. What it does with each picture it is given is said
 * here (the first 40): the frame's number in the bracket as its metadata has it, how many the node expects and has, its processing type,
 * whether it has its processor at all. Nothing is changed. /var/db/nord2-camera-nognrdiag: not wrapped.
 */
static IMP stock_gnr_render, stock_gnr_prepare;

static long
gnr_ivar(id node, const char *name, int bytes)
{
	void *(*get_ivar)(void *, const char *) = (void *(*)(void *, const char *))dlsym(RTLD_DEFAULT, "class_getInstanceVariable");
	long (*offset_of)(void *) = (long (*)(void *))dlsym(RTLD_DEFAULT, "ivar_getOffset");
	void *(*class_of)(id) = (void *(*)(id))dlsym(RTLD_DEFAULT, "object_getClass");
	void *ivar = get_ivar != NULL && class_of != NULL && node != NULL ? get_ivar(class_of(node), name) : NULL;
	long at = ivar != NULL && offset_of != NULL ? offset_of(ivar) : 0;

	if (at <= 0) {
		return -1;
	}
	return bytes == 8 ? *(long *)((char *)node + at) != 0 : bytes == 1 ? *(unsigned char *)((char *)node + at) : *(int *)((char *)node + at);
}

static void
gnr_render(id self, SEL _cmd, void *sample_buffer, id input)
{
	static unsigned said;
	unsigned mine = said < 40 ? ++said : 0;

	if (mine != 0) {
		CFTypeRef (*get)(void *, CFStringRef, void *) = (CFTypeRef (*)(void *, CFStringRef, void *))dlsym(RTLD_DEFAULT, "CMGetAttachment");
		CFStringRef key = CFStringCreateWithCString(NULL, "MetadataDictionary", kCFStringEncodingUTF8);
		CFTypeRef m = get != NULL && sample_buffer != NULL ? get(sample_buffer, key, NULL) : NULL;
		bool dictionary = m != NULL && CFGetTypeID(m) == CFDictionaryGetTypeID();
		char line[300];

		CFRelease(key);
		snprintf(line, sizeof(line), "still: BWGNRNode is given picture %u: metadata %s; BracketedCaptureSequenceNumber %s, PreBracketingFrame %s, "
		    "PortType %s; the node expects %ld and has %ld, processing type %ld, processor %s, capture settings %s", mine,
		    m == NULL ? "NONE (the node returns at once)" : dictionary ? "there" : "not a dictionary", dictionary && has_key((id)m,
		    "BracketedCaptureSequenceNumber") ? "there" : "NOT THERE", dictionary && has_key((id)m, "PreBracketingFrame") ? "there" : "not there",
		    dictionary && has_key((id)m, "PortType") ? "there" : "NOT THERE", gnr_ivar(self, "_expectedFrameCount", 4),
		    gnr_ivar(self, "_receivedFrameCount", 4), gnr_ivar(self, "_processingType", 4),
		    gnr_ivar(self, "_sampleBufferProcessor", 8) == 1 ? "there" : "NOT THERE (GNR.videoprocessor could not be made: every picture ends here)",
		    gnr_ivar(self, "_currentStillImageSettings", 8) == 1 ? "kept" : "none yet");
		rwcam_say_keep("diag: %s", line);
	}
	/*
	 * Run user4: A PHOTOGRAPH OF ONE PICTURE (good light, and every photograph of the front camera) never came out: "took picture 16:
	 * it now expects 1 and has 0", nothing after it, -16800 five seconds later. With the noise reduction on, the node takes a picture
	 * only if its metadata's BracketedCaptureSequenceNumber is 1 ... the count it expects (0x1a1f18718: cmp w20, #1; b.lt -> out),
	 * also when it expects one; the stream numbers the pictures of a bracket only. Here a picture that has no number and is not the
	 * frame before a bracket is given the number 1 before the node sees it (a second one of the same request is then turned away
	 * by the node itself, by the request's ID: 0x1a1f18498). Only where the dictionary can be changed; said the first times.
	 * Not while /var/db/nord2-camera-nosinglenumber exists.
	 */
	{
		static int allowed = -1;
		static unsigned numbered;
		CFTypeRef (*get)(void *, CFStringRef, void *) = (CFTypeRef (*)(void *, CFStringRef, void *))dlsym(RTLD_DEFAULT, "CMGetAttachment");
		const char *(*class_name)(CFTypeRef) = (const char *(*)(CFTypeRef))dlsym(RTLD_DEFAULT, "object_getClassName");
		Boolean (*cf_mutable)(CFTypeRef) = (Boolean (*)(CFTypeRef))dlsym(RTLD_DEFAULT, "_CFDictionaryIsMutable");
		CFStringRef key = CFStringCreateWithCString(NULL, "MetadataDictionary", kCFStringEncodingUTF8);
		CFTypeRef m = get != NULL && sample_buffer != NULL ? get(sample_buffer, key, NULL) : NULL;

		CFRelease(key);
		if (allowed < 0) {
			allowed = access("/var/db/nord2-camera-nosinglenumber", F_OK) != 0;
		}
		if (allowed && m != NULL && CFGetTypeID(m) == CFDictionaryGetTypeID() && !has_key((id)m, "BracketedCaptureSequenceNumber") &&
		    !has_key((id)m, "PreBracketingFrame") && class_name != NULL) {
			const char *cls = class_name(m);
			bool can = cls != NULL && (strcmp(cls, "__NSDictionaryM") == 0 ||
			    (strcmp(cls, "__NSCFDictionary") == 0 && cf_mutable != NULL && cf_mutable(m)));

			if (can) {
				int one = 1;
				CFNumberRef number = CFNumberCreate(NULL, 9 /* kCFNumberIntType */, &one);
				CFStringRef name = CFStringCreateWithCString(NULL, "BracketedCaptureSequenceNumber", kCFStringEncodingUTF8);

				CFDictionarySetValue((CFMutableDictionaryRef)m, name, number);
				CFRelease(name);
				CFRelease(number);
			}
			if (numbered++ < 12) {
				rwcam_say_keep("diag: still: a picture with no number and not the frame before a bracket (metadata a %s): %s",
				    cls != NULL ? cls : "?", can ? "numbered 1 for the node" : "COULD NOT BE NUMBERED, the node will not take it");
			}
		}
	}
	((void (*)(id, SEL, void *, id))stock_gnr_render)(self, _cmd, sample_buffer, input);
	if (mine != 0) {
		rwcam_say_keep("diag: still: BWGNRNode took picture %u: it now expects %ld and has %ld (processing type %ld)", mine,
		    gnr_ivar(self, "_expectedFrameCount", 4), gnr_ivar(self, "_receivedFrameCount", 4), gnr_ivar(self, "_processingType", 4));
	}
}

/*
 * Run q10ia: the node had NO processor when the photograph's first frame came. It is made in -prepareForCurrentConfigurationToBecomeLive
 * (-_setupSampleBufferProcessor, 0x1a1f4e558: FigSampleBufferProcessorCreateForCustom(, "GNR", options,) -> dlopen of
 * /System/Library/VideoProcessors/GNR.videoprocessor, -[GNRProcessor initWithOptions:], which makes a FigMetalContext of the Metal library
 * inside it). Said here: whether the node has it after that.
 */
/*
 * Run q10ib: GNR.videoprocessor opens, its FigMetalContext is made, and -[GNRProcessor initWithOptions:] gives nil -- with not one shader of
 * its library asked of the graphics driver. Between the two it makes a DenoiseFusePipeline (0x1f206b384): four AMBNRStage (a PyramidStage and
 * a DenoiseRemixStage each), a FusionRemixStage, a BilateralGrid, a RegPyrFusion, every one nil when the shaders it asks its shared object
 * for (-getPipeline:..., -getShaders:...; FigMetalContext -computePipelineStateFor:constants:) do not come. Each of those calls is said here
 * with what it gave (all of them return an object and take pointers and whole numbers only: one function stands for them all, and hands
 * its six argument registers on as they are).
 */
static int gnr_quiet;      /* /var/db/nord2-camera-nognrdiag: only what the photograph needs is wrapped, and nothing is said */
static struct { const char *cls, *sel; IMP stock; } gnr_calls[] = {
	{ "GNRProcessor", "initWithOptions:", NULL }, { "FigMetalContext", "initWithLibraryData:ofSize:andOptionalCommandQueue:", NULL },
	{ "FigMetalContext", "computePipelineStateFor:constants:", NULL },
	{ "DenoiseFusePipeline", "initWithContext:supportsHDR:onlySingleImageDenoising:", NULL }, { "AMBNRStage", "init:onlySingleImageDenoising:", NULL },
	{ "PyramidStage", "initWithOptions:context:", NULL }, { "PyramidStageShared", "getPipeline:lumaWrite:chromaType:", NULL },
	{ "DenoiseRemixStage", "initWithContext:onlySingleImageDenoising:", NULL },
	{ "DenoiseRemixStageShared", "getShaders:lumaFP16:chromaFP16:lGAAlgorithm:", NULL }, { "FusionRemixStage", "initBandFusion:", NULL },
	{ "FusionRemixStageShared", "getShaders:fusionType:fp16:staticOIS:reduceOisGhosting:fusionAlignment:", NULL },
	{ "BilateralGrid", "initWithContext:", NULL }, { "BilateralGridShared", "getShaders:normalizeGridConfidence:", NULL },
	{ "RegPyrFusion", "initWithMetalContext:", NULL }, { "RegPyrFusionShared", "getShaders:", NULL },
	/*
	 * Run q10id-b: the processor is there and is given the frame before the bracket and the four of it; nothing comes out of the node and
	 * nothing is said. From here on (GNR_NUMBERS) the calls that say where it stops, with their first two arguments and what they gave as
	 * numbers (for a method that gives nothing the last number means nothing), and never held back by the log's limit:
	 * the processor's own steps; the reference frame's number, which the node waits for and which comes from the stream's class by way of
	 * the coordinator (-[BWFigVideoCaptureStream _handleStillImageSampleBuffer:...] 0x1a1fbab8c -> the coordinator -> a message down the
	 * graph -> -[BWGNRNode handleStillImageReferenceFrameBracketedCaptureSequenceNumber:forInput:]); and the nodes after it.
	 */
#define GNR_NUMBERS 15
	{ "GNRProcessor", "processSampleBuffer:", NULL }, { "GNRProcessor", "_perFrameProcessing:input:cfp:", NULL },
	{ "GNRProcessor", "_appendFrames:cfp:", NULL }, { "GNRProcessor", "finishPendingProcessing", NULL },
	{ "GNRProcessor", "processSequence", NULL }, { "GNRProcessor", "_multiFrameProcessing", NULL },
	{ "GNRProcessor", "_registerImages:", NULL }, { "GNRProcessor", "_fuseImages:", NULL },
	{ "GNRProcessor", "_prepareOutput:params:", NULL }, { "GNRProcessor", "onOutput:", NULL },
	{ "GNRProcessor", "setValue:forProperty:", NULL },
	{ "BWGNRNode", "handleStillImageReferenceFrameBracketedCaptureSequenceNumber:forInput:", NULL },
	{ "BWGNRNode", "_sampleBufferProcessorOutputReady:sampleBuffer:", NULL }, { "BWGNRNode", "handleNodeError:forInput:", NULL },
	{ "BWStillImageCoordinatorNode", "captureStream:didCaptureReferenceFrameWithPTS:referenceFrameBracketedCaptureSequenceNumber:", NULL },
	{ "BWStillImageCoordinatorNode", "captureStream:didCompleteStillImageCaptureWithPTS:error:", NULL },
	{ "BWStillImageCoordinatorNode", "captureStream:stillImageCaptureError:", NULL },
	{ "BWStillImageTurnstileNode", "renderSampleBuffer:forInput:", NULL }, { "BWStillImageScalerNode", "renderSampleBuffer:forInput:", NULL },
	{ "BWMeteorHeadroomNode", "renderSampleBuffer:forInput:", NULL }, { "BWPhotoEncoderNode", "renderSampleBuffer:forInput:", NULL },
	{ "BWPhotoEncoderNode", "handleNodeError:forInput:", NULL },
};

/*
 * Run q10ic: THE FIRST NIL IS -[PyramidStage initWithOptions:context:], before it asks for one shader. At 0x1f20658dc it makes a
 * FigM2MController ([[FigM2MController alloc] init], CMCapture 0x1c789c3b8), which is IOSurfaceAcceleratorCreate() -- Apple's hardware scaler,
 * AppleM2ScalerCSCDriver, which this phone has not got ("matching AppleM2ScalerCSCDriver -> none" in every log) -- and gives up when that is
 * nil (cbz at 0x1f20658f4). The stage only USES the controller in -runM2MWithFilters:, which -runWithFilters: (0x1f206796c) takes when
 * PyrGen.UseHW is 1; -[AMBNRStage init:onlySingleImageDenoising:] passes PyrGen.UseHW = 0 (0x1f20649f8: the constant number 0), so the
 * pyramid is made on the GPU (-runGPUWithFilters:doShift:) and the controller is never sent a message. So: while a PyramidStage is being
 * made ON THIS THREAD, a FigM2MController whose -init failed is given back as it is, with nothing in it (its -dealloc, 0x1c789c73c, releases
 * only what is not NULL). Everywhere else -init answers as it always did. Not while /var/db/nord2-camera-nom2mstub exists.
 */
static IMP stock_m2m_init;
static pthread_t m2m_thread;
static int m2m_wanted;

static id
m2m_init(id self, SEL _cmd)
{
	static int said;
	size_t (*size_of)(Class) = (size_t (*)(Class))dlsym(RTLD_DEFAULT, "class_getInstanceSize");
	Class (*class_of)(id) = (Class (*)(id))dlsym(RTLD_DEFAULT, "object_getClass");
	id made;

	if (!m2m_wanted || !pthread_equal(m2m_thread, pthread_self()) || size_of == NULL || class_of == NULL) {
		return ((id (*)(id, SEL))stock_m2m_init)(self, _cmd);
	}
	CFRetain((CFTypeRef)self);        /* (a failed -init releases the object it was given: this reference is the one handed back) */
	made = ((id (*)(id, SEL))stock_m2m_init)(self, _cmd);
	if (made != NULL) {
		CFRelease((CFTypeRef)self);
		return made;
	}
	memset((char *)self + sizeof(void *), 0, size_of(class_of(self)) - sizeof(void *));       /* (what it released is not released again) */
	if (said++ < 4) {
		rwcam_say("diag: still: FigM2MController has no hardware scaler: an empty one for the PyramidStage, which makes its pyramid on the GPU");
	}
	return self;
}

static id
gnr_call(id self, SEL _cmd, long a, long b, long c, long d, long e, long f)
{
	static unsigned said;
	Class (*class_of)(id) = (Class (*)(id))dlsym(RTLD_DEFAULT, "object_getClass");
	Class mine = class_of != NULL ? class_of(self) : NULL;

	/* (the entry of this object's class and selector; failing that -- a subclass -- the selector's alone) */
	for (unsigned pass = 0, i = 0; pass < 2; i = i + 1 < sizeof(gnr_calls) / sizeof(gnr_calls[0]) ? i + 1 : (pass++, 0)) {
		if (gnr_calls[i].stock != NULL && sel_registerName(gnr_calls[i].sel) == _cmd && (pass == 1 || objc_getClass(gnr_calls[i].cls) == mine)) {
			pthread_t before = m2m_thread;
			int wanted = m2m_wanted;
			id made;

			if (i == 5 && stock_m2m_init != NULL) {      /* PyramidStage: see m2m_init */
				m2m_thread = pthread_self();
				m2m_wanted = 1;
			}
			made = ((id (*)(id, SEL, long, long, long, long, long, long))gnr_calls[i].stock)(self, _cmd, a, b, c, d, e, f);
			if (i == 5 && stock_m2m_init != NULL) {
				m2m_wanted = wanted;
				m2m_thread = before;
			}
			if (i >= GNR_NUMBERS) {
				static unsigned numbers;

				if (numbers++ < 400 && !gnr_quiet) {
					rwcam_say_keep("diag: still: -[%s %s] (%ld, %ld) gave %ld", gnr_calls[i].cls, gnr_calls[i].sel, a, c, (long)(int)(long)made);
				}
			} else if (said < 90 && !gnr_quiet) {
				char name[64] = "";

				said++;
				if (i == 2 && a != 0 && CFGetTypeID((CFTypeRef)a) == CFStringGetTypeID()) {
					CFStringGetCString((CFStringRef)a, name, sizeof(name), kCFStringEncodingUTF8);
				}
				rwcam_say("diag: still: -[%s %s] %s gave %s", gnr_calls[i].cls, gnr_calls[i].sel, name, made != NULL ? "an object" : "NIL");
			}
			return made;
		}
	}
	return NULL;    /* (not reached: a method is only wrapped with its own entry) */
}

/*
 * Run q10ie: -[GNRProcessor processSequence] gives -1 with no fusion tried: the processor's kept error (+0x40) was set while a frame was
 * taken, and the graphics driver says "surface N is 'L00h' and Metal format 10 is 'A008'" inside those very calls (4 times with the first
 * frame, 8 with the last). Every texture the processor makes of a pixel buffer or a surface goes through FigMetalContext: said here for
 * each one that comes back nil, with what was asked and WHO asked (the caller's address in the shared cache).
 */
static struct { const char *sel; IMP stock; } bind_calls[] = {
	{ "bindPixelBufferToMTL2DTexture:pixelFormat:usage:plane:", NULL }, { "bindPixelBufferToMTL2DTexture:pixelFormat:usage:plane:alignmentFactor:", NULL },
	{ "bindPixelBufferToMTL2DTexture:pixelFormat:usage:plane:widthAlignmentFactor:heightAlignmentFactor:", NULL },
	{ "bindPixelBufferToMTL2DTexture:pixelFormat:usage:textureSize:plane:", NULL },
	{ "bindIOSurfaceToMTL2DTexture:pixelFormat:usage:width:height:plane:", NULL }, { "rebindTex:format:usage:plane:xFactor:", NULL },
	{ "create2DTextureFromBuffer:offset:width:height:format:usage:", NULL }, { "create2DTextureFromBuffer:offset:stride:width:height:format:usage:", NULL },
	{ "bindPixelBufferToMTLBuffer:", NULL },
};

/* (g, h: the two arguments the longest of these methods takes on the stack -- ...stride:width:height:format:usage: has seven. Run q10if
 * handed on the six in registers only, the usage arrived as whatever was on the stack and the driver refused the texture for it: that
 * run's nil textures were this function's own doing.) */
static id
bind_call(id self, SEL _cmd, long a, long b, long c, long d, long e, long f, long g, long h)
{
	static unsigned said;
	unsigned long from = (unsigned long)__builtin_return_address(0) - redwood_cache_slide();

	for (unsigned i = 0; i < sizeof(bind_calls) / sizeof(bind_calls[0]); i++) {
		if (bind_calls[i].stock != NULL && sel_registerName(bind_calls[i].sel) == _cmd) {
			id made = ((id (*)(id, SEL, long, long, long, long, long, long, long, long))bind_calls[i].stock)(self, _cmd, a, b, c, d, e, f, g, h);

			if (made == NULL && said < 60) {
				unsigned (*type_of)(void *) = (unsigned (*)(void *))dlsym(RTLD_DEFAULT, "CVPixelBufferGetPixelFormatType");
				CFTypeID (*buffer_id)(void) = (CFTypeID (*)(void))dlsym(RTLD_DEFAULT, "CVPixelBufferGetTypeID");
				size_t (*width_of)(void *) = (size_t (*)(void *))dlsym(RTLD_DEFAULT, "CVPixelBufferGetWidth");
				size_t (*height_of)(void *) = (size_t (*)(void *))dlsym(RTLD_DEFAULT, "CVPixelBufferGetHeight");
				bool buffer = i < 4 && a != 0 && type_of != NULL && buffer_id != NULL && width_of != NULL && height_of != NULL &&
				    CFGetTypeID((CFTypeRef)a) == buffer_id();
				unsigned type = buffer ? type_of((void *)a) : 0x3f3f3f3f;

				said++;
				rwcam_say_keep("diag: still: -[FigMetalContext %s] gave NIL: first argument %s '%c%c%c%c' %ldx%ld, the others %ld %ld %ld %ld %ld; "
				    "asked from cache 0x%lx", bind_calls[i].sel, buffer ? "a pixel buffer" : "no pixel buffer", (char)(type >> 24), (char)(type >> 16),
				    (char)(type >> 8), (char)type, buffer ? (long)width_of((void *)a) : 0, buffer ? (long)height_of((void *)a) : 0, b, c, d, e, f, from);
			}
			return made;
		}
	}
	return NULL;
}

/*
 * Runs q10id-b (portrait) and user3: mediaserverd ended on an exception nobody catches, "captureStreamSettings must have at least 1
 * element", thrown by -[BWStillImageCaptureSettings initWithSettingsID:...captureStreamSettings:] when
 * -[BWFigVideoCaptureDevice _stillImageCaptureSettingsWithID:...] (0x1a2197b00) finds no stream of the device to take the photograph
 * with -- on this phone a request that names only a stream whose still image output the still fix above took away (the ultra-wide
 * one of the pair). The camera then restarts and the photograph is lost with everything else. Here: an empty list is given ONE
 * stream's settings, the back wide camera's (the stream that does have a still image output), with the request's own type and flags
 * and nothing else, and it is said; the photograph is then the wide camera's. Not while /var/db/nord2-camera-emptysettings-throw exists.
 */
static IMP stock_settings_init;

static id
settings_init(id self, SEL _cmd, long long settings_id, int capture_type, unsigned long long capture_flags, unsigned long long scene_flags,
    id statistics, bool original, bool sushi, id streams)
{
	static unsigned said;
	long count = streams != NULL ? ((long (*)(id, SEL))objc_msgSend)(streams, sel_registerName("count")) : 0;

	if (count == 0) {
		CFStringRef *port = (CFStringRef *)dlsym(RTLD_DEFAULT, "kFigCapturePortType_BackFacingCamera");
		Class one = objc_getClass("BWStillImageCaptureStreamSettings"), array = objc_getClass("NSArray");
		id made = NULL;

		if (port != NULL && *port != NULL && one != NULL && array != NULL) {
			made = ((id (*)(Class, SEL))objc_msgSend)(one, sel_registerName("alloc"));
			made = ((id (*)(id, SEL, id, int, unsigned long long, int, id, id))objc_msgSend)(made,
			    sel_registerName("initWithPortType:captureType:captureFlags:timeMachineFrameCount:bracketSettings:validBracketedCaptureSequenceNumbers:"),
			    (id)*port, capture_type, capture_flags, 0, NULL, NULL);
		}
		if (made != NULL) {
			streams = ((id (*)(Class, SEL, id))objc_msgSend)(array, sel_registerName("arrayWithObject:"), made);
			CFRelease((CFTypeRef)made);
		}
		if (said++ < 20) {
			rwcam_say_keep("diag: still: the capture settings (type %d, flags 0x%llx) name NO stream to take the photograph with: %s",
			    capture_type, capture_flags, made != NULL ? "the back wide camera's is put there (this was an exception that ended mediaserverd)" :
			    "and none could be made: the exception follows");
		}
	}
	return ((id (*)(id, SEL, long long, int, unsigned long long, unsigned long long, id, bool, bool, id))stock_settings_init)(self, _cmd,
	    settings_id, capture_type, capture_flags, scene_flags, statistics, original, sushi, streams);
}

static void
gnr_prepare(id self, SEL _cmd)
{
	static int probed;
	void *(*open_it)(const char *, int) = (void *(*)(const char *, int))dlsym(RTLD_DEFAULT, "dlopen");

	if (!probed && open_it != NULL) {
		/* (the two classes are there only once the processor's image is: loaded here, then they can be wrapped) */
		probed = 1;
		rwcam_say("diag: still: GNR.videoprocessor %s", open_it("/System/Library/VideoProcessors/GNR.videoprocessor", 2) != NULL ? "opens" :
		    "DOES NOT OPEN (no processor can be made)");
		for (unsigned i = 0; i < sizeof(gnr_calls) / sizeof(gnr_calls[0]); i++) {
			if (!gnr_quiet || i == 5) {
				wrap_own(gnr_calls[i].cls, gnr_calls[i].sel, i < GNR_NUMBERS ? "@" : "", (IMP)gnr_call, &gnr_calls[i].stock);
			}
		}
		for (unsigned i = 0; !gnr_quiet && i < sizeof(bind_calls) / sizeof(bind_calls[0]); i++) {
			wrap_own("FigMetalContext", bind_calls[i].sel, "@", (IMP)bind_call, &bind_calls[i].stock);
		}
		if (access("/var/db/nord2-camera-nom2mstub", F_OK) != 0) {
			wrap_own("FigM2MController", "init", "@", (IMP)m2m_init, &stock_m2m_init);
		}
	}
	((void (*)(id, SEL))stock_gnr_prepare)(self, _cmd);
	rwcam_say("diag: still: BWGNRNode %s", gnr_ivar(self, "_sampleBufferProcessor", 8) == 1 ? "has its processor" : "has NO processor");
}

/*
 * Run q10hp, the viewfinder still black: without the stitcher the 'Preview Scaler' (BWPixelTransferNode) dropped every picture,
 * "PixelTransferFailure", its rotation session's TransferImage giving -12914 (kVTPixelRotationNotSupportedErr). In run q10hf the same node
 * turned 1504x1128 into 1128x1504 without error; now its input is 1654x1240, a tenth more each way: a rotation AND a change of size, which
 * VideoToolbox does with its Metal function (VTMTSComputeFunction, refused by this phone's graphics library at every picture, in q10hf too)
 * and not otherwise. The tenth is the video stabilization overscan that captureSession_buildGraphWithConfiguration gives the preview pipeline
 * and the source when the client's -[FigCaptureIrisSinkConfiguration momentCaptureMovieRecordingEnabled] is set (0x1a1f13c24: 0.1 or 0;
 * 0x1a1f96818, 0x1a1f5fe84) -- the video taken by holding the shutter in Photo mode, which Camera.app asks for as an iPhone 11. Here the flag
 * reads NO in mediaserverd: no overscan, the sizes of run q10hf, a rotation alone. /var/db/nord2-camera-keepquicktake: as the client set it.
 */
/*
 * Run q10hs: the front camera's viewfinder is seen; the dual-wide device's is not -- its PrimaryScaler is still 1654x1240 and the 'Preview
 * Scaler' still fails (-12914). The tenth here is BWMultiStreamCameraSourceNodeConfiguration's overscanForBravoShiftMitigation, 0.1 unless
 * told otherwise (_csp_configureMultiStreamCameraNode, 0x1a1fb48ec..4998), given when -[FigCaptureSourceConfiguration
 * bravoShiftMitigationEnabled] (0x1a1fd2660): the source's DeviceType is 4, 8 or 9 and depth is not asked for (or spatial over capture is).
 * It is the margin the picture is moved in while the device goes from one lens to the other; the zoom the stream is given is divided by 1.1
 * for it (1.818 at "1x"). Here it reads NO: no margin, the zoom as it is, a jump where an iPhone glides.
 * /var/db/nord2-camera-keepbravoshift: as CMCapture has it.
 */
/*
 * Run q10ij (a film): BWVISNode 'Tail 0 VIS Pipeline Stabilizer (ISP-strip)' drops every picture but two (VideoStabilizationFailure, "VIS Processor
 * ready callback" -12783): it wants the ISP's motion data with each frame, which nothing here makes. The connection's stabilization method
 * (-[FigVideoCaptureConnectionConfiguration videoStabilizationMethod], 0x1a1ee9e78: what +videoStabilizationMethods:includeIris: collects and the
 * tail pipelines are built by) reads 0 in this process: no VIS node, no overscan asked of the stream. /var/db/nord2-camera-keepvis: as asked.
 */
static IMP stock_vis_method, stock_unused;

static int
vis_method(id self, SEL _cmd)
{
	static unsigned said;
	int stock = ((int (*)(id, SEL))stock_vis_method)(self, _cmd);

	if (stock != 0 && said < 3) {
		said++;
		rwcam_say("diag: film: videoStabilizationMethod %d answered 0", stock);
	}
	return 0;
}

/*
 * Run user3. Two answers of CMCapture's changed in this process:
 *  - -[FigCaptureSourceVideoFormat isZeroShutterLagSupported] (0x1a1fceaec; _FigCaptureZeroShutterLagEnabled 0x1a1fc1d70) reads NO for the
 *    front camera's photo format (3088 wide): its photograph was asked with TimeMachineCaptureParams -- a frame out of the firmware's ring
 *    of past frames, which is not kept here -- and the coordinator let it time out (-16800). Without it a photograph is the frames after
 *    the shutter, as the back camera's are. /var/db/nord2-camera-keepzsl: as it was.
 *  - only with /var/db/nord2-camera-noiris: -[FigCaptureIrisSinkConfiguration irisMovieCaptureEnabled] (0x1a1eecf74) reads NO -- no Live
 *    Photo pipeline (BWIrisStagingNode keeps seconds of the viewfinder's pictures staged; mediaserverd held 450 to 548 surfaces, 1.2 to
 *    1.4 GB). Not by default: what the session does with a photograph whose settings still ask for the movie has not been seen.
 */
static IMP stock_zsl;

static bool
zsl_supported(id self, SEL _cmd)
{
	return responds(self, "dimensions") && (int)((long (*)(id, SEL))objc_msgSend)(self, sel_registerName("dimensions")) == 3088 ? false :
	    ((bool (*)(id, SEL))stock_zsl)(self, _cmd);
}

static bool
answer_no(id self, SEL _cmd)
{
	(void)self;
	(void)_cmd;
	return false;
}

static IMP stock_bravo_shift;

static bool
bravo_shift(id self, SEL _cmd)
{
	static unsigned said;
	bool stock = ((bool (*)(id, SEL))stock_bravo_shift)(self, _cmd);

	if (stock && said < 3) {
		said++;
		rwcam_say("diag: preview: bravoShiftMitigationEnabled answered NO (CMCapture's own answer: YES): no overscan on the dual-wide device's pictures");
	}
	return false;
}

static bool
moment_capture(id self, SEL _cmd)
{
	static unsigned said;
	bool asked = ((bool (*)(id, SEL))stock_moment_capture)(self, _cmd);

	if (asked && said < 3) {
		said++;
		rwcam_say("diag: preview: momentCaptureMovieRecordingEnabled answered NO (the client set it: YES): no stabilization overscan on the viewfinder's pictures");
	}
	return false;
}

static bool
rect_modification(id self, SEL _cmd)
{
	static unsigned said;
	bool asked = ((bool (*)(id, SEL))stock_rect_modification)(self, _cmd);

	if (asked && said < 3) {
		said++;
		rwcam_say("diag: preview: primaryCaptureRectModificationEnabled answered NO (the client set it: YES): no BWPreviewStitcherNode in the preview pipeline");
	}
	return false;
}

/*
 * Run q10hm, the front camera: 30 pictures a second given to the graph's handler (mean luma 68 to 92 of 255), BWIrisStagingNode counted 30 a
 * second, and the viewfinder's BWImageQueueSinkNode said "# frames received: 0" when the session stopped. Somewhere between the source node and
 * that sink the pictures are lost, and nothing says where. Here every output of every node of the graph is counted: sample buffers emitted,
 * dropped samples, node errors; said 5 s after the graph starts, every minute, and when it stops (one line for each output that had
 * anything). The first dropped samples and node errors of an output are said as they come. /var/db/nord2-camera-noflow: nothing of this.
 */
#define FLOW_OUTPUTS 192
struct flow {
	id output;
	uint64_t emitted, dropped, errors;
	char who[100];
};
static struct flow flows[FLOW_OUTPUTS];
static unsigned flow_used;
static uint64_t flow_started, flow_said, flow_lost;
static pthread_mutex_t flow_lock = PTHREAD_MUTEX_INITIALIZER;
static IMP stock_emit, stock_emit_dropped, stock_emit_error, stock_graph_start, stock_graph_stop;
extern uint64_t rwcam_now_ms(void);

static void
text_of(CFTypeRef value, char *text, unsigned long size)
{
	text[0] = 0;
	if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID()) {
		CFStringGetCString((CFStringRef)value, text, (CFIndex)size, kCFStringEncodingUTF8);
	}
}

static void
flow_dump(const char *when)
{
	static struct flow copy[FLOW_OUTPUTS];          /* (one dump at a time: under dump_lock) */
	static pthread_mutex_t dump_lock = PTHREAD_MUTEX_INITIALIZER;
	unsigned n;
	uint64_t lost;

	pthread_mutex_lock(&dump_lock);
	pthread_mutex_lock(&flow_lock);
	n = flow_used;
	lost = flow_lost;
	memcpy(copy, flows, n * sizeof(copy[0]));
	pthread_mutex_unlock(&flow_lock);
	rwcam_say_keep("diag: flow %s: %u outputs of the graph's nodes have emitted something%s", when, n, lost ? " (and more than are counted here)" : "");
	for (unsigned i = 0; i < n; i++) {
		rwcam_say_keep("diag: flow:   %s: %llu sample buffers, %llu dropped samples, %llu node errors", copy[i].who, (unsigned long long)copy[i].emitted,
		    (unsigned long long)copy[i].dropped, (unsigned long long)copy[i].errors);
	}
	pthread_mutex_unlock(&dump_lock);
}

/* what: 0 a sample buffer, 1 a dropped sample, 2 a node error; the output's count of that kind after this one (0: not counted) */
static uint64_t
flow_count(id output, int what, char *who, unsigned long who_size)
{
	struct flow *f = NULL;
	uint64_t count = 0, now = rwcam_now_ms();
	bool dump = false;

	pthread_mutex_lock(&flow_lock);
	for (unsigned i = 0; i < flow_used; i++) {
		if (flows[i].output == output) {
			f = &flows[i];
			break;
		}
	}
	if (f == NULL && flow_used < FLOW_OUTPUTS) {
		char node_name[48], output_name[32];
		const char *(*class_name)(id) = (const char *(*)(id))dlsym(RTLD_DEFAULT, "object_getClassName");
		id node = responds(output, "node") ? (id)ask(output, "node") : NULL;

		f = &flows[flow_used++];
		memset(f, 0, sizeof(*f));
		f->output = output;
		text_of(node != NULL && responds(node, "name") ? ask(node, "name") : NULL, node_name, sizeof(node_name));
		text_of(responds(output, "name") ? ask(output, "name") : NULL, output_name, sizeof(output_name));
		snprintf(f->who, sizeof(f->who), "%.40s '%s' output '%s'", node != NULL && class_name != NULL ? class_name(node) : "?", node_name, output_name);
	} else if (f == NULL) {
		flow_lost++;
	}
	if (f != NULL) {
		count = what == 0 ? ++f->emitted : what == 1 ? ++f->dropped : ++f->errors;
		if (who != NULL) {
			strlcpy(who, f->who, who_size);
		}
	}
	if (flow_started != 0 && now - flow_started >= (flow_said == 0 ? 5000u : 60000u * flow_said)) {
		flow_said++;
		dump = true;
	}
	pthread_mutex_unlock(&flow_lock);
	if (dump) {
		flow_dump("so far");
	}
	return count;
}

static void
out_emit(id self, SEL _cmd, void *sample_buffer)
{
	(void)flow_count(self, 0, NULL, 0);
	((void (*)(id, SEL, void *))stock_emit)(self, _cmd, sample_buffer);
}

static void
out_emit_dropped(id self, SEL _cmd, id dropped)
{
	char who[100];

	if (flow_count(self, 1, who, sizeof(who)) <= 3) {
		char what[160];

		struct { int64_t value; int32_t timescale; uint32_t flags; int64_t epoch; } pts = { 0, 0, 0, 0 };
		char reason[80];

		text_of(responds(dropped, "reason") ? ask(dropped, "reason") : NULL, reason, sizeof(reason));
		if (responds(dropped, "pts")) {
			pts = ((__typeof__(pts) (*)(id, SEL))objc_msgSend)(dropped, sel_registerName("pts"));
		}
		snprintf(what, sizeof(what), "flow: a DROPPED SAMPLE from %s, reason '%s', time %.4f s", who, reason,
		    pts.timescale != 0 ? (double)pts.value / pts.timescale : 0.0);
		say_long(what, (CFTypeRef)dropped, 120);
	}
	((void (*)(id, SEL, id))stock_emit_dropped)(self, _cmd, dropped);
}

static void
out_emit_error(id self, SEL _cmd, id error)
{
	char who[100];

	if (flow_count(self, 2, who, sizeof(who)) <= 6) {
		char what[160];

		snprintf(what, sizeof(what), "flow: a NODE ERROR from %s", who);
		say_long(what, (CFTypeRef)error, 280);
	}
	((void (*)(id, SEL, id))stock_emit_error)(self, _cmd, error);
}

static uintptr_t
graph_start(id self, SEL sel, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
	pthread_mutex_lock(&flow_lock);
	flow_used = 0;
	flow_lost = 0;
	flow_said = 0;
	flow_started = rwcam_now_ms();
	pthread_mutex_unlock(&flow_lock);
	return ((generic_imp)stock_graph_start)(self, sel, a, b, c, d);
}

static uintptr_t
graph_stop(id self, SEL sel, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
	uintptr_t r = ((generic_imp)stock_graph_stop)(self, sel, a, b, c, d);

	flow_dump("when the graph was stopped");
	pthread_mutex_lock(&flow_lock);
	flow_started = 0;
	pthread_mutex_unlock(&flow_lock);
	return r;
}

/*
 * Run q10ig (video mode, AVCaptureSessionPresetHigh, 420v 1920x1080 30-30): the session's start ends with -12780 right after the wide camera's
 * source node is made, nothing asked of its stream. That is -[BWMultiStreamCameraSourceNode configure:] (0x1a1fb3130, the last thing
 * _csp_configureMultiStreamCameraNode does), which gives -12780 from: _updateFormatIndex (0x1a1f9e8c8: the configuration's format index is not
 * one of the stream's formats), _updateOutputIDMappingsForStreamingOutputs (0x1a1f84248, 0x1a1f84250: a viewfinder and a film at once and no
 * scaler the film's pictures can come from -- its pixel format among the output's, video stabilization only from the PrimaryScaler, a
 * scaler said not to be there), _updateOutputRequirements (0x1a1f83d94: a raw still output asked of a format that has none) and
 * _outputConfigurationsForAllOutputs (0x1a1fc9700: scaler chaining). Which one cannot be read off the code: what the node was given and what
 * it chose is said here at each configure (a few at a session's start). Nothing is changed.
 */
/* (the probe, node_configure, said what the node was given at each configure in runs q10ii and q10ij; taken out for room in run user1) */

void
rwcam_diag_hooks(void)
{
	if (access("/var/db/nord2-camera-nostillfix", F_OK) != 0) {
		wrap_own("BWMultiStreamCameraSourceNode", "initWithCaptureDevice:captureStream:", "@", (IMP)node_init, &stock_node_init);
		wrap_own("BWMultiStreamCameraSourceNodeConfiguration", "setStillImageOutputEnabled:", "v", (IMP)set_still, &stock_set_still);
		wrap_own("FigCaptureStillImageSinkPipeline", "initWithConfiguration:captureDevice:sourceOutputsByPortType:captureStatusDelegate:inferenceScheduler:graph:name:",
		    "@", (IMP)still_init, &stock_still_init);
		if (stock_still_init != NULL) {
			wrap_own("BWFigVideoCaptureDevice", "isBravoVariant", "", (IMP)is_bravo, &stock_is_bravo);
		}
	}
	if (access("/var/db/nord2-camera-nilconnect-throw", F_OK) != 0) {
		wrap_own("BWGraph", "connectOutput:toInput:pipelineStage:", "", (IMP)graph_connect, &stock_connect);
	}
	gnr_quiet = access("/var/db/nord2-camera-nognrdiag", F_OK) == 0;
	wrap_own("BWGNRNode", "renderSampleBuffer:forInput:", "v", (IMP)gnr_render, &stock_gnr_render);      /* (also the single picture's number) */
	wrap_own("BWGNRNode", "prepareForCurrentConfigurationToBecomeLive", "v", (IMP)gnr_prepare, &stock_gnr_prepare);      /* (also m2m_init) */
	if (access("/var/db/nord2-camera-emptysettings-throw", F_OK) != 0) {
		wrap_own("BWStillImageCaptureSettings",
		    "initWithSettingsID:captureType:captureFlags:sceneFlags:frameStatisticsByPortType:deliverOriginalImage:deliverSushiRaw:captureStreamSettings:",
		    "@", (IMP)settings_init, &stock_settings_init);
	}
	if (access("/var/db/nord2-camera-keepstitcher", F_OK) != 0) {
		wrap_own("FigCaptureVideoPreviewSinkConfiguration", "primaryCaptureRectModificationEnabled", "", (IMP)rect_modification, &stock_rect_modification);
	}
	if (access("/var/db/nord2-camera-noiris", F_OK) == 0) {
		wrap_own("FigCaptureIrisSinkConfiguration", "irisMovieCaptureEnabled", "", (IMP)answer_no, &stock_unused);
	}
	if (access("/var/db/nord2-camera-keepzsl", F_OK) != 0) {
		wrap_own("FigCaptureSourceVideoFormat", "isZeroShutterLagSupported", "", (IMP)zsl_supported, &stock_zsl);
	}
	if (access("/var/db/nord2-camera-keepvis", F_OK) != 0) {
		wrap_own("FigVideoCaptureConnectionConfiguration", "videoStabilizationMethod", "i", (IMP)vis_method, &stock_vis_method);
	}
	if (access("/var/db/nord2-camera-keepbravoshift", F_OK) != 0) {
		wrap_own("FigCaptureSourceConfiguration", "bravoShiftMitigationEnabled", "", (IMP)bravo_shift, &stock_bravo_shift);
	}
	if (access("/var/db/nord2-camera-keepquicktake", F_OK) != 0) {
		wrap_own("FigCaptureIrisSinkConfiguration", "momentCaptureMovieRecordingEnabled", "", (IMP)moment_capture, &stock_moment_capture);
	}
	if (access("/var/db/nord2-camera-noflow", F_OK) != 0) {
		wrap_own("BWNodeOutput", "emitSampleBuffer:", "v", (IMP)out_emit, &stock_emit);
		wrap_own("BWNodeOutput", "emitDroppedSample:", "v", (IMP)out_emit_dropped, &stock_emit_dropped);
		wrap_own("BWNodeOutput", "emitNodeError:", "v", (IMP)out_emit_error, &stock_emit_error);
		wrap_own("BWGraph", "start:", "", (IMP)graph_start, &stock_graph_start);
		wrap_own("BWGraph", "stop:", "", (IMP)graph_stop, &stock_graph_stop);
	}
	if (access("/var/db/nord2-camera-nopropdiag", F_OK) != 0) {
		for (int i = 0; i < BW_CLASSES; i++) {
			wrap_own(bw_class[i], "getProperty:error:", "@", bw_ours[i][0], &stock_bw_get[i]);
			wrap_own(bw_class[i], "copyProperty:error:", "@", bw_ours[i][1], &stock_bw_copy[i]);
			wrap_own(bw_class[i], "setProperty:value:", "i", bw_ours[i][2], &stock_bw_set[i]);
		}
	}
	wrap("FigCaptureBaseStillImageSinkPipelineConfiguration", "setNoiseReductionAndFusionScheme:", (IMP)set_scheme, &stock_set_scheme);
	wrap("BWSISNode", "initWithCameraTuningDictionary:sensorIDDictionary:fusionScheme:", (IMP)sis_init, &stock_sis_init);
	if (access("/var/db/nord2-camera-notap", F_OK) != 0) {
		wrap("BWFigVideoCaptureDevice", "setFocusModeAutoWithRect:restrictToRect:continuous:smooth:rangeRestrictionNear:rangeRestrictionFar:isFocusRectInOverscanSpace:",
		    (IMP)focus_rect, &stock_focus_rect);
		wrap("BWFigVideoCaptureDevice", "setContinuousAutoFocusRect:", (IMP)caf_rect, &stock_caf_rect);
		wrap("BWFigVideoCaptureDevice", "setExposureModeAutoWithRect:", (IMP)exposure_rect, &stock_exposure_rect);
		wrap("BWFigVideoCaptureDevice", "setExposureModeLocked", (IMP)exposure_locked, &stock_exposure_locked);
	}
	log_hook_install();
	exception_hook_install();
	wrap("FigCaptureSessionConfiguration", "initWithXPCEncoding:", (IMP)config_init, &stock_config_init);
	wrap("BWFigCaptureDeviceVendor", "copyStreamsWithPositions:deviceTypes:forDevice:error:", (IMP)copy_streams, &stock_copy_streams);
	wrap("BWFigCaptureDeviceVendor", "copyDeviceForClient:informClientWhenDeviceAvailableAgain:error:", (IMP)copy_device, &stock_copy_device);
	wrap("FigCaptureSessionParsedConfiguration", "initWithSessionConfiguration:clientSetsUserInitiatedCaptureRequestTime:restrictions:", (IMP)parsed_init, &stock_parsed);
}
