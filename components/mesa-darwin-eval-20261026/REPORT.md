# mesa-darwin (rottenapple) evaluation for Nord2: decision memo, 2026-10-26

Evidence labels:
- **[src]** read in source.
- **[log]** seen in our device logs.
- **[their]** their claim, not verified.
- **[guess]** my estimate.

Path abbreviations:
- `REF` = `.../references/mesa-darwin-20261026/mesa-darwin`
- `W` = `.../overnight-20261024/work`
- `P6` = `W/gpu/build/kernel-p1-20261026/patched-sources-0096`

## Summary

1. **Their Mesa does not recognise our GPU.** Our GPU_ID 0x90800011 maps to `PAN_PROD_ID(9,0,0)`. Their model table only has (9,0,1) and (9,0,3) for G57 and (9,2,4) for G68, so Gallium refuses to open the device. Adding one table entry fixes that. Our own compiler is unaffected because it only uses the architecture number.
2. **Running all of Gallium under our Metal frontend ("full Path B") is not recommended.** It would need:
   - a new kernel ABI: syncobjs, a 128 MiB tiler heap that grows on GPU page faults, unlimited buffer lists per submit, and the full set of GPU parameters Mesa queries;
   - a Metal-to-Gallium translation layer;
   - a retargeted lowering, which invalidates our qualified corpus and test suites.

   It also would not remove the costs we actually measure. The useful parts can be copied one at a time instead ("Path B-pieces").
3. **Our frame time is not caused by missing Gallium features.** It comes from:
   - kernel cache maintenance on every job;
   - a synchronous wait after every job;
   - too many full-screen jobs per frame;
   - **a 10 Hz cap on panel presents**: no UI boot-args line sets `sweetfb_hz`, and the kernel default is 10.

   The fastest route to 30 fps is: present fixes, kernel P6 (patch 0096), tile fetch on by default, then asynchronous submission.
4. **Several of their kernel bug findings apply to our code verbatim** (same SweetSurface/SweetFramebuffer lineage):
   - per-plane bytes-per-row is written at the wrong offset;
   - `IOSurfaceClientAddress` is ignored;
   - unknown IOSurface selectors return success with zeroed output;
   - the vsync timer is re-armed only after the present finishes.

## 1. Does this Mesa support our GPU (G77, arch v9)?

**No, not as shipped.**

**How the ID decodes** [src] (`REF/mesa/src/panfrost/model/pan_model.h:25-41,128-133`):
- Mesa builds the product ID from arch_major, arch_minor and product_major.
- For 0x90800011: arch_major 9, arch_minor 0, arch_rev 8 (ignored), product_major 0. The product ID is therefore **0x090000**.
- The revision is r0p1.

**What the table contains** [src] (`REF/mesa/src/panfrost/model/pan_model.c:89-94`):
- arch 9 entries: (9,0,1) G57, (9,0,3) G57, (9,2,4) G68.
- No (9,0,0) entry. Our pinned Mesa `81daea66` also has only the two G57 lines (`references/mesa/src/panfrost/model/pan_model.c:77-79`).

**What happens on open** [src]: Gallium gives up on an unknown model (`REF/mesa/src/gallium/drivers/panfrost/pan_device.c:44-51`, `if (!dev->model) goto err_free_kmod_dev;`).

**Our compiler does not care.** It passes `gpu_id 0x90000000` directly (`W/gpu/tools/nord2_mali_compile.c:32-33`). `W/gpu/tools/frame_model.py:575-585` already notes the G57 fallback.

**Needed for Gallium, panvk or perf counters:**

```c
VALHALL_MODEL(PAN_PROD_ID(9, 0, 0), 0, "G77", "G77", MODEL_ANISO(NONE), MODEL_TB_SIZES(16384, 8192),
              MODEL_RATES_X(2, 4, 8, 32, 32, 8)),
```

