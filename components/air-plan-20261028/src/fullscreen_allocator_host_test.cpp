#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static unsigned long gPhysBase = 0x880000000UL;
static unsigned long gPhysSize = 0x40000000UL;
static const unsigned page_size = 16384;
static long heap_calls, heap_fail_at, heap_live;
static long buffer_calls, buffer_fail_at, buffer_live;
static uint64_t buffer_charged, quota_limit=UINT64_MAX;
static uint64_t next_pa = 0x883000000ULL;
static uint64_t forced_next_pa;
static int buffer_frees;

void *IOMallocZero(size_t n) {
    ++heap_calls;
    if (heap_fail_at && heap_calls == heap_fail_at) return nullptr;
    void *p = calloc(1, n);
    if (p) ++heap_live;
    return p;
}
void IOFree(void *p, size_t) { if (p) { --heap_live; free(p); } }
void IOLog(const char *, ...) {}
int64_t OSAddAtomic64(int64_t v, volatile int64_t *p) { int64_t old=*p; *p+=v; return old; }
void OSMemoryBarrier(void) { __sync_synchronize(); }
int OSCompareAndSwapPtr(void *oldp, void *newp, void * volatile *p) {
    return __sync_bool_compare_and_swap(p, oldp, newp);
}

#include "TeguGPUFullscreenAllocation.h"
extern "C" {
#include "fullscreen_map.c"
}
struct TeguGPUBuffer : public IOBufferMemoryDescriptor {
public:
    static TeguGPUBuffer *allocate(uint64_t bytes) {
        ++buffer_calls;
        if (buffer_fail_at && buffer_calls == buffer_fail_at) return nullptr;
        auto *b = new TeguGPUBuffer;
        b->logical = bytes;
        b->rounded = (bytes + page_size - 1) & ~((uint64_t)page_size - 1);
        if (b->rounded > quota_limit - buffer_charged) { delete b; return nullptr; }
        if (posix_memalign((void **)&b->cpu, 4096, b->rounded)) { delete b; return nullptr; }
        memset(b->cpu, 0, b->rounded);
        b->pa = forced_next_pa ? forced_next_pa : next_pa;
        forced_next_pa = 0;
        next_pa += b->rounded;
        buffer_charged += b->rounded;
        ++buffer_live;
        return b;
    }
    bool gpuPage(uint64_t off, uint64_t *out) {
        if (!out || (off & 4095) || off > rounded || rounded - off < 4096 || fail_page) return false;
        *out = pa + (duplicate_pages ? 0 : off);
        return true;
    }
    uint64_t logicalLength() const { return logical; }
    uint64_t getLength() const { return rounded; }
    void *getBytesNoCopy() { return cpu; }
    void retain() { ++refs; }
    void release() { if (--refs == 0) { --buffer_live; ++buffer_frees; buffer_charged-=rounded; free(cpu); delete this; } }
    uint64_t basePA() const { return pa; }
    void setBasePA(uint64_t v) { pa=v; }
    void setDuplicatePages(bool v) { duplicate_pages = v; }
    bool fail_page = false;
private:
    uint64_t logical=0, rounded=0, pa=0;
    unsigned refs=1;
    bool duplicate_pages=false;
    void *cpu=nullptr;
};

struct WriteFault { volatile uint64_t *unused; int fail_after=-1, writes=0; };
struct CallbackContext { int lock_result=0, flush_result=0, sync_result=0; WriteFault write; };
extern "C" {
static int test_lock(void *ctx, uint64_t, unsigned) { return ((CallbackContext *)ctx)->lock_result; }
static int test_write(void *ctx, volatile uint64_t *p, uint64_t v) {
    auto *w=&((CallbackContext *)ctx)->write;
    if (w->fail_after >= 0 && w->writes++ == w->fail_after) return -1;
    *p=v; return 0;
}
static int test_sync(void *ctx,volatile uint64_t *table,size_t bytes) { assert(table&&bytes==4096);return ((CallbackContext *)ctx)->sync_result; }
static int test_flush(void *ctx) { return ((CallbackContext *)ctx)->flush_result; }
}

#include "TeguGPUFullscreenAllocation.inc"
#include "generic_job.c"

