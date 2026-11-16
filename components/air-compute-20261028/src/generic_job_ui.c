#include "generic_job_ui.h"
#include "compositor_load_assets_v6.h"
#include "compositor_load_bundle_v6.h"
#include "compositor_load_mapping.h"
#include "compositor_v7_scratch_layout.h"
#include "compositor_vertex_state.h"
#include "fixed_ui_render_state.h"
#include "fixed_ui_resources.h"
#include "fixed_ui_texture_render_state.h"
#include "fixed_ui_texture_resources.h"
#include "tegu_memory.h"

void *malloc(size_t);
void free(void *);

/* The XNU UI1/UI2 clients used scratch offset 0x8000 as builder workspace;
 * keeping it reproduces their scratch bytes exactly. */
#define GJ_WORKSPACE 0x8000u

static uint64_t gj_page(uint64_t offset) { return offset & ~(uint64_t)(FS_PAGE_SIZE - 1u); }

/* Builders work on a TGJ2-sized request (identical header); r->version picks
 * the ABI whose limits apply.  TGJ1 results are copied out with gj_to_v1. */
typedef struct tegu_generic_job2_request gj_req;
static uint32_t gj_span_limit(const gj_req *r)
{
    return r->version == TEGU_GENERIC_JOB2_VERSION ? TEGU_GENERIC_JOB2_MAX_SPANS : TEGU_GENERIC_JOB_MAX_SPANS;
}
static uint32_t gj_command_limit(const gj_req *r)
{
    return r->version == TEGU_GENERIC_JOB2_VERSION ? TEGU_GENERIC_JOB2_MAX_COMMANDS : TEGU_GENERIC_JOB_MAX_COMMANDS;
}
static int gj_to_v1(const gj_req *r, struct tegu_generic_job_request *out)
{
    if (r->version != TEGU_GENERIC_JOB_VERSION || r->span_count > TEGU_GENERIC_JOB_MAX_SPANS ||
        r->command_count > TEGU_GENERIC_JOB_MAX_COMMANDS) return -1;
    struct tegu_generic_job_request v;
    memset(&v, 0, sizeof(v));
    v.version = r->version; v.flags = r->flags; v.width = r->width; v.height = r->height;
    v.row_stride = r->row_stride; v.span_count = r->span_count; v.command_count = r->command_count;
    v.reserved = r->reserved; v.target_token = r->target_token; v.target_offset = r->target_offset;
    v.target_length = r->target_length; v.target_bytes = r->target_bytes; v.scratch_token = r->scratch_token;
    v.scratch_offset = r->scratch_offset; v.exec_mask = r->exec_mask;
    memcpy(v.spans, r->spans, sizeof(v.spans));
    memcpy(v.commands, r->commands, sizeof(v.commands));
    *out = v;
    return 0;
}
static int gj_plan(const gj_req *r, struct tegu_generic_job_plan *p)
{
    if (r->version == TEGU_GENERIC_JOB2_VERSION) return tegu_generic_job2_plan(r, p);
    struct tegu_generic_job_request v;
    return gj_to_v1(r, &v) ? -1 : tegu_generic_job_plan(&v, p);
}

static int gj_span(gj_req *r, uint64_t token, uint64_t offset, uint64_t bytes)
{
    uint64_t page = gj_page(offset), length = (offset - page) + bytes;
    if (r->span_count >= gj_span_limit(r) || !token || !bytes || length > UINT32_MAX) return -1;
    r->spans[r->span_count++] = (struct tegu_generic_job_span){token, page, length};
    return 0;
}

static uint64_t gj_va(const gj_req *r, const struct tegu_generic_job_plan *p,
    unsigned span, uint64_t offset)
{
    return p->span_va[span] + (offset - r->spans[span].offset);
}

/* TGJ1 places shader code by region capacity rather than by the exact R80/R85
 * code sizes the selector-16 layouts pin; regions and exec masks are the
 * qualified UI2 layout's. */
static int gj_scratch_layout(uint32_t vs_bytes, uint32_t fs_bytes, int load, struct tegu_v7_scratch_layout *out)
{
    struct tegu_v7_scratch_layout base;
    if (!vs_bytes || !fs_bytes || (vs_bytes & 127u) || (fs_bytes & 127u) ||
        !tegu_ui2_scratch_layout_build(4096u, 1152u, load, &base)) return 0;
    /* The v7 fragment region has seven executable pages (R127: a 763x413
     * UI2 draw's fragment stage exceeded the single page used before). */
    base.fs_capacity = TEGU_V7_FS_PAGES * TEGU_V7_PAGE;
    if (vs_bytes > base.vs_capacity || fs_bytes > base.fs_capacity) return 0;
    *out = base;
    return 1;
}

static int gj_begin(gj_req *r, uint32_t version, const struct tegu_generic_job_images *images,
    uint32_t width, uint32_t height, uint32_t row, uint64_t target_token, uint64_t target_offset,
    uint64_t target_length, uint64_t target_bytes)
{
    if (!images || !images->scratch || !images->scratch_token || (target_offset & (FS_PAGE_SIZE - 1u)))
        return -1;
    memset(r, 0, sizeof(*r));
    r->version = version;
    r->flags = (width > 16 || height > 16) ? TEGU_GENERIC_JOB_HEAP2M : 0;
    r->width = width; r->height = height; r->row_stride = row;
    r->target_token = target_token; r->target_offset = target_offset;
    r->target_length = target_length; r->target_bytes = target_bytes;
    r->scratch_token = images->scratch_token;
    r->command_count = 1; /* placeholder NOP while planning */
    return 0;
}

/* LOAD preload bundle: the preload pages, the LOAD frame descriptor and the
 * command stream with its clear marker replaced by the previous-job wait. */
static int gj_load_apply(const struct tegu_generic_job_plan *p,
    const struct tegu_generic_job_images *images, const struct tegu_v7_scratch_layout *sl,
    struct tegu_render_state *state, const struct tegu_render_addresses *addresses,
    const struct tegu_render_target_layout *target, unsigned load_span,
    size_t vertex_bytes, size_t fragment_bytes)
{
    uint8_t *memory = images->scratch;
    {
        struct tegu_compositor_load_bundle_v6 *bundle = malloc(sizeof(*bundle));
        if (!bundle) return -1;
        memset(bundle, 0, sizeof(*bundle));
        struct tegu_load_mapping map;
        memset(&map, 0, sizeof(map));
        map.resource_va = p->span_va[load_span];
        map.dcd_va = map.resource_va + FS_PAGE_SIZE;
        map.shader_va = FS_APERTURE_VA + 14u * FS_PAGE_SIZE;
        map.target_pages = p->layout.target_pages; map.total_pages = p->layout.total_pages;
        map.page_role[0] = TEGU_LOAD_MAP_RO_NX; map.page_role[1] = TEGU_LOAD_MAP_RO_NX;
        map.page_role[2] = TEGU_LOAD_MAP_RO_X;
        struct tegu_compositor_load_asset_scratch_v6 scratch;
        struct tegu_compositor_load_assets_v6 assets;
        memset(&scratch, 0, sizeof(scratch)); memset(&assets, 0, sizeof(assets));
        memset(images->load, 0, TEGU_GENERIC_JOB_LOAD_BYTES);
        int failed = tegu_compositor_load_assets_build_v6(target, p->target_va, images->load,
                images->load + FS_PAGE_SIZE, memory + sl->load_shader_offset, &scratch, &assets) ||
            tegu_compositor_load_bundle_build_v6(state, addresses, target, &map, &assets,
                vertex_bytes, fragment_bytes, bundle);
        if (!failed) {
            memcpy(images->load, bundle->resource_page, FS_PAGE_SIZE);
            memcpy(images->load + FS_PAGE_SIZE, bundle->dcd_page, FS_PAGE_SIZE);
            memcpy(memory + sl->load_shader_offset, bundle->shader_page, FS_PAGE_SIZE);
            memcpy(state, &bundle->draw, sizeof(*state));
            /* R170: the RT keeps the CLEAR state's Clean Tile Write Enable
             * (word 1 bit 31) while the preload runs in INTERSECT mode, so
             * every tile no primitive of this job touched skipped the preload
             * and was still written back with the clear value 0: each LOAD
             * job erased the target outside its own draw ("one element at a
             * time"). Mesa writes clean tiles only for cleared RTs
             * (pan_get_clean_tile_info) and never pairs them with INTERSECT
             * (pan_fix_frame_shader_mode); a LOAD job leaves them in memory. */
            state->descriptors[TEGU_GENERIC_JOB_RT_OFFSET + 7] &= 0x7fu;
        }
        free(bundle);
        if (failed) return -1;
    }
    return 0;
}

/* LOAD preload bundle, descriptor/tiler pages and the final command stream. */
static int gj_finish(gj_req *r, const struct tegu_generic_job_plan *p,
    const struct tegu_generic_job_images *images, const struct tegu_v7_scratch_layout *sl,
    struct tegu_render_state *state, const struct tegu_render_addresses *addresses,
    const struct tegu_render_target_layout *target, int load, unsigned load_span,
    size_t vertex_bytes, size_t fragment_bytes)
{
    uint8_t *memory = images->scratch;
    if (load && gj_load_apply(p, images, sl, state, addresses, target, load_span, vertex_bytes, fragment_bytes))
        return -1;
    if (state->command_count > sizeof(state->commands) / sizeof(state->commands[0])) return -1;
    memcpy(memory, state->descriptors, sizeof(state->descriptors));
    memcpy(memory + 0x1000, state->tiler, sizeof(state->tiler));
    memcpy(r->commands, state->commands, state->command_count * sizeof(r->commands[0]));
    r->command_count = (uint32_t)state->command_count;
    struct tegu_generic_job_plan check;
    return gj_plan(r, &check) || memcmp(&check, p, sizeof(check)) ? -1 : 0;
}

static struct tegu_render_addresses gj_addresses(const gj_req *r,
    const struct tegu_generic_job_plan *p, const struct tegu_v7_scratch_layout *sl, uint32_t clear)
{
    int heap2m = (r->flags & TEGU_GENERIC_JOB_HEAP2M) != 0;
    return (struct tegu_render_addresses){.descriptors = FS_APERTURE_VA, .tiler = FS_APERTURE_VA + 0x1000,
        .geometry = FS_APERTURE_VA + sl->geometry_offset,
        .chunk = heap2m ? p->heap_va : FS_APERTURE_VA + sl->chunk_offset,
        .target = p->target_va, .vertex_code = FS_APERTURE_VA + sl->vs_offset,
        .fragment_code = FS_APERTURE_VA + sl->fs_offset, .clear_rgba = clear,
        .chunk_bytes = heap2m ? 0x200000u : 0, .hierarchy_mask = heap2m ? 0x82u : 1u};
}

