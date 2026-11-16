# Frontend / shader execution audit — 25 October 2026

This is an independent PC audit, not a UI qualification or an assertion that
all installed shaders are supported. No phone operation, public upload, native
compiler rebuild, or existing source edit was performed. The user requires the
whole rootfs shader inventory and texture-source matrix before another flash.
The corpus and texture-lowering agents own those complementary results.

Source baseline: `009f1cb04fe419c87b8bcfa7c2a661e594444e52`. Line references
below identify that audit snapshot; concurrent compiler edits can move them.
Reference comparison: `../references/ios-tegu/tools/tegu/metal/` contains the
same broad family claim and most resource/compute restrictions. Reuse of that
frontend therefore does not itself establish execution support on Nord2.

## PC evidence

Private, ignored evidence: `gpu/build/frontend-audit-20261025/result.json`,
authored GLSL/SPIR-V, host wrapper and per-case logs. The wrapper calls the real
`tegu_mali_compile_generic_pair` in isolated processes. All 14 inputs passed
`glslangValidator -V` and `spirv-val --target-env vulkan1.0`.

Baseline library: `gpu/build/ui-corpus-20261025/baseline/libnord2_mali_runtime.so`,
SHA-256 `3a7b691b57b3951746cf1ecab85fd4925690c98b9901d200208e5fadbc520278`.

| Authored case | Actual host result |
| --- | --- |
| Smooth float control; `gl_FragCoord`; `gl_FrontFacing`; `gl_PointCoord`; `gl_SampleID`; derivatives | Compile succeeds; no GPU/pixel claim |
| Vertex-stage `textureLod` | Compile succeeds, one resource; frontend binding is missing below |
| Flat float; noperspective | rc 5, `Only smooth perspective varyings are supported` |
| Flat integer | rc 5, `Varying must be a 32bit float scalar or vector` |
| Sampler descriptor array | rc 5, `Generic texture or sampler arrays are unsupported` |
| Read-only storage image | rc 5, `Generic storage images are unsupported` |
| Writable graphics SSBO | rc 5, `Generic storage buffers must be read-only` |
| Fragment depth output | rc 4, `Unsupported graphics output` |

The residual-builtin guard is **not** evidence that all fragment builtins fail:
`nir_lower_system_values` already lowers the successful examples above.
These are authored feature probes, not proof that a particular rootfs entry
uses each feature. Rootfs occurrence counts belong to the corpus report.

## Declared capability versus executable path

`device_render_query_caps.h:16-25` claims Apple1/Apple2 support whenever the
graphics backend is built. `device_limits.h:102-132` instead publishes zero
vertex textures/samplers, compute textures/samplers, threadgroup memory and
many other fields; nonzero 1D/3D sizes describe allocation minima.
`compute_frontend.m:770-771` reports threadgroup memory zero. The family result
is a compatibility assertion, not evidence that the full family contract works.
Do not strengthen or weaken it merely to get a passing corpus count.

`gpu/tools/build_tegu_frontend_objects.sh:12-22` uses an augmented NVIDIA
RTX5080 translator profile. The captured profile enables, among others,
descriptor indexing, storage images, integer/float widths, sparse resources,
anisotropy, and stores/atomics beyond the native ABI. Its own comment correctly
calls this a translation ceiling. AIR success under it cannot be counted as
Mali/binding/dispatch success. Its ASTC-LDR feature is false, so enabling ASTC
planner inputs also requires checking the translator's actual format plan.

Function inventory maps vertex/fragment/kernel/visible/intersection/mesh/object
to public types (`air_bridge.rs:2255-2257`); `library_frontend.inc:203-226`
returns every known nonzero kind. Execution only implements vertex+fragment
render pipelines and kernel compute (`runtime_pipeline.inc:32`). Count the
other kinds separately rather than treating enumeration as compilation.

## Demonstrable gaps and required tests

