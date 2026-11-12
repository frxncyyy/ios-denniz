/*
 * Host test of src/rwcam_camui.c (the dylib in the Camera process) against a made-up Objective-C runtime: a "CAMCaptureCapabilities" whose ivars are
 * looked up by name, and a stock -initWithHostProcess: that fills them as CameraUI 16.7 does for an iPhone 11 (N104) or an iPhone X (D22). Checks:
 * nothing is touched without an ultra-wide camera or in another program; with one, single cameras are forced, what the iPhone 11 adds to the capture
 * session is off again, the new viewfinder is suppressed the way the stock init suppresses it; each marker leaves its part alone; an ivar this CameraUI
 * does not have is skipped. Built with the dylib's own declarations (rwcam_abi.h), so nothing of the host's libc headers is included here.
 */
#include "rwcam_abi.h"
#include "rwcam_log.h"

int printf(const char *, ...);
int vprintf(const char *, __builtin_va_list);
char *getenv(const char *);
char *strstr(const char *, const char *);

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures;
static char last_say[1600];

void rwcam_say(const char *format, ...)
{
	__builtin_va_list ap;

	unsigned long used = 0;

	while (last_say[used] != 0) {
		used++;         /* (everything said since make(): the lines one after the other) */
	}
	__builtin_va_start(ap, format);
	vsnprintf(last_say + used, sizeof(last_say) - used, format, ap);
	__builtin_va_end(ap);
	if (getenv("VERBOSE")) {
		printf("   | %s\n", last_say + used);
	}
}
void rwcam_say_once(const char *what, const char *name, const char *detail) { (void)what; (void)name; (void)detail; }
void rwcam_say_keep(const char *format, ...) { (void)format; }
unsigned long redwood_cache_slide(void) { return 0; }

/* ---- the made-up runtime ---- */
struct caps {
	void *isa;
	uint8_t b[64];
	double over, shown;
};
static const char *const bool_names[] = {
	"_backSuperWideSupported", "_backWideDualSupported", "_featureDevelopmentForceSingleCamera", "_responsiveShutterSupported", "_shotBufferSupported",
	"_backSpatialOverCaptureSupported", "_frontSpatialOverCaptureSupported", "_backLowLightSupported", "_frontLowLightSupported",
	"_autoLowLightVideoSupported", "_deferredProcessingSupported", "_deepFusionSupported", "_deferredPortraitRenderingSupported",
	"_contentAwareDistortionCorrectionSupported", "_stereoAudioRecordingSupported", "_neuralEngineSupported", "_ctmSupported",
	"_useCTMTransientImageWell", "_useCTMModeSelector", "_allowControlDrawer", "_allowDynamicShutterZoom",
	"_interactiveVideoFormatControlAlwaysEnabled", "_exposureSliderSupported", "_ctmSupportSuppressed", "_backLivePhotoSupported", "_backHDRSupported",
};
#define NBOOLS ((int)(sizeof(bool_names) / sizeof(bool_names[0])))
static const char *missing;             /* an ivar this "CameraUI" does not have */
static const char *program = "Camera";
static const char *markers[4];
static int model;                       /* 0: D22, 1: N104 */
static IMP installed;
static int stock_calls, lookups;

static int index_of(const char *name)
{
	for (int i = 0; i < NBOOLS; i++) {
		if (strcmp(bool_names[i], name) == 0) {
			return i;
		}
	}
	return -1;
}
static uint8_t *flag(struct caps *c, const char *name) { return &c->b[index_of(name)]; }

static struct objc_ivar *t_get_ivar(Class cls, const char *name)
{
	lookups++;
	if (cls != (Class)0x1000 || (missing != NULL && strcmp(missing, name) == 0)) {
		return NULL;
	}
	if (strcmp(name, "__backWideDualCameraSwitchOverZoomFactor") == 0) {
		return (struct objc_ivar *)(long)(100 + __builtin_offsetof(struct caps, over));
	}
	if (strcmp(name, "_superWideDisplayZoomFactor") == 0) {
		return (struct objc_ivar *)(long)(100 + __builtin_offsetof(struct caps, shown));
	}
	return index_of(name) >= 0 ? (struct objc_ivar *)(long)(100 + __builtin_offsetof(struct caps, b) + (unsigned long)index_of(name)) : NULL;
}
static long t_ivar_offset(struct objc_ivar *v) { return (long)v - 100; }