int tegu_ui2_generic_job_build(const struct tegu_ui2_info *info, const void *package,
    size_t package_bytes, const struct tegu_ui2_bind_request *request,
    const struct tegu_generic_job_images *images, struct tegu_generic_job_request *out)
{
    struct tegu_ui2_binding b;
    if (!info || !package || !request || !out || tegu_ui2_bind(info, request, &b) ||
        b.store_action != TEGU_UI2_STORE || b.target_token != b.target_object_id) return -10;
    const int load = b.load_action == TEGU_UI2_LOAD;
    if (load && (!images || !images->load || !images->load_token)) return -11;
    gj_req r;
    if (gj_begin(&r, TEGU_GENERIC_JOB_VERSION, images, b.width, b.height, b.target_row_bytes, b.target_token, b.target_offset,
            request->target.allocation.length, b.target_bytes) ||
        gj_span(&r, b.vertex_token, b.vertex_offset, b.vertex_bytes)) return -12;
    for (unsigned i = 0; i < 4; i++)
        if (gj_span(&r, b.resources[i].token, b.resources[i].descriptor_offset, b.resources[i].descriptor_bytes))
            return -13;
    if (gj_span(&r, b.texture.token, b.texture.offset, b.texture.bytes) ||
        (load && gj_span(&r, images->load_token, 0, TEGU_GENERIC_JOB_LOAD_BYTES))) return -14;
    struct tegu_v7_scratch_layout sl;
    if (!gj_scratch_layout(info->stages[0].bytes, info->stages[1].bytes, load, &sl)) return -15;
    r.exec_mask = sl.exec_mask;
    struct tegu_generic_job_plan p;
    if (gj_plan(&r, &p)) return -16; /* spans/resource pages */
    uint8_t *memory = images->scratch;
    memset(memory, 0, TEGU_GENERIC_JOB_SCRATCH_BYTES);
    if (!tegu_v7_scratch_prepare(memory, TEGU_GENERIC_JOB_SCRATCH_BYTES) ||
        tegu_ui2_copy_code(info, package, package_bytes, memory + sl.vs_offset, sl.vs_capacity,
            memory + sl.fs_offset, sl.fs_capacity)) return -17;
    struct tegu_vertex_attribute attrs[3];
    for (unsigned i = 0; i < 3; i++)
        attrs[i] = (struct tegu_vertex_attribute){info->attributes[i].location, info->attributes[i].buffer_index,
            info->attributes[i].format, info->attributes[i].offset, info->attributes[i].stride,
            info->attributes[i].input_rate, info->attributes[i].divisor};
    struct tegu_vertex_buffer vb = {gj_va(&r, &p, 0, b.vertex_offset), b.vertex_bytes};
    struct tegu_vertex_draw draw = {b.vertex_start, b.vertex_count, b.instance_count, b.base_instance};
    struct tegu_compositor_vertex_state *vs = (struct tegu_compositor_vertex_state *)(memory + GJ_WORKSPACE);
    if (sizeof(*vs) > 0x4000 || tegu_compositor_vertex_build(attrs, 3, &vb, &draw, FS_APERTURE_VA + 0x6000, vs))
        return -18;
    memcpy(memory + 0x6000, vs->driver, sizeof(vs->driver));
    struct tegu_fixed_ui_texture_resource_input ri;
    memset(&ri, 0, sizeof(ri));
    ri.version = TEGU_FIXED_UI_TEXTURE_STATE_VERSION; ri.binding = &b;
    ri.table0 = (struct tegu_fixed_ui_texture_span){FS_APERTURE_VA + 0x6000, TEGU_COMPOSITOR_DRIVER_BYTES};
    ri.resource_table = (struct tegu_fixed_ui_texture_span){FS_APERTURE_VA + 0x7000, TEGU_FIXED_UI_TEXTURE_TABLE_BYTES};
    ri.descriptors = (struct tegu_fixed_ui_texture_span){FS_APERTURE_VA + 0x7100, TEGU_FIXED_UI_TEXTURE_DESCRIPTOR_BYTES};
    ri.plane = (struct tegu_fixed_ui_texture_span){FS_APERTURE_VA + 0x7200, TEGU_FIXED_UI_TEXTURE_PLANE_BYTES};
    for (unsigned i = 0; i < 4; i++)
        ri.buffers[i] = (struct tegu_fixed_ui_texture_allocation_span){b.resources[i].token, b.resources[i].object_id,
            gj_va(&r, &p, 1 + i, b.resources[i].descriptor_offset) - b.resources[i].descriptor_offset,
            request->resources[i].allocation.length};
    ri.texture = (struct tegu_fixed_ui_texture_allocation_span){b.texture.token, b.texture.object_id,
        gj_va(&r, &p, 5, b.texture.offset) - b.texture.offset, request->texture.allocation.length};
    struct tegu_fixed_ui_texture_resource_state rs;
    if (tegu_fixed_ui_texture_resource_state_build(&ri, &rs)) return -19;
    memcpy(memory + 0x7000, rs.resource_table, sizeof(rs.resource_table));
    /* Sampler descriptor (index 6): clear Mali 'Minify/Magnify nearest' for linear filters. */
    if (images->sampler_linear & ~3u) return -20;
    uint32_t word = (uint32_t)rs.descriptors[192] | (uint32_t)rs.descriptors[193] << 8 |
        (uint32_t)rs.descriptors[194] << 16 | (uint32_t)rs.descriptors[195] << 24;
    word &= ~(((images->sampler_linear & 1u) ? 1u << 27 : 0u) | ((images->sampler_linear & 2u) ? 1u << 28 : 0u));
    for (unsigned b = 0; b < 4; b++) rs.descriptors[192 + b] = (uint8_t)(word >> (8 * b));
    memcpy(memory + 0x7100, rs.descriptors, sizeof(rs.descriptors));
    memcpy(memory + 0x7200, rs.plane, sizeof(rs.plane));
    struct tegu_render_addresses addresses = gj_addresses(&r, &p, &sl, b.clear_color[0]);
    struct tegu_render_target_layout target = {TEGU_RENDER_TARGET_LAYOUT_VERSION, b.width, b.height,
        b.target_row_bytes, p.layout.target_bytes};
    struct tegu_render_state *state = (struct tegu_render_state *)(memory + GJ_WORKSPACE);
    if (sizeof(*state) > 0x4000 || tegu_fixed_ui_texture_render_build(info, &b, &addresses,
            rs.resource_table_handle, &target, state) ||
        gj_finish(&r, &p, images, &sl, state, &addresses, &target, load, 6,
            info->stages[0].bytes, info->stages[1].bytes)) return -21;
    return gj_to_v1(&r, out);
}

int tegu_ui1_generic_job_build(const struct tegu_ui1_info *info, const void *package,
    size_t package_bytes, const struct tegu_ui1_bind_request *request,
    const struct tegu_generic_job_images *images, struct tegu_generic_job_request *out)
{
    struct tegu_ui1_binding b;
    if (!info || !package || !request || !out || tegu_ui1_bind(info, request, &b) ||
        b.store_action != TEGU_UI1_STORE || b.target_token != b.target_object_id) return -1;
    const int load = b.load_action == TEGU_UI1_LOAD;
    if (load && (!images || !images->load || !images->load_token)) return -1;
    gj_req r;
    if (gj_begin(&r, TEGU_GENERIC_JOB_VERSION, images, b.width, b.height, b.target_row_bytes, b.target_token, b.target_offset,
            request->target.allocation.length, b.target_bytes) ||
        gj_span(&r, b.vertex_token, b.vertex_offset, b.vertex_bytes)) return -1;
    /* As in XNU, two descriptor windows in one allocation share one span. */
    unsigned resource_span[2];
    if (b.resources[0].token == b.resources[1].token) {
        uint64_t begin0 = b.resources[0].descriptor_offset, begin1 = b.resources[1].descriptor_offset;
        uint64_t begin = begin0 < begin1 ? begin0 : begin1;
        uint64_t end0 = begin0 + b.resources[0].descriptor_bytes, end1 = begin1 + b.resources[1].descriptor_bytes;
        resource_span[0] = resource_span[1] = r.span_count;
        if (gj_span(&r, b.resources[0].token, begin, (end0 > end1 ? end0 : end1) - begin)) return -1;
    } else for (unsigned i = 0; i < 2; i++) {
        resource_span[i] = r.span_count;
        if (gj_span(&r, b.resources[i].token, b.resources[i].descriptor_offset, b.resources[i].descriptor_bytes))
            return -1;
    }
    unsigned load_span = r.span_count;
    if (load && gj_span(&r, images->load_token, 0, TEGU_GENERIC_JOB_LOAD_BYTES)) return -1;
    struct tegu_v7_scratch_layout sl;
    if (!gj_scratch_layout(info->stages[0].bytes, info->stages[1].bytes, load, &sl)) return -1;
    r.exec_mask = sl.exec_mask;
    struct tegu_generic_job_plan p;
    if (gj_plan(&r, &p)) return -1;
    uint8_t *memory = images->scratch;
    memset(memory, 0, TEGU_GENERIC_JOB_SCRATCH_BYTES);
    if (!tegu_v7_scratch_prepare(memory, TEGU_GENERIC_JOB_SCRATCH_BYTES) ||
        tegu_ui1_copy_code(info, package, package_bytes, memory + sl.vs_offset, sl.vs_capacity,
            memory + sl.fs_offset, sl.fs_capacity)) return -1;
    struct tegu_vertex_attribute attrs[2];
    for (unsigned i = 0; i < 2; i++)
        attrs[i] = (struct tegu_vertex_attribute){info->attributes[i].location, info->attributes[i].buffer_index,
            info->attributes[i].format, info->attributes[i].offset, info->attributes[i].stride,
            info->attributes[i].input_rate, info->attributes[i].divisor};
    struct tegu_vertex_buffer vb = {gj_va(&r, &p, 0, b.vertex_offset), b.vertex_bytes};
    struct tegu_vertex_draw draw = {b.vertex_start, b.vertex_count, b.instance_count, b.base_instance};
    struct tegu_compositor_vertex_state *vs = (struct tegu_compositor_vertex_state *)(memory + GJ_WORKSPACE);
    if (sizeof(*vs) > 0x4000 || tegu_compositor_vertex_build(attrs, 2, &vb, &draw, FS_APERTURE_VA + 0x6000, vs))
        return -1;
    memcpy(memory + 0x6000, vs->driver, sizeof(vs->driver));
    struct tegu_fixed_ui_resource_input ri;
    memset(&ri, 0, sizeof(ri));
    ri.version = TEGU_FIXED_UI_STATE_VERSION; ri.binding = &b;
    ri.table0 = (struct tegu_fixed_ui_span){FS_APERTURE_VA + 0x6000, TEGU_COMPOSITOR_DRIVER_BYTES};
    ri.resource_table = (struct tegu_fixed_ui_span){FS_APERTURE_VA + 0x7000, TEGU_FIXED_UI_TABLE_BYTES};
    ri.descriptors = (struct tegu_fixed_ui_span){FS_APERTURE_VA + 0x7100, TEGU_FIXED_UI_DESCRIPTOR_BYTES};
    ri.vertex_buffer = (struct tegu_fixed_ui_allocation_span){b.resources[0].token, b.resources[0].object_id,
        gj_va(&r, &p, resource_span[0], b.resources[0].descriptor_offset) - b.resources[0].descriptor_offset,
        request->resources[0].allocation.length};
    ri.fragment_buffer = (struct tegu_fixed_ui_allocation_span){b.resources[1].token, b.resources[1].object_id,
        gj_va(&r, &p, resource_span[1], b.resources[1].descriptor_offset) - b.resources[1].descriptor_offset,
        request->resources[1].allocation.length};
    struct tegu_fixed_ui_resource_state rs;
    if (tegu_fixed_ui_resource_state_build(&ri, &rs)) return -1;
    memcpy(memory + 0x7000, rs.resource_table, sizeof(rs.resource_table));
    memcpy(memory + 0x7100, rs.descriptors, sizeof(rs.descriptors));
    struct tegu_render_addresses addresses = gj_addresses(&r, &p, &sl, b.clear_color[0]);
    struct tegu_render_target_layout target = {TEGU_RENDER_TARGET_LAYOUT_VERSION, b.width, b.height,
        b.target_row_bytes, p.layout.target_bytes};
    struct tegu_render_state *state = (struct tegu_render_state *)(memory + GJ_WORKSPACE);
    if (sizeof(*state) > 0x4000 || tegu_fixed_ui_render_build(info, &b, &addresses,
            rs.resource_table_handle, &target, state) ||
        gj_finish(&r, &p, images, &sl, state, &addresses, &target, load, load_span,
            info->stages[0].bytes, info->stages[1].bytes)) return -1;
    return gj_to_v1(&r, out);
}

