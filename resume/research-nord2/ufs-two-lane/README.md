# Staged pre-root G4 x1 -> x2 -> x1 diagnostic

Future candidate only. Not linked, patched, in CI, packed or executed on hardware.
Current frozen CI source and active sweet_ufs.c were not changed by this task.

`diagnostic.c/.h` is the actual transaction/rollback engine, with injectable
MMIO/time/barrier/ownership/read-digest callbacks. It compiles for AArch64 but
has no kernel adapter or boot argument hook. `test.py` runs the exact source
with modeled MMIO under ASan/UBSan and cross-compiles it; it never accesses a
phone. Test success establishes model behavior, not physical lane1 readiness.

## Deliberate narrow prerequisites

Explicit optin==1; exclusive current root-controller mutex and descriptor/LUN2
ownership; no published root, no enabled root writes, no concurrent I/O or
clock/power manager. HCE enabled; HCS DP/UTRLRDY/UICREADY and host/device error indicators bits4/5 both clear; empty transfer/task
doorbells; IE/IS=0; five UIC error-code registers all zero. AHIT must already be
zero. No AHIT, debug selector, H8, PHY, clock, reset, IE or media writes exist.
A missing prerequisite blocks root publication/writes with zero tuning SETs.

Snapshot18 local attributes: seven Linux power configuration fields, power
mode, G4 adaptation and nine timeout fields. Snapshot6 additional limits:
available/connected RX/TX, local UniPro version and maxRXHSgear. Require exact
G4x1 B FAST/FAST, terminations1, both available+connected2, UniPro1.8(mask5),
maxRXgear4, adaptation0/1 and all9 selected Linux default timeouts. Unknown,
unsupported or unexpected values fail closed. No inferred TX peer capability
or physical lane1 readiness claim is made; failure remains possible.

Read LUN2 primary GPT LBA1/count5 (20480bytes) directly through controller-owned
DMA, never through a filesystem/block cache. Compute SHA256 over all bytes and
compare a pinned known digest BEFORE any SET. Saved primary header4096 plus
entries16384 digest is `0000000000000000000000000000000000000000000000000000000000000000`. Adapter must independently validate fixture
identity/geometry before compiling/passing that digest; tests use synthetic
bytes and do not pretend to validate this private hardware digest. Successful
fresh SCSI transfer is evidence of active data path at that point. AHIT0 plus
exclusive owner/no power manager must prevent a transition between this read
and negotiation; AHIT0 alone is not active-link proof. No unsupported vendor
probe/debug-selector assumptions are used.

## Transaction and restoration protocol

Selected Linux references: ../nord2-current-kernel/drivers/scsi/ufs/ufshcd.c:
4125–4191 asynchronous power completion and4449–4532 SET ordering;
ufs-mediatek.c:1462–1504 initial-adapt policy; unipro.h attributes/defaults.
Each ordinary GET/SET waits fresh UCC bit400 and UIC ready; PA_PWRMODE SET
requires BOTH UCC400 and async power bit10, with HCS UPMCRS==LOCAL1. Every
result byte is checked. Only that transaction's own clean completion bits are
W1C acknowledged. Unexpected IS, dirty UIC error state, timeout or lost
ownership means uncertain state; issue no further commands or rollback.
Polls have500ms and500000iteration bounds and a20second run bound. Deadlines
cannot interrupt a hung MMIO or callback. No retry, UIC abort or host reset.

Transition: initial adaptation1; reissue unchanged RXgear4/RXlanes2/RXterm1,
TXgear4/TXlanes2/TXterm1,HSseries2; all9 snapshotted Linux default timeouts;
PA_PWRMODE11 negotiated request. Verify all18 GET values with lanes2/adapt1;
repeat the exact direct GPT read and pinned digest comparison.

Always negotiate original lanes1 using the same full sequence, even on a
known-completed preparatory error. Verify18 fields; restore original adaptation
0/1 by checked SET; verify all18 again; reread GPT and compare digest. There is
no path that leaves x2 enabled or treats raw lane writes as rollback.
If forward validation failed but negotiated rollback is fully verified, report
failure/root_allowed0 with handoff_allowed1. If rollback or state proof fails,
uncertain1/root_allowed0/handoff_allowed0. A successful diagnostic is x2 test
AND proven original x1, root_allowed1; it is not persistent throughput tuning.

## Integration plan (not implemented)

1. Add dedicated opt-in boot argument and link engine only in a separate future
   candidate. Do not merge it into the presently frozen image.
2. Hook after exclusive takeover, GPT/root-bound selection and all observation
   probes, BEFORE bdevsw/devfs publication, su.ready or su.writable enable.
