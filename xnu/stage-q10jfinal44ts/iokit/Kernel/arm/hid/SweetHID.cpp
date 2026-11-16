/*
 * SWEET: HID event services for the iOS userland (backboardd's HID event
 * system).
 *
 * backboardd watches for IOService objects with HIDServiceSupport = true,
 * opens them with IOServiceOpen type 'esuc' (IOHIDEventServiceUserClient,
 * compiled here from IOHIDFamily-1915.140.3, the version the iOS 16.7
 * userland ships) and reads serialized IOHIDEvents from the user client's
 * shared queue. This file has the minimal IOHIDEventService base class and
 * two services:
 *
 *   SweetHIDButtons  consumer-control keys (power, volume, home)
 *   SweetHIDTouch    the built-in touch screen (digitizer collections)
 *
 * Hardware drivers feed them through the C entry points sweet_hid_key()
 * and sweet_hid_touch(). sweet_hid_ringer() reports the ring/silent switch
 * (Nord2 alert slider) on the buttons service. Boot-arg sweethidtest=N (seconds) presses Home
 * every N seconds, to test the event path without input hardware.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSArray.h>
#include <pexpert/pexpert.h>

#include "IOHIDEventService.h"
#include "IOHIDEventServiceUserClient.h"
#include "IOHIDEvent.h"
#include "IOHIDKeys.h"
#include "IOHIDPrivateKeys.h"
#include "IOHIDUsageTables.h"
#include "../sweet_fb_cutout.h"     /* 0550: touch follows the punch-hole band rotation */
#include <kern/thread_call.h>       /* 0760: sensor guard */
#include <kern/clock.h>             /* 0760: absolutetime_to_nanoseconds */

extern "C" uint64_t mach_absolute_time(void);

/* One open client: the user client and its event callback. */
class IOHIDEventServiceClient : public OSObject {
	OSDeclareDefaultStructors(IOHIDEventServiceClient);
public:
	bool                       initClient() { return OSObject::init(); }
	IOService                 *client;
	void                      *context;
	IOHIDEventService::Action  action;
};
OSDefineMetaClassAndStructors(IOHIDEventServiceClient, OSObject);

#define super IOService
OSDefineMetaClassAndStructors(IOHIDEventService, IOService);

bool
IOHIDEventService::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	_clientLock = IOLockAlloc();
	_clients = OSArray::withCapacity(2);
	_hidWorkLoop = getWorkLoop();
	if (!_clientLock || !_clients || !_hidWorkLoop) {
		return false;
	}
	_hidWorkLoop->retain();
	_hidGate = IOCommandGate::commandGate(this);
	if (!_hidGate || _hidWorkLoop->addEventSource(_hidGate) != kIOReturnSuccess) {
		return false;
	}

	OSString *transport = getTransport();
	OSString *product = getProduct();
	setProperty(kIOHIDTransportKey, transport);
	setProperty(kIOHIDProductKey, product);
	setProperty(kIOHIDManufacturerKey, "Sweet");
	setProperty(kIOHIDVendorIDKey, getVendorID(), 32);
	setProperty(kIOHIDProductIDKey, getProductID(), 32);
	setProperty(kIOHIDLocationIDKey, (unsigned long long)getProductID(), 32);
	setProperty(kIOHIDReportIntervalKey, getReportInterval(), 32);
	setProperty(kIOHIDPrimaryUsagePageKey, getPrimaryUsagePage(), 32);
	setProperty(kIOHIDPrimaryUsageKey, getPrimaryUsage(), 32);
	setProperty(kIOHIDBuiltInKey, kOSBooleanTrue);
	OSSafeReleaseNULL(transport);
	OSSafeReleaseNULL(product);

	OSDictionary *pair = OSDictionary::withCapacity(2);
	OSArray *pairs = OSArray::withCapacity(1);
	OSNumber *page = OSNumber::withNumber(getPrimaryUsagePage(), 32);
	OSNumber *usage = OSNumber::withNumber(getPrimaryUsage(), 32);
	if (pair && pairs && page && usage) {
		pair->setObject(kIOHIDDeviceUsagePageKey, page);
		pair->setObject(kIOHIDDeviceUsageKey, usage);
		pairs->setObject(pair);
		setProperty(kIOHIDDeviceUsagePairsKey, pairs);
	}
	OSSafeReleaseNULL(pair);
	OSSafeReleaseNULL(pairs);
	OSSafeReleaseNULL(page);
	OSSafeReleaseNULL(usage);

	/* What IOHIDFamily's "IOHIDEventServiceUserClient" personality merges in. */
	setProperty("HIDServiceSupport", kOSBooleanTrue);
	setProperty("IOUserClientClass", "IOHIDEventServiceUserClient");
	return true;
}

void
IOHIDEventService::free(void)
{
	if (_hidGate && _hidWorkLoop) {
		_hidWorkLoop->removeEventSource(_hidGate);
	}
	OSSafeReleaseNULL(_hidGate);
	OSSafeReleaseNULL(_hidWorkLoop);
	OSSafeReleaseNULL(_clients);
	if (_clientLock) {
		IOLockFree(_clientLock);
		_clientLock = NULL;
	}
	super::free();
}

bool
IOHIDEventService::open(IOService *client, IOOptionBits options, void *context, Action action)
{
	IOHIDEventServiceClient *data = OSTypeAlloc(IOHIDEventServiceClient);
	bool ok = false;

	if (data && data->initClient()) {
		data->client = client;
		data->context = context;
		data->action = action;
		ok = super::open(client, options, data);
	}
	OSSafeReleaseNULL(data);
	IOLog("SWEET HID: %s open by %s -> %d\n", getName(), client ? client->getName() : "?", ok);
	return ok;
}

void
IOHIDEventService::close(IOService *forClient, IOOptionBits options)
{
	super::close(forClient, options);
}

bool
IOHIDEventService::handleOpen(IOService *client, IOOptionBits options __unused, void *argument)
{
	IOHIDEventServiceClient *data = OSDynamicCast(IOHIDEventServiceClient, (OSObject *)argument);
	bool ok = false;

	if (!data) {
		return false;
	}
	IOLockLock(_clientLock);
	if (!handleIsOpenLocked(client)) {
		/* Copy on write: dispatchEvent iterates a snapshot without the lock. */
		OSArray *next = OSArray::withArray(_clients, _clients->getCount() + 1);
		if (next && next->setObject(data)) {
			_clients->release();
			_clients = next;
			ok = true;
		} else {
			OSSafeReleaseNULL(next);
		}
	}
	IOLockUnlock(_clientLock);
	return ok;
}