- **Tile-buffer sizes and rates** [guess]: copied from G57, which is derived from the G77 core. Verify against the vendor (kbase/DDK) value.
- **Anisotropic filtering**: set to NONE until tested on r0p1.
- **Mesa-side quirks**: the quirk struct only has `no_hierarchical_tiling` and `max_4x_msaa` (`pan_model.h:89-96`). Neither applies to Valhall.
- **Hardware errata** for this GPU revision live in the kernel. Our kernel runs jobs without faults, but I did not audit it against the vendor erratum list.
- **GPU parameters missing from our kernel**: our `nord2_gpu_status` (`W/gpu/include/nord2_gpu_transport.h:31-54`) lacks parameters Mesa asserts on:
  - TEXTURE_FEATURES0-3 and AFBC_FEATURES;
  - MAX_THREADS and the THREAD_* values;
  - SYSTEM_TIMESTAMP_FREQUENCY.

  The full list is in [their] `REF/NOTES.md:112-115`.

## 2. Path A: CoreAnimation on OpenGL ES (excluded; lessons only)

**CIF10 / `b3a8` deep framebuffer.**
- CoreAnimation picks `b3a8` when MobileGestalt reports a P3 display (from the device-tree `artwork-display-gamut`) and `CAEnableDeepFramebuffer` allows it [their] `REF/U2-PROGRESS.md:43-59`. Their robust fix is to set the gamut to "sRGB" in the device tree, which turns CIF10 off everywhere.
- Our preferences fix (`W/gpu/tools/package_metal_addon.py:43-50`) works for backboardd: its display surfaces are plain BGRA [log].
- **However, SpringBoard still creates 20 `b3a8` two-plane surfaces** in the c6k UI log (`0x62336138`, up to 13.9 MB, within the first 64 logged creates) [log] (`W/gpu/build/hardware-20261025/c6k-test/ui654-usb/xnu-live.log`).
- No `TEGU_IOSURFACE_REFUSED` appears, so I cannot link this to a visible defect. It is a latent risk: our frontend refuses to import formats 550/554 from an IOSurface outside the private path (`W/gpu/metal/tegu/render_encoder.inc:538`).
- **Recommendation**: add the device-tree gamut override as well.

**Unaligned linear stride produces zeros.** On their hardware, sampling a linear texture whose row stride is not a multiple of 64 bytes returned zeros, and that whole job's output was lost [their] `REF/U2-PROGRESS.md:70-81`. This supports:
- our shadow copy for unaligned IOSurfaces (c6f);
- the view that the integration test's 4320/4680-stride expectation is the error, not production code.

**Present must wait for the GPU.** Without that they saw stale tiles: 13 to 260 glitches per 1000 frames. They fixed it with kernel patch 0126 (present only when the GPU is done) or a userspace frame wait [their] `REF/U2-PROGRESS.md:204-222`.

**Their frame budget** [their] (`REF/U2-PROGRESS.md:169-176`; Mali-G57 with 2 cores, 828x1792 screen):

| Metric | Value |
|---|---|
| CoreAnimation frames/s | about 47 |
| Panel presents/s (capped by `sweetfb_hz=30`) | about 31 |
| Draws per frame | about 143 |
| CPU time per draw | about 70 µs |
| Batches per frame | about 21 |
| Kernel calls per frame | 42 SUBMIT, 26 WAIT_BO |
| GPU busy per frame | about 0.25 ms |

The CPU and kernel were the limit, not the GPU.

## 3. Path B: keep our Metal frontend, use panfrost Gallium (or parts of it) as the backend

### Our measured cost

Source: c6k run at 654 MHz, baseline, 44 windows and 816 commands [log] (`W/gpu/build/hardware-20261025/c6k-test/ab-ui654.txt`).

Per command buffer:

| Metric | Value |
|---|---|
| Command buffers per second | 7.3 |
| Time per command buffer | 63.1 ms |
| Kernel jobs | 26.9 |
| Full-screen jobs | 13.7 |
| Draws | 44.6 |
| GPU time per kernel job | 2.34 ms |
| Our encode time per draw | 0.173 ms |

Per kernel job:

| Metric | Value |
|---|---|
| Queue time, including cache clean of 745 µs | 753 µs |
| Run time | 980 µs |
| Cache invalidate | 749 µs |
| Total until return | 2500 µs |

Other observations from the `TEGU_PERF` and `NORD2 GPU JOBS` lines [log]:
- about 11.8 MB is cleaned and the same amount invalidated per job;
- every job is waited for synchronously (`waits=399` for `n=399`);
- the main reasons for starting a new job: reading the target 190, target change 136, multiple render targets 50.

So **27 serialised jobs × about 2.3 ms ≈ 63 ms**, which is the whole frame time.

