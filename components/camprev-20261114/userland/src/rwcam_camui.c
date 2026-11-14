/*
 * libredwoodcamui.dylib (/usr/lib): for the Camera application's process ONLY. It makes Camera.app offer the Nord 2's ultra-wide camera ("0.5x")
 * next to the wide one ("1x") as TWO AVCaptureDevices it changes between, which is what RedwoodAudio.driver's capture device has (one sensor
 * streams at a time).
 *
 * How it gets there: DYLD_INSERT_LIBRARIES=/usr/lib/libredwoodcamui.dylib in the environment of the program named Camera, put there by the
 * kernel at exec (where it already adds OS_ACTIVITY_DT_MODE for the programs applog_name names: bsd/kern/kern_exec.c), with dyld's AMFI policy
 * answer for that process allowing path variables (bit 1; and bit 5, so that Camera still starts when the file is not there):
 * bsd/kern/sweet_amfi.c. NOT by a load command added to Camera's executable: run q10gv showed that a Camera whose executable is changed loses
 * its entitlements for TCC and is killed. The constructor has to run before the application's main() (dyld's inserted libraries do): it wraps
 * the init of an object that is made once, early.
 *
 * Why it takes code in the Camera process (CameraUI 16.7, addresses of the cache before its slide):
 *   - the lenses Camera.app offers are CAMCaptureCapabilities' (-initWithHostProcess: 0x1ceb3eab8), made of AVGestalt's table of the model
 *     hw.model names. Every model of that table with an ultra-wide camera also has AVGQDeviceHasAggregateCamera, so _backWideDualSupported is
 *     set, and then -isWideDualSupportedForMode:devicePosition:videoConfiguration: (0x1ceb427ac) says yes for Photo/Square/Cinematic without
 *     looking further: the app asks for AVCaptureDeviceTypeBuiltInDualWideCamera, a virtual device CMCapture makes of two SYNCHRONIZED streams
 *     that both give frames (BWBravoStreamSelector goes by both streams' frame statistics). With no such device the app falls back to BackWide
 *     (-[CAMCaptureCommandContext _captureEngineDeviceForMode:...] 0x1ceaecc08), the viewfinder takes the resolved device (BackWide) and its
 *     zoom control is the wide camera's alone: no 0.5x.
 *   - the app has its own way to offer the lenses of a model one device at a time: +[CAMZoomControlUtilities
 *     shouldEmulateWideDualCameraZoomForMode:...] (0x1cea975e0) is yes when the ultra-wide and the wide camera are there and the dual-wide device
 *     is NOT supported for the mode; the zoom control then goes from 0.5x, and +deviceForEmulatingZoomFactor:fromDevice:mode:... (0x1cea96f78)
 *     changes BackWide <-> BackSuperWide where the zoom crosses 1x (never while a video is recorded).
 *   - -isWideDualSupportedForMode: (and the dual and triple ones, nothing else) asks -featureDevelopmentForceSingleCamera, an ivar that
 *     -initWithHostProcess: fills from the preference CAMFeatureDevelopmentForceSingleCamera on Apple-internal installs only. Set here after the
 *     stock init, it is the switch the app itself has for "single cameras only".
 *
 * So: -[CAMCaptureCapabilities initWithHostProcess:] is wrapped; when the stock one has found an ultra-wide camera (the kernel says an iPhone 11,
 * N104AP, to the Camera process's hw.model; without that nothing here changes anything) the ivars below are set BY NAME (a name this CameraUI does
 * not have is said and left). Besides the single-camera switch, what the iPhone 11's table switches on and the iPhone X's (the model everything
 * else on this phone is, and the one the capture device was brought up with) does not, and that changes the capture session, is switched off
 * again: the capture device here has none of it. Markers (in /var/db) leave parts as the stock init made them:
 *     nord2-camera-dualwide   the dual-wide device is asked for (for a capture device that has one)
 *     nord2-camera-n104-all   night mode, deferred processing, responsive shutter, over-capture, stereo audio... as an iPhone 11 has them
 *     nord2-camera-ctm        the iPhone 11's viewfinder (CAMFullscreenViewfinder, QuickTake) instead of the iPhone X's
 */
#include "rwcam_abi.h"
#include "rwcam_log.h"

typedef struct objc_ivar *Ivar;

static IMP stock_init;
static Ivar (*get_ivar)(Class, const char *);
static long (*ivar_offset)(Ivar);

/* the BOOL (or double) ivar `name` of a CAMCaptureCapabilities, or NULL */
static void *
ivar_at(id self, Class cls, const char *name)
{
	Ivar v = get_ivar(cls, name);
	long offset = v != NULL ? ivar_offset(v) : 0;

	if (v == NULL || offset < 8 || offset > 0x1000) {
		rwcam_say("Camera: CAMCaptureCapabilities has no %s here: left alone", name);
		return NULL;
	}
	return (char *)self + offset;
}

static int
get_bool(id self, Class cls, const char *name)
{
	const uint8_t *p = ivar_at(self, cls, name);

	return p != NULL ? *p != 0 : -1;
}

static unsigned long
length(const char *text)
{
	unsigned long n = 0;

	while (text[n] != 0) {
		n++;
	}
	return n;
}

