# Independent review: NORD2 IOKit daemon busy hold

Verdict: the paired NORD2-only suppression is justified as a built-in-only port
policy and can accompany the cohesive scan fixes. It is not a proven diagnosis
of the historical Settings delay. `nord2-iokit-quiesce.patch` changes only
IOStartIOKit.cpp and IOService.cpp; it does not clear busy state, fake a daemon
notification, publish readiness, change UFS, clocks, boot timing or device state.
Root owns integration and the generated 0082 patch. Add `iokit/Kernel/IOService.cpp`
to `tools/nord2-mkpatch.sh` FILES; no separate 0083 is necessary.

## Proven selected kernel chain

`executed-source` contains exact CI39750474116 tar members; source identities are
pinned separately. The patch is exactly the diff of those executed sources to
`proposed`. The actual linked kernel also contains the live code, not a NO_KEXTD
stub (`actual-kernel-disassembly.txt`):

- StartIOKitMatching 0xfffffe0007a017c0 calls iokitDaemonAvailable at +0x48,
  then getServiceRoot and virtual adjustBusy(+1). The actual full 195a trace's
  boot-args line445 lacks `-restore`.
- OSKext.cpp:1482 iokitDaemonAvailable tests only `-restore`; it does not test
  whether a daemon is running or its launch job exists.
- OSKext.cpp:13217 invokes iokitDaemonLaunched after the daemon's successful
  personality/catalog request, under its one-time daemon_ready condition.
- IOService.cpp:1195 keeps startDeferredMatches, decrements this hold, and
  publishes the IOKit user resource. The patch suppresses precisely the paired
  hold/decrement under NORD2, preserving deferred matches and publication on a
  genuine late callback. All non-NORD2 branches and NO_KEXTD behavior remain.
- IOService::_adjustBusy propagates child work up the provider chain and sends
  kIOMessageServiceBusyStateChange; other busy counts must never be guessed or
  forcibly decremented. getBusyState masks the aggregate count (line5477).

The selected board config defines NORD2. IOService.cpp gains an explicit board
include guarded by ARM64_BOARD_CONFIG_SWEET, so the paired NORD2 branch does not
depend on accidental include ordering and other architectures retain behavior.
The older Topaz0128 patch uses the same paired policy with a broader SWEET guard;
its comments are reference experience, not proof of the Nord2 runtime cause.

## Same-IPSW configd corroboration

REA skill applies to shipped artifact analysis, but no REA provider is connected
in this session. Existing local LLVM static tooling was used; no setup/install,
runtime execution or REA Evidence IDs are claimed.

The actual selected configd embeds InterfaceNamer. Its file identity and 364
original instruction bytes are pinned/verified in report.json and the bounded
`configd-disassembly.txt` (LLVM symbol-stub and literal annotations come from
that same file):

- 0x1000175d4..175dc obtains `IOService:/` via IORegistryEntryFromPath.
- 0x1000175ec..1760c registers `IOBusyInterest`, callback0x100018468, using
  IOServiceAddInterestNotification; 0x100017860 initially reads root busy state.
- 0x100018490 branches on messageArgument==0 (quiet), and 0x1000184c8..184d4 checks the
  0xe0000120 busy-state notification type. The quiet path logs `IOKit quiet`
  at0x100018578 and invalidates/releases its timer at0x1000185cc..185e0.
- `WaitQuietTimeout` CFString at0x10006ba98 references cstring0x10005f3b8;
  lookup0x100016e84..16e90 accepts a positive CFNumber, fallback0x100016f9c
  stores double240.0 at0x100071978. A separate WaitStackTimeout defaults300.
- 0x100018ab8..18acc sets the next timer date to current CFAbsoluteTime plus
  WaitQuietTimeout. Initial stack timer0x100017940..1796c uses the separate
  stack timeout. Thus the report's remembered '300/240' is now distinguished
  in actual selected bytes; it is not a single inferred Settings timeout.

The selected LaunchDaemons directory has no kernelmanagerd/kextd filename, and
no such exec occurs in the captured kernel log. That bounded negative evidence
is not proof that no equivalent service can run, that no ready notification
occurred, or that every second of UI delay is attributable to this hold. The
existing trace contains no busy/hold identity sample. Static configd code proves
a relevant quiet consumer, not live execution of its waiting branch or the full
InterfaceNamer-to-CoreWiFi dependency chain.

## Logging advice and interpretation

Use `<kern/clock.h>`, `uint64_t ticks, ns; clock_get_uptime(&ticks);
absolutetime_to_nanoseconds(ticks, &ns);` then unsigned long long `ns/1000000`
with `%llu` for `t_ms`. This is monotonic uptime, independent of broken RTC/wall
clock, not CPU ticks interpreted directly as milliseconds. The selected XNU
IOService already uses these APIs. `IOService::getServiceRoot()` and virtual
`getBusyState()` are public selected-XNU APIs; check root!=nullptr in client
logging and print a documented sentinel if absent. The kernel-start path already
has a valid service root. Never block for quiet while holding a client gate.

The new kernel logs record daemon_hold=0 at matching start and daemon_release=0
if a genuine late ready callback occurs, plus t_ms and the sampled root count.
Client/PIPE/SCANREQ timestamps and root_busy can correlate elapsed intervals.
A positive aggregate root_busy does NOT identify the daemon hold. A zero sample
at client open does NOT exclude an earlier quiet wait. Continuous busy after
this patch identifies remaining work to inspect; it does not justify clearing
unrelated counts. Completion still requires actual admitted scan/event/results
and human Settings behavior, separately from boot BSS or registry quiet.

## Validation and limits

- Both complete proposed C++ files compiled for generic ARMv8.2-A+crypto with
  selected kernel headers: PASS. Recorded commands/logs/objects retained. Only
  relocated quote-header search path was added to the established C++ command;
  warnings include existing whole-kernel/header diagnostics. This is compile
  validation, not a linked kernel or real IPC/boot proof.
- test-pair.py compiles extracted actual function bodies under NORD2 on/off and
  NO_KEXTD on/off, availability on/off: 8 cases, fail-fast ASan/UBSan PASS.
  It preserves five unrelated busy counts and verifies late rematch/resource
  behavior and matching decrements. Dependencies are modeled, not real IOKit.
  An initial test prelude omitted cstddef/NULL; correcting that fixture required
  no proposed production change.
- Exact executed-CI -> proposed diff and all 364 configd instruction records
  verified. No canonical file, patch generator, device, CI or commit changed
  by this reviewer. Root independently integrated the same proposals during
  review; executed baseline remains the immutable CI tar, not its edited tree.
