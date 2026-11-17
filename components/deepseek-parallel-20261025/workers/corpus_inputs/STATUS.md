# corpus_inputs STATUS — 25 October 2026

Role: missing-input offline shader audit. No phone/adb/fastboot/USB/mount/rootfs,
no publish/upload/push/CI, no production ABI edits, no commits, no NOTES edits.
Frozen production libraries only (these are the historical baseline; the current
takeover sources are listed in the DELTA section):

- compiler `libnord2_mali_runtime.so` SHA256 `3a7b691b57b3951746cf1ecab85fd4925690c98b9901d200208e5fadbc520278`
- bridge `libtegu_air_bridge.so` SHA256 `857f856d8ca79c99c799868e83a29de49dcbccc6f2669623bb58f0142ab2f6c8`
- profile `tegu-air-profile.h` SHA256 `54563c6395ac70e171d452412f685c3d74aed57cbd0e748f264a0ba56e10a826`
- audit-only translator CLI `gpu/build/cargo-target/release/metal2vulkan` SHA256
  `a0c1b3c334ac0d9d8d6122a84a1f9cb6d04eb7aac531f5b96f0798f1c6665dd1`

## Claude takeover DELTA — 25 October 2026 (current)

Current tool SHA256 `406112a830fad5c9f4effb40e1d96f8629a3e2de8c8658a52e748b19f331882f`.
Current compiler is `d91bb485...` (TLS/WLS, buffer table 8..30, true MRT,
graphics TLS, varying cover); bridge `43b87524...` for the final L3c replay.
Root native C6 build is separate; audit evidence remains host-only.
Old baseline evidence below is preserved; source selection and priority features
are additive. No production file edited, no phone/native/build/commit.

DONE (light, no lease):
- `priority` subcommand builds `priority-current/priority-manifest.jsonl`
  (3148 rows, SHA256 `b40d93a4...` at tool 6a6), tier subsets
  `priority-p0..p3.jsonl`, and `priority-summary.json`. P0 is the OBSERVED
  join to ledger-claude-20261025/springboard_critical.json (180 items): 6
  remaining rows = D8F18H, siriChipVert+Frag, 4 QuartzCore; 175 observed
  entries outside these FC batches. MPSImage siblings are S2 => P1, not P0.
  Tiers: P0 6, P1 267, P2 2574, P3 301.
  Exact remaining: 273 graphics-FC + 2875 compute-FC, derived by excluding the
  previously executed 52/51 unique cases (not by name filtering).
- Tiering (authoritative join to springboard_critical.json): P0 6 (D8F18H,
  siriChipVert+Frag, 4 QuartzCore), P1 267 (S1/S2 observed context +
  role-inferred RenderBox/CoreRE/VFX/VectorKit/SceneKit/UI), P2 2574
  (Espresso/MPS compute_ml), P3 301 (video/other). The earlier name-based
  51 P0/202 P1 split and provisional 25 P0 are superseded. Origin: 273 graphics
  joint=inferred (ABI-candidate/AIR descriptor context; all constant *values*
  synthetic), 2875 compute joint=synthetic. No captured constant values remain;
  `app_coverage=false` everywhere; synthetic values never count as app behavior.
- Truthful source finding: no SpringBoard or backboardd metallib path appears in
  the remaining FC sets (0 by path). Observed role evidence supplies P0:
  QuartzCore default.metallib (backboardd renderer), MPSImage D*F*H/V kernels
  (SpringBoard compute D8F18H first), SiriUICore. RenderBox/CoreRE are P1
  role-inferred, not observed P0. 157 other observed entries are outside these
  remaining FC batches.
- Tool source selection: `--compiler/--bridge/--profile`,
  `--expected-compiler-sha256/--expected-bridge-sha256`, `--current`,
  `--priority-manifest`, `--compute-entry v1|v2`. New runs record + verify
  compiler/bridge/profile hashes per child; old baseline specs fall back to
  baseline paths. `--current` uses compiler `ea4454b7...` and the current compute
  cdylib. Compute `v2` uses `tegu_mali_compile_compute_v2`, not the buffer-only
  v1 entry.
