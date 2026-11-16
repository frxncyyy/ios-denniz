/*
 * 0722 (bt-20261115): the IORegistry service "bluetooth" iOS looks for.
 *  - bluetoothd: FindLocalMacAddress reads "local-mac-address" from IOServiceGetMatchingService(IOServiceNameMatching(
 *    "bluetooth")) (else "No bluetooth on this device." and exit); the HCI transport is "transport-encoding" (4 bytes,
 *    3 = H4BC = kernel control + Skywalk, 0721) and "transport-speed" -- verified in the phone's iOS 16.7 bluetoothd
 *    (0x1000e7684: IOServiceNameMatching "marconi-bt" then "bluetooth", IORegistryEntrySearchCFProperty).
 *  - MobileBluetooth/BluetoothManager (Settings) attaches only when that service exists (HakiOS run 735).
 * Derived from HakiOS kernel/iokit/Kernel/RedwoodBluetooth.cpp ((c) 2026 The HakiOS authors, APSL 2.0); here the
 * address is the controller's own after 0720 S9 (the per-device address the n2btd daemon generated and stored on its
 * first run), not Wi-Fi + 1.
 */
#include <arm64/sweet_early.h>
#if defined(NORD2)
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOService.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <sys/errno.h>
#include "../../../bsd/dev/nord2_bt.h"

class Nord2Bluetooth : public IOService
{
	OSDeclareDefaultStructors(Nord2Bluetooth);
};

#define super IOService
OSDefineMetaClassAndStructors(Nord2Bluetooth, IOService);

static Nord2Bluetooth *n2bt_node;

/* bt6: the Bluetooth address where MobileGestalt looks first. [V libMobileGestalt 20H392 (bltorch-20261114/re/mg), key
 * "bluetooth" = XSLlJd/8sMyXO0qtvvUTBQ, handler 0x19c572b18]: true iff an address of >= 6 bytes, not all zero, is found in
 * IODeviceTree:/chosen "mac-address-bluetooth0" (else the IOService named "bluetooth" "local-mac-address", else syscfg BMac).
 * The port's ADT has no such property, so Settings had no Bluetooth row (phone, bt23a: gestalt bluetooth = false while
 * bluetooth-le = true). Set as early as possible: at boot from n2btd's stored address (n2bt_early, nord2_bt.c) and again
 * whenever kern.n2bt_addr / S9 settle the address. MSB first, like mac-address-wifi0. */
extern "C" void
n2bt_chosen_address(const uint8_t addr[6], const char *source)
{
	IORegistryEntry *chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
	if (chosen == NULL) {
		IOLog("NORD2 BT2: no IODeviceTree:/chosen: mac-address-bluetooth0 not set\n");
		return;
	}
	OSData *d = OSData::withBytes(addr, 6);
	if (d) {
		chosen->setProperty("mac-address-bluetooth0", d);
		d->release();
		IOLog("NORD2 BT2: IODeviceTree:/chosen mac-address-bluetooth0 = %02x:%02x:%02x:%02x:%02x:%02x (%s)\n", addr[0], addr[1], addr[2],
		    addr[3], addr[4], addr[5], source);
	}
	chosen->release();
}

/* the early thread (reads n2btd's address file once the root volume is up) starts from the kernel's C++ static initialisation */
struct Nord2BluetoothEarly {
	Nord2BluetoothEarly() { n2bt_early_start(); }
};
static Nord2BluetoothEarly n2bt_early_init;

extern "C" int
n2bt_registry_publish(const uint8_t addr[6])
{
	IOService *platform = IOService::getPlatform();
	Nord2Bluetooth *made;
	uint32_t transport = 3, speed = 3000000;

	if (n2bt_node != NULL) {
		OSData *d = OSData::withBytes(addr, 6);
		if (d) { n2bt_node->setProperty("local-mac-address", d); d->release(); }
		IOLog("NORD2 BT2: service \"bluetooth\" address updated\n");
		return 0;
	}
	for (int tries = 0; platform == NULL && tries < 40; tries++) {
		IOSleep(250);
		platform = IOService::getPlatform();
	}
	if (platform == NULL || (made = OSTypeAlloc(Nord2Bluetooth)) == NULL) {
		IOLog("NORD2 BT2: no platform expert / allocation: service not made\n");
		return ENXIO;
	}
	if (!made->init() || !made->attach(platform)) {
		made->release();
		IOLog("NORD2 BT2: init/attach failed\n");
		return ENXIO;
	}
	made->setName("bluetooth");
	{
		OSData *d = OSData::withBytes(addr, 6);
		OSData *t = OSData::withBytes(&transport, 4);
		OSData *s = OSData::withBytes(&speed, 4);
		if (d) { made->setProperty("local-mac-address", d); d->release(); }
		if (t) { made->setProperty("transport-encoding", t); t->release(); }
		if (s) { made->setProperty("transport-speed", s); s->release(); }
		made->setProperty("vendor", "MediaTek");
		made->setProperty("nord2-controller", "MT6893 CONNAC2 soc3_0 BGFSYS over BTIF (pieces 0720-0722)");
	}
	if (!made->start(platform)) {
		made->detach(platform);
		made->release();
		IOLog("NORD2 BT2: start failed\n");
		return ENXIO;
	}
	made->registerService();
	n2bt_node = made;
	IOLog("NORD2 BT2: service \"bluetooth\" in the registry: local-mac-address %02x:%02x:%02x:%02x:%02x:%02x transport-encoding 3 (H4BC)\n",
	    addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	return 0;
}
#endif
