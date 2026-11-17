# Private normal-iOS x2 candidate — ready for independent review

No active source edits, patch regeneration, CI, image build, device or USB.
Exact frozen inputs/files/hashes are in base.json and validation.json. Source
baseline is active9327714/CI39506694221, including hardware-validated stop-mode
LINERESET exception and narrow buffer-range mapping. Actual x2 transition/read
validation plus x1 negotiated restoration succeeded in the prior diagnostic;
that is not evidence that this retained-x2 normal-iOS candidate has run.

## Contract and publication

Distinct opt-in nord2ufs2live=1, original diagnostic token nord2ufs2lane MUST be
absent (including0). Required unique exact normal-RW tokens: stopat23,cpus8,
rd=ufsroot0,ufsroot=userdata,ufsrw1,rootrw1,profile1. image-contract.json gives
the exact previousce84 normal bundle plus only the new token. No image exists.
This is normalRW, not a new RO exception: exact runtime SMP startup/completion
functions already accept it; tests execute both actual conditions and the real
RW bundle. Driver also rejects duplicate/malformed/missing critical tokens.
Absent live token or exactlive0 preserves ordinary behavior.

Permanent manual-reset-only Linux-return refusal is armed in the earliest UFS
probe and again idempotently at root init/rootdev, before maps/allocation/list
ownership or commands. Invalid opt-in halts before setup. Point11 mutation is
bypassed in valid live mode, and invalid point11/live combinations halt. No
unverified root device is returned even after early initialization failure.

Engine n2l_live reuses the exact stop engine through baseline18+6 attributes,
fixedLUN2LBA1,count5 pinnedSHA256 read, async negotiatedx2, all18 readback and
second full20480-byte hash. Success returns phase5,60UIC,18SET,2reads,
active_verified1,root_allowed1,handoff_allowed0 and retainsx2. It does not run
x1 restoration. Adapter owns the existing mutex, descriptor list/pinned DMA,
pre-root/unpublished fixedpartition/LUN2 and requires su.writable=false and
su.ready=false throughout. Only verified success releases mutex and enters the
existing bdev/cdev publication; only afterward does existing code set RW/ready.

On a known-completed forward failure, the same proven negotiated rollback may
verify originalx1; live wrapper STILL forces root_allowed0/handoff_allowed0.
Timeout/unexpectedstatus/ownership ambiguity never attempts rollback. Every
live failure halts without publication, including earlyinit/publicationfailure.
Even restoredx1 does not enable Linux return in this first conservative mode.

## Runtime error and panic policy

The permanent armed flag blocks both root-host restoration and Linux-return
preparation without taking a mutex or issuing UIC. On any future panic, the
existing early return path sees failure and stays in WFE; saved DMA and owner
state are retained. There is no complex panic-time x1 negotiation or automatic
reset/return. Physical manual reset is explicitly required and logged.

The diagnostic timeout/status guards additionally apply only while livearmed:
1M poll bound, timer timeout, and unexpectedIS retain outstanding DMA/status,
never legacy abort/ACK. Once liveactive, any command error is caught by the
exact SCSI wrapper and panics BEFORE another strategy request can reuse the
bounce/descriptor, in both profile branches. Completed errors are deliberately
fatal in this first experiment too. During pre-root negotiation liveactive is
false, so known-completed errors still reach the engine rollback mechanism.
Ordinary boot and stop-only diagnostic behavior remain unchanged when liveoff.

## Reports and limits

One POLICY line explains return_blocked/manualreset. Three bounded LIVE lines
report outcome,60/18/2 proof/rollback state and profile-field meaning. Failure
then uses the unchanged20-line stop report before halting (outside mutex).
Existing profile LINK/LIMITS fields remain explicitly labeled pre-transition
baseline; they MUST NOT be mistaken for a new posttransition snapshot. The
engine's complete readback+hash proof establishes its reported retainedx2, not
those old profile fields. FirstGUI costs count actual command/copy/wait work;
no boot speed improvement is claimed. No PHY, PLL, clock, AHIT or power-policy
writes are added beyond the already validated UIC negotiation sequence.

## Validation and integration

All four host tests PASS ASan/UBSan; engine also compiles freestandingAArch64.
- test-engine.py runs full unchanged stop fault suite, plus exact frozen9327714
  reference result bytes and register/timer/delay/barrier/ownership/digest
  callback trace comparisons for all115 commands' faults/timeouts. Live tests
  cover every60 forwardcommand fault,timeout,error,ownership,IRQ/HCS failure,
  readback mismatch, digest failure/uncertainty, missinglimits and stalledtimer.
- test-adapter.py runs canonical stop adapter suite, then exact staged live
  driver callsite, actual publication/rootdev/return guard using real savedGPT
  bytes/SHA. Covers verifiedRW, earlyinitfailure, publicationfailure and second
  digest failure with successfulx1 rollback that STILL cannot publish/handoff.
- test-arguments.py uses actual PE_boot_args char* contract and608-byte bounds,
  executes existing SMP gate suite and exact normalRWbundle, plus real driver
  entry before setup and malformed/duplicate/conflicting/RO token failures.
- test-runtime-io.py extracts exact staged production SCSI loop/status/wrapper,
  verifies active timeout/error retains DMA/noabort/noACK and stops before reuse;
  legacy default + stopdiagnostic behavior and sleeping path remain tested.
- compile-integration.py compiles the entire staged UFS with captured successful
  CI flags, local generated headers and active sweet_early.h. Compilecommand,
  log and object stay here. Tests inspect exact max-value LIVE output bounds.

Parent integration after independent review: copy helperC/H/adapter plus new
ufs_two_lane_live.inc into canonical shim; apply private candidate.patch to current
activeUFS after checking basehash, add mkpatch copy/install of ufs_two_lane_live.inc and
regenerate generated files. Port private tests to tracked tools with explicit
canonical paths and generated parity; extend offline imagechecker to enforce
this live contract/manual-reset policy. Recompile/retest all existing stop,
profile/vendor and WiFi checks before newCI/image authorization. No SMP source
relaxation is required. No hardware/image runner is provided by this candidate.
