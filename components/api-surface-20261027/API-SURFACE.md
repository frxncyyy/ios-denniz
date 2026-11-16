# Metal frontend API surface ledger (2026-10-27, host only)

Scope: the Tegu Metal frontend (`gpu/metal/tegu/compute_frontend.m` + `.inc`) as a Metal device for iOS 16.5
clients: CoreAnimation/backboardd, SpringBoard, UIKit, and Metal-using system apps/frameworks.
Everything below is **host-tested or source-compiled only**. No phone, USB, adb, publication.

## How the inventory was made

* `api_inventory.py` parses the **iPhoneOS 16.5 SDK Metal headers** (protocol bodies and properties, iOS <= 16.5),
  maps each protocol to the Tegu class that must implement it, and compares against the selectors implemented in
  the frontend sources (`selscan.py` joins multi-line method headers, includes categories). Output:
  `inventory.json` (per-protocol rows with the implementing classes). Re-run: `python3 api_inventory.py --json inventory.json`.
* "Implemented" in the tool means "a method body exists". This ledger separates **IMPL** (does what Metal documents,
  host-tested), **REFUSE** (selector exists, call poisons the command buffer / returns nil+NSError / is traced; never
  counted as support), **BASE** (answered by the real `_MTLDevice` superclass, not verifiable on host) and **MISSING**.
* Usage evidence (what clients actually call): device logs `gpu/build/hardware-20261025/c6l-test/*-usb/xnu-live.log`
  (the frontend logs only `TEGU_DEVQ supportsFamily/supportsFeatureSet`, `TEGU_FRONTEND UNSUPPORTED gpu_priority`, pass/draw
  refusals; no unrecognized-selector line exists because such a call kills the process), the UI corpus
  (`gpu/build/ui-corpus-20261025`, `pipeline-corpus`), and the reference traces in `ansuz-metal` docs (CA-METAL 1/2/7/8,
  VULKAN-METAL-RESEARCH 8.6: "CA uses no argument buffers", "no indirect draws in QuartzCore, CoreImage uses indirect
  *compute* dispatch", "CoreImage calls newLibraryWithSource: and the stitching SPI", MPS function-constant scans,
  CAMetalDrawable.sharedEvent for app layers).
* The device advertises Apple1/Apple2 (`TEGU_DEVQ supportsFamily 1001,1002 = 1; 1003.. = 0`). Tessellation, argument
  buffers, indirect command buffers, mesh/tile shading, ray tracing, sparse, MTLIO are feature-gated by
  `supportsFamily:`; their entry points are refused (nil + trace), never a crash.

## Ranking by real usage (implemented in this order)