void
IOHIDEventService::handleClose(IOService *client, IOOptionBits options __unused)
{
	IOLockLock(_clientLock);
	OSArray *next = OSArray::withCapacity(_clients->getCount());
	if (next) {
		for (unsigned int i = 0; i < _clients->getCount(); i++) {
			IOHIDEventServiceClient *data = (IOHIDEventServiceClient *)_clients->getObject(i);
			if (data->client != client) {
				next->setObject(data);
			}
		}
		_clients->release();
		_clients = next;
	}
	IOLockUnlock(_clientLock);
}

bool
IOHIDEventService::handleIsOpenLocked(const IOService *client) const
{
	for (unsigned int i = 0; i < _clients->getCount(); i++) {
		IOHIDEventServiceClient *data = (IOHIDEventServiceClient *)_clients->getObject(i);
		if (client == NULL || data->client == client) {
			return true;
		}
	}
	return false;
}

bool
IOHIDEventService::handleIsOpen(const IOService *client) const
{
	IOLockLock(_clientLock);
	bool open = handleIsOpenLocked(client);
	IOLockUnlock(_clientLock);
	return open;
}

void
IOHIDEventService::dispatchEvent(IOHIDEvent *event, IOOptionBits options)
{
	OSArray *clients;

	event->setSenderID(getRegistryEntryID());
	IOLockLock(_clientLock);
	clients = _clients;
	clients->retain();
	IOLockUnlock(_clientLock);

	for (unsigned int i = 0; i < clients->getCount(); i++) {
		IOHIDEventServiceClient *data = (IOHIDEventServiceClient *)clients->getObject(i);
		if (data->action) {
			(*data->action)(data->client, this, data->context, event, options);
		}
	}
	clients->release();
}

IOHIDEvent *
IOHIDEventService::copyEvent(IOHIDEventType type __unused, IOHIDEvent *matching __unused, IOOptionBits options __unused)
{
	return NULL;
}

IOHIDEvent *
IOHIDEventService::copyMatchingEvent(OSDictionary *matching __unused)
{
	return NULL;
}

IOReturn
IOHIDEventService::setElementValue(UInt32 usagePage __unused, UInt32 usage __unused, UInt32 value __unused)
{
	return kIOReturnUnsupported;
}

IOReturn
IOHIDEventService::setProperties(OSObject *properties)
{
	/* The event system pushes its settings here; nothing to configure. */
	return OSDynamicCast(OSDictionary, properties) ? kIOReturnSuccess : kIOReturnBadArgument;
}

UInt32
IOHIDEventService::getPrimaryUsagePage()
{
	return 0;
}

UInt32
IOHIDEventService::getPrimaryUsage()
{
	return 0;
}

OSString *
IOHIDEventService::getTransport()
{
	return OSString::withCString("SPU");
}

OSString *
IOHIDEventService::getProduct()
{
	return OSString::withCString(getName());
}

UInt32
IOHIDEventService::getVendorID()
{
	return 0x05ac;
}

UInt32
IOHIDEventService::getProductID()
{
	return 0;
}

UInt32
IOHIDEventService::getReportInterval()
{
	return 10000;	/* us */
}

IOReturn
IOHIDEventService::newUserClient(task_t owningTask, void *securityID, UInt32 type,
    OSDictionary *properties, IOUserClient **handler)
{
	IOUserClient *client;

	if (type != kIOHIDEventServiceUserClientType) {
		IOLog("SWEET HID: %s user client type 0x%x unsupported\n", getName(), (unsigned)type);
		return super::newUserClient(owningTask, securityID, type, properties, handler);
	}
	client = OSTypeAlloc(IOHIDEventServiceUserClient);
	if (!client) {
		return kIOReturnNoMemory;
	}
	if (!client->initWithTask(owningTask, securityID, type)) {
		client->release();
		IOLog("SWEET HID: %s user client init refused\n", getName());
		return kIOReturnBadArgument;
	}
	if (!client->attach(this)) {
		client->release();
		return kIOReturnUnsupported;
	}
	if (!client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnUnsupported;
	}
	*handler = client;
	return kIOReturnSuccess;
}

/*
 * Buttons. iOS takes the side/volume/home buttons as consumer-control
 * keyboard events (AppleM68Buttons publishes the same usage pair).
 */
class SweetHIDButtons : public IOHIDEventService {
	OSDeclareDefaultStructors(SweetHIDButtons);
public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void key(UInt32 usage, bool down);
	void ringer(bool ringOn);
#if defined(NORD2)
	void publishRingerSwitch(void);
#endif
	virtual IOHIDEvent *copyEvent(IOHIDEventType type, IOHIDEvent *matching, IOOptionBits options) APPLE_KEXT_OVERRIDE;
protected:
	virtual UInt32 getPrimaryUsagePage() APPLE_KEXT_OVERRIDE { return kHIDPage_Consumer; }
	virtual UInt32 getPrimaryUsage() APPLE_KEXT_OVERRIDE { return kHIDUsage_Csmr_ConsumerControl; }
	virtual UInt32 getProductID() APPLE_KEXT_OVERRIDE { return 1; }
private:
	static void testFired(OSObject *owner, IOTimerEventSource *sender);
	IOTimerEventSource *fTest;
	UInt32 fTestSecs;
	UInt32 fTestCount;
};
OSDefineMetaClassAndStructors(SweetHIDButtons, IOHIDEventService);

/*
 * Touch screen: digitizer collection events with finger children,
 * coordinates normalized to 0..1 like IOHIDEventDriver produces for
 * absolute digitizers.
 */
#define SWEET_TOUCH_FINGERS 10
/*
 * Contacts that start this close to a panel edge (pixels, ~3 mm) carry
 * kIOHIDDigitizerEventFromEdgeTip for their whole life, as Apple's touch
 * firmware reports them. UIKit derives UITouch's edge type from that bit
 * (_UIEventHIDTouchEdgeTypeForHIDEvent); without it there is no home or
 * app-switcher gesture.
 */
#define SWEET_TOUCH_EDGE_PX 48
class SweetHIDTouch : public IOHIDEventService {
	OSDeclareDefaultStructors(SweetHIDTouch);
public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void touch(int finger, bool down, int x, int y);
	void proximity(bool near);
	virtual IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
protected:
	virtual UInt32 getPrimaryUsagePage() APPLE_KEXT_OVERRIDE { return kHIDPage_Digitizer; }
	virtual UInt32 getPrimaryUsage() APPLE_KEXT_OVERRIDE { return kHIDUsage_Dig_TouchScreen; }
	virtual UInt32 getProductID() APPLE_KEXT_OVERRIDE { return 2; }
private:
	IOLock *fLock;
	int fLogicalW, fLogicalH, fOffX, fOffY;
	bool fDown[SWEET_TOUCH_FINGERS];
	bool fEdge[SWEET_TOUCH_FINGERS];
	IOFixed fX[SWEET_TOUCH_FINGERS];
	IOFixed fY[SWEET_TOUCH_FINGERS];
};
OSDefineMetaClassAndStructors(SweetHIDTouch, IOHIDEventService);

