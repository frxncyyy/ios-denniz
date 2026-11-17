# Private installer work

2026-10-23: source and upstream deployment inspected. No device, USB, flash, CI,
canonical source or Git mutation. Work is confined here. Kernel installer is not
ready for promotion until compiled and reviewed. Critical dependency: persistent
HAL replacement changes subsequent fallback55 behavior; it does not carry the
rollback hook. Preserve boot-time rollback path in the new kernel and do not call
fallback55 an audio rollback.

Upstream uses flat RedwoodAudio.driver/{RedwoodAudio,Info.plist}; CFBundle factory
RedwoodAudio_Create. Link dependencies are CoreFoundation and libSystem (pthread,
math, mach time, libc); CoreAudio interface definitions are source-contained.
/Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin must be restored from HAL.disabled;
/System/Library/Audio/Plug-Ins/HAL contains only RedwoodAudio.driver after install.
D22 routing and /Library/Audio/Tunings/D22 must remain available unchanged. Codec
and Actuator refer to /dev/redwood-mic and /dev/redwood-haptic and are outside the
initial speaker test; missing nodes must not be described as implemented.

Parser milestone: package.py and package.h agree on N2AP v1; all per-byte
corruption, all truncation boundaries, null and overflow tests PASS, host C
ASan+UBSan enabled. Synthetic Mach-O fixture only, no plugin binary was built.
validate-kernel.c compiles successfully through tools/nord2-xnu-cc.py with
-DNORD2=1 against freshly extracted CI39957914679 buildtree. This is an ADT/hash
validator ONLY, explicitly no filesystem mutation and no rollback implementation.
Output validate-kernel.o and private-buildtree remain private here. No SDK
fetch, macOS run, CI dispatch, kernel link or hardware result claimed.

CONTRACT.md records exact package and proposed vnode transaction, critical
crash-between-renames hole, receipt/unknown-content rules and fallback55 caveat.
Filesystem installer remains BLOCKER; do not promote validator as installer.
workflow-proposal.yml is unexecuted, requires private transport, tracked pinned
source promotion, runner/Xcode verification and reviewed Action SHA pins.

Transaction MODEL tests PASS for both original VirtualAudio states and each
persisted step, including rollback after committed installation. This is an
executable specification, not filesystem implementation coverage. It confirms
fallback55 leaves HAL absent at the inter-rename crash. Compiled validator object
is Mach-O arm64, SHA256
3275e14281c6f580ddc684021f8f5cbf309e973e085b6ad993b938700ccb0b9d.
build-plugin.sh shell syntax passes; actual Apple SDK build remains unexecuted.

## FINAL installer milestone (supersedes earlier pending status)

Real fixed-path install-kernel.c + shared install-flow.inc/install-restore.inc
implemented. Call int nord2_audio_install(void) EVERY boot before launchd; mode1
install, mode2 package-independent rollback, absent mode validates/recover journal.
Nonzero return must block launchd and use parent-selected recovery policy.
Immutable journal binds original/custom/VirtualAudio inode+fsid+type identities,
payload hashes and original-state identity SHA; commit/rollback markers bind its
digest. No original tree file contents are modified or recursively hashed.
Unknown stage/backup/journal/collisions refuse; no unlink/overwrite cleanup exists.
Pre-journal partial staging leaves original layout safe but requires separately
reviewed rescue cleanup before retry. Rollback retains S/J/C/R for inspection.

Kernel VFS code uses NAMEI_NOFOLLOW_ANY + kernel context, exclusive vnode_open,
UIO_SYSSPACE vnode IO, readback, file+directory fsync, VNOP_MKDIR and VNOP_RENAME
with mount rename serialization and identity/absence checks. Requires single
prelaunch caller and no competing writes to these fixed directories. Normal
committed boots validate exact custom file bytes/metadata/entry allowlists.

