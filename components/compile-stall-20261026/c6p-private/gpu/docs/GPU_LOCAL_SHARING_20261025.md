# GPU rasterisation works; viewport clipping remains — 25 October 2026

Exact approved boot1e9d810a + installed addon2972a959 booted successfully.
User confirms lockscreen/touch/buttons (call_0c165229bfbe49a88fc5d8c81921d529:0).
180s capture FINISHED, no error/capacity limit. 409080 bytes SHA9c88011e248ae2ddad4074820dc2ce579e70261d079dec62a82b392d979d5ad0.
No USB reader/transfer/VM active. Phone healthy; no rollback required.

All11 tiler variants now4096 green/0 red (previously0/4096). Raw actual BO
leaves confirm SH=3. Compute64/64,20/25 pixel cases,4/4 geometry,14 texture
roundtrips PASS.59GPU completions,0fault/timeout/quarantine. Only viewport
cases fail:752 wrong pixels in each of5 targets. UI Metal stays OFF, full
qualification false. GPU sharing correction restores actual drawing.
Evidence: gpu/build/hardware-20261025/shared-test/probe-usb,
batch-analysis.json and result-summary.json.

Next: correct viewport/scissor intersection in Metal renderer against Mesa
panfrost_emit_viewport. Current oversized triangle geometrically predicts
exactly752 extra pixels outside16..48 viewport because scissor stays64x64.
Need host regression, updated addon build, exact-artifact approval and repeat
probe before UI enable. Keep same working1e9d810a kernel. No new device write
authorized. Private source stays local. Refresh latest rootfs seed before any
future addon update (tiler-memory-test/offline-update/metal-rootfs.img).

---

# GPU-local sharing correction — 25 October 2026

## Evidence and scope

The installed boot15143c05/addon2972a959 completed the full v6 matrix:
compute64/64 PASS;11 tiler variants each0 green/4096 red;5 clear-only PASS,
20 draw cases FAIL;4 geometry FAIL;14 CPU texture roundtrips PASS.
59 GPU completions,0 fault/timeouts. The probe memory limit fix worked:
256MiB readback,21.2MiB peak. User confirms healthy lockscreen/touch/buttons.
Hardware rendering is still unqualified; a completed GPU job is not proof of
correct pixels. The eleven comparisons share one context and order.

Claude's resumed analysis reached the same MMU parity gap, then exhausted its
session quota. No additional Claude patch was applied. Parent implementation
and verification continue locally; private donor sources are not uploaded.

## Demonstrable Android divergence

Exact device Android r32p1 source, not the older sibling driver:
`drivers/gpu/mediatek/gpu_mali/mali_valhall/mali-r32p1/drivers/gpu/arm/midgard/`.

- `mali_kbase_mem.c`, JIT allocation: requests `BASE_MEM_COHERENT_LOCAL`,
  including the JM tiler-aligned allocation path.
- Region flag conversion: `BASE_MEM_COHERENT_LOCAL` -> `KBASE_REG_SHARE_IN`,
  independently of the CPU coherent-system capability.
- `mmu/mali_kbase_mmu_mode_aarch64.c`, `get_mmu_flags`: SHARE_IN becomes
  `SHARE_INNER_BITS`, defined as3<<8 in `mali_kbase_defs.h`.
- Cached default memory attribute remains index0. Uncached regions clear
  sharing; the new mapper rejects the uncached+inner combination.

Our old page leaves set SH=0 for every BO. The candidate sets SH=3 for ordinary
BOs (heap, descriptors, shader resources, targets and imported surfaces),
retaining index0, permissions, CPU cache maintenance and CPU coherency NONE.
This is a concrete parity correction and a plausible cause of missing tiler
output, **not a proven diagnosis of the Nord2 rasterisation failure**. Android
JIT evidence directly establishes heap policy; applying the same local-sharing
policy to all ordinary BOs is our driver design, not a claim that every Android
allocation uses identical flags.

The special Android r0p1 dummy-job buffer retains its original non-shareable,
read-only, NX mapping (`map_flags=6`); the vendor workaround is unchanged.
Ordinary RO/RW and executable/NX controls are preserved. Idempotent remaps now
require matching attributes as well as physical addresses; conflicting or
invalid mappings fail before changing any earlier page. Initial and large BO
allocations log actual raw leaf attributes, without dumping physical addresses.

## Host verification

`gpu/tests/android_mmu_oracle.py` extracts and compiles the unchanged Android
`get_mmu_flags` and original constants into an ignored build fixture. It walks
our raw page tables independently:44 combinations,176 leaves including four
GPU pages per16KiB CPU page. It checks permissions, invalid attributes, rejected
shareability/permission changes, atomic rejection and exact WA flags.

ASan/UBSan suites: MMU,5000-operation randomized mapping model,6000-operation
page-table reclaim, buffer/quarantine, scheduler, production-core lifetime
(14 scenarios/1024 reuse cycles), and actual-core vendor workaround PASS.
Canonical source/shim/staged patch sync and preserved audio/Wi-Fi/display
checks PASS. See `gpu/build/shared-memory-tests/` and `android-mmu-oracle/`.
An initial ad-hoc fuzz compile used stricter warning flags than the established
suite and rejected an existing omitted zero-initialized log callback; rerun
with the established suite flags passed. No production failure was suppressed.

The native build replaces only MMU,core and diagnostic IOKit objects in the
verified board/WA kernel link. All26 baseline link inputs are hash-checked
before/after; original artifacts are preserved. Strict link succeeds; existing
framework-path/section-alignment warnings remain. Receipt:
`gpu/build/mmu-shared-candidate/receipt.json`. Packing/ARMv8.2 compatibility and
boot checks are recorded separately in `device-mmu-shared-candidate/`.

## Device test gate

Boot-only candidate; same installed addon2972a959, shaders and v6 probe.
No filesystem update needed. UI Metal remains OFF. New exact image approval
is required before flash. Capture latest boot only and confirm MAP entries
show SH=3 (`leaf_attr=0x743`, with NX bit54 where applicable). Require compute,
clear/draw/geometry/texture and GPU health evidence; do not enable Metal UI
merely because jobs finish or one matrix variant succeeds.

Fallback: currently working probe boot15143c05 or audio/Wi-Fi boota5b6e6e3;
tested RAM ADB rescue d78bf9fe. No stock Android over iOS userdata.
