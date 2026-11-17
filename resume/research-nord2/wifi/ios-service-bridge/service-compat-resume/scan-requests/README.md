# Settings scan compatibility: bounded source correction

The actual58f62ae6 trace has no SET10 and no queued scan. GET103 appears76times and is CURRENT_NETWORK, queried for the associated network; it must not be confused with a scan result/cache request. Registry/client discovery and boot radio scan succeed, while live Settings scanning remains unproven.

## Concrete next integrated change

1. Publish the real retained interface through an IONetworkInterface ancestor below our IO80211Controller, with actual BSD name/unit, authenticated MAC and truthful built-in fields. Root owns this canonical correction. Same-IPSW SystemConfiguration evidence below confirms the missing enumeration/classification path.
2. Return an honest unassociated state and no-current-network error. GET103 accepts the exact1164-byte request with adequate output capacity after owner/name/value checks, then returns literal6/ENXIO without a fabricated record while association is unimplemented. The selected framework's IOUC RawGet stores nonzero return unchanged into errno; its GET conversion maps6/102 to-3903. This agrees with the pinned Redwood implementation's unjoined case103. Do not alias the scan-result cursor11 or return a success row with zero/guessed SSID/BSSID.
3. Promote the narrow pure `niwi_scan_admit` candidate after review. Preserve and consistently extend the historical rawscan1→passive discovery compatibility policy. Rawscan1 or explicit passive2 accepts all actual allowed channels or a unique subset, up to39. Raw1 is source-named ACTIVE; serving nondirected/default-timing raw1 through passive discovery is our explicit port policy, not a claim to issue active probes. Match each requested channel to the authoritative nss_info allowlist; accept no channel flags or20MHz2 and/or exact2.4GHz8/5GHz16 only. Unspecified band requires a unique match. The known ACTIVE128 channel-permission flag is accepted orthogonally because it permits activity, rather than requiring probe transmission; the backend stays passive. DFS/wider channel attributes remain refused. The backend still owns regulatory/native admission; this pure parser cannot grant a lease.
4. Keep directed BSSID/SSID/name-list/shortSSID, nondefault dwell/rest, extra scan flags, background/fast/other modes and unknown bytes Unsupported. The supported modes still require the existing raw BSS3/PHY1 unrestricted profile. No new active probe or timing semantics are claimed. The exact current Settings payload is unknown because no SET10 reached the service; accepting this broader source-proven passive subset is not a promise that every future Settings profile will fit. Bounded shape/option diagnostics can identify a refused profile in the same cohesive candidate.

`proposed/wifi_ios_policy.c/h` are canonical-based pure copies; only the admission function changes. The existing envelope/name/control/rates entrypoints remain byte-identical. ABI is unchanged. The caller must supply its actual native-owned nss_info (not an unauthenticated caller clone), preserve input lifetime, serialize admission and enqueue on the retained queue.

## Exact SystemConfiguration consumer evidence

Selected image `/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration`, base0x190638000, original TEXT512000B SHA2566c66f212500375389c7d781e01460e2931e21dcd0a9d2f89ad472260d870b168. The local MH_OBJECT is an analysis-only TEXT carrier, not loadable/reconstituted framework.

