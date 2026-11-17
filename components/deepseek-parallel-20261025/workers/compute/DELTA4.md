# Compute delta4 — TGP2 TLS/WLS transport (host only)

Date: 25 October 2026. No phone/adb/fastboot/USB/mount/writer/publish/CI or commit.

## What changed

`gpu/metal/nord2_metal_bridge.c` now consumes the lowering-ACKed
`tegu_pipeline_info2.tls_size/wls_size` words for every TGP2 dispatch.

- `n2c_prepare_local` queries `NORD2_GPU_SEL_STATUS` for the shader-core mask,
  computes `core_id_range` exactly from `shader_present_lo/hi`, and allocates
  bridge-owned BOs before the command is built.
- TLS: `tls_size` field = `pan_get_stack_shift(tls_size)`; per-thread bytes =
  next power of two of the 16-byte-aligned TLS size; allocation =
  `bytes_per_thread * 512 * core_id_range` (arch9 max threads/core); base is
  4096-aligned, packed as `ptr>>8` with `PACKED` address mode.
- WLS: `wls_size_scale = log2(next_pow2(max(wls_size,128))) + 1`;
  `wls_instances = min(next_pow2(grid.x)*next_pow2(grid.y)*next_pow2(grid.z),
  core_id_range*512)`; allocation =
  `adjusted_size * wls_instances * core_id_range`; base must be 4096-aligned
  and must not cross a 4 GiB boundary. `allow_merging` remains false.
- Both handles are added to the final `NORD2_GPU_SEL_SUBMIT_JM` request and are
  released only after the job wait, or in the failure cleanup. If any
  allocation/status/layout check fails, dispatch returns `BAD` before submit;
  a package with nonzero TLS/WLS can never be dispatched with the old zero
  LOCAL_STORAGE descriptor.
- TLS-only, WLS-only, mixed and no-local-memory packages are packed
  separately. The no-local-memory 32-byte descriptor remains byte-identical to
  `nord2_desc_local_storage(...,0,0,0,0)`.

## Host tests and actual results

- `python3 build/deepseek-parallel-20261025/workers/compute/check_compute_v2.py`
  release + ASan/UBSan: **PASS** (TGP1 exact regression, TGP2 parser/manifest,
  full job, TLS-only, WLS-only, mixed, v9_pack oracle, no-local byte identity,
  oversized TLS refusal, partial-allocation release on refusal).
- The C test includes the generated pinned
  `build/mesa-build/src/panfrost/genxml/v9_pack.h` and uses
  `MALI_LOCAL_STORAGE_unpack` to verify tls_size, address mode, TLS base,
  WLS instances/scale and WLS base independently of the bridge packer.
- Integration from `.../overnight-20261024/work`:
  `TEGU_AIR_BRIDGE_SO=.../libtegu_air_bridge.so python3
  gpu/tests/metal_resource_integration_test.py --candidate` => **610/610,
  0 required, 0 pending, release+ASan** on the delta4 bridge. Evidence:
  `workers/compute/evidence-integration-delta4.{log,json}`.
- Real producer with the lowered compiler
  `libnord2_mali_runtime.so 742cdf3d...`: **7/7 PASS**, receipt
  `gpu/build/compute-v2-producer-tls-742/result.json`.
- No GPU execution, native build, device boot or hardware verification.

## Current hashes

- `metal/nord2_metal_bridge.c` `40aab80c141f25847064b62f62caabd819f940b3aa7538e761eb1c47e9d857f1`
- `metal/nord2_metal_bridge.h` unchanged `79928e7d...`
- `metal/tegu/pipeline_format.h` `ff2889fc...`
- worker test `check_compute_v2_bridge.c` `a43d0651...` (updated test file is
  under `workers/compute/tests/`; hash may change if integration extends it)
- worker runner `check_compute_v2.py` `9da8c0cc...`
- lowering compiler `742cdf3d8d10364402ff3f1c7896be43a884e93f95684edf891b206db1c7ffbb`
- cdylib `06a6c6ee...`

## Explicit unresolved / request

1. **Exact WLS instance sizing**: `struct nord2_gpu_status` exposes the shader
   core mask but not `THREAD_FEATURES`/`max_tasks_per_core`, so the bridge uses
   a safe upper bound of 512 concurrent workgroups per core. This may
   overallocate WLS (including a larger descriptor instance count) but cannot
   undersize it. Exact Mesa `pan_calc_wls_instances` needs an additive status
   field or another kernel-provided property. This is a known limitation, not
   silently substituted.
2. TLS/WLS BO lifetime and LOCAL_STORAGE words are host-tested only. Root L4
   native build/device test is required; no hardware claim here.
3. Integration should add its own fake-JM check for nonzero TLS/WLS to its
   suite so this safety gate is not only in the worker C test. Suggested
   vectors: TLS-only 64 bytes/thread/grid2, WLS-only 256 bytes/workgroup/grid2,
   and oversized allocation refusal with no submit.
