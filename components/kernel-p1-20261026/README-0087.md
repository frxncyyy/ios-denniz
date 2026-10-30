# 0087 - scope GPU cache maintenance to the job's own buffers (offline, NOT applied, NOT flashed)

Host-only. No phone/USB/adb, no boot image built, nothing applied to gpu/src, shim/, xnu/patches or build/xnu-stage.
Series position: after 0084 (nord2-gpu), 0085 (sweet-fb-surface-retention), 0086 (nord2-gpu-import-budget).
0085 touches only SweetFramebuffer; 0087 builds on the 0086 versions of `bsd/dev/nord2_gpu_core.{c,h}` and
`iokit/Kernel/arm/Nord2GPU.cpp` (it will not apply on 0084 alone).

## Files
| file | purpose |
|---|---|
| `0087-scope-cache-maintenance-to-job-buffers.patch` | xnu series patch (`-p1`, a/b paths like 0085/0086): core.c, core.h, Nord2GPU.cpp |
| `candidate-gpu-source-0087.patch` | same change for the candidate source of truth (gpu/include, gpu/src, shim/nord2-xnu, gpu/tests); apply from `work/` with `patch -p1` AFTER `candidate-gpu-source-0086.patch`; keeps tools/check_sync.sh valid |
| `tests-0087-bridge-list-closure.patch` | optional, independent: pointer-closure check in `gpu/tests/nord2_metal_bridge_test.c` (generated against the file as of 2026-10-26 13:15; regenerate if the test changes) |
| `patched-sources-0087/` | resulting sources; `objs-0087/` ARM64 XNU-flag objects + core compile log; `tests-new/*-0087.c` resulting test files |

## Problem (source facts, line numbers refer to the 0086 sources)
`sync_page()` in Nord2GPU.cpp always runs `FlushPoC_DcacheRegion` (clean+invalidate), so "clean" and "invalidate" cost the same per 4 KiB page.
* `try_start()` (core.c ~765-776) cleaned **every MAPPED buffer of the context** before each job (once per job; phase 0 / fragment-only).
* `job_finish()` DONE (core.c ~368-376) invalidated **every GPU_WRITE buffer of the context** after each job. All driver buffers and imported
  IOSurfaces are mapped `GPU_READ|GPU_WRITE`, so this is every buffer.
* Cost therefore scales with all mapped memory, not with what the job touches. The 0086 budget work raised the imported-surface headroom to 128 MiB:
  three 11,239,424 B display surfaces are 3 x 2,744 = 8,232 pages = 16,464 page-CIVAC per job just for the surfaces, plus 2 x 512 for the 2 MiB tiler heap.
  (Parent's measurement from `c6gc-test/ui-usb/xnu-live.log`: 1.88 s / 61 jobs, ~31 ms/job. I did not re-derive that number; the log carries no
  per-job timestamps I could find. The page counts above are from source/log MAP lines: `NORD2 GPU MAP: ctx=1 bytes=11239424`.)

## Change
* `n2c_job.full_maint` decides the scope at submit time. Scoped (default): clean `job->bos[]` before the job (those still MAPPED), invalidate the
  `job->bos[]` that have `GPU_WRITE` after DONE. A GPU write can only land in a buffer mapped `GPU_WRITE`, so the invalidate set is the same
  filter as before, restricted to the listed buffers. `write_mask` is deliberately NOT used to narrow the invalidate (the bridge uses it as a
  hint and v1 compute sets it conservatively), only the list is.