- TOOL-DEFECT FIX (root-assigned, compute root cause): `specialize_library`
  returned the base handle when the constant list was empty; on a retry the
  worker closed that base and reused it (use-after-free SIGSEGV). It now always
  calls `tegu_air_library_specialize` (count=0 returns an owned clone) and closes
  only owned handles. Added `BridgeOwnershipTests`. Re-ran the 7 former SIGSEGV
  cases with compiler ea4454b7 + bridge 8861: no signals; 2 native compiled,
  4 native refused, 1 explicit VertexAttribute{4} refusal. Old SIGSEGV rows are
  marked tool-defect in `SIGSEGV_TOOL_DEFECT.md`, not bridge-defect.
- COMPUTE STAGE-IN: `parse_stage_inputs_ll` + `metal_stage_input_descriptor`
  materialise an explicit StageInputDescriptor (packed attributes,
  ThreadPositionInGridX layout, UInt16 defaults, origin descriptor-derived/
  synthetic). The 10 P1 cases that previously stopped at
  `StageInputDescriptor{attribute:0}` were rerun through the audit-only CLI:
  10/10 translated, 8 native compiled, 2 native refused. Evidence
  `evidence-current-ea4454b7-8861e0a0/stagein_p1`; no zero-fill-as-captured.

P0 BOUNDED REPLAY (root-authorized; lease taken and released):
- 35 graphics P0 (QuartzCore/RenderBox/CoreRE), corrected after the tool-defect
  fix: 14 native compiled, 17 native refused, 4 translation refused, 0 signals.
  Evidence `evidence-current-ea4454b7-3fa3020d/graphics_fc_p0` (raw tool-defect
  rows) plus `evidence-current-ea4454b7-8861e0a0/sigsegv_rerun` (corrected).
- 16 compute P0 CoreRE with v2 compiler: 6 native compiled, 7 native refused
  (`unsupported TGP2 native pipeline or resource manifest requirements`), 2
  translation refused (`reducePointCloudToAABB`/`performSkinning` Reflection),
  1 backend abort (`f7a70d...`). v1 comparison used only 1/16 compiled.
  Evidence `.../compute_fc_p0_v2` and `.../compute_fc_p0`.
- P0 used source snapshot compiler ea4454b7 + bridge 3fa3020d. A later rerun of
  the 7 former SIGSEGV cases with bridge 8861 had no signals; P0 raw rows are not
  reusable as a final-hash dataset.

P1 BOUNDED REPLAY (root-authorized; lease taken and released):
- 147 graphics P1 (VFX/VectorKit/SceneKit/UI), corrected after the tool-defect
  fix: 78 native compiled, 49 native refused, 20 translation refused (mostly
  `particle_*`/`pbr_fragment` Reflection; one StandardPostchain EntryNotFound),
  0 signals. Raw `worker_abort` rows in `graphics_fc_p1` are tool-defect; the
  corrected cases are in `.../8861e0a0/sigsegv_rerun`.
- 55 compute P1 with v2: 19 native compiled, 23 native refused (TGP2 native
  pipeline/resource requirements), 3 translation refused with empty
  specialization detail. The 10 earlier `StageInputDescriptor` missing-input
  cases were then re-run through the audit-only CLI with a derived
  StageInputDescriptor: 10/10 translated, 8 native compiled, 2 native refused.
  Evidence `evidence-current-ea4454b7-8861e0a0/stagein_p1` (bridge 8861).
- Compute `[[stage_in]]` support: `parse_stage_inputs_ll` +
  `metal_stage_input_descriptor` produce an explicit JSON
  `StageInputDescriptor` (packed attributes, ThreadPositionInGridX layout,
  16-bit index defaults); the CLI route is labelled audit-only, never a silent
  zero/default. This removes the 10 P1 missing-input outcomes for the next hash.
