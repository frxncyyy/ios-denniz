# After compatibility hardware: role/parent and actual continuation

The investigated same-IPSW path does **not** turn a failed GET96 into AWDL. It caches zero, and CoreWiFi calls that role “undefined/infra”. The next concrete selection predicate is the intersection of SystemConfiguration Wi-Fi names with the framework's physical/virtual interface-name lists, followed by event-handler/monitoring setup. This addendum closes the earlier constructor-only limitation but does not claim the new post-UI hardware trace has passed those steps.

No canonical changes, builds or device actions were performed. Investigation used the existing selected split-cache view, extracted CoreWiFi TEXT carrier and IO80211 disassembly. REA instructions apply; its provider remains unavailable as already documented. These are ordinary bounded LLVM/cache static observations, not a decompiler or live-userland trace.

## Exact role relationship: wire96 and CoreWiFi

1. `CWFApple80211 virtualInterfaceRole:` (`1abd7cb1c`) calls the actual `Apple80211Get` import with request96 and API result extent4. On success its raw signed32 result is wrapped by `NSNumber numberWithInt:` (`1abe256c0`, selector reference `1d8475a28`). There is no enum translation. On error it returns nil (and an optional error only if supplied).
2. Constructor `initWithInterfaceName:` calls that method with error=NULL at `1abd7b898`, then `unsignedIntValue` at `1abd7b8a4` and writes the result to its cached word at +52. A nil NSNumber gives zero. The cached getter at `1abd807cc` is literally `ldr w0,[x0,#52]; ret`.
3. `CWFXPCRequestProxy __apple80211WithVirtualInterfaceRole:parentInterfaceName:` at `1abd6658c` reads this cached getter at `1abd666d8` and compares its number directly with requested role at `1abd666dc`. Again no remapping.
4. The exact CoreWiFi role formatter at `1abd6f5bc` indexes CFString pointers at `1d8457928 + 8*role` for its accepted role mask. Decoded selected-cache constants are: 0 “undefined/infra”, 3 “p2p-device”, 4 “p2p-client”, 5 “p2p-go”, 6 “awdl”, 7 “ap-sta”, 8 “nan-discovery”, 9 “nan-discovery-data”, 10 “nan-data”, 11 “wds”. Values1/2 are not accepted by that formatter's mask. See `role-strings.json` with object addresses and raw bytes.

Thus the raw wire96 value and the cached/proxy CoreWiFi role are the same integer on this path. Returning6 for Infrastructure would misclassify it as AWDL. A future explicit GET96=0 is a source-backed truthful infrastructure-role response, but the currently unsupported getter already yields cached0 here; changing it alone is not a proven fix.

`__defaultInterfaceRoleForRequestType:` (`1abdb2800`) returns either0 or6 based on request-type bitmaps. Its6 branch is a caller-selected AWDL default, **not** the GET96 failure path. No claim is made that every caller/request type has been recovered.

## Parent and effective selection

`virtualInterfaceParent:` (`1abd7ca78`) uses `Apple80211CopyValue` request97. Its framework GET body is16 name bytes, with no version prefix. Failure returns nil; constructor stores it at +64. Cached parent getter `1abd807d4` loads that pointer.

The proxy does use parent equality when matching an already cached nonzero-role virtual interface: after role equality it accepts unspecified parent, pointer-equal parent, or `isEqual:` string equality (`1abd666e4..66748`). A nil parent will fail a requested nonnil parent in that **cache match**.

But the role0 fallback is different. If no matching object is found, it obtains the requested physical interface by name (`1abd6680c`) and tests requested role at `1abd66818`; role0 branches directly to returning that physical object at `1abd6699c`, without enumerating virtual children or requiring GET97 equality. With unspecified parent it selects the first physical interface name (`1abd66944..66998`). The nonzero-role path enumerates children and queries their role; role7 also has a separate AP capability fallback. Those virtual-interface branches are not proof that this driver needs a fabricated parent.

The correct successful GET97 value for a physical interface remains unproven: this investigation does not establish “self”, empty string, or another interface as a universal physical-parent ABI. Keeping request97 unsupported/nil remains coherent with the demonstrated role0 selection. Do not invent a virtual parent.

## The real next gate before pipe setup

`CWFXPCRequestProxy __updateWiFiNetworkInterfaces` (`1abdb5aa4`, internal work block `1abdb5c28`) obtains:

