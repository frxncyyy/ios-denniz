# Priority manifest — remaining function-constant batches

Static, no compiler invocation. Tool SHA256 `6a6ce410...`; generated from the
frozen corpus plus existing `workers/corpus_inputs/evidence/inputs_*` ledgers and
the authoritative `ledger-claude-20261025/springboard_critical.json` join.

Files:
- `priority-manifest.jsonl` — 3148 rows (273 graphics-FC, 2875 compute-FC),
  sorted by `selection_rank`, SHA256 `b40d93a4...` at tool 6a6.
- `priority-p0..p3.jsonl` — tier subsets (6 / 267 / 2574 / 301).
- `priority-top100.jsonl` — first 100 rows for quick review.
- `priority-summary.json` — counts, observed join, source classes, origins.

Row schema (schema 1):
```
family, case, name, library_source, library_sha256,
vertex_air_sha256, fragment_air_sha256, air_sha256,
source_class, priority_tier, priority_score, selection_rank,
observed_priority, observed_name, observed_tier, observed_evidence,
constant_origin, descriptor_origin, joint_origin_rank, origin_rank,
app_coverage, synthetic_constant_values,
declared_constant_count, required_constant_count, optional_constant_count,
constant_types, expected_input_shape[],
pairing_evidence, descriptor_evidence, priority_reasons[],
compiler_sha256, bridge_sha256, remaining, previously_executed
```

Origin rules:
- `constant_origin=captured` only for actual captured constant values. None of
  the remaining cases has one.
- `descriptor_origin` is `captured` only for explicit captured donor/fixed-UI
  pairing or attachments/serial; `abi_candidate`/AIR-derived is `inferred`;
  synthetic text is never treated as captured (the phrase "not captured" is
  ignored).
- `joint_origin_rank`: captured > inferred > synthetic; captured descriptor plus
  synthetic constants is `inferred`, not `captured`.
- `app_coverage=false` for every row: exact application constant values are
  unknown. Synthetic values are diagnostic branches only.

Tier rules:
- P0: authoritative observed join to `springboard_critical.json`: D8F18H,
  siriChipVert+Frag, and 4 QuartzCore entries. **6 rows** intersect the remaining
  FC batches. MPSImage D2..D8 siblings are `S2_sibling_of_observed` and therefore
  P1, not P0.
- P1: S1/S2 observed context plus role-inferred UI/graphics frameworks
  (RenderBox, CoreRE, VFX, VectorKit, SceneKit, SpriteKit, PencilKit, MetalKit,
  ARKit, RealityKit, UIFoundation...).
- P2: compute/ML acceleration (Espresso, MPS, Metal BVH...).
- P3: video/processor/other system frameworks.

No installed metallib path contains SpringBoard or backboardd; P0 is an
observed-role join, not a name match. Observed items total 180: 6 P0 rows inside
the remaining FC batches, 175 observed entries outside them.

Batch command (after final compiler/bridge hashes):
```sh
python3 gpu/tools/audit_shader_pipeline_inputs.py priority \
  --springboard-critical <critical.json> --compiler <comp> --bridge <bridge> \
  --output .../priority-current
python3 gpu/tools/audit_shader_pipeline_inputs.py plan --family graphics_fc \
  --compiler <comp> --bridge <bridge> \
  --priority-manifest .../priority-current/priority-p0.jsonl --output <dir>
python3 gpu/tools/audit_shader_pipeline_inputs.py run --workers 1 --output <dir>
python3 gpu/tools/audit_shader_pipeline_inputs.py summarize --output <dir>
```
Repeat for compute (with `--compute-entry v2`) and P1/P2/P3. The manifest is a
static input ordering, not a claim that any row compiles; each run records
compiled/refused/abort/missing-input/CPU-limit with exact source hashes.
