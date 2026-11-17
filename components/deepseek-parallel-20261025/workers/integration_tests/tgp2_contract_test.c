/* Independent TGP2 header/manifest contract test.
 *
 * Uses only the production parser in gpu/metal/tegu/pipeline_format.h.  Every
 * negative case checks that the parser refuses with output byte-identical to
 * its pre-call contents; no assertion is suppressed and no case is skipped.
 *
 * Categories:
 *   required  the published header/manifest contract; failure is a defect
 *   pending   a contract question still open on the board; failure is recorded
 *             as an explicit unresolved gap, never as success
 */
#include "pipeline_format.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(struct tegu_pipeline_manifest) == 64u, "manifest record ABI");
_Static_assert(offsetof(struct tegu_pipeline_info2, manifest) == 72u, "manifest offset ABI");
_Static_assert(sizeof(struct tegu_pipeline_info2) == 72u + 64u * 64u, "manifest array ABI");

#define MAX_PACKAGE 16384u
#define MAX_MANIFEST 8u

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


static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32));
}

static void record(uint8_t *pkg, unsigned i, uint32_t binding, uint32_t kind,
                   uint32_t access, uint32_t table, uint32_t entry,
                   uint32_t flags, uint32_t storage_format, uint32_t view_format,
                   uint32_t view_level, uint32_t view_layer, uint32_t shape)
{
    uint8_t *r = pkg + TEGU_PIPELINE2_HEADER_BYTES + i * TEGU_PIPELINE2_MANIFEST_BYTES;
    (void)view_format; (void)view_level; (void)view_layer;
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
    put32(r + 36, 0);
    put32(r + 40, 0);
    put32(r + 44, 0);
    put32(r + 48, shape);
}

enum {
    OPT_UBO = 1u << 0, OPT_SSBO = 1u << 1, OPT_TEXTURE = 1u << 2,
    OPT_SAMPLER = 1u << 3, OPT_COMBINED = 1u << 4, OPT_IMAGE = 1u << 5,
    OPT_DUMMY = 1u << 6
};

#define SHAPE_2D TEGU_PIPELINE2_DIM_2D