static struct tegu_fs_surface_layout layout_for(uint32_t w=16, uint32_t h=16,
    uint32_t resource_pages=0, uint64_t offset=0) {
    struct tegu_fs_surface_layout l{};
    assert(!tegu_fs_surface_layout(w,h,((w*4+63)/64)*64,offset,resource_pages,&l));
    return l;
}
static struct tegu_fs_surface_layout heap_layout(uint32_t w=16,uint32_t h=16,
    uint32_t stride=64,uint32_t resource_pages=6) {
    struct tegu_fs_surface_layout l{};
    assert(!tegu_fs_surface_layout_with_heap(w,h,stride,0,resource_pages,
        FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES,&l));return l;
}
static struct tegu_fs_reserved_range exclusions[] = {
    {0x881000000ULL,0x881001000ULL},
    {0x880100000ULL,0x880200000ULL}
};
static struct tegu_fs_reserved_context reserved={exclusions,2};
static TeguGPUBuffer *buf(uint64_t n) { auto *b=TeguGPUBuffer::allocate(n); assert(b); return b; }
static struct tegu_fullscreen_backing *create(TeguGPUBuffer *s,TeguGPUBuffer *t,
    struct tegu_fs_surface_layout *l, volatile uint64_t *l2,
    uint64_t l2pa=TEGU_FS_QUEUE_L2_PA,
    const struct tegu_fs_reserved_context *r=&reserved,
    const struct tegu_fs_resource_span *rs=nullptr,size_t rn=0) {
    return tegu_fullscreen_backing_create(s,t,0,l,rs,rn,0,l2,l2pa,r);
}
static int initial_heap_live, initial_buffers;
static void reset_faults() { heap_calls=0; heap_fail_at=0; buffer_calls=0; buffer_fail_at=0; quota_limit=UINT64_MAX; }
static void clean_counts() { assert(heap_live==initial_heap_live); assert(buffer_live==initial_buffers); assert(buffer_charged==0); }

static void test_each_allocator_failure_stage() {
    for (int fail=1; fail<=10; ++fail) {
        reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto *r=buf(4096);
        auto l=layout_for(16,16,1);
        volatile uint64_t l2[512]={}; heap_fail_at=fail;
        struct tegu_fs_resource_span span={r,0,4096};
        auto *b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1);
        assert(!b); s->release(); t->release(); r->release(); clean_counts();
    }
    reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto *r=buf(4096);
    auto l=layout_for(16,16,1);
    volatile uint64_t l2[512]={}; buffer_fail_at=buffer_calls+1;
    struct tegu_fs_resource_span span={r,0,4096};
    auto *b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1);
    assert(!b); s->release(); t->release(); r->release(); clean_counts();
    reset_faults(); s=buf(0x40000); t=buf(4096); l=layout_for();
    volatile uint64_t l2b[512]={};
    quota_limit=buffer_charged+16384-1;
    assert(!create(s,t,&l,l2b));
    quota_limit=UINT64_MAX; s->release(); t->release(); clean_counts();
}

static void test_logical_bounds_and_staged_cleanup() {
    reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto *r=buf(4096);
    auto l=layout_for(); volatile uint64_t l2[512]={};
    struct tegu_fs_resource_span span={r,4096,4096};
    assert(!create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1));
    auto offset_layout=layout_for(16,16,0,4096);
    assert(!tegu_fullscreen_backing_create(s,t,4096,&offset_layout,nullptr,0,0,
        l2,TEGU_FS_QUEUE_L2_PA,&reserved));
    struct fs_ops ops{}; CallbackContext cb{}; cb.lock_result=1;
    ops.ctx=&cb; ops.lock_begin=test_lock; ops.write_entry=test_write; ops.sync_table=test_sync; ops.flush_unlock=test_flush;
    /* Allocation and PA staging succeeds; injected lock failure releases staged state. */
    auto *b=create(s,t,&l,l2); assert(b);
    cb.sync_result=1;
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED);
    for(unsigned i=0;i<512;i++)assert(!l2[i]);
    cb.sync_result=0;
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED);
    tegu_fullscreen_backing_finish(b,1);
    s->release(); t->release(); r->release(); clean_counts();
}

static void test_mapper_refusals_and_owned_references() {
    reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto l=layout_for();
    volatile uint64_t l2[512]={}; struct fs_ops ops{}; CallbackContext cb{};
    ops.ctx=&cb; ops.lock_begin=test_lock; ops.write_entry=test_write; ops.sync_table=test_sync; ops.flush_unlock=test_flush;
    s->setDuplicatePages(true);
    auto *b=create(s,t,&l,l2); assert(b);
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED);
    tegu_fullscreen_backing_finish(b,1); s->setDuplicatePages(false);
    b=create(s,t,&l,l2); assert(b);
    /* User drops both objects while backing owns them. */
    s->release(); t->release();
    assert(buffer_live==3);
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_KEEP_LIVE);
    tegu_fullscreen_backing_finish(b,1);
    assert(buffer_live==0); assert(buffer_frees>=2); clean_counts();
}

