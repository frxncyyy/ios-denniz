Sidecar transport request for compute/render (textures owner)

The common helper and n2r v2 path are implemented and host-tested. To make them reachable in the installed UI, two owned-file wirings are needed; I am not editing them.

COMPUTE (nord2_metal_bridge.c/.h owner):
1. Add an additive `n2mb_draw_v2(uintptr_t, const shader*, const request*, const struct n2r_texture_resources*, uint64_t *out, unsigned *count)` (or an equivalent sidecar argument) next to n2mb_draw. Do not change n2mb_draw or its zero/default behavior.
2. In draw_locked, resolve the binding base as today, but use n2r_build_v2 with the sidecar when supplied. Retain the whole BO (current resolve already add_refs the BO; keep that). `binding.memory.bytes` may be the full span from level0; n2r v2 accepts larger only when a sidecar entry exists and requires `n2r_texture_binding.backing_length <= memory.bytes`.
3. Existing old n2mb_draw must keep calling n2r_build (old exact span validation). If you add fields to n2r_addresses, append only; do not reinterpret existing offsets.
4. For compute texture/sampler tables, call the frozen `n2t_layout_pack_sampled`/`n2t_layout_pack_storage` (or the `n2t_texture_pack`/`n2t_storage_pack`/`n2t_plane_pack` wrappers), `n2t_sampler_pack_for_format`, and `n2t_resource_entry`. Do not duplicate the packer. Table numbering/reflection remains your contract with lowering/frontend_audit. Storage uses table3 per your proposal; that is outside n2r.
5. Your initial 2D single-level subset is accepted, but do not refuse ASTC/cube/3D/array/MS on the helper's behalf: the helper supports them, subject to render/creation reachability. Unknown formats must use the helper's explicit error, never gd_format's BGRA default.

RENDER (render_encoder.inc owner):
1. Populate `struct n2r_texture_binding` from `TeguTexture` per binding: kind, allocation format, view_format (translator plan view/alias, or 0), width/height/depth of the view base, levels, layers (cube faces included), samples, array_length, first_level, first_layer, access (SAMPLE for sampled; storage read/write for storage), usage, storage_mode, swizzle_word (descriptor swizzle only; not plan shader_swizzle), and per-level offset/row_stride/slice_stride/layer_stride. Offsets must be relative to `mip[0].offset` (resolved binding base), not the BO. Set memory.bytes to the full span from level0 and backing_length to the same or less.
2. Populate `sampler` from the full TeguSampler state via `tegu_sampler_get_state` (or the new `newSamplerStateWithExactDescriptor:` for non-legacy states). Use `tegu_sampler_pack_state` per use with the plan's `compare_enabled`; the same Metal sampler can be regular in one draw and compare in another.
3. Creation currently forces arrayLength=1 and has no cube/3D/MS creation path. The helper supports those layouts, but they are NOT reachable until render implements allocation all-level offsets/strides and extraction. Do not claim end-to-end array/cube/3D/MS support before that; use explicit refusals in the meantime.
4. For ASTC, remove the resource-collection omission only together with the Rust typed view-format support and descriptor identity; otherwise an intentionally bound ASTC texture can still become neutral. The helper refuses unknown formats, but render must not drop the binding first.
5. VS/FS same Metal slot retention must use separate native binding arrays; n2r_v2 sidecar is keyed by the request binding number, not Metal slot. Do not reuse compositorTextures[slot] for both stages.

Cross-owner test fixture request: one real TeguTexture with 3 mip levels and one cube/2D-array case through the render snapshot into n2r_build_v2, asserting descriptor level/array bits and per-plane addresses; one sampler with repeat/mips/aniso through tegu_sampler_pack_state; one storage alias view (RGBA16F allocation / RGBA16Uint view) through n2t_layout_pack_storage. I can provide the expected words from Mesa v10 pack macros in the worker test.

Frontend audit: please review the exact structs/offsets in gpu/metal/nord2_texture_resources.h and the sidecar structs in nord2_metal_render.h. No phone/build/commit/publish by me.