| Gap | Evidence / consequence | Concrete completion test and fix |
| --- | --- | --- |
| Compute sampled textures and storage images | `compute_frontend.m:507,1591,1652-1655` retains textures but passes only buffers to runtime. No compute sampler state/selectors. `air_bridge.rs:2323-2333,2383-2389` only plans buffer descriptors. | AIR kernel sampling a real texture into SSBO, then kernel writing a texture read by a later pass. Implement the complete compute resource path below. A compiler refusal replaces a crash but is not support. |
| Exact-thread dispatch | `compute_frontend.m:1618-1621` always invalidates `dispatchThreads`. | 17×9 threads with 8×4 local size and guard pixels/atomic counts proving no padded invocation. Carry the translator's exact dispatch regions/edge strategy; do not round the grid silently. |
| Storage-image creation placeholders | `air_bridge.rs:73-114` guesses one of three formats, at most four slots; `2320-2321` refuses every dispatch of that creation-only pipeline. | Bind different supported integer/float storage formats after PSO creation, create the correct format variant, then verify data and variant-cache separation. |
| Vertex texture/sampler bind | Compiler succeeds in authored test. `render_encoder.inc:1840-1842` supplies only fragment textures/samplers and `1906,1935` consumes only stage 2. | VS texture lookup changes position/varying while FS uses a different texture at the same Metal slot. Add both stages to typed planning, snapshot and ownership. |
| Texture dimensions/subresources | Allocation has exact 1D/3D/MSAA exceptions (`render_encoder.inc:336-369`), while generic snapshots require kind 2 (`1755-1770`). Wire binding has only width/height/rows (`generic_job_ui.h:62-66`); `nord2_render_resources.inc:49-76` emits 2D, one plane/level. | Test each 1D/1D-array/2D/2D-array/3D/cube/cube-array/MS/buffer dimension with distinguishable slices/levels. Extend allocation, typed facts, view/subresource span, descriptor layout and shader lowering together. Compiler success alone is insufficient. |
| Mips and LOD | `render_encoder.inc:1451` can report several mips, including exact captured BGRA blur chain; `1914-1915` binds only level zero. Sampler word has no mip or LOD field (`generic_job_ui.h:42-45`). | Distinct mip colors; explicit LOD 0/1/fractional, bias, gradients, query levels/size. Retain complete mip table, spans and sampler LOD policy. Correct tex-source lowering does not supply missing mip storage. |
| ASTC planner disconnect | Allocation and late texture validation accept ASTC (`render_encoder.inc:402,1757`), but resource collection excludes it (`1435`) and Rust typed input excludes it (`air_bridge.rs:1316`). Missing sampled input can become a neutral descriptor; therefore late ASTC packing can be unreachable without an explicit error. | Real ASTC bind → typed plan → non-neutral compiled binding → hardware descriptor. Add format facts/profile support and assert resource identity; distinguish linear/sRGB/HDR formats actually implemented. |
| Runtime and immutable sampler semantics | Runtime sampler only nearest/linear, normalized, clamp, no compare/aniso/mips (`sampler_state.inc:52-59`). Planner facts are fabricated nearest/LOD0 (`render_encoder.inc:1459-1465`), even when hardware receives linear bits at `1937`. Immutable metadata contains mip mode/border color (`air_bridge.rs:1723-1725`), but encoder keeps only min/mag/wrap (`render_encoder.inc:1720-1725`). | Sample outside bounds with repeat/mirror/border-white; compare/depth; unnormalized coordinates; mip/aniso cases. Carry exact descriptor state through planning and packing. Never discard unsupported immutable fields silently. |
| Writable/array resources | Actual host refusals above; generic compiler set0 binding index <16 (`mali_graphics_compiler.c:815`), arrays and storage images rejected in `lower_generic_resources`; planner requires element0 (`air_bridge.rs:1698`). | Two-element descriptor array with dynamic index; read/write/atomic storage image and buffer; overlapping-resource visibility. Add resource/use classes, descriptor indexing and write lifetime/cache handling, not just accepted declarations. |
| Runtime variants/specialization | Generic plan refuses any translator variant or specialization differing from its default (`air_bridge.rs:1670-1679`). | Same shader with texture facts or function constants producing distinct SPIR-V/plans; cache both identities and verify output. Do not label a default-only compile as all variants. |
| Varying classes | Actual flat, noperspective, integer failures. `mali_graphics_compiler.c:113-130` also refuses centroid/sample/low or medium precision. | Interpolation gradients and provoking-vertex tests; implement relevant varying descriptors/IDVS layout and half/integer paths. Preserve strict interface matching. |
| Vertex input and draw identity | AIR fetch accepts only five Vulkan formats and divisor≤1 (`air_bridge.rs:158-170`). Generic fallback only one per-vertex layout ≤256 bytes (`render_pipeline.inc:1449-1450`); snapshot requires one instance/base0 (`render_encoder.inc:1809`). Attribute path rebases buffer to `vertexStart` but intentionally sets shader IDs to zero (`1822-1823`, plan at `1844`). | Multiple vertex buffers, packed/normalized/integer attributes, instancing/base instance; nonzero `vertexStart` with both an attribute and `vertex_id`. Preserve original shader IDs as well as fetch offsets. Indexed attribute expansion also needs original-ID tests. |
| Depth, MSAA, target topology | Native descriptor validation rejects depth/stencil and sample count≠1 (`render_pipeline.inc:665-667`); fragment depth host test refuses. Rust generic MRT is only BGRA8+BGRA8 (`air_bridge.rs:1635-1640`), narrower than initial same-format fallback. | Depth-writing shader, stencil clip, multisample fetch/resolve, two different target formats and each legal MRT format. Implement complete render-pass/target semantics before expanding checks. |
| Resource/code footprint | Compute v1 caps eight single-page bindings, 4 KiB code, no TLS/WLS (`pipeline_format.h:11,28-47`; `air_bridge.rs:2395`). Graphics has 128-byte pushes/stage, sixteen bindings, ≤64-byte varyings and rejects TLS (`generic_job_ui.h:32-35`; `mali_graphics_compiler.c:675`; `nord2_render_resources.inc:98`). | Boundary and one-over fixtures plus large real entries. Allocate/retain actual scratch/shared/code/resource regions if needed; do not inflate metadata limits without transport backing. |