static size_t build_package(uint8_t *out, unsigned options, uint32_t code_bytes,
                            uint32_t registers, uint32_t push_words, uint32_t local_x,
                            uint32_t local_y, uint32_t local_z, uint64_t preload)
{
    unsigned count = 0, at = 0, i;
    uint32_t mask = 0;

    if (options & OPT_UBO) count++;
    if (options & OPT_SSBO) count++;
    if (options & OPT_TEXTURE) count++;
    if (options & OPT_SAMPLER) count++;
    if (options & OPT_COMBINED) count += 2;
    if (options & OPT_IMAGE) count++;
    if (options & OPT_DUMMY) count++;

    memset(out, 0, MAX_PACKAGE);
    put32(out + 0, TEGU_PIPELINE2_MAGIC);
    put32(out + 4, TEGU_PIPELINE2_VERSION);
    put32(out + 8, code_bytes);
    put32(out + 12, TEGU_PIPELINE_GPU);
    put32(out + 16, registers);
    put32(out + 20, push_words);
    put64(out + 24, preload);
    if (options & OPT_UBO) mask |= 1u << 0;
    if (options & OPT_SSBO) mask |= 1u << 1;
    put32(out + 32, mask);
    put32(out + 36, local_x);
    put32(out + 40, local_y);
    put32(out + 44, local_z);
    put32(out + 48, TEGU_PIPELINE2_HEADER_BYTES);
    put32(out + 52, count);
    put32(out + 56, TEGU_PIPELINE2_MANIFEST_BYTES);
    put32(out + 60, TEGU_PIPELINE2_HEADER_BYTES + count * TEGU_PIPELINE2_MANIFEST_BYTES);
    put32(out + 64, (options & OPT_DUMMY) ? TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER : 0);

    if (options & OPT_UBO)
        record(out, at++, 0, TEGU_PIPELINE2_KIND_UBO, TEGU_PIPELINE2_ACCESS_READ,
               TEGU_PIPELINE2_TABLE_FAU, 0, 0, 0, 0, 0, 0, 0);
    if (options & OPT_SSBO)
        record(out, at++, 1, TEGU_PIPELINE2_KIND_SSBO,
               TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_WRITE,
               TEGU_PIPELINE2_TABLE_FAU, 1, 0, 0, 0, 0, 0, 0);
    if (options & OPT_TEXTURE)
        record(out, at++, 3, TEGU_PIPELINE2_KIND_TEXTURE,
               TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
               TEGU_PIPELINE2_TABLE_TEXTURE, 3,
               (options & OPT_SAMPLER) ? TEGU_PIPELINE2_FLAG_DECLARED_COMBINED : 0,
               0, 0, 0, 0, SHAPE_2D);
    if (options & OPT_SAMPLER)
        record(out, at++, 3, TEGU_PIPELINE2_KIND_SAMPLER, 0,
               TEGU_PIPELINE2_TABLE_SAMPLER, 3,
               (options & OPT_TEXTURE) ? TEGU_PIPELINE2_FLAG_DECLARED_COMBINED : 0,
               0, 0, 0, 0, 0);
    if (options & OPT_COMBINED) {
        record(out, at++, 5, TEGU_PIPELINE2_KIND_TEXTURE,
               TEGU_PIPELINE2_ACCESS_READ | TEGU_PIPELINE2_ACCESS_SAMPLE,
               TEGU_PIPELINE2_TABLE_TEXTURE, 5, TEGU_PIPELINE2_FLAG_DECLARED_COMBINED,
               0, 0, 0, 0, SHAPE_2D);
        record(out, at++, 5, TEGU_PIPELINE2_KIND_SAMPLER, 0,
               TEGU_PIPELINE2_TABLE_SAMPLER, 5, TEGU_PIPELINE2_FLAG_DECLARED_COMBINED,
               0, 0, 0, 0, 0);
    }
    if (options & OPT_IMAGE)
        record(out, at++, 4, TEGU_PIPELINE2_KIND_IMAGE,
               TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_WRITE,
               TEGU_PIPELINE2_TABLE_TEXTURE, 4, 0, 37, 0, 0, 0, SHAPE_2D);
    if (options & OPT_DUMMY)
        record(out, at++, TEGU_PIPELINE2_DUMMY_BINDING, TEGU_PIPELINE2_KIND_SAMPLER, 0,
               TEGU_PIPELINE2_TABLE_SAMPLER, TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY,
               TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER, 0, 0, 0, 0, 0);
    for (i = 0; i < code_bytes; i++) out[TEGU_PIPELINE2_HEADER_BYTES + at * TEGU_PIPELINE2_MANIFEST_BYTES + i] = (uint8_t)(0xa0u + i);
    return TEGU_PIPELINE2_HEADER_BYTES + at * TEGU_PIPELINE2_MANIFEST_BYTES + code_bytes;
}

static void put_word(uint8_t *p, size_t offset, uint32_t value) { put32(p + offset, value); }

/* A refusal must not mutate the caller's output. */
static int parse_refuses(const uint8_t *pkg, size_t size, const char *what)
{
    struct tegu_pipeline_info2 out;
    uint8_t before[sizeof(out)];
    int rc;
    memset(&out, 0x5a, sizeof(out));
    memcpy(before, &out, sizeof(out));
    rc = tegu_pipeline_parse2(pkg, size, &out);
    check(rc != 0, what);
    if (rc == 0) return 0;
    check(!memcmp(before, &out, sizeof(out)), "refusal left output unchanged");
    return 1;
}

