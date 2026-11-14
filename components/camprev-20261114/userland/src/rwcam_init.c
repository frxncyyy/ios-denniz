/*
 * Arms the camera inside the process that loads RedwoodAudio.driver (mediaserverd: the audio HAL and CMCapture live there). A constructor of this
 * bundle -- run when the plug-in is loaded, before anything of the audio code -- does NOTHING unless the kernel has /dev/redwood-camera, and then
 * only registers a dyld callback: the audio plug-in's own code (audio_plugin.c) is not touched and does not depend on any of this. When CMCapture's
 * image arrives (or is already there), the two methods are replaced, each only if it is what it must be:
 *
 *   +[BWFigCaptureDeviceVendor sharedCaptureDeviceVendor]  stock: a vendor made with the create function of H10ISP.mediacapture (found through the
 *                                                          IOService AppleH13CamIn / H10 / H9 / H6, which this phone does not have: the vendor is
 *                                                          nil, and no camera). Ours: the stock answer if there is one, else a vendor made with
 *                                                          redwood_camera_device_create (rwcam_device.c). CMCapture 16.7 (0x1a1f40c84): calls
 *                                                          -initWithDeviceCreateFunction: on a new object; the function is called as
 *                                                          fn(kCFAllocatorDefault, NULL, &device) by -_createDevice:clientPID: (checked, REPORT.md).
 *   +[BWCoreImageFilterRenderer prewarmPortraitFilterVersion:semanticStyleFilters:]
 *                                                          stock: a Core Image portrait filter prewarm soon after mediaserverd starts. HakiOS's
 *                                                          phone had no GL and the daemon jumped to 0 at every start. Replaced by nothing unless
 *                                                          /var/db/nord2-camera-prewarm exists (portrait is not a thing here).
 *
 * The capture servers. mediaserverd's main() calls FigMediaServerStart (MediaToolbox) right after AudioToolboxServerInit, and that starts CMCapture's
 * servers (FigStartCaptureServers) BEFORE this bundle is loaded: FigCaptureSourceServerStart then asks the stock vendor (nil), gets "no device, no
 * error", and keeps a FigCaptureSourceBackings with no sources for good (run q10gl: Camera opens, black preview, no lens). So the system keeps
 * StartRecorderServer=false in MediaToolbox.framework/<model>/SubServerConfiguration.plist (stock FigMediaServerStart then starts no capture
 * server) and this bundle calls FigStartCaptureServers() itself, once, on the main queue, after both methods are replaced. If the plist does not
 * say false the stock start has already happened and nothing is started here.
 *
 * Unlike HakiOS's libredwood-media nothing is compared with an address of an expected cache: the methods are found by class and selector and
 * their type encodings are checked.
 */
#include "rwcam_abi.h"
#include "rwcam_log.h"
#include "rwcam_device.h"
#include "redwood_camera.h"

void rwcam_diag_after_servers(void);
void rwcam_diag_hooks(void);

static IMP original_shared_vendor;
static id ours;
static pthread_mutex_t ours_lock = PTHREAD_MUTEX_INITIALIZER;
static bool armed_vendor, armed_prewarm;

/*
 * VideoToolbox's FigGetAlignmentForIOSurfaceOutput and this kernel's framebuffer (run q10h9: mediaserverd's abort while the capture graph is built).
 *
 * -[BWImageQueueSinkNode _updateInputRequirements] (the preview sink of every capture graph; CMCapture 16.7 0x1a21a9948) calls VideoToolbox's
 * FigGetAlignmentForIOSurfaceOutput(&plane, &stride) (0x194f3b5a8 -> FigGetAlignmentForIOSurfaceOutputToDisplayService, 0x194f3b410), which, once per
 * process, takes the display's IOService (AppleCLCD, else AppleCLCD2, else AppleRGBOUT) and reads two of its registry properties as DICTIONARIES:
 *
 *      "DisplayPipePlaneBaseAlignment"  = { "PlaneBaseAlignmentLinear" = number }      (plane alignment; 64 when the property or the key is not there)
 *      "DisplayPipeStrideRequirements"  = { "StrideLinearHorizontal"   = number }      (bytes-per-row alignment; 64 likewise)
 *
 * with CFDictionaryGetValue and no type check of the property itself. This kernel's AppleCLCD (SweetFramebuffer.cpp) publishes
 * DisplayPipePlaneBaseAlignment as a plain NUMBER (64): CFDictionaryGetValue of a number is -[__NSCFNumber objectForKey:], an NSInvalidArgumentException
 * nobody catches. The right correction is the kernel's (publish the dictionary, or nothing). Until a kernel has it, this does what the function would have
 * done: when (and only when) the registry really has a non-dictionary there, the function's two "already read" flags and cached values in VideoToolbox's
 * data are filled in. They are found from the function's own instructions (adrp + ldrb flag / tbnz #0 to "ldr w, [value]"), not from an address of some
 * cache; anything unexpected and nothing is written. Only this process (mediaserverd) is covered: another process that calls the function still needs
 * the kernel's correction.
 */