## Compute implementation map

1. Add a **versioned** compute resource/planning ABI in `air_bridge.h/.rs`.
   Inputs need buffers, texture facts (dimension, format, usage, views, levels,
   layers and samples), exact samplers, and threadgroup/dispatch shape.
   Outputs must preserve descriptor source→Metal slot mapping, immutable and
   neutral descriptors, specialization/format variant key, push/constant data,
   and read/write use ranges. The current `TeguAIRBufferBinding` and
   `TeguAIRNativeDispatch` cannot express these (`air_bridge.h:168-200`).
2. Extend `TeguEncoder` and `tegu_runtime_dispatch` to pass retained texture and
   sampler snapshots, with ownership checks and whole-range validation.
   Sampler setters alone do not implement binding. Retain resources until
   completion and preserve existing command-buffer error behavior.
3. Compile format-dependent AIR variants using the real bound resources.
   `runtime_pipeline.inc:105-108` currently keys only specialization constants;
   include the selected translator variant/shader identity and storage formats.
   Extend the compute compiler's reflected resource contract and handle
   lowering consistently with the new plan; validate every resource before
   Mesa's final codegen. The texture-lowering agent owns compiler changes.
4. Add versioned package/dispatch descriptors next to TGP1, preserving v1.
   `nord2_metal_bridge.c:171-211` currently allocates a 640-byte
   job/shader/local-storage/FAU block, writes eight bounded-buffer descriptors
   in FAU and leaves `chain.resources=0`. Build validated texture/sampler/image
   tables, all subresource spans, and fill `chain.resources`. JM already has
   this field (`nord2_gpu_jm.h:31`, validator in `nord2_gpu_jm.c:18-33`). No
   arbitrary pointers should cross the retained allocation/token boundary.
5. Use actual read/write classification in submit handles and completion
   synchronization. Image outputs need visibility in later texture reads,
   render passes and CPU reads; immutable input textures must not substitute
   for writable storage images. WLS/TLS need allocation and descriptor support
   if the corpus requires them, not just relaxed package checks.
6. Host gate: AIR→SPIR-V validation→Mali→typed plan→resource packing→submit
   request with matching exact counts/uses. Include sampled2D→buffer,
   imageLoad/imageStore→texture, read/write image aliasing, dimension and
   source-kind matrices, nonuniform dispatch, variant cache eviction, release
   before completion, corrupt spans and expected deterministic errors.
   Pixel qualification remains a later, explicitly authorized device stage.

No isolated selector or validation tweak completes compute textures. A safe
small implementation unit would be a pure host-tested v2 resource ABI and
descriptor packer after its exact interface is agreed with the compiler owner;
this audit does not claim such implementation has been done.

## Corpus acceptance checklist

- Enumerate every installed library and every entry, including duplicates,
  non-render stages, parse failures, missing required constants and timeouts.
  Keep Apple bytes/names/raw diagnostics only in ignored private evidence.