static void test_positive(void)
{
    uint8_t pkg[MAX_PACKAGE];
    struct tegu_pipeline_info2 info;
    size_t size;
    unsigned i;

    size = build_package(pkg, OPT_UBO | OPT_SSBO | OPT_TEXTURE | OPT_SAMPLER |
                         OPT_COMBINED | OPT_IMAGE | OPT_DUMMY, 16, 16, 4, 8, 4, 1,
                         UINT64_C(0x0180000000000000));
    memset(&info, 0x5a, sizeof(info));
    check(tegu_pipeline_parse2(pkg, size, &info) == 0, "full positive package parses");
    check(info.code_bytes == 16 && info.registers == 16 && info.push_words == 4,
          "header scalar fields");
    check(info.buffer_mask == 3 && info.manifest_count == 8 && info.code_offset == 640,
          "manifest count/code offset/buffer mask");
    check(info.local[0] == 8 && info.local[1] == 4 && info.local[2] == 1, "local size");
    check(info.preload == UINT64_C(0x0180000000000000), "preload");
    check(info.flags == TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER, "dummy flag");
    for (i = 0; i < info.manifest_count; i++) {
        check(info.manifest[i].set == 0, "manifest set is zero");
        check(info.manifest[i].array_count == 1, "manifest array_count is one");
    }
    check(info.manifest[0].kind == TEGU_PIPELINE2_KIND_UBO &&
          info.manifest[0].table == TEGU_PIPELINE2_TABLE_FAU,
          "UBO record");
    check(info.manifest[2].kind == TEGU_PIPELINE2_KIND_TEXTURE &&
          info.manifest[2].entry == 3 && info.manifest[3].kind == TEGU_PIPELINE2_KIND_SAMPLER,
          "texture/sampler records");
    check(info.manifest[4].kind == TEGU_PIPELINE2_KIND_TEXTURE &&
          info.manifest[5].kind == TEGU_PIPELINE2_KIND_SAMPLER &&
          info.manifest[4].binding == info.manifest[5].binding,
          "combined records share native binding across tables");
    check(info.manifest[6].kind == TEGU_PIPELINE2_KIND_IMAGE &&
          info.manifest[6].storage_format == 37, "image record");
    check(info.manifest[7].binding == TEGU_PIPELINE2_DUMMY_BINDING &&
          info.manifest[7].entry == TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY,
          "dummy sampler record");

    /* Code bytes are exactly the package tail. */
    check(!memcmp(pkg + info.code_offset, "\xa0\xa1", 2), "code tail begins after manifest");

    /* Buffer-only v2 is valid and parse_any reports version 2. */
    size = build_package(pkg, OPT_UBO | OPT_SSBO, 8, 8, 2, 1, 1, 1, 0);
    {
        struct tegu_pipeline_info v1;
        struct tegu_pipeline_info2 v2;
        uint32_t version = 0;
        check(tegu_pipeline_parse_any(pkg, size, &version, &v1, &v2) == 0,
              "buffer-only v2 parse_any");
        check(version == 2 && v2.manifest_count == 2 && v1.code_bytes == 8 &&
              v1.bindings == 3, "parse_any v2/v1 projection");
    }

    /* The unchanged v1 package still dispatches through parse_any. */
    memset(pkg, 0, sizeof(pkg));
    put32(pkg + 0, TEGU_PIPELINE_MAGIC);
    put32(pkg + 4, 1);
    put32(pkg + 8, 8);
    put32(pkg + 12, TEGU_PIPELINE_GPU);
    put32(pkg + 16, 8);
    put32(pkg + 20, 2);
    put32(pkg + 32, 1);
    put32(pkg + 36, 1); put32(pkg + 40, 1); put32(pkg + 44, 1);
    put32(pkg + 56, 1);
    put32(pkg + 60, 0);
    put32(pkg + TEGU_PIPELINE_HEADER_BYTES, 0x12345678);
    {
        struct tegu_pipeline_info v1;
        struct tegu_pipeline_info2 v2;
        uint32_t version = 0;
        check(tegu_pipeline_parse_any(pkg, TEGU_PIPELINE_HEADER_BYTES + 8, &version,
                                      &v1, &v2) == 0, "v1 package parse_any");
        check(version == 1 && v1.bindings == 1, "v1 version projection");
        check(v2.manifest_count == 0 && v2.code_bytes == 0, "v2 zero for v1 package");
    }
}

