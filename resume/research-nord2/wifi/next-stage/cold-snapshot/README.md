# Ready isolated candidate, not linked or hardware tested

snapshot.c/.h is a pure exact3-register observer with injectable check/read
callbacks. adapter.inc is the proposed native bridge, and sweet_touch.candidate.c
is a private copy demonstrating the exact insertion. Active sweet_touch.c and
current CI sources were NOT changed. integration.patch is explanatory, not a
automatically applied patch.

Requires BOTH nord2wifi=2 and new nord2wificold=1. Default adds no map/read/log.
Runs inside the existing power probe after cfg_id read and successful
HW ID20010101, before success/out/nw_restore. The adapter rechecks existing
AP power/control/twoack/clock/twoprotection prerequisites before each new read,
then host sleepack. Maps only source-supported CONN_RGU4KiB; reuses existing
host4KiB map. Exactly three NEW registers, in this order:
18000010 CPUresetbit0,18000000 topbit7,180602cc powerstatusbit2.
Cold requires all three maskedbits0. Raw otherbits retained. No WFSYS-local
version read. Callback/read failure or noncold data cannot bypass the outer
probe restoration; existing restoration refusal/panic stays unchanged.

At most3newreads, no retry/loop/write/CPUrelease/EMImapping/SMC. This is a
bounded operation count, not a recoverable MMIO-timeout promise: a hardware bus
stall cannot be interrupted by software. Existing powered/readable/id guard
must remain. Check/read callbacks are caller-owned and serialized; no new
worker thread or PMIF concurrency. Lines are tested below actual255char cap.

Validation performed:
- python3 .../cold-snapshot/test.py: exact helper+adapter ASan/UBSan; every
  prerequisite/read failure, exact address/mask gates, optout/default/no-map,
  cold/noncold observations, caller restore invocation on all paths.
- snapshot.c compiled with actual BSD ARM64 flags via tools/nord2-xnu-cc.py.
- Private full sweet_touch.candidate.c compiled with actual BSD ARM64 flags.
No monitor/device operations occurred. A hardware pass needs all3valid+cold,
power ID20010101/restored1 and unchanged all8 SMP/input/lockscreen behavior.
It is not authority to place ROM or infer effective EMI permissions.

# Smallest next common-init transaction

First prepare a FOUR-register D-die retention-control transaction, not the
full common-init/A-die port. Selected mt6893_pos.c:780–800 writes:
18000038=0,1800003c=ff,18000050=0,18000054=1.
These are SYSRAM/COEXT HW powerdown/sleep controls. Prerequisites: guarded
owned CONN window, all3cold state reads, existing cleanup armed and no active
shared radio. Snapshot ALL4 before any write, validate supported initial
field bits, write source values, barrier/readback each, then restore original
controls and verify BEFORE original CONN isolation/poweroff/rail restoration.
Partial write failure must be treated as touched; restore failure retains
ownership/power and refuses handoff. No blind write of unknown control bits.
Host model should inject each read/write/readback/restore failure and preserve
all existing power/PMIF state. Do not call this complete D-die init: efuse
handoff into SYSRAM and optional buildmode handoff remain unimplemented. Do
not log private efuse/calibration values.

After this small transaction is proven, prepare SPI sleep-controller table
programming as a SEPARATE transaction from mt6893_pos.c:805ff. It includes
Wi-Fi/BT/GPS address/command fields, so cannot be reduced to an arbitrary
handful of writes while claiming full SPI initialization. Audit its entire
source write list/masks first, snapshot every destination before mutation,
select the Wi-Fi/non-BT-only table, require exclusive cold shared controller,
and verify+restore all accepted writes. Do not trigger sleep-controller
commands, release A-die reset, run RF SPI or calibration in the first table
transaction. A source-supported TOPSPI A-die ID-only stage can follow once
strap/reset/semaphore/clock prerequisites are owned; details and primary URLs
are in research/nord2/wifi/emi-abi-and-common-next.md.

The ambiguous positive2 SMC return does not block these CONN-side cold and
retention/SPI ownership milestones. It continues to block any claim that the
ROM transaction's AP-permission prerequisite has been established.
