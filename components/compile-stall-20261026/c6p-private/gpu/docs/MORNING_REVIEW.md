# Morning review — 24 October 2026

This is the authoritative state after the parent review. The overnight
`MORNING_REPORT.md` below its review notice and `gpu/out/` package describe the
older DeepSeek revision; they are retained as history, not current artifacts.

## Outcome

**Not ready for a GPU/Metal device test or an iOS graphics switch.** The overnight
work provides useful arch9 backend modules and real shader compiler tests, but
did not deliver the requested complete Metal implementation. The actual Tegu
Metal render frontend has not been ported. No GPU ID, compute token, triangle,
IOSurface fence, displayed pixel or sustained Metal frame was measured on Nord 2.

The supervisor `nord2-metal-night-20261024.service` was checked inactive/dead,
MainPID 0, before editing. `runtime/status.json` records the deadline at
2026-10-24 06:00 UTC / 08:00 Europe/Rome, turn 15. The OpenCode session is
`ses_d09f0cf5acaf59632250133533`, model
`opencode-go/deepseek-v4.1-flash#max`. Review started only after 08:00.

The overnight branch remains at `deepseek/nord2-metal` / `0ff8789`. Morning
changes are isolated on `codex/nord2-metal-review`, with no remotes, publication
or CI upload. The original main checkout's working audio/Wi-Fi implementation
is preserved. No phone, USB, block device, mount, rootfs or boot operation was
performed. Current known-good boot remains `nord2-ios-audio-calibrated-playback-stop23.img`
(SHA-256 `a5b6e6e3c86a825e2828018597e687922496f82dbd1ff705113a99db1fb6be41`).

## Demonstrated defects corrected during review

| Area | Defect and correction | Evidence |
| --- | --- | --- |
| Mali MMU | Intermediate entries used type 1 (large block), not type 3 (next-level table). Corrected encoding/walker and 40-bit table-address validation. | Independent raw page-table walker failed at three levels before the fix and passes after it; vendor r32p1 AArch64 MMU format. |
| Compute packet | Shader environment pointed directly to machine code. Added the required SHADER_PROGRAM descriptor and pointed the environment to it. | Whole compute packet, environment and shader program byte-compared against pinned Mesa v9 packers. |
| Small allocations | Every 128-byte compute command was rejected by the minimum 4096-byte allocation check. Backing now rounds nonzero requests to XNU 16 KiB pages and charges the rounded quota. | Production allocation helper tested for 128/160-byte requests, boundaries and overflow. |
| SCPsys | MFG1 checked the wrong status bank (0x258 instead of 0x228); SET/CLR aliases were read-modify-written. Corrected banks, write-one operations and separated ordered power/isolation writes. | Independent register-bank model with poisoned SET/CLR reads and unrelated-bit checks; vendor MT6885/MT6893 power source. |
| Device tree | APMIXED was incorrectly declared at infracfg 0x10001000. Corrected to 0x1000c000; removed an unsubstantiated dma-coherent claim. | Vendor mt6893.dts and generated-ADT regression. |
| User-client ABI | Input/output sizes and pointers were used without sufficient validation; imported surfaces lacked rounded quota/bounds checks. Added production preflight validation, aligned charging and compute bounds. | ASan/UBSan malformed-call and allocation tests, plus native ARM64 compilation. |
| Shared state | Different clients raced on the same AS0/scheduler/quota; mapping retained the wrapper rather than the returned memory descriptor. Added provider serialization and corrected descriptor ownership. | Source lifetime/lock-order review and native compilation; concurrency on hardware remains untested. |
| Completion | Failure interrupt bits 16..18 were ignored; success read JS_STATUS unnecessarily. Handle both classes and clear captured bits before a subsequent job can start. | Vendor r32p1 JM IRQ flow and native compilation. |
| Job reuse | All 16 scheduler records stayed occupied after successful completion; running IDs were truncated to 32 bits. Successful WAIT now consumes the completion record; IDs remain 64-bit; reused records preserve FIFO by ID. | 1024 submit/complete/reap cycles using IDs above 2^32; queued-order and reference-count checks under ASan/UBSan. |
| Data visibility | Compute submit cleaned the command/shader but omitted FAU and resource buffers. Added CPU-to-device cache synchronization before dispatch. | Source path review and native compilation. GPU-to-CPU visibility remains a blocker below. |
| Diagnostic controls | The advertised read-only path programmed AS0, RESET merely reset software, and shutdown could free unproven DMA backing. Snapshot mode now has no AS setup/submission, RESET refuses, uncertain backing stays pinned. | Source path review and native compilation; intentionally conservative until real reset/stop acknowledgements exist. |
| Test/integration scripts | Piped failures could be hidden by tee; copied/compiled modules and generated patch lists diverged. Enable pipefail and synchronize all canonical modules/headers, including render-plan and transport. | Full suite, source-sync check and patch reverse-apply check. |