- SystemConfiguration `IEEE80211NetworkInterfacesNames` (`1abdb5b28`), captured at block +40;
- `CWFApple80211 interfaceNames:` (`1abdb5b4c`), captured at +48;
- `virtualInterfaceNames:` (`1abdb5b74`), captured at +56.

The block enumerates the SC list at `1abdb5ca8..cc0`. For each name, it requires membership in the physical list or virtual list (`1abdb5d04..20`). Otherwise it skips setup. For an admitted name it obtains the Apple80211 object; if `eventHandler` is absent, it installs handlers (`1abdb5dbc`), then starts default monitoring if the proxy's monitoring byte +104 is set (`1abdb5e10..2c`). Names removed from this intersection lose their cached handler/state. This is a real downstream filter; a successfully constructed/opened Apple80211 object alone is insufficient.

`__startMonitoringDefaultEventIDsForInterfaceName:` (`1abdb38e4`) then invokes `startMonitoringEventWithType:interfaceName:` for the defaults. `CWFApple80211 __setupEventMonitoring` (`1abd7b9cc`) calls the actual exported `Apple80211RawEventMonitoringInit` and `Apple80211RawStartMonitoringEventAll`; these imports are resolved in `resolved-relationships.json`. This is the path expected to reach the native pipe allocation/async registration/maps; failure or lack of a subscription can leave only the initial control-capability query visible.

## Important correction: the old Apple class whitelist is not this list path

The exact optimized import thunk at `1afb67ec4` resolves to `Apple80211GetIfListCopy` at `1d3ed3cf8`. `CWFApple80211 interfaceNames:` calls it at `1abd612d0`.

GetIfListCopy calls `1d3ed32d8`, which uses actual `getifaddrs` (indirect slot `1dac9c5e8` -> exported `libsystem_info _getifaddrs` at `1946d95c8`), then SIOCGIFMEDIA `0xc02c6938` through `_ioctl`. It admits the BSD name when `(ifm_current & 0xe0)==0x80` (`1d3ed374c..3788`). It subsequently filters names through `1d3ed3fec`: find the IORegistry service by actual interface name and require the CFString property **IO80211InterfaceRole = Infrastructure**. Both literal CFStrings are decoded in `resolved-relationships.json`.

The separate legacy routine at `1d3ed44e0` does use IOServiceMatching(IO80211Controller), then IORegistryEntrySearchCFProperty/CFString comparisons against IOClass/IOUserClass values ACIWiFiDriver, AirPort_BrcmNIC and AppleBCMWLANCore, with additional IOUserService reporter branches. These are property-string checks, not those three metaclass tests. However, this routine is **not** called by the proven GetIfListCopy chain above. My intermediate inference to root that it was the required list filter is withdrawn. No Apple/Broadcom identity should be added based on that inference.

The current driver's actual media80, real BSD name, Infrastructure property and IONetworkInterface/SystemConfiguration ancestry implement the relevant source predicates. Source and prior host/linked tests establish the implementation; they do not establish what the live SC list or getifaddrs result currently contains. The initial real client open proves successful name binding and at least one discovery route, not this entire intersection.

Redwood's custom IO80211Controller/RedwoodWLAN can satisfy the same generic media and Infrastructure-property path without posing as Broadcom. Its `newUserClient` refusal deliberately selects socket IOCTL for driver commands and PF_SYSTEM events. That transport difference is real, but does not make the old class whitelist mandatory for its GetIfListCopy enumeration. Its README/comments remain source evidence rather than independent proof of every Settings runtime outcome.

## What can and cannot be concluded from a199cd0c now

The early trace has one client, GET43/control selector1/GET12 and unsupported96/97, without allocation/maps/scan. Root subsequently reports the human opened Wi-Fi after about3 minutes with no networks and is collecting a separate post-UI trace. The early trace is not that final trace.

Confuted: failed96 -> role6/AWDL; physical role0 requires a made-up GET97 parent; the old Apple IOClass whitelist necessarily blocks this CoreWiFi physical-list call.

Concrete remaining conditions: SC must expose the BSD interface as IEEE80211; the media/name/property physical list must also contain it; the proxy must install handlers and activate monitoring/subscriptions; actual IOUC event initialization must then succeed. Only the final trace can distinguish “no monitoring requested”, native setup rejection and later scan/result issues. This addendum requests no speculative enum/property/firmware patch and performs no additional hardware probe.
