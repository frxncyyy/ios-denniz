# 0112 - display-surface reservation (frontend contract, kernel side)

Host-built only. Nothing here was flashed or run on a device. Series position: after 0111 (P14 image dir `gpu/build/device-mmu-p14-candidate`).

## Why
The framebuffer decides at vsync whether a display surface may be shown with `nord2_gpu_surface_busy(S)`. Until now that was true only while a submitted
job that declared `write_mask` for an import of S was unfinished. A frame the client has NOT yet submitted (still encoding) therefore looked idle, was
scanned out half-written or was replaced by the previous frame without the swap being requeued. The reservation lets the frontend say "jobs that will
write S are coming" at commit time, and lets the kernel keep S busy until those jobs are terminal.

## ABI (transport v2, two new selectors; both also work through the 0111 trap path)
| selector | in | out | meaning |
| --- | --- | --- | --- |
| `NORD2_GPU_SEL_SURFACE_RESERVE` = 13 | scalar[0] = n (0..8), structure = `uint32_t handles[n]` (this context's IMPORT_SURFACE handles) | scalar[0] = reservation id R | n = 0: success, R = 0 (availability probe). R is never 0 for n > 0 and is monotonic per context |
| `NORD2_GPU_SEL_SURFACE_RETIRE` = 14 | scalar[0] = R, scalar[1] = seq | none | seq != 0: busy until every job of THIS context with id <= seq is terminal (done, faulted-then-reset, cancelled, reset); seq = 0: cancel now; R = 0: success no-op |

Errors: `kIOReturnBadArgument` (n > 8, unknown handle, handle of another context, a buffer that is not an imported surface, seq that was never issued),
`kIOReturnBusy` (more than 4 live reservations in the context; id out = 0), `kIOReturnNotFound` (unknown or already finished R on RETIRE),
`kIOReturnUnsupported` (boot-arg kill switch, both selectors including the n = 0 probe, so the frontend falls back).
The call validator `nord2_gpu_call_valid` demands `structureInputSize == 4 * scalar[0]` and refuses n > 8 before any buffer is touched.

## Semantics and lifetimes (core: `n2c_resv_*` in nord2_gpu_core.{h,c})
* A reservation stores opaque surface tokens (what `Nord2GPU::resv_token` returns: the `SweetSurface *` behind the import's BufferOwner). It holds no buffer
  reference and nothing is dereferenced at check time, so releasing a buffer, destroying a surface or losing the client cannot cause a use-after-free.
  A recycled surface address can only matter for the (<= 200 ms or until-job) lifetime of the reservation and errs on the side of "busy".
* `nord2_gpu_surface_busy` (called by the framebuffer under no framebuffer lock, under the GPU lock here) additionally returns `n2c_resv_busy`, so every
  client and the vsync handler see the same answer, and a busy swap is requeued by the existing 0102 logic (at most 6 ticks, then `busy_forced`).
  RESERVE returns only after the reservation is stored under the same GPU lock `surfaceBusy` takes.
* Fail-safe: (1) an unretired reservation older than 200 ms stops counting and is counted (`resv_expired`); (2) a retired reservation ends when the jobs up to
  its seq are all terminal, never earlier and never by time (a long job keeps it); a faulted job is not terminal until its stop is verified or the GPU is
  reset; (3) closing a context (including process death) drops its reservations (`cancels`); (4) a GPU reset drops all of them; (5) at most 4 live per
  context and 32 overall, so a misbehaving client cannot starve the display beyond what its own frames could.
* Nothing blocks and no allocation happens: fixed table in `struct n2c_core`.

## Observability
`NORD2 GPU JOBS` line: `resv=<reserves>/<retires>/<cancels>/<expired>`. `NORD2 FB PRESENT` line: `resv_expired=<n>`. Boot line:
`... surface reservations on|off`. Kill switch: boot-arg `nord2_gpu_resv=0` (alias `n2resv=0`).

## Files
Patch (xnu stage, applies after 0111): `0112-display-surface-reservation.patch` (nord2_gpu_core.{h,c}, nord2_gpu_transport.h, Nord2GPU.cpp, SweetSurface.{h,cpp},
SweetFramebuffer.cpp). Candidate mirror: `candidate-gpu-source-0112.patch` (gpu/, shim/nord2-xnu/, and the three tests). Sources: `patched-sources-0112/`.
Regeneration and byte-identity proof: `regen-0112.sh`.

## Tests (host, ASan+UBSan)
`nord2_core_test.c` (+3 scenarios, 26 total): probe/n = 0, argument errors, per-context monotonic ids, 5th reservation Busy, cancel, retire unknown/cross-context,
seq never issued, double retire, expiry boundary at 199/200 ms, retired reservation not expiring under a running job, DONE release, fault -> STOPPING stays busy
-> verified reset releases, RESET drops retired and unretired, context close drops, kill switch, token callback (NULL = not a surface).
`nord2_transport_test.c`: selector numbers, validator for n = 0..8, size/count mismatch, n > 8, 64-bit n without wrap, RETIRE shape. `nord2_trap_abi_test.c`: the
calls as the frontend builds them through the trap block, reply packing of R.
Mutation checks (each must fail the core test): no expiry, no drop on close, no drop on reset, release on non-terminal state, no per-context limit, no imported check.

## Not covered by host tests
The IOKit user-client glue (selector switch, boot-arg read, JOBS/FB lines) is compiled by the kernel build but exercised only on a device. The vsync requeue path
that consumes `surface_busy` is covered by the 0102 `sweet_fb_swapwait_test`, not by a model of a reservation.

## Follow-up 0113 and repo sync (kernel sources P14 + 0113)
`0113-transport-trap-block-yields-to-frontend.patch` (+ `candidate-gpu-source-0113.patch`, `regen-0113.sh`, new test `nord2_trap_order_test.c`): the IOConnectTrap6 call block in
`nord2_gpu_transport.h` is wrapped in `#ifndef NORD2_GPU_TRAP_CALL`, because the frontend's `gpu/metal/tegu/nord2_transport.inc` defines the identical macros and struct and
`build/api-surface-20261027/host/api_surface_frontend_test.m` includes it before the header (redefinition error otherwise). Kernel code is unchanged; P15 images were rebuilt for traceability
(`device-mmu-p15-candidate`). The repo's gpu/include, gpu/src, shim/nord2-xnu and gpu/tests/nord2_*_test.c are byte-identical to the P15 stage; `run_all.sh` then skips re-applying the candidate patches.

## Re-stage of gpu/build/xnu-stage (approved by the coordinator, after "kernel sources P14 + 0113")
* Backup of the previous stage, obj tree, fakeroot, sdk-kernel-platform and the old 0084 patch: `gpu/build/xnu-stage-backup-pre-p15-20261028/`.
* `tools/stage_xnu.sh` (baseline CI tree + overlay of the repo's gpu sources, now P15) then `tools/regenerate_patch.sh` rewrote `xnu/patches/0084-nord2-gpu.patch`
  (sha256 2377ba5f50dd702a..., 40 files incl. nord2_gpu_irq.h, nord2_gpu_lvts.{c,h}); the reverse-apply dry-run check in run_host_tests.sh passes.
  The xnu-side P15 files that are not in the repo (SweetFramebuffer/SweetSurface/sweet_fb_*.h, nord2_cpufreq.*, bsd/conf/files entries, 0099 sched) are NOT in xnu/patches: they live in the K patches 0085..0113 and in device-mmu-p15-candidate/kernela/stage.
* The pristine base that `build_p1_candidate.py` (device-mmu-pNN) expects as STAGE is the OLD stage: for any rebuild of the P9..P15 series point STAGE at
  `gpu/build/xnu-stage-backup-pre-p15-20261028/xnu-stage` (the new xnu-stage already contains the 0084 overlay of P15 sources and would make patch 0085.. fail).