### Full Path B: Gallium's `pipe_context` under our Metal frontend

**What Gallium needs from the kernel:**
- buffer creation with a GPU address below 4 GiB, CPU mapping, address query, buffer wait, MADVISE and SET_LABEL;
- vertex/tiler and fragment work submitted separately on two job slots, ordered with syncobjs [their] `REF/NOTES.md:106-125`;
- a 128 MiB tiler heap that grows on GPU page faults [src] (`pan_device.c:96-103`; their header `REF/rottenapple-drm/include/rottenapple_gpu_uapi.h:186-194`);
- an unlimited buffer list per submit [src] (`pan_jm.c:110-133`).

**What our kernel offers** (`W/gpu/include/nord2_gpu_transport.h:8-30,92-104`):
- allocate, submit (one request can carry both vertex/tiler and fragment heads), wait by job id, sync, and IOSurface import;
- **a maximum of 32 buffers per job**;
- a 64 MiB private memory budget;
- no syncobjs and no grow-on-fault heap.

**Two ways to close that gap:**
- **(a) A userspace shim like theirs, translating onto our kernel.**
  - Merge Gallium's two back-to-back submits into one of ours; they are issued together under one lock (`pan_jm.c:196-216`).
  - Emulate syncobjs with job sequence numbers, since our kernel executes in order per context.
  - Patch Mesa to use a fixed-size heap.
  - The kernel would still need to accept more than 32 buffers per job.
- **(b) Implement their ABI in our kernel.** Their header is MIT-labelled and documents the semantics. A heap that grows on GPU faults means handling MMU faults and resuming the job, which is hard and risky.

**Frontend work:**
- Every encoder (render, compute, blit) would be rewritten as Gallium calls, and every Metal resource would become a Gallium resource.
- IOSurfaces would need a new import handle type; they never wired one into Mesa (`REF/NOTES.md:162-164`).
- Our shader lowering targets our own descriptor layout (buffer table slots 8 to 30, SRT, thread/workgroup local storage). Gallium's binding model is different (`pan_nir_lower_sysvals.c`, `pan_nir_lower_res_indices.c`).
- Retargeting it invalidates the 3148-entry corpus, the 939/1005 integration checks and the C6 hardware qualification.

**Effort and risk** [guess]: months, high risk. It also does not remove cache maintenance, synchronous waits or the present cap. Their kernel still paid 0.4 to 0.6 ms of cache maintenance per job [their] `REF/U2-PROGRESS.md:115,139-140`.

### Path B-pieces: copy the useful mechanisms into our bridge (recommended)

We already work this way: `nord2_metal_render.c` cites Mesa pan_* functions at lines 62, 123, 176, 411, 426, 465 and 501.

**One job pair per render pass, any number of draws** (Gallium: `pan_jm.c:180-224`, `pan_job.c`)
- Ours: draws batch only on the same target, later draws must use LOAD, attachment reads are excluded, and a batch is limited to 32 buffers and the heap's geometry budget (`nord2_metal_bridge.c:1480-1517`).
- Action: raise the kernel's buffer cap (for example to 128), use a larger GPU-only heap (patch 0096 flag bit 1), and relax `batch_prefix_fits`.

**Read the framebuffer from the tile buffer, not memory** (Gallium: `src/panfrost/compiler/pan_nir_lower_framebuffer.c`)
- Ours: the tile-fetch variant (TF1) exists and cut kernel jobs by 37% and full-screen jobs by 28% [log].
- Action: make it the default, after explaining why encode time per draw rose from 0.17 to 1.2 ms in that window; pipeline compiles are the likely cause.

**Pre-frame preload** (Gallium: `pan_fb_preload.c:1173-1240`; on v9 it uses early-ZS-always / intersect modes)
- Ours: the pre-frame variant (PF1) was disabled automatically after errors.
- Action: compare our pre-frame descriptor field by field with `pan_preload_emit_pre_frame_dcd`, using a host-side oracle.

**Wait only when a resource is actually contended** (Gallium: `pan_job.c:822-860`)
- Ours: every job is waited for; the async variant (AP1) was disabled after errors.
- Action: submit asynchronously and record each buffer's last write sequence number; wait only for CPU access or present. Kernel patch 0090 already provides an async wait on sequence number.