/*
 * 0284: the SCP's sensors (nord2_scp.inc) as Apple-vendor sensor services, the usage pairs iOS matches its built-in
 * sensors by (AppleHIDUsageTables.h: page 0xFF00, usage 0x08 proximity, 0x04 ambient light). Proximity: backboardd's
 * proximity processor (BKHIDProximityEventProcessor) takes kIOHIDEventTypeProximity events from it; near = the
 * receiver covered (kIOHIDProximityDetectionReceiver | LargeBodyContact), far = mask 0. Until the SCP reports, the
 * state is "far", so a call on the receiver never leaves the touch screen ignored (q10eb..q10ed: callservicesd asked
 * for proximity mode "active", backboardd put the digitizer in faceExpected mode and nothing ever said "far").
 * The state is answered on copyEvent and dispatched again whenever backboardd sets the service's properties
 * (its proximity detection mode).
 */
#ifndef N2_SENSOR_MASK
#define N2_SENSOR_MASK          1       /* bit 0 proximity, 1 light, 2 accelerometer, 3 gyro, 4 compass (q10ef: all five -> backboardd crash loop) */
#endif
#ifndef N2_PROX_DISPATCH_ON_SET
#define N2_PROX_DISPATCH_ON_SET 0
#endif
/*
 * 0760 (sensors-20261115): light, accelerometer, gyroscope and compass services are published too (bits 1-4), the proximity
 * service is left exactly as it was (bit 0 = N2_SENSOR_MASK).  The consumers [V-disasm locationd 20H392]: CLAccelerometer matches
 * usage pair 0xFF00/0x03 + "Built-In", reads kIOHIDEventTypeAccelerometer X/Y/Z as floats (g) and drops type 1 (shake); CLGyro
 * 0xFF00/0x09 kIOHIDEventTypeGyro; CLMagnetometer 0xFF00/0x0A kIOHIDEventTypeCompass; CLOrientationNotifier (behind backboardd's
 * CMDeviceOrientationManager = the auto-rotation) needs both the accelerometer and the gyro; locationd sets "ReportInterval" (us)
 * / "BatchInterval" / "MaxFIFOEvents" on the service through the event system.  CoreBrightness reads ALS events' level as a float.
 * The SPU compass HID plugins match only services with "compass-acc-compensation" / "compass-disp-coex" (not published).
 * Values arrive already in iOS units (nord2_scp.inc 0760: IOFixed g, rad/s, uT).
 * Guard: q10ef showed that a sensor service backboardd cannot digest puts it in a crash loop.  Every start of backboardd opens
 * each service once, so a new service opened N2_SENSOR_GUARD_OPENS times within N2_SENSOR_GUARD_WINDOW_S seconds removes all
 * the 0760 services again (terminate, logged "SWEET HID: GUARD"); proximity stays.
 * Switches: N2_SENSOR_MASK_NEW at build time, n2gdv bit 18 (262144) at boot = none of the new services, "nord2sensors=0" = none
 * at all (0284), kern.n2scp_motion=0 = no motion sampling (the services stay, silent).
 */
/*
 * 0761 (sensors-20261115 r2, after the q10jfinal25 phone run: accelerometer streams but iOS does not rotate): locationd's
 * CLOrientationNotifier [V-disasm locationd 20H392, numberOfSpectatorsChanged 0x100107540 / onIohidEvent 0x10010a12c] always
 * opens a HID service with usage pair 0x20/0x8A (HID sensor page, "device orientation") + Built-In and, on devices where the
 * orientation comes from the always-on processor, takes kIOHIDEventTypeOrientation (10) events from it: field 0xA0004
 * (DeviceOrientationUsage) in 0x64..0x6A (unknown, portrait, upside down, landscape left, landscape right, face up, face down =
 * UIDeviceOrientation + 0x64), orientation type CMUsage (1); the optional first child (vendor page 0xFF15 usage 2, 8 data bytes)
 * is the AOP time stamp.  Our phone is an iPhone10,3 to iOS, so that service is published (bit 5, "device-orientation") and fed
 * by nord2_scp.inc's detector on the accelerometer; the accelerometer / gyro services stay for the AP-side detector and CoreMotion.
 * "ReportInterval" -1 (0xFFFFFFFF, seen on the phone) now means "not configured" instead of 1 us.
 */
#ifndef N2_SENSOR_MASK_NEW
#define N2_SENSOR_MASK_NEW      0x3E
#endif
#ifndef N2_SENSOR_GUARD_OPENS
#define N2_SENSOR_GUARD_OPENS   3
#define N2_SENSOR_GUARD_WINDOW_S 300
#endif
class SweetHIDSensor : public IOHIDEventService {
	OSDeclareDefaultStructors(SweetHIDSensor);
public:
	bool initSensor(UInt32 usage, const char *name, UInt32 page = 0xFF00);
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
	virtual IOHIDEvent *copyEvent(IOHIDEventType type, IOHIDEvent *matching, IOOptionBits options) APPLE_KEXT_OVERRIDE;
	void report(SInt32 value);
	void motion(SInt32 x, SInt32 y, SInt32 z);      /* accelerometer / gyro / compass, IOFixed iOS units (0760) */
	IOHIDEvent *current(void);
	int wantHz(int defaultHz);                      /* 0760 */
	void status(char *buf, int len);                /* 0760 */
	UInt32 usage(void) const { return fUsage; }
protected:
	virtual bool handleOpen(IOService *client, IOOptionBits options, void *argument) APPLE_KEXT_OVERRIDE;
protected:
	virtual UInt32 getPrimaryUsagePage() APPLE_KEXT_OVERRIDE { return fPage; }
	virtual UInt32 getPrimaryUsage() APPLE_KEXT_OVERRIDE { return fUsage; }
	virtual UInt32 getProductID() APPLE_KEXT_OVERRIDE { return 0x10 + fUsage; }
	virtual OSString *getProduct() APPLE_KEXT_OVERRIDE { return OSString::withCString(fName); }
private:
	UInt32 fUsage;
	UInt32 fPage;                   /* 0761: 0xFF00, or 0x20 for the device orientation service */
	const char *fName;
	volatile SInt32 fValue;         /* proximity: 1 near, 0 far; light: lux */
	volatile SInt32 fXYZ[3];        /* motion: last sample, IOFixed g / rad/s / uT (0760) */
	bool fHaveXYZ;
	UInt32 fSaid;
	volatile UInt32 fInterval;      /* 0760: last "ReportInterval" (us) set by iOS */
	volatile bool fIntervalSet;
	UInt32 fOpens, fSaidProps;
	UInt64 fOpenT[N2_SENSOR_GUARD_OPENS];
	volatile UInt32 fEvents;
};
OSDefineMetaClassAndStructors(SweetHIDSensor, IOHIDEventService);

