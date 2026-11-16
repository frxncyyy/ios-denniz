# Hardware update — 25 October 2026

Addon bce281f8 was authorized, installed reversibly (81MiB verified delta,
full rootfs SHA c00556a629af9bb6127714f779c7bf4171122cf1b22622ddec654063cb199952),
and tested on unchanged probe15143c05. User confirms lockscreen/touch/buttons.
UI Metal remains OFF. This supersedes the pending-install/approval status below.

180s USB capture: 411466B, SHA77fc425370ee7f4920c7f874251d1e854b62be21800f28592cd9acf806b7650e,
no transport errors. batch-idvs-v4 executes completely: compute64/64 passes,
fiveclear-only passes,20draws and4geometry comparisons fail.48completions,
0fault/timeout/quarantine. Actual position SPD80000138 confirms HALF bit.
The proven source divergence was real but insufficient to fix rendering.

All8 geometry-before samples (65792B each) are zero; positions appear after
GPU completion at advancing64B offsets. End256B sampled bins remain zero.
This does not prove all bins are empty, or establish a cache bug. In particular,
the trace's `poly` is simply the first8bytes of the tiler context and must be
interpreted against the packet layout before calling it a GPU-written pointer.
Evidence: gpu/build/hardware-20261024/idvs-test/ (batch-analysis.json,
geometry-analysis.json,health-confirmation.json and probe-usb/xnu-live.log).

At the user's explicit request, Claude Opus is analyzing the complete source
path read-only: ../claude-idvs-analysis-20261025/PROMPT.md, session.json,
stream.jsonl, eventual REPORT.md and FINAL.md. No new hardware test is started.

---

# Geometry results and the arch9 IDVS descriptor correction

## Device evidence

Exact installed addon `80b181dd6f30012261bd4c6084e67785eb2bd2dedc2910a9c53ea16daa3e8239`
ran on the unchanged approved probe boot `15143c05...71cbecc1` with UI Metal OFF.
User confirmed lockscreen, touch and buttons: “Sì, tutto funziona”. The
ten-minute USB collection finished at its deadline, without a transport error.
Private evidence: `gpu/build/hardware-20261024/geometry-test/`.
Final log: 432904 bytes, SHA-256
`b153240a40d8afd24ad757e29c4a95eba3f3d25fc1402e0ad0d4921616d239d7`.

* Compute: 64/64 correct; all four formats now admitted on hardware.
* Four geometry cases: all 4096 pixels wrong in each case.
* Twenty actual draw comparisons: output remains the clear/seed value.
* Five clear-only comparisons: pass. This alone does not prove shaders ran;
  the framebuffer descriptor also carries the clear value.
* Fourteen CPU texture roundtrips pass. IOSurface drawn-pixel check fails.
* Final status: 48 completions, zero faults, timeouts or quarantined buffers.
  `live=15` is sampled before the probe's autorelease pool drains, not leak proof.

The first four low-level heap dumps include implicit clear/preload work. They
do **not** cover all four public geometry cases. Attribute-triangle and quad
positions are present and consistent with their viewport; sampled bin pointers
are zero. `packet=1` is an input mode flag, not a generated packet pointer.
Earlier position records recur in later dumps; this suggests checking reuse or
cache state, but does not establish a cache bug. No cache policy was changed.

## Proven descriptor divergence

Pinned Mesa `81daea66703eeea1a232446e3a36902353fc3045`:

* `src/gallium/drivers/panfrost/pan_cmdstream.c`, `panfrost_upload_shader`:
  arch9 position SPD uses `MALI_WARP_LIMIT_HALF`; varying uses FULL.
* `src/panfrost/vulkan/panvk_vX_shader.c` independently uses the same limits
  for position-points/triangles versus the varying SPD.
* `src/panfrost/genxml/v9.xml`: HALF is value1 in bits8:9; default0 is FULL.

Nord2 previously left both vertex SPDs at zero. The candidate now uses HALF
for position and FULL for varying. Fragment coverage interpretation shares the
same bits, so the fragment descriptor is deliberately unchanged. Both the
ordinary and LOAD-preload draw go through this builder.

The production descriptor test, decoded by unmodified Mesa v9 unpackers,
fails on the old source at the position limit assertion and passes after this
change. It also checks varying and fragment descriptors remain appropriate.
This is a demonstrated divergence from Mesa, **not yet a demonstrated fix
for missing pixels**. It changes no kernel, shader machine code or MMIO.

Android r32p1 JM submission already matches START clean+invalidate and END
clean for this CLEAN_ONLY_SAFE device; CPU maintenance uses CIVAC. There is
no evidence here to justify changing either policy speculatively.

## Consolidated next probe

`batch-idvs-v4` retains all four geometry cases and 25 format/pass comparisons.
Bounded opt-in tracing now spans eight low-level draws, including implicit
clears/preloads. It samples the owned heap before submission, then reports
record/bin counts, first and last nonzero records and actual position SPD
after completion. It never follows GPU-generated pointers, changes allocation
contents or touches MMIO. Invalid bottom pointers are refused. Tracing OFF
performs no extra reads/maps; normal UI registration remains disabled.

Host models check empty/nonempty heaps, bounds, trace limits, allocation/map/
submit/wait failures and lifetime cleanup. They do not emulate rasterization.
The qualification analyzer recognizes v4 while still requiring every pixel
case and explicitly never qualifying UI Metal from this batch alone.

## Recovery and next action

Current phone remains healthy on the approved probe with addon80b181dd.
No rootfs or boot writes were performed during this analysis. Preserve the
verified current clone `geometry-test/offline-update/metal-rootfs.img` as a
seed; refresh changed blocks from the current phone before any later update.
Never send an old rehearsal image. New addon installation requires exact
approval, tested RAM rescue, offline validation and full device readback.
Reuse the same15143c05 probe; no UI Metal test before pixel qualification.
Audio/Wi-Fi fallbacks and original addon backups remain available. All donor
code and resulting artifacts stay private; no public CI or upload.

## Verified next artifact

Private addon: `gpu/build/device-idvs-v4/nord2-metal-addon.tar.gz`
SHA-256 `bce281f84a684b3be56885c70e97c0cbe07b854484150963bc9cc7f45daa06d9`
8768018 bytes. Approval pending; not installed. Only the bundle executable
and probe change; varying.bc and Info.plist remain byte-identical.
Strict native builds/link, actual AIR/Mesa/frontend/JM integration under
ASan/UBSan, and descriptor/lifetime/bounds tests pass. The transport test
includes70 draws per heap fixture with tracing ON/OFF and a16KiB log cap.
Actual old/new archives pass update -> restore -> original uninstall on a
host directory fixture with unchanged launchd. No new device/HFS test is
claimed. Original verified kernel/rescue/fallbacks are reused.