- Separate counts for inventory, specialized AIR translation, SPIR-V
  validation, stage/pair compilation, native typed plan, descriptor/submit
  construction and pixels. No success in one column fills later columns.
- Pair actual VS/FS interfaces and replay actual descriptor/specialization
  classes; an arbitrary VS/FS cross product or defaults-only run is not full
  pipeline coverage. Record unresolved pairing/constant/format requirements.
- Run each risky backend input in its own bounded process; report signal,
  timeout and normal refusal separately. Containment does not qualify support.
- Reject neutral-resource substitution when the case intentionally binds a
  non-neutral resource. Test binding identity, subresource, sampler semantics
  and output writes, not only absence of assertions.
- Include every observed resource/source kind and independently authored
  boundary probes; preserve expected unsupported cases as visible remaining
  work. Another flash is not justified by the 14 tests in this audit.

## Subsequent coordinated implementation review

The table above records the baseline, not a claim that later worker edits are
already accepted. The private coordination ledger is
`gpu/build/deepseek-parallel-20261025/board/frontend_audit_003_review_ledger.md`.

- The render worker implemented stage-aware VS/FS texture and sampler lookup
  and retention keyed by native binding. Its actual frontend host harness
  reports successful same-Metal-slot/different-stage lifetime tests and an
  installed shader pair through a simulated JM commit. Hardware is untested.
- Native indexed vertex pulling preserves the original index values. An
  attribute-rebased or CPU-expanded draw can still lose VertexIndex identity.
  Valhall has a signed primitive base-vertex field; the missing part is the
  active request/bridge/snapshot contract. An additive path is being wired.
- Twelve VS-texture cases refused by vertex-fetch planning all contain native
  VkFormat83 (R16G16_SFLOAT). Their vertex layouts are inferred descriptors,
  not captured runtime layouts. Do not confuse the original Metal format
  numbers with linked Vulkan formats. Four source gates need the same4-byte
  hardware mapping: Rust planner, vertex-fetch byte size, vertex packer and
  Nord2 resource builder.
- Full compute resource support remains incomplete. The compiler owner agreed
  set0 binding N -> texture/image table1[N], sampler table2[N], with a private
  sampler at2[256] for samplerless operations. The package must serialize
  resource identity, access and shape and match the exact selected Rust plan.
  The reviewed draft does not yet satisfy that contract.
- Texture/sampler packer tests are useful descriptor evidence, but cannot
  replace review of block geometry, views, bounds and frontend reachability.
  Outstanding draft findings and required tests remain in the private ledger.

No new image or device test is justified by these intermediate implementation
results. The full corpus must be replayed against the integrated, frozen build,
with every refusal, missing input, crash, timeout and exclusion still visible.


## Independent integration checkpoint, 25 October 15:34 UTC

The compiler owner reports the frozen graphics specialization suite66/66,
existing texture suite58/58, and tile suite10/10 passed. Default and explicit
module-default specializations preserve legacy output bytes. Effective changed
scalar values specialize before resource reflection and linkage. These are
compiler-level results, not proof that frontend sampler variants are wired.

The current shared texture helper fixes were reviewed in source: compressed
block geometry, explicit swizzle validity, cube geometry, whole backing bounds,
view-relative addressing, and NULL-layout refusal. Its owner reports771 helper
checks plus sanitizer runs. Render-side richer allocation facts and a sidecar
submit hook now exist, but the active bridge symbol/planner/factory path still
needs integration. Tests of a direct helper are not application reachability.

Remaining source-visible integration defects were sent to owners with executable
checks in private board/frontend_audit_005_integration_test_brief.md:

- Compute v2 builds a job pointing to shader-program/local-storage descriptors
  without initializing their bytes. Decode the actual selector7 producer using
  the independent pinned arch9 XML oracle, including FAU count and preload.
- Rust exports a Vulkan image-view format, while the C bridge forwards it into
  the Metal-format field of the shared texture helper. Define and test the
  conversion and alias compatibility; copying the integer is incorrect.
- The bridge receives a multi-plane array/cube descriptor from a convenience
  helper but serializes only plane0. Emit all payload planes with a checked
  advancing cursor. The current base offset is duplicated into two wire fields;
  define base/relative-level fields consistently on both sides before changing
  address arithmetic. Otherwise a naive fix can double the existing offset.