| # | gap (before) | who calls it | outcome |
|---|---|---|---|
| 1 | `encodeSignalEvent:value:` invalidated the command buffer; `newEvent/newSharedEvent/newFence`, `encodeWaitForEvent:value:`, shared-event listeners did not exist (unrecognized selector = process death) | CAMetalDrawable.sharedEvent (app layers), MPS/CoreImage/SceneKit encoder ordering, any multi-queue app | **IMPL** (below) |
| 2 | `newLibraryWithSource:` returned Apple's foreign library object (functions not ours -> pipeline refused as foreign class) | CoreImage kernels, ANGLE/WebKit (GLSL->MSL), SceneKit/SpriteKit snippets, apps | **IMPL** via the base front end (host-tested with a fake base; device-unproven) |
| 3 | async creation (`...completionHandler:`) for compute/library/tile; `options:reflection:error:` render | virtually every game engine | **IMPL** (synchronous result delivered through the handler, like the existing render/function handlers) |
| 4 | command buffer/queue misc: `commandQueue`, `retainedReferences`, `errorOptions`, `logs`, `computeCommandEncoderWithDescriptor:`, `blitCommandEncoderWithDescriptor:`, `commandBufferWithUnretainedReferences`, `insertDebugCaptureBoundary`, `newCommandQueueWithMaxCommandBufferCount:`, `sampleTimestamps:gpuTimestamp:` | Metal-using UIKit/system frameworks | **IMPL** |
| 5 | compute encoder: `setBuffers:offsets:withRange:`, `useResource(s)`, `useHeap(s)`, `memoryBarrier*`, `updateFence/waitForFence`, LOD-clamped samplers, `dispatchType` | MPS (heavy `useResource/useHeap`), CoreImage | **IMPL** |
| 6 | `dispatchThreadgroupsWithIndirectBuffer:` | CoreImage indirect compute | **IMPL** (execution-time planning; below) |
| 7 | render encoder: `setViewports:count:`, `setScissorRects:count:`, store-action/option setters, `useResources/useHeaps`, fence + barrier selectors, sampler LOD clamp forms, `setVertexTexture:atTextureIndex:samplerState:...` | engines, SpriteKit/SceneKit | **IMPL** (count must be 1; depth/stencil store only DontCare) |
| 8 | `MTLParallelRenderCommandEncoder` | SceneKit and multi-threaded engines | **IMPL** (record/replay; below) |
| 9 | `MTLBinaryArchive` | engines caching pipelines | **IMPL as honest no-op cache** (never claims a hit) |
| 10 | blit `copyFromTexture:...sliceCount:levelCount:`, `optimizeContentsForCPUAccess:`; texture `parentTexture/rootResource/swizzle/isSharable/compressionType...`, view-with-swizzle (identity only); buffer debug markers; function/library reflection stubs | frameworks that probe | **IMPL** |
| 10b | capability/identity queries answered explicitly instead of through the base `_featureQueries` ivar: `readWriteTextureSupport`, raster-order groups, 32-bit filtering/MSAA, BC, barycentric, dynamic libraries, ray tracing, function pointers, `productName/familyName/vendorName`, `sharedMemorySize`, `requiresIABEmulation`, `minimumTextureBufferAlignment...`, `supportsVertexAmplificationCount:` (all NO / traced `TEGU_DEVQ`) | frameworks probing before using a feature | **IMPL** |
| 0 | **resource options**: `newBufferWithLength:options:` returned nil for EVERY options != 0 (private 0x20, write-combined 0x1, hazard untracked 0x100/tracked 0x200); `newTextureWithDescriptor:` returned nil for any CPU-cache or hazard bit; heaps refused WC/untracked | every game engine, MPS, MetalKit (untracked is the default for heaps) | **IMPL** (found by the option-matrix probe; cache/hazard bits validated and normalised, app's options reported back) |
| 10c | buffer-backed textures (`-[MTLBuffer newTextureWithDescriptor:offset:bytesPerRow:]`) were a refused selector | MPS, CoreImage, ANGLE, video | **IMPL** (2D, 1 level, 64-aligned; `-buffer/-bufferOffset/-bufferBytesPerRow`) |
| 10d | `newBufferWithBytesNoCopy:` was a refused selector (nil) | MPS, engines (reference plugin implements it for CA) | **IMPL** (`contents` = caller pointer; GPU-side copy refreshed per command; blit write-back of the written range only; compute writes are NOT copied back) |
| 10e | command-buffer failures used a private error domain | clients branching on `MTLCommandBufferErrorDomain`/codes | **IMPL** (domain `MTLCommandBufferErrorDomain`; code 2 Timeout for an expired event wait, 1 Internal otherwise; description text) |
| 11 | counters, dynamic libraries, shared textures, rasterization-rate maps, stitched libraries, argument encoders, ICB creation, MTLIO, acceleration structures, resource-state encoders | not Apple2-baseline | **REFUSE** (nil + `NSError` + `TEGU_FRONTEND UNSUPPORTED <name>`) |

## What each IMPL means (semantics + test)

All cases in `gpu/metal/tegu/api_surface_host_test.inc`, driven by `gpu/tests/api_surface_frontend_test.py`
(builds the production frontend with the render-harness prelude, real AIR bridge, real Mali compiler and real n2mb
bridge over the fake JM; ASan+UBSan unless `API_FAST=1`).

* **Events / fences** (`api_events.inc`). `TeguFence` is an identity object: fences order encoders within one queue and
  the queue is strictly serial, so encoders only validate class+device and count updates. `TeguEvent` / `TeguSharedEvent`
  (`signaledValue`, `setSignaledValue:`, `waitUntilSignaledValue:timeoutMS:`, `notifyListener:atValue:block:`) are real:
  `encodeSignalEvent/encodeWaitForEvent` append an **event pseudo-job** at the encode position (a new `Dispatch.eventKind`).
  Every reorder/batch pass already treats a non-draw job as a barrier. Execution: signal = earlier GPU jobs reaped, then the
  value is stored and listeners at or below it are scheduled; wait = the queue thread polls (shared backoff) until the value
  is reached, bounded (`TEGU_EVENT_WAIT_LIMIT_S`, 30 s; Metal's watchdog also fails stalled buffers) -> command fails with an
  error instead of hanging the queue. A signal that did not run because an earlier job failed is still delivered after the
  loop. Events made by another implementation (an IOSurface-backed `MTLSharedEvent` a framework created itself) are honoured through the
  public protocol (`setSignaledValue:` on signal, polling `signaledValue` on wait); a plain foreign `MTLEvent` has no readable value and stays refused.
  Cross-process sharing needs a Mach port: `newSharedEventHandle` / `newSharedEventWithHandle:` return nil (traced).
  Tests: ordering around a wait on another thread (work before runs, work after does not), cross-queue hand-off, timeout,
  failed producer still signals, foreign/other-device/nil/active-encoder/after-commit refusals, listener threshold/ordering/
  immediate delivery, block+queue retain balance, event dealloc with pending listener.
* **newLibraryWithSource:** calls `[super newLibraryWithSource:options:error:]` (Apple's MTLCompilerService front end in
  `_MTLDevice`), serializes the resulting library (`serializeToURL:error:` to `$TMPDIR`) and rebuilds it as a Tegu library
  through `newTeguLibraryWithBytes:` so functions belong to this bridge. Compiler error, a library that cannot serialize and a
  nil source yield nil + NSError. Reference: the same route is used by ansuz (`rottenapple_metal_device.m:601`). **Not proven on
  device**: depends on MTLCompilerService being reachable in the process (sandbox) and on the 16.5 `_MTLLibrary` having
  `serializeToURL:error:`.
* **Indirect compute dispatch** (`api_surface.inc`, event kind 3). The v2 plan bakes the grid into the specialized pipeline and
  push constants, so nothing can be planned at encode time. The encoder snapshots its bindings (buffers, textures, samplers,
  threadgroup lengths) into a pseudo-job; when it runs (after earlier GPU jobs were reaped) it reads the three `uint32`
  counts, and dispatches through the ordinary encoder on a scratch command buffer, so planner, validation and submit path are
  exactly those of `dispatchThreadgroups:`. Test: the grid is produced by a blit job of the *same command* after the encode
  and the submit sees it; zero in any dimension dispatches nothing; > 65536 fails the command; bad offset/nil/zero local/no
  pipeline/foreign buffer refuse at encode. IOSurface-shadowed textures in an indirect dispatch are refused.
  Limitation: the spy asserts the submitted wire (`method(7)`), it does not execute a shader.
* **Parallel render encoder** (`api_parallel.inc`). The real pass encoder is created with the descriptor; sub-encoders are
  recorders (plain records: opcode, retained objects, scalar arguments, copied byte data; no block literals, so host harnesses need no Blocks runtime). `endEncoding` replays every sub-encoder in creation order and
  restores pass defaults for any viewport/scissor/cull/winding/fill/depth state a sub-encoder changed. Tests: two threads
  record concurrently and end out of order; the replayed draw requests equal an ordinary encoder's (vertex count, scissor,
  viewport, bindings) and the second sub-encoder does not inherit the first one's viewport; un-ended sub-encoder, use after end,
  creation after end, multi-viewport and byte copies at record time are checked; block copy/release balanced.
  Refusals of the real encoder surface at replay (e.g. cull mode other than none, see below).
* **Binary archive.** `add*` validate the descriptor, `serializeToURL:` writes a header, loading validates it; no binaries
  are stored, so `MTLPipelineOptionFailOnBinaryArchiveMiss` with descriptor archives fails (a miss is the truth).

* **Resource options.** `tegu_buffer_options_ok`: cache {default,WC} x storage {shared,private} x hazard {default,untracked,tracked}; managed (macOS
  only), memoryless buffers, cache 2/3, hazard 3 and bits >= 0x400 stay nil. Textures: cache/hazard bits are validated then stripped before the
  allocator (`newTextureWithDescriptor:` and `newTextureWithDescriptor:iosurface:plane:` head hunks), the app's options are stored and returned (`-resourceOptions/-cpuCacheMode/-hazardTrackingMode`).
  Heaps: resources must match the heap's storage+cache, hazard may differ. Test: option matrix (11 valid combinations x buffer/texture/render target,
  blit into private buffer, draw sampling and rendering for every combination, 10 invalid values), heap matrix.