3. Adapter `owned_preroot` must inspect the real mutex ownership, fixed LUN2,
   DMA bases/run state, pending UIC ownership, publication/write flags and
   prohibit another caller/cleanup/interrupt path from consuming completions.
   Transfer descriptor addresses/LUN/root bounds never change in this engine.
4. Adapter read_digest issues exact1+5 direct device read via existing bounce
   maintenance/protocol, checks OCS/response identity/residual, hashes20480bytes;
   leaves unexpected/fatal status visible. read_digest return0 means success,1 is ONLY a known-completed read error, every other nonzero (including timeout) marks state uncertain even if a doorbell was cleared. DSB SY callback plus compiler barrier
   must use the existing arch path. Frequency comes from actual CNTFRQ.
5. Record every result including phase/last command/result/IS/HCS, no printf
   inside controller lock. Block root publication/writes on EVERY failure.
   On uncertain state, preserve DMA and storage ownership, prohibit generic
   Linux fallback/normal cleanup, never reset/free/rebind behind the engine.
   Only explicit independently audited drain/restore could change that policy.
6. Root/peer review and physical incoming LIMITS evidence are mandatory gates
   before any candidate build/hardware proposal. This host stage authorizes no
   flash/boot/tuning and does not prove lane1 PHY readiness or performance gain.

## Verification

`python3 research/nord2/ufs-two-lane/test.py` passes exact-source ASan/UBSan and
AArch64 freestanding compile. It faults each of115 baseline/forward/rollback
commands independently with completed UIC errors, timeouts, ownership loss, host/device error indications and UIC errors; all forward/rollback readback mismatches; every incoming
AP gate and key limit; missing ordinary/async completion, bad UPMCRS, unrelated
IS, stalled/wrapped counters; baseline/forward/rollback read errors and digest
corruption. Only model data and state are used. More tests are appropriate if
peer review changes the protocol or adapter contracts.

## Staged kernel adapter (2026-10-20 follow-up)

The plan above now has a private adapter implementation, still NOT merged into
active sweet_ufs.c or linked into any CI/image. `sweet_ufs.base.c` plus base.json
freeze its source origin. `sweet_ufs.c` is the isolated modified copy;
`adapter.inc` supplies exact kernel callbacks; integration.patch is a review/merge
artifact against the captured base. Root is concurrently editing the active
vendor path, so do not blindly overwrite the active file with this snapshot.
Canonical pure engine copies live in shim/nord2-xnu/ufs_two_lane.{c,h}; those
are unlinked. Private aliases match them; the staged copy includes ufs_two_lane.c
exactly once and must NOT also link a separate engine object.

Dedicated stop-only argument nord2ufs2lane=1 arms a permanent fallback block at
root-init entry, including early init failure. After takeover/GPT/profile under
the mutex, it runs the experiment and unlocks ONLY for final reports, then stops
before bdev/devfs/ready/writable publication regardless of result. It does not
boot iOS onward or chain into normal Android. Existing interrupt/tick/watchdog
path remains active; reports occur once with no repeated stop output. The
permanent diagnostic flag also blocks restore/prepare_return even if engine
successfully restored x1. Manual physical recovery is needed by design.

The adapter asserts real mutex ownership, pins selected root bounds, LUN2,
DMA base/list/run state, requires unpublished/not-writable state and sleepable
context. Its UIC writes synchronize the existing pending-UIC ownership flag.
No media write callback exists. A staged-only SCSI change prevents UTRLCLR
abort on diagnostic read timeout and preserves unexpected IS instead of generic
ack-all; otherwise a cleared timeout/fatal bit could be hidden from the engine.
Timeouts map to uncertain, not known-completed read errors. SHA is over exact
LBA1/count5 primary GPT bytes through existing cache/bounce transfer protocol.

Incoming18 attributes and6 limits now reside in the result with separate masks,
so a timeout-policy refusal emits concrete observed values. Seven report lines
are each checked below255bytes even with maximum32bit values. Final stop is
outside the UFS lock. No new raw watchdog writes or PHY/clock controls.

`python3 research/nord2/ufs-two-lane/test-adapter.py` passes exact adapter
ASan/UBSan ownership/context/descriptor/write-allowlist/read-error tests, static
hook/fallback/abort-order checks and maximum-value report length checks. The
whole staged sweet_ufs.c also passes local ARM kernel compilation using the
existing nord2-xnu-cc.py environment; not a CI build or hardware result.

The adapter host test additionally runs the exact engine through the exact
adapter with the real saved primary GPT header/entry bytes and real SHA256,
using modeled fresh UIC completions. It verifies all115 transactions,3reads,
balanced shared UIC-pending ownership, restored lanes1/adapt1 and permanent
stop-only fallback block. This remains a host model, not physical UFS evidence.
