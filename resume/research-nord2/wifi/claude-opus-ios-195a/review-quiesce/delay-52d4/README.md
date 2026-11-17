# Remaining initial Wi-Fi delay on 52d4b159

The new boot has a distinct, source-proven missing initialization contract:
configd InterfaceNamer requires an open IONetworkStack connection as well as
registry quiet. Its initial stack timer defaults to300seconds. The selected
kernel provides our IONetworkInterface/real attached ifnet, but no built-in
IONetworkStack service/client. This is a concrete adapter gap, not a reason to
clear more root busy counts. Its causal role in the whole Settings freeze is a
strong timing-supported inference, not a live configd/wifid stack trace.

No canonical/device/USB collector/CI/build changes. Root remains sole collector
owner. Two read-once copies of root-owned files are frozen here; these are live
snapshots, not the final capture. Original byte identities and relevant numeric
rows are pinned. Root subsequently relayed human confirmation: Wi-Fi networks
appear after roughly five minutes. Do not confuse that with fast initialization.

## Observed timing and comparison

- Matching log: daemon_hold0, available1, root_busy1 at18617ms. This is other
  matching work; the paired daemon hold is suppressed in the executed kernel.
- Three client opens26190/26235/26386ms all sample root_busy0. First capability
  PIPE selector1 at26196ms. No event allocation/registration initially.
- First event allocation selector5/3 at322618ms; Armed selector2 at322619ms.
  Delay from first client is296429ms. configd starts near20seconds, so its
  initial300second stack timeout predicts roughly320seconds.
- Fourth client opens332901ms and reaches Armed332920ms (19ms). The delay is
  initial/global, not an intrinsic per-client operation timeout.
- First actual UI SET10 at335643ms; enqueue335644; event10 notified336902;
  completion336903 with7delivered rows and1omission. Actual subsequent scans
  and human-visible networks are separate successful delivery evidence.
- The earlier195a trace had no precise client/PIPE monotonic timestamps and
  event setup only bounded319..490seconds. Both are consistent with an initial
  wait, but rootbusy was not recorded there. It is invalid to attribute either
  historical interval solely to the daemon hold based on aggregate busy state.

## Exact selected configd caller

Original configd SHA06b0330ef3a40bb648c00cc63ce3ab0a59b51d8dd4734b33143edd07ad0a1539.
All saved disassembly records are checked against original bytes; report in
byte-verification.json totals2285original instructions (including the bounded
CoreWiFi consumer methods). Ordinary LLVM/split-cache analysis was used under
the previously read REA skill; no connected REA provider/runtime claim.

1. InterfaceNamer obtains IOService:/ and observes IOBusyInterest; initial busy
   read is0x100017860. After that succeeds,0x100017940..1796c constructs a
   CFRunLoopTimer at now+global0x100071970, the positive-configurable
   WaitStackTimeout (fallback double300). Timer callback0x100018710.
2.0x10001799c..179cc registers IOServiceFirstMatch for IONetworkStack, callback
   0x100018894, then drains its initial iterator at0x100017b3c. It separately
   observes IONetworkInterface at0x100017b44..17c44.
3. Stack callback0x1000188c4 gets first service, then IOServiceOpen(service,
   mach_task_self,type0,&global0x100071b38) at0x1000188e8. Only successful open
   leads to the 'IONetworkStack found' path. A matching name alone is insufficient.
   It releases the stack matching notifier and sets the timer's next date to
   now+WaitQuietTimeout(default240) at0x100018ab8..18acc.
4. Quiet callback0x100018468 rejects busy argument!=0. Even with quiet, it checks
   connection0x100071b38 at0x1000185ac. Zero goes to 'No network stack object'
   and returns; it does NOT invalidate the timer or complete interface naming.
   Nonzero invalidates/releases timer, processes interfaces, records QUIET&NAMED,
   and publishes state. Thus rootbusy0 alone cannot fulfill this requirement.
5. Naming work0x10001a1fc also returns immediately when the same connection is0.
   For an interface that already has a unit0x10001a62c, the consumer follows the
   already-named branch, records it, and need not allocate/rename its BSD ifnet.