* **Buffer-backed textures.** `tegu_buffer_make_texture` (api_surface.inc) builds a TeguTexture without allocation over the app's buffer.
* **Events under asynchronous kernel submission.** Test `api_test_async_events` runs with the fake kernel's WAIT_SEQ model (async on): a foreign
  shared event whose `setSignaledValue:` asserts that no kernel job is pending proves the signal ran after the earlier jobs were reaped.
* **Randomized API misuse** (`api_test_fuzz`, `API_FUZZ=n`): n random command buffers mixing events, compute/blit/render/parallel encoders, indirect draws and
  dispatches with bad offsets, unended encoders and invalid arguments; ASan+UBSan clean at 4000, live-object balance 0.

* **No-copy buffers.** Test `api_test_nocopy`: caller writes after creation are seen by blits/compute dispatches/textures-over-the-buffer at execution,
  blit writes return to the caller's memory in exactly the written range (an app write at a nearby offset between encode and commit survives), the
  deallocator runs once after the last command user, refused creations never call it, block copy/release balanced.

## Added after STABLE #5 (linked functions, tessellation, argument buffers, residency)

* **Compute descriptor inputs** (`compute_descriptor.inc`, wired in `runtime_pipeline.inc` through `teguNewComputePipelineWithFunction:descriptor:error:`).
  `linkedFunctions.{functions,privateFunctions,binaryFunctions}` (each TeguFunction by its own library handle, its `functionName`, and
  `link_name` = specializedName when it differs), `stageInputDescriptor` (attributes, layouts, indexType, indexBufferIndex; present descriptor =>
  `has_stage_input=1`) and `maxCallStackDepth > 1` are marshalled into `TeguAIRComputeCreateOptions` v1 and created with the weak
  `tegu_air_pipeline_create_ex`. A plain descriptor still takes `tegu_air_pipeline_create` unchanged. Refused with a reason (nil + error, never dropped):
  linked groups, insert/preloaded dynamic libraries, foreign / other-device / non-visible functions, out-of-range stage input, stale native build without
  create_ex. Test `api_test_compute_descriptor` (every field, every refusal) and the real bridge accepts depth 3, an empty stage input and a self-link.
  **Not done:** visible function tables / `functionHandleWithFunction:` (waiting for the handle export; refused with a trace), MPS `specializedName` aliasing
  is a translator change (shader-coverage).
