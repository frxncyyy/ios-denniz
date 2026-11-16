# Memory-budget probe completed; rasterisation still failing — 25 October 2026

Phone: boot15143c05 + addon2972a959; user confirms lockscreen/touch/buttons
healthy (call_a787d6e6d96a4236bfa2dfa1da414bab:0). UI Metal remains OFF.
Latest batch-tiler-v6 completed all eleven variants, none rasterised:
0 green/4096 red in each. Compute64/64, 5 clear-only cases and14 CPU texture
roundtrips PASS;20 draw cases and4 geometry cases FAIL.59 GPU completions,
0 faults/timeouts,16 live allocations,0 quarantined. Previous6/6MiB process
cap is now confirmed;256/256MiB readback succeeded; peak22200720 bytes.
Memory fix works; actual drawing remains unqualified. All tiler state words
zero; each variant wrote only one64-byte heap block with positions.
No unique root cause proven; matrix variants share a context/order.
Evidence: gpu/build/hardware-20261025/tiler-memory-test/{probe-usb,batch-analysis.json}.
Log413234 bytes SHA9fcd4fe668d1f0074815aa741533c1731cf3e0ec0246755770c84a20af3113b8.
Capture finished, no transfer/VM active. No new phone operation authorized.
Latest seed: tiler-memory-test/offline-update/metal-rootfs.img; MUST refresh
from current device before any subsequent update. Audio/Wi-Fi baseline preserved.
Next: compare full IDVS/shader/tiler path with Android and pinned Mesa; keep
private donor code local and require exact new artifact approval before testing.

# Installation update

Exact addon2972a959 was authorized and installed.15MiB verified writes; full
rootfs hash8e4eb49b…e0c1a48 matches the offline clone. Phone is in RAM ADB
rescue awaiting manual fastboot for the already-approved same15143c05 probe.
No v6 GPU result yet; UI Metal OFF. This supersedes pending-approval text below.

---

# Diagnostic process memory budget

## Device result

Authorized kernel15143c05 with addon672892d5 booted iOS normally. The user
confirmed lockscreen, touch and buttons. The180-second USB collection is saved
under `gpu/build/hardware-20261025/tiler-test/probe-usb`. Its388224-byte log has
SHA-256 `49e65bcb06aafc6592bfdef6ff25159a6f6fa8900237c928a5eed1ea904017f8`.

The latest boot reports compute64/64, one CPU texture roundtrip and the GPU
feature identity. Immediately afterwards the probe terminates with signal9,
reason namespace1/code7. Matched XNU sources identify these as `OS_REASON_JETSAM`
and `JETSAM_REASON_MEMORY_PERPROCESSLIMIT` (`bsd/sys/reason.h:88`,
`bsd/sys/kern_memorystatus.h:290` in `gpu/build/patchtest`). This is a confirmed
process-cap termination; the previous cap and peak footprint were not logged.
The backtrace is consistent with the tiler matrix's buffer-mapping path.
No variant produced a result. Do not interpret this run as an all-red matrix,
a GPU hang, or a rendering success. The phone itself remained healthy.

## Scoped correction

The standalone probe uses `memorystatus_control` commands7/8 on `getpid()` to
set and read back256MiB active/inactive limits, retaining fatal enforcement.
It stops before loading the bundle if the API, call or verification fails.
This follows the explicit diagnostic-budget mechanism already in the private
donor's `compositor_public_probe.c`; no global Jetsam policy is disabled.
The default launchd job and other processes remain unchanged.

`TASK_VM_INFO` logs the physical footprint, resident size and available peak
at configuration, compute and draw/batch checkpoints. `batch-tiler-v6` retains
the same eleven matrix variants and rendering implementation. Qualification
requires verified policy and memory samples as well as the full existing
pixel evidence; a probe crash always prevents qualification.256MiB is a finite
diagnostic budget, not a claim about production UI memory needs.

The only payload change is the89568-byte probe. Bundle, Info.plist and bitcode
hashes exactly match installed v5. The selective offline updater copies only
changed files into rollback storage and replaces only those files. It still
verifies the full owned inventory, original launchd, source/device snapshots
and written blocks. Version1 full backups remain restorable. The initial
uninstaller still restores launchd byte-for-byte.

## Verification and artifact

`bash gpu/tools/test_probe_memory.sh` passes: ASan/UBSan tests of each API
failure/readback mismatch, analyzer complete/incomplete/crash cases, selective
update failure/tamper/rollback cases and legacy rollback, original installer,
preserved baseline, kernel source synchronization and strict ARM64 linking.
Evidence: `gpu/build/probe-memory-review/host.log`.

The actual old/new archive fixture also passes update, restore and original
uninstall. It verifies one changed payload, an89360-byte old-probe backup, and
unchanged inodes for all other payload files. This is host verification, not
yet an HFS/device result. Evidence: `device-tiler-memory-v6/update-rehearsal.json`.

Addon: `gpu/build/device-tiler-memory-v6/nord2-metal-addon.tar.gz`,8771788bytes,
SHA-256 `2972a9596a1be7312c539f118c0f9d6168be465ebab2f7104f68ccfee2b21fa4`.
Same probe boot15143c05; UI Metal remains disabled. Exact-addon approval is
pending. Latest local seed is `tiler-test/offline-update/metal-rootfs.img`;
refresh it from the current phone before any future device write.