- P1 used compiler ea4454b7 + bridge snapshot 1e6cc1e2. A later light
  9-case selftest passed on bridge 8861; `graphics-00086` still SIGSEGVs.

L3b FINAL FULL REPLAY COMPLETE (lease taken/released):
- Sources: compiler `742cdf3d...` (TLS/WLS), bridge `06a6c6ee...`, priority
  manifest `b40d93a4...` (authoritative observed P0 6). Evidence:
  `evidence-current-742cdf3d-06a6c6ee/{graphics_all,compute_all}`.
- Result: 3148/3148 explicit, no row omitted.
  - Graphics 273: 116 native compiled, 75 native refused, 82 translation
    refused, 0 abort/timeout.
  - Compute 2875: 2418 native compiled, 164 native refused, 152 translation
    refused, 126 translation_missing_input (LinkedFunction symbols), 7
    translation_unsupported, 8 cpu_limit_unknown (SIGKILL at Mali after
    translation; hard CPU/AS limit, not backend refusal).
- Enriched row ledger `results-enriched.jsonl` / `summary-enriched.json` joins
  every result to the priority manifest: 3148 unique cases, manifest/result sets
  identical, 3148 synthetic constant sets, app_coverage=false. Totals:
  2534 native compiled, 239 native refused, 234 translation refused, 126
  missing input, 7 unsupported, 8 cpu-limit-unknown. By origin: 116 graphics
  inferred rows compiled and 273 inferred total; 2418 compute synthetic rows
  compiled and 2875 synthetic total. By tier: P0 1 compiled/2 refused/3
  translation refused; P1 160/82/25; P2 2152/141/144+126+3+8; P3 221/14/62+4.
- TLS/WLS compiler improvement cut compute native refusals from 1174 to 164;
  private/workgroup-memory refusals are gone. Remaining compute reasons: 91
  TGPN set0 single-descriptor range, 61 logical descriptor/physical pointer,
  9 no code, etc. Graphics: 19 varying-not-32bit, 18 too-many-generic-varyings,
  12 smooth-perspective-only, 6 `_rt` additional target, 6 binding index, 3
  writable storage buffers, 3 ClipDistance, 2 storage images; 58 synthetic MRT
  ColorAttachment refusals.
- Observed P0 (authoritative 6): D8F18H **compiled** (TGP2 TLS 32 bytes/thread,
  kUseSoftwareLinearFilter=true synthetic; false branch not captured but both
  compile per lowering). Graphics P0 still fail: siriChipVert+Frag varying class,
  4 QuartzCore fixed_* Reflection (cpf/cph), fixed_vert_lpf_spc+read_surf
  'More generic varyings than qualified policy'. All constants synthetic;
  app_coverage=false; compiled package is not runtime/pixel proof.
- Previous 57db/8861 L3 counts (1408 compiled compute) are superseded by this
  L3b dataset.

L3c DELTA6 FULL REPLAY COMPLETE (lease taken/released):
- Sources: compiler `d91bb485...`, bridge `43b87524...`, priority manifest
  `9fff0f7d...` (tool 4061, observed P0 6). Evidence:
  `evidence-current-d91bb485-43b87524/{graphics_all,compute_all_merged}`.
- 3148/3148 explicit, manifest/result sets match, all origins present in
  `results-enriched.jsonl` / `summary-enriched.json`.
- Totals: 2624 native compiled, 148 native refused, 234 translation refused,
  126 missing input, 7 unsupported, 9 cpu_limit_unknown.
  - Graphics 273: 116 compiled/75 native refused/82 translation refused
    (unchanged from L3b).
  - Compute 2875: 2508 compiled/73 native refused/152 translation refused/
    126 LinkedFunction missing/7 unsupported/9 cpu_limit_unknown (workers=2
    plus a 37-case CPU-60 retry: 28 then compiled, 9 remain unknown).
