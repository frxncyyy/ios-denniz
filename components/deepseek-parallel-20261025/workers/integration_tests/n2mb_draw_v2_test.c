/* Independent n2mb_draw_v2 sidecar/retention test through a fake JM model.
 *
 * Drives the real bridge graphics path with the real n2r_build_v2 sidecar,
 * captures the submitted JM request and command block, verifies the texture
 * descriptor/plane came from the sidecar, and checks that target/binding
 * handles are retained until the modeled completion.  No GPU execution.
 */
#include "nord2_metal_bridge.h"
#include "nord2_gpu_transport.h"
#include "nord2_metal_render.h"
#include "nord2_texture_resources.h"
#include "nord2_preload.h"
#include "genxml/v9_pack.h"
#include "tegu/generic_job_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BO 64u
#define BO_MAX_BYTES (8u * 1024u * 1024u)
#define CMD_BYTES (2u * N2R_BLOCK)
#define SNAPSHOT_BYTES (2u * N2R_BLOCK)

static unsigned failures, checks;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL [required] %s\n", what); failures++; }
}

struct fbo {
    uint32_t handle;
    uint64_t va;
    size_t size;
    uint8_t *host;
    int used, released;
};

struct fixture {
    struct n2mb_ops ops;
    uintptr_t dev;
    struct fbo bo[MAX_BO];
    uint32_t next_handle;
    uint64_t next_va;
    struct nord2_gpu_submit_request request;
    int have_request;
    uint8_t snapshot[SNAPSHOT_BYTES];
    size_t snapshot_bytes;
    uint64_t snapshot_base;
    unsigned submits, waits, syncs;
    uint64_t next_job;
    uint32_t last_alloc_handle;
    int last_alloc_alive_at_wait;
};

static struct fixture F;

static struct fbo *find_handle(uint32_t handle)
{
    unsigned i;
    for (i = 0; i < MAX_BO; i++)
        if (F.bo[i].used && F.bo[i].handle == handle) return &F.bo[i];
    return NULL;
}

static struct fbo *find_va(uint64_t va)
{
    unsigned i;
    for (i = 0; i < MAX_BO; i++)
        if (F.bo[i].used && va >= F.bo[i].va && va < F.bo[i].va + F.bo[i].size)
            return &F.bo[i];
    return NULL;
}

static struct fbo *alloc_bo(size_t request)
{
    unsigned i;
    struct fbo *b = NULL;
    size_t size = (request + 16383u) & ~(size_t)16383u;
    if (!size || size > BO_MAX_BYTES) return NULL;
    for (i = 0; i < MAX_BO; i++)
        if (!F.bo[i].used) { b = &F.bo[i]; break; }
    if (!b) return NULL;
    memset(b, 0, sizeof(*b));
    b->host = calloc(1, size);
    if (!b->host) return NULL;
    b->used = 1; b->handle = ++F.next_handle; b->va = F.next_va;
    F.last_alloc_handle = b->handle;
    F.next_va += 0x8000000ull;
    b->size = size;
    return b;
}