- Native sampler binding capacity is still confused with Metal slot capacity;
  1D-array and ordinary-cube shape classification need correction. Resource
  identity, shape, numeric class, use/access and selected image view must match
  the exact variant manifest, whose format domains are not yet agreed.
- Compute variant cache hits return a borrowed pipeline after unlocking; the
  caller retains later. Eviction can invalidate that pointer. The miss path
  also retains an extra owner, and cached TGP1 entries can bypass a later v2
  requirement. Check owned references and package version on every path.
- Render v2 sidecar preflight examines the request binding array before the
  later validator checks its count. Enforce the fixed capacity before lookup
  and add a malformed-count sanitizer case.
- The ordinary sampler factory, runtime exact sampler specialization, immutable
  sampler enum conversion, per-job variant lifetime, full mip/array allocation
  and upload paths, and nonzero vertex identity remain separate reachability
  gates. Explicit refusal records limitations; it does not implement them.

A fifth independent worker now owns executable C/Rust ABI and full descriptor
producer tests. This reviewer changed no production source and ran no builds.
The pending integration must be tested against hashes of the final coherent
source; later owner fixes supersede the defects recorded at this checkpoint.


### Superseding source check, 25 October 15:47 UTC

The compute owner has since corrected the C region flags declaration, separate
Vulkan/Metal view-format fields, cache reference ownership, v2 code import offset,
level0 array-plane emission (bounded to16layers), shader/local-storage descriptor
initialization, and package/wire FAU count equality. The independent integration
worker must still execute these together against a fresh matching Rust library;
source inspection does not clear that gate. Earlier findings above describe the
reviewed draft and are superseded for these specific corrected lines.

Still open at this checkpoint: compiler manifest image-format/shape domains and
compiler-unavailable bound-view fields; exact declared texture span; full compute
mip-chain transport; storage atomic/readonly/writeonly use semantics; thread edge
region local sizes; render request-count preflight; runtime graphics sampler
specialization and selected-variant lifetime; ordinary exact sampler creation
reachability; and nonzero vertex identity. Render added guards to fixed/compositor
legacy sampler consumers. The shared helper NULL-layout cases are resolved.

Private board messages and source hashes distinguish all checkpoints. No native
build, device operation, public write or production edit was performed by the
independent frontend reviewer during this coordination phase.


### Arch9 resource-table pointer gate

The active compute bridge passes a plain64-aligned SRT address. The shared
compute-chain validator (gpu/src/nord2_gpu_jm.c:20) requires that alignment and
passes the pointer unchanged to shader-environment packing. Arch9 Resources
also encodes the table count in its low6bits: pinned Mesa pan_cmdstream.h
panfrost_emit_resources returns address|aligned_table_count; v9 requires count
alignment4. Existing Nord2 graphics emits SRT|4. Compute table1/table2 need four
allocated/zeroed SRT records, hence encoded count4. Plain SRT has count0.

This needs an additive count-aware chain contract or validated wrapper; merely
ORing4 into the current chain fails its address check. Root owns scope assignment
for gpu/include/nord2_gpu_jm.h and gpu/src/nord2_gpu_jm.c/mirrors. The integration
worker must decode both the base address and count from the actual complete job.
Legacy no-resource compute remains unchanged. Board1792943374099175193 has exact
source evidence and required positive/negative cases. No production fix by audit.


The compiler owner subsequently proposed the exact compiler-emittable TGP2
schema (private board1792943385674965132), and this reviewer ACKed that proposal
(board1792943480874602160). The three bound-view manifest words become reserved
zero; canonical declared Vk storage format, named shape/numeric domains,
combined-declaration provenance, sampler comparison and atomicRMW access are
explicit. This is an agreement on the proposed contract only. The matching
header/parser, runtime implementation, compiler implementation and independent
integration test results still require review; no support approval follows from
that structural ACK.

### Superseding integration review, 25 October 16:34 UTC

The count-bearing SRT pointer is now validated and encoded by an additive wrapper
in the actual compute bridge. The shared builder receives the aligned base, then
the wrapper writes base|4. The real rawJM submission path does not repeat the
builder's alignment test. This closes the source/transport SRT issue above; no
shared JM change is required for this active path. Hardware remains untested.

