/* Independent end-to-end test of the active n2mb v2 compute transport.
 *
 * The production bridge (gpu/metal/nord2_metal_bridge.c) is driven through
 * selector 5/7 with real TGP2 packages and TDG2 dispatch wires.  A fake JM
 * kernel model captures the submitted command BO, and the test decodes the
 * complete COMPUTE_JOB + SHADER_PROGRAM + LOCAL_STORAGE + FAU + SRT tables.
 *
 * Oracle sources:
 *  - v1 production emitters for the COMPUTE_JOB/SHADER_PROGRAM/LOCAL_STORAGE
 *    envelope (v2 must preserve the established job encoding);
 *  - pinned Mesa v10 pack macros via texture_state_mesa_golden.c for the
 *    16x16 RGBA8 texture/plane/sampler words;
 *  - the textures owner's public n2t helpers only for non-golden sampler /
 *    storage variants whose helper bytes are separately Mesa-tested.
 *
 * No GPU execution.  Required failures are transport defects; pending
 * failures are explicitly open contract questions and never count as support.
 */
#include "nord2_metal_bridge.h"
#include "pipeline_format.h"
#include "nord2_gpu_transport.h"
#include "nord2_gpu_jm.h"
#include "nord2_gpu_desc.h"
#include "nord2_texture_resources.h"
#include "texture_state_mesa_golden.h"
void n2mb_v9_golden_page(uint64_t descriptor, uint64_t backing,
                         unsigned char page[4096]);

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BO 64u
#define BO_HOST_BYTES (256u * 1024u)
#define SNAPSHOT_BYTES 32768u
#define MAX_DESC 10u

#define BAD ((int)0xe00002c2u)

static unsigned required_failures, pending_failures, checks;
static const char *category = "required";

static void fail(const char *what)
{
    fprintf(stderr, "FAIL [%s] %s\n", category, what);
    if (!strcmp(category, "pending")) pending_failures++;
    else required_failures++;
}

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) fail(what);
}

static void begin_pending(void) { category = "pending"; }
static void end_pending(void) { category = "required"; }

struct fbo {
    uint32_t handle;
    uint64_t va;
    size_t size;
    uint8_t *host;
    int used, released, external;
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
    uint64_t snapshot_va;
    size_t snapshot_bytes;
    unsigned submits, waits;
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

static struct fbo *alloc_bo(size_t request, int external, uint64_t external_va)
{
    unsigned i;
    struct fbo *b = NULL;
    size_t size = (request + 16383u) & ~(size_t)16383u;
    if (!size || size > BO_HOST_BYTES) return NULL;
    for (i = 0; i < MAX_BO; i++)
        if (!F.bo[i].used) { b = &F.bo[i]; break; }
    if (!b) return NULL;
    memset(b, 0, sizeof(*b));
    b->host = calloc(1, size);
    if (!b->host) return NULL;
    b->used = 1;
    b->handle = ++F.next_handle;
    b->va = external_va ? external_va : F.next_va;
    if (!external_va) F.next_va += 0x2000000ull;
    b->size = size;
    b->external = external;
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
        s->gpu_id = 0x90800011;
        *no = 0; *outsize = sizeof(*s);
        return 0;
    }
    case NORD2_GPU_SEL_ALLOC: {
        struct fbo *b;
        if (ni != 2 || !no || *no < 3) return 1;
        b = alloc_bo((size_t)in[0], 0, 0);
        if (!b) return 1;
        out[0] = b->handle; out[1] = b->va; out[2] = b->size; *no = 3;
        return 0;
    }
    case NORD2_GPU_SEL_RELEASE: {
        struct fbo *b;
        if (ni != 1) return 1;
        b = find_handle((uint32_t)in[0]);
        if (!b || b->external) return 1;
        b->released = 1;
        if (no) *no = 0;
        return 0;
    }
    case NORD2_GPU_SEL_IMPORT_SURFACE: {
        struct fbo *b;
        uint64_t va = 0x600000000ull + F.next_handle * 0x2000000ull;
        if (ni != 4 || !no || *no < 3) return 1;
        b = alloc_bo((size_t)in[2], 1, va);
        if (!b) return 1;
        out[0] = b->handle; out[1] = b->va; out[2] = b->size; *no = 3;
        return 0;
    }
    case NORD2_GPU_SEL_SUBMIT_JM: {
        const struct nord2_gpu_submit_request *r = bytes;
        struct fbo *cmd;
        if (size != sizeof(*r) || !r || !no || *no < 1) return 1;
        memcpy(&F.request, r, sizeof(*r));
        F.have_request = 1;
        F.submits++;
        cmd = find_va(r->heads[0]);
        if (!cmd) return 1;
        F.snapshot_va = cmd->va;
        F.snapshot_bytes = cmd->size < SNAPSHOT_BYTES ? cmd->size : SNAPSHOT_BYTES;
        memcpy(F.snapshot, cmd->host + (r->heads[0] - cmd->va), F.snapshot_bytes);
        out[0] = ++F.next_job; *no = 1;
        return 0;
    }
    case NORD2_GPU_SEL_WAIT:
        if (ni != 2 || !no || *no < 1) return 1;
        out[0] = 4; *no = 1; F.waits++;
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
    *address = (uint64_t)(uintptr_t)b->host;
    *size = b->size;
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
        .close = fake_close, .lock = fake_lock, .unlock = fake_unlock
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
    uint64_t out[3] = {0,0,0}; unsigned n = 3;
    int e = n2mb_scalar(F.dev, NORD2_GPU_SEL_ALLOC, (uint64_t[]){size}, 1, out, &n);
    if (e || n != 2) return NULL;
    return find_handle((uint32_t)out[0]);
}

static uint64_t import_surface(void *host, size_t size)
{
    uint64_t handle = 0;
    if (n2mb_import_surface(F.dev, 7, size, (uint64_t)(uintptr_t)host, &handle))
        return 0;
    return handle;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *p) { return tegu_pipeline_word(p); }
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static void manifest(uint8_t *pkg, unsigned i, uint32_t binding, uint32_t kind,
                     uint32_t access, uint32_t table, uint32_t entry, uint32_t flags,
                     uint32_t storage_format, uint32_t shape)
{
    uint8_t *r = pkg + TEGU_PIPELINE2_HEADER_BYTES + i * TEGU_PIPELINE2_MANIFEST_BYTES;
    memset(r, 0, TEGU_PIPELINE2_MANIFEST_BYTES);
    put32(r + 0, 0);
    put32(r + 4, binding);
    put32(r + 8, kind);
    put32(r + 12, 1);
    put32(r + 16, access);
    put32(r + 20, table);
    put32(r + 24, entry);
    put32(r + 28, flags);
    put32(r + 32, storage_format);
    put32(r + 48, shape);
}

enum {
    M_UBO = 1u << 0, M_SSBO = 1u << 1, M_TEXTURE = 1u << 2,
    M_SAMPLER = 1u << 3, M_COMBINED = 1u << 4, M_IMAGE = 1u << 5,
    M_DUMMY = 1u << 6
};