static int fake_call(void *opaque, unsigned sel, const uint64_t *in, unsigned ni,
                     const void *bytes, size_t size, uint64_t *out, unsigned *no,
                     void *outbytes, size_t *outsize)
{
    (void)opaque; (void)outbytes; (void)outsize;
    switch (sel) {
    case NORD2_GPU_SEL_STATUS: {
        struct nord2_gpu_status *s = outbytes;
        if (!s || !outsize || *outsize < sizeof(*s)) return 1;
        memset(s, 0, sizeof(*s));
        s->abi_version = 2; s->enabled = s->powered = s->arch_ok = 1;
        s->gpu_id = 0x90800011; s->tiler_features = 0x809;
        *no = 0; *outsize = sizeof(*s);
        return 0;
    }
    case NORD2_GPU_SEL_ALLOC: {
        struct fbo *b;
        if (ni != 2 || !no || *no < 3) return 1;
        b = alloc_bo((size_t)in[0]);
        if (!b) return 1;
        out[0] = b->handle; out[1] = b->va; out[2] = b->size; *no = 3;
        return 0;
    }
    case NORD2_GPU_SEL_RELEASE: {
        struct fbo *b;
        if (ni != 1) return 1;
        b = find_handle((uint32_t)in[0]);
        if (!b) return 1;
        b->released = 1;
        if (no) *no = 0;
        return 0;
    }
    case NORD2_GPU_SEL_SYNC:
        F.syncs++;
        if (ni != 2) return 1;
        return 0;
    case NORD2_GPU_SEL_SUBMIT_JM: {
        const struct nord2_gpu_submit_request *r = bytes;
        struct fbo *cmd;
        if (size != sizeof(*r) || !r || !no || *no < 1) return 1;
        memcpy(&F.request, r, sizeof(*r));
        F.have_request = 1;
        F.submits++;
        cmd = find_va(r->heads[0]);
        if (!cmd) return 1;
        F.snapshot_base = cmd->va;
        F.snapshot_bytes = cmd->size < SNAPSHOT_BYTES ? cmd->size : SNAPSHOT_BYTES;
        memcpy(F.snapshot, cmd->host, F.snapshot_bytes);
        out[0] = ++F.next_job; *no = 1;
        return 0;
    }
    case NORD2_GPU_SEL_WAIT:
        if (ni != 2 || !no || *no < 1) return 1;
        if (F.last_alloc_handle) {
            struct fbo *tls = find_handle(F.last_alloc_handle);
            F.last_alloc_alive_at_wait = tls && !tls->released;
        }
        F.waits++;
        out[0] = 4; *no = 1;
        return 0;
    default:
        return 1;
    }
}

static int fake_map(void *opaque, unsigned handle, uint64_t *address, uint64_t *size)
{
    struct fbo *b = find_handle(handle);
    (void)opaque;
    if (!b || !address || !size) return 1;
    *address = (uint64_t)(uintptr_t)b->host; *size = b->size;
    return 0;
}

static int fake_unmap(void *opaque, unsigned handle, uint64_t address)
{
    (void)opaque; (void)handle; (void)address;
    return 0;
}

static int fake_close(void *opaque) { (void)opaque; return 0; }
static void fake_lock(void *opaque) { (void)opaque; }
static void fake_unlock(void *opaque) { (void)opaque; }

static void open_fixture(void)
{
    unsigned i;
    memset(&F, 0, sizeof(F));
    F.next_va = 0x1000000000ull;
    F.ops = (struct n2mb_ops){
        .call = fake_call, .map = fake_map, .unmap = fake_unmap,
        .close = fake_close, .lock = fake_lock, .unlock = fake_unlock,
    };
    F.dev = n2mb_open(&F.ops);
    if (!F.dev) { fprintf(stderr, "FATAL n2mb_open\n"); exit(2); }
    for (i = 0; i < MAX_BO; i++) F.bo[i].handle = 0;
}

static void close_fixture(void)
{
    unsigned i;
    if (F.dev) n2mb_close(F.dev);
    for (i = 0; i < MAX_BO; i++) free(F.bo[i].host);
    F.dev = 0;
}

static struct fbo *alloc_resource(size_t size)
{
    uint64_t out[3] = {0, 0, 0}; unsigned n = 3;
    if (n2mb_scalar(F.dev, NORD2_GPU_SEL_ALLOC, (uint64_t[]){size}, 1, out, &n) ||
        n != 2) return NULL;
    return find_handle((uint32_t)out[0]);
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }

static void fill_sidecar(struct n2r_texture_binding *tb, struct n2t_texture_layout *l,
                         struct n2t_sampler_state *s, uint64_t backing_length)
{
    memset(l, 0, sizeof(*l));
    l->version = N2T_TEXTURE_RESOURCES_VERSION;
    l->kind = N2T_KIND_2D;
    l->format = 80;
    l->width = l->height = 16;
    l->depth = l->layers = 1;
    l->levels = l->samples = 1;
    l->usage = 1;
    l->access = N2T_ACCESS_SAMPLE;
    l->level[0].offset = 0;
    l->level[0].row_stride = 64;
    l->level[0].slice_stride = 1024;
    l->level[0].width = 16;
    l->level[0].height = 16;
    l->level[0].depth = 1;
    memset(s, 0, sizeof(*s));
    s->version = N2T_TEXTURE_RESOURCES_VERSION;
    s->min_filter = N2T_FILTER_NEAREST;
    s->mag_filter = N2T_FILTER_NEAREST;
    s->mip_filter = N2T_MIP_NOT_MIPMAPPED;
    s->address_s = s->address_t = s->address_r = N2T_ADDRESS_CLAMP_EDGE;
    s->normalized = 1;
    s->max_anisotropy = 1;
    s->reduction = N2T_REDUCTION_AVERAGE;
    s->lod_algorithm = N2T_LOD_ISOTROPIC;
    tb->binding = 0;
    tb->kind = TEGU_GENERIC_DRAW_COMBINED;
    tb->backing_length = backing_length;
    tb->layout = *l;
    tb->sampler = *s;
}