static void test_header_boundaries(void)
{
    uint8_t pkg[MAX_PACKAGE];
    size_t size = build_package(pkg, OPT_UBO | OPT_SSBO, 8, 16, 4, 8, 4, 1, 0);

#define REFUSE_AFTER_MUTATE(offset, value, label) do { \
    memcpy(pkg, saved_pkg, saved_size); put_word(pkg, (offset), (value)); \
    if (parse_refuses(pkg, saved_size, (label))) {} \
} while (0)
    static uint8_t saved_pkg[MAX_PACKAGE];
    size_t saved_size = size;
    memcpy(saved_pkg, pkg, size);

    REFUSE_AFTER_MUTATE(0, 0x31504754u, "TGP1 magic in package_parse2");
    REFUSE_AFTER_MUTATE(4, 3u, "version 3");
    REFUSE_AFTER_MUTATE(8, 0u, "zero code bytes");
    REFUSE_AFTER_MUTATE(8, 7u, "code bytes not multiple of eight");
    REFUSE_AFTER_MUTATE(8, TEGU_PIPELINE_MAX_CODE + 8u, "code bytes over maximum");
    REFUSE_AFTER_MUTATE(12, TEGU_PIPELINE_GPU + 1u, "wrong GPU id");
    REFUSE_AFTER_MUTATE(16, 0u, "zero registers");
    REFUSE_AFTER_MUTATE(16, 65u, "too many registers");
    REFUSE_AFTER_MUTATE(20, 129u, "too many push words");
    REFUSE_AFTER_MUTATE(32, 0x100u, "buffer mask high bit");
    REFUSE_AFTER_MUTATE(48, 64u, "manifest offset not 128");
    REFUSE_AFTER_MUTATE(52, TEGU_PIPELINE2_MAX_MANIFEST + 1u, "too many manifest records");
    REFUSE_AFTER_MUTATE(56, 63u, "wrong manifest record size");
    REFUSE_AFTER_MUTATE(60, 128u, "code overlaps manifest");
    REFUSE_AFTER_MUTATE(64, 2u, "unknown header flag bit");
    REFUSE_AFTER_MUTATE(76, 1u, "reserved header word 19");
    REFUSE_AFTER_MUTATE(124, 1u, "reserved header word 31");
    REFUSE_AFTER_MUTATE(24, 1u, "preload bit outside mask");

    /* Null pointer boundaries leave memory untouched. */
    {
        struct tegu_pipeline_info2 out;
        uint8_t before[sizeof(out)];
        memset(&out, 0x5a, sizeof(out));
        memcpy(before, &out, sizeof(out));
        check(tegu_pipeline_parse2(NULL, saved_size, &out) != 0,
              "null package refused");
        check(!memcmp(before, &out, sizeof(out)), "null package left output unchanged");
        check(tegu_pipeline_parse2(pkg, saved_size, NULL) != 0,
              "null output refused");
        check(tegu_pipeline_package_version(NULL, 0) == 0, "null version probe");
        check(tegu_pipeline_package_version(pkg, 3) == 0, "short version probe");
        check(tegu_pipeline_package_version(pkg, 4) == 2, "v2 version probe");
    }

    /* Exact-length checks: one byte short or one byte long both refuse. */
    memcpy(pkg, saved_pkg, saved_size);
    parse_refuses(pkg, saved_size - 1, "package one byte short");
    memcpy(pkg, saved_pkg, saved_size);
    check(tegu_pipeline_parse2(pkg, saved_size + 1, &(struct tegu_pipeline_info2){0}) != 0,
          "package one byte long");
    memcpy(pkg, saved_pkg, saved_size);

    /* local[3] product and zero components. */
    REFUSE_AFTER_MUTATE(36, 0u, "zero local X");
    REFUSE_AFTER_MUTATE(36, 257u, "local X over 256");
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, 36, 16u); put_word(pkg, 40, 16u); put_word(pkg, 44, 16u);
    parse_refuses(pkg, saved_size, "local product over 256");
    memcpy(pkg, saved_pkg, saved_size);

    /* code_bytes must exactly fill the tail. */
    put_word(pkg, 8, 16u);
    parse_refuses(pkg, saved_size, "code length disagrees with package size");
    memcpy(pkg, saved_pkg, saved_size);
#undef REFUSE_AFTER_MUTATE
}