static SweetHIDButtons *gSweetButtons;
static SweetHIDTouch *gSweetTouch;
static SweetHIDSensor *gSweetProx, *gSweetALS, *gSweetAccel, *gSweetGyro, *gSweetCompass;
static SweetHIDSensor *gSweetOrient;           /* 0761: 0x20/0x8A device orientation */
#define N2_ORIENT_USAGE 0x8A                    /* 0761: fUsage of the orientation service (page 0x20) */

bool
SweetHIDSensor::initSensor(UInt32 usage, const char *name, UInt32 page)
{
	fUsage = usage;
	fPage = page;
	fName = name;
	fValue = usage == 0x08 ? 0 : -1;
	return init(NULL);
}

bool
SweetHIDSensor::start(IOService *provider)
{
	if (!IOHIDEventService::start(provider)) {
		return false;
	}
	if (fUsage == 0x08) {
		/* q10f0 logs: backboardd threw "-[NSNull applyUIMode:]" for our proximity service: its proximity wrapper (discrete
		 * or host-state) was not instantiated. The discrete one is driven through this property ("send mode %@/%d to
		 * proximity service", ProxDetectionMode): published, so the service looks like a discrete proximity sensor. */
		setProperty("ProxDetectionMode", 0ULL, 32);
		/* backboardd's proximity matcher (the block of -[BKHIDUISensorController _proximityServiceWrapper]) reads
		 * "Placement": 1 = "discrete prox service ... top placement" -> BKHIDUIDiscreteProximityServiceWrapper; anything
		 * else = "ignoring discrete prox service ... not top placement", a nil wrapper and -[NSNull applyUIMode:] */
		setProperty("Placement", 1ULL, 32);
	}
	registerService();
	IOLog("SWEET HID: %s sensor registered (0x%04x/0x%02x)\n", fName, (unsigned)fPage, (unsigned)fUsage);
	return true;
}

IOHIDEvent *
SweetHIDSensor::current(void)
{
	SInt32 v = fValue;
	if (fUsage == 0x08) {
		return IOHIDEvent::proximityEvent(mach_absolute_time(),
		    v > 0 ? (IOHIDProximityDetectionMask)(kIOHIDProximityDetectionReceiver | kIOHIDProximityDetectionLargeBodyContact) :
		    (IOHIDProximityDetectionMask)0, v > 0 ? 1000 : 0);
	}
	if (fPage == 0x20 && fUsage == N2_ORIENT_USAGE) {     /* 0761: UIDeviceOrientation 0..6 -> usage 0x64.. */
		if (v < 0 || v > 6) {
			return NULL;
		}
		/* locationd notifyAOO drops an update whose sensor time is not newer than the last one ("Skipping stale orientation"):
		 * the time comes only from the first child, vendor page 0xFF15 usage 2 with 8 data bytes, so it is always attached */
		uint64_t now = mach_absolute_time();
		IOHIDEvent *e = IOHIDEvent::orientationEvent(now, kIOHIDOrientationTypeCMUsage);
		if (e) {
			e->setIntegerValue(kIOHIDEventFieldOrientationDeviceOrientationUsage, 0x64 + v);
			IOHIDEvent *t = IOHIDEvent::vendorDefinedEvent(now, 0xFF15, 2, 0, (UInt8 *)&now, (UInt32)sizeof(now));
			if (t) {
				e->appendChild(t);
				t->release();
			}
		}
		return e;
	}
	if (fUsage == 0x03 || fUsage == 0x09 || fUsage == 0x0A) {
		if (!fHaveXYZ) {
			return NULL;
		}
		/* 0760: already IOFixed in iOS units (g, rad/s, uT: nord2_scp.inc n2scp_motion_out) */
		IOFixed x = fXYZ[0], y = fXYZ[1], z = fXYZ[2];
		if (fUsage == 0x03) {
			return IOHIDEvent::accelerometerEvent(mach_absolute_time(), x, y, z);
		}
		if (fUsage == 0x09) {
			return IOHIDEvent::gyroEvent(mach_absolute_time(), x, y, z);
		}
		return IOHIDEvent::compassEvent(mach_absolute_time(), x, y, z);
	}
	if (v < 0) {
		return NULL;
	}
	return IOHIDEvent::ambientLightSensorEvent(mach_absolute_time(), (UInt32)v);
}

void
SweetHIDSensor::motion(SInt32 x, SInt32 y, SInt32 z)
{
	fXYZ[0] = x; fXYZ[1] = y; fXYZ[2] = z;
	fHaveXYZ = true;
	if (!_clients || _clients->getCount() == 0) {
		return;
	}
	IOHIDEvent *event = current();
	if (event) {
		if (fSaid < 3) {
			fSaid++;
			IOLog("SWEET HID: %s %d %d %d (IOFixed, %u clients)\n", fName, (int)x, (int)y, (int)z, _clients->getCount());
		}
		dispatchEvent(event);
		event->release();
		fEvents++;
	}
}

void
SweetHIDSensor::report(SInt32 value)
{
	fValue = value;
	IOHIDEvent *event = current();
	if (event) {
		if (fSaid < 20 || fUsage == 0x08) {
			fSaid++;
			IOLog("SWEET HID: %s %d (%u clients)\n", fName, (int)value, _clients ? _clients->getCount() : 0);
		}
		dispatchEvent(event);
		event->release();
	}
}

IOReturn
SweetHIDSensor::setProperties(OSObject *properties)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, properties);
	if (!dict) {
		return kIOReturnBadArgument;
	}
	if (fUsage != 0x08) {          /* 0760: what iOS asks of the light / motion services */
		OSNumber *ri = OSDynamicCast(OSNumber, dict->getObject("ReportInterval"));
		OSNumber *bi = OSDynamicCast(OSNumber, dict->getObject("BatchInterval"));
		if (ri) {
			UInt64 v = ri->unsigned64BitValue();
			if (v == 0xFFFFFFFFULL || v == ~0ULL || (SInt64)v < 0) {
				fIntervalSet = false;           /* 0761: -1 = not configured (seen on the gyro) */
			} else {
				fInterval = v > 0xFFFFFFFEULL ? 0xFFFFFFFEu : (UInt32)v;
				fIntervalSet = true;
			}
		}
		if (fPage == 0x20 && fValue >= 0) {      /* 0761: a (re)configuring locationd gets the current orientation at once */
			IOHIDEvent *e = current();
			if (e) {
				dispatchEvent(e);
				e->release();
			}
		}
		if (fSaidProps < 40) {
			fSaidProps++;
			IOLog("SWEET HID: %s properties set (%u keys): ReportInterval %lld BatchInterval %lld\n", fName, dict->getCount(),
			    ri ? (long long)ri->unsigned64BitValue() : -1LL, bi ? (long long)bi->unsigned64BitValue() : -1LL);
		}
		return kIOReturnSuccess;
	}
	if (fUsage == 0x08 && N2_PROX_DISPATCH_ON_SET) {
		IOLog("SWEET HID: proximity properties set (%u keys): state %s again\n", dict->getCount(), fValue > 0 ? "near" : "far");
		IOHIDEvent *event = current();
		if (event) {
			dispatchEvent(event);
			event->release();
		}
	}
	return kIOReturnSuccess;
}