static int draw_v2(const struct tegu_generic_draw_request *q,
                   const struct n2r_texture_resources *tr, uint64_t out[2])
{
    unsigned count = 2;
    return n2mb_draw_v2(F.dev, &n2_preload_shader, q, tr, out, &count);
}

int main(void)
{
    struct tegu_generic_draw_request q;
    struct n2r_texture_binding tb;
    struct n2t_texture_layout layout;
    struct n2t_sampler_state sampler;
    struct n2r_texture_resources tr;
    struct fbo *target, *texture;
    uint64_t out[2] = {0, 0};
    const uint8_t *cmd;
    unsigned i;

    open_fixture();
    target = alloc_resource(16384);
    texture = alloc_resource(4096);
    check(target && texture, "target and texture allocated");
    if (!target || !texture) { close_fixture(); return 1; }

    memset(&q, 0, sizeof(q));
    q.width = q.height = 16;
    q.row_bytes = 64;
    q.target_format = 80;
    q.load_action = 2;
    q.vertex_count = 3;
    q.scissor[0] = q.scissor[1] = 0;
    q.scissor[2] = q.scissor[3] = 16;
    q.target_token = target->handle;
    q.target_length = 16384;
    q.binding_count = 1;
    q.bindings[0] = (struct tegu_generic_draw_binding){
        .binding = 0, .kind = TEGU_GENERIC_DRAW_COMBINED,
        .memory = {.token = texture->handle, .bytes = 1024},
        .width = 16, .height = 16, .row_bytes = 64, .format = 80,
    };
    fill_sidecar(&tb, &layout, &sampler, 1024);
    tr = (struct n2r_texture_resources){
        .version = N2R_TEXTURE_RESOURCES_VERSION, .count = 1, .bindings = &tb,
    };

    F.submits = F.waits = 0;
    check(draw_v2(&q, &tr, out) == 0 && F.submits == 1 && F.waits == 1,
          "draw_v2 submitted and completed once");
    check(F.have_request && F.request.kind == 2, "graphics JM request captured");
    check(out[0] && out[0] == out[1], "draw_v2 reports one completed job");

    cmd = F.snapshot;
    check(F.snapshot_bytes >= N2R_BLOCK, "command block snapshot captured");
    check((get32(cmd + 4096) & 15u) == 2u, "sidecar table1[0] texture descriptor");
    check(get32(cmd + 4096 + 4) == (15u | (15u << 16)), "sidecar texture width/height");
    check(get64(cmd + 4096 + 16) == F.snapshot_base + N2R_PAYLOAD,
          "sidecar texture descriptor points at its plane payload");
    check(get64(cmd + N2R_PAYLOAD + 8) == texture->va &&
          get64(cmd + N2R_PAYLOAD + 16) == 64,
          "sidecar plane points at texture backing and row stride");
    check(get64(cmd + N2R_PAYLOAD + 24) == 1024, "sidecar plane slice stride");
    check((get64(cmd + 2304 + 16) & ((UINT64_C(1) << 56) - 1)) ==
          F.snapshot_base + 4096u && get64(cmd + 2304 + 24) == 1024u,
          "SRT table1 points at the v2 descriptor table");

    /* Retention: target/texture/code/heap/command handles must be in the
     * submit request; target and heap are write-retained, the sampled texture
     * is not.  The bridge only releases its owned command/code after wait. */
    {
        int target_ref = 0, texture_ref = 0, heap_ref = 0, cmd_ref = 0;
        int target_write = 0, texture_write = 0;
        for (i = 0; i < F.request.nr_handles; i++) {
            unsigned h = F.request.handles[i];
            struct fbo *b = find_handle(h);
            if (!b) continue;
            if (h == target->handle) { target_ref = 1; target_write = (F.request.write_mask >> i) & 1u; }
            if (h == texture->handle) { texture_ref = 1; texture_write = (F.request.write_mask >> i) & 1u; }
            if (b->size >= (2u << 20) && b->size <= (8u << 20)) heap_ref = 1;
            if (b->va == F.snapshot_base) cmd_ref = 1;
        }
        check(target_ref && texture_ref && heap_ref && cmd_ref,
              "target, texture, heap and command retained in the JM request");
        check(target_write && !texture_write,
              "write mask retains target write and leaves sampled texture read-only");
    }

    /* The app can release its handles only after modeled completion; both
     * releases must still find the bridge's live handle table. */
    {
        unsigned n = 0;
        uint64_t handle = texture->handle;
        check(n2mb_scalar(F.dev, NORD2_GPU_SEL_RELEASE, &handle, 1, NULL, &n) == 0,
              "texture release after completion");
        n = 0; handle = target->handle;
        check(n2mb_scalar(F.dev, NORD2_GPU_SEL_RELEASE, &handle, 1, NULL, &n) == 0,
              "target release after completion");
    }

    /* Negative boundaries: bad sidecar backing span and kind mismatch must
     * refuse before any submit with output untouched. */
    {
        uint64_t bad_out[2] = {0x1111111111111111ull, 0x2222222222222222ull};
        uint64_t before[2];
        memcpy(before, bad_out, sizeof(before));
        tb.backing_length = 1023;
        F.submits = 0;
        check(draw_v2(&q, &tr, bad_out) != 0 && F.submits == 0 &&
              !memcmp(before, bad_out, sizeof(before)),
              "short sidecar backing length refused before submit with output unchanged");
        tb.backing_length = 1024;
        tb.kind = TEGU_GENERIC_DRAW_TEXTURE;
        F.submits = 0;
        check(draw_v2(&q, &tr, bad_out) != 0 && F.submits == 0,
              "sidecar/request kind mismatch refused");
        tb.kind = TEGU_GENERIC_DRAW_COMBINED;
        F.submits = 0;
        check(n2mb_draw_v2(F.dev, &n2_preload_shader, &q, NULL, bad_out,
                           &(unsigned){2}) != 0 && F.submits == 0,
              "NULL sidecar refused by draw_v2");
    }

    /* draw v3: vertex/base-instance FAU words and first-provoking flat state. */
    {
        struct n2r_draw_options opts;
        struct tegu_generic_draw_shader v3shader = n2_preload_shader;
        struct fbo *v3target = alloc_resource(16384), *v3texture = alloc_resource(4096);
        uint64_t v3out[2] = {0, 0};
        check(v3target && v3texture, "draw_v3 resources allocated");
        q.target_token = v3target->handle;
        q.bindings[0].memory.token = v3texture->handle;
        unsigned v3count = 2;
        v3shader.vertex_fau_words = 20;
        memset(&opts, 0, sizeof(opts));
        opts.version = N2R_DRAW_OPTIONS_VERSION;
        opts.flags = N2R_DRAW_WRITE_VERTEX_BASE | N2R_DRAW_WRITE_BASE_INSTANCE;
        opts.vertex_base = 3;
        opts.base_instance = 2;
        F.submits = 0;
        check(n2mb_draw_v3(F.dev, &v3shader, &q, &tr, &opts,
                           v3out, &v3count) == 0 && F.submits == 1,
              "draw_v3 vertex/base-instance dispatch");
        {
            uint32_t vb = 0, bi = 0;
            memcpy(&vb, F.snapshot + 1664 + 64, 4);
            memcpy(&bi, F.snapshot + 1664 + 68, 4);
            check(vb == 3u, "draw_v3 vertex_base FAU word");
            check(bi == 2u, "draw_v3 base_instance FAU word");
        }
        opts.vertex_base = -3;
        opts.base_instance = 5;
        F.submits = 0;
        check(n2mb_draw_v3(F.dev, &v3shader, &q, &tr, &opts,
                           v3out, &v3count) == 0 && F.submits == 1,
              "draw_v3 signed vertex/base-instance dispatch");
        {
            uint32_t vb = 0, bi = 0;
            memcpy(&vb, F.snapshot + 1664 + 64, 4);
            memcpy(&bi, F.snapshot + 1664 + 68, 4);
            check(vb == 0xfffffffdU, "draw_v3 signed vertex_base FAU word");
            check(bi == 5u, "draw_v3 second base_instance FAU word");
        }
        opts.flags |= N2R_DRAW_FLAT_SHADING;
        F.submits = 0;
        check(n2mb_draw_v3(F.dev, &v3shader, &q, &tr, &opts,
                           v3out, &v3count) == 0 && F.submits == 1,
              "draw_v3 flat triangle dispatch");
        {
            pan_unpack((const struct mali_tiler_context_packed *)(F.snapshot + 512), TILER_CONTEXT, t);
            check(t.first_provoking_vertex == 1, "flat triangle first_provoking_vertex");
        }
        /* arch9 has no line-DRAW flat bit; this must be an explicit refusal. */
        opts.flags |= N2R_DRAW_FLAT_SHADING;
        q.primitive = TEGU_GENERIC_DRAW_LINES;
        q.vertex_count = 4;
        F.submits = 0;
        if (n2mb_draw_v3(F.dev, &v3shader, &q, &tr, &opts,
                         v3out, &v3count) == 0 || F.submits) {
            checks++;
            fprintf(stderr, "FAIL [required] flat line topology accepted instead of explicit refusal\n");
            failures++;
        } else checks++;
        q.primitive = TEGU_GENERIC_DRAW_TRIANGLES;
        q.vertex_count = 3;
    }


    /* True-MRT v4: four attachments, distinct formats/masks, version2 blends. */
    {
        struct n2mb_render_target targets[4];
        struct tegu_blend_inputs blends[4] = {
            {1, 1, 0, 1, 5, 0, 1, 5, 15},
            {1, 0, 0, 1, 0, 0, 1, 0, 3},
            {1, 0, 0, 1, 0, 0, 1, 0, 8},
            {1, 0, 0, 1, 0, 0, 1, 0, 15},
        };
        struct n2mb_render_targets rts;
        struct n2r_texture_resources empty = {
            .version = N2R_TEXTURE_RESOURCES_VERSION, .count = 0, .bindings = NULL,
        };
        struct n2r_draw_options opts;
        struct fbo *rt[4];
        uint64_t mout[2] = {0, 0};
        unsigned count = 2;
        const uint32_t formats[4] = {80, 80, 10, 80};
        const uint32_t masks[4] = {15, 3, 8, 15};
        const unsigned sizes[4] = {16384, 16384, 16384, 16384};

        memset(&opts, 0, sizeof(opts));
        opts.version = N2R_DRAW_OPTIONS_VERSION;
        memset(&rts, 0, sizeof(rts));
        rts.version = N2MB_RENDER_TARGETS_BLEND_VERSION;
        rts.count = 4;
        rts.targets = targets;
        rts.blends = blends;
        F.submits = 0;
        for (unsigned i = 0; i < 4; i++) {
            rt[i] = alloc_resource(sizes[i]);
            check(rt[i] != NULL, "four MRT targets allocated");
            if (!rt[i]) { close_fixture(); return 1; }
            targets[i] = (struct n2mb_render_target){
                .token = rt[i]->handle, .offset = 0, .length = sizes[i],
                .format = formats[i], .load_action = 2, .write_mask = masks[i],
                .row_bytes = 64, .width = 16, .height = 16,
                .clear_words = {0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u},
            };
        }
        q.target_token = rt[0]->handle;
        q.target_length = sizes[0];
        q.target_format = 80;
        q.load_action = 2;
        q.binding_count = 0;
        F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &empty, &opts,
                           &rts, mout, &count) == 0 && F.submits == 1,
              "true-MRT v4 four-target dispatch");
        check(F.snapshot_bytes >= N2R_BLOCK, "true-MRT command snapshot");
        {
            pan_unpack((const struct mali_framebuffer_parameters_packed *)F.snapshot,
                       FRAMEBUFFER_PARAMETERS, fb);
            check(fb.render_target_count == 4u, "MRT framebuffer target count");
            check(fb.color_buffer_allocation >= 1024u,
                  "MRT framebuffer color budget");
        }
        for (unsigned i = 0; i < 4; i++) {
            pan_unpack((const struct mali_rgb_render_target_packed *)
                         (F.snapshot + 128 + 64 * i), RGB_RENDER_TARGET, t);
            check(t.writeback_buffer.base == rt[i]->va &&
                  t.writeback_buffer.row_stride == 64 &&
                  t.clear.color_0 == targets[i].clear_words[0],
                  "MRT target descriptor format/address/clear");
            check(t.writeback_format == (formats[i] == 10
                  ? MALI_COLOR_FORMAT_R8 : MALI_COLOR_FORMAT_R8G8B8A8),
                  "MRT target descriptor writeback format");
            check(t.write_enable == 1, "MRT target write enable");
        }
        {
            pan_unpack((const struct mali_draw_packed *)(F.snapshot + N2R_JOB + 128),
                       DRAW, d);
            check(d.blend_count == 4u, "MRT DRAW blend_count");
            check(d.flags_1.render_target_mask == 0x0fu, "MRT DRAW target mask");
            check(d.blend == F.snapshot_base + 928u, "MRT DRAW blend pointer");
        }
        for (unsigned i = 0; i < 4; i++) {
            uint32_t metal_mask = masks[i];
            uint32_t mali_mask = ((metal_mask & 8u) >> 3) |
                                 ((metal_mask & 4u) >> 1) |
                                 ((metal_mask & 2u) << 1) |
                                 ((metal_mask & 1u) << 3);
            pan_unpack((const struct mali_blend_packed *)(F.snapshot + 928 + 16 * i),
                       BLEND, b);
            check(b.equation.color_mask == mali_mask, "MRT per-target blend mask");
            check(b.internal.fixed_function.rt == i, "MRT per-target blend rt stamp");
            if (i == 0) check(b.load_destination == 1,
                             "MRT target0 source-over reads destination");
            if (i == 3) check(b.load_destination == 0,
                             "MRT target3 opaque does not read destination");
        }
        {
            int all_refs = 1, all_write = 1;
            for (unsigned i = 0; i < 4; i++) {
                int found = 0;
                for (unsigned h = 0; h < F.request.nr_handles; h++) {
                    if (F.request.handles[h] == rt[i]->handle) {
                        found = 1;
                        if (!((F.request.write_mask >> h) & 1u)) all_write = 0;
                    }
                }
                if (!found) all_refs = 0;
            }
            check(all_refs, "all four MRT target BOs retained");
            check(all_write, "all four MRT targets write-retained");
        }
        /* v2 must fail closed before submit for missing or mismatched blends. */
        {
            uint64_t canary[2] = {0x1111222233334444ull, 0x5555666677778888ull};
            uint64_t before[2];
            memcpy(before, canary, sizeof(before));
            rts.blends = NULL;
            F.submits = 0;
            check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &empty, &opts,
                               &rts, canary, &count) != 0 && F.submits == 0 &&
                  !memcmp(before, canary, sizeof(before)),
                  "MRT v2 NULL blend array refused before submit");
            rts.blends = blends;
            blends[1].write_mask = 15;
            F.submits = 0;
            check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &empty, &opts,
                               &rts, canary, &count) != 0 && F.submits == 0 &&
                  !memcmp(before, canary, sizeof(before)),
                  "MRT v2 mismatched blend mask refused before submit");
            blends[1].write_mask = 3;
        }
    }

    /* Graphics TLS: IDVS position/varying and fragment jobs share the
     * LOCAL_STORAGE descriptor; the TLS BO must be read-retained and live at
     * wait. */
    {
        struct n2r_draw_options opts;
        struct fbo *tls_target = alloc_resource(16384);
        struct n2r_texture_resources empty = {
            .version = N2R_TEXTURE_RESOURCES_VERSION, .count = 0, .bindings = NULL,
        };
        uint64_t tout[2] = {0, 0};
        unsigned count = 2;
        check(tls_target != NULL, "graphics TLS target allocated");
        if (!tls_target) { close_fixture(); return 1; }
        memset(&opts, 0, sizeof(opts));
        opts.version = N2R_DRAW_OPTIONS_VERSION;
        opts.thread_storage_bytes = 32;
        q.target_token = tls_target->handle;
        q.target_length = 16384;
        q.target_format = 80;
        q.load_action = 2;
        q.binding_count = 0;
        F.last_alloc_handle = 0;
        F.last_alloc_alive_at_wait = 0;
        F.submits = 0;
        check(n2mb_draw_v3(F.dev, &n2_preload_shader, &q, &empty, &opts,
                           tout, &count) == 0 && F.submits == 1,
              "graphics TLS v3 dispatch");
        uint64_t expected_tls_total = 0;
        check(n2r_thread_storage_total(32, 2048, 16, &expected_tls_total) == 0 &&
              expected_tls_total != 0 && find_handle(F.last_alloc_handle) &&
              find_handle(F.last_alloc_handle)->size ==
                  (expected_tls_total + 16383u) / 16384u * 16384u,
              "graphics TLS allocation uses the transport size formula");
        check(F.last_alloc_alive_at_wait == 1,
              "graphics TLS BO alive at modeled wait");
        check(F.last_alloc_handle != 0 &&
              find_handle(F.last_alloc_handle) &&
              find_handle(F.last_alloc_handle)->released,
              "graphics TLS BO released after wait");
        {
            int tls_ref = 0, tls_write = 0;
            for (unsigned h = 0; h < F.request.nr_handles; h++)
                if (F.request.handles[h] == F.last_alloc_handle) {
                    tls_ref = 1; tls_write = (F.request.write_mask >> h) & 1u;
                }
            check(tls_ref && !tls_write, "graphics TLS BO read-retained");
        }
        {
            pan_unpack((const struct mali_local_storage_packed *)(F.snapshot + 832),
                       LOCAL_STORAGE, ls);
            check(ls.tls_size == n2r_thread_storage_shift(32),
                  "graphics TLS LOCAL_STORAGE tls_size");
            check(ls.tls_base_pointer != 0 &&
                  ls.tls_address_mode == MALI_ADDRESS_MODE_PACKED,
                  "graphics TLS LOCAL_STORAGE base/address mode");
        }
        {
            pan_unpack((const struct mali_shader_environment_packed *)
                         (F.snapshot + N2R_JOB + 256), SHADER_ENVIRONMENT, vs);
            check(vs.thread_storage == F.snapshot_base + 832u,
                  "graphics TLS IDVS position thread_storage");
        }
        {
            pan_unpack((const struct mali_shader_environment_packed *)
                         (F.snapshot + N2R_JOB + 320), SHADER_ENVIRONMENT, vs);
            check(vs.thread_storage == F.snapshot_base + 832u,
                  "graphics TLS IDVS varying thread_storage");
        }
        {
            pan_unpack((const struct mali_draw_packed *)(F.snapshot + N2R_JOB + 128),
                       DRAW, d);
            check(d.shader.thread_storage == F.snapshot_base + 832u,
                  "graphics TLS fragment thread_storage");
        }
        /* Above the declared arch9 immediate bound must refuse, no submit. */
        opts.thread_storage_bytes = N2R_THREAD_STORAGE_MAX + 1u;
        F.submits = 0;
        check(n2mb_draw_v3(F.dev, &n2_preload_shader, &q, &empty, &opts,
                           tout, &count) != 0 && F.submits == 0,
              "graphics TLS over-limit refused before submit");
    }
    /* Restore the single sampled binding for the legacy-regression block. */
    q.binding_count = 1;
    q.bindings[0].memory.token = texture->handle;
    q.bindings[0].memory.bytes = 1024;

    /* Keep the old v10/legacy expectation separately: the legacy n2mb_draw
     * path still emits the qualified 16x16 plane tail 0x000f000f, while v2
     * sidecar planes use arch9 word7=0. */
    {
        struct fbo *legacy_target = alloc_resource(16384);
        struct fbo *legacy_texture = alloc_resource(4096);
        struct tegu_generic_draw_request lq = q;
        uint64_t legacy_out[2] = {0, 0};
        unsigned count = 2;
        int e;
        check(legacy_target && legacy_texture, "legacy target/texture allocated");
        if (!legacy_target || !legacy_texture) { close_fixture(); return 1; }
        lq.target_token = legacy_target->handle;
        lq.bindings[0].memory.token = legacy_texture->handle;
        e = n2mb_draw(F.dev, &n2_preload_shader, &lq, legacy_out, &count);
        check(e == 0 && F.submits > 0, "legacy n2mb_draw still succeeds");
        check(F.snapshot_bytes >= N2R_BLOCK, "legacy command block captured");
        check(get32(F.snapshot + 6144 + 28) == 0x000f000fu,
              "legacy v10 plane word7 preserved separately");
        check(get32(F.snapshot + 6144 + 28) != 0u,
              "legacy plane tail is nonzero and not waived");
    }

    printf("n2mb draw_v2: %u checks, %u required failures, 0 pending failures\n", checks, failures);
    close_fixture();
    return failures ? 1 : 0;
}
