// SPDX-License-Identifier: MIT
// n2sens (sensors-20261115): ssh diagnostic for the Nord 2 sensor path, run as root:  n2sens [seconds]
//  1. MobileGestalt answers for the sensor capability keys (MGCopyAnswer; "(null)" = key unknown / no answer)
//  2. CoreLocation: headingAvailable / locationServicesEnabled / significantLocationChangeMonitoringAvailable
//  3. CoreMotion pull mode (CMMotionManager): availability flags, then accelerometer / gyro / magnetometer / device motion
//     samples for N seconds (device motion with the magnetic-north reference frame = what the Compass app needs).
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct { double x, y, z; } v3;
typedef struct { v3 field; int accuracy; } calfield;

static id cls(const char *n) { return (id)objc_getClass(n); }
static SEL S(const char *n) { return sel_registerName(n); }
#define MSG(ret, ...) ((ret (*)(id, SEL, ##__VA_ARGS__))objc_msgSend)

static void mg(void)
{
	void *h = dlopen("/usr/lib/libMobileGestalt.dylib", RTLD_NOW);
	CFTypeRef (*copy)(CFStringRef, CFDictionaryRef) = h ? (CFTypeRef (*)(CFStringRef, CFDictionaryRef))dlsym(h, "MGCopyAnswer") : NULL;
	static const char *keys[] = { "accelerometer", "gyroscope", "magnetometer", "compass", "ambient-light-sensor", "proximity-sensor",
		"DeviceSupportsAlwaysOnCompass", "DeviceSupportsHeading", "gps", "location-services", "ProductType", "HWModelStr",
		"DeviceSupportsAOP", "aop", "front-facing-camera", NULL };
	printf("== MobileGestalt\n");
	if (!copy) { printf("  MGCopyAnswer not found\n"); return; }
	for (int i = 0; keys[i]; i++) {
		CFStringRef k = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
		CFTypeRef v = copy(k, NULL);
		char buf[200] = "(null)";
		if (v) {
			if (CFGetTypeID(v) == CFBooleanGetTypeID()) snprintf(buf, sizeof(buf), "%s", CFBooleanGetValue(v) ? "true" : "false");
			else if (CFGetTypeID(v) == CFStringGetTypeID()) CFStringGetCString(v, buf, sizeof(buf), kCFStringEncodingUTF8);
			else if (CFGetTypeID(v) == CFNumberGetTypeID()) { long long n = 0; CFNumberGetValue(v, kCFNumberLongLongType, &n); snprintf(buf, sizeof(buf), "%lld", n); }
			else snprintf(buf, sizeof(buf), "(type %lu)", (unsigned long)CFGetTypeID(v));
			CFRelease(v);
		}
		printf("  %-30s %s\n", keys[i], buf);
		CFRelease(k);
	}
}

static void cl(void)
{
	dlopen("/System/Library/Frameworks/CoreLocation.framework/CoreLocation", RTLD_NOW);
	id c = cls("CLLocationManager");
	printf("== CoreLocation\n");
	if (!c) { printf("  CLLocationManager missing\n"); return; }
	printf("  headingAvailable %d  locationServicesEnabled %d\n", (int)MSG(BOOL)(c, S("headingAvailable")),
	    (int)MSG(BOOL)(c, S("locationServicesEnabled")));
	SEL a = S("authorizationStatusForBundleIdentifier:");
	if (class_getClassMethod((Class)c, a)) {
		id bid = MSG(id, const char *)(cls("NSString"), S("stringWithUTF8String:"), "com.apple.compass");
		printf("  authorization com.apple.compass %d (0 notDetermined 1 restricted 2 denied 3 always 4 whenInUse)\n", (int)MSG(int, id)(c, a, bid));
	}
}

static void cm(int secs)
{
	dlopen("/System/Library/Frameworks/CoreMotion.framework/CoreMotion", RTLD_NOW);
	id m = MSG(id)(MSG(id)(cls("CMMotionManager"), S("alloc")), S("init"));
	printf("== CoreMotion\n");
	if (!m) { printf("  CMMotionManager missing\n"); return; }
	printf("  available: accel %d gyro %d mag %d deviceMotion %d  reference frames %#lx (1 arbitraryZ 2 corrected 4 magneticNorth 8 trueNorth)\n",
	    (int)MSG(BOOL)(m, S("isAccelerometerAvailable")), (int)MSG(BOOL)(m, S("isGyroAvailable")), (int)MSG(BOOL)(m, S("isMagnetometerAvailable")),
	    (int)MSG(BOOL)(m, S("isDeviceMotionAvailable")), (unsigned long)MSG(unsigned long)(cls("CMMotionManager"), S("availableAttitudeReferenceFrames")));
	MSG(void, double)(m, S("setDeviceMotionUpdateInterval:"), 0.1);
	MSG(void)(m, S("startAccelerometerUpdates"));
	MSG(void)(m, S("startGyroUpdates"));
	MSG(void)(m, S("startMagnetometerUpdates"));
	MSG(void, unsigned long)(m, S("startDeviceMotionUpdatesUsingReferenceFrame:"), 4ul);
	for (int t = 0; t < secs; t++) {
		sleep(1);
		id a = MSG(id)(m, S("accelerometerData")), g = MSG(id)(m, S("gyroData")), mg_ = MSG(id)(m, S("magnetometerData")), d = MSG(id)(m, S("deviceMotion"));
		v3 av = a ? MSG(v3)(a, S("acceleration")) : (v3){0}, gv = g ? MSG(v3)(g, S("rotationRate")) : (v3){0}, mv = mg_ ? MSG(v3)(mg_, S("magneticField")) : (v3){0};
		printf("  t=%d acc %s%.3f %.3f %.3f g | gyro %s%.3f %.3f %.3f rad/s | mag %s%.1f %.1f %.1f uT | dm ", t + 1, a ? "" : "(nil)", av.x, av.y, av.z,
		    g ? "" : "(nil)", gv.x, gv.y, gv.z, mg_ ? "" : "(nil)", mv.x, mv.y, mv.z);
		if (d) {
			id att = MSG(id)(d, S("attitude"));
			double heading = class_getInstanceMethod(object_getClass(d), S("heading")) ? MSG(double)(d, S("heading")) : -999;
			calfield cf = MSG(calfield)(d, S("magneticField"));
			printf("roll %.2f pitch %.2f yaw %.2f heading %.1f calmag %.1f %.1f %.1f acc %d\n", att ? MSG(double)(att, S("roll")) : 0,
			    att ? MSG(double)(att, S("pitch")) : 0, att ? MSG(double)(att, S("yaw")) : 0, heading, cf.field.x, cf.field.y, cf.field.z, cf.accuracy);
		} else {
			printf("(nil)\n");
		}
		fflush(stdout);
	}
	MSG(void)(m, S("stopDeviceMotionUpdates"));
	MSG(void)(m, S("stopMagnetometerUpdates"));
	MSG(void)(m, S("stopGyroUpdates"));
	MSG(void)(m, S("stopAccelerometerUpdates"));
}

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 8;
	setvbuf(stdout, NULL, _IOLBF, 0);
	mg();
	cl();
	cm(secs > 0 ? secs : 8);
	return 0;
}