- SCNetworkInterfaceCopyAll0x190641b68 tail-calls WithPreferences0x190641870; its enumeration calls0x1906472c4. At0x1906472e8..2f0 this builds IOServiceMatching("IONetworkInterface"). Stub0x194909b3c resolves by actual instructions to IOKit IOServiceMatching0x18e556804. Our previous child had IOService→IO80211Interface→Skywalk ancestry, so it did not conform to the enumerated class.
- The exported `_SCNetworkInterfaceCreateWithIONetworkInterfaceObject`0x190678444 explicitly calls IOObjectConformsTo(object,"IONetworkInterface") at0x190678484. Its false path tries another class, then refuses. This is real metaclass conformance, not an IOClass string property.
- IFT_ETHER6 selects branch0x19064a750. It checks the parent for IO80211Controller via IOObjectConformsTo0x19064a75c, resolves to actual IOKit0x18e57ac14, and assigns the Wi-Fi interface type on success. A generic Ethernet fallback is different classification.
- Interface IOBuiltin is consumed0x19064a7cc; if missing/invalid it tries IOPrimaryInterface0x19064a7fc then other policy. These fields are consumed metadata/fallback inputs, not every one a mandatory hard-refusal gate.
- Parent property dictionary IOMACAddress is read0x19064aa04, accepted only as CFData; missing/wrong type omits hardware address rather than unconditionally aborting creation. IOInterfaceNamePrefix is read0x19064aa40. Root's actual authenticated6-byte board address is the valid source; never invent one.

Missing ancestry therefore concretely excludes the old child from this enumeration path, even though wifid's separate Infrastructure notification and IOUC open worked. It is a strong integration gap; static evidence alone does not prove it was the sole runtime cause of the freeze or absence of SET10. No live configd/SystemConfiguration trace was collected.

## Other actual requests: no blanket-success policy

Exact same-IPSW request-name table identifies106 RSSI_BOUNDS,51 COUNTRY_CODE,365 DISPLAY_STATE,25 HOST_AP_MODE,383 SET_PROPERTY.

GET106 consumes16B (version and signed32 fields at+4/+8/+12, converted to CFNumbers). The framework's input default is(1,0,0,0); this is not evidence that those are meaningful driver bounds. Do not manufacture radio measurements or calibration bounds.

GET51 is exactly3raw country bytes, converted through a zero-terminated local string path, without a version word. It must come from the actual reviewed regulatory state or an explicitly supported unset representation; no guessed country/defaultpower.

SET383 `Apple80211SetProperty`0x1d3ed0c90 sends4104B: uint32version1, uint32length,4096B XML plist storage. It serializes a one-key dictionary (format100), caps reported length at4096, and terminates the text. wifid direct keys include IO80211InterfaceIPV4Params, IO80211InterfaceIPV6Params, IO80211InterfaceDHCPRenewalWakeScheduled and IO80211InterfaceIPConfigurationBusy. These are IP/scheduling metadata, not a scan trigger. The observed trace did not capture which keys/values were submitted; no fabricated application of them is proposed.

SET365 uses a4-byte display-state payload in the wifid caller0x100046e18. SET25 has more than one shape/value (wifid0x100046c98 value1/body8,0x100046cf0 value2/no body); do not blindly accept both or imply HostAP support. The exact reference Redwood SET switch only implements19/10/20/22 and leaves383/365/25 EOPNOTSUPP. Their presence in a trace does not prove they are mandatory startup successes.

Framework ScanAsync0x1d3ebe7bc validates context and interface, runs the parameter builder0x1d3ebb534 and emits request10/5456 at0x1d3ebe8c0 only if building succeeds. Existing wifid has two direct call sites0x100047de8/0x10014c694. Therefore absent SET10 proves no request reached this service; it cannot distinguish an earlier wifid state/profile failure from pre-RawSet builder refusal. Broadening native admission alone cannot change a request never submitted.

## Validation and scope

11,071 Clang ASan/UBSan fail-fast checks passed: source5456 layout offsets, old default and explicit passive all/subsets/order/band/width profiles, every truncation, every input byte high-bit mutation, channel ambiguity/duplicates/missing authority, advanced option refusal and atomic output preservation. Synthetic fixtures contain no device SSID/MAC. `python3 test-policy.py` reruns host tests without editing author logs.

Primary source reference is the parent-fetched exact kernel commit8e61187098f2c26c5daee8f9f72b6416aa64b9ab. Redwood is a source comparison, not proof that its code executed on this phone or that its broad omissions/claims are correct for every iOS path. No canonical/device/CI mutation, native full build or hardware execution occurred here. Association, encryption, carrier, packet networking and Settings success remain unimplemented/unproven.