The final header/parser SHA dfc0a4104d746b1d911d31097a06f4818edeb86b2801488a4aa55a2ddbabfc97
implements the agreed compiler-emittable manifest. Independent tests report156
parser checks, matching160/64/1172/11496-byte C/Rust resource/descriptor/region/
dispatch layouts, and47 actual Rust planner checks. The compiler owner is now
implementing the real TGP2 producer; hand-built test packages are transport
evidence, not yet an end-to-end compiled texture dispatch.

Audit independently executed the stored release bridge test, against its recorded
52f00fec source snapshot:111 checks and three failures (sampler slot99, declared
1023-byte span authorizing1024 bytes, and swizzle0x210 replaced by0x688). Current
50e4468d bridge source fixes those cases; the old failures must not be attributed
to the later source. Fresh matching binaries/tests remain required.

Audit found that the common helper wrote v10 extent fields into arch9 plane's
reserved word7, while the integration oracle compared only seven words. The
textures owner corrected the new shared/v2 packers to zero word7, retained legacy
qualified bytes, and reports complete32-byte v9 Generic/ASTC checks. Source
inspection confirms the correction. Render oversized binding-count preflight is
also corrected and owner-tested with sanitizer cases.

Still demonstrable at this checkpoint: runtime_pipeline.inc:323 compares every
exact-thread edge region with the original local size; sampler_state's ordinary
factory is enabled too late at compute_frontend.m:1708, never before a render-only
app's first sampler creation; bridge sampled numeric/depth checks are incomplete;
storage numeric checks and combined sampler-format restrictions use allocation
format instead of the effective view. Per-region validation, early sampler
readiness, and actual view/use checks need real frontend/compiler integration
tests. Full mip transport, more than16 layers, atomics and full graphics variant
reachability remain explicit gaps. No production edit, build or device operation
was performed by this reviewer.

### Real TGP2 producer and sampler semantics follow-up

The compiler owner has implemented the additive TGP2 export. Audit reviewed its
table/binding mapping, canonical storage format, shadow/combined flags, semantic
access reflection, scalar specialization checks and serialized manifest against
headerdfc0a410. Seven saved real-producer packages (nearest/linear TestFastMath,
its four exact-thread regions, and accumulateKernel) match their recorded hashes
and exact code offsets/lengths. Their edge-region local headers independently
confirm8x4,8x1,1x4,1x1. This supersedes the earlier missing-compiler-symbol finding;
final source/binary freeze and frontend integration remain necessary.

A source-derived atomic boundary concern was reproduced by the compiler owner:
valid float32 SSBO atomicAdd reached the pinned backend's integer-only opcode
switch and aborted. The owner reports adding pinned CAS-based float32 add/min/max
lowering and integer32/64 operation/width validation, with expanded tests pending.
Runtime ATOMIC transport remains explicitly refused, so this is compiler support
and crash prevention, not completed Metal atomic execution.

New semantic gate (board1792946446749540937): the compute producer does not call
the pinned translator's bind::sampler_desc. It discards opaque_black metadata and
copies original Metal sampler facts into the runtime wire. But the selected
translated program expects per-use resolved sampler state: integer texture uses
force nearest/no anisotropy; native non-Metal nearest paths suppress hardware
anisotropy where shader taps implement it; LOD clamps and border mapping can
change. The helper currently accepts integer+linear raw fields, and its ClampToZero
branch discards an explicit opaque-white border. These differ from the selected
translator contract; no hardware result is inferred.

Resolve per-descriptor sampler facts through bind::sampler_desc and carry the
result to native packing, including opaque-black mapping, or explicitly reject
unrepresentable cases. Preserve qualified legacy behavior separately. Required
integration cases: integer+linear/anisotropy, float nearest+anisotropy, fractional
LOD bounds, ClampToZero with opaqueWhite, and Transparent/Mapped opaque-black
decisions. Compare the actual producer/wire/all32 sampler bytes, not only a helper
called with manually chosen expected values.

An additional direct example is pixel coordinates: pinned vk/structs.rs:203
requires every native SamplerDesc to use normalized coordinates, and its Vulkan
conversion explicitly disables unnormalizedCoordinates. The translated shader
already scales Metal pixel coordinates (lower/sampler/shared.rs:434). Copying the
original Metal normalizedCoordinates=false into the hardware sampler therefore
breaks that contract. Test a linear pixel sampler that actually emits sampling;
a nearest pixel path may use pure texel fetch and hide this mismatch. Keep the
original flag in shader specialization facts and normalized=true in the resolved
native sampler. Board1792946549921724454 records the source chain.