/* ---- Reflection-driven generic draw --------------------------------------
 * Scratch page 7 holds the resource tables: the SRT at 0x7000 (table0 vertex
 * attributes at 0x6000, table1 at 0x7100, table2 at 0x7300), texture planes
 * at 0x7500 and the padded constant blob at 0x7700. */
#define GD_SRT 0x7000u
#define GD_TABLE1 0x7100u
#define GD_TABLE2 0x7300u
#define GD_PLANES 0x7500u
#define GD_CONSTANT 0x7700u
#define GD_ZERO 0x7900u /* 256 zero bytes: neutral textures and buffers */
#define GD_ENTRIES 16u

static void gd_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void gd_put64(uint8_t *p, uint64_t v) { gd_put32(p, (uint32_t)v); gd_put32(p + 4, (uint32_t)(v >> 32)); }

/* Several Metal bindings usually window one allocation: map it once. */
static int gd_span(gj_req *r, uint64_t token, uint64_t offset, uint64_t bytes)
{
    if (!token || !bytes || offset > UINT64_MAX - bytes) return -1;
    uint64_t begin = gj_page(offset), end = offset + bytes;
    for (uint32_t i = 0; i < r->span_count; i++) {
        struct tegu_generic_job_span *s = &r->spans[i];
        if (s->token != token) continue;
        uint64_t b = s->offset < begin ? s->offset : begin, e = s->offset + s->length > end ? s->offset + s->length : end;
        if (e - b > UINT32_MAX) return -1;
        s->offset = b; s->length = e - b;
        return 0;
    }
    return gj_span(r, token, offset, bytes);
}

static uint64_t gd_va(const gj_req *r, const struct tegu_generic_job_plan *p,
    uint64_t token, uint64_t offset)
{
    for (uint32_t i = 0; i < r->span_count; i++)
        if (r->spans[i].token == token && offset >= r->spans[i].offset &&
            offset - r->spans[i].offset < r->spans[i].length) return gj_va(r, p, i, offset);
    return 0;
}

static void gd_resource(uint8_t *p, uint64_t address, uint64_t bytes)
{
    gd_put64(p, (address & ((1ull << 56) - 1)) | (1ull << 56)); gd_put64(p + 8, bytes);
}
static void gd_buffer(uint8_t *p, uint64_t address, uint32_t bytes)
{
    memset(p, 0, 32); gd_put32(p, 9u | (1u << 4)); gd_put32(p + 4, bytes); gd_put64(p + 8, address);
}
/* Linear 2D texture and its plane, as fixed_ui_texture_resources.c.  Mali
 * format words (Mesa MALI_PACK_FMT): BGRA8 = RGBA8_UNORM/BGRA order,
 * R8 = R8_UNORM/R001 (RGB1). */
/* Metal format -> Mali format word (Mesa MALI_PACK_FMT), texel size, plane
 * clump format and descriptor swizzle.  A8 samples R8 with (0,0,0,R). */
static uint32_t gd_format(uint32_t metal)
{
    switch (metal) {
    case 1: case 10: return (163u << 12) | 16u;   /* R8_UNORM, R001 */
    case 30: return (171u << 12) | 16u;           /* RG8_UNORM, RG01 */
    case 115: return 223u << 12;                  /* RGBA16F, RGBA */
    case 70: return 187u << 12;                   /* RGBA8_UNORM, RGBA */
    default: return (187u << 12) | 4u;            /* BGRA8 */
    }
}
static uint32_t gd_bytes_per_texel(uint32_t metal)
{
    return metal == 1 || metal == 10 ? 1u : metal == 30 ? 2u : metal == 115 ? 8u : 4u;
}
static uint32_t gd_clump(uint32_t metal)
{
    uint32_t bytes = gd_bytes_per_texel(metal);
    return bytes == 1 ? 0u : bytes == 2 ? 1u : bytes == 8 ? 3u : 2u; /* RAW8/16/64/32 */
}
static uint32_t gd_swizzle(uint32_t metal) { return metal == 1 ? 0x124u : 0x688u; }
/* Metal ASTC 2D formats: 186-200 sRGB, 204-218 LDR (block widths/heights). */
static int gd_astc(uint32_t metal, uint32_t *bw, uint32_t *bh)
{
    static const uint8_t dims[15][2] = {{4,4},{5,4},{5,5},{6,5},{6,6},{0,0},{8,5},{8,6},{8,8},
        {10,5},{10,6},{10,8},{10,10},{12,10},{12,12}};
    uint32_t i = metal >= 186 && metal <= 200 ? metal - 186 : metal >= 204 && metal <= 218 ? metal - 204 : 99;
    if (i >= 15 || !dims[i][0]) return 0;
    *bw = dims[i][0]; *bh = dims[i][1];
    return 1;
}
/* Mali v10 ASTC 2D Dimension: 4->0, 5->1, 6->2, 8->4, 10->6, 12->7. */
static uint32_t gd_astc_dim(uint32_t d) { return d == 4 ? 0 : d == 5 ? 1 : d == 6 ? 2 : d == 8 ? 4 : d == 10 ? 6 : 7; }
static void gd_texture(uint8_t *p, uint8_t *plane, uint64_t plane_va, uint64_t texels,
    uint32_t width, uint32_t height, uint32_t row_bytes, uint32_t metal_format)
{
    uint32_t bw, bh;
    if (gd_astc(metal_format, &bw, &bh)) {
        /* Valhall: the texture format is the decode interchange format (RGBA8
         * sRGB, else RGBA16F); the ASTC 2D plane carries the block size. */
        int srgb = metal_format <= 200;
        uint32_t fmt = srgb ? (187u << 12) | (1u << 20) : 223u << 12;
        memset(p, 0, 32);
        gd_put32(p, 2u | (2u << 4) | (fmt << 10));
        gd_put32(p + 4, (width - 1u) | ((height - 1u) << 16)); gd_put32(p + 8, 0x688u); gd_put64(p + 16, plane_va);
        memset(plane, 0, 32);
        uint32_t rows = (height + bh - 1u) / bh;
        gd_put32(plane, 10u | (4u << 4) | (2u << 8) | ((srgb ? 0u : 1u) << 25) |
            (gd_astc_dim(bw) << 26) | (gd_astc_dim(bh) << 29));
        gd_put32(plane + 4, row_bytes * rows);
        gd_put64(plane + 8, texels); gd_put32(plane + 16, row_bytes); gd_put32(plane + 28, (width - 1u) | ((height - 1u) << 16));
        return;
    }
    memset(p, 0, 32);
    gd_put32(p, 2u | (2u << 4) | (gd_format(metal_format) << 10));
    gd_put32(p + 4, (width - 1u) | ((height - 1u) << 16)); gd_put32(p + 8, gd_swizzle(metal_format)); gd_put64(p + 16, plane_va);
    memset(plane, 0, 32);
    /* Generic plane: clump format is the texel size (RAW8 0, RAW32 2). */
    uint32_t clump = gd_clump(metal_format);
    gd_put32(plane, 10u | (1u << 4) | (2u << 8) | (clump << 24)); gd_put32(plane + 4, row_bytes * height);
    gd_put64(plane + 8, texels); gd_put32(plane + 16, row_bytes); gd_put32(plane + 28, (width - 1u) | ((height - 1u) << 16));
}
static int gd_sampler(uint8_t *p, uint32_t word)
{
    uint32_t wrap[3];
    if (word & ~0xffffu) return -1;
    for (unsigned i = 0; i < 3; i++) {
        uint32_t w = (word >> (4 + 4 * i)) & 15u;
        if (w && w != 8 && w != 9 && w != 11 && w != 12 && w != 13) return -1;
        wrap[i] = w ? w : 9u;
    }
    memset(p, 0, 32);
    /* Type, wraps R/T/S, round-to-even, seamless, normalized, clamp array
     * index; nearest unless the word asks for linear. */
    gd_put32(p, 1u | (wrap[2] << 8) | (wrap[1] << 12) | (wrap[0] << 16) | (1u << 21) | (1u << 23) |
        (1u << 25) | (1u << 26) | ((word & 1u) ? 0 : 1u << 27) | ((word & 2u) ? 0 : 1u << 28));
    return 0;
}
static uint32_t gd_spd(uint32_t registers, uint32_t low)
{
    return ((registers <= 32u ? 2u : 0u) << 30) | low; /* pan_register_allocation */
}


/* A command stream being built.  Counts past its capacity, so a batch
 * builder sees the overflow and refuses. */
struct gd_cmds { uint64_t *v; uint32_t cap, n; };
static void gd_emit(struct gd_cmds *s, uint64_t v)
{
    if (s->n < s->cap) s->v[s->n] = v;
    s->n++;
}
static void gd_move32(struct gd_cmds *s, unsigned r, uint32_t v) { gd_emit(s, (2ULL << 56) | ((uint64_t)r << 48) | v); }
static void gd_move64(struct gd_cmds *s, unsigned r, uint64_t v)
{
    if (v < (1ULL << 48)) gd_emit(s, (1ULL << 56) | ((uint64_t)r << 48) | v);
    else { gd_move32(s, r, (uint32_t)v); gd_move32(s, r + 1, (uint32_t)(v >> 32)); }
}
static void gd_wait(struct gd_cmds *s) { gd_emit(s, (3ULL << 56) | (0xffULL << 16)); }
static uint32_t gd_half(uint32_t n)
{
    unsigned e = 31u - (unsigned)__builtin_clz(n);
    return ((126u + e) << 23) | ((n - (1u << e)) << (23u - e));
}
/* Register shadow of a batched job: a later draw re-emits only registers
 * whose value changes (panvk updates dirty draw state the same way; RUN_IDVS
 * consumes the registers when it is issued).  NULL emits every move. */
struct gd_regs { uint32_t value[96]; uint8_t known[96]; };
static void gd_set32(struct gd_cmds *s, struct gd_regs *g, unsigned r, uint32_t v)
{
    if (g && g->known[r] && g->value[r] == v) return;
    gd_move32(s, r, v);
    if (g) { g->value[r] = v; g->known[r] = 1; }
}
static void gd_set64(struct gd_cmds *s, struct gd_regs *g, unsigned r, uint64_t v)
{
    if (g && g->known[r] && g->known[r + 1] && g->value[r] == (uint32_t)v && g->value[r + 1] == (uint32_t)(v >> 32)) return;
    gd_move64(s, r, v);
    if (g) { g->value[r] = (uint32_t)v; g->value[r + 1] = (uint32_t)(v >> 32); g->known[r] = g->known[r + 1] = 1; }
}

/* Where one draw's descriptors and resource tables live, as offsets from a
 * base.  A single-draw job keeps them in the descriptor page and on scratch
 * pages 6-7; each draw of a batched job has its own 4 KiB block. */
struct gd_slots {
    uint32_t blend, fragment_spd, position_spd, varying_spd, vertex_fau, fragment_fau;
    uint32_t table0, srt, table1, table2, planes, constant, zero;
};
static const struct gd_slots gd_single = {0x440, 0x480, 0x600, 0x700, 0xa00, 0xb00,
    0x6000, GD_SRT, GD_TABLE1, GD_TABLE2, GD_PLANES, GD_CONSTANT, GD_ZERO};
static const struct gd_slots gd_block = {0x40, 0x80, 0xc0, 0x100, 0x200, 0x300,
    0x400, 0x000, 0x700, 0x900, 0xb00, 0xd00, 0xf00};
/* GPU VAs one draw's commands name. */
struct gd_draw_vas { uint64_t table, blend, fragment_spd, position_spd, varying_spd, vertex_fau, fragment_fau; };
static struct gd_draw_vas gd_vas(uint64_t va, const struct gd_slots *o)
{
    return (struct gd_draw_vas){(va + o->srt) | 4u, va + o->blend, va + o->fragment_spd, va + o->position_spd,
        va + o->varying_spd, va + o->vertex_fau, va + o->fragment_fau};
}

