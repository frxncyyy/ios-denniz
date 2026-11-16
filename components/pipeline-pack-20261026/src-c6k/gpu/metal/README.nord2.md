# Native Metal on Nord2: device qualification candidate

The full frontend from private ios-tegu `7f6f7ca9` is connected to a native
Mali-G77 **Job Manager** backend. It does not submit donor CSF commands and
does not route rendering through OpenGL ES. The old `Nord2Metal.m` minimal
compute shim and synthetic/stub bundles are not installed.

Actual iOS arm64 artifacts now exist: `build/native/Nord2Metal.bundle`, an
executable API/pixel probe, a strictly linked full XNU kernel, two boot variants
and a separate rootfs addon. All are local/private. This is not a declaration
of complete Metal conformance or working accelerated UI. Hardware compute
passes64/64; four geometry cases and all actual draws still fail, while five
framebuffer-clear checks pass without GPU faults/timeouts. The IDVS
descriptor correction is installed and hardware-tested but does not fix the
missing pixels. Independent source analysis is delegated to Claude. Read
`gpu/docs/IDVS_GEOMETRY_20261024.md` and `gpu/docs/idvs-receipt.json`.
Addon bce281f8 ran on probe15143c05. User confirms lockscreen/touch/buttons.
USB capture completed normally; all pixel draws still fail. UI Metal stays OFF.

## Implemented path

- Metal device, command queues/buffers, encoders, textures, buffers, blits,
  function constants, AIR specialization/translation and pipeline caches are
  reused from Tegu. Native AIR/metal2vulkan and Mesa compilers are statically
  linked into the bundle; only iOS system frameworks remain dynamic dependencies.
- AIR → SPIR-V → actual Mesa arch9 code, paired position/secondary vertex
  programs and fragment programs, hardware blend conversion, resources/FAU,
  64-bit GPU VAs, MALLOC_VERTEX → FRAGMENT jobs and acknowledged completion.
- R8, RGBA8, BGRA8 and RGBA16F targets; CLEAR, LOAD, clear-only passes, scissor,
  dynamic viewport, supported fixed-function blending and channel masks.
  Two same-format color targets use ordered per-target draws with both old
  attachment contents snapshotted before either output is written.
- Runtime textures/samplers, framebuffer-fetch snapshots, indexed input and
  retained IOSurface imports. Shared-storage copies execute in command order;
  they do not replace shader execution with a CPU renderer.
- Separate per-client GPU address spaces, quota/bounds checks, synchronized
  maps and submits, error completion, fault quarantine and acknowledged reset.
  A failed draw/unsupported accepted-path request invalidates the command;
  Nord2 does not turn a refused render pipeline into a successful no-op.
- Power/clock/PMIC integration uses the existing owner thread, with an explicit
  `nord2_gpu=1` boot opt-in. `nord2_metal=1` publishes discovery properties;
  the initial probe image keeps system registration off.
- Probe checkpoints go through bounded diagnostic selector11 to kernel USB
  logs (16 KiB/client, 240 bytes/call); full userland diagnostics persist in
  `/private/var/db/nord2-metal/probe.log`.

## Evidence on the PC

`docs/continuation-receipt.json` records the source/artifact hashes and logs.

- ASan/UBSan descriptor/MMU/memory/scheduler/power/transport tests pass,
  including actual unmodified Mesa v9 packet decoding, high GPU addresses,
  failure/quarantine cases, concurrency and pre-draw snapshot copies.
- Six captured QuartzCore shader variants from the matching iOS16 metallib
  compile for all four target formats: 24 cases. Four additional masked-blend
  cases pass. These are compiler/metadata tests, not rendered pixel tests.
- Native arm64 AIR, Mesa, complete frontend and API probe link without
  `undefined dynamic_lookup` or placeholder symbols. Probe compute AIR also
  translates and compiles with an actual 32×1×1 local size.