static size_t build_tgp2(uint8_t *pkg, unsigned options, uint32_t push_words,
                         uint32_t registers, uint32_t lx, uint32_t ly, uint32_t lz,
                         uint64_t preload)
{
    unsigned at = 0, count = 0, i;
    uint32_t mask = 0;
    const uint32_t code_bytes = 8;

    if (options & M_UBO) count++;
    if (options & M_SSBO) count++;
    if (options & M_TEXTURE) count++;
    if (options & M_SAMPLER) count++;
    if (options & M_COMBINED) count += 2;
    if (options & M_IMAGE) count++;
    if (options & M_DUMMY) count++;

    memset(pkg, 0, 16384);
    put32(pkg + 0, TEGU_PIPELINE2_MAGIC);
    put32(pkg + 4, TEGU_PIPELINE2_VERSION);
    put32(pkg + 8, code_bytes);
    put32(pkg + 12, TEGU_PIPELINE_GPU);
    put32(pkg + 16, registers);
    put32(pkg + 20, push_words);
    put64(pkg + 24, preload);
    if (options & M_UBO) mask |= 1u << 0;
    if (options & M_SSBO) mask |= 1u << 1;
    put32(pkg + 32, mask);
    put32(pkg + 36, lx); put32(pkg + 40, ly); put32(pkg + 44, lz);
    put32(pkg + 48, TEGU_PIPELINE2_HEADER_BYTES);
    put32(pkg + 52, count);
    put32(pkg + 56, TEGU_PIPELINE2_MANIFEST_BYTES);
    put32(pkg + 60, TEGU_PIPELINE2_HEADER_BYTES + count * TEGU_PIPELINE2_MANIFEST_BYTES);
    put32(pkg + 64, (options & M_DUMMY) ? TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER : 0);

    if (options & M_UBO)
        manifest(pkg, at++, 0, TEGU_PIPELINE2_KIND_UBO, TEGU_PIPELINE2_ACCESS_READ,
                 TEGU_PIPELINE2_TABLE_FAU, 0, 0, 0, 0);
    if (options & M_SSBO)
        manifest(pkg, at++, 1, TEGU_PIPELINE2_KIND_SSBO,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_WRITE,
                 TEGU_PIPELINE2_TABLE_FAU, 1, 0, 0, 0);
    if (options & M_TEXTURE)
        manifest(pkg, at++, 3, TEGU_PIPELINE2_KIND_TEXTURE,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 TEGU_PIPELINE2_TABLE_TEXTURE, 3, 0, 0, 1u);
    if (options & M_SAMPLER)
        manifest(pkg, at++, 6, TEGU_PIPELINE2_KIND_SAMPLER, 0,
                 TEGU_PIPELINE2_TABLE_SAMPLER, 6, 0, 0, 0);
    if (options & M_COMBINED) {
        manifest(pkg, at++, 5, TEGU_PIPELINE2_KIND_TEXTURE,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 TEGU_PIPELINE2_TABLE_TEXTURE, 5,
                 TEGU_PIPELINE2_FLAG_DECLARED_COMBINED, 0, 1u);
        manifest(pkg, at++, 5, TEGU_PIPELINE2_KIND_SAMPLER, 0,
                 TEGU_PIPELINE2_TABLE_SAMPLER, 5,
                 TEGU_PIPELINE2_FLAG_DECLARED_COMBINED, 0, 0);
    }
    if (options & M_IMAGE)
        manifest(pkg, at++, 4, TEGU_PIPELINE2_KIND_IMAGE,
                 TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_WRITE,
                 TEGU_PIPELINE2_TABLE_TEXTURE, 4, 0, 37, 1u);
    if (options & M_DUMMY)
        manifest(pkg, at++, TEGU_PIPELINE2_DUMMY_BINDING, TEGU_PIPELINE2_KIND_SAMPLER, 0,
                 TEGU_PIPELINE2_TABLE_SAMPLER, TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY,
                 TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER, 0, 0);
    for (i = 0; i < code_bytes; i++)
        pkg[TEGU_PIPELINE2_HEADER_BYTES + at * TEGU_PIPELINE2_MANIFEST_BYTES + i] =
            (uint8_t)(0xc0u + i);
    return TEGU_PIPELINE2_HEADER_BYTES + at * TEGU_PIPELINE2_MANIFEST_BYTES + code_bytes;
}

static uint8_t *desc_at(uint8_t *wire, unsigned i)
{
    return wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + i * TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
}

static void wire_header(uint8_t *wire, unsigned descriptors, uint32_t push_words,
                        uint32_t gx, uint32_t gy, uint32_t gz)
{
    memset(wire, 0, TEGU_PIPELINE_DISPATCH2_BYTES);
    put32(wire + 0, TEGU_PIPELINE_DISPATCH2_MAGIC);
    put32(wire + 4, TEGU_PIPELINE_DISPATCH2_VERSION);
    put32(wire + 8, descriptors);
    put32(wire + 12, gx); put32(wire + 16, gy); put32(wire + 20, gz);
    put32(wire + 24, push_words);
    put32(wire + 28, 0);
}

static void base_desc(uint8_t *wire, unsigned i, uint32_t binding, uint32_t kind,
                      uint32_t metal, uint32_t flags, uint64_t token,
                      uint64_t offset, uint64_t span)
{
    uint8_t *d = desc_at(wire, i);
    put32(d + TEGU_PIPELINE_D2_BINDING, binding);
    put32(d + TEGU_PIPELINE_D2_KIND, kind);
    put32(d + TEGU_PIPELINE_D2_METAL_INDEX, metal);
    put32(d + TEGU_PIPELINE_D2_FLAGS, flags);
    put64(d + TEGU_PIPELINE_D2_TOKEN, token);
    put64(d + TEGU_PIPELINE_D2_OFFSET, offset);
    put64(d + TEGU_PIPELINE_D2_SPAN, span);
}

static void texture_desc(uint8_t *wire, unsigned i, uint32_t binding, uint32_t kind,
                         uint32_t metal, uint32_t flags, uint64_t token,
                         uint32_t format, uint32_t view_format, uint32_t dim,
                         uint32_t w, uint32_t h, uint32_t depth, uint32_t layers,
                         uint32_t levels, uint32_t samples, uint32_t usage,
                         uint32_t row, uint32_t storage_mode, uint64_t level_offset,
                         uint32_t slice, uint32_t layer, uint32_t swizzle)
{
    uint8_t *d = desc_at(wire, i);
    /* OFFSET is the resource base relative to the BO; LEVEL_OFFSET is the
     * view-relative level origin (0 for a base view).  The active frontend
     * writes exactly this split. */
    base_desc(wire, i, binding, kind, metal, flags, token, 0,
              (uint64_t)row * h * depth * layers);
    put32(d + TEGU_PIPELINE_D2_FORMAT, format);
    put32(d + TEGU_PIPELINE_D2_VIEW_FORMAT, view_format);
    put32(d + TEGU_PIPELINE_D2_DIM, dim);
    put32(d + TEGU_PIPELINE_D2_WIDTH, w);
    put32(d + TEGU_PIPELINE_D2_HEIGHT, h);
    put32(d + TEGU_PIPELINE_D2_DEPTH, depth);
    put32(d + TEGU_PIPELINE_D2_LAYERS, layers);
    put32(d + TEGU_PIPELINE_D2_LEVELS, levels);
    put32(d + TEGU_PIPELINE_D2_SAMPLES, samples);
    put32(d + TEGU_PIPELINE_D2_USAGE, usage);
    put32(d + TEGU_PIPELINE_D2_ROW_STRIDE, row);
    put32(d + TEGU_PIPELINE_D2_STORAGE_MODE, storage_mode);
    put64(d + TEGU_PIPELINE_D2_LEVEL_OFFSET, level_offset);
    put32(d + TEGU_PIPELINE_D2_SLICE_STRIDE, slice);
    put32(d + TEGU_PIPELINE_D2_LAYER_STRIDE, layer);
    put32(d + TEGU_PIPELINE_D2_SWIZZLE, swizzle);
    /* The manifest storage format is a VkFormat.  Every image in this suite is
     * Metal RGBA8Unorm (70), whose canonical Vulkan storage format is 37. */
    put32(d + TEGU_PIPELINE_D2_VIEW_FORMAT_VK,
          kind == TEGU_PIPELINE2_KIND_IMAGE ? 37u : 0u);
}

static void sampler_fields(uint8_t *wire, unsigned i, uint32_t sampler_index,
                           uint32_t minf, uint32_t magf, uint32_t mipf,
                           uint32_t address, uint32_t normalized, uint32_t compare,
                           uint32_t aniso, uint32_t reduction, float lod_min,
                           float lod_max, uint32_t border)
{
    uint8_t *d = desc_at(wire, i);
    uint32_t lod_min_bits, lod_max_bits;
    memcpy(&lod_min_bits, &lod_min, 4);
    memcpy(&lod_max_bits, &lod_max, 4);
    put32(d + TEGU_PIPELINE_D2_SAMPLER_INDEX, sampler_index);
    put32(d + TEGU_PIPELINE_D2_SAMPLER_FLAGS, compare & 1u);
    put32(d + TEGU_PIPELINE_D2_MIN_FILTER, minf);
    put32(d + TEGU_PIPELINE_D2_MAG_FILTER, magf);
    put32(d + TEGU_PIPELINE_D2_MIP_FILTER, mipf);
    put32(d + TEGU_PIPELINE_D2_ADDRESS_S, address);
    put32(d + TEGU_PIPELINE_D2_ADDRESS_T, address);
    put32(d + TEGU_PIPELINE_D2_ADDRESS_R, address);
    put32(d + TEGU_PIPELINE_D2_BORDER_COLOR, border);
    put32(d + TEGU_PIPELINE_D2_NORMALIZED, normalized);
    put32(d + TEGU_PIPELINE_D2_COMPARE, 0);
    put32(d + TEGU_PIPELINE_D2_MAX_ANISOTROPY, aniso);
    put32(d + TEGU_PIPELINE_D2_REDUCTION, reduction);
    put32(d + TEGU_PIPELINE_D2_LOD_AVERAGE, 0);
    put32(d + TEGU_PIPELINE_D2_LOD_MIN, lod_min_bits);
    put32(d + TEGU_PIPELINE_D2_LOD_MAX, lod_max_bits);
}