**Buffer cache, keep CPU mappings** (Gallium: `pan_bo.c`; their patch 0005)
- Ours: command-buffer pool and code cache already do this. No action.

**AFBC compression and CRC tile elimination** (Gallium: `pan_afbc.c`, `pan_desc.c`)
- Ours: not used.
- Action: later. Display surfaces are read by the CPU present copy, so AFBC would need a GPU resolve, and the GPU is not the bottleneck.

**Tiler heap** (Gallium: `pan_device.c:96-103`)
- Ours: a 2 MiB heap that is CPU-mapped, so it is cache-maintained on every job.
- Action: allocate it GPU-only; a fixed 16 to 32 MiB [guess].

**Effort** [guess]: days per item. Each one can be tested with our existing host harnesses and the on-device A/B variants.

## 4. Fixes that apply regardless of path

### a) The panel shows at most 10 frames per second today [src] [log] (highest priority, smallest change)

- `P6/iokit/Kernel/arm/SweetFramebuffer.cpp:247-249` defaults `hz` to 10, so the panel is updated every 6th tick of the 60 Hz timer. The staging tree has the same default (`W/gpu/build/xnu-stage/.../SweetFramebuffer.cpp:230-232`).
- No captured Metal UI boot-args line sets `sweetfb_hz`: c6fc, c6gc, c6ic, and c6k at 350 and 654 MHz (for example `c6k-test/ui654-usb/xnu-live.log:1140`).
- Whatever CoreAnimation produces, **at most 10 frames per second reach the panel**. The early SpringBoard images did use `sweetfb_hz=30` (`W/NOTES.md:10117,10125`).
- The boot command line is already 600 of 607 characters (`README-0095-0096.md`). Either change the kernel default to 30 (later 60), or add a short alias such as `n2fbhz=`, as patch 0095 did for other arguments.

### b) Present timing [src]

**The vsync clock is stretched by every present.**
- `vsyncFired` re-arms the timer only after `present()` returns (`P6/.../SweetFramebuffer.cpp:347`, then `:362`).
- So the tick period, and the vsync notifications CoreAnimation paces itself on (`:290-300`), become 16.7 ms plus the present time.

**`present()` is expensive:**
- it copies about 10 MB (1080x2400 at 4 bytes) into panel memory with the CPU;
- then `nord2_display_refresh()` busy-waits up to 10 ms for the display link to be idle and up to 20 ms for frame-done (`W/gpu/build/xnu-stage/osfmk/arm64/sweet_early.c:664-690`);
- [guess] transferring 2400 lines to a command-mode panel takes about 8 to 16 ms;
- for scale, they measured about 6 ms of CPU copy for a 5.9 MB frame [their] `REF/U2-PROGRESS.md:116,140`.

**Fixes:**
- re-arm the timer on a fixed absolute schedule before doing the work;
- start the panel transfer and check frame-done on the next tick instead of spinning;
- move the copy off the timer thread.

**Present does not wait for in-flight GPU work.** `present()` invalidates the surface's cache (`:527`) but does not wait for GPU writes still in progress. Today the synchronous per-job waits hide this. Once submission is asynchronous, adopt their rule: present a surface only when its GPU work is done, otherwise show the previous complete frame [their] `REF/U2-PROGRESS.md:215-220`.

### c) Cache maintenance [log] [src]

- Ours costs about 0.75 ms clean plus 0.75 ms invalidate per job, about 11.8 MB each way.
- Theirs was 0.4 to 0.6 ms per job, and that was about half of backboardd's CPU time [their].
- Kernel patch 0096 (track whether the CPU can see a buffer, a GPU-only allocation flag, and an IOSurface lock protocol) is the right fix. It has not run on hardware yet.
- Bridge follow-ups:
  - allocate the heap, thread/workgroup local storage and private targets GPU-only;
  - in compute dispatch v2, add the shader code buffer to the list first (per the README-0087 table).

### d) Per-plane bytes-per-row written at the wrong offset [src] (same bug as theirs)

- `P6/iokit/Kernel/arm/SweetSurface.cpp:593` writes each plane's bytes-per-row at offset `+0x08`.
- Their reverse engineering says IOSurface.framework reads it at `+0x10`, so `IOSurfaceGetBytesPerRowOfPlane` returns 0 [their] `REF/U2-PROGRESS.md:63-66`.
- Our offset (`+0x0c`) and size (`+0x14`) fields already match their layout.
- This affects every multi-plane surface (`b3a8` plane 1, YUV).
- Fix: write the value at `+0x10`, then check `IOSurfaceGetBytesPerRowOfPlane` on the device.