static bool
alignment_statics(const uint32_t *code, uint8_t *flag[2], uint32_t *value[2])
{
	uintptr_t page[32];
	bool known[32] = { false };
	int found = 0;

	for (int i = 0; i < 160 && found < 2; i++) {
		uint32_t w = code[i];
		uintptr_t pc = (uintptr_t)&code[i];

		if (w == 0xd65f03c0u) {                                 /* ret */
			break;
		}
		if ((w & 0x9f000000u) == 0x90000000u) {                 /* adrp xd, page */
			int64_t imm = (int64_t)(((w >> 5) & 0x7ffff) << 2 | ((w >> 29) & 3));

			imm = imm & 0x100000 ? imm - 0x200000 : imm;
			page[w & 31] = (pc & ~(uintptr_t)0xfff) + (uintptr_t)(imm * 4096);
			known[w & 31] = true;
		} else if ((w & 0xffc00000u) == 0x39400000u && known[(w >> 5) & 31]) {        /* ldrb wt, [xn, #imm] */
			uint8_t *f = (uint8_t *)(page[(w >> 5) & 31] + ((w >> 10) & 0xfff));
			unsigned rt = w & 31;

			for (int k = i + 1; k <= i + 2; k++) {
				uint32_t t = code[k];

				if ((t & 0x9f000000u) == 0x90000000u) {         /* (the value's adrp sits between the two) */
					int64_t imm = (int64_t)(((t >> 5) & 0x7ffff) << 2 | ((t >> 29) & 3));

					imm = imm & 0x100000 ? imm - 0x200000 : imm;
					page[t & 31] = ((uintptr_t)&code[k] & ~(uintptr_t)0xfff) + (uintptr_t)(imm * 4096);
					known[t & 31] = true;
				} else if ((t & 0xfff8001fu) == (0x37000000u | rt)) {       /* tbnz wt, #0, target */
					int32_t off = (int32_t)((t >> 5) & 0x3fff);
					uint32_t l;

					off = off & 0x2000 ? off - 0x4000 : off;
					if (off <= 0 || k + off >= 200) {
						return false;
					}
					l = code[k + off];
					if ((l & 0xffc00000u) != 0xb9400000u || !known[(l >> 5) & 31]) {       /* ldr w, [xm, #imm] */
						return false;
					}
					flag[found] = f;
					value[found] = (uint32_t *)(page[(l >> 5) & 31] + ((l >> 10) & 0xfff) * 4);
					found++;
					i = k;
					break;
				}
			}
		}
	}
	return found == 2;
}

static void display_alignment_fix_locked(void);

static void
display_alignment_fix(void)
{
	static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&once);
	display_alignment_fix_locked();
	pthread_mutex_unlock(&once);
}