- Versus L3b: +90 compiled, -91 native refused, same translation/missing/
  unsupported, +1 cpu-limit unknown. TLS/WLS/private-memory refusals are gone;
  remaining compute native: 61 logical descriptor/physical pointer, 9 no code,
  2 descriptor arrays. Translation: 120 cnnConvArray Reflection, 3 descriptor
  refinement; graphics 58 synthetic MRT ColorAttachment + particle/fixed
  Reflection; graphics native 19 varying-not-32bit, 18 too-many-varyings,
  12 smooth-only, 6 `_rt`, 6 binding index.
- Observed P0 (6): D8F18H compiled; siriChipVert+Frag and fixed_vert_lpf_spc
  varying refusals; 4 QuartzCore fixed_* Reflection. No app/pixel claim.

PENDING: no production edits. L3c is hash-specific; a bridge/compiler change
requires delta rerun.


DONE — implementation, independent unit tests, full static input ledgers and
representative host execution. IN PROGRESS — none blocking the deliverable;
full-category compiler batches remain for a root lease.

Tool SHA256 at the time of this status:
`a97af564d78304a7881266d28bd348cfef60da02022a54590109bcc44b87c817` (re-hash
after the final freeze; evidence summaries record the exact hash they were made
with). Executed evidence: 205 unique cases / 296 variants, each in its own child
process. Full static ledger: `evidence/inputs_all/inputs-ledger.jsonl` (3459 cases).

## What was built

New production tool (only owned source file):

`gpu/tools/audit_shader_pipeline_inputs.py`

Subcommands:

- `ledger` — complete 18190-row entry/exclusion ledger: all coverage statuses,
  2430 special/extent exclusions, 9 non-MTLB carriers, 9 unpaired graphics
  entries and the 4 bounded CPU-limit unknowns. Never modifies corpus evidence.
- `classify` — static missing-input ledger for every selected case: declared
  function constants with type/index/required/optional, materialisation status,
  per-slot attachment plans, exact required integer formats, patch descriptors.
- `plan` — auditable variant specs. Every constant has index, name, exact bytes,
  origin (`captured` / `descriptor_derived` / `synthetic_diagnostic` /
  `...:optional_defined`), and every descriptor records provenance. Rows whose
  integer targets the frozen C bridge cannot express get an audit-only
  `translator=cli` route through the existing standalone `metal2vulkan` CLI.
- `run` — one isolated child per candidate: `RLIMIT_AS=2 GiB`, `RLIMIT_CPU=12 s`,
  `RLIMIT_CORE=0`, parent wall timeout 30 s. Default worker count 1. Resumption
  uses `done.json` keyed by full variant SHA256 + compiler SHA256 + bridge SHA256;
  a stale `result.json` is removed before a retry so a native abort cannot
  masquerade as the previous run.
- `summarize` — variant-level and unique-case outcomes, translator-backend split,
  baseline→outcome map, descriptor-materialised list, explicit limitations.
- `selftest` — pure checks plus optional `--host` real representative shaders.

Independent tests: `workers/corpus_inputs/test_audit_shader_pipeline_inputs.py`
(35 tests, unittest). They cover exact Metal type/byte encodings, AIR metadata
parsing, descriptor selection/refusal, CLI integer-format planning, explicit
synthetic origins, error classification, candidate identity/resume,
translation-survives-native-abort composition, pre-translation crash accounting
and synthetic-corpus ledger I/O.

## Full static input coverage (no compiler invocation)

Evidence under `workers/corpus_inputs/evidence/inputs_*`; the combined
`inputs_all/inputs-ledger.jsonl` has one row per case:

| family | cases | result |
| --- | ---: | --- |
| graphics MRT / colour targets | 197 | 47 materialisable through the frozen production bridge; 150 need exact integer formats (`RGBA32Sint`/`RGBA32Uint`/`R32Uint`/`RG32Uint`/`R16Uint`) and are materialisable through the audit-only translator CLI; 150 rows carry `production_bridge_blocked` |
| graphics patch control points | 11 | 11/11 materialisable with explicit PerPatchControlPoint attributes/layouts |
| graphics function constants | 325 | 325/325 have declared constants and an explicit canonical initial set |
| compute function constants | 2926 | 2926/2926 have declared constants and an explicit canonical initial set |