const char *t_getprogname(void) { return program; }
int t_access(const char *path, int mode)
{
	(void)mode;
	for (int i = 0; i < 4; i++) {
		if (markers[i] != NULL && strcmp(markers[i], path) == 0) {
			return 0;
		}
	}
	return -1;
}
void *t_dlsym(void *handle, const char *name)
{
	(void)handle;
	return strcmp(name, "class_getInstanceVariable") == 0 ? (void *)t_get_ivar : strcmp(name, "ivar_getOffset") == 0 ? (void *)t_ivar_offset : NULL;
}
Class objc_getClass(const char *name) { return strcmp(name, "CAMCaptureCapabilities") == 0 ? (Class)0x1000 : NULL; }
SEL sel_registerName(const char *name) { return (SEL)name; }
Method class_getInstanceMethod(Class cls, SEL sel) { return cls == (Class)0x1000 && strcmp((const char *)sel, "initWithHostProcess:") == 0 ? (Method)0x2000 : NULL; }
Method class_getClassMethod(Class cls, SEL sel) { (void)cls; (void)sel; return NULL; }

/* the stock init: what CameraUI's makes of AVGestalt's row of the model, for the Camera application (host process 0) */
static id stock_init(id self, SEL _cmd, long host)
{
	struct caps *c = (struct caps *)self;
	static const char *const n104_on[] = {
		"_backSuperWideSupported", "_backWideDualSupported", "_responsiveShutterSupported", "_shotBufferSupported", "_backSpatialOverCaptureSupported",
		"_backLowLightSupported", "_autoLowLightVideoSupported", "_deferredProcessingSupported", "_deepFusionSupported", "_deferredPortraitRenderingSupported",
		"_stereoAudioRecordingSupported", "_neuralEngineSupported", "_ctmSupported", "_useCTMModeSelector", "_allowControlDrawer", "_allowDynamicShutterZoom",
		"_interactiveVideoFormatControlAlwaysEnabled", "_exposureSliderSupported",
	};

	(void)_cmd;
	(void)host;
	stock_calls++;
	if (c == NULL) {
		return NULL;
	}
	memset(c->b, 0, sizeof(c->b));
	*flag(c, "_backLivePhotoSupported") = *flag(c, "_backHDRSupported") = 1;
	c->over = model ? 2.0 : 1.0;
	c->shown = model ? 0.5 : 0.0;
	for (unsigned i = 0; model && i < sizeof(n104_on) / sizeof(n104_on[0]); i++) {
		*flag(c, n104_on[i]) = 1;
	}
	return self;
}
IMP method_setImplementation(Method m, IMP imp)
{
	CHECK(m == (Method)0x2000);
	installed = imp;
	return (IMP)stock_init;
}
IMP method_getImplementation(Method m) { (void)m; return installed; }

static struct caps *make(struct caps *c)
{
	last_say[0] = 0;
	return (struct caps *)((id (*)(id, SEL, long))installed)((id)c, (SEL)"initWithHostProcess:", 0);
}