* **Tessellation** (`tessellator.h`, `api_tess.inc`, `tests/tessellator_test.py`). CPU tessellator for triangle and quad domains; pow2 / integer /
  fractional_odd / fractional_even; half-precision factor structs; culling; winding. Independent oracle (segment counts, fractional placement rebuilt by list
  insertion, closed-manifold and area invariants, edge points equal to the oracle's so neighbouring patches match, continuity at band boundaries, quad
  interior lattice == {(i/n,j/n)}); mutation-checked. Encoder: `setTessellationFactorBuffer/Scale`, `drawPatches`/`drawIndexedPatches` (direct and
  CPU-written indirect with execution-time verification), step functions, instance stride, patch index buffer, control point index buffer (uint16/uint32),
  stream record per vertex = shader-coverage's contract (patch, instance+base, control-point element indices in uvec4 groups, position_in_patch),
  drawn as ONE indexed triangle list with the stream at vertex slot 30 (slot restored). **Not working end to end:** no pipeline carries a
  `TeguTessInfo` yet (`tegu_pipeline_tess_info` returns NULL, so every draw fails the command with `TEGU_FRONTEND UNSUPPORTED tessellation_pipeline`);
  the render pipeline route (`tegu_air_render_pipeline_create_tessellated`, `MTLFunction.patchType/patchControlPointCount`, cache variant) is unwired and
  asked on the board. Heuristics that only a device can confirm: edge numbering, born-at-centre fractional placement, inside<=0 culling, clockwise convention.
* **Argument buffers** (`api_argbuf.inc`, `n2mb_buffer_va` in the bridge). `MTLBuffer.gpuAddress` is the BO's GPU VA (cached). `newArgumentEncoderWithArguments:`
  builds a real encoder: members by ascending index at natural MSL alignment, arrays, `constantBlockAlignment`; `setArgumentBuffer:offset:` /
  `startOffset:arrayElement:`, `setBuffer(s)` (writes VA+offset, nil writes 0), `constantDataAtIndex:`. Texture/sampler/pipeline/ICB/function-table members and
  function-derived encoders are traced refusals. Test `api_test_argument_buffers` (hand-computed layouts). **Not claimed to work on a device:** the kernel's
  cache maintenance is driven by the submit handle list, which does not name pointee BOs; the compiler's treatment of `air.indirect_buffer` is open (board).
* **Residency retention.** `useResource(s)` / `useHeap(s)` (compute and render, including the single-resource render forms that were silent no-ops in
  `ws_selector_compat.inc`) retain the object for the command's life (same list as the indirect-draw verifications, `words==0`). Test: the app's last release
  does not free a used buffer before the command.