static uint64_t import_package(const uint8_t *pkg, size_t size)
{
    uint64_t handle = 0;
    unsigned count = 1;
    int error = n2mb_method(F.dev, 5, NULL, 0, pkg, size, &handle, &count, NULL, NULL);
    if (error || count != 1 || !handle) return 0;
    return handle;
}

static int dispatch(uint64_t pipeline, const uint8_t *wire, uint64_t out[2])
{
    unsigned count = 2;
    out[0] = UINT64_C(0xdeadbeefdeadbeef);
    out[1] = UINT64_C(0xfeedfacefeedface);
    return n2mb_method(F.dev, 7, &pipeline, 1, wire, TEGU_PIPELINE_DISPATCH2_BYTES,
                       out, &count, NULL, NULL);
}

static const uint8_t *snapshot(void)
{
    return F.snapshot;
}

static void check_zero(const uint8_t *p, size_t bytes, const char *what)
{
    size_t i;
    checks++;
    for (i = 0; i < bytes; i++)
        if (p[i]) break;
    if (i != bytes) {
        fprintf(stderr, "FAIL [required] %s (nonzero at %zu: %02x)\n", what, i, p[i]);
        required_failures++;
    }
}

static void check_bytes(const uint8_t *got, const uint8_t *want, size_t bytes,
                        const char *what)
{
    size_t i;
    checks++;
    for (i = 0; i < bytes; i++) {
        if (got[i] != want[i]) {
            unsigned a, b;
            memcpy(&a, got + (i & ~3u), 4);
            memcpy(&b, want + (i & ~3u), 4);
            fprintf(stderr, "FAIL [required] %s at byte %zu: got %08x want %08x\n",
                    what, i, a, b);
            required_failures++;
            return;
        }
    }
}

/* Build the v1 envelope for the same dispatch facts. */
static void expected_job(uint8_t job[NORD2_GPU_COMPUTE_CHAIN_BYTES], uint64_t cmd_va,
                         const uint32_t groups[3], const uint32_t local[3],
                         uint32_t fau_words, uint64_t resources)
{
    struct nord2_gpu_compute_chain chain;
    memset(&chain, 0, sizeof(chain));
    chain.chain_pa = cmd_va;
    chain.next_pa = 0;
    chain.index = 1;
    chain.resources = resources;
    chain.shader = cmd_va + 128u;
    chain.thread_storage = cmd_va + 192u;
    chain.fau = cmd_va + 256u;
    chain.fau_count = (uint8_t)fau_words;
    memcpy(chain.workgroup_count, groups, sizeof(chain.workgroup_count));
    memcpy(chain.workgroup_size, local, sizeof(chain.workgroup_size));
    if (!nord2_gpu_jm_build_compute(job, NORD2_GPU_COMPUTE_CHAIN_BYTES, &chain)) {
        fprintf(stderr, "FATAL expected job builder\n");
        exit(2);
    }
}

static void expected_shader(uint8_t out[NORD2_DESC_SHADER_PROGRAM_SIZE],
                            uint64_t code_va, uint32_t preload_low16,
                            uint32_t registers)
{
    memset(out, 0, NORD2_DESC_SHADER_PROGRAM_SIZE);
    nord2_desc_shader_program(out, NORD2_STAGE_COMPUTE, code_va, preload_low16);
    /* Mesa v9 register allocation enum: 0=64, 2=32.  The v1 transport already
     * applies this and v2 must preserve it. */
    if (registers <= 32) put32(out, get32(out) | (2u << 30));
}

static void expected_local(uint8_t out[NORD2_DESC_LOCAL_STORAGE_SIZE])
{
    memset(out, 0, NORD2_DESC_LOCAL_STORAGE_SIZE);
    nord2_desc_local_storage(out, 0, 0, 0, 0);
}

static void expected_fau(uint8_t out[16], const struct fbo *b, uint64_t offset,
                         uint64_t length)
{
    memset(out, 0, 16);
    put64(out, b->va + offset);
    put32(out + 8, (uint32_t)length);
    put32(out + 12, 0);
}

static struct fbo *resource_handle(uint32_t handle) { return find_handle(handle); }

