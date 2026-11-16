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

# Tiler v5 device result — 25 October 2026

The authorized addon672892d5 / kernel15143c05 boot completed. User confirms
lockscreen, touch and buttons work. USB capture is complete, no reader remains.
The probe passed compute64/64 and one texture roundtrip, then was killed by
Jetsam: namespace1/code7 = per-process memory limit. No matrix variant completed;
this is not evidence for or against the tiler hypotheses. UI Metal remains OFF.
See PROBE_MEMORY_20261025.md and the receipt for the scoped diagnostic fix.

---

# Installation update — 25 October 2026

The user authorized exact addon672892d5 and same15143c05 probe. Installation
is complete:80MiB verified writes; full rootfs hash3dfba482…20c72290 matches
the offline clone. Phone remains in RAM ADB rescue awaiting manual fastboot.
No v5 GPU result yet. This supersedes pending-approval text below; see
NOTES.md and tiler-receipt.json for the current next step.

---

# Claude review and consolidated tiler candidate — 25 October 2026

Claude session c3796e1e-85bd-462c-9f1f-1acbe9658ea0 completed successfully:
`../claude-idvs-analysis-20261025/resume-01/session.json`.
Its original report and scratch patch remain intact in that directory.
Reviewed patch SHA-256:
`f8f4d38a08b6ec9eb8345907a69507609fe51ef9e27520c7b09ad6e6a2b25f38`.
No device operations or public publication were performed in this review.

## Established evidence and remaining uncertainty

The latest device batch is still idvs-v4: compute 64/64 and five clear-only
cases pass, but 20 drawn-image and four geometry cases fail. The GPU reports
48 completions with no fault/timeout/quarantine. Positions appear in the heap;
the sampled polygon-list header remains zero. This narrows the investigation
to primitive processing/binning, but does not identify a unique cause.

Claude decoded the tiler-context writeback as a tagged polygon-list pointer.
The inferred heap address was not logged in v4; v5 logs its actual allocation.
Advancing positions across draws suggest retained allocation state. They do
not prove its scope (context, descriptor address, cache, or another mechanism)
or that a persistent heap alone fixes rasterisation. We retain the heap for
the GPU-context lifetime, following the pinned Mesa design, without claiming
the original failure is solved.

The pinned Mesa model table does not explicitly identify this G77 revision.
That is narrower than the report's claim that Mesa has never supported G77.
Likewise, failure of every matrix variant would leave an unknown cause; it
would not prove a proprietary-driver-only requirement.

## Implemented candidate

One context-owned tiler heap, referenced as GPU-written, survives until close:
2 MiB normally, 16 MiB for the opt-in diagnostic. Default descriptor bytes
remain unchanged. No kernel/MMIO/cache-policy change is included.

`NORD2_METAL_TILER_MATRIX=1` runs eleven variants before the ordinary draw
batch in the same GPU context: baseline; 256 KiB chunks; 16 MiB heap; Mesa
hierarchy mask; a single hierarchy level; combined heap/chunk/mask; constant
fragment shader; no varying stage; combined constant/no-varying configuration;
a changed descriptor VA; and repeated baseline. Red clear versus green drawn
pixels distinguish a successful framebuffer clear from actual rasterisation.
The matrix logs feature registers, actual heap VA, polygon pointer, heap
writeback, nonzero heap blocks and all tiler-context state words.

The first descriptor-only variant producing all 4096 green pixels is used for
the remaining public Metal batch. Constant-shader/no-varying/VA-spacer results
are diagnostic only. State persists across variants: ordering effects must
be considered, and the repeated baseline is an order check, not a full GPU
reset. No interpretation of these variants automatically enables UI Metal.

## Corrections added during parent review

* Target/heap synchronization failures stop the diagnostic context before
  reading pixels or submitting further jobs. No configuration is adopted
  after a broken context. Regression tests inject both synchronization
  failures and a wait failure with retained in-flight resources.
* Normal geometry tracing reads the persistent heap without clearing it.
  The diagnostic matrix explicitly initializes its own heap between variants;
  ordinary tracing no longer changes the state it observes.
* v5 qualification requires all eleven unique pixel/writeback/state records,
  feature identity and a consistent successful adoption. Missing, duplicated,
  skipped or failed matrix records reject qualification, even if later draws
  pass. Existing v2/v3/v4 qualification behavior is preserved.
* The report's retained-state/root-cause conclusions are qualified above.

## Verification

Host evidence: `gpu/build/tiler-host-review.log`,
`gpu/build/tiler-review/bridge-final-trace{0,1}.log`,
`gpu/build/tiler-native-review.log`, `gpu/build/tiler-update-review.log`.

Passed: Mesa-unpacked render semantics; ASan/UBSan transport tests with tracing
off/on, ownership/failure/concurrency/log bounds; complete/incomplete analyzer
fixtures; real AIR/Mesa/frontend/JM integration (raw GPU execution mocked);
strict iOS ARM64 compilation and bundle link; reversible addon fixture tests.
Claude additionally saved a 5600-case mask oracle and 60-case packet-identity
comparison; those are Claude's scratch test results, not device evidence.

Actual old/new archives pass update -> restore -> original uninstall on a
host directory fixture, preserving launchd. Three payload files change:
bundle executable, probe executable and varying.bc. The latter's disassembled
LLVM IR is identical apart from the input filename comment; serialized bytes
differ. No new HFS/device installation or drawn-pixel success is claimed.

## Artifact and next action

Private addon: `gpu/build/device-tiler-v5/nord2-metal-addon.tar.gz`
SHA-256 `672892d57210ee273030aa919467c3be8f999bd8e8b58835e2204e41262ba07d`
(8771346 bytes). Exact-addon authorization is pending.
Reuse unchanged probe boot15143c05…71cbecc1; UI Metal stays OFF.

After exact approval: tested RAM ADB rescue, refresh current changed blocks
using `gpu/build/hardware-20261024/idvs-test/offline-update/metal-rootfs.img`
as the local seed, offline journal/HFS/catalog checks, verified delta commit
and full readback, manual fastboot, same probe boot, initial 180s USB capture.
Do not send the old seed/rehearsal contents directly to the device.
Analyze only the latest boot, `batch-tiler-v5`.

Phone currently remains on healthy15143c05 + addon bce281f8. Preserve this
addon for rollback, audio/Wi-Fi boot a5b6e6e3, and tested RAM rescue d78bf9fe.
No stock Android over iOS userdata. Private donor sources/artifacts stay local.