* **Refusals instead of unrecognized-selector crashes** (`api_encoder_refusals.inc`, generated by `gen_refusals.py` from the SDK headers): render 50, compute 7,
  blit 7 encoder selectors; device mesh/sparse/ray-tracing entry points; pipeline-state / texture / sampler members of those families (`gpuResourceID` is 0).
* Inventory scanner now understands the `TEGU_API_QUERY` getter macro (the ledger previously listed 19 implemented capability getters as missing).

## Gate record (STABLE #7, 05:00, host only)

`tools/run_host_tests.sh` rc=0 including the strict iOS bundle link; `api_surface_frontend_test` ASan+UBSan PASS (600-iteration misuse fuzz, live balance 0);
`tessellator_test` PASS; `texture_format_frontend_test` PASS; `metal_resource_integration_test.py --candidate` (from `work/`): only the known CoreAnimation render
pass stride oracle fails; iOS arm64 freestanding `-Wall -Wextra -Werror` compile of `compute_frontend.m` clean (`ios_syntax.sh`). Hashes: `gates/SHA256SUMS.short`.

## Still open after #7 (owner)

* Visible function tables / `functionHandleWithFunction:` / intersection tables: need `tegu_air_pipeline_function_handle` (shader-coverage, stopped at freeze).
* Tessellation pipeline route: `tegu_air_render_pipeline_create_tessellated` is declared in `air_bridge.h` only after shader-coverage's freeze set; the frontend needs
  `MTLFunction.patchType/patchControlPointCount` (an export that does not exist) and the render_pipeline.inc / pipeline_cache variant wiring (render lead).
* Argument buffers on a device: kernel handle-list/cache contract for pointee BOs, compiler model for `air.indirect_buffer` (real VA loads vs flattened members),
  texture/sampler members, function-derived encoders. Lands after C6m.
* Silent state setters in `ws_selector_compat.inc` (`setBlendColor*`, `setDepthBias`, `setStencil*Reference`, `setDepthClipMode`, `setVisibilityResultMode`)
  remain no-ops; safe only while the pipelines/passes that would observe them are refused (render lead).
* GPU-written indirect draw arguments, cross-process shared events/textures, compute write-back for no-copy buffers, counters / rasterization-rate maps /
  dynamic libraries / stitching (all traced refusals).

## Silent no-ops audited (all are safe by construction, none changed)

`setBlendColor*` (dynamic blend constants are refused at pipeline creation: `blend_state.h`), `setDepthBias`,
`setStencilReferenceValue`, `setDepthClipMode`, `setVisibilityResultMode` (a pass with a visibility buffer is refused,
`render_encoder.inc:4505`; depth tests/writes and stencil are refused at draw), `synchronizeResource`/`didModifyRange`
(shared storage), `useResource` in the old compat file (all resources resident), `setStencil*ReferenceValue`. They cannot
change pixels because every pipeline/pass/draw that would make them observable is refused with a trace first.

## Gaps owned by other agents (not implemented here)

Found while reading, each with a minimal reproduction for its owner (all are *defined failures*, not crashes):

