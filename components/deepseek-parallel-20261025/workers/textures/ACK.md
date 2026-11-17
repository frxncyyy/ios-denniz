Frozen common arch9 texture/sampler helper API (textures owner)

Root grant confirmed new helper ownership. Production file is HEADER-ONLY:
  gpu/metal/nord2_texture_resources.h
No .c link unit is required (all functions are static inline, freestanding, no libc calls). Include it directly; compute_frontend.m can include it through sampler_state.inc. I will not add a second packer.

EXACT NAMES/SEMANTICS (ACKed request changes included)
  #define N2T_TEXTURE_RESOURCES_VERSION 1u
  #define N2T_MAX_LEVELS 16u
  #define N2T_MAX_LAYERS 2048u
  #define N2T_MAX_SUBRESOURCES 256u
  #define N2T_MAX_DIMENSION 16384u
  #define N2T_MAX_DEPTH 2048u
  #define N2T_MAX_ADDRESS (1ull << 48)

  struct n2t_format_info { version,mali_format,bytes_per_block,block_width,
    block_height,clump_format,astc_plane,astc_block_width,astc_block_height,
    default_swizzle,flags; }
  struct n2t_level { uint64_t offset; uint32_t row_stride,slice_stride,
    layer_stride,width,height,depth; }
  struct n2t_texture_layout { version,kind,format,view_format,width,height,
    depth,levels,layers,samples,array_length,first_level,first_layer,
    swizzle_word,usage,storage_mode,access; n2t_level level[16]; }
  struct n2t_sampler_state { version,min_filter,mag_filter,mip_filter,
    address_s,address_t,address_r,normalized,compare_enabled,compare_function,
    max_anisotropy,reduction,lod_algorithm,clamp_integer_coords,
    clamp_integer_array,srgb_override,seamless_cube,magnify_cutoff,
    border_color_kind; float lod_min,lod_max,lod_bias,border[4];
    uint32_t border_raw[4]; }
  struct n2t_packed_texture { version,descriptor_words[8],plane_words[8],
    plane_count,payload_bytes,mali_format,levels,array_size,sample_count;
    uint64_t descriptor_address,payload_address; }

  int n2t_format_query(uint32_t metal, struct n2t_format_info *);
  int n2t_layout_validate(const layout*, uint64_t backing_address, uint64_t backing_length);
  int n2t_layout_subresource(const layout*, uint64_t backing_address,
                             uint32_t level, uint32_t layer, struct n2t_subresource*);
  int n2t_layout_pack_sampled(const layout*, uint64_t backing_address,
      uint64_t backing_length, uint64_t descriptor_address,
      uint64_t payload_address, void *payload, size_t payload_capacity,
      struct n2t_packed_texture*);
  int n2t_layout_pack_storage(... same arguments ...);
  int n2t_sampler_validate(const sampler*);
  int n2t_sampler_pack(const sampler*, uint8_t out[32]); /* refuses format-dependent ClampToZero */
  int n2t_sampler_pack_for_format(const sampler*, const format_info*, uint8_t out[32]);
  int n2t_sampler_layout_check(const sampler*, const layout*);

Compute-facing convenience wrappers (same implementation, no duplicate):
  int n2t_texture_validate(const layout*, uint64_t backing_length, uint32_t access);
  int n2t_texture_pack(const layout*, uint64_t backing_address, uint64_t backing_length,
      uint64_t descriptor_address, uint64_t payload_address,
      uint8_t out[32], char *why, size_t why_capacity);
  int n2t_storage_pack(... same ...);
  int n2t_plane_pack(const layout*, uint64_t backing_address, uint32_t level,
      uint32_t layer, uint8_t out[32], char *why, size_t why_capacity);
  int n2t_resource_entry(uint64_t address, uint64_t bytes, uint8_t out[16]);
  void n2t_texture_copy_descriptor(const packed*, void *out);
  void n2t_error_copy(int, char *why, size_t);

Stable errors: 0, -1 invalid/version, -2 kind, -3 format, -4 dimensions,
-5 levels, -6 layers, -7 samples, -8 stride, -9 span, -10 overflow, -11
address/alignment, -12 capacity, -13 sampler field, -14 sampler combination,
-15 unsupported, -16 access, -17 view, -18 output, -19 subresource count.
Every refusal validates completely before the first payload store. Descriptor
words are returned in the result; caller copies them to descriptor_address.

Requested additions from frontend_audit are implemented:
- `view_format`: 0 means allocation format. `n2t_view_format_check` only admits
  audited same-block/same-component-order reinterpretations (sRGB toggle,
  same-width float/int/unorm). RGB/BGR and different block geometry refuse.
  Caller must use the translator plan's view format and compile the matching
  variant. `shader_swizzle` is NOT folded into the descriptor; keep it in the
  compiled shader. `swizzle_word` is only the descriptor's own component
  swizzle.
- `compare_enabled`: per use; 0 packs compare Never, 1 packs the Metal compare
  function. Note correction: pinned panvk `panvk_translate_sampler_compare_func`
  actually returns NEVER when compareEnable is false; I do the same. The plan
  still must pass compare_enabled because compareFunction alone cannot express
  intent.
- Border: kind 0/1/2 presets, 3 custom float RGBA, 4 custom raw uint32 RGBA in
  `border_raw[4]`. Integer/depth formats refuse float presets; raw is required
  for integer border. ClampToZero resolves alpha/no-alpha per view format.
- Arch9 reduction: average only; min/max return -15 (v10 field not written).

Coverage: kinds 0..8, 1..16 levels, arrays/cube/cube-array, 3D depth, MS/MS
array fused plane, storage images (uncompressed non-depth non-sRGB), formats
listed in STATUS (including ASTC 2D sRGB/LDR/HDR, ETC2/EAC, depth16/32).
Explicit refusals: texture buffer kind 9, PVRTC/BC/YUV, combined depth/stencil,
stencil-only, format 41 A1BGR5, XR formats, >256 subresources, >16 levels,
>2048 layers, memoryless sampled textures, compressed/depth/sRGB storage.
This is descriptor/packing coverage, not GPU qualification.

Render sidecar API now implemented in `nord2_metal_render.h`:
  struct n2r_texture_binding { uint32_t binding, kind; uint64_t backing_length;
      struct n2t_texture_layout layout; struct n2t_sampler_state sampler; };
  struct n2r_texture_resources { uint32_t version,count; const binding *bindings; };
  int n2r_build_v2(...); int n2r_validate_v2(...);
Old n2r_build bytes are unchanged; empty sidecar is byte-identical; sidecar
entries are authoritative. Payload uses the free 8 KiB block tail (max 256
plane entries). Layout offsets are relative to the resolved binding base;
render must use mip[level].offset - mip[0].offset and set memory.bytes to the
full span. Bridge must pass the sidecar and retain the whole BO.

Tests: helper 687 checks PASS, same under ASan/UBSan; n2r v2 suite PASS under
ASan/UBSan; existing n2r semantic oracle PASS; bridge and compute_frontend
ARM64 freestanding compile PASS; freestanding helper object has no undefined
symbols. No phone/build lease/commit/publish. Full details in
workers/textures/STATUS.md.

Remaining cross-owner actions: render_encoder fact extraction/allocation for
array/cube/3D/MS; compute bridge additive `n2mb_draw_v2` sidecar transport;
frontend_audit review of exact structs/offsets; then integration tests. v2 is
not reachable from installed UI yet.
