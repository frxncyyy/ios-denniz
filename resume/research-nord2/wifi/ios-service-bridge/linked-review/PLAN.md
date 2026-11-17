# Linked iOS scan bridge review

Await the parent's exact completed CI run, source, kernel path, byte count and SHA256. Do not inspect a partial artifact.

Verify the supplied kernel identity, then use the existing bounded local LLVM/Mach-O method to record actual addresses, call targets, vtable/metaclass data and small disassembly ranges. Extraction alone is not a verdict. Do not use first-match duplicate OUTLINED symbols as authority.

Review only the promoted integration:

- Actual IO80211Controller, IO80211Interface and IO80211SkywalkInterface metaclasses and parent links; real start/newUserClient and catalogue registration linkage.
- Actual Infrastructure/IOInterfaceName publication and real BSD ifnet media callback: IEEE80211/AUTO and AVALID without ACTIVE; output refuses data.
- Actual NiwlLifecycleClient creation, task/entitlement admission, prepared OOL copy and balanced failure cleanup; scan/get/cache/event/close callback call targets and exact EOF code.
- Actual CAP worker calls retained nss continuation after boot26, opt-in default-off retains old park; ready hook reaches registration, user scan enqueues rather than doing MMIO in IOUC context.
- Actual nss queue/owner, nct continuation and ncr idle sentinel linkage; preserved RX cursors/segmented state, bounded no-publication drain refusal and common fatal drain.
- Pin parent-generated-source equality and normal generic ARM64 audit separately; do not re-audit the whole firmware pipeline or claim live IPC/Settings success.

Save concise linked-review.json, bounded evidence and review-manifest.json here. Parent owns artifact download, CI, integration, gates and device.
