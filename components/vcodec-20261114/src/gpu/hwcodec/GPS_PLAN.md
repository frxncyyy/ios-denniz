# Nord2 location / GNSS plan (2026-11-12, host-only analysis)

V = verified (log, binary, mirror or source read). I = inferred.

## 0. Where we are

* **locationd is alive.** One `exec locationd` per boot and 0 `CRASH locationd` across the last 30 runs (dp1..ph-vt18).
  It answers AKS sel 0/7/17 with 0x0 and has written its state: `/private/var/root/Library/Caches/locationd/clients.plist`
  (Camera entry `{SupportedAuthorizationMask: 3}` with no `Authorization`, so its status is "not determined") and
  `/private/var/mobile/Library/Preferences/com.apple.locationd.plist`, which has `LastSystemVersion` and other keys but
  **no `LocationServicesEnabledIn8.0`**. (V, mirror 09:46)
* **The Camera popup** is locationd's "big switch" re-enable alert ("Turn On Location Services to Allow Camera...",
  buttons Settings / Cancel; phone.py taps the right-hand button = Cancel). Strings found in locationd: `location disabled
  system-wide, denying executable %s`, `App asked us too many times to show big switch re-enable prompt.` (V). That the
  popup is this alert, and not the "Allow While Using" prompt, is inferred from the button layout and the missing switch (I).
* **Fix = payload-locationd1** (staged, rehearsed on the host, not on the phone): sets `LocationServicesEnabledIn8.0 = true`
  (key string V in locationd 20H392; file I, strongly: it is the locationd prefs file that locationd itself writes) and
  Camera `Authorization = 2`. That 2 = While Using is I: masks seen are 1 for an app with no usage string, 3 for Camera
  (Never | While Using), 5 for system services (Never | Always), so bit 2 = While Using.
* **Settings, Privacy > Location Services row.** `-[PUIPrivacyController refreshLocationServicesAvailable]`
  (PrivacySettingsUI, cache 0x1ce6cf26c) does **not** ask MobileGestalt. It loops over the CoreTelephony subscription contexts
  and reads the **carrier-bundle key `LocationServicesAvailable`** (bundle type 1). If no context gives an NSNumber, it uses a
  default constant (pointer at 0x1daca9820, probably kCFBooleanTrue: I). Then it calls `set_cachedLocationServicesAvailableValue:`.
  (V, disassembly.) Our carrier bundles (22288, 22288_GID1, Default) have no such key (V). So that check should not hide the
  row. A MobileGestalt override (CacheExtra `location-services` = `0pY9r1XBV1duZ8HO3tBvFg`; today the key is absent and
  the cache has only 48 keys) is **not** staged, because nothing ties the row to it. Next step: after locationd1, take a
  screenshot of Settings > Privacy & Security. If the row is still missing, disassemble `-[PUIPrivacyController specifiers]`
  (the tools are in the scratchpad: dsc.py, xref.py, dis.py).
* **GPS today: none.** The QMI emulation answers the 26 PDS (0x06) requests with generic success, and the 60 s report says
  "GPS off" (V). Nothing produces a position. Wi-Fi scans do run (`NORD2 WIFI SCANRFCHANNEL`, 1358 WIFI lines per boot: V).

## 1. What the phone has for GNSS (Android side, V unless marked)

* Hardware: GNSS is inside the MT6893 CONNSYS (CONNAC2 / soc3_0, the same conninfra that Wi-Fi already powers on). It is
  not part of the modem.
* Kernel source (`nord2-current-kernel/drivers/misc/mediatek/connectivity/gps/`): `data_link/` (gps_dl, about 14 kLOC: L1/L5
  links `/dev/gpsdl0`, `/dev/gpsdl1`, conninfra power and clock, EMI buffers, DMA, IRQ), `gps_pwr/` (`/dev/gps_pwr`),
  `gps_stp/` (`/dev/gps_emi`, legacy STP), `gps_scp` (`/dev/gps2scp`). Android loads it after `vendor.connsys.driver.ready`
  (`init.gps_drv.rc`, insmod `${ro.vendor.gps.chrdev}.ko`).
* Userland (cam-vendor-20261109/vfs): `bin/mnld` (403 KB) + `lib64/mt6893/libmnl.so` (5.7 MB, the MediaTek positioning
  engine, which runs on the AP), `etc/MNL_Config.xml`, `etc/gnss/agps_profiles_conf2.xml`, state files in `/data/vendor/gps/`
  (EPO/QEPO ephemeris, mtkgps.dat, gps.conf), plus the HAL `android.hardware.gnss@2.1-impl-mediatek` and the AGPS daemon
  `mtk_agpsd`/`lbs_hidl_service`. There is no separate GNSS firmware file in vendor/firmware (82 files, all soc3_0 Wi-Fi/BT/MCU).
  GNSS MCU code probably comes in the connsys MCU patch/ROM or is pushed by gps_dl/libmnl (I).