static void test_gpu_page_resolution_failures_release_staging() {
    reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto l=layout_for();
    volatile uint64_t l2[512]={};
    s->fail_page=true;
    assert(!create(s,t,&l,l2));
    s->fail_page=false; t->fail_page=true;
    assert(!create(s,t,&l,l2));
    t->fail_page=false;
    s->release(); t->release(); clean_counts();
}

static void test_reserved_and_page_table_aliases() {
    reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto l=layout_for();
    volatile uint64_t l2[512]={}; struct fs_ops ops{}; CallbackContext cb{};
    ops.ctx=&cb; ops.lock_begin=test_lock; ops.write_entry=test_write; ops.sync_table=test_sync; ops.flush_unlock=test_flush;
    struct tegu_fs_reserved_range deny={s->basePA(),s->basePA()+4096};
    struct tegu_fs_reserved_context rc={&deny,1};
    auto *b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&rc); assert(b);
    deny.begin=0x881000000ULL; deny.end=0x881001000ULL;
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED);
    tegu_fullscreen_backing_finish(b,1);
    auto *r=buf(4096); struct tegu_fs_resource_span span={r,0,4096};
    forced_next_pa=r->basePA(); /* Private PT page collides with an RO resource PA. */
    auto rl=layout_for(16,16,1);
    b=create(s,t,&rl,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1); assert(b);
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED);
    tegu_fullscreen_backing_finish(b,1);
    r->release(); s->release(); t->release(); clean_counts();
}

static void test_resource_aliases_keep_per_binding_contiguous_va() {
    reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto *r=buf(8192);
    auto l=layout_for(16,16,3); volatile uint64_t l2[512]={};
    struct tegu_fs_resource_span spans[3]={{r,100,96},{r,200,128},{r,4128,64}};
    auto *b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,spans,3); assert(b);
    assert(tegu_fullscreen_backing_resource_count(b)==3);
    uint64_t va0=0,va1=0,va2=0;
    assert(tegu_fullscreen_backing_resolve_resource(b,0,132,16,&va0));
    assert(tegu_fullscreen_backing_resolve_resource(b,1,208,32,&va1));
    assert(tegu_fullscreen_backing_resolve_resource(b,2,4144,16,&va2));
    assert(va0==FS_APERTURE_VA+(FS_SCRATCH_PAGES+1u)*FS_PAGE_SIZE+132);
    assert(va1==FS_APERTURE_VA+(FS_SCRATCH_PAGES+1u+1u)*FS_PAGE_SIZE+208);
    assert(va2==FS_APERTURE_VA+(FS_SCRATCH_PAGES+3u)*FS_PAGE_SIZE+48);
    uint64_t ignored;
    assert(!tegu_fullscreen_backing_resolve_resource(b,0,190,8,&ignored));
    const struct tegu_full_render_job *job=tegu_fullscreen_backing_job(b);
    assert(job && job->request && job->request->resource_count==3);
    assert(job->request->resources[0].pa==job->request->resources[1].pa);
    assert(job->request->resources[2].pa!=job->request->resources[0].pa);
    struct fs_ops ops{}; CallbackContext cb{};
    ops.ctx=&cb; ops.lock_begin=test_lock; ops.write_entry=test_write; ops.sync_table=test_sync; ops.flush_unlock=test_flush;
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_KEEP_LIVE);
    unsigned first=FS_SCRATCH_PAGES+job->request->target_count;
    assert(job->request->l3[0].entries[first]==
        (job->request->resources[0].pa|FS_PTE_RO_NX));
    assert(job->request->l3[0].entries[first+1]==
        (job->request->resources[1].pa|FS_PTE_RO_NX));
    assert(job->request->l3[0].entries[first+2]==
        (job->request->resources[2].pa|FS_PTE_RO_NX));
    r->release(); s->release(); t->release();
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_KEEP_LIVE);
    tegu_fullscreen_backing_finish(b,1);
    clean_counts();
}

static void test_forbidden_aliases_and_owner_range_refusals() {
    reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto *r=buf(4096);
    auto l=layout_for(16,16,1); volatile uint64_t l2[512]={};
    struct tegu_fs_resource_span span={r,0,4096}; struct fs_ops ops{}; CallbackContext cb{};
    ops.ctx=&cb; ops.lock_begin=test_lock; ops.write_entry=test_write; ops.sync_table=test_sync; ops.flush_unlock=test_flush;
    uint64_t original_rpa=r->basePA();
    assert(!create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA+FS_PAGE_SIZE,&reserved,&span,1));
    r->setBasePA(s->basePA()); auto *b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1); assert(b);
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED); tegu_fullscreen_backing_finish(b,1);
    r->setBasePA(t->basePA()); b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1); assert(b);
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED); tegu_fullscreen_backing_finish(b,1);
    r->setBasePA(TEGU_FS_QUEUE_L2_PA); b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1); assert(b);
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED); tegu_fullscreen_backing_finish(b,1);
    r->setBasePA(0x8d0000000ULL); b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1); assert(b);
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED); tegu_fullscreen_backing_finish(b,1);
    r->setBasePA(original_rpa); b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,&span,1); assert(b);
    auto *request=(struct fs_request *)tegu_fullscreen_backing_job(b)->request;
    struct fs_page *mutable_resources=(struct fs_page *)request->resources;
    uint64_t owner=mutable_resources[0].owner; mutable_resources[0].owner++;
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_RELEASE_STAGED);
    mutable_resources[0].owner=owner; tegu_fullscreen_backing_finish(b,1);
    unsigned long saved_base=gPhysBase, saved_size=gPhysSize;
    gPhysBase=0x8c1000000UL; gPhysSize=0x01000000UL;
    auto no_range=create(s,t,&l,l2); assert(!no_range);
    gPhysBase=saved_base; gPhysSize=saved_size;
    r->release(); s->release(); t->release(); clean_counts();
}

static void test_1080p_partial_page_and_small_resource_spans() {
    reset_faults();
    const uint32_t width=1080, height=2424, stride=4352;
    struct tegu_fs_surface_layout l{};
    assert(!tegu_fs_surface_layout(width,height,stride,0,2,&l));
    auto *s=buf(0x40000); auto *t=buf(l.target_bytes);
    auto *ubo=buf(96); auto *texture=buf(1024);
    struct tegu_fs_resource_span spans[2]={{ubo,0,96},{texture,0,1024}};
    volatile uint64_t l2[512]={};
    auto *b=create(s,t,&l,l2,TEGU_FS_QUEUE_L2_PA,&reserved,spans,2); assert(b);
    struct fs_ops ops{}; CallbackContext cb{};
    ops.ctx=&cb; ops.lock_begin=test_lock; ops.write_entry=test_write; ops.sync_table=test_sync; ops.flush_unlock=test_flush;
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_KEEP_LIVE);
    tegu_fullscreen_backing_finish(b,1);
    s->release(); t->release(); ubo->release(); texture->release();
    clean_counts();
}

/* TGJ1 userspace computes per-job VAs; they must equal the real v7 backing's. */
static void test_generic_job_plan_matches_backing_vas() {
    reset_faults();
    const uint64_t lengths[4]={288,3u*4096u,8192u+16u,4096u};
    struct tegu_generic_job_request r{};r.version=TEGU_GENERIC_JOB_VERSION;r.flags=TEGU_GENERIC_JOB_HEAP2M;
    r.width=1125;r.height=2436;r.row_stride=4608;r.span_count=4;r.command_count=1;r.commands[0]=1ull<<56;
    r.target_token=7;r.target_offset=0;r.target_length=0xab8000;r.target_bytes=11224980;
    r.scratch_token=9;r.exec_mask=FS_V7_SCRATCH_EXEC_ALLOWED;
    TeguGPUBuffer *res[4];struct tegu_fs_resource_span spans[4];
    for(unsigned i=0;i<4;i++){res[i]=buf(lengths[i]);r.spans[i]={100u+i,0,lengths[i]};spans[i]={res[i],0,(uint32_t)lengths[i]};}
    struct tegu_generic_job_plan p{};assert(!tegu_generic_job_plan(&r,&p));
    assert(p.resource_pages==1+3+3+1&&p.layout.target_pages==2741&&p.layout.heap_pages==FS_HEAP_PAGES);
    auto *s=buf(TEGU_GENERIC_JOB_SCRATCH_BYTES);auto *t=buf(0xab8000);volatile uint64_t l2[512]={};
    auto *b=tegu_fullscreen_backing_create_v7_heap2m(s,t,0,&p.layout,spans,4,
        FS_V7_SCRATCH_EXEC_ALLOWED,l2,TEGU_FS_QUEUE_L2_PA,&reserved);assert(b);
    auto *job=tegu_fullscreen_backing_job(b);
    assert(job->target_gpu_va==p.target_va&&job->heap_gpu_va==p.heap_va&&p.scratch_va==FS_APERTURE_VA);
    for(unsigned i=0;i<4;i++){uint64_t va=0;
        assert(tegu_fullscreen_backing_resolve_resource(b,i,0,lengths[i],&va)&&va==p.span_va[i]);}
    tegu_fullscreen_backing_finish(b,1);
    /* Refusals leave the plan untouched. */
    struct tegu_generic_job_plan keep;memset(&keep,0x5a,sizeof(keep));struct tegu_generic_job_plan probe=keep;
    struct tegu_generic_job_request bad=r;bad.commands[0]=21ull<<56;assert(tegu_generic_job_plan(&bad,&probe)==-2);
    bad=r;bad.commands[0]=33ull<<56;assert(tegu_generic_job_plan(&bad,&probe)==-2);
    bad=r;bad.spans[1].offset=16;assert(tegu_generic_job_plan(&bad,&probe)<0);
    bad=r;bad.flags=0;assert(tegu_generic_job_plan(&bad,&probe)<0);
    bad=r;bad.exec_mask|=1ull<<1;assert(tegu_generic_job_plan(&bad,&probe)<0);
    bad=r;bad.target_length=11225087;assert(tegu_generic_job_plan(&bad,&probe)<0);
    bad=r;bad.command_count=TEGU_GENERIC_JOB_MAX_COMMANDS+1;assert(tegu_generic_job_plan(&bad,&probe)<0);
    assert(!memcmp(&keep,&probe,sizeof(keep)));
    s->release();t->release();for(auto *x:res)x->release();clean_counts();
}

static void test_heap2m_owned_pages_mapping_and_failure_rollback() {
    /* Allocation failure at the first/middle/last child must drop every staged
     * owned object; no map has been exposed yet. */
    for (long child_ordinal : {1L,64L,128L}) {
        reset_faults();auto *s=buf(0x70000);auto *t=buf(4096);TeguGPUBuffer *r[6];
        struct tegu_fs_resource_span spans[6];
        for(unsigned i=0;i<6;i++){r[i]=buf(4096);spans[i]={r[i],0,4096};}
        auto l=heap_layout();volatile uint64_t l2[512]={};
        buffer_fail_at=buffer_calls+1+child_ordinal; /* one PT backing, then children */
        assert(!tegu_fullscreen_backing_create_v7_heap2m(s,t,0,&l,spans,6,
            FS_V7_SCRATCH_EXEC_ALLOWED,l2,TEGU_FS_QUEUE_L2_PA,&reserved));
        buffer_fail_at=0;s->release();t->release();for(auto *x:r)x->release();clean_counts();
    }
    /* The ordinary buffer quota also covers the private page tables and every
     * heap child. Refusal at the final byte must release all partial state. */
    {
        reset_faults();auto *s=buf(0x70000);auto *t=buf(4096);TeguGPUBuffer *r[6];
        struct tegu_fs_resource_span spans[6];
        for(unsigned i=0;i<6;i++){r[i]=buf(4096);spans[i]={r[i],0,4096};}
        auto l=heap_layout();volatile uint64_t l2[512]={};
        quota_limit=buffer_charged+16384u+0x200000u-1u;
        assert(!tegu_fullscreen_backing_create_v7_heap2m(s,t,0,&l,spans,6,
            FS_V7_SCRATCH_EXEC_ALLOWED,l2,TEGU_FS_QUEUE_L2_PA,&reserved));
        quota_limit=UINT64_MAX;s->release();t->release();for(auto *x:r)x->release();clean_counts();
    }
    reset_faults();auto *s=buf(0x70000);auto *t=buf(4096);TeguGPUBuffer *r[6];
    struct tegu_fs_resource_span spans[6];
    for(unsigned i=0;i<6;i++){r[i]=buf(4096);spans[i]={r[i],0,4096};}
    auto l=heap_layout(16,16,64,6);volatile uint64_t l2[512]={};
    auto *b=tegu_fullscreen_backing_create_v7_heap2m(s,t,0,&l,spans,6,
        FS_V7_SCRATCH_EXEC_ALLOWED,l2,TEGU_FS_QUEUE_L2_PA,&reserved);assert(b);
    auto *job=tegu_fullscreen_backing_job(b);assert(job&&job->page_count==631&&
        job->target_gpu_va==FS_APERTURE_VA+112u*4096u&&
        job->heap_gpu_va==FS_APERTURE_VA+119u*4096u&&job->heap_bytes==0x200000u);
    assert(tegu_fullscreen_backing_heap_buffer_count(b)==128);
    const struct fs_request *q=job->request;assert(q->heap_count==512&&q->heap);
    for(unsigned i=0;i<128;i++){
        auto *child=tegu_fullscreen_backing_heap_buffer(b,i);assert(child);
        auto *bytes=(const uint8_t *)child->getBytesNoCopy();assert(bytes&&child->logicalLength()==16384);
        for(unsigned j=0;j<16384;j++)assert(bytes[j]==0);
        uint64_t first=0;assert(child->gpuPage(0,&first));
        for(unsigned j=0;j<4;j++){
            uint64_t pa=0;assert(child->gpuPage((uint64_t)j*4096,&pa));
            assert(pa==first+(uint64_t)j*4096&&q->heap[i*4+j].pa==pa&&q->heap[i*4+j].owner==job->owner);
        }
    }
    /* The fixed qualified ordering leaves VA pages 0..118 unchanged and
     * appends the heap at page119, across L3[0] 393 entries + L3[1] 119. */
    assert(q->l3_count==2&&q->l3[0].page.pa!=q->l3[1].page.pa);
    struct fs_ops ops{};CallbackContext cb{};ops.ctx=&cb;ops.lock_begin=test_lock;
    ops.write_entry=test_write;ops.sync_table=test_sync;ops.flush_unlock=test_flush;
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_KEEP_LIVE);
    auto *mapping=tegu_fullscreen_backing_mapping(b);assert(mapping->leaf_count==631);
    assert(q->l3[0].entries[119]==(q->heap[0].pa|FS_PTE_RW_NX));
    assert(q->l3[1].entries[118]==(q->heap[511].pa|FS_PTE_RW_NX));
    tegu_fullscreen_backing_finish(b,1);for(auto *x:r)x->release();s->release();t->release();clean_counts();
}

static void test_quarantine_roots_all_pages_and_blocks_new_maps() {
    reset_faults(); auto *s=buf(0x40000); auto *t=buf(4096); auto l=layout_for();
    volatile uint64_t l2[512]={}; struct fs_ops ops{}; CallbackContext cb{};
    ops.ctx=&cb; ops.lock_begin=test_lock; ops.write_entry=test_write; ops.sync_table=test_sync; ops.flush_unlock=test_flush;
    auto *b=create(s,t,&l,l2); assert(b);
    s->release(); t->release();
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_KEEP_LIVE);
    tegu_fullscreen_backing_finish(b,0);
    assert(tegu_fullscreen_backing_map(b,&ops)==FS_QUARANTINE);
    tegu_fullscreen_backing_finish(b,0); /* repeated shutdown cannot free the root */
    assert(buffer_live==3); /* scratch, target, private PT remain rooted */
    auto *s2=buf(0x40000); auto *t2=buf(4096);
    assert(!create(s2,t2,&l,l2));
    s2->release(); t2->release();
    assert(buffer_live==3); /* no ordinary shutdown/free path releases quarantine */
}

int main() {
    initial_heap_live=heap_live; initial_buffers=buffer_live;
    test_each_allocator_failure_stage();
    test_logical_bounds_and_staged_cleanup();
    test_mapper_refusals_and_owned_references();
    test_gpu_page_resolution_failures_release_staging();
    test_reserved_and_page_table_aliases();
    test_resource_aliases_keep_per_binding_contiguous_va();
    test_forbidden_aliases_and_owner_range_refusals();
    test_1080p_partial_page_and_small_resource_spans();
    test_heap2m_owned_pages_mapping_and_failure_rollback();
    test_generic_job_plan_matches_backing_vas();
    test_quarantine_roots_all_pages_and_blocks_new_maps();
    puts("allocator adapter host doubles: all scenarios passed");
    return 0;
}