/* how many of the names were changed */
static int
set_bools(id self, Class cls, const char *const *names, int n, bool value, char *said, unsigned long said_size)
{
	int changed = 0;

	for (int i = 0; i < n; i++) {
		uint8_t *p = ivar_at(self, cls, names[i]);

		if (p != NULL && (*p != 0) != value) {
			unsigned long used = length(said);

			*p = value;
			changed++;
			if (used + length(names[i]) + 2 < said_size) {
				snprintf(said + used, said_size - used, " %s", names[i] + 1);
			}
		}
	}
	return changed;
}

static id
capabilities_init(id self, SEL _cmd, long host_process)
{
	static const char *const single[] = { "_featureDevelopmentForceSingleCamera" };
	/* in the iPhone 11's row of AVGestalt's table and not in the iPhone X's, and asked by the commands that set the session up */
	static const char *const n104[] = {
		"_responsiveShutterSupported", "_shotBufferSupported", "_backSpatialOverCaptureSupported", "_frontSpatialOverCaptureSupported",
		"_backLowLightSupported", "_frontLowLightSupported", "_autoLowLightVideoSupported", "_deferredProcessingSupported", "_deepFusionSupported",
		"_deferredPortraitRenderingSupported", "_contentAwareDistortionCorrectionSupported", "_stereoAudioRecordingSupported",
		"_neuralEngineSupported",
	};
	/*
	 * what -initWithHostProcess: makes of AVGQDeviceSupportsMomentCapture: for the host processes it does not give the new viewfinder to (1, 3, 4)
	 * it clears exactly these and sets _ctmSupportSuppressed
	 */
	static const char *const ctm[] = {
		"_ctmSupported", "_useCTMTransientImageWell", "_useCTMModeSelector", "_allowControlDrawer", "_allowDynamicShutterZoom",
		"_interactiveVideoFormatControlAlwaysEnabled", "_exposureSliderSupported",
	};
	static const char *const ctm_off[] = { "_ctmSupportSuppressed" };
	Class cls;
	char said[400] = "";
	int ultra, dual;

	self = ((id (*)(id, SEL, long))stock_init)(self, _cmd, host_process);
	if (self == NULL || (cls = objc_getClass("CAMCaptureCapabilities")) == NULL) {
		return self;
	}
	ultra = get_bool(self, cls, "_backSuperWideSupported");
	dual = get_bool(self, cls, "_backWideDualSupported");
	if (ultra != 1) {
		rwcam_say("Camera: capabilities (host process %ld): no back ultra-wide camera for this model (hw.model is not an ultra-wide iPhone's for "
		    "the Camera process?): NOTHING CHANGED, no 0.5x", host_process);
		return self;
	}
	if (access("/var/db/nord2-camera-dualwide", F_OK) != 0) {
		(void)set_bools(self, cls, single, 1, true, said, sizeof(said));
	}
	if (access("/var/db/nord2-camera-n104-all", F_OK) != 0) {
		(void)set_bools(self, cls, n104, (int)(sizeof(n104) / sizeof(n104[0])), false, said, sizeof(said));
	}
	if (access("/var/db/nord2-camera-ctm", F_OK) != 0 &&
	    set_bools(self, cls, ctm, (int)(sizeof(ctm) / sizeof(ctm[0])), false, said, sizeof(said)) != 0) {
		(void)set_bools(self, cls, ctm_off, 1, true, said, sizeof(said));
	}
	{
		const double *over = ivar_at(self, cls, "__backWideDualCameraSwitchOverZoomFactor"), *shown = ivar_at(self, cls, "_superWideDisplayZoomFactor");

		rwcam_say("Camera: capabilities (host process %ld): back ultra-wide camera, dual-wide device %s, ultra-wide to wide zoom %.2f, shown as "
		    "%.2fx; single cameras %s", host_process, dual == 1 ? "supported by the model" : "not supported", over ? *over : 0.0,
		    shown ? *shown : 0.0, get_bool(self, cls, "_featureDevelopmentForceSingleCamera") == 1 ?
		    "FORCED (0.5x and 1x are the ultra-wide and the wide AVCaptureDevice)" : "not forced (the dual-wide device is asked for)");
		/* (a line of its own: rwcam_say's lines are 400 bytes) */
		rwcam_say("Camera: capabilities changed:%s", said[0] ? said : " nothing");
	}
	return self;
}

__attribute__((constructor))
static void
camui_constructor(void)
{
	const char *name = getprogname();
	Class cls;
	Method m;

	if (name == NULL || strcmp(name, "Camera") != 0) {
		return;
	}
	if (access("/var/db/nord2-camera-noultrawide", F_OK) == 0) {
		rwcam_say("Camera: /var/db/nord2-camera-noultrawide: CAMCaptureCapabilities left as it is");
		return;
	}
	get_ivar = (Ivar (*)(Class, const char *))dlsym(RTLD_DEFAULT, "class_getInstanceVariable");
	ivar_offset = (long (*)(Ivar))dlsym(RTLD_DEFAULT, "ivar_getOffset");
	cls = objc_getClass("CAMCaptureCapabilities");
	m = cls != NULL ? class_getInstanceMethod(cls, sel_registerName("initWithHostProcess:")) : NULL;
	if (get_ivar == NULL || ivar_offset == NULL || m == NULL) {
		rwcam_say("Camera: no -[CAMCaptureCapabilities initWithHostProcess:] here (class %p, method %p): nothing changed", (void *)cls, (void *)m);
		return;
	}
	stock_init = method_setImplementation(m, (IMP)capabilities_init);
	rwcam_say("Camera: -[CAMCaptureCapabilities initWithHostProcess:] wrapped (single back cameras for the ultra-wide lens)");
}