This is a missing-input classification, not a compile/pixel claim. Every row has
the source/case name, AIR hash, declared constant types and flags, exact synthetic
values or blocked reason, required target formats, compiler/bridge/CLI hashes and
status.

Useful aggregate facts:

- compute required-constant counts range 0..many; 449 cases have exactly 1 required
  constant, 473 have 5, 407 have 4, 349 have 6; 35 cases have none required but
  still request an optional constant.
- compute declared types in this family: ushort 5529, short 4024, bool 2815,
  uint 1853, float 124, int 93, half3 22, half 8, plus a few vectors.
- graphics declared types: bool 2123, int 155, half 31, uint 20, float 9, uchar 3.
- 279 cases have zero required constants; 71 of those are function-constant cases
  whose missing input is an optional `is_function_constant_defined` value.
- required Metal integer target formats across the 150 production-bridge-blocked
  rows (per-slot occurrences): RGBA32Sint 173, RGBA32Uint 173, R32Uint 47,
  RG32Uint 22, R16Uint 3. This is the actionable bridge-surface request.

## Representative real host execution

All production runs use the frozen bridge; integer-target rows additionally use
the existing CLI as an audit route. Variant counts include explicit
`undefined`/`optional_defined` alternatives; unique-case outcomes use the best
variant per case.

| evidence | unique cases | unique-case outcomes |
| --- | ---: | --- |
| `graphics_mrt_materializable` (production bridge) | 47/47 | 26 native compiled, 17 native refused, 4 backend abort (known `load_base_instance` assertion) |
| `graphics_mrt_cli` (audit CLI, one case per declared target signature) | 44/44 | 5 native compiled, 39 native refused `Unsupported graphics output`; all 44 translated |
| `graphics_mrt_examples` | 4 explicit | 1 native compiled (`graphics-00003` half2 via bridge), 2 native refused (`graphics-01840` bridge, `graphics-01566` CLI), 1 backend abort (`graphics-00081` MRT via bridge) |
| `graphics_patch_all` | 11/11 | 11 bridge expanded-stage-topology refusals after successful descriptor translation |
| `graphics_fc` | 16 representative | 8 native compiled, 5 native refused, 2 translation refused, 1 bridge crash at `translate_graphics` |
| `graphics_fc_optional_only` | 36 | 19 native refused, 17 translation refused (best variant); 27/36 *undefined* variants crashed in the bridge before translation |
| `compute_fc` | 16 representative | 3 native compiled, 7 native refused, 6 backend abort (known texture-source assertion) |
| `compute_fc_optional_only` | 35 | 1 compiled, 17 native refused, 7 backend abort, 5 translation refused, 4 translator unsupported, 1 still missing input; 31/35 *undefined* variants crashed before translation |

Notable reclassifications (all baseline `required_pipeline_input_missing` or
`audit_descriptor_insufficient`):

- `graphics-00003` + `graphics-00081`: MRT attachment set materialised; 00003
  native-compiles, 00081 reaches the known backend abort instead of a descriptor
  error.
- `graphics-00037` and all 11 patch cases: descriptor error gone; the remaining
  refusal is the frozen bridge's expanded tessellation topology, not a missing
  descriptor.
- `graphics-01566` and 43 further integer-target signature representatives:
  translator CLI accepts exact integer formats; 5 native-compile and 39 reach
  `Unsupported graphics output`. The production bridge still cannot bind those
  formats; this is an audit-only route.
- `graphics-00063`: `COLOR_ATTACHMENT_INDEX` materialised, native compiled.
- `3ed15613…` (Sparserendering XHLRB scan): all five declared constants
  materialised; translation succeeds and the remaining abort is the known
  `pan_nir_lower_tex.c:55` texture-source assertion.