static void
display_alignment_fix_locked(void)
{
	static const char *const classes[] = { "AppleCLCD", "AppleCLCD2", "AppleRGBOUT" };
	static const char *const keys[2] = { "DisplayPipePlaneBaseAlignment", "DisplayPipeStrideRequirements" };
	static bool done;
	CFMutableDictionaryRef (*matching)(const char *) = (CFMutableDictionaryRef (*)(const char *))dlsym(RTLD_DEFAULT, "IOServiceMatching");
	uint32_t (*get_service)(uint32_t, CFDictionaryRef) = (uint32_t (*)(uint32_t, CFDictionaryRef))dlsym(RTLD_DEFAULT, "IOServiceGetMatchingService");
	CFTypeRef (*search)(uint32_t, const char *, CFStringRef, CFAllocatorRef, uint32_t) =
	    (CFTypeRef (*)(uint32_t, const char *, CFStringRef, CFAllocatorRef, uint32_t))dlsym(RTLD_DEFAULT, "IORegistryEntrySearchCFProperty");
	int (*release)(uint32_t) = (int (*)(uint32_t))dlsym(RTLD_DEFAULT, "IOObjectRelease");
	const uint32_t *code = (const uint32_t *)dlsym(RTLD_DEFAULT, "FigGetAlignmentForIOSurfaceOutputToDisplayService");
	uint32_t service = 0, replace[2] = { 0, 0 };
	uint8_t *flag[2];
	uint32_t *value[2];
	size_t cache_size = 0;
	uintptr_t cache = (uintptr_t)_dyld_get_shared_cache_range(&cache_size);
	bool wrong = false;

	if (done) {
		return;
	}
	if (matching == NULL || get_service == NULL || search == NULL || release == NULL) {
		done = true;
		rwcam_say("display alignment: IOKit's functions are not there: nothing checked");
		return;
	}
	for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]) && service == 0; i++) {
		CFMutableDictionaryRef m = matching(classes[i]);

		service = m != NULL ? get_service(0, (CFDictionaryRef)m) : 0;  /* (takes the dictionary over) */
	}
	if (service == 0) {
		rwcam_say("display alignment: no AppleCLCD / AppleCLCD2 / AppleRGBOUT service (yet): nothing to correct");
		return;                                                 /* asked again at the next call */
	}
	done = true;
	for (int i = 0; i < 2; i++) {
		CFStringRef key = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
		CFTypeRef v = search(service, "IOService", key, NULL, 0);

		CFRelease(key);
		if (v == NULL) {
			rwcam_say("display alignment: the display has no %s: VideoToolbox takes 64", keys[i]);
		} else if (CFGetTypeID(v) == CFDictionaryGetTypeID()) {
			rwcam_say("display alignment: the display's %s is a dictionary, as VideoToolbox reads it", keys[i]);
		} else {
			int64_t n = 0;

			if (CFGetTypeID(v) == CFNumberGetTypeID()) {
				CFNumberGetValue(v, kCFNumberSInt64Type, &n);
			}
			replace[i] = n >= 16 && n <= 4096 && (n & (n - 1)) == 0 ? (uint32_t)n : 64;
			wrong = true;
			rwcam_say("display alignment: the display's %s is NOT a dictionary (%s %lld): VideoToolbox's FigGetAlignmentForIOSurfaceOutput would throw; %u to be put in its place",
			    keys[i], CFGetTypeID(v) == CFNumberGetTypeID() ? "a number," : "type", CFGetTypeID(v) == CFNumberGetTypeID() ? (long long)n : (long long)CFGetTypeID(v),
			    replace[i]);
		}
		if (v != NULL) {
			CFRelease(v);
		}
	}
	release(service);
	if (!wrong) {
		return;
	}
	if (code == NULL) {
		/* the exported entry is "mov x2, x1; mov x1, x0; b FigGetAlignmentForIOSurfaceOutputToDisplayService" */
		const uint32_t *entry = (const uint32_t *)dlsym(RTLD_DEFAULT, "FigGetAlignmentForIOSurfaceOutput");

		for (int i = 0; entry != NULL && i < 4 && code == NULL; i++) {
			if ((entry[i] & 0xfc000000u) == 0x14000000u) {
				int32_t off = (int32_t)(entry[i] & 0x3ffffff);

				off = off & 0x2000000 ? off - 0x4000000 : off;
				code = entry + i + off;
			}
		}
	}
	if (code == NULL || !alignment_statics(code, flag, value)) {
		rwcam_say("display alignment: VideoToolbox's FigGetAlignmentForIOSurfaceOutput %s: NOT corrected (the preview sink will throw)",
		    code == NULL ? "is not found" : "is not the code expected");
		done = code != NULL;                                    /* (VideoToolbox not loaded yet: asked again at the next call) */
		return;
	}
	for (int i = 0; i < 2; i++) {
		uintptr_t f = (uintptr_t)flag[i], v = (uintptr_t)value[i];

		if (cache == 0 || f < cache || f >= cache + cache_size || v < cache || v + 4 > cache + cache_size || (v & 3) != 0 || *flag[i] > 1 || flag[0] == flag[1] ||
		    value[0] == value[1]) {
			rwcam_say("display alignment: VideoToolbox's cached values are not where expected (%d: flag %#lx, value %#lx unslid): NOT corrected", i,
			    (unsigned long)f - redwood_cache_slide(), (unsigned long)v - redwood_cache_slide());
			return;
		}
	}
	for (int i = 0; i < 2; i++) {
		if (replace[i] == 0) {
			continue;                                       /* this property is fine: the function reads it itself */
		}
		if (*flag[i] == 0) {
			*value[i] = replace[i];
			*flag[i] = 1;
		}
		rwcam_say("display alignment: %s: VideoToolbox's cached value is now %u (flag %#lx, value %#lx unslid): the registry is not read for it in this process",
		    keys[i], *value[i], (unsigned long)(uintptr_t)flag[i] - redwood_cache_slide(), (unsigned long)(uintptr_t)value[i] - redwood_cache_slide());
	}
}