/* Pass state shared by every draw: framebuffer, RT, depth/stencil, thread
 * storage and the tiler context. */
static void gd_frame(struct tegu_render_state *s, const struct tegu_render_addresses *a, const struct tegu_render_target_layout *l)
{
    uint8_t *d = s->descriptors, *t = s->tiler;
    uint64_t va = a->descriptors, surface = (uint64_t)l->row_stride * l->height;
    uint32_t chunk = a->chunk_bytes ? a->chunk_bytes : 131072u, hm = a->hierarchy_mask ? a->hierarchy_mask : 1u;
    uint32_t extent = (l->width - 1u) | ((l->height - 1u) << 16);
    gd_put64(d + 16, va + 0x100); gd_put32(d + 32, extent); gd_put32(d + 40, extent);
    gd_put32(d + 44, (1u << 24) | (8u << 9) | (2u << 6)); gd_put32(d + 48, 1u << 16); gd_put64(d + 56, a->tiler);
    gd_put32(d + 128, 1u << 26); gd_put32(d + 132, 1u | (19u << 3) | (2u << 8) | (0x60au << 16) | (1u << 31));
    gd_put64(d + 160, a->target); gd_put32(d + 168, l->row_stride); gd_put32(d + 172, (uint32_t)surface);
    for (unsigned x = 0; x < 4; x++) gd_put32(d + 176 + x * 4, a->clear_rgba);
    gd_put32(d + 0x100, 0x00800080); gd_put32(d + 0x180, 0x00800080);
    gd_put32(d + 0x400, 7u | (7u << 16)); gd_put32(d + 0x410, (7u << 29) | (1u << 22) | (1u << 23) | (2u << 25));
    gd_put32(d + 0x4c4, 31);
    gd_put32(t + 8, hm); gd_put32(t + 12, extent); gd_put64(t + 24, a->tiler + 0x80); gd_put64(t + 32, a->geometry | 16u);
    gd_put32(t + 0x80, 0x29); gd_put32(t + 0x84, chunk); gd_put64(t + 0x88, a->chunk); gd_put64(t + 0x90, a->chunk + 64);
    gd_put64(t + 0x98, a->chunk + chunk);
}
/* One draw's blend descriptor, shader programs and FAU images (viewport
 * system values, blend constants, push constants at word 32). */
static void gd_draw_descriptors(uint8_t *d, const struct gd_slots *o, const struct tegu_generic_draw_shader *sh,
    const struct tegu_generic_draw_request *q, const struct tegu_blend_state *blend, uint64_t vertex_code, uint64_t fragment_code)
{
    for (unsigned x = 0; x < 4; x++) gd_put32(d + o->blend + x * 4, blend->words[x]);
    if (sh->flags & TEGU_GENERIC_SHADER_TILE_FETCH) gd_put32(d + o->blend, blend->words[0] | 1u);
    gd_put32(d + o->position_spd, gd_spd(sh->vertex_registers, 0x38u)); gd_put32(d + o->position_spd + 4, (uint32_t)(sh->vertex_preload >> 48));
    gd_put64(d + o->position_spd + 8, vertex_code + sh->position_offset);
    gd_put32(d + o->varying_spd, gd_spd(sh->secondary_registers, 0x38u)); gd_put32(d + o->varying_spd + 4, (uint32_t)(sh->secondary_preload >> 48));
    gd_put64(d + o->varying_spd + 8, vertex_code + sh->secondary_offset);
    gd_put32(d + o->fragment_spd, gd_spd(sh->fragment_registers, 0x128u)); gd_put32(d + o->fragment_spd + 4, (uint32_t)(sh->fragment_preload >> 48));
    gd_put64(d + o->fragment_spd + 8, fragment_code);
    memcpy(d + o->vertex_fau + 0x80, q->vertex_push, q->vertex_push_bytes);
    memcpy(d + o->fragment_fau + 0x80, q->fragment_push, q->fragment_push_bytes);
    uint8_t *v = d + o->vertex_fau;
    uint32_t hw = gd_half(q->width), hh = gd_half(q->height);
    gd_put32(v, hw); gd_put32(v + 4, hh | 0x80000000u); gd_put32(v + 8, 0x3f800000);
    gd_put32(v + 0x10, hw); gd_put32(v + 0x14, hh); gd_put32(v + 0x20, 1);
    gd_put64(d + o->fragment_fau + 0x30, (uint64_t)blend->words[2] | ((uint64_t)blend->words[3] << 32));
}
static void gd_frame_head(struct gd_cmds *s, const struct tegu_render_addresses *a)
{
    gd_emit(s, 23ULL << 56); gd_move64(s, 72, a->tiler + 0xc0); gd_emit(s, (48ULL << 56) | (72ULL << 40));
    gd_emit(s, 49ULL << 56); gd_wait(s); gd_emit(s, (34ULL << 56) | 0xe);
}
/* One RUN_IDVS and the registers it reads. */
static void gd_draw_commands(struct gd_cmds *s, struct gd_regs *g, const struct tegu_generic_draw_shader *sh,
    const struct tegu_generic_draw_request *q, const struct tegu_render_addresses *a, const struct gd_draw_vas *v,
    uint64_t index_va, int reads_destination)
{
    uint64_t va = a->descriptors;
    gd_set64(s, g, 0, v->table); gd_set64(s, g, 4, v->table);
    gd_set64(s, g, 12, v->fragment_fau | ((uint64_t)((sh->fragment_fau_words + 1) / 2) << 56));
    gd_set64(s, g, 18, v->varying_spd); gd_set64(s, g, 28, 0); gd_set64(s, g, 46, 0);
    if (!g || !q->index_size) gd_set64(s, g, 54, 0);
    gd_set64(s, g, 8, v->vertex_fau | ((uint64_t)((sh->vertex_fau_words + 1) / 2) << 56));
    gd_set64(s, g, 16, v->position_spd); gd_set64(s, g, 20, v->fragment_spd); gd_set64(s, g, 24, va + 0x4c0);
    /* v10 IDVS: r33 index/vertex count, r34 instances, r39 index buffer
     * bytes, r54 index buffer address. */
    for (unsigned r = 32; r < 40; r++)
        gd_set32(s, g, r, r == 33 ? q->vertex_count : r == 34 ? 1 : r == 39 && q->index_size ? (uint32_t)q->index.bytes : 0);
    if (q->index_size) gd_set64(s, g, 54, index_va);
    gd_set64(s, g, 40, a->tiler);
    gd_set64(s, g, 42, ((uint64_t)(((q->scissor[1] + q->scissor[3] - 1u) << 16) | (q->scissor[0] + q->scissor[2] - 1u)) << 32) |
        ((q->scissor[1] << 16) | q->scissor[0]));
    gd_set32(s, g, 44, 0); gd_set32(s, g, 45, 0x3f800000); gd_set32(s, g, 48, sh->varying_bytes);
    gd_set64(s, g, 50, v->blend | 1); gd_set64(s, g, 52, va + 0x400); gd_set32(s, g, 56, (3u << 16) | (1u << 22));
    gd_set32(s, g, 57, (sh->flags & TEGU_GENERIC_SHADER_TILE_FETCH) ? 0u : reads_destination ? 2u : 3u); gd_set32(s, g, 58, 0x1ffff); gd_set64(s, g, 60, 0x3f800000);
    /* RUN_IDVS primitive flags: draw mode Triangles (8) or Lines (2). */
    gd_emit(s, (6ULL << 56) | (1ULL << 33) | (1ULL << 38) |
        (q->primitive == TEGU_GENERIC_DRAW_LINES ? 2u : q->primitive == TEGU_GENERIC_DRAW_TRIANGLE_STRIP ? 10u : 8u) |
        ((uint64_t)(q->index_size == 2 ? 2u : q->index_size == 4 ? 3u : 0u) << 8) | (1ULL << 18));
}
/* RUN_FRAGMENT bounding box in pixels, inclusive. */
struct gd_bbox { uint32_t x0, y0, x1, y1; };
static int gd_fragment_bbox_enabled;
void tegu_generic_set_fragment_bbox(int enabled) { gd_fragment_bbox_enabled = enabled != 0; }
int tegu_generic_fragment_bbox(void) { return gd_fragment_bbox_enabled; }
static int gd_code_share_enabled;
void tegu_generic_set_code_share(int enabled) { gd_code_share_enabled = enabled != 0; }
int tegu_generic_code_share(void) { return gd_code_share_enabled; }
static int gd_resource_share_enabled;
void tegu_generic_set_resource_share(int enabled) { gd_resource_share_enabled = enabled != 0; }
int tegu_generic_resource_share(void) { return gd_resource_share_enabled; }
/* v10/v11 support up to1024 pixels/tile. This path has one single-sample
 * BGRA8 target: allocate four bytes per pixel, in1024-byte units. Apply to
 * the final serialized FBD so LOAD's prebuilt parameters cannot undo it. */
static unsigned gd_tile_pixels = 256;
int tegu_generic_set_tile_pixels(unsigned pixels)
{
    if (pixels != 256 && pixels != 1024) return -1;
    gd_tile_pixels = pixels;
    return 0;
}
unsigned tegu_generic_tile_pixels(void) { return gd_tile_pixels; }
static void gd_apply_tile_size(uint8_t *memory)
{
    if (gd_tile_pixels == 256) return;
    uint32_t word = get_le32(memory + 44);
    word &= ~((15u << 9) | (255u << 24));
    gd_put32(memory + 44, word | (10u << 9) | (4u << 24));
}
static struct gd_bbox gd_bbox_target(const struct tegu_render_target_layout *l)
{
    return (struct gd_bbox){0, 0, l->width - 1u, l->height - 1u};
}
/* Grow b (empty: x0 > x1) by a validated scissor {x, y, w, h}. */
static void gd_bbox_add(struct gd_bbox *b, const uint32_t scissor[4])
{
    uint32_t x1 = scissor[0] + scissor[2] - 1u, y1 = scissor[1] + scissor[3] - 1u;
    if (b->x0 > b->x1) { *b = (struct gd_bbox){scissor[0], scissor[1], x1, y1}; return; }
    if (scissor[0] < b->x0) b->x0 = scissor[0];
    if (scissor[1] < b->y0) b->y0 = scissor[1];
    if (x1 > b->x1) b->x1 = x1;
    if (y1 > b->y1) b->y1 = y1;
}
/* Finish tiling, one fragment pass over bbox (the whole target unless the
 * opt-in LOAD bbox applies), flush. */
static void gd_frame_tail(struct gd_cmds *s, const struct tegu_render_addresses *a, struct gd_bbox b)
{
    gd_emit(s, 9ULL << 56); gd_wait(s);
    gd_emit(s, (49ULL << 56) | (1ULL << 32)); gd_wait(s);
    gd_move64(s, 40, a->descriptors | 1); gd_move32(s, 42, b.x0 | (b.y0 << 16)); gd_move32(s, 43, b.x1 | (b.y1 << 16));
    gd_move64(s, 44, 0); gd_move32(s, 46, 0);
    gd_emit(s, 7ULL << 56); gd_wait(s); gd_emit(s, (49ULL << 56) | (3ULL << 32)); gd_wait(s);
    gd_move32(s, 74, 0); gd_emit(s, (36ULL << 56) | (74ULL << 40) | 0x211); gd_emit(s, (3ULL << 56) | (1ULL << 16));
    gd_emit(s, 34ULL << 56);
}

/* The UI2 draw (tegu_fixed_ui_texture_render_build) with the shader's own
 * register allocation, FAU sizes, push images, varying size and blend. */