### e) `IOSurfaceClientAddress` is ignored [src] (same as theirs)

- Our surface creation (`SweetSurface.cpp:279-350,690-707`) always allocates a fresh zeroed buffer, even when the caller supplies its own memory.
- They found that CoreAnimation creates image surfaces this way for images without a pixel-format code. The textures came out all zero and icons were missing [their] `REF/U2-PROGRESS.md:131-138`.
- [guess] That code is in CoreAnimation's renderer-independent image path, so it probably affects our Metal CoreAnimation too. I could not confirm it: our creation log only prints the number of keys, and only for the first 64 surfaces (`:710`).
- Fix: map the caller's memory (`IOMemoryDescriptor::withAddressRange` plus `prepare()`). At minimum, fail creation when the key is present, so CoreAnimation falls back to uploading. Log once when the key is seen.

### f) Unknown IOSurface selectors report success with zeroed output [src]

- The `default:` case at `SweetSurface.cpp:775-784` returns success with zeroed outputs.
- In c6k, selectors 5, 9, 20, 27, 40, 41 and 43 were called [log].
- In their kernel, selector 6 (`IOSurfaceWrapClientImage`) failing made CoreAnimation retry it every frame, about 11% of CPU samples [their] `REF/U2-PROGRESS.md:172-173`. Ours would "succeed" with an empty result instead. Selector 6 does not appear in our c6k log.
- Action: identify each of these selectors and decide success or failure deliberately, rather than defaulting to success.

### g) Cost of each IOKit call

- They measured about 37 µs for a generic IOKit method call [their] `REF/U2-PROGRESS.md:176-177`.
- We make about 2 to 3 calls per job (submit plus wait). That is negligible at today's per-job cost, but it will matter once maintenance drops to tens of microseconds.
- Async submission with batched sequence waits brings it to about one call per job. Their trap-based call path (`RA_GPU_FEATURE_TRAPS`) is the next step after that.

### h) MADVISE, SET_LABEL and keeping CPU mappings

- Their overhead came from Gallium's own kernel calls: 7% of samples in SET_LABEL and 7500 MADVISE calls per 10 s. We do not make these calls.
- The equivalent rule for us, no kernel call per draw, already holds thanks to the command-buffer pool and code cache.

### i) LaunchServices and MobileGestalt freezes [their] (`REF/U2-PROGRESS.md:149-166`)

- When `lsd` rebuilds its database and IONetworkController is missing, each MobileGestalt query blocks for 5 s. Every LaunchServices client blocks with it, including SpringBoard on app launch.
- This is not a GPU problem. Before blaming the GPU for a UI stall, check whether our root filesystem provides IONetworkController and whether `lsregister` runs while SpringBoard is up.

## 5. Their Mesa vs ours

**Versions:**
- Ours: `81daea66`, 2026-06-18, 26.2.0-devel [src], pinned in `W/gpu/tools/build_mesa_arch9.sh:12` and `W/gpu/metal/tegu/build_mali_compiler.py:15`. Our lowering sits on top of it; `d91bb485` is our built compiler library, not a Mesa revision.
- Theirs: `3dfa4800`, 2026-10-20, 27.0.0-devel, plus 5 patches that only touch kernel-interface, fence and build code.

**Diff** (both clones are shallow, so file level only, using `git diff --no-index --numstat`):
- `src/panfrost/compiler`: 138 files, +34.8k / −4.8k lines. Most of it is `kraid/`, the new Rust backend, which we build without (`-Dpanfrost-rust=false`).
- C backend changes:
  - `bifrost_compile.c` +371/−676, `bifrost_nir.c` +453/−211, `pan_compiler.c` +444;
  - `pan_nir_lower_tex.c` +436/−92, `pan_nir_lower_image.c` +221;
  - `bi_ra.c` +73/−26, `va_merge_flow.c` +93, `va_perf.c` +72.