* Whole-context maintenance (exactly the old behaviour) is used when
  1. `n2c_submit.flags & N2C_SUBMIT_FULL_MAINT` - new core-internal flag. `Nord2GPU::compute()` (selector 3, ABI v1 compute, used by the probe)
     sets it: its FAU/resource tables hold raw GPU pointers to buffers that are not in its 4-handle list (the probe's output buffer is such a case);
  2. `nr_handles == N2C_JOB_BOS (32)`: a full list cannot be trusted to be complete (see compute v2 below);
  3. the rollback valve is on: boot-arg `nord2_gpu_cache=1` (read once in `configure()`, logged as `NORD2 GPU: cache maintenance
     per-job buffer list | whole-context (boot-arg)`). Default is scoped.
  `SUBMIT_JM` (ABI v2, the only path the bridge uses) cannot request the flag from user space; unknown `flags` bits are rejected with N2C_E_ARGS.
  There is no "job without a list": `n2c_submit` already rejects `nr_handles == 0` and `SUBMIT_JM` rejects `version != 2`, so the "legacy ABI" fallback is
  concretely item 1 plus item 2.
* New counters `stats.cache_clean_pages / cache_inval_pages / cache_full_jobs` (host-visible; not yet surfaced in `nord2_gpu_status`).
* `n2c_bo_sync` (explicit SYNC selector, used by readbacks and the tiler diagnostics) is unchanged.

## Correctness argument for imported/display surfaces
* CPU-written before GPU read: the surface is read/written by the GPU only through a job that lists it (target, texture binding, LOAD preload source, snapshot);
  it is cleaned at the start of that job exactly as before. Dirty CPU lines of an UNLISTED buffer are never evicted over GPU data because the GPU does not
  write that buffer in this job; if the CPU wrote it, the next job that lists it cleans it first.
* GPU-written before CPU/display read: render targets and MRT/resolve targets are listed with `write=1` (and have `GPU_WRITE`), so they are invalidated at DONE
  before `present()` / readback. Surfaces not written by the job are not invalidated, which is correct because the GPU did not change them.
* Ordering is unchanged: the clean still happens in `try_start()` after the older job of the context completed, the invalidate in `job_finish()` on DONE only.
  A FAULTED/CANCELLED job still performs no invalidate (unchanged, test covers it).

## Bridge list completeness (gpu/metal/nord2_metal_bridge.c, read-only review, 2026-10-26)
| job type | where | list contents | complete? |
|---|---|---|---|
| render draw v1..v4 (incl. MRT, resolve, snapshot, clear-only via `n2mb_clear`, tiler matrix) | `draw_locked` 1135-1235 | MRT/target (w), vertex/index/bindings (`resolve`), tls, cmd block `owned[0..1]`, snapshot `owned[4]`, preload code `owned[5..6]`, code[0..3], persistent heap (w); overflow of the 32 slots makes `add_ref` return BAD (draw refused), never silent truncation | yes. `owned[7]` (VA spacer, diagnostic) is intentionally unlisted and never accessed by the GPU |
| LOAD preload | same job; reads target in place (c6h) | target is both target and binding (`add_ref` dedups, keeps the write bit); preload shaders listed | yes |
| compute v1 `dispatch` | 807-818 | cmd, code, up to 8 resources, all write-flagged | yes |
| compute v2 `dispatch_v2` | 749-763 | cmd (w), tls, wls, buffers (manifest write flag), textures (manifest write flag), code **only if `nr_handles < 32`** | **NO in one corner**: `if(req.nr_handles<32){(void)add_ref(&req,code,0);}` silently drops the shader code BO when the buffer/texture list fills all 32 slots. With scoping, a freshly written code BO would then not be cleaned. Covered in the kernel by item 2 (full list => whole-context maintenance, tested). Recommended bridge fix (parent/owner): add `code` first (or refuse the dispatch) so the list is never silently incomplete; with that the item-2 fallback becomes belt-and-braces |
| blit/copy `n2mb_copy` | 862-873 | no GPU job: CPU memcpy src->dst (dst never an imported surface, `external_cpu` refused) | n/a. dst dirty lines are cleaned by the next job that lists dst (any consumer must list it through `resolve`/target); no gap |
| kernel `SUBMIT_COMPUTE` (ABI v1 / probe) | Nord2GPU.cpp `compute()` 438-446 | cmd, shader, res, fau only; probe's FAU points at the output buffer which is not listed | **NO by design**: forced to `N2C_SUBMIT_FULL_MAINT` |
| other callers of `SUBMIT_JM` | grep of gpu/ (excluding build/) | only the bridge; `frontend-repro.m` and the host tests are mocks | - |

Independent evidence of closure (not just code reading): `tests-0087-bridge-list-closure.patch` adds a scan to the fake kernel in
`nord2_metal_bridge_test.c`: for every `SUBMIT_JM`, every 64-bit word in a listed non-exec buffer <= 256 KiB that falls inside any live buffer's VA range must point into a
LISTED buffer. Result on the current bridge: 8,004 cross-buffer GPU pointers across all scenarios (compute v1, draw/LOAD/clear/MRT/snapshot/tiler matrix), 0 unlisted.
Mutation check: removing the `code[i]` `add_ref` from a scratch copy of the bridge makes it fail at once (`UNLISTED GPU pointer 1008000000 ... kind 2`).
Limits of the check: it covers pointers stored in descriptor/command buffers the test can see, not pointers a shader computes at run time, and compute v2 is not
driven by this test file (v2 is covered by the code review above and by the frontend/integration tests).

## Verification performed (host only)
* `nord2_core_test` (ASan/UBSan, `cc -O1 -fsanitize=address,undefined`; sources core/mmu/va/mem): baseline 0086 = 15 scenarios PASS; with 0087 = 17 scenarios PASS
  (existing 15 unchanged, including the 1024-cycle reuse test whose `sync_out==1024` still holds). New `test_scoped_cache_maintenance` checks per-buffer sync logs:
  listed vs unlisted, read-only listed (clean only), CPU-uncached listed (nothing), imported surface as render target (two-phase job, one clean + one invalidate),
  FULL flag, rollback valve, unknown flag rejected, faulted job no invalidate. `test_full_list_is_not_trusted`: 31 handles stay scoped, 32 handles go whole-context.
  Mutation check: the new test FAILS against the unpatched 0086 core.c (`syncs_in(B,3,true)==0` at the first scoped job).
* ARM64 XNU-flag compile (tools/xnu_staged_compile.py, CI clang flags, `-Wall -Wextra -Werror`) of the patched `nord2_gpu_core.c`: 0 warnings, Mach-O arm64 object in `objs-0087/`;
  patched `Nord2GPU.cpp`: 0 errors (object in `objs-0087/`; its ~1,300 warnings are the pre-existing `-Wunsafe-buffer-usage` noise, suppressed). Compiled from a scratch stage copy; `build/xnu-stage` was not touched.
* Patch application: 0087 applies cleanly on `patched-sources/` (0086) and on `orig/` + 0086 in order, reproducing the compiled files byte for byte; candidate 0087 applies
  after candidate 0086 on a scratch copy of gpu/include, gpu/src, gpu/tests and shim/nord2-xnu, and the gpu/ and shim/ copies of core.c, core.h, Nord2GPU.cpp are identical (check_sync property).
* Not done: kernel link/pack, the full `run_host_tests.sh`, any hardware execution. Gains are therefore a source-level expectation, not a measurement.

## Expected effect and limits
* Jobs that do not list a display surface stop paying for all three surfaces (about 16.5k page-CIVAC per job); sweeps of every other unlisted pipeline/texture/scratch buffer vanish too (the persistent heap is listed by every render job and stays).
* A render job whose target IS a display surface still cleans and invalidates that surface once (2 x 2,744 pages, ~1/3 of the old surface cost) plus the 2 MiB heap that every render job lists
  (2 x 512 pages). Further, separate wins if needed: a "GPU-only, never CPU-touched" BO flag for the heap/tls, or dirty-range hints for display targets. Not part of 0087.
* Measure on the next authorized hardware run by comparing the same 61-job workload; `stats.cache_*_pages` gives the page counts (log them if wanted).

## Risks
1. Any future producer that lets the GPU read/write a buffer without listing it will see stale data (read) or stale CPU lines (write) where the old code masked it. Mitigations: pointer-closure test,
   FULL flag for pointer-chasing ABI, 32-slot fallback, boot-arg `nord2_gpu_cache=1` to restore the old behaviour without rebuilding the kernel.
2. Compute v2 `code` omission at 32 slots (above): only safe through the kernel fallback until the bridge is fixed.
3. Hardware unverified: cache behaviour of imported surfaces mapped by other processes (SpringBoard/backboardd) is argued from coherent inner-shareable maintenance by VA/PA; it is unchanged in kind but the set of lines
   maintained per job is smaller, so a latent incoherence that the old full sweep happened to repair would now show up as stale display data in that job's target only. First hardware run should keep `nord2_gpu_cache=1` as the A/B reference.
4. `NORD2 GPU: cache maintenance ...` is one new IOLog line at configure time (fits the existing log quota).
5. `n2c_submit` gained a trailing `flags` field: any out-of-tree caller that does not zero-initialise the struct would now be rejected (only Nord2GPU.cpp and the core test call it, both zero-initialise).

## Composition
* 0085: independent (SweetFramebuffer). Together they remove two per-frame costs: 0085 keeps surface refs until the panel copy, 0087 stops charging every job for all surfaces.
* 0086: raised the import budget to 128 MiB, which made the per-job full sweep grow with the number of imported surfaces; 0087 is the matching fix. Same files (core.c/core.h/Nord2GPU.cpp) - apply order 0086 then 0087.
  0087 does not change `imported`/`wired` accounting, the transport ABI, `nord2_gpu_status`, or user-visible limits.
* Adoption: copy the xnu patch to `xnu/patches/0087-...`, apply `candidate-gpu-source-0087.patch` (after 0086's) from `work/`, regenerate the stage overlay, rebuild with the kernel P1 flow. Rollback: boot-arg or drop 0087.