/* +[BWFigCaptureDeviceVendor sharedCaptureDeviceVendor] */
static id
shared_vendor(id self, SEL selector)
{
	id vendor = ((id (*)(id, SEL))original_shared_vendor)(self, selector);
	static int asked;

	display_alignment_fix();         /* (before any capture graph; does its work once) */
	if (asked < 4) {
		asked++;
		rwcam_say("mediaserverd: sharedCaptureDeviceVendor asked (%d), stock answer %s", asked, vendor ? "there" : "nil");
	}
	if (vendor != NULL) {
		return vendor;
	}
	pthread_mutex_lock(&ours_lock);
	if (ours == NULL) {
		Class cls = objc_getClass("BWFigCaptureDeviceVendor");
		id made = ((id (*)(Class, SEL))objc_msgSend)(cls, sel_registerName("alloc"));

		ours = ((id (*)(id, SEL, void *))objc_msgSend)(made, sel_registerName("initWithDeviceCreateFunction:"), (void *)redwood_camera_device_create);
		rwcam_say("mediaserverd: the camera's device vendor %s", ours ? "made (rwcam_device.c)" : "CANNOT BE MADE");
	}
	vendor = ours;
	pthread_mutex_unlock(&ours_lock);
	return vendor;
}

/* +[BWCoreImageFilterRenderer prewarmPortraitFilterVersion:semanticStyleFilters:] */
static void
prewarm_portrait_filter(id self, SEL selector, int version, BOOL styles)
{
	(void)self;
	(void)selector;
	(void)version;
	(void)styles;
}

/* true only when the model's (else Default's) SubServerConfiguration.plist is there, is XML, and has StartRecorderServer false */
static bool
stock_capture_servers_off(void)
{
	static const char *const paths[] = {
		"/System/Library/Frameworks/MediaToolbox.framework/D22/SubServerConfiguration.plist",
		"/System/Library/Frameworks/MediaToolbox.framework/Default/SubServerConfiguration.plist",
	};
	static const char key[] = "<key>StartRecorderServer</key>";

	for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		char text[4096];
		int fd = open(paths[i], O_RDONLY);
		long n;

		if (fd < 0) {
			continue;
		}
		n = read(fd, text, sizeof(text) - 1);
		close(fd);
		if (n <= 0) {
			return false;
		}
		text[n] = 0;
		for (long at = 0; at + (long)sizeof(key) - 1 <= n; at++) {
			if (memcmp(text + at, key, sizeof(key) - 1) == 0) {
				const char *v = text + at + sizeof(key) - 1;

				while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') {
					v++;
				}
				return memcmp(v, "<false/>", 8) == 0;
			}
		}
		return false;           /* no key: the stock code starts them */
	}
	return false;
}

static void
start_capture_servers(void *unused)
{
	void (*start)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "FigStartCaptureServers");

	(void)unused;
	if (start == NULL) {
		rwcam_say("mediaserverd: FigStartCaptureServers not found: no capture server");
		return;
	}
	rwcam_diag_hooks();
	rwcam_say("mediaserverd: starting the capture servers (FigStartCaptureServers)");
	start();
	rwcam_say("mediaserverd: capture servers started");
	rwcam_diag_after_servers();
}

