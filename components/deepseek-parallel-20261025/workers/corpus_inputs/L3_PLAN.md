# L3 full remaining-batch plan (paused on L2)

Status: **L3 granted, but paused while `lowering` holds `LEASE TAKE L2`** for
`libnord2_mali_runtime.so` (TGP2 4 KiB code-cap fix). Do not start until
`LEASE RELEASE L2` and final hashes are posted.

## Inputs

- Tool: `gpu/tools/audit_shader_pipeline_inputs.py` SHA256 `36f61b34...`.
- Priority manifest: `priority-current/priority-manifest.jsonl` (SHA `ddd51078...`
  at tool 8dd5; regenerate after L2 with the final tool/compiler/bridge and
  record the new hash).
- Tier subsets: `priority-p0.jsonl` (51), `priority-p1.jsonl` (202),
  `priority-p2.jsonl` (2594), `priority-p3.jsonl` (301).
- Counts: 273 graphics-FC + 2875 compute-FC = 3148 remaining cases.

## Source snapshot at L3

After `LEASE RELEASE L2`, rehash:
```sh
COMP=gpu/build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so
BR= gpu/build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so
sha256sum "$COMP" "$BR"
```
Copy both to `sources/<comp8>-<bridge8>/`, regenerate:
```sh
python3 gpu/tools/audit_shader_pipeline_inputs.py priority \
  --compiler <snapshot-compiler> --bridge <snapshot-bridge> \
  --output gpu/build/deepseek-parallel-20261025/workers/corpus_inputs/priority-current
```
Recreate tier files from the new manifest (same script used before).

## Run order (workers=1, isolated 2 GiB/12 CPU/30 s)

For each tier, run graphics first then compute (priority order preserved by
`--priority-manifest`):
```sh
plan --family graphics_fc --compiler <comp> --bridge <bridge> \
  --expected-compiler-sha256 <comp8> --expected-bridge-sha256 <bridge8> \
  --priority-manifest priority-current/priority-p0.jsonl --output <root>/graphics_p0
run --workers 1 --output <root>/graphics_p0
summarize --output <root>/graphics_p0
```
Repeat for `graphics_p1`, `graphics_p2`, `graphics_p3`, then
`compute_fc` with `--compute-entry v2` for each tier. Between graphics and
compute batches, check `pgrep -x rustc|ninja|meson`; if a new lease is taken,
pause.

Expected per-case outcomes: compiled / native refused with reason / backend
abort / translation refused / translation missing input / worker timeout; each
row carries origin (`captured`/`inferred`/`synthetic`) and compiler+bridge hashes.
Synthetic constants are never app coverage. Persist under
`evidence-current-<comp8>-<bridge8>/`; post LEASE TAKE/RELEASE and summary.