- Full private kernel link preserves every baseline archive member except
  the two explicit integration objects (registry table and owner polling
  hook). The original audio, Wi-Fi, display and touch driver objects remain.
- Exact boot images fit 32 MiB; see the respective image receipts. Recovery
  contents are retained using the previously prepared recompressed rescue.
- Offline addon installation/rollback preserves all 530 jobs in the actual
  rootfs launchd table and restores its original bytes. Installer refuses
  missing/corrupt payloads, path collisions and unrelated rollback edits.

## Boundaries and remaining proof

This is a bounded first UI/compute implementation. It does not implement all
Metal APIs: depth/stencil, MSAA, indirect execution, arbitrary instancing,
multiple vertex-buffer layouts, all pixel formats or later Apple GPU families
remain outside the admitted hardware path. Broad translator support alone
must not be confused with native backend coverage. Performance and memory
pressure of snapshot-based LOAD/MRT are unmeasured.

Next requires the phone and exact-image approval: power/ID; public Metal device
load; compute readback; five 4096-pixel passes across four formats and an
IOSurface; then actual backboardd rendering and audio/Wi-Fi regression. No
hardware success, Metal lockscreen or performance result is claimed.

See `docs/METAL_DEVICE_PLAN.md` for fresh-rootfs preparation, installation,
qualification and recovery. Never overwrite current userdata with the old
local rootfs image.

## Reproduce locally

Prerequisites/pins are preserved under ignored `build/` and `research/`:
real iOS SDK, Apple Rust target, pinned Mesa, metal2vulkan, baseline CI archive,
XNU stage, CCTools linker and LLVM15. Keep the private Tegu derivatives local.

```
python3 gpu/tools/build_native_compilers.py air
python3 gpu/tools/build_native_compilers.py mali
NORD2_AIR_LLVM_BIN=/path/to/llvm15/bin bash gpu/tools/run_host_tests.sh
python3 gpu/tools/build_kernel_support.py
bash gpu/tools/build_kernel_integration.sh
python3 gpu/tools/link_kernel_local.py
python3 gpu/tools/pack_gpu_image.py
python3 gpu/tools/pack_gpu_image.py --system-metal
python3 gpu/tools/package_metal_addon.py
```

### Pipeline pack (no compile stalls on the first boot of a build)

Every pipeline cache key contains the bundle's build id, so a pack is made per
bundle, after `link_metal_bundle.py` and before `package_metal_addon.py`. What
survives across builds is the *corpus*: build-independent pipeline descriptions
(`TPC_MISS v1 air` records: library SHA-256, function constants, targets,
blends, vertex layout, profile). The host replays them with the real frontend
and compilers under the new build id:

```
# once per device boot log (the log is of bundle B; --bundle says which build produced it)
python3 gpu/metal/tegu/pipeline_corpus.py scan LOG... --bundle B/Nord2Metal.bundle --out gpu/build/pipeline-corpus.txt
# per candidate bundle (a replay is one heavy job: wait for the build lease)
python3 gpu/tools/build_pipeline_pack.py --bundle gpu/build/native/Nord2Metal.bundle \
    --corpus gpu/build/pipeline-corpus.txt --work gpu/build/pipeline-pack-<tag>
python3 gpu/tools/package_metal_addon.py --pipeline-pack gpu/build/pipeline-pack-<tag>/PipelinePack.tpcpack
```

Console logs from builds with `TEGU_PSO_REC` carry the complete record (function
constants included) of every pipeline a boot built or served, so one boot of a
candidate extends the corpus for the next; older logs only verify descriptions
whose constants are known (see `pipeline_corpus.py`). The pack is private-donor
material and is never published; `package_metal_addon.py` refuses a pack made for
another build id. `gpu/tools/pipeline_cache_report.py LOG --pack PACK` measures
what a pack would have served in a boot log of the same build.

Packing refuses to overwrite an existing image; preserve/move the previous
artifact deliberately. `audit_quartzcore.py` requires a local matching Apple
metallib which must stay ignored. No build script contacts the phone.
