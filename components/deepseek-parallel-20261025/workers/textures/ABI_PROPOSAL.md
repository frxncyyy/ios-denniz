Initial plan and ABI proposal (textures owner)

I read HANDOFF/TASK/AGENTS, the frontend audit, all board messages, current texture/sampler/n2r code, and pinned Mesa 81daea6 pan_texture.c/pan_format.c/pan_layout.c/genxml v9/v10 and panvk sampler mapping. I will not touch other owners' files, build a native plugin, use the phone, CI or publish.

OBSERVATION: active render path is render_encoder -> n2mb_draw -> n2r_build. Its generic binding has one VA plus width/height/row/format, and gd_texture hardcodes 2D/level0/layer0/sample1. The render snapshot already retains levelCount/mip[] but n2r has no place to put more than one plane. tegu_texture_state_build is arch11-only and is preserved.

REQUEST TO ROOT: ownership of two NEW pure production files
  gpu/metal/nord2_texture_resources.h
  gpu/metal/nord2_texture_resources.c
(dependency-free C11, usable by compute and render). If root prefers no new files, I will place the same symbols in gpu/metal/tegu/texture_state.{h,c} instead; please answer before integration. I am developing them under workers/textures/ meanwhile.

Proposed common ABI (names exact; ACK or replace before consumers code):
  #define N2T_TEXTURE_RESOURCES_VERSION 1
  #define N2T_MAX_LEVELS 16
  #define N2T_MAX_LAYERS 2048
  #define N2T_MAX_DIMENSION 16384
  #define N2T_MAX_SUBRESOURCES 256

  enum n2t_access { N2T_ACCESS_SAMPLE=1, N2T_ACCESS_READ=2, N2T_ACCESS_WRITE=4 };
  enum n2t_texture_kind { metal MTLTextureType numbering 0..9 }.
  struct n2t_level { uint64_t offset; uint32_t row_stride, slice_stride, layer_stride, size; uint32_t width,height,depth; };
  struct n2t_texture_layout {
    uint32_t version, kind, format, width, height, depth, levels, layers, samples;
    uint32_t first_level, level_count, first_layer, layer_count;
    uint32_t swizzle[4];              /* Metal component index per RGBA output */
    uint32_t usage, storage_mode, access;
    uint64_t backing_length;
    struct n2t_level level[N2T_MAX_LEVELS]; /* physical chain, layer-0 offsets */
  };
  struct n2t_sampler_state {
    uint32_t version, min_filter, mag_filter, mip_filter;
    uint32_t address_s, address_t, address_r, normalized;
    uint32_t compare_function, max_anisotropy, reduction, lod_algorithm;
    uint32_t clamp_integer_coords, clamp_integer_array, srgb_override;
    uint32_t seamless_cube, magnify_cutoff, border_color_kind;
    float lod_min, lod_max, lod_bias, border[4];
  };
  struct n2t_packed_texture {
    uint32_t descriptor_words[8], plane_words[8], plane_count, descriptor_bytes;
    uint64_t descriptor_address, payload_address, payload_bytes;
    uint32_t mali_format, levels, array_size, sample_count;
  };

  int n2t_format_query(uint32_t metal_format, struct n2t_format *out);
  int n2t_layout_validate(const struct n2t_texture_layout *, uint64_t backing_address, uint64_t backing_length);
  int n2t_layout_subresource(const struct n2t_texture_layout *, uint32_t level, uint32_t layer, struct n2t_subresource *);
  int n2t_layout_pack_sampled(const struct n2t_texture_layout *, uint64_t descriptor_address, uint64_t payload_address, uint64_t backing_address, void *payload, size_t payload_capacity, struct n2t_packed_texture *);
  int n2t_layout_pack_storage(const struct n2t_texture_layout *, ... same ...);
  int n2t_sampler_validate(const struct n2t_sampler_state *);
  int n2t_sampler_pack(const struct n2t_sampler_state *, uint8_t out[32]);
  const char *n2t_error_string(int);

Error codes are stable negative values: -1 invalid/version, -2 kind, -3 format, -4 dimensions, -5 levels, -6 layers, -7 samples, -8 stride, -9 span, -10 overflow, -11 address/alignment, -12 capacity, -13 sampler field, -14 sampler combination, -15 hardware-unsupported, -16 access, -17 view, -18 output. Descriptor and payload output are unchanged on every refusal (full validation precedes the first store). No output mutation, substitution or assertion suppression.

Coverage intended now: MTL kinds 0,1,2,3,4,5,6,7,8 (1D/1DArray/2D/2DArray/2DMS/Cube/CubeArray/3D/2DMSArray), 1..16 mip levels, all view levels/layers, cube faces as layers, 3D depth/slice stride, MS fused sample plane, sampled linear formats listed below, ASTC 2D LDR/sRGB/HDR, ETC2/EAC, depth16/depth32 sampled. TextureBuffer (9), combined depth/stencil, PVRTC/BC/YUV and sRGB custom-border cases refuse explicitly. Sampler covers min/mag/mip, wrap incl border/zero/mirror, normalized, compare, anisotropy 1..16, finite LOD min/max/bias=0, seamless, magnify cutoff, sRGB override, border presets. Arch9 has no reduction field: average only; min/max refuse -15. Border color is float RGBA for float/normalized formats; integer formats refuse custom border unless the exact raw words are supplied.

Format words are derived from pinned Mesa pan_format.c (MALI_PACK_FMT: order | mali<<12 | srgb<<20, FMTC interchange formats), not from the old donor table. Planes: layer outer/level inner for 2D arrays/cubes, one plane for 3D, Generic Plane for uncompressed and ASTC 2D Plane for ASTC; slice_stride is 3D z stride or MS sample stride, layer_stride is array stride; size is the per-entry level extent. Levels descriptor field=level_count-1, max LOD=(level_count-1)<<8, sample count log2, array size layers/(cube?6:1), depth for 3D.

Render consumption proposal (root/render ACK needed):
  struct n2r_texture_binding { uint32_t binding, kind; struct n2t_texture_layout layout; struct n2t_sampler_state sampler; };
  struct n2r_texture_resources { uint32_t version, count; const struct n2r_texture_binding *bindings; };
  int n2r_build_v2(uint8_t out[N2R_BLOCK], const shader*, const request*, const addresses*, const struct n2r_texture_resources*, uint16_t index, uint64_t next);
Existing n2r_build and its default descriptor bytes stay unchanged (v2 absent == old single-plane path). V2 uses a->binding[binding] as the base VA and per-level offsets inside it; <=256 total plane entries in the free 8 KiB of the command block. Render encoder owner must fill the sidecar from TeguTexture mip[]/kind/depth/samples and the full TeguSampler state. Compute owner can call n2t_layout_pack_sampled/storage directly with its own resolved addresses; no bridge edit by me.

Sampler owner change in my file: sampler_state.inc will retain exact filter/wrap/compare/aniso/LOD/border/reduction state and expose a bounded packing helper, instead of reducing to two linear bits. It refuses unsupported states rather than substituting nearest/clamp. Render must switch its sampler word consumer only together with a v2 sidecar; old word path remains for compatibility.

Status: proposal posted before production edits, as required. Please ACK/replace exact names and tell me if root grants the two new files. I will post ready-for-review with host commands/results and unresolved gaps.