Canonical sources are `gpu/src/` and `gpu/include/`; synchronized copies live in
`shim/nord2-xnu/` and `xnu/patches/0084-nord2-gpu.patch`. The compute ABI is still
a diagnostic contract, not general Metal execution: zero register preloads and
no TLS/advanced resource layout are supported by this packet builder.

## Evidence produced on the PC

The final integrated suite ran with:

```sh
NORD2_AIR_LLVM_BIN=$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin \
  gpu/tools/run_host_tests.sh
```

`gpu/build/morning-review/final-full.log` passed:

- Production descriptors versus Mesa v9, plus 2000 randomized cases.
- Independent hardware-format MMU walk, plus 5000 mapping-model operations.
- ABI/allocation bounds, BO lifetime/quarantine, scheduler and 3000 randomized
  scheduler operations; the new 1024 completion/reuse stress is included.
- SCPsys model, board model, JM/render-plan builders, compute-only frontend
  core and Objective-C syntax/ABI stub link. Models do not validate hardware.
- Actual Mesa arch9 compilation of authored compute/vertex/fragment fixtures
  and negative inputs, with disassembly/reflection.
- Actual AIR -> SPIR-V -> arch9 translation for the authored no-resource
  vertex/fragment fixture pair: 1572/5320-byte SPIR-V yields 1920/384-byte Mali
  code. This proves those compiler cases, not a functioning Metal renderer.
- Native ARM64 compilation of all nine new kernel units into the partial
  `gpu/build/libnord2gpu.a`; generated patch reverse-applies to the stage.

After that suite, the ADT base/coherency correction and comment-only SCPsys
header update were checked separately: `final-adt.log`, source synchronization,
baseline preservation, patch reverse-apply and `git diff --check`. No further
production C/C++ change followed the final integrated run.

The linkable `gpu/build/ios-bundle/Nord2Metal.bundle` is an **ABI stub only**,
using dynamic symbol lookup and handwritten declarations. It has no demonstrated
Metal.framework loading, render pipeline or presentation. Do not install it.
The partial kernel archive and synthetic link are not a complete kernel build.

Final synthetic relink: baseline 17,849,472 B, with GPU 17,915,296 B,
**delta 65,824 B**. The known-good boot has only 12,288 B margin. This is a size
warning, not an exact boot-size prediction: final linking, compression and image
packing have not been done. An earlier -Os experiment also exceeded the margin.
There is no exact new boot image or SHA suitable for an authorization request.

`morning-review-receipt.json` records code revision, source/artifact/test-log
hashes and constraints. The older `gpu/out/nord2-gpu-candidate.tar.gz` is stale
and must not be mistaken for the reviewed tree.

## Remaining work before a useful device candidate

1. **Source-backed power and clocks.** `Nord2GPU::regulatorSet` and `cgSet`
   still return false. Implement the MT6315 VGPU/PMIC VSRAM callbacks, clock
   parent/gates and bounded rollback from the Android source. The SCPsys engine
   exists, but has not run on hardware. Implement GPU-internal L2/tiler/shader
   power requests: the current board sequence waits for READY after reset
   without explicitly powering those blocks on. Test the state transitions,
   including failure recovery, rather than faking READY from the outset.