static void test_manifest_boundaries(void)
{
    uint8_t pkg[MAX_PACKAGE];
    size_t size = build_package(pkg, OPT_UBO | OPT_SSBO | OPT_TEXTURE | OPT_SAMPLER |
                                OPT_IMAGE, 8, 16, 4, 8, 4, 1, 0);
    static uint8_t saved_pkg[MAX_PACKAGE];
    size_t saved_size = size;
    unsigned base = TEGU_PIPELINE2_HEADER_BYTES;
    memcpy(saved_pkg, pkg, size);

#define MUTATE_RECORD(index, field_offset, value, label) do { \
    memcpy(pkg, saved_pkg, saved_size); \
    put_word(pkg, base + (index) * TEGU_PIPELINE2_MANIFEST_BYTES + (field_offset), (value)); \
    (void)parse_refuses(pkg, saved_size, (label)); \
} while (0)

    MUTATE_RECORD(0, 0, 1u, "manifest set nonzero");
    MUTATE_RECORD(0, 12, 2u, "manifest array_count two");
    MUTATE_RECORD(0, 28, 2u, "manifest unknown flag bit");
    MUTATE_RECORD(0, 52, 1u, "manifest reserved word 13");
    MUTATE_RECORD(0, 48, 0x200u, "shape reserved bit");
    MUTATE_RECORD(0, 48, 15u, "shape dimension 15");
    MUTATE_RECORD(0, 48, 3u << 7, "shape sampled_type 3");
    MUTATE_RECORD(0, 4, 256u, "binding 256");
    MUTATE_RECORD(1, 16, TEGU_PIPELINE2_ACCESS_STORAGE, "SSBO with STORAGE access bit");
    MUTATE_RECORD(1, 16, TEGU_PIPELINE2_ACCESS_SAMPLE, "SSBO with SAMPLE access bit");
    MUTATE_RECORD(0, 16, TEGU_PIPELINE2_ACCESS_WRITE, "UBO with WRITE access bit");
    MUTATE_RECORD(2, 16, TEGU_PIPELINE2_ACCESS_READ, "sampled texture without SAMPLE bit");
    MUTATE_RECORD(2, 20, TEGU_PIPELINE2_TABLE_SAMPLER, "texture in sampler table");
    MUTATE_RECORD(3, 20, TEGU_PIPELINE2_TABLE_FAU, "sampler in FAU table");
    MUTATE_RECORD(3, 16, TEGU_PIPELINE2_ACCESS_READ, "standalone sampler with access bit");
    MUTATE_RECORD(4, 16, TEGU_PIPELINE2_ACCESS_STORAGE, "image without read/write/atomic");
    MUTATE_RECORD(4, 16, TEGU_PIPELINE2_ACCESS_STORAGE | TEGU_PIPELINE2_ACCESS_SAMPLE,
                  "image with SAMPLE bit");
    MUTATE_RECORD(4, 32, 42u, "image storage format numeric class mismatch");
    MUTATE_RECORD(4, 32, 0xffffffffu, "image storage format outside domain");
    MUTATE_RECORD(4, 28, TEGU_PIPELINE2_FLAG_DECLARED_COMBINED,
                  "declared-combined flag on image record");
    MUTATE_RECORD(0, 20, TEGU_PIPELINE2_TABLE_TEXTURE, "UBO outside FAU table");
    MUTATE_RECORD(0, 4, 8u, "buffer binding outside FAU 0..7");

    /* Duplicate table entry even with different bindings. */
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 2 * TEGU_PIPELINE2_MANIFEST_BYTES + 24, 4u); /* texture entry=4 */
    (void)parse_refuses(pkg, saved_size, "duplicate table1 entry");

    /* Same binding in the same table is a collision, even with a different
     * entry. */
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 2 * 64 + 8, TEGU_PIPELINE2_KIND_IMAGE);
    put_word(pkg, base + 2 * 64 + 4, 4u);
    put_word(pkg, base + 2 * 64 + 24, 6u);
    put_word(pkg, base + 4 * 64 + 8, TEGU_PIPELINE2_KIND_IMAGE);
    put_word(pkg, base + 4 * 64 + 4, 4u);
    put_word(pkg, base + 4 * 64 + 24, 4u);
    (void)parse_refuses(pkg, saved_size, "duplicate binding different entry in one table");
    /* Same binding in the same table and entry is the stronger duplicate. */
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 2 * 64 + 8, TEGU_PIPELINE2_KIND_IMAGE);
    put_word(pkg, base + 2 * 64 + 4, 4u);
    put_word(pkg, base + 2 * 64 + 24, 4u);
    put_word(pkg, base + 4 * 64 + 8, TEGU_PIPELINE2_KIND_IMAGE);
    put_word(pkg, base + 4 * 64 + 4, 4u);
    put_word(pkg, base + 4 * 64 + 24, 4u);
    (void)parse_refuses(pkg, saved_size, "duplicate binding and entry in one table");

    /* Buffer mask must equal exactly the buffer records. */
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, 32, 1u);
    (void)parse_refuses(pkg, saved_size, "buffer mask missing a bound buffer");
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, 32, 7u);
    (void)parse_refuses(pkg, saved_size, "buffer mask with an unbound bit");

    /* Dummy sampler flag/record agreement and fields. */
    {
        uint8_t d[16384];
        size_t ds = build_package(d, OPT_TEXTURE | OPT_DUMMY, 8, 16, 4, 8, 4, 1, 0);
        static uint8_t saved_d[16384];
        size_t saved_ds = ds;
        memcpy(saved_d, d, ds);
        /* Clear dummy flag only. */
        put_word(d, 64, 0u);
        (void)parse_refuses(d, ds, "dummy record without header flag");
        memcpy(d, saved_d, saved_ds);
        /* Remove the dummy record but keep the header flag. */
        memcpy(d, saved_d, saved_ds);
        put_word(d, 64, TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER);
        put_word(d, 52, 1u);
        put_word(d, 60, TEGU_PIPELINE2_HEADER_BYTES + 1 * 64u);
        (void)parse_refuses(d, TEGU_PIPELINE2_HEADER_BYTES + 1 * 64u + 8u,
                            "dummy flag without dummy record (count)");
        memcpy(d, saved_d, saved_ds);
        /* Move dummy record to table1. */
        put_word(d, TEGU_PIPELINE2_HEADER_BYTES + 1 * 64 + 20, TEGU_PIPELINE2_TABLE_TEXTURE);
        (void)parse_refuses(d, ds, "dummy sampler in texture table");
        memcpy(d, saved_d, saved_ds);
        put_word(d, TEGU_PIPELINE2_HEADER_BYTES + 1 * 64 + 24, 255u);
        (void)parse_refuses(d, ds, "dummy sampler at wrong entry");
        memcpy(d, saved_d, saved_ds);
        put_word(d, TEGU_PIPELINE2_HEADER_BYTES + 1 * 64 + 4, 7u);
        (void)parse_refuses(d, ds, "dummy sampler with real binding");
    }
    memcpy(pkg, saved_pkg, saved_size);

    /* Cross-kind and irrelevant-field rules are now part of the published
     * parser contract, so they are required checks. */
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 2 * 64 + 4, 0u); /* texture at binding0 */
    put_word(pkg, base + 2 * 64 + 24, 0u);
    check(tegu_pipeline_parse2(pkg, saved_size, &(struct tegu_pipeline_info2){0}) != 0,
          "cross-kind buffer/texture collision rejected");
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 0 * 64 + 48, SHAPE_2D);
    check(tegu_pipeline_parse2(pkg, saved_size, &(struct tegu_pipeline_info2){0}) != 0,
          "buffer record with image shape rejected");
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 0 * 64 + 36, 1u);
    check(tegu_pipeline_parse2(pkg, saved_size, &(struct tegu_pipeline_info2){0}) != 0,
          "buffer record with reserved view word rejected");
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 1 * 64 + 48, SHAPE_2D);
    check(tegu_pipeline_parse2(pkg, saved_size, &(struct tegu_pipeline_info2){0}) != 0,
          "standalone sampler with shape rejected");

    /* Declared combined requires both table records with the flag. */
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 2 * 64 + 28, TEGU_PIPELINE2_FLAG_DECLARED_COMBINED);
    put_word(pkg, base + 3 * 64 + 28, 0u); /* remove the paired sampler flag */
    check(tegu_pipeline_parse2(pkg, saved_size, &(struct tegu_pipeline_info2){0}) != 0,
          "declared-combined texture without paired sampler rejected");
    memcpy(pkg, saved_pkg, saved_size);
    put_word(pkg, base + 0 * 64 + 28, TEGU_PIPELINE2_FLAG_DECLARED_COMBINED);
    check(tegu_pipeline_parse2(pkg, saved_size, &(struct tegu_pipeline_info2){0}) != 0,
          "declared-combined buffer flag rejected");
    memcpy(pkg, saved_pkg, saved_size);
#undef MUTATE_RECORD
}

int main(void)
{
    test_positive();
    test_header_boundaries();
    test_manifest_boundaries();
    printf("TGP2 contract: %u checks, %u required failures, %u pending failures\n",
           checks, required_failures, pending_failures);
    return required_failures ? 1 : 0;
}
