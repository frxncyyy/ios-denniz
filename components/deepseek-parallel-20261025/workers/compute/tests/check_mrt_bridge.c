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
        s->shader_present_lo = 1;
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

/* The fake transport does not recycle slots on RELEASE; test-local resources
 * are freed only after every draw that names them has completed. */
static void release_test_resource(struct fbo *b)
{
    if (!b) return;
    free(b->host); b->host = NULL; memset(b, 0, sizeof(*b));
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

    /* Additive true-MRT target-token path: RT0/RT1 BGRA8 + RT2 R8, only RT2
     * shaded.  The bridge must resolve/retain all target BOs, pass the actual
     * per-target rows/addresses to n2r_build_v4, and preserve v3 defaults. */
    {
        struct fbo *t0 = alloc_resource(16384), *t1 = alloc_resource(16384), *t2 = alloc_resource(4096);
        struct fbo *t3 = alloc_resource(16384), *mtex = alloc_resource(4096);
        struct n2mb_render_target mt[4];
        struct n2mb_render_targets mts;
        struct tegu_blend_inputs mb[4];
        uint8_t v1pkt[N2R_BLOCK];
        struct n2r_draw_options mut_opts = {.version = N2R_DRAW_OPTIONS_VERSION};
        uint64_t mout[2] = {0, 0};
        unsigned mcount = 2;
        check(t0 && t1 && t2 && t3 && mtex, "MRT target BOs allocated");
        if (!t0 || !t1 || !t2 || !t3 || !mtex) { close_fixture(); return 1; }
        memset(mt, 0, sizeof(mt));
        mt[0] = (struct n2mb_render_target){.token = t0->handle, .offset = 0, .length = 16384,
            .format = 80, .load_action = 2, .write_mask = 0, .row_bytes = 64,
            .width = 16, .height = 16, .clear_words = {0xff0000ffu, 0, 0, 0}};
        mt[1] = (struct n2mb_render_target){.token = t1->handle, .offset = 0, .length = 16384,
            .format = 80, .load_action = 2, .write_mask = 0, .row_bytes = 64,
            .width = 16, .height = 16, .clear_words = {0xff00ff00u, 0, 0, 0}};
        mt[2] = (struct n2mb_render_target){.token = t2->handle, .offset = 0, .length = 4096,
            .format = 10, .load_action = 2, .write_mask = 15, .row_bytes = 64,
            .width = 16, .height = 16, .clear_words = {0x7fu, 0, 0, 0}};
        mt[3] = (struct n2mb_render_target){.token = t3->handle, .offset = 0, .length = 16384,
            .format = 80, .load_action = 2, .write_mask = 0, .row_bytes = 64,
            .width = 16, .height = 16, .clear_words = {0xff000000u, 0, 0, 0}};
        mb[0] = (struct tegu_blend_inputs){1,0,0,1,0,1,0,0,0};
        mb[1] = (struct tegu_blend_inputs){1,0,0,1,0,1,0,0,0};
        mb[2] = (struct tegu_blend_inputs){1,1,0,4,1,0,4,1,15};
        mb[3] = (struct tegu_blend_inputs){1,0,0,1,0,1,0,0,0};
        mts = (struct n2mb_render_targets){.version = N2MB_RENDER_TARGETS_VERSION,
            .count = 4, .targets = mt};
        q.target_format = 80; q.target_token = t0->handle; q.target_length = 16384;
        q.target_offset = 0; q.load_action = 2; q.row_bytes = 64;
        q.bindings[0].memory.token = mtex->handle;
        F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                           mout, &mcount) == 0 && F.submits == 1,
              "draw_v4 true-MRT dispatch");
        check(F.snapshot_bytes >= N2R_BLOCK, "MRT command block captured");
        memcpy(v1pkt, F.snapshot, N2R_BLOCK);
        {
            pan_unpack((const struct mali_framebuffer_parameters_packed *)(F.snapshot),
                       FRAMEBUFFER_PARAMETERS, fb);
            pan_unpack((const struct mali_rgb_render_target_packed *)(F.snapshot + 128),
                       RGB_RENDER_TARGET, rt0);
            pan_unpack((const struct mali_rgb_render_target_packed *)(F.snapshot + 192),
                       RGB_RENDER_TARGET, rt1);
            pan_unpack((const struct mali_rgb_render_target_packed *)(F.snapshot + 256),
                       RGB_RENDER_TARGET, rt2);
            pan_unpack((const struct mali_rgb_render_target_packed *)(F.snapshot + 320),
                       RGB_RENDER_TARGET, rt3);
            pan_unpack((const struct mali_draw_packed *)(F.snapshot + N2R_JOB + 128),
                       DRAW, draw);
            check(fb.render_target_count == 4, "MRT framebuffer count");
            check(rt0.writeback_buffer.base == t0->va &&
                  rt1.writeback_buffer.base == t1->va &&
                  rt2.writeback_buffer.base == t2->va &&
                  rt3.writeback_buffer.base == t3->va,
                  "MRT per-target writeback addresses");
            check(rt2.writeback_format == MALI_COLOR_FORMAT_R8 &&
                  rt2.writeback_buffer.row_stride == 64,
                  "MRT RT2 R8 format/row");
            check(draw.flags_1.render_target_mask == 4u,
                  "MRT shader writes only RT2");
            check(get32(F.snapshot + 12288) == 0x00800080u,
                  "MRT sample locations payload");
        }
        {
            int r0 = 0, r1 = 0, r2 = 0, r3 = 0, w0 = 0, w1 = 0, w2 = 0, w3 = 0;
            for (i = 0; i < F.request.nr_handles; i++) {
                unsigned h = F.request.handles[i];
                if (h == t0->handle) { r0 = 1; w0 = (F.request.write_mask >> i) & 1u; }
                if (h == t1->handle) { r1 = 1; w1 = (F.request.write_mask >> i) & 1u; }
                if (h == t2->handle) { r2 = 1; w2 = (F.request.write_mask >> i) & 1u; }
                if (h == t3->handle) { r3 = 1; w3 = (F.request.write_mask >> i) & 1u; }
            }
            check(r0 && r1 && r2 && r3 && w0 && w1 && w2 && w3,
                  "all MRT target BOs retained write in the JM request");
        }
        /* Version 1 with a non-NULL blends pointer must ignore it and emit
         * the exact old packet. */
        mts.blends = mb; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                           mout, &mcount) == 0 && F.submits == 1 &&
              !memcmp(v1pkt + 928, F.snapshot + 928, 64),
              "v4 version1 ignores blends pointer in all BLEND descriptors");
        /* Version 2 carries per-target blends through to n2r's BLEND array. */
        mts.version = N2MB_RENDER_TARGETS_BLEND_VERSION; mts.blends = mb;
        F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                           mout, &mcount) == 0 && F.submits == 1,
              "v4 version2 per-target blends dispatch");
        {
            MALI_BLEND_PACKED_T bp; struct MALI_BLEND bl;
            memcpy(&bp, F.snapshot + 928 + 16 * 2, sizeof(bp));
            MALI_BLEND_unpack(&bp, &bl);
            check(bl.equation.color_mask == 15u && bl.enable,
                  "v4 version2 RT2 blend descriptor mask/enable");
            memcpy(&bp, F.snapshot + 928, sizeof(bp));
            MALI_BLEND_unpack(&bp, &bl);
            check(bl.equation.color_mask == 0u,
                  "v4 version2 RT0 blend descriptor mask");
            check(memcmp(F.snapshot + 928, F.snapshot + 928 + 16 * 2, 16) != 0,
                  "v4 version2 per-target blend bytes are distinct");
        }
        /* Version2 NULL/mismatch/invalid records refuse before submit. */
        mts.blends = NULL; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                           mout, &mcount) != 0 && F.submits == 0,
              "v4 version2 NULL blends refused");
        mts.blends = mb; mb[2].write_mask = 0; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                           mout, &mcount) != 0 && F.submits == 0,
              "v4 version2 blend/target mask mismatch refused");
        mb[2].write_mask = 15; mb[2].enabled = 2; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                           mout, &mcount) != 0 && F.submits == 0,
              "v4 version2 invalid blend equation refused");
        mb[2].enabled = 1;
        /* Unbound RT0/RT1 are represented by the existing per-RT one-target
         * path.  RT2 is R8; the bridge routes this through the v4 target path
         * because the legacy row check assumes four bytes per texel. */
        q.target_format = 10; q.target_token = t2->handle; q.width = q.height = 64;
        q.target_length = 4096; q.target_offset = 0; q.row_bytes = 64;
        q.scissor[0] = q.scissor[1] = 0; q.scissor[2] = q.scissor[3] = 16;
        q.load_action = 2;
        F.submits = 0;
        check(n2mb_draw_v3(F.dev, &n2_preload_shader, &q, &tr, &mut_opts,
                           mout, &mcount) == 0 && F.submits == 1,
              "v3 RT2-only R8 target dispatch");
        {
            pan_unpack((const struct mali_framebuffer_parameters_packed *)(F.snapshot),
                       FRAMEBUFFER_PARAMETERS, fb);
            pan_unpack((const struct mali_rgb_render_target_packed *)(F.snapshot + 128),
                       RGB_RENDER_TARGET, rt);
            check(fb.render_target_count == 1 && rt.writeback_buffer.base == t2->va &&
                  rt.writeback_format == MALI_COLOR_FORMAT_R8 &&
                  rt.writeback_buffer.row_stride == 64,
                  "v3 R8 one-target packet uses actual target row");
        }
        /* The per-RT R8 Load path must not use the four-byte-row snapshot
         * preflight; it builds a one-target v4 descriptor with the actual row. */
        q.load_action = 1; F.submits = 0;
        check(n2mb_draw_v3(F.dev, &n2_preload_shader, &q, &tr, &mut_opts,
                           mout, &mcount) == 0 && F.submits == 1,
              "v3 RT2-only R8 Load dispatch");
        {
            pan_unpack((const struct mali_rgb_render_target_packed *)(F.snapshot + 128),
                       RGB_RENDER_TARGET, rt);
            check(!rt.clean_tile_write_enable && rt.writeback_buffer.row_stride == 64,
                  "R8 Load target descriptor preserves memory");
        }
        q.load_action = 2;
        /* True-MRT LOAD has no per-target snapshot transport yet; the public
         * sidecar must refuse instead of silently loading the wrong memory. */
        mt[0].load_action = 1; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                           mout, &mcount) != 0 && F.submits == 0,
              "true-MRT Load without snapshots refused");
        mt[0].load_action = 2;
        /* Refusals: duplicate address, short span and mismatched target size
         * must not submit. */
        {
            struct n2mb_render_target bad = mt[2];
            bad.token = t0->handle;
            mt[2] = bad; F.submits = 0;
            check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                               mout, &mcount) != 0 && F.submits == 0,
                  "duplicate MRT target address refused");
            mt[2] = (struct n2mb_render_target){.token = t2->handle, .offset = 0,
                .length = 512, .format = 10, .load_action = 2, .write_mask = 15,
                .row_bytes = 64, .width = 16, .height = 16};
            F.submits = 0;
            check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                               mout, &mcount) != 0 && F.submits == 0,
                  "short MRT target span refused");
            mt[2] = (struct n2mb_render_target){.token = t2->handle, .offset = 0,
                .length = 4096, .format = 10, .load_action = 2, .write_mask = 15,
                .row_bytes = 64, .width = 8, .height = 16};
            F.submits = 0;
            check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &mut_opts, &mts,
                               mout, &mcount) != 0 && F.submits == 0,
                  "MRT target width mismatch refused");
        }
    }

    /* Version 3 MSAA: sample_counts and resolve targets are resolved through
     * the bridge's BO table, retained write, and forwarded to n2r. */
    {
        struct fbo *st[4] = {0}, *sr[2] = {0}, *sx = 0;
        struct n2mb_render_target stt[4];
        struct n2mb_resolve_target sr3[4];
        struct n2mb_render_targets smts;
        uint32_t sc[4] = {4, 4, 4, 4};
        uint64_t smout[2] = {0, 0};
        unsigned smc = 2, i, j;
        struct n2r_draw_options sopt = {.version = N2R_DRAW_OPTIONS_VERSION};
        for (i = 0; i < 4; i++) st[i] = alloc_resource(16384);
        for (i = 0; i < 2; i++) sr[i] = alloc_resource(4096);
        sx = alloc_resource(4096);
        check(st[0] && st[1] && st[2] && st[3] && sr[0] && sr[1] && sx,
              "MSAA target/resolve BOs allocated");
        if (!st[0] || !st[1] || !st[2] || !st[3] || !sr[0] || !sr[1] || !sx) {
            close_fixture(); return 1;
        }
        memset(stt, 0, sizeof(stt)); memset(sr3, 0, sizeof(sr3));
        for (i = 0; i < 4; i++)
            stt[i] = (struct n2mb_render_target){.token = st[i]->handle, .offset = 0,
                .length = 16384, .format = 80, .load_action = 2, .write_mask = 15,
                .row_bytes = 64, .width = 16, .height = 16,
                .clear_words = {0xff000000u, 0, 0, 0}};
        for (i = 0; i < 2; i++)
            sr3[i] = (struct n2mb_resolve_target){.token = sr[i]->handle, .offset = 0,
                .length = 4096, .format = 0, .row_bytes = 64, .width = 16, .height = 16};
        smts = (struct n2mb_render_targets){.version = N2MB_RENDER_TARGETS_MSAA_VERSION,
            .count = 4, .targets = stt, .sample_counts = sc, .resolves = sr3};
        q.target_format = 80; q.target_token = st[0]->handle; q.target_length = 16384;
        q.target_offset = 0; q.load_action = 2; q.row_bytes = 64;
        q.width = q.height = 16; q.scissor[0] = q.scissor[1] = 0;
        q.scissor[2] = q.scissor[3] = 16; q.bindings[0].memory.token = sx->handle;
        F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) == 0 && F.submits == 1,
              "draw_v4 v3 sample4+resolve dispatch");
        pan_unpack((const struct mali_framebuffer_parameters_packed *)(F.snapshot),
                   FRAMEBUFFER_PARAMETERS, fb);
        pan_unpack((const struct mali_rgb_render_target_packed *)(F.snapshot + 128),
                   RGB_RENDER_TARGET, rt0);
        pan_unpack((const struct mali_rgb_render_target_packed *)(F.snapshot + 256),
                   RGB_RENDER_TARGET, rt2);
        check(fb.sample_count == 4 && fb.sample_pattern == MALI_SAMPLE_PATTERN_ROTATED_4X_GRID,
              "v3 framebuffer sample count/pattern");
        check(rt0.writeback_msaa == MALI_MSAA_AVERAGE &&
              rt0.writeback_buffer.base == sr[0]->va &&
              rt0.writeback_buffer.row_stride == 64,
              "v3 resolve0 AVERAGE base/row");
        check(rt2.writeback_msaa == MALI_MSAA_LAYERED &&
              rt2.writeback_buffer.base == st[2]->va,
              "v3 no-resolve target LAYERED writeback");
        check(get32(F.snapshot + 12288) == 0x00500070u,
              "v3 rotated4x sample LUT at expected span");
        {
            unsigned tseen[4] = {0}, rseen[2] = {0}, tw[4] = {0}, rw[2] = {0};
            for (i = 0; i < F.request.nr_handles; i++) {
                unsigned h = F.request.handles[i];
                for (j = 0; j < 4; j++) if (h == st[j]->handle) {
                    tseen[j] = 1; tw[j] = (F.request.write_mask >> i) & 1u;
                }
                for (j = 0; j < 2; j++) if (h == sr[j]->handle) {
                    rseen[j] = 1; rw[j] = (F.request.write_mask >> i) & 1u;
                }
            }
            check(tseen[0] && tseen[1] && tseen[2] && tseen[3] &&
                  tw[0] && tw[1] && tw[2] && tw[3] && rseen[0] && rseen[1] && rw[0] && rw[1],
                  "v3 targets and resolves retained write");
        }
        /* v3 refusals happen before submission. */
        smts.sample_counts = NULL; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) != 0 && F.submits == 0,
              "v3 NULL sample_counts refused");
        smts.sample_counts = sc; sc[1] = 1; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) != 0 && F.submits == 0,
              "v3 mixed sample counts refused");
        sc[1] = 4;
        sc[0] = sc[1] = sc[2] = sc[3] = 1; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) != 0 && F.submits == 0,
              "v3 resolve with sample1 refused");
        sc[0] = sc[1] = sc[2] = sc[3] = 4;
        sr3[0].format = 10; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) != 0 && F.submits == 0,
              "v3 resolve format mismatch refused");
        sr3[0].format = 0; sr3[0].width = 8; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) != 0 && F.submits == 0,
              "v3 resolve width mismatch refused");
        sr3[0].width = 16; sr3[0].token = st[0]->handle; sr3[0].length = 16384;
        F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) != 0 && F.submits == 0,
              "v3 resolve/target address alias refused");
        sr3[0].token = sr[0]->handle; sr3[0].length = 512; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) != 0 && F.submits == 0,
              "v3 short resolve span refused");
        sr3[0].length = 4096;
        /* No-resolve sample4 and sample1 variants remain explicit and exact. */
        smts.resolves = NULL; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) == 0 && F.submits == 1,
              "v3 sample4 without resolves dispatch");
        sc[0] = sc[1] = sc[2] = sc[3] = 1; F.submits = 0;
        check(n2mb_draw_v4(F.dev, &n2_preload_shader, &q, &tr, &sopt, &smts,
                           smout, &smc) == 0 && F.submits == 1,
              "v3 sample1 without resolves dispatch");
        for (i = 0; i < 4; i++) release_test_resource(st[i]);
        for (i = 0; i < 2; i++) release_test_resource(sr[i]);
        release_test_resource(sx);
    }

    /* Graphics THREAD_STORAGE: the bridge owns the per-thread BO, passes
     * n2r the resolved base, retains it in the JM request, and releases it
     * after completion.  TLS0 remains the old packet. */
    {
        struct fbo *tt = alloc_resource(16384), *tx = alloc_resource(4096);
        struct n2r_draw_options to = {.version = N2R_DRAW_OPTIONS_VERSION,
            .thread_storage_bytes = 64};
        struct tegu_generic_draw_request tq;
        uint64_t tout[2] = {0, 0}, total = 0, tls_va = 0;
        unsigned tc = 2;
        check(tt && tx, "graphics TLS target/texture allocated");
        if (!tt || !tx) { close_fixture(); return 1; }
        memset(&tq, 0, sizeof(tq));
        tq.width = tq.height = 16; tq.row_bytes = 64; tq.target_format = 80;
        tq.load_action = 2; tq.vertex_count = 3;
        tq.scissor[0] = tq.scissor[1] = 0; tq.scissor[2] = tq.scissor[3] = 16;
        tq.target_token = tt->handle; tq.target_length = 16384;
        tq.binding_count = 1;
        tq.bindings[0] = (struct tegu_generic_draw_binding){.binding = 0,
            .kind = TEGU_GENERIC_DRAW_COMBINED,
            .memory = {.token = tx->handle, .bytes = 1024},
            .width = 16, .height = 16, .row_bytes = 64, .format = 80};
        check(n2r_thread_storage_total(64, 2048, 1, &total) == 0 && total == 131072,
              "n2r TLS total helper for 64B/2048/core");
        F.submits = 0;
        check(n2mb_draw_v3(F.dev, &n2_preload_shader, &tq, &tr, &to,
                           tout, &tc) == 0 && F.submits == 1,
              "graphics TLS draw submitted");
        {
            MALI_LOCAL_STORAGE_PACKED_T lp;
            struct MALI_LOCAL_STORAGE ls;
            MALI_DRAW_PACKED_T dp;
            struct MALI_DRAW dr;
            MALI_SHADER_ENVIRONMENT_PACKED_T ep;
            struct MALI_SHADER_ENVIRONMENT env;
            struct fbo *tlsbo = 0;
            int in_req = 0, tls_write = 0;
            memcpy(lp.opaque, F.snapshot + 832, 32);
            MALI_LOCAL_STORAGE_unpack(&lp, &ls);
            tls_va = ls.tls_base_pointer << 8;
            tlsbo = find_va(tls_va);
            check(ls.tls_size == 2 && ls.tls_address_mode == MALI_ADDRESS_MODE_PACKED &&
                  ls.tls_base_pointer == (tls_va >> 8) && tlsbo &&
                  tlsbo->size >= total &&
                  ls.wls_instances == MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM,
                  "graphics TLS LOCAL_STORAGE bytes/base/size");
            memcpy(&dp, F.snapshot + N2R_JOB + 128, sizeof(dp));
            MALI_DRAW_unpack(&dp, &dr);
            memcpy(&ep, F.snapshot + N2R_JOB + 256, sizeof(ep));
            MALI_SHADER_ENVIRONMENT_unpack(&ep, &env);
            check(dr.shader.thread_storage == F.snapshot_base + 832 &&
                  env.thread_storage == F.snapshot_base + 832,
                  "graphics TLS descriptor shared by DRAW and SHADER_ENVIRONMENT");
            for (i = 0; i < F.request.nr_handles; i++)
                if (tlsbo && F.request.handles[i] == tlsbo->handle) {
                    in_req = 1; tls_write = (F.request.write_mask >> i) & 1u;
                }
            check(in_req && !tls_write, "graphics TLS BO retained read-only in JM request");
            check(tlsbo && tlsbo->released, "graphics TLS BO released after completion");
        }
        /* Too large for the 64 MiB wired context must fail before submit. */
        to.thread_storage_bytes = 32768; F.submits = 0;
        check(n2mb_draw_v3(F.dev, &n2_preload_shader, &tq, &tr, &to,
                           tout, &tc) != 0 && F.submits == 0,
              "graphics TLS over 64 MiB refused before submit");
        to.thread_storage_bytes = 32769; F.submits = 0;
        check(n2mb_draw_v3(F.dev, &n2_preload_shader, &tq, &tr, &to,
                           tout, &tc) != 0 && F.submits == 0,
              "graphics TLS over n2r byte maximum refused");
        to.thread_storage_bytes = 0;
    }
    /* Keep the old v10/legacy expectation separately: the legacy n2mb_draw
     * path still emits the qualified 16x16 plane tail 0x000f000f, while v2
     * sidecar planes use arch9 word7=0. */
    {
        struct fbo *legacy_target = alloc_resource(16384);
        struct fbo *legacy_texture = alloc_resource(4096);
        struct tegu_generic_draw_request lq;
        uint64_t legacy_out[2] = {0, 0};
        unsigned count = 2;
        int e;
        memset(&lq, 0, sizeof(lq));
        lq.width = lq.height = 16; lq.row_bytes = 64; lq.target_format = 80;
        lq.load_action = 2; lq.vertex_count = 3; lq.primitive = TEGU_GENERIC_DRAW_TRIANGLES;
        lq.scissor[0] = lq.scissor[1] = 0; lq.scissor[2] = lq.scissor[3] = 16;
        lq.binding_count = 1;
        lq.bindings[0] = (struct tegu_generic_draw_binding){.binding = 0,
            .kind = TEGU_GENERIC_DRAW_COMBINED, .width = 16, .height = 16,
            .row_bytes = 64, .format = 80};
        check(legacy_target && legacy_texture, "legacy target/texture allocated");
        if (!legacy_target || !legacy_texture) { close_fixture(); return 1; }
        lq.target_token = legacy_target->handle; lq.target_length = 16384;
        lq.bindings[0].memory = (struct tegu_generic_draw_memory){
            .token = legacy_texture->handle, .bytes = 1024};
        e = n2mb_draw(F.dev, &n2_preload_shader, &lq, legacy_out, &count);
        check(e == 0 && F.submits > 0, "legacy n2mb_draw still succeeds");
        check(F.snapshot_bytes >= N2R_BLOCK, "legacy command block captured");
        check(get32(F.snapshot + 6144 + 28) == 0x000f000fu,
              "legacy v10 plane word7 preserved separately");
        check(get32(F.snapshot + 6144 + 28) != 0u,
              "legacy plane tail is nonzero and not waived");
    }

    printf("n2mb MRT: %u checks, %u required failures, 0 pending failures\n", checks, failures);
    close_fixture();
    return failures ? 1 : 0;
}