static int gd_render_build(const struct tegu_generic_draw_shader *sh, const struct tegu_generic_draw_request *q,
    const struct tegu_render_addresses *a, const struct tegu_render_target_layout *l,
    uint64_t index_va, struct tegu_render_state *out)
{
    struct tegu_blend_state blend;
    if (tegu_blend_build_format(80, &sh->blend, &blend)) return -3;
    memset(out, 0, sizeof(*out));
    gd_frame(out, a, l);
    gd_draw_descriptors(out->descriptors, &gd_single, sh, q, &blend, a->vertex_code, a->fragment_code);
    struct gd_draw_vas v = gd_vas(a->descriptors, &gd_single);
    v.table = (FS_APERTURE_VA + GD_SRT) | 4u;
    struct gd_cmds c = {out->commands, (uint32_t)(sizeof(out->commands) / sizeof(out->commands[0])), 0};
    gd_frame_head(&c, a);
    gd_draw_commands(&c, NULL, sh, q, a, &v, index_va, (int)blend.reads_destination);
    struct gd_bbox bbox = gd_bbox_target(l);
    if (gd_fragment_bbox_enabled && q->load_action == 1) { bbox = (struct gd_bbox){1, 1, 0, 0}; gd_bbox_add(&bbox, q->scissor); }
    gd_frame_tail(&c, a, bbox);
    out->command_count = c.n;
    return 0;
}

static int gd_request_ok(const struct tegu_generic_draw_shader *sh, const struct tegu_generic_draw_request *q)
{
    if (!sh->vertex_code || !sh->fragment_code || !sh->vertex_bytes || !sh->fragment_bytes ||
        sh->position_offset >= sh->secondary_offset || sh->secondary_offset >= sh->vertex_bytes ||
        !sh->varying_bytes || sh->varying_bytes > 64 || !sh->vertex_registers || sh->vertex_registers > 64 ||
        !sh->secondary_registers || sh->secondary_registers > 64 ||
        !sh->fragment_registers || sh->fragment_registers > 64 ||
        /* A stage that reads no push constant uses fewer than 32 FAU words. */
        sh->vertex_fau_words > 32 + TEGU_GENERIC_DRAW_PUSH_BYTES / 4 ||
        sh->fragment_fau_words > 32 + TEGU_GENERIC_DRAW_PUSH_BYTES / 4 ||
        /* No attributes: the vertex shader pulls its data by vertex_id. */
        sh->attribute_count > TEGU_GENERIC_DRAW_MAX_ATTRIBUTES ||
        (sh->flags & ~TEGU_GENERIC_SHADER_TILE_FETCH)) return 0;
    if (q->width < 16 || q->height < 16 || q->width > 4096 || q->height > 4096 || (q->row_bytes & 63) ||
        q->row_bytes < q->width * 4u || q->row_bytes > 65536 || q->load_action > 2 ||
        q->primitive > TEGU_GENERIC_DRAW_TRIANGLE_STRIP ||
        q->vertex_count < (q->primitive == TEGU_GENERIC_DRAW_LINES ? 2u : 3u) || q->vertex_count > TEGU_GENERIC_DRAW_MAX_VERTICES ||
        (q->primitive == TEGU_GENERIC_DRAW_TRIANGLES && q->vertex_count % 3) ||
        (q->primitive == TEGU_GENERIC_DRAW_LINES && q->vertex_count % 2) ||
        (!sh->attribute_count && (q->vertex.token || q->vertex.bytes)) ||
        (q->index_size != 0 && q->index_size != 2 && q->index_size != 4) ||
        (q->index_size && (sh->attribute_count || !q->index.token || (q->index.offset % q->index_size) ||
            q->index.bytes < (uint64_t)q->vertex_count * q->index_size || q->index.bytes > 0xffffffffu)) ||
        (!q->index_size && (q->index.token || q->index.bytes)) ||
        (16u + sh->varying_bytes) * q->vertex_count > TEGU_GENERIC_DRAW_GEOMETRY_BUDGET ||
        !q->scissor[2] || !q->scissor[3] || q->scissor[0] >= q->width || q->scissor[1] >= q->height ||
        q->scissor[2] > q->width - q->scissor[0] || q->scissor[3] > q->height - q->scissor[1] ||
        q->binding_count > TEGU_GENERIC_DRAW_MAX_BINDINGS ||
        q->vertex_push_bytes > TEGU_GENERIC_DRAW_PUSH_BYTES || q->fragment_push_bytes > TEGU_GENERIC_DRAW_PUSH_BYTES ||
        q->constant_bytes > TEGU_GENERIC_DRAW_CONSTANT_BYTES) return 0;
    unsigned tile_bindings = 0;
    for (uint32_t i = 0; i < q->binding_count; i++) {
        const struct tegu_generic_draw_binding *b = &q->bindings[i];
        if (b->binding >= GD_ENTRIES) return 0;
        for (uint32_t j = 0; j < i; j++) if (q->bindings[j].binding == b->binding) return 0;
        switch (b->kind) {
        case TEGU_GENERIC_DRAW_BUFFER:
            if (!b->memory.token || !b->memory.bytes || b->memory.bytes > UINT32_MAX) return 0;
            break;
        case TEGU_GENERIC_DRAW_CONSTANT:
            if (!q->constant_bytes) return 0;
            break;
        case TEGU_GENERIC_DRAW_COMBINED: case TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE: case TEGU_GENERIC_DRAW_TEXTURE: {
            uint32_t bw, bh;
            if (gd_astc(b->format, &bw, &bh)) {
                /* Rows of 16-byte ASTC blocks. */
                if (!b->memory.token || !b->width || !b->height || b->width > 4096 || b->height > 4096 ||
                    (b->row_bytes & 63) || b->row_bytes < (b->width + bw - 1u) / bw * 16u || (b->memory.offset & 63) ||
                    b->memory.bytes != (uint64_t)b->row_bytes * ((b->height + bh - 1u) / bh)) return 0;
                break;
            }
            if (!b->memory.token || !b->width || !b->height || b->width > 4096 || b->height > 4096 ||
                (b->format && b->format != 80 && b->format != 70 && b->format != 10 && b->format != 1 &&
                 b->format != 30 && b->format != 115) ||
                (b->row_bytes & 63) || b->row_bytes < b->width * gd_bytes_per_texel(b->format) || (b->memory.offset & 63) ||
                b->memory.bytes != (uint64_t)b->row_bytes * b->height) return 0;
            break;
        }
        case TEGU_GENERIC_DRAW_TILE_ATTACHMENT:
            if (!(sh->flags & TEGU_GENERIC_SHADER_TILE_FETCH) || b->memory.token || b->memory.offset || b->memory.bytes ||
                b->width || b->height || b->row_bytes || b->sampler || b->format || b->reserved) return 0;
            tile_bindings++;
            break;
        case TEGU_GENERIC_DRAW_ATTACHMENT:
            if (sh->flags & TEGU_GENERIC_SHADER_TILE_FETCH) return 0;
            break;
        case TEGU_GENERIC_DRAW_SAMPLER:
        case TEGU_GENERIC_DRAW_NEUTRAL_IMAGE: case TEGU_GENERIC_DRAW_NEUTRAL_BUFFER:
            break;
        default:
            return 0;
        }
    }
    return !!tile_bindings == !!(sh->flags & TEGU_GENERIC_SHADER_TILE_FETCH);
}

/* The draw's allocations: vertex buffer, index buffer, bound windows. */
static int gd_draw_spans(gj_req *r, const struct tegu_generic_draw_shader *sh,
    const struct tegu_generic_draw_request *q)
{
    if ((sh->attribute_count && gd_span(r, q->vertex.token, q->vertex.offset, q->vertex.bytes)) ||
        (q->index_size && gd_span(r, q->index.token, q->index.offset, q->index.bytes))) return -1;
    for (uint32_t i = 0; i < q->binding_count; i++) {
        const struct tegu_generic_draw_binding *b = &q->bindings[i];
        if (b->memory.token && gd_span(r, b->memory.token, b->memory.offset, b->memory.bytes)) return -1;
    }
    return 0;
}

/* Table 0 (vertex attributes), the SRT and tables 1/2 with texture planes,
 * the constant blob and the zero block, at o's offsets from m (GPU VA mva).
 * vs is builder workspace. */
static int gd_tables(uint8_t *m, uint64_t mva, const struct gd_slots *o, const gj_req *r,
    const struct tegu_generic_job_plan *p, const struct tegu_generic_draw_shader *sh,
    const struct tegu_generic_draw_request *q, struct tegu_vertex_state *vs)
{
    /* Table 0: attributes at their locations, the driver sampler at 16 and
     * the vertex buffer at 17, as compositor_vertex_state.c lays it out. */
    struct tegu_vertex_attribute attrs[TEGU_GENERIC_DRAW_MAX_ATTRIBUTES];
    uint32_t required = 0;
    for (uint32_t i = 0; i < sh->attribute_count; i++) {
        attrs[i] = sh->attributes[i];
        if (attrs[i].location >= 16 || attrs[i].buffer_index != sh->attributes[0].buffer_index) return -1;
        required |= 1u << attrs[i].location;
    }
    struct tegu_vertex_buffer buffers[TEGU_VERTEX_SLOTS];
    memset(buffers, 0, sizeof(buffers));
    if (sh->attribute_count)
        buffers[sh->attributes[0].buffer_index] = (struct tegu_vertex_buffer){gd_va(r, p, q->vertex.token, q->vertex.offset),
            q->vertex.bytes};
    struct tegu_vertex_draw draw = {0, q->vertex_count, 1, 0};
    /* The builder wants a page-aligned scratch address; only the attribute
     * and buffer descriptors it packs are copied. */
    if (tegu_vertex_fetch_build(required, attrs, sh->attribute_count, buffers, &draw, (mva + o->table0) & ~(uint64_t)4095, vs))
        return -5;
    uint8_t *table0 = m + o->table0;
    memset(table0, 0, TEGU_COMPOSITOR_DRIVER_BYTES);
    for (uint32_t i = 0; i < sh->attribute_count; i++) {
        unsigned location = attrs[i].location;
        memcpy(table0 + location * 32, vs->descriptors + 0x100 + location * 32, 32);
        gd_put32(table0 + location * 32 + 12, 17);
    }
    gd_put32(table0 + 16 * 32, 0x02a99901u);
    if (sh->attribute_count)
        memcpy(table0 + 17 * 32, vs->descriptors + 0x100 + (32 + sh->attributes[0].buffer_index) * 32, 32);
    /* Tables 1 and 2. */
    uint8_t *srt = m + o->srt, *t1 = m + o->table1, *t2 = m + o->table2;
    memset(srt, 0, 48); memset(t1, 0, GD_ENTRIES * 32u); memset(t2, 0, GD_ENTRIES * 32u);
    memset(m + o->planes, 0, GD_ENTRIES * 32u); memset(m + o->constant, 0, TEGU_GENERIC_DRAW_CONSTANT_BYTES); memset(m + o->zero, 0, 256);
    gd_resource(srt, mva + o->table0, TEGU_COMPOSITOR_DRIVER_BYTES);
    gd_resource(srt + 16, mva + o->table1, GD_ENTRIES * 32u);
    gd_resource(srt + 32, mva + o->table2, GD_ENTRIES * 32u);
    for (uint32_t i = 0; i < q->binding_count; i++) {
        const struct tegu_generic_draw_binding *b = &q->bindings[i];
        uint8_t *e1 = t1 + b->binding * 32u, *e2 = t2 + b->binding * 32u, *plane = m + o->planes + b->binding * 32u;
        uint64_t plane_va = mva + o->planes + b->binding * 32u;
        uint64_t at = b->memory.token ? gd_va(r, p, b->memory.token, b->memory.offset) : 0;
        if (b->memory.token && !at) return -1;
        switch (b->kind) {
        case TEGU_GENERIC_DRAW_BUFFER:
            gd_buffer(e1, at, (uint32_t)b->memory.bytes);
            break;
        case TEGU_GENERIC_DRAW_CONSTANT:
            memcpy(m + o->constant, q->constant, q->constant_bytes);
            gd_buffer(e1, mva + o->constant, (q->constant_bytes + 15u) & ~15u);
            break;
        case TEGU_GENERIC_DRAW_COMBINED: case TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE:
            if (gd_sampler(e2, b->sampler)) return -1;
            /* fall through */
        case TEGU_GENERIC_DRAW_TEXTURE: {
            /* This legacy packer knows exactly these formats; any other one
             * (the n2r/n2t path packs the full arch9 table) is refused here
             * rather than packed as BGRA8. */
            uint32_t bw, bh;
            if (b->format && b->format != 1 && b->format != 10 && b->format != 30 && b->format != 70 &&
                b->format != 80 && b->format != 115 && !gd_astc(b->format, &bw, &bh)) return -1;
            gd_texture(e1, plane, plane_va, at, b->width, b->height, b->row_bytes, b->format);
        }
            /* Texel fetches name the sampler at their own binding. */
            if (b->kind == TEGU_GENERIC_DRAW_TEXTURE && !get_le32(e2) && gd_sampler(e2, 0)) return -1;
            break;
        case TEGU_GENERIC_DRAW_SAMPLER:
            if (gd_sampler(e2, b->sampler)) return -1;
            break;
        case TEGU_GENERIC_DRAW_NEUTRAL_IMAGE:
            gd_texture(e1, plane, plane_va, mva + o->zero, 1, 1, 64, 80);
            if (gd_sampler(e2, 0)) return -1;
            break;
        case TEGU_GENERIC_DRAW_NEUTRAL_BUFFER:
            gd_buffer(e1, mva + o->zero, 256);
            break;
        case TEGU_GENERIC_DRAW_TILE_ATTACHMENT:
            /* LD_TILE names the active tile directly, not a texture/sampler. */
            break;
        case TEGU_GENERIC_DRAW_ATTACHMENT:
            /* Framebuffer fetch reads the render target's pre-pass contents. */
            gd_texture(e1, plane, plane_va, p->target_va, q->width, q->height, q->row_bytes, 80);
            if (gd_sampler(e2, 0)) return -1;
            break;
        }
    }
    return 0;
}