| gap | repro | owner |
|---|---|---|
| back-face culling (`setCullMode:` != None) and `setTriangleFillMode:` != Fill poison the command (`render_encoder.inc:1839-1849`). No cull bits exist in `generic_job_ui.h`/n2r | encode any draw after `setCullMode:MTLCullModeBack` | render lead (needs request field + n2r/tiler bits) |
| depth/stencil testing and writing (render pass depth attachment is DontCare-only; `render_pipeline.inc:665`) | any 3D scene with a depth buffer | render lead + lowering |
| indirect draws (`drawPrimitives:indirectBuffer:` -> `TEGU_INVALID` on the JM transport, `render_encoder.inc:3806`) | `drawPrimitives:indirectBuffer:indirectBufferOffset:` | render lead (deferred draw parameters through the planner) |
| tessellation pipeline route (frontend runtime done, see above), mesh/object shaders, tile shading, vertex amplification, programmable sample positions | feature-gated by family | lowering + shader-coverage |
| argument buffers: texture/sampler members, function-derived encoders, compiler pointer loads, kernel handle-list residency; ICB execution (`newIndirectCommandBufferWithDescriptor:` traced refusal) | `-newArgumentEncoderWithBufferIndex:` | lowering + shader-coverage (needs GPU-visible resource ids in the AIR->NIR path) |
| function stitching (`newLibraryWithStitchedDescriptor:` traced refusal) and CoreImage-kind containers | CoreImage kernel graphs | shader-coverage (AIR->NIR) |
| `parentRelativeLevel/Slice` (no ivar records a view's origin) | sub-range view then `-parentRelativeLevel` | texture agent |
| `newBufferWithBytesNoCopy:` stays nil (an honest `TEGU_SELECTOR_REFUSED`): the kernel cannot import arbitrary CPU pages and a copy-in/copy-out shadow cannot be made race-free for apps that keep writing other slots of the same memory | any no-copy client | kernel agent (BO import of user pages) |
| sampler: `lodAverage`, `supportArgumentBuffers`, compare/aniso restrictions in the legacy factory (`sampler_state.inc`) | exact factory enabled only with the v2 compiler | texture agent |
| 5 required integration checks fail in `metal_resource_integration_test.py --candidate` (CoreAnimation render pass stride oracle; candidate provenance receipt mismatch; render specialized variant cache; ...) | see `integration-run*.log` | owners named in the report |

## Not verifiable on host (need a device artifact; do not claim)

* Only `argumentBuffersSupport` is still answered by the `_MTLDevice` base (Metal has no "none" tier); every other capability getter is explicit.
* The MSL-source route (`[super newLibraryWithSource:]` through MTLCompilerService, then `serializeToURL:error:` of Apple's `_MTLLibrary`): host-tested
  against a fake base only. Needs the compiler service reachable from the client's sandbox on the 16.5 userland.
* libdispatch scheduling of shared-event listeners and the block runtime hooks (host tests install fakes).
* Real kernel behaviour: events/parallel/indirect/no-copy/buffer-texture tests run against the fake JM kernel (jobs are counted and their wires decoded,
  nothing renders). Pixels, GPU cache coherence for CPU-written no-copy memory, and the cost of the per-command copy-in are device questions.
* Acceptance of cache/hazard option bits by CoreAnimation and UIKit clients (they previously saw nil); the `TEGU_UNRECOGNIZED` log of the next run is the
  evidence collector for everything else.

## Files

New: `gpu/metal/tegu/api_events.inc`, `api_surface.inc`, `api_parallel.inc`, `api_tess.inc`, `tessellator.h`, `api_argbuf.inc`, `compute_descriptor.inc`, `api_encoder_refusals.inc`, `api_surface_host_test.inc`, `gpu/tests/tessellator_test.py`,
`gpu/tests/api_surface_frontend_test.py`, this directory. Shared-file edits: `compute_frontend.m` (3 Dispatch fields, one
`encodeSignalEvent:` body, 6 execute-loop lines, one dealloc release, three includes), `blit_encoder.inc` (fence bodies).

## Reproduce

```
python3 gpu/tests/api_surface_frontend_test.py            # ASan+UBSan, ~3 min; API_FAST=1 for -O0 iteration, API_FUZZ=n for the misuse fuzz length
gpu/build/api-surface-20261027/refresh_ledger.sh          # regenerate inventory.json and the appendix from the SDK headers
clang --target=arm64-apple-ios16.0 -ffreestanding -Wall -Wextra -Werror ... -c gpu/metal/tegu/compute_frontend.m   # device flags of tools/build_tegu_frontend_objects.sh
```

Field inventory on the next device run: grep the process logs for `TEGU_UNRECOGNIZED class=... sel=...` (every selector a client sends that no
Tegu class implements, repeats suppressed, cap 256; `respondsToSelector:` probes appear too) and `TEGU_FRONTEND UNSUPPORTED <name>` /
`TEGU_SELECTOR_REFUSED` (refused entry points).

Policy note: GPU families Common1..3 (3001-3003) stay NO by design (`framework_device_probe.inc` asserts it; only Apple1/Apple2 are claimed).
## Appendix: per-protocol coverage (iPhoneOS 16.5 SDK headers vs frontend; generated by gen_table.py)

| protocol | Tegu class | IMPL | REFUSE | MISSING | total |
|---|---|---|---|---|---|
| MTLAccelerationStructure | TeguAccelerationStructure | 0 | 0 | 2 | 2 |
| MTLAccelerationStructureCommandEncoder | TeguAccelerationStructureEncoder | 0 | 0 | 14 | 14 |
| MTLArgumentEncoder | TeguArgumentEncoder | 5 | 21 | 0 | 26 |
| MTLBinaryArchive | TeguBinaryArchive | 7 | 1 | 0 | 8 |
| MTLBlitCommandEncoder | TeguBlitEncoder | 16 | 7 | 0 | 23 |
| MTLBuffer | TeguBuffer | 6 | 0 | 0 | 6 |
| MTLCaptureScope | TeguCaptureScope | 0 | 0 | 6 | 6 |
| MTLCommandBuffer | TeguCommand | 32 | 5 | 0 | 37 |
| MTLCommandEncoder | TeguEncoder, TeguRenderEncoder, TeguBlitEncoder | 7 | 0 | 0 | 7 |
| MTLCommandQueue | TeguQueue | 7 | 0 | 0 | 7 |
| MTLComputeCommandEncoder | TeguEncoder | 22 | 13 | 0 | 35 |
| MTLComputePipelineState | TeguPipeline | 6 | 6 | 0 | 12 |
| MTLCounter | TeguCounter | 0 | 0 | 1 | 1 |
| MTLCounterSet | TeguCounterSet | 0 | 0 | 2 | 2 |
| MTLCounterSampleBuffer | TeguCounterSampleBuffer | 0 | 0 | 4 | 4 |
| MTLDepthStencilState | TeguDepthStencilState | 2 | 0 | 0 | 2 |
| MTLDevice | TeguComputeDevice | 71 | 28 | 1 | 100 |
| MTLDrawable | - | 0 | 0 | 6 | 6 |
| MTLDynamicLibrary | TeguDynamicLibrary | 0 | 0 | 5 | 5 |
| MTLEvent | TeguEvent | 3 | 0 | 0 | 3 |
| MTLSharedEvent | TeguSharedEvent | 4 | 0 | 0 | 4 |
| MTLFence | TeguFence | 3 | 0 | 0 | 3 |
| MTLFunctionHandle | TeguFunctionHandle | 0 | 0 | 3 | 3 |
| MTLLogContainer | - | 0 | 0 | 4 | 4 |
| MTLFunctionLog | - | 0 | 0 | 4 | 4 |
| MTLHeap | TeguHeap | 17 | 0 | 4 | 21 |
| MTLIOCommandBuffer | TeguIOCommandBuffer | 0 | 0 | 18 | 18 |
| MTLIOCommandQueue | TeguIOCommandQueue | 0 | 0 | 5 | 5 |
| MTLIOFileHandle | TeguIOFileHandle | 0 | 0 | 2 | 2 |
| MTLIndirectCommandBuffer | TeguIndirectCommandBuffer | 0 | 0 | 5 | 5 |
| MTLIndirectRenderCommand | TeguIndirectRenderCommand | 0 | 0 | 8 | 8 |
| MTLIndirectComputeCommand | TeguIndirectComputeCommand | 0 | 0 | 10 | 10 |
| MTLIntersectionFunctionTable | TeguIntersectionFunctionTable | 0 | 0 | 9 | 9 |
| MTLFunction | TeguFunction | 11 | 2 | 0 | 13 |
| MTLLibrary | TeguLibrary | 11 | 0 | 2 | 13 |
| MTLParallelRenderCommandEncoder | TeguParallelRenderEncoder | 7 | 0 | 0 | 7 |
| MTLRasterizationRateMap | TeguRasterizationRateMap | 0 | 0 | 10 | 10 |
| MTLRenderCommandEncoder | TeguRenderEncoder | 65 | 59 | 0 | 124 |
| MTLRenderPipelineState | TeguRenderPipeline | 10 | 7 | 0 | 17 |
| MTLResource | TeguBuffer, TeguTexture | 13 | 0 | 0 | 13 |
| MTLResourceStateCommandEncoder | TeguResourceStateEncoder | 0 | 0 | 6 | 6 |
| MTLSamplerState | TeguSampler | 2 | 1 | 0 | 3 |
| MTLTexture | TeguTexture | 30 | 5 | 0 | 35 |
| MTLVisibleFunctionTable | TeguVisibleFunctionTable | 0 | 0 | 3 | 3 |
| **total** | | **357** | **155** | **134** | **646** |

Missing selectors (a call is an unrecognized-selector process death; all belong to features the device does not advertise, to base-class capability getters, or to owners named above):

* **MTLAccelerationStructure**: size gpuResourceID
* **MTLAccelerationStructureCommandEncoder**: buildAccelerationStructure:descriptor:scratchBuffer:scratchBufferOffset: refitAccelerationStructure:descriptor:destination:scratchBuffer:scratchBufferOffset: refitAccelerationStructure:descriptor:destination:scratchBuffer:scratchBufferOffset:options: copyAccelerationStructure:toAccelerationStructure: writeCompactedAccelerationStructureSize:toBuffer:offset: writeCompactedAccelerationStructureSize:toBuffer:offset:sizeDataType: copyAndCompactAccelerationStructure:toAccelerationStructure: updateFence: waitForFence: useResource:usage: useResources:count:usage: useHeap: useHeaps:count: sampleCountersInBuffer:atSampleIndex:withBarrier:
* **MTLCaptureScope**: beginScope endScope label setLabel: device commandQueue
* **MTLCounter**: name
* **MTLCounterSet**: name counters
* **MTLCounterSampleBuffer**: device label sampleCount resolveCounterRange:
* **MTLDevice**: argumentBuffersSupport
* **MTLDrawable**: present presentAtTime: presentAfterMinimumDuration: addPresentedHandler: presentedTime drawableID
* **MTLDynamicLibrary**: label setLabel: device installName serializeToURL:error:
* **MTLFunctionHandle**: functionType name device
* **MTLLogContainer**: functionName URL line column
* **MTLFunctionLog**: type encoderLabel function debugLocation
* **MTLHeap**: newAccelerationStructureWithSize: newAccelerationStructureWithDescriptor: newAccelerationStructureWithSize:offset: newAccelerationStructureWithDescriptor:offset:
* **MTLIOCommandBuffer**: addCompletedHandler: loadBytes:size:sourceHandle:sourceHandleOffset: loadBuffer:offset:size:sourceHandle:sourceHandleOffset: loadTexture:slice:level:size:sourceBytesPerRow:sourceBytesPerImage:destinationOrigin:sourceHandle:sourceHandleOffset: copyStatusToBuffer:offset: commit waitUntilCompleted tryCancel addBarrier pushDebugGroup: popDebugGroup enqueue waitForEvent:value: signalEvent:value: label setLabel: status error
* **MTLIOCommandQueue**: enqueueBarrier commandBuffer commandBufferWithUnretainedReferences label setLabel:
* **MTLIOFileHandle**: label setLabel:
* **MTLIndirectCommandBuffer**: size gpuResourceID resetWithRange: indirectRenderCommandAtIndex: indirectComputeCommandAtIndex:
* **MTLIndirectRenderCommand**: setRenderPipelineState: setVertexBuffer:offset:atIndex: setFragmentBuffer:offset:atIndex: drawPatches:patchStart:patchCount:patchIndexBuffer:patchIndexBufferOffset:instanceCount:baseInstance:tessellationFactorBuffer:tessellationFactorBufferOffset:tessellationFactorBufferInstanceStride: drawIndexedPatches:patchStart:patchCount:patchIndexBuffer:patchIndexBufferOffset:controlPointIndexBuffer:controlPointIndexBufferOffset:instanceCount:baseInstance:tessellationFactorBuffer:tessellationFactorBufferOffset:tessellationFactorBufferInstanceStride: drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance: drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance: reset
* **MTLIndirectComputeCommand**: setComputePipelineState: setKernelBuffer:offset:atIndex: concurrentDispatchThreadgroups:threadsPerThreadgroup: concurrentDispatchThreads:threadsPerThreadgroup: setBarrier clearBarrier setImageblockWidth:height: reset setThreadgroupMemoryLength:atIndex: setStageInRegion:
* **MTLIntersectionFunctionTable**: setBuffer:offset:atIndex: setBuffers:offsets:withRange: gpuResourceID setFunction:atIndex: setFunctions:withRange: setOpaqueTriangleIntersectionFunctionWithSignature:atIndex: setOpaqueTriangleIntersectionFunctionWithSignature:withRange: setVisibleFunctionTable:atBufferIndex: setVisibleFunctionTables:withBufferRange:
* **MTLLibrary**: newIntersectionFunctionWithDescriptor:completionHandler: newIntersectionFunctionWithDescriptor:error:
* **MTLRasterizationRateMap**: device label screenSize physicalGranularity layerCount parameterBufferSizeAndAlign copyParameterDataToBuffer:offset: physicalSizeForLayer: mapScreenToPhysicalCoordinates:forLayer: mapPhysicalToScreenCoordinates:forLayer:
* **MTLResourceStateCommandEncoder**: updateTextureMappings:mode:regions:mipLevels:slices:numRegions: updateTextureMapping:mode:region:mipLevel:slice: updateTextureMapping:mode:indirectBuffer:indirectBufferOffset: updateFence: waitForFence: moveTextureMappingsFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:destinationSlice:destinationLevel:destinationOrigin:
* **MTLVisibleFunctionTable**: gpuResourceID setFunction:atIndex: setFunctions:withRange:
