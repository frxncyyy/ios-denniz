#include "generic_job.h"
#include "tegu_memory.h"

/* CSF opcodes emitted by the qualified builders, plus their direct
 * fullscreen/fragment-finish relatives.  Anything that reads or writes
 * arbitrary GPU memory or redirects the stream is refused. */
int tegu_generic_job_command_allowed(uint64_t command)
{
    switch ((unsigned)(command >> 56)) {
    case 0:  /* NOP */
    case 1:  /* MOVE48 */
    case 2:  /* MOVE32 */
    case 3:  /* WAIT */
    case 6:  /* RUN_IDVS */
    case 7:  /* RUN_FRAGMENT */
    case 8:  /* RUN_FULLSCREEN */
    case 9:  /* FINISH_TILING */
    case 11: /* FINISH_FRAGMENT */
    case 23: /* SET_SB_ENTRY */
    case 34: /* REQ_RESOURCE */
    case 36: /* FLUSH_CACHE2 */
    case 48: /* HEAP_SET */
    case 49: /* HEAP_OPERATION */
        return 0;
    default:
        return -1;
    }
}

/* Shared by TGJ1 and TGJ2: identical header rules, per-ABI array limits.
 * Both requests start with the same header fields, read here by name. */
#define TGJ_HEADER_ARGS(r) (r)->version, (r)->flags, (r)->width, (r)->height, (r)->row_stride, \
    (r)->span_count, (r)->command_count, (r)->reserved, (r)->target_token, (r)->target_offset, \
    (r)->target_length, (r)->target_bytes, (r)->scratch_token, (r)->scratch_offset, (r)->exec_mask
static int tegu_generic_job_plan_core(uint32_t want_version, uint32_t max_spans, uint32_t max_commands,
    uint32_t version, uint32_t flags, uint32_t width, uint32_t height, uint32_t row_stride,
    uint32_t span_count, uint32_t command_count, uint32_t reserved, uint64_t target_token,
    uint64_t target_offset, uint64_t target_length, uint64_t target_bytes, uint64_t scratch_token,
    uint64_t scratch_offset, uint64_t exec_mask, const struct tegu_generic_job_span *spans,
    const uint64_t *commands, struct tegu_generic_job_plan *out)
{
    if (!out || version != want_version ||
        (flags & ~TEGU_GENERIC_JOB_FLAGS) || reserved ||
        span_count > max_spans ||
        !command_count || command_count > max_commands ||
        !target_token || !scratch_token ||
        (target_offset & (FS_PAGE_SIZE - 1u)) ||
        (scratch_offset & (FS_PAGE_SIZE - 1u)) ||
        (exec_mask & ~FS_V7_SCRATCH_EXEC_ALLOWED))
        return -1;
    /* The 2 MiB heap is mandatory above the 16x16 qualification target. */
    int heap2m = width > 16 || height > 16;
    if (heap2m != !!(flags & TEGU_GENERIC_JOB_HEAP2M)) return -1;
    for (uint32_t i = 0; i < command_count; i++)
        if (tegu_generic_job_command_allowed(commands[i])) return -2;
    struct tegu_generic_job_plan p;
    memset(&p, 0, sizeof(p));
    for (uint32_t i = 0; i < span_count; i++) {
        const struct tegu_generic_job_span *s = &spans[i];
        if (!s->token || !s->length || s->length > UINT32_MAX ||
            (s->offset & (FS_PAGE_SIZE - 1u)) || s->offset > UINT64_MAX - s->length)
            return -3;
        uint64_t pages = (s->length + FS_PAGE_SIZE - 1u) / FS_PAGE_SIZE;
        if (pages > FS_MAX_RESOURCE_PAGES - p.resource_pages) return -3;
        p.span_pages[i] = (uint32_t)pages;
        p.resource_pages += (uint32_t)pages;
    }
    if (tegu_fs_surface_layout_with_heap(width, height, row_stride,
            target_offset, p.resource_pages, FS_V7_SCRATCH_PAGES,
            heap2m ? FS_HEAP_PAGES : 0, &p.layout) ||
        !tegu_fs_surface_visible_span_fits(&p.layout, target_bytes,
            target_length > target_offset ? target_length - target_offset : 0))
        return -4;
    /* 0204: refuse here what fs_map's 32 MiB AS1 lock cannot cover; a failed
     * map inside tg_submit stops the GPU queue. */
    if (p.layout.total_pages > TEGU_GENERIC_JOB_APERTURE_PAGES) return -4;
    /* Mirrors tegu_fullscreen_backing_create_v7*: scratch, target, spans, heap. */
    p.scratch_va = FS_APERTURE_VA;
    p.target_va = FS_APERTURE_VA + (uint64_t)FS_V7_SCRATCH_PAGES * FS_PAGE_SIZE;
    uint64_t page = FS_V7_SCRATCH_PAGES + (uint64_t)p.layout.target_pages;
    for (uint32_t i = 0; i < span_count; i++) {
        p.span_va[i] = FS_APERTURE_VA + page * FS_PAGE_SIZE;
        page += p.span_pages[i];
    }
    p.heap_va = heap2m ? FS_APERTURE_VA + page * FS_PAGE_SIZE : 0;
    *out = p;
    return 0;
}

int tegu_generic_job_plan(const struct tegu_generic_job_request *r,
    struct tegu_generic_job_plan *out)
{
    if (!r) return -1;
    return tegu_generic_job_plan_core(TEGU_GENERIC_JOB_VERSION, TEGU_GENERIC_JOB_MAX_SPANS,
        TEGU_GENERIC_JOB_MAX_COMMANDS, TGJ_HEADER_ARGS(r), r->spans, r->commands, out);
}

int tegu_generic_job2_plan(const struct tegu_generic_job2_request *r,
    struct tegu_generic_job_plan *out)
{
    if (!r) return -1;
    return tegu_generic_job_plan_core(TEGU_GENERIC_JOB2_VERSION, TEGU_GENERIC_JOB2_MAX_SPANS,
        TEGU_GENERIC_JOB2_MAX_COMMANDS, TGJ_HEADER_ARGS(r), r->spans, r->commands, out);
}