int main(void)
{
	struct caps c;

	/* the constructor has run (the program is "Camera"): the init is wrapped */
	CHECK(installed != NULL && installed != (IMP)stock_init);
	if (installed == NULL) {
		printf("t_camui: the constructor did not wrap the init\n");
		return 1;
	}
	/* 1. an iPhone X (hw.model not changed for the Camera): the stock init is called and NOTHING else is done */
	model = 0;
	CHECK(make(&c) == &c && stock_calls == 1);
	for (int i = 0; i < NBOOLS; i++) {
		CHECK(c.b[i] == (i == index_of("_backLivePhotoSupported") || i == index_of("_backHDRSupported")));
	}
	CHECK(strstr(last_say, "NOTHING CHANGED") != NULL);
	/* 2. an iPhone 11: single cameras forced; the lenses stay; what it adds to the session is off; the old viewfinder, suppressed as the stock init does */
	model = 1;
	CHECK(make(&c) == &c);
	CHECK(*flag(&c, "_backSuperWideSupported") == 1 && *flag(&c, "_backWideDualSupported") == 1 && *flag(&c, "_featureDevelopmentForceSingleCamera") == 1);
	CHECK(*flag(&c, "_responsiveShutterSupported") == 0 && *flag(&c, "_shotBufferSupported") == 0 && *flag(&c, "_backSpatialOverCaptureSupported") == 0 &&
	    *flag(&c, "_backLowLightSupported") == 0 && *flag(&c, "_autoLowLightVideoSupported") == 0 && *flag(&c, "_deferredProcessingSupported") == 0 &&
	    *flag(&c, "_deepFusionSupported") == 0 && *flag(&c, "_deferredPortraitRenderingSupported") == 0 && *flag(&c, "_stereoAudioRecordingSupported") == 0 &&
	    *flag(&c, "_neuralEngineSupported") == 0);
	CHECK(*flag(&c, "_ctmSupported") == 0 && *flag(&c, "_useCTMModeSelector") == 0 && *flag(&c, "_allowControlDrawer") == 0 &&
	    *flag(&c, "_allowDynamicShutterZoom") == 0 && *flag(&c, "_interactiveVideoFormatControlAlwaysEnabled") == 0 &&
	    *flag(&c, "_exposureSliderSupported") == 0 && *flag(&c, "_ctmSupportSuppressed") == 1);
	CHECK(*flag(&c, "_backLivePhotoSupported") == 1 && *flag(&c, "_backHDRSupported") == 1 && c.over == 2.0 && c.shown == 0.5);
	CHECK(strstr(last_say, "FORCED") != NULL && strstr(last_say, "featureDevelopmentForceSingleCamera") != NULL && strstr(last_say, "2.00") != NULL);
	/* 3. the markers, one at a time */
	markers[0] = "/var/db/nord2-camera-dualwide";
	make(&c);
	CHECK(*flag(&c, "_featureDevelopmentForceSingleCamera") == 0 && *flag(&c, "_backLowLightSupported") == 0 && *flag(&c, "_ctmSupported") == 0);
	CHECK(strstr(last_say, "not forced") != NULL);
	markers[0] = "/var/db/nord2-camera-n104-all";
	make(&c);
	CHECK(*flag(&c, "_featureDevelopmentForceSingleCamera") == 1 && *flag(&c, "_backLowLightSupported") == 1 && *flag(&c, "_responsiveShutterSupported") == 1 &&
	    *flag(&c, "_ctmSupported") == 0 && *flag(&c, "_ctmSupportSuppressed") == 1);
	markers[0] = "/var/db/nord2-camera-ctm";
	make(&c);
	CHECK(*flag(&c, "_featureDevelopmentForceSingleCamera") == 1 && *flag(&c, "_backLowLightSupported") == 0 && *flag(&c, "_ctmSupported") == 1 &&
	    *flag(&c, "_allowControlDrawer") == 1 && *flag(&c, "_ctmSupportSuppressed") == 0);
	markers[0] = NULL;
	/* 4. a CameraUI without one of the names: that one is left, the rest is done; without the single-camera one nothing is forced and it is said */
	missing = "_deepFusionSupported";
	make(&c);
	CHECK(*flag(&c, "_deepFusionSupported") == 1 && *flag(&c, "_deferredProcessingSupported") == 0 && *flag(&c, "_featureDevelopmentForceSingleCamera") == 1);
	missing = "_featureDevelopmentForceSingleCamera";
	make(&c);
	CHECK(*flag(&c, "_featureDevelopmentForceSingleCamera") == 0 && strstr(last_say, "not forced") != NULL);
	missing = "_backSuperWideSupported";
	make(&c);
	CHECK(*flag(&c, "_featureDevelopmentForceSingleCamera") == 0 && *flag(&c, "_ctmSupported") == 1 && strstr(last_say, "NOTHING CHANGED") != NULL);
	missing = NULL;
	/* 5. the stock init gives nothing: nothing is touched */
	lookups = 0;
	CHECK(make(NULL) == NULL && lookups == 0);
	printf(failures ? "t_camui: %d FAILURES\n" : "t_camui: all passed (%d inits)\n", failures ? failures : stock_calls);
	return failures != 0;
}