* iOS side: locationd on a real D22 gets GNSS from the Qualcomm baseband (`kCLConnectionMessageGnssDisablementAssertReq`,
  `MotionEnableBasebandCompassCoexForTesting` and other baseband GNSS paths). Our baseband is an emulation, so that path
  has nothing behind it.

## 2. Ways to feed CoreLocation (cheapest first)

| # | Path | Effort | Risk |
|---|------|--------|------|
| A | **Simulated location**: locationd has a `com.apple.locationd.simulation` Mach service (V, launchd plist) used by Xcode's "simulate location". A small root tool (CLSimulationManager SPI in CoreLocation) pushes fixed or scripted coordinates. Proves the whole stack (Maps, Camera geotag, Weather) without hardware. | 1 to 2 days | low; needs an entitlement (`com.apple.locationd.simulation`: I), which our AMFI accepts |
| B | **Wi-Fi location**: locationd's Wi-Fi provider asks wifid for scans and Apple's server for positions. It needs working Wi-Fi scan results to wifid (partly there) and network access. | depends on the Wi-Fi and data milestones | medium |
| C | **Real GNSS**: port gps_dl to XNU (like the Wi-Fi conninfra port), run mnld/libmnl's engine natively or in a translated helper, and inject its NMEA/fixes into locationd as in A, or emulate the baseband GNSS channel in the QMI bridge (the LOC 0x10 service; PDS is the legacy one). | weeks | high: libmnl is an Android/bionic ELF and would have to be reimplemented or run under an ELF loader |

## 3. Milestones

1. **M0 (staged now)**: payload-locationd1. Big switch on, Camera "While Using". No popup; locationd still alive.
2. **M1**: Settings row check (screenshot). Disassemble `specifiers` if it is still missing.
3. **M2**: simulation injector (path A), a fixed coordinate. Camera photos get GPS EXIF; Maps shows the dot.
4. **M3**: XNU gps_dl port: conninfra GNSS power-on (reuse the Wi-Fi conninfra code), `/dev/gpsdl0` equivalent, raw
   measurement stream visible in the log (`NORD2 GNSS ...`).
5. **M4**: positioning engine. Either the reverse-engineered libmnl protocol plus an open engine (RTKLIB-class) on raw
   measurements, or a libmnl ELF-loader helper. Output NMEA, fed into M2's injector.