6. Timer callback0x100018710 records TIMEOUT, logs 'timed out waiting for IOKit
   to quiesce', dumps actual busy services, calls quiet callback with message0,
   argument0, then UNCONDITIONALLY records/publishes InterfaceNamer state via
   0x100018ccc. That generic timeout log does not identify a daemon busy hold.
  0x100018ccc creates '%@InterfaceNamer' under kSCDynamicStoreDomainPlugin and
   SCDynamicStoreSetValue. The dictionary's STACK/QUIET/QUIET&NAMED/TIMEOUT keys
   are actual decoded CFStrings in configd-cfstrings.json.

## Minimal functional stack/client contract to implement

Station_wire owns the separate private adapter and Apple source comparison.
These are direct selected-consumer obligations, not speculative selectors:

- Publish an actual IONetworkStack metaclass/service with a type0 userclient;
  class naming/property spoofing or returning success without a real client
  cannot satisfy IOServiceOpen/lifetime behavior. Pair attach/start/publication
  and close/free/unwind with real ownership.
- The bounded InterfaceNamer path uses IOConnectSetCFProperties, not an
  externalMethod scalar/struct selector. Naming helper0x10001d6a4 builds exactly
  three keys and calls IOConnectSetCFProperties at0x10001d7a8:
  IONetworkStackUserCommand = CFNumber SInt32(!arg1);
  IORegistryEntryID = CFData of exactly8native-endian bytes containing the
  actual SC interface registryID; IOInterfaceUnit = desired CFNumber object.
- Apple IONetworkingFamily source comparison delegated to station_wire resolves
  commands0=fixedunit,1=lowest available at/abovehint,2=all. Do not claim support
  for all source commands from this one consumer trace. Preserve exact shapes,
  bounds, command-dependent requirements and refusal for unimplemented cases.
- Our already attached ifnet owns its actual name/unit/MAC. Resolve only the
  exact currently owned interface registryID and verify its current ifnet state;
  idempotent registration may truthfully confirm it. Never allocate another
  ifnet, rename an unowned one, report an unattached requested unit as success,
  or fake carrier/association. IOInterfaceState bits require source-backed
  actual attachment/registration semantics, not a guessed readiness constant.
- Selected configd XML entitlement blob includes the allowed userclient class
  IONetworkStackUserClient, but does NOT include the newer source entitlement
  com.apple.networking.ionetworkstack.user-client. The extracted signed blob is
  not an independently verified live task credential. Use reviewed task/admin
  checks compatible with the selected caller; do not silently invent an
  entitlement or adopt a newer incompatible gate. Pin open/client permissions
  and concurrent close/setProperties behavior in the actual-method fixture.

## Downstream CoreWiFi and remaining causal limit

CWFSCNetworkConfiguration::__networkInterfaceStateConfig at0x1abddcf44 calls
SCDynamicStoreKeyCreateNetworkInterface(NULL,State:) through actual thunk
0x1afb6811c ->0x19064359c, then SCDynamicStoreCopyValue through0x1afb680f8
->0x19063b0e8. networkInterfacesNames at0x1abddcfcc takes the Interfaces array
from State:/Network/Interface. IEEE80211NetworkInterfacesNames0x1abddd138 filters
those names using socket+SIOCGIFMEDIA0xc02c6938 and `(current&0xe0)==0x80` in
__isIEEE80211NetworkInterfaceName:0x1abddd010. Constants/thunks/selectors are
resolved from original selected-cache pointers in corewifi-sc-resolved.json.

The previously pinned proxy __updateWiFiNetworkInterfaces0x1abdb5aa4 intersects
that SC list with physical/virtual Apple80211 names; only admitted names get
handlers and default event monitoring, leading to the event-pipe API. This
shows the relevant downstream dependency, but does not by itself prove which
live SC update or wifid dispatch queue delayed handler setup until322seconds.

Bounded wifid inspection found no InterfaceNamer literal or direct300second
wait. It has SCPreferencesLock calls, including wait=true at0x10003f1d4, but
that has not been connected to the observed startup wait and is not asserted
as its cause. No live configd task state, IORegistry dump, plugin dictionary or
userland backtrace was acquired. Missing built-in IONetworkStack is established
by selected kernel symbols/source; absence of an unexpected dynamically loaded
service is inferred, not measured by a live registry inventory.

The justified next candidate is a functional bounded stack/client adapter plus
honest actual-interface state and diagnostics (open/command/status/t_ms), with
source/auth/lifetime tests. Do not reduce timers or forge Plugin readiness.
Actual faster event initialization and human Settings response must validate
the causal prediction; successful backend scans alone cannot do that.