IOHIDEvent *
SweetHIDSensor::copyEvent(IOHIDEventType type, IOHIDEvent *matching __unused, IOOptionBits options __unused)
{
	if (fPage == 0x20) {            /* 0761 */
		return type == kIOHIDEventTypeOrientation ? current() : NULL;
	}
	if ((fUsage == 0x08 && type != kIOHIDEventTypeProximity) || (fUsage == 0x04 && type != kIOHIDEventTypeAmbientLightSensor) ||
	    (fUsage == 0x03 && type != kIOHIDEventTypeAccelerometer) || (fUsage == 0x09 && type != kIOHIDEventTypeGyro) ||
	    (fUsage == 0x0A && type != kIOHIDEventTypeCompass)) {
		return NULL;
	}
	return current();
}

/* 0760: guard state, the thread call that removes the new services, the published mask */
static volatile UInt32 gSweetSensorGuard;      /* 0 armed, 1 tripped (services being removed), 2 removed */
static volatile UInt32 gSweetSensorMask;
static thread_call_t gSweetGuardCall;

static void
sweet_hid_guard_fire(thread_call_param_t p0 __unused, thread_call_param_t p1 __unused)
{
	SweetHIDSensor **list[5] = { &gSweetALS, &gSweetAccel, &gSweetGyro, &gSweetCompass, &gSweetOrient };
	for (int i = 0; i < 5; i++) {
		SweetHIDSensor *s = *list[i];
		if (!s) {
			continue;
		}
		*list[i] = NULL;
		IOLog("SWEET HID: GUARD: removing the %s service\n", s->getName());
		s->terminate();
	}
	gSweetSensorGuard = 2;
	IOLog("SWEET HID: GUARD: light/motion services removed (proximity kept); reboot to retry\n");
}