6. **M5**: assistance (EPO/QEPO download; MediaTek's EPO server needs network) and power management.

**Cheapest first deliverable after M0: M2 (simulation injector)**, because it needs no kernel work.

---
## UPDATE 2026-11-12 ~10:45 (after loc1 on the phone; Subway Surfers request)

### Subway Surfers is NOT waiting for GPS
* `/Applications/SubwaySurf.app` is a decrypted (cryptid 0) Unity 2022.3 build with **iGameGod.framework** injected
  (iOSGods cheat menu with StartApp ads). Its Info.plist has **no NSLocation*UsageDescription** (V), so iOS never gives
  it a location (clients.plist mask 1 = Never only, V). Unity adds the key itself when a game uses Input.location.
* An older run (q10cb) has the main thread's backtrace from RunningBoard's kill, symbolized with the cache: blocked in
  CoreLocation +0xca6c = `[[conn synchronousRemoteObject] getAuthorizationStatusForBundleID:orBundlePath:replyBlock:]`, a
  **synchronous XPC call to locationd that never got a reply** (V for q10cb). The app asks *whether* it is authorised,
  which is not a location request. In locationd this call lands in `_CLDaemonGetAuthorizationStatus` (method imp 0x1012d8538).
  Why no reply came: still open.
* Phone experiment A (decides it): launch, wait 25 s, Home, wait 10 s, then grep `CRASH SubwaySurf` (bt), symbolize with
  `scratchpad/tools/img.py SLIDE addr...`.

### Settings > Privacy & Security > Location Services (V, disassembly + plists)
* Privacy.plist row `LOCATION` (PUILocationServicesListController, `get: locationServicesEnabled:`) has no requiredCapabilities.
  The top-level Privacy row in Settings.plist has none either.
* `-[PUIPrivacyController specifiers]` removes LOCATION only if `_cachedLocationServicesAvailableValue.boolValue == NO`.
  `refreshLocationServicesAvailable` reads the carrier-bundle key `LocationServicesAvailable` for every CT subscription and
  falls back to **kCFBooleanTrue** (GOT -> 0x1df28ff90, which is the object CFBooleanGetValue compares against). Our carrier
  bundles have no such key. **So no MobileGestalt/capability payload can be the fix.** The row's value comes from a
  synchronous CLLocationManager call to locationd, the same kind of call that hangs Subway Surfers. Phone experiment B decides it.

### Simulated location (staged: payload-locsim1)
* locationd has a built-in **"Using simulated location from defaults"**: the CLSimulationController constructor
  (0x10084e428, called from `Beginning Simulation Service`) reads CLPreferences `SimulatedLocationLatitude/Longitude/Accuracy/
  Type/Altitude/AltitudeAccuracy`. If Accuracy > 0 it builds a CLDaemonLocation and feeds it (V, disassembly).
  Inferred: the prefs domain is the mobile com.apple.locationd.plist (it holds LocationServicesEnabledIn8.0, which locationd
  kept after loc1: V, mirror 10:33). Also inferred: Type 1 = GPS, and that the simulation service begins at startup or on
  first use; if it only begins when a client connects to `com.apple.locationd.simulation`, a 20-line client is needed
  (CLSimulationManager startLocationSimulation): milestone M2b.
* Coordinates are a placeholder (Rome 41.9028, 12.4964); edit `payload-locsim1/sim.json` before the transaction.

### Real GNSS: what has to be written
1. **Kernel (XNU) port of gps_dl** (`connectivity/gps/data_link`, 14 kLOC; we need about 3-4 kLOC: hal/gps_dl_power_ctrl.c,
   hw/gps_dl_hw_{conn_infra,bgf,gps,power_ctrl,usrt_apb,semaphore}.c, link/ (L1 ring + EMI buffer), linux/ (char device ->
   an IOUserClient)). It sits on the conninfra that the Wi-Fi port already brings up (`NORD2 WIFI FW probe status=0`):
   reuse its power-on, add `consys_hw_gps_power_ctl` / `pmic_mng_gps_power_ctrl` and the GPS EMI region (emi_mng.h
   "GPS EMI"). **gps_dl calls request_firmware nowhere** (V): the GNSS core runs from ROM, or code is pushed by
   libmnl through the data link (I). vendor/firmware has no GNSS file (V).
2. **Data link protocol**: the AP exchanges binary MNL frames with the GNSS MCU through `/dev/gpsdl0` (L1) and
   `/dev/gpsdl1` (L5) (read/write + ioctls, gps_dl_linux*.c). The frame content is proprietary (libmnl).
3. **Position engine**: `mnld` + `lib64/mt6893/libmnl.so` (Android bionic ELF, 5.7 MB) turn those frames into fixes/NMEA.
   Options: (a) a bionic ELF loader on iOS (hard: libc, threads, sockets); (b) reverse the MNL frame format for raw
   measurements and run an open engine (RTKLIB-class); (c) run mnld unchanged on a small Linux side processor:
   not available. Without network, A-GNSS (EPO/QEPO files in /data/vendor/gps) is missing, so cold start takes 30 s or more.
4. **Bridge to CoreLocation**: NMEA/fix -> locationd. The same path as payload-locsim1 / CLSimulationManager
   (appendSimulatedLocations, startLocationSimulation) gives one feed for both the simulated and the real source.
   Estimate: kernel bring-up to raw frames 1-2 weeks; engine 2-6 weeks (the main risk); bridge 2-3 days.

### Ordered phone experiments
1. **A**: Subway Surfers: launch, 25 s, Home, 10 s; send the `CRASH SubwaySurf` 4 lines (bt) and any `exec`/`CRASH locationd`.
2. **B**: Settings > Privacy & Security: screenshot after 10 s. Is the Location Services row there, blank, or hanging?
3. **locsim1**: `env PAYLOAD=payload-locsim1 GUEST=guest-locsim1.sh OWNED=$P/owned-locsim1.json bash txn.sh $IMG locsim1`
   (q10jk 36aca6cf8bddd242). Look for `LOCSIM1_INSTALLED` in offline.log, catalog ok, then after boot no `CRASH locationd`.
   Open Camera, take a photo, then fetch and check its EXIF GPS (fetch_media.sh). Open Maps if present.
   Undo: `payload-locsim1-undo` + `guest-locsim1-undo.sh` + the same owned list.