2. **MMU/cache and lifetime.** Implement vendor-compatible AS lock-region/
   update/invalidate protocol and GPU-to-CPU completion synchronization;
   establish stop/reset acknowledgment before freeing or recycling any page.
   The current VA allocator grows monotonically and page-table slots are not
   reclaimed. Bound/reclaim them only after verified invalidation. The service
   must handle client close, faults, reset and buffer release without dangling
   DMA references. Morning quarantine retention avoids unsafe freeing, but is
   not a complete recovery implementation.
3. **Job capabilities and isolation.** Read JS_FEATURES and select supported
   slot capabilities instead of relying on unverified vertex/fragment/compute
   indices. Validate nested resource/FAU GPU addresses and compiler reflection;
   all clients currently share AS0 with owner 1. Provider locking alone is not
   process/resource isolation. Finish asynchronous completion/fences and
   register preload/TLS/resource contracts before a general frontend can use it.
4. **Actually port Tegu's frontend.** The present 446-line `Nord2Metal.m` is a
   compute ABI shim. Reuse the donor device/queue/command-buffer/render/blit/
   texture/IOSurface/runtime AIR implementation and its tests; adapt the
   arch11 CSF backend to G77 arch9 Job Manager, including MALLOC_VERTEX/IDVS
   and compiler metadata. Build and resolve the real bundle, not a stand-in.
   Absence of a full iPhoneOS SDK is not by itself a reason to stop: the donor
   `tools/tegu/metal/build_compute_bundle.py` already uses freestanding Clang
   and framework TBD stubs. Verify that route locally and inventory actual
   missing dependencies rather than assuming an external SDK is mandatory.
5. **Presentation and rollback.** A retained SweetSurface import exists, but
   there is no completed userland IOSurface mapping, rendering/fence handoff,
   cache-consistent presentation or QuartzCore integration. Keep CPU scanout
   and audio/Wi-Fi intact until real Metal pixels are verified. Do not publish
   MetalPlugin registry properties for the current incomplete ABI stub.
6. **Private local build/packaging.** Produce a full kernel and real bundle
   using a private/local route, preserve proven rescue contents, solve the
   measured size pressure, and prepare reversible install/rollback with exact
   image checks. Never upload private ios-tegu-derived sources to public CI.

These are implementation gaps, not issues that should be discovered by
repeated flashes. Complete and review the offline contracts first. Hardware
validation will still require the user's return and explicit approval of the
exact prepared image, followed by ID/power, token, real render/readback,
IOSurface/fence/presentation and sustained UI plus audio/Wi-Fi regression tests.

## Primary sources used for the review

All references are local and pinned by the stage’s `references.json`:

- Android kernel `04d00c2565764c3922c9b4826b5acff1106e533f`:
  `drivers/gpu/mediatek/gpu_mali/mali_valhall/mali-r32p1/drivers/gpu/arm/midgard/mmu/mali_kbase_mmu_mode_aarch64.c`,
  `backend/gpu/mali_kbase_jm_hw.c`, `mali_kbase_js.c`,
  `drivers/clk/mediatek/clk-mt6885-pg.c`,
  `drivers/gpu/mediatek/gpufreq/v1/mt6893/mtk_gpufreq_plat.c`,
  `arch/arm64/boot/dts/mediatek/mt6893.dts`.
- Mesa `81daea66703eeea1a232446e3a36902353fc3045`: generated v9 packers,
  `src/panfrost/lib/pan_jm.c`, shader environment and shader-program layout.
- Private ios-tegu `7f6f7ca9c58ab840cb1a9c306e7cba3bddd2d572`:
  Metal frontend/build scripts and pinned metal2vulkan
  `720ef1ce9ae2a2068999a8675717418b9416283e`.

Review fixes and tests improve the candidate; they are not a claim that every
remaining defect has been found or that the GPU path can yet execute safely.