static bool
sweet_hid_guard_trip(void)
{
	UInt32 expected = 0;
	return __atomic_compare_exchange_n(&gSweetSensorGuard, &expected, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

bool
SweetHIDSensor::handleOpen(IOService *client, IOOptionBits options, void *argument)
{
	bool ok = IOHIDEventService::handleOpen(client, options, argument);
	if (!ok || fUsage == 0x08) {
		return ok;
	}
	uint64_t now, ns;
	now = mach_absolute_time();
	absolutetime_to_nanoseconds(now, &ns);
	UInt64 t = ns / 1000000000ULL;
	fOpenT[fOpens % N2_SENSOR_GUARD_OPENS] = t;
	fOpens++;
	IOLog("SWEET HID: %s opened (open #%u by %s)\n", fName, fOpens, client ? client->getName() : "?");
	if (fOpens >= N2_SENSOR_GUARD_OPENS) {
		UInt64 oldest = fOpenT[fOpens % N2_SENSOR_GUARD_OPENS];       /* the N-th last open */
		if (t - oldest <= N2_SENSOR_GUARD_WINDOW_S && sweet_hid_guard_trip()) {
			IOLog("SWEET HID: GUARD: %s opened %u times in %llu s (client restarting = backboardd crash loop?): removing the new services\n",
			    fName, (unsigned)N2_SENSOR_GUARD_OPENS, (unsigned long long)(t - oldest));
			if (gSweetGuardCall) {
				thread_call_enter(gSweetGuardCall);
			}
		}
	}
	return ok;
}

int
SweetHIDSensor::wantHz(int defaultHz)
{
	if (fIntervalSet) {
		UInt32 us = fInterval;
		if (us == 0) {
			return 0;
		}
		return (int)(1000000u / us) > 0 ? (int)(1000000u / us) : 1;
	}
	/* nobody has configured the service: stream while it has a client (the event system's open) */
	return (_clients && _clients->getCount() > 0) ? defaultHz : 0;
}

void
SweetHIDSensor::status(char *buf, int len)
{
	snprintf(buf, (size_t)len, "%s c%u ri%s%u o%u e%u", fName, _clients ? _clients->getCount() : 0u, fIntervalSet ? "" : "?",
	    (unsigned)fInterval, (unsigned)fOpens, (unsigned)fEvents);
}

static SweetHIDSensor *
sweet_hid_new_sensor(IOService *provider, UInt32 usage, const char *name, UInt32 page = 0xFF00)
{
	SweetHIDSensor *s = OSTypeAlloc(SweetHIDSensor);
	if (!s || !s->initSensor(usage, name, page) || !s->attach(provider)) {
		OSSafeReleaseNULL(s);
		return NULL;
	}
	if (!s->start(provider)) {
		s->detach(provider);
		s->release();
		return NULL;
	}
	return s;       /* the registry holds it from here; our reference is the global */
}

/*
 * Ring/silent switch. iOS 16.7 backboardd (ringer processor,
 * -processEvent:sender:dispatcher:) takes a keyboard event with usage page
 * 0x0B (Telephony) and usage 0x2E (kHIDUsage_Tfon_Ring): ringer state =
 * (KeyboardDown != 0), 1 = "tone1" (ringer on), 0 = "silent". At start it
 * asks the services for the current state with a keyboard copyEvent
 * matching 0x0B/0x2E; without an answer its state stays 0 (silent).
 * -1 = no hardware reading yet, 0 = silent, 1 = ringer on.
 */
static volatile SInt32 gSweetRinger = -1;

bool
SweetHIDButtons::start(IOService *provider)
{
	if (!IOHIDEventService::start(provider)) {
		return false;
	}
	if (PE_parse_boot_argn("sweethidtest", &fTestSecs, sizeof(fTestSecs)) && fTestSecs) {
		fTest = IOTimerEventSource::timerEventSource(this, &SweetHIDButtons::testFired);
		if (fTest && _hidWorkLoop->addEventSource(fTest) == kIOReturnSuccess) {
			fTest->setTimeoutMS(fTestSecs * 1000);
		}
	}
	gSweetButtons = this;
#if defined(NORD2)
	publishRingerSwitch();
	{
		int sensors = 1;
		(void)PE_parse_boot_argn("nord2sensors", &sensors, sizeof(sensors));
		if (sensors) {
			/* 0760: the new services unless n2gdv bit 18 says no; the proximity line is 0284's */
			int gdv = 0;
			UInt32 mask = N2_SENSOR_MASK | N2_SENSOR_MASK_NEW;
			if (PE_parse_boot_argn("n2gdv", &gdv, sizeof(gdv)) && (gdv & (1 << 18))) {
				mask = N2_SENSOR_MASK;
			}
			gSweetGuardCall = thread_call_allocate(sweet_hid_guard_fire, NULL);
			if (N2_SENSOR_MASK & 1) { gSweetProx = sweet_hid_new_sensor(provider, 0x08, "proximity"); }
			if (mask & 2) { gSweetALS = sweet_hid_new_sensor(provider, 0x04, "ambient-light"); }
			if (mask & 4) { gSweetAccel = sweet_hid_new_sensor(provider, 0x03, "accelerometer"); }
			if (mask & 8) { gSweetGyro = sweet_hid_new_sensor(provider, 0x09, "gyro"); }
			if (mask & 16) { gSweetCompass = sweet_hid_new_sensor(provider, 0x0A, "compass"); }
			if (mask & 32) { gSweetOrient = sweet_hid_new_sensor(provider, N2_ORIENT_USAGE, "device-orientation", 0x20); }   /* 0761 */
			gSweetSensorMask = (gSweetProx ? 1u : 0u) | (gSweetALS ? 2u : 0u) | (gSweetAccel ? 4u : 0u) | (gSweetGyro ? 8u : 0u) |
			    (gSweetCompass ? 16u : 0u) | (gSweetOrient ? 32u : 0u);
			IOLog("SWEET HID: sensors published mask %#x (asked %#x, guard %u opens / %u s)\n", (unsigned)gSweetSensorMask, (unsigned)mask,
			    (unsigned)N2_SENSOR_GUARD_OPENS, (unsigned)N2_SENSOR_GUARD_WINDOW_S);
		}
	}
#endif
	registerService();
	IOLog("SWEET HID: buttons registered (test every %u s)\n", fTestSecs);
	return true;
}

#if defined(NORD2)
/*
 * MobileGestalt "ringer-switch" (libMobileGestalt: IODeviceTree:/buttons has a
 * property named "function-button_ringerab", key hash Ah5yfSlY2yycIQb92HbmrA).
 * SpringBoard's -_updateRingerState:withVisuals:updatePreferenceRegister:
 * returns at once when that answer is false: no mute state, no HUD, no
 * com.apple.springboard.ringerstate update. The ADT /buttons node lacks it,
 * so add it here, before userland. Value as in the D22AP ADT (phandle
 * 0x4e + 'RntbNIRb'); only its presence is read. nord2slider=0 skips it.
 */
void
SweetHIDButtons::publishRingerSwitch(void)
{
	int slider = 1;
	(void)PE_parse_boot_argn("nord2slider", &slider, sizeof(slider));
	if (!slider) {
		return;
	}
	IORegistryEntry *buttons = IORegistryEntry::fromPath("/buttons", gIODTPlane);
	if (!buttons) {
		IOLog("SWEET HID: no IODeviceTree:/buttons; ringer-switch not published\n");
		return;
	}
	if (!buttons->getProperty("function-button_ringerab")) {
		static const UInt8 value[12] = {0x4e, 0, 0, 0, 'R', 'n', 't', 'b', 'N', 'I', 'R', 'b'};
		OSData *data = OSData::withBytes(value, sizeof(value));
		if (data) {
			buttons->setProperty("function-button_ringerab", data);
			data->release();
		}
	}
	IOLog("SWEET HID: IODeviceTree:/buttons function-button_ringerab %s\n",
	    buttons->getProperty("function-button_ringerab") ? "present" : "missing");
	buttons->release();
}
#endif

/*
 * 0640: delivery counters for the USB autotest status (nord2_autotest.inc words 24..29): every dispatched HID report is counted
 * here, so injected gestures / keys can be confirmed without the log.  [0] injected touch reports (fingers >= 8), [1] injected
 * down transitions | key reports << 16, [2] all touch reports, [5] injected reports dispatched while the touch service had no
 * client (not delivered to anyone).  Relaxed atomics, no lock.
 */
#define SWEET_HID_INJECTED_FINGER 8
static uint32_t sweet_hid_cnt[6], sweet_hid_touch_clients, sweet_hid_btn_clients;
static void
sweet_hid_note_touch(int finger, bool went_down, int x, int y, unsigned clients)
{
	__atomic_fetch_add(&sweet_hid_cnt[2], 1u, __ATOMIC_RELAXED);
	__atomic_store_n(&sweet_hid_touch_clients, clients, __ATOMIC_RELAXED);
	if (finger < SWEET_HID_INJECTED_FINGER) { return; }
	__atomic_fetch_add(&sweet_hid_cnt[0], 1u, __ATOMIC_RELAXED);
	if (went_down) { __atomic_fetch_add(&sweet_hid_cnt[1], 1u, __ATOMIC_RELAXED); }
	if (clients == 0) { __atomic_fetch_add(&sweet_hid_cnt[5], 1u, __ATOMIC_RELAXED); }
	__atomic_store_n(&sweet_hid_cnt[4], ((uint32_t)x & 0xffffu) | ((uint32_t)y << 16), __ATOMIC_RELAXED);
}

extern "C" void sweet_hid_counts(uint32_t c[6]);
extern "C" void
sweet_hid_counts(uint32_t c[6])
{
	for (unsigned i = 0; i < 6; i++) { c[i] = __atomic_load_n(&sweet_hid_cnt[i], __ATOMIC_RELAXED); }
	c[3] = (__atomic_load_n(&sweet_hid_touch_clients, __ATOMIC_RELAXED) & 0xffffu) | (__atomic_load_n(&sweet_hid_btn_clients, __ATOMIC_RELAXED) << 16);
}

void
SweetHIDButtons::key(UInt32 usage, bool down)
{
	IOHIDEvent *event = IOHIDEvent::keyboardEvent(mach_absolute_time(), kHIDPage_Consumer, usage, down);

	if (event) {
		IOLog("SWEET HID: key 0x%x %s (%u clients)\n", (unsigned)usage, down ? "down" : "up",
		    _clients ? _clients->getCount() : 0);
		dispatchEvent(event);
		event->release();
		__atomic_fetch_add(&sweet_hid_cnt[1], 1u << 16, __ATOMIC_RELAXED);      /* 0640: key events dispatched */
		__atomic_store_n(&sweet_hid_btn_clients, _clients ? _clients->getCount() : 0u, __ATOMIC_RELAXED);
	}
}

void
SweetHIDButtons::ringer(bool ringOn)
{
	IOHIDEvent *event = IOHIDEvent::keyboardEvent(mach_absolute_time(), kHIDPage_Telephony,
	    kHIDUsage_Tfon_Ring, ringOn);

	if (event) {
		IOLog("SWEET HID: ringer %s (%u clients)\n", ringOn ? "on" : "silent",
		    _clients ? _clients->getCount() : 0);
		dispatchEvent(event);
		event->release();
	}
}

IOHIDEvent *
SweetHIDButtons::copyEvent(IOHIDEventType type, IOHIDEvent *matching, IOOptionBits options __unused)
{
	SInt32 state = gSweetRinger;

	if (type != kIOHIDEventTypeKeyboard || state < 0) {
		return NULL;
	}
	if (matching && (matching->getType() != kIOHIDEventTypeKeyboard ||
	    (UInt32)matching->getIntegerValue(kIOHIDEventFieldKeyboardUsagePage) != kHIDPage_Telephony ||
	    (UInt32)matching->getIntegerValue(kIOHIDEventFieldKeyboardUsage) != kHIDUsage_Tfon_Ring)) {
		return NULL;
	}
	return IOHIDEvent::keyboardEvent(mach_absolute_time(), kHIDPage_Telephony, kHIDUsage_Tfon_Ring,
	           state != 0);
}

void
SweetHIDButtons::testFired(OSObject *owner, IOTimerEventSource *sender)
{
	SweetHIDButtons *self = (SweetHIDButtons *)owner;

	/*
	 * Home (Menu) press, then the release 100 ms later; Setup's "Hello"
	 * screen waits for it. Even counts press, odd counts release.
	 */
	bool press = (self->fTestCount & 1) == 0;
	self->key(kHIDUsage_Csmr_Menu, press);
	if (++self->fTestCount < 60) {
		sender->setTimeoutMS(press ? 100 : self->fTestSecs * 1000);
	}
}

bool
SweetHIDTouch::start(IOService *provider)
{
	fLock = IOLockAlloc();
	if (!fLock || !IOHIDEventService::start(provider)) {
		return false;
	}
	int native = 0;
	PE_parse_boot_argn("sweetfb_native", &native, sizeof(native));
	fLogicalW = native ? 1080 : 1125;
	fLogicalH = native ? 2400 : 2436;
	fOffX = (fLogicalW - 1080) / 2;
	fOffY = (fLogicalH - 2400) / 2;
	/* The digitizer covers the main display (backboardd maps it 1:1). */
	setProperty("DisplayIntegrated", kOSBooleanTrue);
	setProperty("SurfaceWidth", (unsigned long long)(68 << 16), 32);	/* mm, 16.16 */
	setProperty("SurfaceHeight", (unsigned long long)(151 << 16), 32);
	gSweetTouch = this;
	registerService();
	IOLog("SWEET HID: touch registered\n");
	return true;
}

void
SweetHIDTouch::touch(int finger, bool down, int x, int y)
{
	uint64_t now = mach_absolute_time();
	IOHIDEvent *collection;
	bool any = false;

	if (finger < 0 || finger >= SWEET_TOUCH_FINGERS) {
		return;
	}
	x = x < 0 ? 0 : (x > 1079 ? 1079 : x);
	y = y < 0 ? 0 : (y > 2399 ? 2399 : y);
	if (__atomic_load_n(&sweet_fb_cut_live, __ATOMIC_RELAXED) != 0u) {      /* 0550: the status bar band is shown rotated (sweet_fb_cutout.h) */
		struct sweet_fb_cut_cfg cut = sweet_fb_cut_g;
		int32_t cx = x, cy = y;
		sweet_fb_cut_touch(&cut, 1080u, &cx, &cy);
		x = cx;
	}

	IOLockLock(fLock);
	bool was = fDown[finger];
	/* Panel pixels -> the presented 1125x2436 display (SweetFramebuffer.cpp). */
	IOFixed nx = (IOFixed)(((int64_t)(x + fOffX) << 16) / fLogicalW);
	IOFixed ny = (IOFixed)(((int64_t)(y + fOffY) << 16) / fLogicalH);
	bool moved = down && (nx != fX[finger] || ny != fY[finger]);
	if (was == down && !moved) {
		IOLockUnlock(fLock);
		return;
	}
	if (!was && down) {
		fEdge[finger] = x < SWEET_TOUCH_EDGE_PX || x > 1079 - SWEET_TOUCH_EDGE_PX ||
		    y < SWEET_TOUCH_EDGE_PX || y > 2399 - SWEET_TOUCH_EDGE_PX;
	}

	collection = IOHIDEvent::digitizerEvent(now, 0, kIOHIDDigitizerTransducerTypeHand, false, 0, 0, 0);
	if (!collection) {
		IOLockUnlock(fLock);
		return;
	}
	collection->setIntegerValue(kIOHIDEventFieldDigitizerCollection, TRUE);

	UInt32 cmask = 0;
	for (int i = 0; i < SWEET_TOUCH_FINGERS; i++) {
		bool d = (i == finger) ? down : fDown[i];
		if (i != finger && !d) {
			continue;
		}
		IOFixed fx = (i == finger && down) ? nx : fX[i];
		IOFixed fy = (i == finger && down) ? ny : fY[i];
		IOHIDEvent *child = IOHIDEvent::digitizerEvent(now, (UInt32)i + 2, kIOHIDDigitizerTransducerTypeFinger,
		    d, d ? 1 : 0, fx, fy);
		if (!child) {
			continue;
		}
		UInt32 mask = fEdge[i] ? kIOHIDDigitizerEventFromEdgeTip : 0;
		if (i == finger) {
			if (was != down) {
				mask |= kIOHIDDigitizerEventTouch | kIOHIDDigitizerEventRange;
			}
			if (moved) {
				mask |= kIOHIDDigitizerEventPosition;
			}
			if (!was && down) {
				mask |= kIOHIDDigitizerEventIdentity;
			}
		}
		child->setIntegerValue(kIOHIDEventFieldDigitizerIndex, i);
		child->setIntegerValue(kIOHIDEventFieldDigitizerIdentity, i + 2);
		child->setIntegerValue(kIOHIDEventFieldDigitizerTouch, d);
		child->setIntegerValue(kIOHIDEventFieldDigitizerEventMask, mask);
		collection->appendChild(child);
		child->release();
		cmask |= mask;
		any |= d;
	}
	fDown[finger] = down;
	if (down) {
		fX[finger] = nx;
		fY[finger] = ny;
	}
	collection->setFixedValue(kIOHIDEventFieldDigitizerX, nx);
	collection->setFixedValue(kIOHIDEventFieldDigitizerY, ny);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerRange, any);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerTouch, any);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerEventMask, cmask);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerButtonMask, any ? 1 : 0);
	IOLockUnlock(fLock);

	unsigned clients = _clients ? _clients->getCount() : 0;
	if (was != down && finger >= SWEET_HID_INJECTED_FINGER) {      /* 0640: the USB autotest's fingers 8/9 (nord2_autotest.inc) */
		IOLog("SWEET HID: injected touch %d %s at %d,%d%s (%u clients)\n", finger, down ? "down" : "up", x, y, fEdge[finger] ? " edge" : "", clients);
	} else if (was != down) {
		IOLog("SWEET HID: touch %d %s at %d,%d%s\n", finger, down ? "down" : "up", x, y,
		    fEdge[finger] ? " edge" : "");
	}
	dispatchEvent(collection);
	collection->release();
	sweet_hid_note_touch(finger, down && !was, x, y, clients);      /* 0640 */
}