Host test-flow.c directly includes the production flow AND recovery functions;
144 mutation-boundary before/after failure/crash scenarios PASS ASan/UBSan plus
idempotent install/rollback, unknown collisions, malformed/missing commit,
post-commit partial rollback without R, and damaged installed payload refusal.
Model backend is not a live HFS/VFS test. Parser tests rerun PASS.

Complete current NORD2 source compiled with -Wall -Wextra -Werror via
nord2-xnu-cc.py using extracted CI39957914679 headers. install-kernel.o SHA256
ee672487666cfe5140c5468698b94a0bae7bc9e824c0f1aae4ba6de6f5381292.
Full link/Apple SDK build/hardware test pending parent integration and approval.
No canonical source, Git, CI dispatch, USB or device mutations were performed.

## Bounded local-rootfs / ADT followup

Actual clean local HFSX image inspected read-only:22 relevant exact paths,
180224physical bytes. Prerequisite dirs root:wheel0755/no symlink, VirtualAudio
correctly disabled, D22 tunings present, Null_Device config intact. Detailed
receipts rootfs-prerequisites.json/rootfs-audio-config.json; observations are
baseline image only, not current phone. STARTUP-PREREQUISITES.md has concrete
startup conditions, one-time /dev/redwood-audio open requirement and historical
vdef-cause distinction. No Apple binary RE was needed.

Native D22 product/audio child omission confirmed; pinned Topaz mkadt.py has
same omission, so it is not proven blocker alone. Private optional audio-service
patch copies only2native identity flags; default byte-identical and phandle/
allowlist testsPASS. No vdef ADT property invented. Parent decides promotion.

Mode2 no-journal changed to read-only success only for verified baseline
structure(exact9AppleHALentries, VirtualAudio disabled, privatepaths absent).
Unknown/live-nojournal state refuses. Shared production flow144fault/crash
model stillPASS, including new baseline mode2 cases. Full kernel compilePASS,
newobjectSHA46027751e7c4b7e69c4d7695a27a8f432e94983ad623f14d3f26d16eba88bf45.

## Final independent promoted-source integration review

FINAL-INTEGRATION-REVIEW.md records actual bundled HFS/VFS audit: no demonstrated
fatal runtime blocker. Private optional EXCLrename+HFSdirentABI patch cross-compiles
current canonical source Wall/Wextra/Werror. Compiler objectSHA
f26f5a01b396a0a28c60c4322dfdbef77ecb0e42e6d3ec1599061ffb2952bc9f.
Current CI plugin PIN/all3sourcehashes match fresh source. Missing MIT LICENSE
artifact notice identified; exactlicense privately fetched/pinned. Optional
builder patch addslicense, explicitSDK/min16/noadhoc+signatureabsence/platform
checks,6mock scenariosPASS. No actualSDKbuild, canonicaledit,Git/CI/deviceaction.

## Actual CI39969463181 HAL artifact review

Read-only hal-preview checked independently:4source+3output hashes match pinned
provenance, thinarm64ALL MH_BUNDLE platformIOS minimum16 SDK17, soleexport
RedwoodAudio_Create, CoreFoundation+libSystem only. N2AP69560B exactroundtrip
(executable68416B/plist1048B), matching factory/type plist. Actualpackage passes
hostC ASan/UBSan everybytecorruption/alltruncations/overflow/null tests; parser
byte-identical to canonical audio_package.h. Receipt hal-preview-review.json.
No concrete fatal first-load issue found; no device/runtime load result claimed.

## Postflash runtime bounded followup

No device access. Existing xnu-live.log now shows successful installerphase6,
HALopens1 and nonzero PCM writes/peaks while hardware initialized0/error-1/running0.
Frozen relevant-line receipt current-hardware-hal-review.json contains inputprefix
hash/length and exact counters. `writes` is nsa_enqueue256frame-chunk counter,
not write syscalls/audible output. No HAL routing/source rewrite justified.
Committed installed hashes require exact same package on futuremode1; use pinned
original82253445...package or byte-verify rebuilt package, or normal mode0 boot.