/* ------------------------------------------------------------------ */
static void test_full_positive(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    uint8_t golden[4096];
    struct fbo *ubo, *ssbo, *img, *surf;
    uint8_t *surface;
    uint64_t surface_token;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    uint8_t want[NORD2_GPU_COMPUTE_CHAIN_BYTES];
    uint32_t groups[3] = {2, 3, 1}, local[3] = {8, 4, 1};
    uint64_t table1, table2;

    open_fixture();
    ubo = alloc_resource(4096);
    ssbo = alloc_resource(4096);
    img = alloc_resource(4096);
    surface = calloc(1, 4096);
    surface_token = import_surface(surface, 4096);
    surf = resource_handle((uint32_t)surface_token);
    check(ubo && ssbo && img && surface_token && surf, "resources allocated");
    if (!ubo || !ssbo || !img || !surface_token || !surf) {
        free(surface); close_fixture(); return;
    }

    pkg_size = build_tgp2(pkg, M_UBO | M_SSBO | M_COMBINED | M_IMAGE,
                          4, 16, 8, 4, 1, UINT64_C(0x0180000000000000));
    pipe = import_package(pkg, pkg_size);
    check(pipe != 0, "TGP2 package imported");

    wire_header(wire, 4, 4, 2, 3, 1);
    base_desc(wire, 0, 0, TEGU_PIPELINE2_KIND_UBO, 0, TEGU_PIPELINE2_ACCESS_READ,
              ubo->handle, 16, 64);
    /* Metal buffer 30 -> native FAU slot 1 proves namespace separation. */
    base_desc(wire, 1, 1, TEGU_PIPELINE2_KIND_SSBO, 30,
              TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_WRITE,
              ssbo->handle, 0, 128);
    /* Combined at native binding 5 from Metal texture 9 / sampler 9, imported
     * IOSurface backing and a linear/repeat/mip-linear sampler. */
    texture_desc(wire, 2, 5, TEGU_PIPELINE2_KIND_COMBINED, 9,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 surface_token, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
    sampler_fields(wire, 2, 9, 1, 1, 2, 2, 1, 0, 1, 0, 0.0f, 3.0f, 0);
    /* Storage image at native binding 4 backed by a separate allocation. */
    texture_desc(wire, 3, 4, TEGU_PIPELINE2_KIND_IMAGE, 8,
                 TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_WRITE,
                 img->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 2, 64, 0, 0, 1024, 0, 0);

    F.submits = 0;
    check(dispatch(pipe, wire, out) == 0 && F.submits == 1 && F.waits == 1,
          "full v2 dispatch submitted once and waited");
    check(F.have_request, "JM request captured");
    if (!F.have_request || !F.submits) { free(surface); close_fixture(); return; }

    m = snapshot();
    table1 = F.snapshot_va + 832u;
    table2 = F.snapshot_va + 9024u;

    /* Complete COMPUTE_JOB envelope, byte-identical to the v1 emitters. */
    {
        uint64_t srt = 0;
        check(n2mb_compute_srt_pointer(F.snapshot_va + 768u,
                                       F.snapshot_va + 40960u, 4, &srt) == 0,
              "SRT pointer helper accepts the manifest tables");
        expected_job(want, F.snapshot_va, groups, local, (4 + 1) / 2,
                     F.snapshot_va + 768u);
        put64(want + 96, srt);
    }
    check_bytes(m, want, NORD2_GPU_COMPUTE_CHAIN_BYTES, "COMPUTE_JOB complete envelope");
    check((get64(m + 96) & 63u) == 4u, "COMPUTE_JOB SRT low bits carry four tables");

    /* SHADER_PROGRAM: code VA, preload, register allocation. */
    {
        uint8_t expect[NORD2_DESC_SHADER_PROGRAM_SIZE];
        expected_shader(expect, get64(m + 128 + 8), 0x0180, 16);
        check_bytes(m + 128, expect, NORD2_DESC_SHADER_PROGRAM_SIZE,
                    "SHADER_PROGRAM codeVA/preload/registers");
    }
    {
        uint8_t expect[NORD2_DESC_LOCAL_STORAGE_SIZE];
        expected_local(expect);
        check_bytes(m + 192, expect, NORD2_DESC_LOCAL_STORAGE_SIZE,
                    "LOCAL_STORAGE no-TLS descriptor");
    }

    /* FAU buffer descriptors and unused slots. */
    {
        uint8_t expect[16];
        expected_fau(expect, ubo, 16, 64);
        check_bytes(m + 256, expect, 16, "FAU UBO binding0 nonzero range offset");
        expected_fau(expect, ssbo, 0, 128);
        check_bytes(m + 256 + 16, expect, 16, "FAU SSBO binding1 with Metal slot30");
        check_zero(m + 256 + 32, 6 * 16, "FAU unused slots 2..7 zero");
    }

    /* SRT resource entries: table1 size 6*32, table2 size 6*32. */
    {
        uint8_t expect[16];
        check(n2t_resource_entry(table1, 6u * 32u, expect) == N2T_OK, "resource entry oracle table1");
        check_bytes(m + 768 + 16, expect, 16, "SRT table1 address/size");
        check((get64(m + 768 + 16) >> 56) == 1, "SRT table1 valid bit");
        check(n2t_resource_entry(table2, 6u * 32u, expect) == N2T_OK, "resource entry oracle table2");
        check_bytes(m + 768 + 32, expect, 16, "SRT table2 address/size");
    }

    /* Legacy v10 golden is retained as a separate labelled expectation.  The
     * v2/arch9 Generic Plane leaves word7 reserved zero; the old qualified v10
     * tail is 0x000f000f and is not the v2 contract. */
    tegu_texture_mesa_golden_page(0x20000000ull, 0x21000000ull, golden);
    {
        uint32_t want_words[8], plane[8]; unsigned i;
        uint64_t surface_ptr = get64(m + 832 + 5u * 32u + 16u);
        for (i = 0; i < 8; i++) memcpy(&want_words[i], golden + 0x120 + 4u * i, 4);
        want_words[4] = (uint32_t)surface_ptr;
        want_words[5] = (uint32_t)(surface_ptr >> 32);
        check_bytes(m + 832 + 5u * 32u, (const uint8_t *)want_words, 32,
                    "legacy v10 combined table1[5] descriptor (30/31-bit fields)");
        check(surface_ptr >= F.snapshot_va && surface_ptr + 32u <= F.snapshot_va + F.snapshot_bytes,
              "combined surface pointer lies inside the command BO");
        for (i = 0; i < 8; i++) memcpy(&plane[i], golden + 0x140 + 4u * i, 4);
        check(plane[7] == 0x000f000fu,
              "legacy v10 plane word7 is nonzero and kept as a separate expectation");
    }
    {
        uint8_t arch9[4096];
        n2mb_v9_golden_page(0x20000000ull, 0x21000000ull, arch9);
        {
            uint32_t want_words[8]; unsigned i;
            uint64_t surface_ptr = get64(m + 832 + 5u * 32u + 16u);
            for (i = 0; i < 8; i++) memcpy(&want_words[i], arch9 + 0x120 + 4u * i, 4);
            want_words[4] = (uint32_t)surface_ptr;
            want_words[5] = (uint32_t)(surface_ptr >> 32);
            check_bytes(m + 832 + 5u * 32u, (const uint8_t *)want_words, 32,
                        "combined table1[5] full32 arch9 (v9_pack) golden");
            for (i = 0; i < 8; i++) memcpy(&want_words[i], arch9 + 0x140 + 4u * i, 4);
            want_words[2] = (uint32_t)surf->va;
            want_words[3] = (uint32_t)(surf->va >> 32);
            check_bytes(m + (surface_ptr - F.snapshot_va), (const uint8_t *)want_words, 32,
                        "combined plane full32 arch9 (v9_pack, word7 reserved zero)");
            check(get32(m + (surface_ptr - F.snapshot_va) + 28u) == 0,
                  "v2 combined plane word7 is reserved zero for arch9");
        }
    }
    {
        struct n2t_sampler_state s;
        uint8_t expect[32];
        memset(&s, 0, sizeof(s));
        s.version = N2T_TEXTURE_RESOURCES_VERSION;
        s.min_filter = N2T_FILTER_LINEAR; s.mag_filter = N2T_FILTER_LINEAR;
        s.mip_filter = N2T_MIP_LINEAR;
        s.address_s = s.address_t = s.address_r = N2T_ADDRESS_REPEAT;
        s.normalized = 1; s.max_anisotropy = 1;
        s.reduction = N2T_REDUCTION_AVERAGE;
        s.lod_algorithm = N2T_LOD_ISOTROPIC;
        s.clamp_integer_array = 1; s.seamless_cube = 1;
        s.lod_max = 3.0f;
        check(n2t_sampler_pack(&s, expect) == N2T_OK, "sampler oracle pack");
        check_bytes(m + 9024 + 5u * 32u, expect, 32,
                    "combined table2[5] exact linear/repeat sampler state");
    }

    /* Storage image: table1[4], format/width independent of the texture. */
    {
        uint32_t words[8], golden_words[8]; unsigned i;
        uint64_t surface_ptr = get64(m + 832 + 4u * 32u + 16u);
        for (i = 0; i < 8; i++) memcpy(&golden_words[i], golden + 0x120 + 4u * i, 4);
        for (i = 0; i < 8; i++) memcpy(&words[i], m + 832 + 4u * 32u + 4u * i, 4);
        check(words[0] == golden_words[0],
              "storage image type/dimension/format word (Mesa golden)");
        check(words[1] == (15u | (15u << 16)), "storage image width/height");
        check(surface_ptr >= F.snapshot_va && surface_ptr + 32u <= F.snapshot_va + F.snapshot_bytes,
              "storage image surface pointer inside command BO");
        {
            uint32_t plane[8], arch9[8]; unsigned k;
            uint8_t arch9_page[4096];
            for (i = 0; i < 8; i++) memcpy(&plane[i], m + (surface_ptr - F.snapshot_va) + 4u * i, 4);
            check(plane[2] == (uint32_t)img->va && plane[3] == (uint32_t)(img->va >> 32),
                  "storage image plane points at its retained allocation");
            n2mb_v9_golden_page(0x20000000ull, 0x21000000ull, arch9_page);
            for (k = 0; k < 8; k++) memcpy(&arch9[k], arch9_page + 0x140 + 4u * k, 4);
            arch9[2] = (uint32_t)img->va;
            arch9[3] = (uint32_t)(img->va >> 32);
            check_bytes(m + (surface_ptr - F.snapshot_va), (const uint8_t *)arch9, 32,
                        "storage image plane full32 arch9 (v9_pack, word7 zero)");
        }
        check_zero(m + 832 + 0u * 32u, 4u * 32u, "table1 unused entries 0..3 zero");
        check_zero(m + 9024 + 0u * 32u, 5u * 32u, "table2 unused entries 0..4 zero");
    }

    /* Retained handles/write mask: SSBO and image are writable; read-only
     * IOSurface texture and UBO are not. */
    {
        unsigned i;
        int ubo_w = 0, ssbo_w = 0, img_w = 0, surf_w = 0;
        unsigned cmd_present = 0, code_present = 0;
        for (i = 0; i < F.request.nr_handles; i++) {
            unsigned h = F.request.handles[i];
            if (h == ubo->handle) ubo_w = (F.request.write_mask >> i) & 1u;
            if (h == ssbo->handle) ssbo_w = (F.request.write_mask >> i) & 1u;
            if (h == img->handle) img_w = (F.request.write_mask >> i) & 1u;
            if (h == surface_token) surf_w = (F.request.write_mask >> i) & 1u;
            if (find_handle(h) && find_handle(h)->va == F.snapshot_va) cmd_present = 1;
        }
        {
            uint64_t code_va = get64(m + 128 + 8);
            code_present = find_va(code_va) != NULL;
        }
        check(!ubo_w && ssbo_w && img_w && !surf_w,
              "write mask classifies SSBO/image only");
        check(cmd_present && code_present, "command and code BOs retained by the request");
        check(F.request.nr_handles >= 6, "request retains command/code/resources");
    }

    /* v2 fields survive import: code bytes copied after manifest. */
    {
        struct fbo *code = find_va(get64(m + 128 + 8));
        check(code && code->host[0] == 0xc0 && code->host[7] == 0xc7,
              "imported code bytes copied into code BO");
    }

    free(surface);
    close_fixture();
}

static void test_dummy_sampler(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *tex = NULL;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;

    open_fixture();
    tex = alloc_resource(4096);
    check(tex != NULL, "dummy case texture allocated");
    if (!tex) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_TEXTURE | M_DUMMY, 2, 16, 4, 4, 1, 0);
    pipe = import_package(pkg, pkg_size);
    wire_header(wire, 1, 2, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
    F.submits = 0;
    check(dispatch(pipe, wire, out) == 0 && F.submits == 1, "dummy sampler dispatch");
    m = snapshot();
    check((get64(m + 768 + 32) & ((UINT64_C(1) << 56) - 1)) == F.snapshot_va + 9024u &&
          get64(m + 768 + 40) == (uint64_t)257u * 32u,
          "dummy table2 address and 257-entry size");
    {
        struct n2t_sampler_state s;
        uint8_t expect[32];
        memset(&s, 0, sizeof(s));
        s.version = N2T_TEXTURE_RESOURCES_VERSION;
        s.min_filter = N2T_FILTER_NEAREST; s.mag_filter = N2T_FILTER_NEAREST;
        s.mip_filter = N2T_MIP_NOT_MIPMAPPED;
        s.address_s = s.address_t = s.address_r = N2T_ADDRESS_CLAMP_EDGE;
        s.normalized = 1; s.max_anisotropy = 1; s.reduction = N2T_REDUCTION_AVERAGE;
        s.lod_algorithm = N2T_LOD_ISOTROPIC; s.clamp_integer_array = 1; s.seamless_cube = 1;
        check(n2t_sampler_pack(&s, expect) == N2T_OK, "dummy sampler oracle pack");
        check_bytes(m + 9024 + 256u * 32u, expect, 32, "dummy sampler table2[256] exact words");
    }
    close_fixture();
}

static void test_v1_regression(void)
{
    uint8_t pkg[1024], wire[TEGU_PIPELINE_DISPATCH_BYTES];
    struct fbo *buf;
    uint64_t pipe, out[2];
    const uint8_t *m;
    uint8_t want[NORD2_GPU_COMPUTE_CHAIN_BYTES];

    open_fixture();
    buf = alloc_resource(4096);
    check(buf != NULL, "v1 buffer allocated");
    if (!buf) { close_fixture(); return; }
    memset(pkg, 0, sizeof(pkg));
    put32(pkg + 0, TEGU_PIPELINE_MAGIC);
    put32(pkg + 4, 1);
    put32(pkg + 8, 8);
    put32(pkg + 12, TEGU_PIPELINE_GPU);
    put32(pkg + 16, 8);
    put32(pkg + 20, 2);
    put32(pkg + 32, 1);
    put32(pkg + 36, 1); put32(pkg + 40, 1); put32(pkg + 44, 1);
    put32(pkg + 56, 1); put32(pkg + 60, 0);
    put64(pkg + 64, 0);
    pkg[64] = 0x5a; pkg[71] = 0x5a;
    pipe = import_package(pkg, TEGU_PIPELINE_HEADER_BYTES + 8);
    check(pipe != 0, "v1 package imported");
    memset(wire, 0, sizeof(wire));
    put32(wire + 0, 1); put32(wire + 4, 2); put32(wire + 8, 1); put32(wire + 12, 1);
    put32(wire + 16, (uint32_t)buf->handle); put32(wire + 20, 0); put32(wire + 24, 64);
    put32(wire + 28, 0);
    put32(wire + 144, 0x11223344);
    F.submits = 0;
    out[0] = out[1] = 0;
    check(n2mb_method(F.dev, 7, &pipe, 1, wire, sizeof(wire), out, &(unsigned){2}, NULL, NULL) == 0 &&
          F.submits == 1, "v1 dispatch still works");
    m = snapshot();
    expected_job(want, F.snapshot_va, (uint32_t[]){2, 1, 1}, (uint32_t[]){1, 1, 1}, 1, 0);
    check_bytes(m, want, NORD2_GPU_COMPUTE_CHAIN_BYTES, "v1 COMPUTE_JOB byte-identical");
    check(get64(m + 96) == 0, "v1 chain.resources remains zero");
    {
        uint8_t expect[16];
        expected_fau(expect, buf, 0, 64);
        check_bytes(m + 256, expect, 16, "v1 FAU descriptor unchanged");
    }
    check(get32(m + 384) == 0x11223344, "v1 push bytes unchanged");
    check_zero(m + 768, 16, "v1 SRT entry remains zero");
    close_fixture();
}

static void test_refusals(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *tex;
    size_t pkg_size;
    uint64_t pipe, out[2];
    unsigned descs;

    open_fixture();
    tex = alloc_resource(4096);
    check(tex != NULL, "refusal texture allocated");
    if (!tex) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_TEXTURE | M_SAMPLER, 4, 16, 8, 8, 1, 0);
    pipe = import_package(pkg, pkg_size);
    descs = 2;

#define EXPECT_REFUSAL(label) do { \
    unsigned count = 2; uint64_t canary[2] = {UINT64_C(0x1111222233334444), UINT64_C(0x5555666677778888)}; \
    F.submits = 0; \
    memcpy(out, canary, sizeof(out)); \
    if (n2mb_method(F.dev, 7, &pipe, 1, wire, TEGU_PIPELINE_DISPATCH2_BYTES, out, &count, NULL, NULL) == 0 || \
        F.submits != 0) { fail(label); } \
    if (memcmp(out, canary, sizeof(canary))) { fprintf(stderr, "FAIL [required] %s mutated the output\n", label); fail("refusal mutated the output words"); } \
    checks++; \
} while (0)

    wire_header(wire, 2, 4, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    check(dispatch(pipe, wire, out) == 0, "baseline refusal wire accepted");
    {
        unsigned count = 2; uint64_t canary[2] = {1, 2};
        F.submits = 0;
        memcpy(out, canary, sizeof(canary));
        if (n2mb_method(F.dev, 7, &pipe, 1, NULL, TEGU_PIPELINE_DISPATCH2_BYTES,
                        out, &count, NULL, NULL) == 0 || F.submits ||
            memcmp(out, canary, sizeof(canary))) {
            fail("null dispatch wire refused without submit/output mutation");
        }
        checks++;
    }

    /* Bad wire header. */
    put32(wire + 0, 0x31504754u); EXPECT_REFUSAL("wire wrong magic");
    wire_header(wire, descs, 4, 1, 1, 1);
    put32(wire + 4, 3u); EXPECT_REFUSAL("wire version 3");
    wire_header(wire, descs, 4, 1, 1, 1);
    put32(wire + 8, 33u); EXPECT_REFUSAL("wire descriptor count over 32");
    wire_header(wire, descs, 4, 1, 1, 1);
    put32(wire + 12, 0u); EXPECT_REFUSAL("wire zero group X");
    wire_header(wire, descs, 4, 1, 1, 1);
    put32(wire + 24, 129u); EXPECT_REFUSAL("wire push words over 128");
    wire_header(wire, descs, 4, 1, 1, 1);
    put32(wire + 28, 1u); EXPECT_REFUSAL("wire reserved nonzero");

    /* Restore valid descriptors. */
    wire_header(wire, 2, 4, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    put64(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_SPAN, 1u << 20);
    EXPECT_REFUSAL("texture span exceeds retained BO");
    wire_header(wire, 2, 4, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    put64(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_LEVEL_OFFSET, 1u << 20);
    EXPECT_REFUSAL("level offset equals BO size");

    /* Wrong native binding/table: sampler declared at binding3, wire at 4. */
    wire_header(wire, 2, 4, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
    base_desc(wire, 1, 4, TEGU_PIPELINE2_KIND_SAMPLER, 4, 0, 0, 0, 0);
    sampler_fields(wire, 1, 4, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    EXPECT_REFUSAL("sampler at undeclared native binding");

    /* Missing descriptor against manifest. */
    wire_header(wire, 1, 4, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
    EXPECT_REFUSAL("missing required sampler descriptor");

    /* Unknown BO token. */
    wire_header(wire, 2, 4, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 0x12345678u, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 0, 0, 0);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    EXPECT_REFUSAL("unknown resource token");

    /* Unsupported format and memoryless storage mode. */
    wire_header(wire, 2, 4, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 999, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 0, 0, 0);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    EXPECT_REFUSAL("unsupported texture format");
    wire_header(wire, 2, 4, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 3, 0, 0, 0, 0);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    EXPECT_REFUSAL("memoryless texture refused");

    /* ---- required gaps in the current draft, recorded as failures ---- */
    /* Wrong wire push_words versus the imported package requirement. */
    {
        unsigned count = 2;
        wire_header(wire, 2, 7, 1, 1, 1); /* package push_words is 4 */
        texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                     TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                     tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
        base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
        sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
        F.submits = 0;
        if (n2mb_method(F.dev, 7, &pipe, 1, wire, TEGU_PIPELINE_DISPATCH2_BYTES,
                        out, &count, NULL, NULL) == 0 || F.submits) {
            checks++;
            fail("wire push_words accepted although it differs from the package");
        } else checks++;
    }

    /* Invalid Metal sampler identity on a combined descriptor. */
    {
        uint8_t pkg2[16384], wire2[TEGU_PIPELINE_DISPATCH2_BYTES];
        size_t size2 = build_tgp2(pkg2, M_COMBINED, 4, 16, 8, 8, 1, 0);
        uint64_t pipe2 = import_package(pkg2, size2);
        unsigned count = 2;
        wire_header(wire2, 1, 4, 1, 1, 1);
        texture_desc(wire2, 0, 5, TEGU_PIPELINE2_KIND_COMBINED, 5,
                     TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                     tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
        sampler_fields(wire2, 0, 99, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
        F.submits = 0;
        if (n2mb_method(F.dev, 7, &pipe2, 1, wire2, TEGU_PIPELINE_DISPATCH2_BYTES,
                        out, &count, NULL, NULL) == 0 || F.submits) {
            checks++;
            fail("combined descriptor accepts sampler_index 99 outside 0..15");
        } else checks++;
    }
    /* Declared span one byte shorter than a valid 16x16 RGBA8 layout. */
    {
        uint8_t spkg[16384], swire[TEGU_PIPELINE_DISPATCH2_BYTES];
        size_t ssize = build_tgp2(spkg, M_TEXTURE, 4, 16, 8, 8, 1, 0);
        uint64_t spipe = import_package(spkg, ssize);
        unsigned count = 2;
        wire_header(swire, 1, 4, 1, 1, 1);
        texture_desc(swire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                     TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                     tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 1024, 0, 0);
        put64(swire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_SPAN, 1023u);
        F.submits = 0;
        if (n2mb_method(F.dev, 7, &spipe, 1, swire, TEGU_PIPELINE_DISPATCH2_BYTES,
                        out, &count, NULL, NULL) == 0 || F.submits) {
            checks++;
            fail("declared span one byte short still authorized the full layout");
        } else checks++;
    }
    /* IMAGE storage_format=0 is legal only with a nonzero runtime Vk view. */
    {
        uint8_t ipkg[16384], iwire[TEGU_PIPELINE_DISPATCH2_BYTES];
        struct fbo *rimg = alloc_resource(4096);
        size_t isize = build_tgp2(ipkg, M_IMAGE, 4, 16, 8, 8, 1, 0);
        uint64_t ipipe;
        unsigned count = 2;
        put32(ipkg + TEGU_PIPELINE2_HEADER_BYTES + 32, 0u);
        ipipe = import_package(ipkg, isize);
        if (rimg) {
            wire_header(iwire, 1, 4, 1, 1, 1);
            texture_desc(iwire, 0, 4, TEGU_PIPELINE2_KIND_IMAGE, 4,
                         TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_WRITE,
                         rimg->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 2, 64, 0, 0, 1024, 0, 0);
            put32(iwire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_VIEW_FORMAT_VK, 0u);
            F.submits = 0;
            if (n2mb_method(F.dev, 7, &ipipe, 1, iwire, TEGU_PIPELINE_DISPATCH2_BYTES,
                            out, &count, NULL, NULL) == 0 || F.submits) {
                checks++;
                fail("IMAGE storage0 without runtime Vk view accepted");
            } else checks++;
            /* storage0 + nonzero runtime Vk view must be accepted. */
            put32(iwire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_VIEW_FORMAT_VK, 37u);
            F.submits = 0;
            if (n2mb_method(F.dev, 7, &ipipe, 1, iwire, TEGU_PIPELINE_DISPATCH2_BYTES,
                            out, &count, NULL, NULL) != 0 || F.submits != 1) {
                checks++;
                fail("IMAGE storage0 with runtime Vk view 37 refused");
            } else checks++;
        }
    }

    /* Image runtime Vk view must equal the manifest storage format. */
    {
        uint8_t ipkg[16384], iwire[TEGU_PIPELINE_DISPATCH2_BYTES];
        struct fbo *rimg = alloc_resource(4096);
        size_t isize = build_tgp2(ipkg, M_IMAGE, 4, 16, 8, 8, 1, 0);
        uint64_t ipipe = import_package(ipkg, isize);
        unsigned count = 2;
        if (rimg) {
            wire_header(iwire, 1, 4, 1, 1, 1);
            texture_desc(iwire, 0, 4, TEGU_PIPELINE2_KIND_IMAGE, 4,
                         TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_WRITE,
                         rimg->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 2, 64, 0, 0, 1024, 0, 0);
            put32(iwire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_VIEW_FORMAT_VK, 44u);
            F.submits = 0;
            if (n2mb_method(F.dev, 7, &ipipe, 1, iwire, TEGU_PIPELINE_DISPATCH2_BYTES,
                            out, &count, NULL, NULL) == 0 || F.submits) {
                checks++;
                fail("image Vk view format 44 accepted against manifest format 37");
            } else checks++;
        }
    }
#undef EXPECT_REFUSAL
    close_fixture();
}

/* Swizzle, atomic-only image and view-level are separate so each minimal
 * repro names its cross-owner contract question. */
static void test_swizzle_and_atomic(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *tex;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;

    open_fixture();
    tex = alloc_resource(4096);
    check(tex != NULL, "swizzle texture allocated");
    if (!tex) { close_fixture(); return; }

    /* A nonzero wire swizzle must reach the descriptor; textures asked for
     * swizzle_valid=1 whenever the word is nonzero.  RRRR = 0x000 is
     * indistinguishable from default, so use RGBA->BGR 0x210 here. */
    pkg_size = build_tgp2(pkg, M_TEXTURE | M_SAMPLER, 2, 16, 8, 8, 1, 0);
    pipe = import_package(pkg, pkg_size);
    wire_header(wire, 2, 2, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 0, 0, 0x210);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    if (dispatch(pipe, wire, out) == 0) {
        uint32_t word2;
        m = snapshot();
        memcpy(&word2, m + 832 + 3u * 32u + 8u, 4);
        checks++;
        if ((word2 & 0xfffu) != 0x210u) {
            fprintf(stderr, "FAIL [required] wire swizzle 0x210 silently replaced by 0x%03x\n",
                    word2 & 0xfffu);
            required_failures++;
        }
    } else {
        checks++;
        /* Explicit refusal would also be acceptable; silently dropping is not. */
        check(1, "nonzero swizzle explicitly refused");
    }

    /* Atomic-only storage image: manifest STORAGE|ATOMIC and matching wire.
     * The n2t packer has no atomic class; it must refuse, not become READ. */
    {
        uint8_t apkg[16384], awire[TEGU_PIPELINE_DISPATCH2_BYTES];
        struct fbo *img = alloc_resource(4096);
        size_t asize;
        uint64_t apipline;
        unsigned count = 2;
        if (!img) { close_fixture(); return; }
        asize = build_tgp2(apkg, M_IMAGE, 2, 16, 8, 8, 1, 0);
        /* Patch image access to STORAGE|ATOMIC. */
        put32(apkg + TEGU_PIPELINE2_HEADER_BYTES + 16,
              TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_ATOMIC);
        apipline = import_package(apkg, asize);
        wire_header(awire, 1, 2, 1, 1, 1);
        texture_desc(awire, 0, 4, TEGU_PIPELINE2_KIND_IMAGE, 4,
                     TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_ATOMIC,
                     img->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 2, 64, 0, 0, 0, 0, 0);
        F.submits = 0;
        if (n2mb_method(F.dev, 7, &apipline, 1, awire, TEGU_PIPELINE_DISPATCH2_BYTES,
                        out, &count, NULL, NULL) == 0 || F.submits) {
            checks++;
            fail("atomic-only storage image silently downgraded instead of refused");
        } else checks++;
    }
    close_fixture();
}

static void test_view_level(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *tex;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    uint32_t words[8];

    open_fixture();
    tex = alloc_resource(4096);
    check(tex != NULL, "view-level texture allocated");
    if (!tex) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_TEXTURE | M_SAMPLER, 2, 16, 8, 8, 1, 0);
    pipe = import_package(pkg, pkg_size);
    wire_header(wire, 2, 2, 1, 1, 1);
    /* View-relative convention: level[].offset is already view-adjusted.
     * A nonzero view_level with a nonzero level offset must place the plane at
     * backing + level_offset and must not add the level twice. */
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 1, 64, 0, 0, 0, 0, 0);
    put64(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_OFFSET, 1024u);
    put32(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_VIEW_LEVEL, 1u);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    if (dispatch(pipe, wire, out) == 0) {
        uint64_t surface;
        m = snapshot();
        surface = get64(m + 832 + 3u * 32u + 16u);
        memcpy(words, m + (surface - F.snapshot_va) + 8u, 8);
        check(surface >= F.snapshot_va && surface + 32u <= F.snapshot_va + F.snapshot_bytes,
              "view-level surface inside command BO");
        if (get64((const uint8_t *)words) != tex->va + 1024u) {
            fprintf(stderr,
                    "FAIL [required] view-relative offset not applied once: plane=%llx expected=%llx\n",
                    (unsigned long long)get64((const uint8_t *)words),
                    (unsigned long long)(tex->va + 1024u));
        }
        check(get64((const uint8_t *)words) == tex->va + 1024u,
              "view-relative level offset applied exactly once");
    } else {
        /* The transport currently refuses levels!=1 but must still accept a
         * one-level view with a nonzero base offset.  Treat a refusal as an
         * explicit unresolved limitation, not a pass. */
        begin_pending();
        checks++;
        fail("view with nonzero level offset refused (mip/view wiring incomplete)");
        end_pending();
    }
    close_fixture();
}

static void test_mip_layers(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *tex;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    uint64_t surface;

    open_fixture();
    tex = alloc_resource(8192);
    check(tex != NULL, "mip texture allocated");
    if (!tex) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_TEXTURE | M_SAMPLER, 2, 16, 8, 8, 1, 0);
    pipe = import_package(pkg, pkg_size);
    wire_header(wire, 2, 2, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 2, 16, 16, 1, 1, 3, 1, 1, 64, 0, 0, 0, 0, 0);
    /* The contiguous linear-mip convention: 16x16 + 8x8 + 4x4 at row64,
     * with a nonzero view-relative level origin. */
    put64(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_SPAN, 1024u + 512u + 256u);
    put64(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_LEVEL_OFFSET, 1024u);
    base_desc(wire, 1, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 1, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    F.submits = 0;
    check(dispatch(pipe, wire, out) == 0 && F.submits == 1, "3-level mip dispatch");
    m = snapshot();
    surface = get64(m + 832 + 3u * 32u + 16u);
    check(surface >= F.snapshot_va && surface + 3u * 32u <= F.snapshot_va + F.snapshot_bytes,
          "mip surface pointer covers three planes");
    {
        uint32_t words[8];
        memcpy(words, m + 832 + 3u * 32u, 8 * 4);
        check(((words[2] >> 16) & 31u) == 2u, "mip descriptor levels field is 2");
        check(words[3] >> 16 == 0x0200u, "mip descriptor maxLOD is 2.0");
        memcpy(words, m + (surface - F.snapshot_va) + 8u, 8);
        check(get64((const uint8_t *)words) == tex->va + 1024u, "mip level0 plane address");
        memcpy(words, m + (surface - F.snapshot_va) + 32u + 8u, 8);
        check(get64((const uint8_t *)words) == tex->va + 2048u, "mip level1 plane address");
        memcpy(words, m + (surface - F.snapshot_va) + 64u + 8u, 8);
        check(get64((const uint8_t *)words) == tex->va + 2560u, "mip level2 plane address");
        {
            uint32_t tail; memcpy(&tail, m + (surface - F.snapshot_va) + 64u + 28u, 4);
            check(tail == 0, "arch9 mip plane word7 reserved zero");
        }
    }
    close_fixture();
}


static void test_3d_mips(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *tex;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    uint64_t surface;

    open_fixture();
    tex = alloc_resource(4096);
    check(tex != NULL, "3D texture allocated");
    if (!tex) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_TEXTURE, 2, 16, 8, 8, 1, 0);
    put32(pkg + TEGU_PIPELINE2_HEADER_BYTES + 48, TEGU_PIPELINE2_DIM_3D);
    pipe = import_package(pkg, pkg_size);
    check(pipe != 0, "3D package imported");
    wire_header(wire, 1, 2, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 7, 8, 8, 2, 1, 2, 1, 1, 64, 0, 0, 512, 0, 0);
    put64(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_SPAN, 1280u);
    F.submits = 0;
    check(dispatch(pipe, wire, out) == 0 && F.submits == 1, "3D 2-level dispatch");
    m = snapshot();
    surface = get64(m + 832 + 3u * 32u + 16u);
    check(surface >= F.snapshot_va && surface + 2u * 32u <= F.snapshot_va + F.snapshot_bytes,
          "3D surface pointer covers two planes");
    {
        uint32_t words[8];
        memcpy(words, m + 832 + 3u * 32u, 8 * 4);
        check(((words[2] >> 16) & 31u) == 1u, "3D descriptor levels field");
        check(words[7] == 1u, "3D descriptor depth-1");
        memcpy(words, m + (surface - F.snapshot_va) + 8u, 8);
        check(get64((const uint8_t *)words) == tex->va, "3D level0 plane address");
        memcpy(words, m + (surface - F.snapshot_va) + 32u + 8u, 8);
        check(get64((const uint8_t *)words) == tex->va + 1024u, "3D level1 plane address");
    }
    close_fixture();
}

static void test_17_layers(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *tex;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    uint64_t surface;

    open_fixture();
    tex = alloc_resource(32768);
    check(tex != NULL, "17-layer texture allocated");
    if (!tex) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_TEXTURE, 2, 16, 8, 8, 1, 0);
    put32(pkg + TEGU_PIPELINE2_HEADER_BYTES + 48,
          TEGU_PIPELINE2_DIM_2D | TEGU_PIPELINE2_SHAPE_ARRAYED);
    pipe = import_package(pkg, pkg_size);
    check(pipe != 0, "17-layer package imported");
    wire_header(wire, 1, 2, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 3, 16, 16, 1, 17, 1, 1, 1, 64, 0, 0, 1024, 1024, 0);
    put64(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_SPAN,
          (uint64_t)64u * 16u * 17u);
    F.submits = 0;
    check(dispatch(pipe, wire, out) == 0 && F.submits == 1, "17-layer dispatch");
    m = snapshot();
    surface = get64(m + 832 + 3u * 32u + 16u);
    check(surface >= F.snapshot_va && surface + 17u * 32u <= F.snapshot_va + F.snapshot_bytes,
          "17-layer surface pointer covers all planes");
    {
        uint32_t words[8];
        memcpy(words, m + 832 + 3u * 32u, 8 * 4);
        check(words[6] == 16u, "17-layer descriptor array size is 16");
        memcpy(words, m + (surface - F.snapshot_va) + 16u * 32u + 8u, 8);
        check(get64((const uint8_t *)words) == tex->va + 16u * 1024u,
              "17-layer last plane address");
    }
    close_fixture();
}

static void test_readonly_image(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *img;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    unsigned count = 2;

    open_fixture();
    img = alloc_resource(4096);
    check(img != NULL, "read-only image allocated");
    if (!img) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_IMAGE, 2, 16, 8, 8, 1, 0);
    put32(pkg + TEGU_PIPELINE2_HEADER_BYTES + 16,
          TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_READ);
    pipe = import_package(pkg, pkg_size);
    wire_header(wire, 1, 2, 1, 1, 1);
    texture_desc(wire, 0, 4, TEGU_PIPELINE2_KIND_IMAGE, 4,
                 TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_READ,
                 img->handle, 70, 0, 2, 16, 16, 1, 1, 1, 1, 2, 64, 0, 0, 1024, 0, 0);
    F.submits = 0;
    check(n2mb_method(F.dev, 7, &pipe, 1, wire, TEGU_PIPELINE_DISPATCH2_BYTES,
                      out, &count, NULL, NULL) == 0 && F.submits == 1,
          "read-only storage image accepted");
    m = snapshot();
    {
        unsigned i, found = 0;
        for (i = 0; i < F.request.nr_handles; i++)
            if (F.request.handles[i] == img->handle) {
                found = 1;
                check(((F.request.write_mask >> i) & 1u) == 0,
                      "read-only image is not marked written");
            }
        check(found, "read-only image retained in the submit request");
    }
    check((get32(m + 832 + 4u * 32u) & 15u) == 2u,
          "read-only image uses table1 descriptor");
    close_fixture();
}

static void test_sampler_compare_flag(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    unsigned count = 2;

    open_fixture();
    pkg_size = build_tgp2(pkg, M_SAMPLER, 2, 16, 8, 8, 1, 0);
    /* Declare a per-use compare sampler in the manifest. */
    put32(pkg + TEGU_PIPELINE2_HEADER_BYTES + 28, TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE);
    pipe = import_package(pkg, pkg_size);
    check(pipe != 0, "compare sampler package imported");
    wire_header(wire, 1, 2, 1, 1, 1);
    base_desc(wire, 0, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 0, 6, 0, 0, 0, 0, 1, 1, 1, 0, 0.0f, 0.0f, 0);
    put32(wire + TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE_D2_COMPARE, 1u);
    F.submits = 0;
    check(n2mb_method(F.dev, 7, &pipe, 1, wire, TEGU_PIPELINE_DISPATCH2_BYTES,
                      out, &count, NULL, NULL) == 0 && F.submits == 1,
          "compare sampler with matching manifest flag accepted");
    m = snapshot();
    {
        struct n2t_sampler_state s;
        uint8_t expect[32];
        memset(&s, 0, sizeof(s));
        s.version = N2T_TEXTURE_RESOURCES_VERSION;
        s.min_filter = N2T_FILTER_NEAREST; s.mag_filter = N2T_FILTER_NEAREST;
        s.mip_filter = N2T_MIP_NOT_MIPMAPPED;
        s.address_s = s.address_t = s.address_r = N2T_ADDRESS_CLAMP_EDGE;
        s.normalized = 1; s.max_anisotropy = 1; s.reduction = N2T_REDUCTION_AVERAGE;
        s.lod_algorithm = N2T_LOD_ISOTROPIC; s.clamp_integer_array = 1; s.seamless_cube = 1;
        s.compare_enabled = 1; s.compare_function = N2T_COMPARE_LESS;
        check(n2t_sampler_pack(&s, expect) == N2T_OK, "compare sampler oracle pack");
        check_bytes(m + 9024 + 6u * 32u, expect, 32,
                    "compare sampler descriptor keeps compare enable/function");
    }
    /* Manifest says compare, wire says no compare -> refuse. */
    size_t pkg2_size = build_tgp2(pkg, M_SAMPLER, 2, 16, 8, 8, 1, 0);
    put32(pkg + TEGU_PIPELINE2_HEADER_BYTES + 28, TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE);
    pipe = import_package(pkg, pkg2_size);
    wire_header(wire, 1, 2, 1, 1, 1);
    base_desc(wire, 0, 6, TEGU_PIPELINE2_KIND_SAMPLER, 6, 0, 0, 0, 0);
    sampler_fields(wire, 0, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    F.submits = 0;
    check(n2mb_method(F.dev, 7, &pipe, 1, wire, TEGU_PIPELINE_DISPATCH2_BYTES,
                      out, &count, NULL, NULL) != 0 && F.submits == 0,
          "compare-manifest sampler with non-compare wire use refused");
    close_fixture();
}

static void test_v2_buffer_only(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *ubo, *ssbo;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    uint8_t want[NORD2_GPU_COMPUTE_CHAIN_BYTES];
    unsigned count = 2;

    open_fixture();
    ubo = alloc_resource(4096);
    ssbo = alloc_resource(4096);
    check(ubo && ssbo, "v2 buffer-only resources allocated");
    if (!ubo || !ssbo) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_UBO | M_SSBO, 2, 64, 1, 1, 1, 0);
    pipe = import_package(pkg, pkg_size);
    wire_header(wire, 2, 2, 1, 1, 1);
    base_desc(wire, 0, 0, TEGU_PIPELINE2_KIND_UBO, 0, TEGU_PIPELINE2_ACCESS_READ,
              ubo->handle, 0, 64);
    base_desc(wire, 1, 1, TEGU_PIPELINE2_KIND_SSBO, 0,
              TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_WRITE,
              ssbo->handle, 0, 64);
    F.submits = 0;
    check(n2mb_method(F.dev, 7, &pipe, 1, wire, TEGU_PIPELINE_DISPATCH2_BYTES,
                      out, &count, NULL, NULL) == 0 && F.submits == 1,
          "v2 buffer-only dispatch");
    m = snapshot();
    expected_job(want, F.snapshot_va, (uint32_t[]){1, 1, 1}, (uint32_t[]){1, 1, 1},
                 1, F.snapshot_va + 768u);
    put64(want + 96, get64(m + 96));
    check_bytes(m, want, NORD2_GPU_COMPUTE_CHAIN_BYTES, "v2 buffer-only COMPUTE_JOB");
    {
        uint64_t t1 = get64(m + 768 + 16) & ((UINT64_C(1) << 56) - 1);
        uint64_t t2 = get64(m + 768 + 32) & ((UINT64_C(1) << 56) - 1);
        check_zero(m + 768, 16, "v2 buffer-only SRT[0] zero");
        check_zero(m + 768 + 48, 16, "v2 buffer-only SRT[3] zero");
        /* A single zeroed table entry is acceptable; no descriptor may exist. */
        if (t1) check_zero(m + (t1 - F.snapshot_va), 32, "v2 buffer-only table1 entry zero");
        if (t2) check_zero(m + (t2 - F.snapshot_va), 32, "v2 buffer-only table2 entry zero");
    }
    check(get64(m + 256) == ubo->va && get64(m + 272) == ssbo->va,
          "v2 buffer-only FAU addresses");
    close_fixture();
}

static void test_array_layers(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    struct fbo *tex;
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    uint64_t surface;

    open_fixture();
    tex = alloc_resource(8192);
    check(tex != NULL, "2-layer texture allocated");
    if (!tex) { close_fixture(); return; }
    pkg_size = build_tgp2(pkg, M_TEXTURE, 2, 16, 8, 8, 1, 0);
    /* 2D arrayed shape in the manifest domain. */
    put32(pkg + TEGU_PIPELINE2_HEADER_BYTES + 48,
          TEGU_PIPELINE2_DIM_2D | TEGU_PIPELINE2_SHAPE_ARRAYED);
    pipe = import_package(pkg, pkg_size);
    check(pipe != 0, "2-layer package imported");
    wire_header(wire, 1, 2, 1, 1, 1);
    texture_desc(wire, 0, 3, TEGU_PIPELINE2_KIND_TEXTURE, 3,
                 TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
                 tex->handle, 70, 0, 3, 16, 16, 1, 2, 1, 1, 1, 64, 0, 0, 1024, 1024, 0);
    F.submits = 0;
    check(dispatch(pipe, wire, out) == 0 && F.submits == 1, "2-layer texture dispatch");
    m = snapshot();
    surface = get64(m + 832 + 3u * 32u + 16u);
    check(surface >= F.snapshot_va && surface + 64u <= F.snapshot_va + F.snapshot_bytes,
          "2-layer surface pointer covers two planes");
    {
        uint32_t words[8];
        memcpy(words, m + 832 + 3u * 32u, 8 * 4);
        check(words[6] == 1u, "2-layer descriptor array size is layer_count-1");
        memcpy(words, m + (surface - F.snapshot_va) + 8u, 8);
        check(get64((const uint8_t *)words) == tex->va, "layer0 plane address");
        memcpy(words, m + (surface - F.snapshot_va) + 32u + 8u, 8);
        check(get64((const uint8_t *)words) == tex->va + 1024u, "layer1 plane address");
    }
    close_fixture();
}

static void test_sparse_sampler(void)
{
    uint8_t pkg[16384], wire[TEGU_PIPELINE_DISPATCH2_BYTES];
    size_t pkg_size;
    uint64_t pipe, out[2];
    const uint8_t *m;
    unsigned count = 2;

    open_fixture();
    pkg_size = build_tgp2(pkg, M_SAMPLER, 2, 16, 8, 8, 1, 0);
    put32(pkg + TEGU_PIPELINE2_HEADER_BYTES + 4, 255u);
    put32(pkg + TEGU_PIPELINE2_HEADER_BYTES + 24, 255u);
    pipe = import_package(pkg, pkg_size);
    check(pipe != 0, "native sampler 255 package imported");
    wire_header(wire, 1, 2, 1, 1, 1);
    base_desc(wire, 0, 255, TEGU_PIPELINE2_KIND_SAMPLER, 0, 0, 0, 0, 0);
    sampler_fields(wire, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0.0f, 0.0f, 0);
    F.submits = 0;
    check(n2mb_method(F.dev, 7, &pipe, 1, wire, TEGU_PIPELINE_DISPATCH2_BYTES,
                      out, &count, NULL, NULL) == 0 && F.submits == 1,
          "native sampler binding255/Metal0 dispatch");
    m = snapshot();
    check(get64(m + 768 + 40) == 256u * 32u, "table2 sized for native entry255");
    check((get32(m + 9024 + 255u * 32u) & 15u) == 1u, "table2[255] sampler descriptor");
    check_zero(m + 9024, 255u * 32u, "table2 sparse entries 0..254 zero");
    close_fixture();
}

static void test_srt_pointer(void)
{
    uint64_t out = 0;
    check(n2mb_compute_srt_pointer(0x1004, 0x10000, 4, &out) != 0,
          "SRT pointer rejects unaligned address");
    check(n2mb_compute_srt_pointer(0x1000, 0x10000, 6, &out) != 0,
          "SRT pointer rejects entry count not multiple of four");
    check(n2mb_compute_srt_pointer(0x1000, 0x1040, 8, &out) != 0,
          "SRT pointer rejects table larger than limit");
    check(n2mb_compute_srt_pointer(0x1000, 0x10000, 4, &out) == 0 && (out & 63u) == 4u,
          "SRT pointer tags four tables");
}

int main(void)
{
    test_full_positive();
    test_dummy_sampler();
    test_v1_regression();
    test_refusals();
    test_swizzle_and_atomic();
    test_readonly_image();
    test_sampler_compare_flag();
    test_v2_buffer_only();
    test_array_layers();
    test_sparse_sampler();
    test_srt_pointer();
    test_view_level();
    test_mip_layers();
    test_3d_mips();
    test_17_layers();
    printf("n2mb v2 job: %u checks, %u required failures, %u pending failures\n",
           checks, required_failures, pending_failures);
    return required_failures ? 1 : 0;
}