/*
 * 0284: proximity from the touch screen's own service. backboardd tells the digitizer its mode (faceExpected while a call
 * is on the receiver) and then ignores touches while it believes an object is in front of the screen; a separate
 * Apple-vendor proximity service made backboardd throw at once (q10eg..q10eu), so the SCP's proximity state is
 * dispatched here, as kIOHIDEventTypeProximity from the digitizer, and repeated on every property backboardd sets.
 */
static volatile SInt32 gSweetNear;              /* 0 far (also before the SCP says anything), 1 near */

void
SweetHIDTouch::proximity(bool near)
{
	IOHIDEvent *event = IOHIDEvent::proximityEvent(mach_absolute_time(),
	    near ? (IOHIDProximityDetectionMask)(kIOHIDProximityDetectionReceiver | kIOHIDProximityDetectionLargeBodyContact) :
	    (IOHIDProximityDetectionMask)0, near ? 1000 : 0);
	if (event) {
		dispatchEvent(event);
		event->release();
	}
}

IOReturn
SweetHIDTouch::setProperties(OSObject *properties)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, properties);
	static unsigned said;
	if (!dict) {
		return kIOReturnBadArgument;
	}
	if (said < 40) {
		said++;
		OSCollectionIterator *it = OSCollectionIterator::withCollection(dict);
		char keys[160];
		unsigned n = 0;
		keys[0] = 0;
		if (it) {
			OSSymbol *k;
			while ((k = OSDynamicCast(OSSymbol, it->getNextObject())) != NULL && n < sizeof(keys) - 24) {
				OSNumber *num = OSDynamicCast(OSNumber, dict->getObject(k));
				n += (unsigned)snprintf(keys + n, sizeof(keys) - n, "%s=%lld ", k->getCStringNoCopy(),
				    num ? (long long)num->unsigned64BitValue() : -1LL);
			}
			it->release();
		}
		IOLog("SWEET HID: touch properties set: %s(proximity %s)\n", keys, gSweetNear ? "near" : "far");
	}
	proximity(gSweetNear != 0);
	return kIOReturnSuccess;
}