## Exact unresolved gaps

1. **Production bridge target-format surface.** 150/197 MRT cases declare integer
   colour targets. The frozen production render entry accepts only R8Unorm,
   RGBA8Unorm, BGRA8Unorm and RGBA16Float. The audit-only CLI route exists and all
   44 distinct target signatures translate; the production bridge still needs the
   exact formats above. Do not confuse the audit route with production support.
2. **Native Mali integer-output support.** Of 44 CLI signature representatives,
   39 are refused by the frozen Mali compiler as `Unsupported graphics output`
   and 5 compile. That is a backend capability result, not a translator or
   descriptor failure.
3. **Frozen bridge specialization segfault.** `tegu_air_library_specialize`
   crashes (SIGSEGV) for some constants, most reproducibly when an *optional*
   constant is added on demand: 27/36 graphics and 31/35 compute optional-only
   variants, plus `graphics-00087`/`graphics-01279`. The child process contains it
   and records the phase and attempted value. Supplying all declared optional
   constants explicitly (`--include-optional`, origin `...:optional_defined`)
   avoids many of these and produced the reported best outcomes; it is a different
   Metal branch, not a silent default.
4. **Frozen bridge expanded stage topology.** The 11 patch cases translate with
   a valid control-point descriptor, then the bridge refuses
   `unsupported expanded render stage topology`. This is a bridge/backend stage
   ABI gap, not an AIR decode or missing-layout failure.
5. **Dual-source attachment extension crash.** `graphics-00064` requests
   attachment index 1 after its `SOURCE_/DESTINATION_COLOR_ATTACHMENT_INDEX`
   constants are supplied; extending the attachment list crashes the frozen
   bridge at `tegu_air_render_pipeline_create_with_attachments`. Recorded.
6. **Fixed UI `fixed_frag_lph_cpf` reflection.** Supplying the declared constants
   reaches `unsupported: Reflection (fixed_frag_lph_cpf); needs NotImplemented`.
   No captured constant values for this function were found; the captured
   `cph` sibling fixture is not used as if it were `cpf`.
7. **Full-category compiler batches not run.** Executed: 205 unique cases /296
   variants. Not run: 106 further CLI MRT cases (same 44 signatures), 273
   graphics FC cases, 2875 compute FC cases. Static ledgers cover all of them;
   the missing column is actual compiler verdicts. A root lease is requested.
8. **No hardware execution.** No pixels, runtime binding or device claim.

## Commands actually run

```sh
python3 gpu/tools/audit_shader_pipeline_inputs.py ledger \
  --output gpu/build/deepseek-parallel-20261025/workers/corpus_inputs/evidence
python3 gpu/tools/audit_shader_pipeline_inputs.py classify --family all \
  --output .../evidence/inputs_all
python3 gpu/tools/audit_shader_pipeline_inputs.py plan/run/summarize ...   # representative sets
python3 gpu/tools/audit_shader_pipeline_inputs.py selftest --host \
  --output .../evidence/selftest
python3 -m unittest -q test_audit_shader_pipeline_inputs.py                # 35 tests OK
```

Last selftest results: `SELFTEST PASS (pure+host)`; unit tests 35/35 OK;
`py_compile` OK. All evidence summaries record the tool SHA256 that produced them.

## Files

- `gpu/tools/audit_shader_pipeline_inputs.py` (new, owned)
- `workers/corpus_inputs/test_audit_shader_pipeline_inputs.py` (new, worker)
- `workers/corpus_inputs/STATUS.md` (this file)
- `workers/corpus_inputs/evidence/` (private, ignored):
  `README.md`, `ledger.jsonl`, `ledger-summary.json`, `inputs_*`, `graphics_*`,
  `compute_*`, `selftest/`, `done.json`, per-candidate `spec.json` /
  `translation.json` / `result.json` / SPIR-V / `worker.log` / `progress.json`.

No production file was edited. No corpus baseline evidence was modified.