- New C passes: `pan_nir_opt_push_ubo.c` (432 lines), `pan_nir_lower_divergent_scratch.c` (189), `pan_nir_lower_mem_to_global.c` (112), `pan_nir_lower_image_64bit.c` (69), `pan_nir_lower_vs_inputs.c` (57).
- `src/compiler/nir`: 124 files, +11.4k / −2.6k. `src/panfrost/genxml`: 13 files, +2.9k.

I did not review what these changes do. Any gain is unknown until our corpus is replayed through them.

**Assessment:** shader quality is not our bottleneck. GPU run time is about 1 ms per job, mostly full-screen load and store, and our encode costs 0.17 ms per draw. Rebasing means re-qualifying the lowering, the 3148-entry corpus and the hardware. **Defer**, and cherry-pick a specific fix only if the corpus shows a concrete miscompile.

## 6. Recommendation: fastest plan to 30 fps or more, with CoreAnimation on Metal

Expected time per command buffer [guess, extrapolated from the c6k numbers]:

| Stage | Kernel jobs | ms per job | Time per command | Result |
|---|---|---|---|---|
| Today | 27 | 2.34 | 63 ms | about 15/s, then capped at 10 by the panel |
| After step 2 (patch 0096) | 27 | about 0.9 to 1.0 | about 25 to 27 ms | |
| After step 3 (tile fetch default) | about 17 | | about 16 to 18 ms | |
| After step 4 (async) | | | about 15 to 20 ms, the larger of GPU and CPU | 30 to 50 command buffers/s |
| With step 1 (panel at 30 Hz) | | | | 30 fps on screen |

Steps, in order:
1. **Present path** (small kernel change):
   - make 30 the `sweetfb_hz` default, or add a short alias set to 30; go to 60 once present cost allows;
   - re-arm the timer on a fixed absolute schedule;
   - stop spinning on panel frame-done;
   - log the present time in the framebuffer diagnostic line.
2. **Run P6 (patch 0096) on hardware**, A/B against tracking level 1 (`n2trk=1`). In the bridge, allocate heap, local storage and private targets GPU-only, and add compute v2 shader code first.
3. **Tile fetch on by default**, after explaining the encode-time jump in the TF1 window (probably pipeline compiles, not tile fetch itself).
4. **Asynchronous submission with per-buffer tracking**, Gallium style: wait only for CPU access, readback or present, and present only after GPU completion (4b).
5. **SweetSurface fixes** (4d, 4e, 4f) and the device-tree sRGB gamut override, for correctness: missing images and CIF10 still on in SpringBoard.
6. **Only if still needed:**
   - match the pre-frame preload to `pan_fb_preload.c`;
   - raise the buffer cap and heap size for longer batches;
   - investigate the read-target breaks to cut full-screen passes.

**Do not:**
- move CoreAnimation to OpenGL ES;
- port Gallium's whole `pipe_context`;
- rebase the compiler onto `3dfa4800` now;
- enable AFBC or CRC on display surfaces, which the CPU present copies;
- relax Mali's 64-byte linear-stride rule (their hardware test produced zeros);
- raise budgets or caps without patch 0096's accounting;
- treat the kernel's whole-context fallback for a full 32-buffer list as the long-term answer.

## 7. Licensing and provenance

**Mesa** is MIT-licensed, with license headers in place (for example `REF/mesa/src/panfrost/model/pan_model.c:4` and `REF/mesa/src/gallium/drivers/panfrost/pan_jm.c:5`). Anything we take must keep its copyright and permission notices.

**Their shim and kernel interface header** are labelled MIT (`REF/rottenapple-drm/src/rottenapple_drm.c:13`, `REF/rottenapple-drm/include/rottenapple_gpu_uapi.h:11`). There is no copyright or author line, and the sibling project's terms are unconfirmed. Use them as reference only and re-implement from the documented behaviour, rather than copying, until provenance is confirmed.

**Apple SDKs:**
- The SDKs (`sdk/` in their layout, `REF/NOTES.md:19`) are absent from the extraction and must never be redistributed.
- `prefix-ios/` and `out/` may contain binaries linked against the SDK; do not redistribute them either.
- Their reverse-engineering notes on QuartzCore, OpenGLES and MobileGestalt offsets are interoperability information, not code to copy.

**Other third-party code:** SPIRV-Tools and the mako/markupsafe packages come from AOSP (`REF/NOTES.md:17-18`) and keep their own licenses.

**Our tree:** nothing from their tree was copied into ours by this evaluation.