int tegu_generic_draw_job_build(const struct tegu_generic_draw_shader *sh,
    const struct tegu_generic_draw_request *q, const struct tegu_generic_job_images *images,
    struct tegu_generic_job_request *out)
{
    if (!sh || !q || !images || !out || !gd_request_ok(sh, q)) return -1;
    const int load = q->load_action == 1;
    if (load && (!images->load || !images->load_token)) return -1;
    uint64_t target_bytes = (uint64_t)(q->height - 1u) * q->row_bytes + (uint64_t)q->width * 4u;
    gj_req r;
    if (gj_begin(&r, TEGU_GENERIC_JOB_VERSION, images, q->width, q->height, q->row_bytes, q->target_token, q->target_offset,
            q->target_length, target_bytes) || gd_draw_spans(&r, sh, q)) return -2;
    unsigned load_span = r.span_count;
    if (load && gj_span(&r, images->load_token, 0, TEGU_GENERIC_JOB_LOAD_BYTES)) return -2;
    struct tegu_v7_scratch_layout sl;
    if (!tegu_ui2_scratch_layout_build(4096u, 1152u, load, &sl)) return -1;
    sl.fs_capacity = TEGU_V7_FS_PAGES * TEGU_V7_PAGE;
    if (sh->vertex_bytes > sl.vs_capacity || sh->fragment_bytes > sl.fs_capacity) return -4;
    r.exec_mask = sl.exec_mask;
    struct tegu_generic_job_plan p;
    if (gj_plan(&r, &p)) return -3; /* spans exceed TGJ1 resource pages */
    uint8_t *memory = images->scratch;
    memset(memory, 0, TEGU_GENERIC_JOB_SCRATCH_BYTES);
    if (!tegu_v7_scratch_prepare(memory, TEGU_GENERIC_JOB_SCRATCH_BYTES)) return -1;
    memcpy(memory + sl.vs_offset, sh->vertex_code, sh->vertex_bytes);
    memcpy(memory + sl.fs_offset, sh->fragment_code, sh->fragment_bytes);
    struct tegu_vertex_state *vs = (struct tegu_vertex_state *)(memory + GJ_WORKSPACE);
    if (sizeof(*vs) > 0x4000) return -5;
    int tables = gd_tables(memory, FS_APERTURE_VA, &gd_single, &r, &p, sh, q, vs);
    if (tables) return tables;
    struct tegu_render_addresses addresses = gj_addresses(&r, &p, &sl, q->load_action == 2 ? q->clear_color : 0);
    struct tegu_render_target_layout target = {TEGU_RENDER_TARGET_LAYOUT_VERSION, q->width, q->height,
        q->row_bytes, p.layout.target_bytes};
    struct tegu_render_state *state = (struct tegu_render_state *)(memory + GJ_WORKSPACE);
    uint64_t index_va = q->index_size ? gd_va(&r, &p, q->index.token, q->index.offset) : 0;
    if (sizeof(*state) > 0x4000 || (q->index_size && !index_va) ||
        gd_render_build(sh, q, &addresses, &target, index_va, state) ||
        gj_finish(&r, &p, images, &sl, state, &addresses, &target, load, load_span,
            sh->vertex_bytes, sh->fragment_bytes)) return -7;
    gd_apply_tile_size(images->scratch);
    return gj_to_v1(&r, out);
}

/* ---- Several draws of one pass in one job --------------------------------
 * One tiler context bins every draw; one fragment pass loads (or clears) the
 * target once and stores it once.  Draws keep their order, own resource
 * tables, FAU images, blend descriptors and shader programs (one 4 KiB block
 * each, from GD_BATCH_BLOCKS), and share the spans, the two executable code
 * regions and the geometry buffer. */
#define GD_BATCH_BLOCKS 0x20000u
/* Blocks 0-15 at 0x20000-0x2ffff and 16-31 at 0x37000-0x46fff (unused by the
 * v7 layout).  Blocks 32-63 use 0x50000-0x6ffff, the 128 KiB scratch tiler
 * chunk, which a job with the 2 MiB heap never names (its heap descriptor
 * points at the heap), so only such jobs get them. */
static uint32_t gd_block_offset(unsigned k)
{
    return k < 16 ? GD_BATCH_BLOCKS + k * 0x1000u : k < 32 ? 0x37000u + (k - 16) * 0x1000u :
        TEGU_V7_CHUNK_OFFSET + (k - 32) * 0x1000u;
}
_Static_assert(TEGU_GENERIC_BATCH_MAX_DRAWS == 64 && (TEGU_GENERIC_BATCH_MAX_DRAWS - 32) * 0x1000u <= TEGU_V7_CHUNK_BYTES &&
    TEGU_V7_CHUNK_OFFSET + TEGU_V7_CHUNK_BYTES <= TEGU_V7_SCRATCH_BYTES, "batch blocks fit the scratch image");
static unsigned gd_batch_blocks(const gj_req *r) { return (r->flags & TEGU_GENERIC_JOB_HEAP2M) ? 64u : 32u; }
/* Executable scratch present in both exec masks: VS pages 2-3, FS pages 48-54. */
static const uint32_t gd_code_region[2][2] = {{TEGU_V7_VS_OFFSET, TEGU_V7_VS_PAGES * TEGU_V7_PAGE},
    {TEGU_V7_FS_OFFSET, TEGU_V7_FS_PAGES * TEGU_V7_PAGE}};

/* Tiler heap estimate.  The 2 MiB heap never grows: a TILER_OOM stalls the
 * job until the kernel's 5 s timeout stops the GPU queue, so a batch must not
 * plan for more than it holds.  Measured once (2026-10-21 heap2m-r1e, one
 * full-size 1125x2436 tiling-only draw): the heap top moved down 0x61780 =
 * 144 bytes per 32x32 bin of the target + 64.  Per draw (inferred, not
 * measured): 16 bytes per primitive and 32x32 bin of its scissor for its
 * first four primitives (any primitive may cover the whole scissor), four
 * bins for each further primitive, plus its IDVS positions and varyings.
 * TGJ2 kernels report what a job really used (TEGU_BATCH heap=). */
static uint64_t gd_bins32(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    return (uint64_t)((x + w + 31u) / 32u - x / 32u) * ((y + h + 31u) / 32u - y / 32u);
}
uint64_t tegu_generic_batch_heap_frame(uint32_t width, uint32_t height)
{
    return 144u * gd_bins32(0, 0, width, height) + 64u;
}
uint64_t tegu_generic_batch_heap_draw(const struct tegu_generic_draw_shader *sh, const struct tegu_generic_draw_request *q)
{
    uint64_t v = q->vertex_count;
    uint64_t prims = q->primitive == TEGU_GENERIC_DRAW_LINES ? v / 2 :
        q->primitive == TEGU_GENERIC_DRAW_TRIANGLE_STRIP ? (v >= 2 ? v - 2 : 0) : v / 3;
    uint64_t first = prims < 4 ? prims : 4;
    uint64_t bins = gd_bins32(q->scissor[0], q->scissor[1], q->scissor[2], q->scissor[3]);
    return 16u * (first * bins + 4u * (prims - first)) + (16u + sh->varying_bytes) * v;
}

/* R220/R226/R230: <=24 triangles in total, with 3, 6 or 48 nonindexed
 * vertices per draw and 16/24/32 varying bytes. Full-frame tail uses
 * 656 bytes per 32x32 bin +64; narrower scissors remain conservative.
 * per-draw head fits64*(16+varying_bytes)+128. Reserve an additional4KiB
 * per draw and128KiB outside the budget. This is a measured narrow model,
 * not a replacement for arbitrary meshes. Never expand index/line workloads. */
static int gd_small_heap_bound(const struct tegu_generic_draw_shader *const *sh,
    const struct tegu_generic_draw_request *const *q,unsigned n,uint64_t *bound)
{
    if(gd_tile_pixels!=1024||!n||n>24)return 0;
    unsigned primitives=0;uint64_t head=0;
    for(unsigned k=0;k<n;k++) {
        if(!sh[k]||!q[k]||q[k]->index_size||q[k]->primitive!=TEGU_GENERIC_DRAW_TRIANGLES||
           (q[k]->vertex_count!=3&&q[k]->vertex_count!=6&&q[k]->vertex_count!=48)||
           (sh[k]->varying_bytes!=16&&sh[k]->varying_bytes!=24&&sh[k]->varying_bytes!=32))return 0;
        primitives+=q[k]->vertex_count/3;
        if(primitives>24)return 0;
        head+=64u*(16u+sh[k]->varying_bytes)+128u+4096u;
    }
    *bound=656u*gd_bins32(0,0,q[0]->width,q[0]->height)+64u+head;
    return 1;
}
struct gd_batch_layout {
    gj_req r; /* spans, without the LOAD page */
    uint32_t vertex_code[TEGU_GENERIC_BATCH_MAX_DRAWS], fragment_code[TEGU_GENERIC_BATCH_MAX_DRAWS];
    uint32_t used[2];
    uint64_t heap;
    unsigned small_heap;
};
static int gd_code_place(const uint8_t *code, uint32_t bytes, const uint8_t *const *placed, const uint32_t *placed_bytes,
    const uint32_t *placed_at, unsigned count, uint32_t used[2], uint32_t *at)
{
    for (unsigned i = 0; i < count; i++)
        if (placed_bytes[i] == bytes && (placed[i] == code || (gd_code_share_enabled && !memcmp(placed[i], code, bytes)))) {
            *at = placed_at[i]; return 0;
        }
    for (unsigned g = 0; g < 2; g++) {
        uint32_t start = (used[g] + 127u) & ~127u;
        if (start <= gd_code_region[g][1] && bytes <= gd_code_region[g][1] - start) {
            *at = gd_code_region[g][0] + start; used[g] = start + bytes;
            return 0;
        }
    }
    return -1;
}
/* A later draw joins draw 0's job when it draws the same target, continues
 * it (LOAD) and does not read the target's memory: the batch's fragment pass
 * only stores once, so framebuffer fetch sees the pre-job contents. */
