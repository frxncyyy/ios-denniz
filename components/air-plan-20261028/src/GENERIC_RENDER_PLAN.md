# Generic render path (TGJ1)

## Why

Through R105 every Metal pipeline class has its own exact-shape gate in
userspace and its own descriptor/command-stream builder inside XNU
(selectors 10-18). Each new CoreAnimation specialization therefore costs a
kernel build, and every hardware rule (64-byte rows, DCD flags, viewport
sign, scissor box) is rediscovered by hand. R105 renders the boot logo, but
the lock screen uses four further `fixed_vert_lph_spc`/`fixed_frag_lph_cph`
specializations that the exact R80/R85 classes refuse.

Snapintosh (Adreno, macOS) splits the work differently: the Metal plug-in
encodes complete command streams; the kext relocates, validates against a
resource list, maps, maintains caches and fences. NativeMetal reaches the same
place through metal2vulkan and a full Mesa Vulkan driver (Turnip). PanVK is
not built for this GPU's arch and needs a Panthor DRM ABI, so the Mali
equivalent here is the Snapintosh split using Mesa's own Mali encoders.

## Split

Userspace (`TeguMetal.bundle`):

- Shaders: unchanged path, AIR -> metal2vulkan SPIR-V -> Mesa Mali compiler.
- State: one draw encoder driven by compiler reflection and Metal encoder
  state, written with Mesa's header-only `v10_pack.h` packers and
  `cs_builder.h`, following panvk's per-draw emission. No per-class gates.
- Output per job: descriptor blob, CSF command words, resource list and
  relocation list.

Kernel (one additive selector, old selectors untouched):

1. Resolve the target, each resource span and the scratch-image source to
   owned buffers or imported IOSurfaces, with bounds.
2. Build the existing v7 per-job backing (`tegu_fullscreen_backing_create_v7*`)
   from those spans; copy the userspace-built scratch image into scratch.
3. Screen the CS: allowlisted opcodes only (NOP, MOVE48, MOVE32, WAIT,
   RUN_IDVS, RUN_FRAGMENT, RUN_FULLSCREEN, FINISH_TILING, FINISH_FRAGMENT,
   SET_SB_ENTRY, REQ_RESOURCE, FLUSH_CACHE2, HEAP_SET, HEAP_OPERATION).
   Memory-reaching or control-flow opcodes (LOAD/STORE_MULTIPLE, BRANCH,
   JUMP, CALL, SYNC_*, PROGRESS_*, STORE_STATE, PROT_REGION, exception
   handler) are refused.
4. Flush caches, submit through `tegu_gpu_render_fullscreen`, complete,
   flush the target, `sweet_fb_note_gpu_write()`.

No relocation engine is needed: per-job GPU VAs are a pure function of page
counts (scratch at `FS_APERTURE_VA`, target next, each span packed in order,
heap last), so userspace computes them with the same shared code. The GPU can
only reach pages mapped into the job backing, so the kernel need not
understand draws; it validates memory reach and command shape.

## Proving it without wasting CI

1. Host parity: the generic encoder reproduces the R105 UI1, UI2 and UI1/UI2
   LOAD render states byte-for-byte (same fake addresses) under ASan/UBSan.
2. Host kernel logic: the selector body lives in a shared C file
   (`generic_job.c`) tested on the host for relocation, screening, bounds and
   refusal immutability, as the other shared builders are.
3. One CI build adds the selector. On device, userspace routes UI1/UI2 through
   it; success = the same `TEGU UI2 TARGET` hash as R105
   (`0x956fd9786317b76`) with no GPU fault.
4. After that, new specializations are userspace-only (plus compiler fixes
   such as "Fragment varying layout does not cover interface"); root-image
   iterations, no kernel builds.

## Arch note

Mesa decodes GPU ID `0xb8a21030` as arch 11 and has no v11 genxml; all
descriptors that have worked on the device use v10 layouts, so the encoder
targets `v10_pack.h`.