/* key: 0 power (side button), 1 volume up, 2 volume down, 3 home */
extern "C" void
sweet_hid_key(int key, int down)
{
	static const UInt32 usages[] = {
		kHIDUsage_Csmr_Power, kHIDUsage_Csmr_VolumeIncrement,
		kHIDUsage_Csmr_VolumeDecrement, kHIDUsage_Csmr_Menu,
	};

	if (gSweetButtons && key >= 0 && key < (int)(sizeof(usages) / sizeof(usages[0]))) {
		gSweetButtons->key(usages[key], down != 0);
	}
}

/* ring_on: 1 ringer on, 0 silent. Every call dispatches one event. */
extern "C" void sweet_hid_ringer(int ring_on);
extern "C" void
sweet_hid_ringer(int ring_on)
{
	gSweetRinger = ring_on ? 1 : 0;
	if (gSweetButtons) {
		gSweetButtons->ringer(ring_on != 0);
	}
}

extern "C" void nord2_dvfs_touch_kick(void);      /* bsd/dev/nord2_cpufreq.c (0460): a finger event, the CPU and GPU governors boost */
extern "C" void
sweet_hid_touch(int finger, int down, int x, int y)
{
	nord2_dvfs_touch_kick();
	if (gSweetTouch) {
		gSweetTouch->touch(finger, down != 0, x, y);
	}
}

/* 0284: near 1 = something in front of the receiver, 0 = clear (nord2_scp.inc) */
extern "C" void sweet_hid_proximity(int near);
extern "C" void
sweet_hid_proximity(int near)
{
	gSweetNear = near ? 1 : 0;
	if (gSweetTouch) {
		gSweetTouch->proximity(near != 0);
		IOLog("SWEET HID: proximity %s (touch service)\n", near ? "near" : "far");
	}
	if (gSweetProx) {
		gSweetProx->report(near ? 1 : 0);
	}
}

extern "C" void sweet_hid_ambient(int lux);
extern "C" void
sweet_hid_ambient(int lux)
{
	if (gSweetALS && lux >= 0 && gSweetSensorGuard == 0) {
		gSweetALS->report(lux);
	}
}

/* 0284: kind 0 accelerometer, 1 gyro, 2 compass; values in the SCP hub's units */
extern "C" void sweet_hid_motion(int kind, int x, int y, int z);
extern "C" void
sweet_hid_motion(int kind, int x, int y, int z)
{
	SweetHIDSensor *s = kind == 0 ? gSweetAccel : kind == 1 ? gSweetGyro : gSweetCompass;
	if (s && gSweetSensorGuard == 0) {
		s->motion(x, y, z);
	}
}

/* 0760: Hz iOS wants from a motion service (0 none / no service), for nord2_scp.inc's on-demand enable */
extern "C" int sweet_hid_motion_want(int kind);
extern "C" int
sweet_hid_motion_want(int kind)
{
	static const int dflt[4] = { 50, 50, 25, 25 };
	SweetHIDSensor *s = kind == 0 ? gSweetAccel : kind == 1 ? gSweetGyro : kind == 2 ? gSweetCompass : kind == 3 ? gSweetOrient : NULL;
	if (!s || gSweetSensorGuard != 0) {
		return 0;
	}
	return s->wantHz(dflt[kind]);
}

/* 0761: device orientation (UIDeviceOrientation 0..6) for the 0x20/0x8A service; dispatched by report() (callers: on change) */
extern "C" void sweet_hid_orientation(int o);
extern "C" void
sweet_hid_orientation(int o)
{
	if (gSweetOrient && gSweetSensorGuard == 0 && o >= 0 && o <= 6) {
		gSweetOrient->report(o);
	}
}

/* 0760: one line for kern.n2scp_raw */
extern "C" void sweet_hid_sensor_status(char *buf, int len);
extern "C" void
sweet_hid_sensor_status(char *buf, int len)
{
	SweetHIDSensor *list[5] = { gSweetAccel, gSweetGyro, gSweetCompass, gSweetALS, gSweetOrient };
	int n = snprintf(buf, (size_t)len, "hid mask %#x guard %u", (unsigned)gSweetSensorMask, (unsigned)gSweetSensorGuard);
	for (int i = 0; i < 5 && n > 0 && n < len - 2; i++) {
		if (!list[i]) {
			continue;
		}
		buf[n++] = ' ';
		buf[n++] = '|';
		buf[n++] = ' ';
		if (n >= len) {
			break;
		}
		list[i]->status(buf + n, len - n);
		n += (int)strnlen(buf + n, (size_t)(len - n));
	}
	if (len > 0) {
		buf[len - 1] = 0;
	}
}