static unsigned gd_batch_join_stop(const struct tegu_generic_draw_request *q0, const struct tegu_generic_draw_request *q)
{
    if (q->width != q0->width || q->height != q0->height || q->row_bytes != q0->row_bytes ||
        q->target_token != q0->target_token || q->target_offset != q0->target_offset ||
        q->target_length != q0->target_length || q->load_action != 1) return TEGU_BATCH_STOP_SHAPE;
    if (q->vertex.token == q0->target_token || q->index.token == q0->target_token) return TEGU_BATCH_STOP_READS_TARGET;
    for (uint32_t i = 0; i < q->binding_count; i++)
        if (q->bindings[i].kind == TEGU_GENERIC_DRAW_ATTACHMENT || q->bindings[i].memory.token == q0->target_token)
            return TEGU_BATCH_STOP_READS_TARGET;
    return TEGU_BATCH_STOP_NONE;
}
/* Spans, code placement, geometry and heap for the first n draws.  On
 * failure *why (if given) names the limit the last draw hit
 * (TEGU_BATCH_STOP_*); the return value and *out do not depend on it. */
#define GD_STOP(reason) do { if (why) *why = (reason); return -1; } while (0)
static int gd_batch_layout(const struct tegu_generic_draw_shader *const *sh, const struct tegu_generic_draw_request *const *q,
    unsigned n, const struct tegu_generic_job_images *images, const struct tegu_generic_batch_limits *lim,
    struct gd_batch_layout *out, unsigned *why)
{
    const struct tegu_generic_draw_request *q0 = q[0];
    const int load = q0->load_action == 1;
    if (n < 1 || n > TEGU_GENERIC_BATCH_MAX_DRAWS || n > lim->max_draws) GD_STOP(TEGU_BATCH_STOP_BLOCKS);
    if (load && (!images->load || !images->load_token)) GD_STOP(TEGU_BATCH_STOP_INVALID);
    uint64_t target_bytes = (uint64_t)(q0->height - 1u) * q0->row_bytes + (uint64_t)q0->width * 4u;
    memset(out, 0, sizeof(*out));
    if (gj_begin(&out->r, lim->version, images, q0->width, q0->height, q0->row_bytes, q0->target_token, q0->target_offset,
            q0->target_length, target_bytes)) GD_STOP(TEGU_BATCH_STOP_INVALID);
    if (n > gd_batch_blocks(&out->r)) GD_STOP(TEGU_BATCH_STOP_BLOCKS);
    if (lim->small_heap_budget > TEGU_GENERIC_SMALL_HEAP_BUDGET) GD_STOP(TEGU_BATCH_STOP_INVALID);
    const uint8_t *placed[2 * TEGU_GENERIC_BATCH_MAX_DRAWS];
    uint32_t placed_bytes[2 * TEGU_GENERIC_BATCH_MAX_DRAWS], placed_at[2 * TEGU_GENERIC_BATCH_MAX_DRAWS], geometry = 0;
    unsigned codes = 0;
    uint64_t legacy_heap = tegu_generic_batch_heap_frame(q0->width, q0->height);
    out->heap = legacy_heap;
    for (unsigned k = 0; k < n; k++) {
        if (!sh[k] || !q[k] || !gd_request_ok(sh[k], q[k])) GD_STOP(TEGU_BATCH_STOP_INVALID);
        if (k) { unsigned join = gd_batch_join_stop(q0, q[k]); if (join) GD_STOP(join); }
        if (gd_draw_spans(&out->r, sh[k], q[k])) GD_STOP(TEGU_BATCH_STOP_SPANS);
        geometry += (16u + sh[k]->varying_bytes) * q[k]->vertex_count;
        if (geometry > TEGU_GENERIC_DRAW_GEOMETRY_BUDGET) GD_STOP(TEGU_BATCH_STOP_GEOMETRY);
        legacy_heap += tegu_generic_batch_heap_draw(sh[k], q[k]);
        out->heap=legacy_heap;out->small_heap=0;
        if (legacy_heap > lim->heap_budget) {
            if (!lim->small_heap_budget || !(out->r.flags&TEGU_GENERIC_JOB_HEAP2M) ||
                !gd_small_heap_bound(sh,q,k+1,&out->heap) || out->heap>lim->small_heap_budget)
                GD_STOP(TEGU_BATCH_STOP_HEAP);
            out->small_heap=1;
        }
        if (gd_code_place(sh[k]->vertex_code, sh[k]->vertex_bytes, placed, placed_bytes, placed_at, codes, out->used,
                &out->vertex_code[k])) GD_STOP(TEGU_BATCH_STOP_CODE);
        placed[codes] = sh[k]->vertex_code; placed_bytes[codes] = sh[k]->vertex_bytes; placed_at[codes++] = out->vertex_code[k];
        if (gd_code_place(sh[k]->fragment_code, sh[k]->fragment_bytes, placed, placed_bytes, placed_at, codes, out->used,
                &out->fragment_code[k])) GD_STOP(TEGU_BATCH_STOP_CODE);
        placed[codes] = sh[k]->fragment_code; placed_bytes[codes] = sh[k]->fragment_bytes; placed_at[codes++] = out->fragment_code[k];
    }
    gj_req *probe = malloc(sizeof(*probe));
    if (!probe) GD_STOP(TEGU_BATCH_STOP_BUILD);
    *probe = out->r;
    struct tegu_generic_job_plan p;
    probe->exec_mask = load ? TEGU_V7_LOAD_EXEC_MASK : TEGU_V7_CLEAR_EXEC_MASK;
    int spans = load && gj_span(probe, images->load_token, 0, TEGU_GENERIC_JOB_LOAD_BYTES);
    int rc = spans || gj_plan(probe, &p) ? -1 : 0;
    free(probe);
    if (rc) GD_STOP(spans ? TEGU_BATCH_STOP_SPANS : TEGU_BATCH_STOP_PLAN);
    return 0;
}
#undef GD_STOP
/* Identical descriptor bytes in an earlier block: name that block's copy. */
static uint64_t gd_batch_share(const uint8_t *memory, unsigned k, uint32_t slot, uint32_t bytes)
{
    for (unsigned j = 0; j < k; j++)
        if (!memcmp(memory + gd_block_offset(j) + slot, memory + gd_block_offset(k) + slot, bytes))
            return FS_APERTURE_VA + gd_block_offset(j) + slot;
    return FS_APERTURE_VA + gd_block_offset(k) + slot;
}
/* Tables are immutable for the lifetime of a submitted batch. Canonicalize
 * each byte-identical table, then the SRT naming those tables. Embedded
 * per-draw pointers (texture planes, constants, neutral storage) deliberately
 * prevent sharing unless their bytes already match. No pointer rebasing. */
static uint64_t gd_batch_resources(uint8_t *memory, unsigned k)
{
    uint8_t *srt = memory + gd_block_offset(k) + gd_block.srt;
    gd_resource(srt, gd_batch_share(memory, k, gd_block.table0, TEGU_COMPOSITOR_DRIVER_BYTES), TEGU_COMPOSITOR_DRIVER_BYTES);
    gd_resource(srt + 16, gd_batch_share(memory, k, gd_block.table1, GD_ENTRIES * 32u), GD_ENTRIES * 32u);
    gd_resource(srt + 32, gd_batch_share(memory, k, gd_block.table2, GD_ENTRIES * 32u), GD_ENTRIES * 32u);
    return gd_batch_share(memory, k, gd_block.srt, 48) | 4u;
}
/* The descriptor/tiler pages and the final stream of a batch.  cmds[0..n)
 * is frame head + draws + tail (from tail_start).  The LOAD bundle rewrites
 * only the tail's clear marker and prepends the previous-job wait, so it is
 * given the tail alone and the draws are spliced back in between: the same
 * stream as rewriting the whole one, without its 124-command input bound. */
static int gj_finish_batch(gj_req *r, const struct tegu_generic_job_plan *p, const struct tegu_generic_job_images *images,
    const struct tegu_v7_scratch_layout *sl, struct tegu_render_state *state, const struct tegu_render_addresses *a,
    const struct tegu_render_target_layout *target, int load, unsigned load_span, size_t vertex_bytes, size_t fragment_bytes,
    const uint64_t *cmds, uint32_t n, uint32_t tail_start, uint32_t cap)
{
    const uint32_t room = (uint32_t)(sizeof(state->commands) / sizeof(state->commands[0]));
    if (tail_start > n || n - tail_start > room - 3u || cap > gj_command_limit(r)) return -1;
    uint32_t total;
    if (load) {
        uint32_t tail = n - tail_start;
        memcpy(state->commands, cmds + tail_start, tail * sizeof(cmds[0]));
        state->command_count = tail;
        if (gj_load_apply(p, images, sl, state, a, target, load_span, vertex_bytes, fragment_bytes) ||
            state->command_count < 3 || state->command_count > room) return -1;
        uint32_t rest = state->command_count - 3u;
        total = 3u + tail_start + rest;
        if (total > cap) return -8;
        memcpy(r->commands, state->commands, 3u * sizeof(cmds[0]));
        memcpy(r->commands + 3, cmds, tail_start * sizeof(cmds[0]));
        memcpy(r->commands + 3 + tail_start, state->commands + 3, rest * sizeof(cmds[0]));
    } else {
        total = n;
        if (total > cap) return -8;
        memcpy(r->commands, cmds, n * sizeof(cmds[0]));
    }
    memcpy(images->scratch, state->descriptors, sizeof(state->descriptors));
    memcpy(images->scratch + 0x1000, state->tiler, sizeof(state->tiler));
    r->command_count = total;
    struct tegu_generic_job_plan check;
    return gj_plan(r, &check) || memcmp(&check, p, sizeof(check)) ? -1 : 0;
}
/* 0, -8 when the command stream exceeds lim->max_commands (*fit: the most
 * draws whose stream fits), else < 0. */