Private compile-final-runtime.sh recipe prepared; current audio_device.c with
Wall/Wextra/Werror and activegeneratedsweet_touch.c compile against freshly
extracted CI39969463181 exportedheaders, output runtime-compile-ci39969463181.
Source+objectSHA receipt retained. This compile is current-at-run source, not a
promise future mappingfix edits are included. `tools/nord2-audio-check.py` host
suite exit0; private host-models.log. No linkedkernel/CI/device claim or canonical
source changes from this followup.

## Nonhardware startup audit for Claude

startup-audit/FINDINGS.md records source-backed diagnostic deadline defect and
private minimal wrapperpatch: rebase nonzero testdeadline AFTER boundedbackend
startup, continuousduration0 unchanged; stop+report overflow/regression; no
successfulprintfwhileDMAactive. Original1.2s-start regression reproduces expired
1sdeadline; fixedASan/UBSan scenariosPASS. FullproposedNORD2 sourcecompiles
Wall/Wextra/Werror CI39969463181,objectSHA
4637b45ca01016d6303160b137b3392c8c9712eec77c564d46ed87a883c3d92f.
Queueflush dropped-accounting observation documentedonly perparent scope.
No new fatalring/live/HALbug found. No canonical/phone/CIedits.

Current-source deadline regression integration prepared privately: startup-audit/
test_device_deadline.py extracts canonical start() eachrun into tempinclude;
test-device-deadline.c strictclang ASan/UBSan fixture. Currentunfixed canonical
fails specific deadlineassert; proposedsource passes focusedcases. One-line
nord2-audio-check hookdiffready. Lexer malformed/comment/quotedbrace checksPASS.
No broadunchangedsuite rerun/canonicaledits.

Claude candidate audio_device diagnostic review: fields match candidateaudio_hw
header and savedstatusfill, noextraMMIO or USBABIchange. Deadlinefixabsent; exact
private claude-merged source+2current-source regressionfiles+candidatecheckhook
prepared with4filehashreceipt. FocusedASan/UBSan regressionPASS, fullNORD2 merged
devicecompilePASS candidateheadersCI39969463181;objectSHA
a07bf046c2d096298afcfebe769d1821763df36b2135f4f9549bd922b4971ce7.
Seestartup-audit/FINDINGS.md latestsectionforpromotionmap. No canonicaledits.

## Fixed stage8 AP snapshot diagnostic candidate

Private i2c-start3filecandidate reorders same5APreadonlyreads before unchanged
predicates, savesvalues+validmask; callbacksstopimmediatelyonfailure, and refusal
lastPA/value is restored toexactfirstfailingpredicate. Prior shadow/CCU/DMAguards
andparentAPbusybit0 retained. NoUSB/rawreadinterface/controllerwrites added.
Focusedactualtransport31combinations/5readfailures/6earlyguards/START2/resetPASS
ASan/UBSan. Parentrequested test-diagnostics.c candidate delivered, accepts ERROR
plusAPprintf, assertsfull/partialsavedpayload/refusedPAvalue andzero poisonedMMIO.
FocuseddiagnosticASan/UBSanPASS. Parentowns finalsuitehook/fulltests/promotion.
README+manifest exactbaseline/candidate/testhashes private. No canonical/phone/CI.

## Independent inherited-I3C downstream audit

Private i3c-inherited/FINDINGS.md audits actualAP2/CTRL28/IRQ1/FIFO0/I3C8800
against selectedAndroidI2C+DTS+TFA sources. No additional demonstratedcontroller/
profile startupblocker found; fixed100k sourcelegacyconfiguration matches,
all15V2offsets correct, inheritedIRQadoption+exactcompletion/NACK retained.
Independent audit-source-profile.py PASS5erratatables+104selectedboardbitfields
frompinnedblob, preservesPWDN/AMPE/AUDFS; hashreceipt saved. Coordinated with
hardwareagent implementingI3Ccapability semantics and exhaustive/repeat tests.
PhysicalACK/REV/reset/CLKS/profile gates remainunobserved; no guarantees or
relaxation proposed. No canonical/phone/CIactions.