static void *
start_capture_servers_thread(void *unused)
{
	start_capture_servers(unused);
	return NULL;
}

static void
schedule_capture_servers(void)
{
	static bool scheduled;
	void (*async_f)(void *, void *, void (*)(void *));
	void *main_q;
	pthread_t thread;

	if (scheduled) {
		return;
	}
	scheduled = true;
	if (!stock_capture_servers_off()) {
		rwcam_say("mediaserverd: StartRecorderServer is not false: the capture servers are the stock code's (started before these hooks?)");
		return;
	}
	async_f = (void (*)(void *, void *, void (*)(void *)))dlsym(RTLD_DEFAULT, "dispatch_async_f");
	main_q = dlsym(RTLD_DEFAULT, "_dispatch_main_q");
	rwcam_say("mediaserverd: StartRecorderServer is false: capture servers to be started here (%s)", async_f != NULL && main_q != NULL ? "main queue" : "thread");
	if (async_f != NULL && main_q != NULL) {
		async_f(main_q, NULL, start_capture_servers);
	} else if (pthread_create(&thread, NULL, start_capture_servers_thread, NULL) == 0) {
		pthread_detach(thread);
	}
}

static void
image_added(const struct mach_header *header, intptr_t slide)
{
	(void)header;
	(void)slide;
	if (!armed_vendor) {
		Class cls = objc_getClass("BWFigCaptureDeviceVendor");

		if (cls != NULL) {
			SEL sel = sel_registerName("sharedCaptureDeviceVendor");
			Method m = class_getClassMethod(cls, sel);
			Method init = class_getInstanceMethod(cls, sel_registerName("initWithDeviceCreateFunction:"));
			const char *enc = m != NULL ? method_getTypeEncoding(m) : NULL;

			armed_vendor = true;
			/* returns an object, takes self and _cmd: "@16@0:8" */
			if (m == NULL || init == NULL || enc == NULL || enc[0] != '@') {
				rwcam_say("+[BWFigCaptureDeviceVendor sharedCaptureDeviceVendor] / -initWithDeviceCreateFunction: not as expected (%s, init %s): camera off",
				    enc ? enc : "missing", init ? "there" : "missing");
			} else {
				original_shared_vendor = method_getImplementation(m);
				method_setImplementation(m, (IMP)shared_vendor);
				rwcam_say("mediaserverd: +[BWFigCaptureDeviceVendor sharedCaptureDeviceVendor] (%s) replaced", enc);
			}
		}
	}
	if (!armed_prewarm) {
		Class cls = objc_getClass("BWCoreImageFilterRenderer");

		if (cls != NULL) {
			Method m = class_getClassMethod(cls, sel_registerName("prewarmPortraitFilterVersion:semanticStyleFilters:"));
			const char *enc = m != NULL ? method_getTypeEncoding(m) : NULL;

			armed_prewarm = true;
			if (access("/var/db/nord2-camera-prewarm", F_OK) == 0) {
				rwcam_say("portrait prewarm left alone (/var/db/nord2-camera-prewarm)");
			} else if (m == NULL || enc == NULL || enc[0] != 'v') {
				rwcam_say("+[BWCoreImageFilterRenderer prewarmPortraitFilterVersion:semanticStyleFilters:] not as expected (%s): left alone", enc ? enc : "missing");
			} else {
				method_setImplementation(m, (IMP)prewarm_portrait_filter);
				rwcam_say("mediaserverd: portrait filter prewarm skipped");
			}
		}
	}
	if (armed_vendor && armed_prewarm) {
		schedule_capture_servers();
	}
}

__attribute__((constructor))
static void
rwcam_constructor(void)
{
	/* the audio plug-in must come up whatever happens here: no camera node, no hooks, nothing else done */
	if (access(REDWOOD_CAMERA_DEVICE, F_OK) != 0) {
		return;
	}
	rwcam_say("%s: the kernel has a camera (%s): arming the CMCapture hooks (slide %#lx)", getprogname() ? getprogname() : "?", REDWOOD_CAMERA_DEVICE,
	    redwood_cache_slide());
	display_alignment_fix();
	_dyld_register_func_for_add_image(image_added);         /* also called for every image already there */
}