#define GD_BATCH_STREAM (TEGU_GENERIC_JOB2_MAX_COMMANDS + 64u)
static int gd_batch_try(const struct tegu_generic_draw_shader *const *sh, const struct tegu_generic_draw_request *const *q,
    unsigned n, const struct tegu_generic_job_images *images, const struct tegu_generic_batch_limits *lim,
    gj_req *out, unsigned *fit, struct tegu_generic_batch_info *info)
{
    struct gd_batch_layout *b = malloc(sizeof(*b));
    struct tegu_vertex_state *vs = malloc(sizeof(*vs));
    struct tegu_render_state *state = malloc(sizeof(*state));
    struct gd_regs *g = malloc(sizeof(*g));
    uint64_t *cmds = malloc(GD_BATCH_STREAM * sizeof(*cmds));
    uint32_t after[TEGU_GENERIC_BATCH_MAX_DRAWS];
    int rc = -1;
    const int load = q[0]->load_action == 1;
    struct tegu_v7_scratch_layout sl;
    if (!b || !vs || !state || !g || !cmds || gd_batch_layout(sh, q, n, images, lim, b, NULL) ||
        !tegu_ui2_scratch_layout_build(4096u, 1152u, load, &sl)) goto done;
    gj_req *r = &b->r;
    unsigned load_span = r->span_count;
    if (load && gj_span(r, images->load_token, 0, TEGU_GENERIC_JOB_LOAD_BYTES)) goto done;
    r->exec_mask = sl.exec_mask;
    struct tegu_generic_job_plan p;
    if (gj_plan(r, &p)) goto done;
    uint8_t *memory = images->scratch;
    memset(memory, 0, TEGU_GENERIC_JOB_SCRATCH_BYTES);
    if (!tegu_v7_scratch_prepare(memory, TEGU_GENERIC_JOB_SCRATCH_BYTES)) goto done;
    struct tegu_render_addresses a = gj_addresses(r, &p, &sl, q[0]->load_action == 2 ? q[0]->clear_color : 0);
    struct tegu_render_target_layout target = {TEGU_RENDER_TARGET_LAYOUT_VERSION, q[0]->width, q[0]->height,
        q[0]->row_bytes, p.layout.target_bytes};
    memset(state, 0, sizeof(*state)); memset(g, 0, sizeof(*g));
    struct gd_cmds c = {cmds, GD_BATCH_STREAM, 0};
    gd_frame(state, &a, &target);
    gd_frame_head(&c, &a);
    for (unsigned k = 0; k < n; k++) {
        struct tegu_blend_state blend;
        uint32_t block = gd_block_offset(k);
        uint8_t *m = memory + block;
        uint64_t mva = FS_APERTURE_VA + block;
        uint64_t index_va = q[k]->index_size ? gd_va(r, &p, q[k]->index.token, q[k]->index.offset) : 0;
        memcpy(memory + b->vertex_code[k], sh[k]->vertex_code, sh[k]->vertex_bytes);
        memcpy(memory + b->fragment_code[k], sh[k]->fragment_code, sh[k]->fragment_bytes);
        if (tegu_blend_build_format(80, &sh[k]->blend, &blend) || (q[k]->index_size && !index_va) ||
            gd_tables(m, mva, &gd_block, r, &p, sh[k], q[k], vs)) goto done;
        gd_draw_descriptors(m, &gd_block, sh[k], q[k], &blend, FS_APERTURE_VA + b->vertex_code[k],
            FS_APERTURE_VA + b->fragment_code[k]);
        struct gd_draw_vas v = gd_vas(mva, &gd_block);
        if (gd_resource_share_enabled) v.table = gd_batch_resources(memory, k);
        v.blend = gd_batch_share(memory, k, gd_block.blend, 16);
        v.fragment_spd = gd_batch_share(memory, k, gd_block.fragment_spd, 16);
        v.position_spd = gd_batch_share(memory, k, gd_block.position_spd, 16);
        v.varying_spd = gd_batch_share(memory, k, gd_block.varying_spd, 16);
        v.vertex_fau = gd_batch_share(memory, k, gd_block.vertex_fau, 0x100);
        v.fragment_fau = gd_batch_share(memory, k, gd_block.fragment_fau, 0x100);
        gd_draw_commands(&c, g, sh[k], q[k], &a, &v, index_va, (int)blend.reads_destination);
        after[k] = c.n;
    }
    uint32_t tail_start = c.n;
    /* Opt-in: a LOAD pass's fragment job covers only its draws' scissors. */
    struct gd_bbox bbox = gd_bbox_target(&target);
    if (gd_fragment_bbox_enabled && load) {
        bbox = (struct gd_bbox){1, 1, 0, 0};
        for (unsigned k = 0; k < n; k++) gd_bbox_add(&bbox, q[k]->scissor);
    }
    gd_frame_tail(&c, &a, bbox);
    /* The LOAD bundle replaces two tail commands with three. */
    const uint32_t extra = load ? 1u : 0u, tail = c.n - tail_start;
    uint32_t cap = lim->max_commands < gj_command_limit(r) ? lim->max_commands : gj_command_limit(r);
    if (c.n > GD_BATCH_STREAM || c.n + extra > cap) {
        unsigned m = 0;
        while (m < n && after[m] + tail + extra <= cap) m++;
        *fit = m;
        rc = -8;
        goto done;
    }
    rc = gj_finish_batch(r, &p, images, &sl, state, &a, &target, load, load_span,
        b->used[0] ? b->used[0] : 128u, b->used[1] ? b->used[1] : 128u, cmds, c.n, tail_start, cap);
    if (rc) { if (rc == -8) *fit = n - 1; goto done; }
    gd_apply_tile_size(images->scratch);
    if (info) { info->heap_estimate = b->heap; info->commands = r->command_count; info->small_heap=b->small_heap; }
    *out = *r;
done:
    free(b); free(vs); free(state); free(g); free(cmds);
    return rc;
}

int tegu_generic_draw_batch_build2(const struct tegu_generic_draw_shader *const *shaders,
    const struct tegu_generic_draw_request *const *requests, unsigned count,
    const struct tegu_generic_job_images *images, const struct tegu_generic_batch_limits *limits,
    struct tegu_generic_job2_request *out, struct tegu_generic_batch_info *info)
{
    /* info->stop: why the batch ends before the next offered draw. */
    if (!shaders || !requests || !images || !images->scratch || !limits || !out || count < 2 ||
        (limits->version != TEGU_GENERIC_JOB_VERSION && limits->version != TEGU_GENERIC_JOB2_VERSION)) {
        if (info) info->stop = TEGU_BATCH_STOP_INVALID;
        return 0;
    }
    const unsigned offered = count;
    if (count > TEGU_GENERIC_BATCH_MAX_DRAWS) count = TEGU_GENERIC_BATCH_MAX_DRAWS;
    if (count > limits->max_draws) count = limits->max_draws;
    struct gd_batch_layout *b = malloc(sizeof(*b));
    if (!b) {
        if (info) info->stop = TEGU_BATCH_STOP_BUILD;
        return 0;
    }
    unsigned n = 0, stop = offered > count ? TEGU_BATCH_STOP_BLOCKS : TEGU_BATCH_STOP_NONE;
    while (n < count && !gd_batch_layout(shaders, requests, n + 1, images, limits, b, &stop)) n++;
    free(b);
    while (n >= 2) {
        unsigned fit = 0;
        int rc = gd_batch_try(shaders, requests, n, images, limits, out, &fit, info);
        if (!rc) break;
        if (rc != -8) { n = 0; stop = TEGU_BATCH_STOP_BUILD; break; }
        stop = TEGU_BATCH_STOP_COMMANDS;
        n = fit < n ? fit : n - 1;
    }
    if (info) info->stop = stop;
    return n >= 2 ? (int)n : 0;
}

int tegu_generic_job_to_v1(const struct tegu_generic_job2_request *request, struct tegu_generic_job_request *out)
{
    return request && out ? gj_to_v1(request, out) : -1;
}

int tegu_generic_draw_batch_build(const struct tegu_generic_draw_shader *const *shaders,
    const struct tegu_generic_draw_request *const *requests, unsigned count,
    const struct tegu_generic_job_images *images, struct tegu_generic_job_request *out)
{
    const struct tegu_generic_batch_limits limits = {TEGU_GENERIC_JOB_VERSION, TEGU_GENERIC_JOB_SUBMIT_COMMANDS,
        TEGU_GENERIC_BATCH_TGJ1_DRAWS, TEGU_GENERIC_BATCH_HEAP_BUDGET,0};
    gj_req *r = out ? malloc(sizeof(*r)) : 0;
    if (!r) return 0;
    int n = tegu_generic_draw_batch_build2(shaders, requests, count, images, &limits, r, 0);
    if (n && gj_to_v1(r, out)) n = 0;
    free(r);
    return n;
}

/* ---- GPU floor microbenchmark variants -----------------------------------
 * The stream shape these rewrite is gd_frame_head + draws + gd_frame_tail,
 * after the LOAD bundle (which drops the tail's HEAP_OPERATION 1 + wait and
 * prepends wait/FLUSH_CACHE2 0x200/wait).  Only allowlisted opcodes are
 * produced: NOP, MOVE32, WAIT, RUN_FRAGMENT, REQ_RESOURCE, FLUSH_CACHE2. */
static unsigned gb_op(uint64_t c) { return (unsigned)(c >> 56); }
static void gb_remove(uint64_t *c, uint32_t *n, uint32_t at)
{
    for (uint32_t i = at; i + 1 < *n; i++) c[i] = c[i + 1];
    (*n)--;
}
static int gb_is_wait(uint64_t c) { return gb_op(c) == 3; }
int tegu_generic_bench_transform(uint64_t *c, uint32_t *count, uint32_t cap, unsigned variant, const uint32_t bbox[4])
{
    if (!c || !count || !*count || *count > cap) return -1;
    uint32_t n = *count, fragment = UINT32_MAX, tiling = UINT32_MAX, fragments = 0, tilings = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (gb_op(c[i]) == 7) { fragment = i; fragments++; }
        if (gb_op(c[i]) == 9) { tiling = i; tilings++; }
    }
    const uint64_t flush_id = 2ULL << 56 | 74ULL << 48, flush = 36ULL << 56 | 74ULL << 40 | 0x211u,
        flush_wait = 3ULL << 56 | 1ULL << 16;
    switch (variant) {
    case TEGU_BENCH_AS_BUILT:
        return 0;
    case TEGU_BENCH_NOP:
        c[0] = 0; *count = 1;
        return 0;
    case TEGU_BENCH_FLUSH1: case TEGU_BENCH_FLUSH8: {
        uint32_t k = variant == TEGU_BENCH_FLUSH1 ? 1u : 8u;
        if (3u * k > cap) return -1;
        for (uint32_t i = 0; i < k; i++) { c[3 * i] = flush_id; c[3 * i + 1] = flush; c[3 * i + 2] = flush_wait; }
        *count = 3u * k;
        return 0;
    }
    case TEGU_BENCH_REQRES:
        if (cap < 2) return -1;
        c[0] = 34ULL << 56 | 0xeu; c[1] = 34ULL << 56; *count = 2;
        return 0;
    default:
        break;
    }
    /* The rest edit a production stream: one FINISH_TILING before one
     * RUN_FRAGMENT, each RUN_FRAGMENT followed by its wait. */
    if (fragments != 1 || tilings != 1 || tiling > fragment || fragment + 1 >= n || !gb_is_wait(c[fragment + 1])) return -1;
    switch (variant) {
    case TEGU_BENCH_BBOX: {
        if (!bbox || bbox[0] > bbox[2] || bbox[1] > bbox[3] || (bbox[2] | bbox[3]) > 0xffffu) return -1;
        uint32_t found = 0;
        for (uint32_t i = tiling + 1; i < fragment; i++) {
            unsigned r = (unsigned)((c[i] >> 48) & 255u);
            if (gb_op(c[i]) != 2 || (r != 42 && r != 43)) continue;
            uint32_t v = r == 42 ? bbox[0] | bbox[1] << 16 : bbox[2] | bbox[3] << 16;
            c[i] = (c[i] & ~(uint64_t)0xffffffffu) | v;
            found |= r == 42 ? 1u : 2u;
        }
        return found == 3u ? 0 : -1;
    }
    case TEGU_BENCH_NOFLUSH:
        for (uint32_t i = fragment + 2; i + 1 < n; i++)
            if (c[i] == flush && c[i + 1] == flush_wait) { gb_remove(c, &n, i); gb_remove(c, &n, i); *count = n; return 0; }
        return -1;
    case TEGU_BENCH_KEEPRES:
        for (uint32_t i = n; i-- > fragment + 2;)
            if (c[i] == 34ULL << 56) { gb_remove(c, &n, i); *count = n; return 0; }
        return -1;
    case TEGU_BENCH_NODRAW: {
        uint32_t removed = 0;
        for (uint32_t i = 0; i < n;) if (gb_op(c[i]) == 6) { gb_remove(c, &n, i); removed++; } else i++;
        *count = n;
        return removed ? 0 : -1;
    }
    case TEGU_BENCH_NOFRAG:
        gb_remove(c, &n, fragment); gb_remove(c, &n, fragment); *count = n;
        return 0;
    case TEGU_BENCH_FRAG2:
        if (n + 2 > cap) return -1;
        for (uint32_t i = n; i > fragment + 2; i--) c[i + 1] = c[i - 1];
        c[fragment + 2] = c[fragment]; c[fragment + 3] = c[fragment + 1];
        *count = n + 2;
        return 0;
    default:
        return -1;
    }
}
