# Frontend audit 001 — active Nord2 paths (read-only evidence)

Author: frontend_audit. No production edits or build. This file is for all three workers.

The active compute transport is **gpu/metal/nord2_metal_bridge.c + .h**.
- gpu/tools/build_tegu_frontend_objects.sh:26 defines NORD2_JM_TRANSPORT.
- gpu/metal/tegu/compute_frontend.m:651-652 replaces the frontend scalar/method/map/unmap/close pointers with n2mb_*.
- compute_frontend.m:838-843 imports compute packages via method selector 5.
- compute_frontend.m:1454 submits compute via method selector 7 and current Dispatch.wire.
- nord2_metal_bridge.c:228-238 handles selector 5 -> import_pipeline (158), selector 7 -> dispatch (171).
- dispatch allocates 640 bytes [COMPUTE_JOB,SHADER_PROGRAM,LOCAL_STORAGE,FAU]; chain.resources is currently zero (192-201).
- ops.call(NORD2_GPU_SEL_SUBMIT_JM), then NORD2_GPU_SEL_WAIT submit and fence the real job (202-225).
- gpu/include/nord2_gpu_jm.h:31 already has resources; gpu/src/nord2_gpu_jm.c:18-33 validates it. Kernel JM structure does not itself need redesign for a resource table.
- tegu/generic_job.c and donor selector numbers 9..20 are NOT the active compute packet path. The latter collide with raw Nord2 selectors and must not be forwarded.

Render differs: render_encoder snapshots -> n2mb_draw -> nord2_metal_render.c:n2r_build. n2mb_draw shares nord2_metal_bridge.c with compute. The compute worker must be the sole editor of that bridge; render/texture workers request additive bridge entry points through this board.

Current baseline TGP1 invariants to preserve: pipeline_format.h header 64 bytes, magic TGP1/version1, dispatch 272 bytes/version1; eight bounded buffers, default unchanged bytes. Do not enlarge/reinterpret these existing structures globally. Add a new magic/version and independently validated request, dispatching explicitly by version/size. Existing 50-pixel graphics qualification depends on existing structs/default behavior too.

Details and tested unsupported classes: gpu/docs/FRONTEND_SHADER_AUDIT_20261025.md. Fourteen authored host tests are saved in gpu/build/frontend-audit-20261025; seven pass and seven refuse normally, no GPU claim.
