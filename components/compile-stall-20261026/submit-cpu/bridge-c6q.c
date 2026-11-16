#include "nord2_metal_bridge.h"
#include "tegu/pipeline_format.h"
#include "nord2_gpu_transport.h"
#include "nord2_gpu_jm.h"
#include "nord2_metal_render.h"
#include "nord2_preload.h"
/* LOAD/clear preloads: blendable targets sample as float; raw targets copy
 * their bits through the uint twin view/format (n2r_rt_raw_twin): the
 * preload request's target and binding formats become the twin. */
static unsigned preload_format(unsigned f){unsigned t=n2r_rt_raw_twin(f);return t?t:(f?f:80);}
static const struct tegu_generic_draw_shader *preload_shader(unsigned f){
 return n2r_rt_raw_twin(f)?&n2_preload_uint_shader:&n2_preload_shader;}
static const struct tegu_generic_draw_shader *frame_preload_shader(unsigned f){
 return n2r_rt_raw_twin(f)?&n2_frame_preload_uint_shader:&n2_frame_preload_shader;}
#include "nord2_texture_resources.h"
#ifdef __APPLE__
extern void *calloc(size_t,size_t);
extern void free(void *);
extern char *getenv(const char *);
extern int snprintf(char *,size_t,const char *,...);
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
#else
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#endif
_Static_assert(TEGU_PIPELINE2_MAX_BUFFERS <= TEGU_PIPELINE2_MAX_RESOURCES,
               "extended buffer bindings must fit TDG2 descriptor capacity");
_Static_assert(TEGU_PIPELINE2_BUFFER_TABLE_RECORD == 16u,
               "extended buffer table record ABI");
_Static_assert(TEGU_PIPELINE_DISPATCH2_DESC_OFFSET +
               TEGU_PIPELINE2_MAX_BUFFERS * TEGU_PIPELINE_DISPATCH2_DESC_BYTES <=
               TEGU_PIPELINE_DISPATCH2_PUSH_OFFSET,
               "31 buffer TDG2 descriptors must fit the existing wire");
#define BAD ((int)0xe00002c2u)
#define UNSUPPORTED ((int)0xe00002c7u)
#define NOTREADY ((int)0xe00002d8u)
#define FAILED ((int)0xe00002bcu)
#define KBUSY ((int)0xe00002d5u) /* kIOReturnBusy */
#ifndef NORD2_GPU_SEL_WAIT_SEQ
#define NORD2_GPU_SEL_WAIT_SEQ 12u /* kernel 0090: {seq,timeout_ms} -> {state,count,first_failed} */
#endif
/* Kernel P9 (0105) allows 1024 handles per context, P8 and earlier 256
 * (N2C_MAX_BOS). The table holds 1024 (56 B each: 56 KiB per bridge); the
 * usable cap starts at 1024 and drops to P8's 256 on the first kernel
 * allocation refusal at exactly 256 live handles. Scans stop at the high
 * water mark, so a 256-handle client pays the old scan cost. */
#define MAX_BO 1024
#define MAX_BO_P8 256
#define MAX_PIPE 64
#define PIPE_TAG (1ull<<63)
struct bo {uint32_t handle;uint64_t va,size,cpu,mapped;unsigned internal,external_cpu,gpu_only;};
struct pipeline {uint64_t id;struct tegu_pipeline_info info;struct tegu_pipeline_info2 info2;unsigned version;uint32_t code;};
struct bridge {
    struct n2mb_ops ops;
    struct bo bos[MAX_BO];
    unsigned bo_hw,bo_live,bo_cap; /* scan bound, live handles, usable handles (MAX_BO or MAX_BO_P8) */
    struct pipeline pipes[MAX_PIPE];
    uint64_t next_pipeline;
    unsigned broken,trace_draws,trace_geometry,trace_batches;
    /* Keep the heap mapped until context close, following Mesa's lifetime.
     * Hardware traces suggest state survives between job chains; they do
     * not establish whether that state is keyed by context or descriptor VA. */
    struct bo *heap;
    struct {uint32_t heap_bytes,chunk_enum1,mask,no_secondary;} tiler;
    unsigned matrix; /* 1: NORD2_METAL_TILER_MATRIX pending, 2: done */
    /* Kernel 0096 ALLOC flag bit1 (GPU-only: never CPU-mapped, never cache
     * maintained). 0 unknown, 1 accepted, 2 refused by an older kernel. */
    unsigned gpu_only_state;
    /* Source line of the last draw refusal (negative: entry validation).
     * Diagnostic only; read by n2mb_last_failure. */
    int bad_site;
    /* Shader code BOs reused across draws (content-compared, LRU). Every
     * draw waits for its job before returning, so an evicted entry has no
     * GPU user. */
    struct bo *code[16];size_t code_len[16];uint64_t code_used[16],code_clock,code_total;
    /* Entries used after this clock value belong to the job being built and
     * are never reclaimed (UINT64_MAX outside a draw: all reclaimable). */
    uint64_t code_protect;
    unsigned batch_last; /* draws the last successful n2mb_draw_batch submitted */
    unsigned batch_cap; /* draws per batch, 2..N2MB_BATCH_CAPACITY (n2mb_batch_capacity) */
    unsigned preframe; /* 1: LOAD preload as pre-frame DCD (n2mb_preframe) */
    unsigned extent; /* 1: fragment job bounded to the draws' extent (n2mb_extent) */
    uint64_t stat_tiles[3]; /* n2mb_tile_stats: graphics jobs, target tiles, fragment-job tiles */
    /* Asynchronous graphics submission (n2mb_async): 0 off, 1 on, 2 requested
     * by NORD2_METAL_ASYNC=1 and probed at the first draw. `pending` is the
     * newest submitted, unreaped job id; async_failed the first failing id
     * a WAIT_SEQ reported. */
    unsigned async;uint64_t pending,async_failed;
    /* Command-BO pool (cmdpool_get/put): mapped internal command buffers kept
     * across jobs. An entry is reused only when its last job id <= reaped
     * (the newest id known complete: sync WAIT, WAIT_SEQ). */
    struct {struct bo *o;uint64_t job,used;unsigned busy;} cmdpool[48];unsigned cmdpool_n;uint64_t cmdpool_bytes,cmdpool_clock;
    uint64_t reaped,cmdpool_hits,cmdpool_misses;
    /* Clear colours (n2mb_clear): 64-byte slots of one mapped slab, a slot
     * reused once its clear job is reaped (job ids as in cmdpool). */
    struct bo *clear_slab;uint64_t clear_slot_job[256];unsigned clear_next;
    /* Chained submission (n2mb_chain_begin/end, NORD2_METAL_CHAIN). `req`
     * collects every frame's handles; ttail/ftail are the CPU views of the
     * last tiler and fragment job headers, tn/fn their indices; blocks and
     * slots get the kernel id when the chain is submitted. */
    struct {
        unsigned mode,open,max,frames,probe;int slice_now;
        struct bo *heap;uint64_t slice;uint32_t used;
        struct nord2_gpu_submit_request req;
        uint8_t *ttail,*ftail;unsigned tn,fn;
        struct bo *blocks[N2MB_CHAIN_MAX_FRAMES];unsigned nblocks;
        int slots[2*N2MB_CHAIN_MAX_FRAMES];unsigned nslots;
        uint64_t code_clock,last_job,ends,st[N2MB_CHAIN_STATS];
    } chain;
    /* The handles of the newest submitted graphics jobs (n2mb_wait_handles):
     * a CPU access waits only for the jobs that list one of its buffers. */
    struct {uint64_t job;uint32_t n;uint32_t h[32];} recent[64];unsigned recent_next;
    /* Kernel 0112 surface reservation: 0 unknown, 1 available, 2 absent. */
    unsigned resv;uint64_t resv_stats[4];
};
enum {CHAIN_FLUSH_DEP=3,CHAIN_FLUSH_HANDLES=4,CHAIN_FLUSH_FULL=5,CHAIN_FLUSH_OTHER=6,CHAIN_FLUSH_END=7};
static int chain_flush(struct bridge *b,unsigned reason);
static int chain_holds(const struct bridge *b,uint64_t handle){
    for(unsigned i=0;b->chain.frames&&i<b->chain.req.nr_handles;i++)if(b->chain.req.handles[i]==handle)return 1;
    return 0;
}
#ifndef NORD2_EXTENT_DEFAULT
#define NORD2_EXTENT_DEFAULT 1
#endif
#ifndef NORD2_PREFRAME_DEFAULT
#define NORD2_PREFRAME_DEFAULT 0
#endif
#define HEAP_DEFAULT (2u<<20)
#define HEAP_MATRIX (16u<<20)
static struct bo *lookup(struct bridge *b,uint64_t h) {
    if(!h||h>UINT32_MAX)return NULL;
    for(unsigned i=0;i<b->bo_hw;i++)if(b->bos[i].handle==h)return &b->bos[i];
    return NULL;
}
/* A free table slot within the usable cap (NULL when the cap is reached). */
static struct bo *bo_slot(struct bridge *b) {
    if(b->bo_live>=(b->bo_cap?b->bo_cap:MAX_BO))return NULL;
    for(unsigned i=0;i<MAX_BO;i++)if(!b->bos[i].handle)return &b->bos[i];
    return NULL;
}
static void bo_taken(struct bridge *b,struct bo *o) {
    unsigned i=(unsigned)(o-b->bos);b->bo_live++;if(i+1>b->bo_hw)b->bo_hw=i+1;
}
static int scalar(struct bridge *b,unsigned sel,const uint64_t *in,unsigned ni,uint64_t *out,unsigned *no) {
    return b->ops.call(b->ops.opaque,sel,in,ni,NULL,0,out,no,NULL,NULL);
}
static int drop(struct bridge *b,struct bo *o) {
    if(!o||!o->handle)return BAD;
    /* A buffer of a not yet submitted chain: submit the chain first. */
    if(chain_holds(b,o->handle)){int e=chain_flush(b,CHAIN_FLUSH_OTHER);if(e)return e;}
    if(o->cpu&&!o->external_cpu) {
        int e=b->ops.unmap(b->ops.opaque,o->handle,o->cpu);
        if(e)return e;
        o->cpu=o->mapped=0;
    }
    uint64_t h=o->handle;unsigned n=0;
    int e=scalar(b,NORD2_GPU_SEL_RELEASE,&h,1,NULL,&n);
    /* Kernel: releasing while this context has unreaped jobs consumes the
     * handle and defers the free until the context is idle (kIOReturnBusy).
     * Only the async path leaves jobs unreaped. */
    if(e==KBUSY&&b->async==1&&b->pending)e=0;
    if(!e){*o=(struct bo){0};if(b->bo_live)b->bo_live--;
        while(b->bo_hw&&!b->bos[b->bo_hw-1].handle)b->bo_hw--;}
    return e;
}
/* Reap every job up to the newest submitted one (kernel 0090 WAIT_SEQ: jobs
 * of one context run in submission order). A failure or a wait error breaks
 * the bridge exactly as a failed synchronous WAIT does. */
static int wait_pending(struct bridge *b){
    if(b->chain.frames){int e=chain_flush(b,CHAIN_FLUSH_OTHER);if(e)return e;}
    if(!b->pending)return 0;
    for(unsigned tries=0;;tries++){
        uint64_t in[2]={b->pending,5000},out[3]={0};unsigned n=3;
        int e=scalar(b,NORD2_GPU_SEL_WAIT_SEQ,in,2,out,&n);
        if(!e&&n==3&&out[0]==2&&tries<2)continue; /* timeout: nothing reaped */
        if(e||n!=3||out[0]!=4){b->broken=1;if(n==3)b->async_failed=out[2];return e?e:FAILED;}
        if(b->pending>b->reaped)b->reaped=b->pending;
        b->pending=0;return 0;
    }
}
/* WAIT_SEQ(0,0) only answers on a kernel that has it (nothing to reap).
 * Kernel 0108 (P11+) answers it {4,0,0}; 0090-0107 refuse seq 0 with
 * kIOReturnNotFound and a kernel without the selector fails its argument
 * check: both stay synchronous (one WAIT per job), the path every pre-0108
 * device run took. */
static void async_probe(struct bridge *b){
    if(b->async!=2)return;
    uint64_t in[2]={0,0},out[3]={0};unsigned n=3;
    int e=scalar(b,NORD2_GPU_SEL_WAIT_SEQ,in,2,out,&n);
    b->async=!e&&n==3&&out[0]==4?1:0;
}
/* Submit one JM request. Synchronous (historical): WAIT for it. Async: record
 * it as pending and return; `sync` forces completion (diagnostics that read
 * GPU-written memory afterwards). The kernel's in-flight window answers
 * kIOReturnBusy: reap and resubmit once. */
static int jm_submit(struct bridge *b,struct nord2_gpu_submit_request *req,unsigned sync,uint64_t *job_out){
    uint64_t job=0;unsigned n=1;
    int e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_SUBMIT_JM,NULL,0,req,sizeof(*req),&job,&n,NULL,NULL);
    if(e==KBUSY&&b->async==1&&b->pending){
        if((e=wait_pending(b)))return e;
        job=0;n=1;e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_SUBMIT_JM,NULL,0,req,sizeof(*req),&job,&n,NULL,NULL);
    }
    if(e)return e;
    if(n!=1||!job){b->broken=1;return FAILED;}
    *job_out=job;
    {unsigned r=b->recent_next++%64u;b->recent[r].job=job;b->recent[r].n=req->nr_handles<=32?req->nr_handles:32;
     memcpy(b->recent[r].h,req->handles,b->recent[r].n*sizeof(uint32_t));}
    if(b->async==1){b->pending=job;return sync?wait_pending(b):0;}
    uint64_t wait[2]={job,5000},state=0;n=1;e=scalar(b,NORD2_GPU_SEL_WAIT,wait,2,&state,&n);
    if(e||n!=1||state!=4){b->broken=1;return e?e:FAILED;}
    if(job>b->reaped)b->reaped=job; /* in order: every earlier job is complete too */
    return 0;
}
/* The shader-code cache is the only bridge-retained memory that can be given
 * back on demand. The kernel charges every context allocation (and imported
 * IOSurface) against one budget and refuses past it; reclaim before failing
 * an allocation the client needs (c6ic: rc=-15 at ~117 jobs). */
static unsigned code_reclaim(struct bridge *b){
    unsigned n=0;
    for(unsigned i=0;i<16;i++)if(b->code[i]&&b->code_used[i]<=b->code_protect){
        b->code_total-=b->code_len[i];(void)drop(b,b->code[i]);b->code[i]=NULL;b->code_len[i]=0;n++;}
    return n;
}
static unsigned cmdpool_reclaim(struct bridge *b);
#define ALLOC_EXEC 1u
#define ALLOC_GPU_ONLY 2u /* kernel 0096: never CPU-mapped (map refused), never cache-maintained */
static int alloc(struct bridge *b,uint64_t size,unsigned exec,unsigned internal,struct bo **result) {
    struct bo *o=bo_slot(b);
    /* c6n: backboardd held 256 handles (215 small client buffers + pooled
     * command blocks + cached code); a new code BO then failed NOTREADY and
     * dropped the command. The table is the kernel's per-context handle cap
     * (N2C_MAX_BOS 256): free idle bridge-owned BOs first (reap, pooled
     * command blocks, then cached code), as for a budget refusal. */
    if(!o&&b->pending)(void)wait_pending(b);
    if(!o&&cmdpool_reclaim(b))o=bo_slot(b);
    if(!o&&code_reclaim(b))o=bo_slot(b);
    if(!o)return NOTREADY;
    /* `exec` carries the kernel ALLOC flags. A kernel before 0096 refuses
     * bit1 with kIOReturnBadArgument (as it does a budget refusal): retry
     * without it and remember only when the plain retry succeeds. */
    if(b->gpu_only_state==2)exec&=~ALLOC_GPU_ONLY;
    uint64_t in[2]={size,exec},out[3]={0};unsigned n=3;
    int e=scalar(b,NORD2_GPU_SEL_ALLOC,in,2,out,&n);
    if(e==BAD&&(exec&ALLOC_GPU_ONLY)&&b->gpu_only_state!=1){
        in[1]=exec&~ALLOC_GPU_ONLY;n=3;e=scalar(b,NORD2_GPU_SEL_ALLOC,in,2,out,&n);
        if(!e)b->gpu_only_state=2;
    } else if(!e&&(exec&ALLOC_GPU_ONLY))b->gpu_only_state=1;
    /* Async: buffers released while jobs are unreaped still count against the
     * budget until the context is idle; reap before reclaiming code. */
    if(e&&b->pending&&!wait_pending(b)){n=3;e=scalar(b,NORD2_GPU_SEL_ALLOC,in,2,out,&n);}
    if(e&&cmdpool_reclaim(b)){n=3;e=scalar(b,NORD2_GPU_SEL_ALLOC,in,2,out,&n);}
    if(e&&code_reclaim(b)){n=3;e=scalar(b,NORD2_GPU_SEL_ALLOC,in,2,out,&n);}
    /* A P8 kernel refuses the 257th handle: remember its cap, so the table
     * fills only to 256 and reclaims before asking (P9 1024 keeps going). */
    if(e&&b->bo_live==MAX_BO_P8&&b->bo_cap!=MAX_BO_P8)b->bo_cap=MAX_BO_P8;
    if(e)return e;
    if(n!=3||!out[0]||out[0]>UINT32_MAX||lookup(b,out[0])||out[2]<size||
       !out[1]||(out[1]&16383)||out[1]>=(1ull<<40)||out[2]>(1ull<<40)-out[1]) {
        /* Unexpected ABI output: do not attempt a release using an untrusted
         * handle that might belong to another allocation. Close owns recovery. */
        b->broken=1;return FAILED;
    }
    *o=(struct bo){.handle=(uint32_t)out[0],.va=out[1],.size=out[2],.internal=internal,.gpu_only=(in[1]&ALLOC_GPU_ONLY)!=0};
    bo_taken(b,o);*result=o;return 0;
}
static int map_bo(struct bridge *b,struct bo *o) {
    if(o->cpu||o->gpu_only)return BAD;
    uint64_t address=0,size=0;
    int e=b->ops.map(b->ops.opaque,o->handle,&address,&size);if(e)return e;
    if(!address||size<o->size){if(address)(void)b->ops.unmap(b->ops.opaque,o->handle,address);return FAILED;}
    o->cpu=address;o->mapped=size;return 0;
}
/* ---- Command-BO pool --------------------------------------------------
 * Every kernel job used to allocate, map, unmap and release a fresh command
 * BO (4 kernel transitions plus zero-fill faults of fresh pages). The pool
 * keeps up to 48 mapped internal BOs (at most 4 MiB) and hands one out again once the job
 * that last used it is complete (id <= reaped). Contents need no clearing:
 * n2r_build_impl zeroes every block it builds and unbuilt blocks are never
 * referenced by a job. Under budget pressure alloc() drops the idle ones. */
#define CMDPOOL_MAX 48u
#define CMDPOOL_BYTES (4ull<<20)
static int cmdpool_get(struct bridge *b,uint64_t size,struct bo **result) {
    for(unsigned i=0;i<b->cmdpool_n;i++) {
        struct bo *o=b->cmdpool[i].o;
        if(b->cmdpool[i].busy||b->cmdpool[i].job>b->reaped||o->size<size||o->size>2*size+N2R_BLOCK)continue;
        b->cmdpool[i].busy=1;b->cmdpool[i].used=++b->cmdpool_clock;b->cmdpool_hits++;*result=o;return 0;
    }
    struct bo *o=NULL;int e=alloc(b,size,0,1,&o);if(e)return e;
    e=map_bo(b,o);if(e){(void)drop(b,o);return e;}
    b->cmdpool_misses++;
    /* Full: replace the least recently used idle entry (e.g. another size
     * class a previous workload left behind); in-flight entries stay. */
    while(b->cmdpool_n&&(b->cmdpool_n>=CMDPOOL_MAX||b->cmdpool_bytes+o->size>CMDPOOL_BYTES)) {
        unsigned victim=UINT32_MAX;
        for(unsigned i=0;i<b->cmdpool_n;i++)
            if(!b->cmdpool[i].busy&&b->cmdpool[i].job<=b->reaped&&(victim==UINT32_MAX||b->cmdpool[i].used<b->cmdpool[victim].used))victim=i;
        if(victim==UINT32_MAX)break;
        uint64_t vsize=b->cmdpool[victim].o->size;
        if(drop(b,b->cmdpool[victim].o))break;
        b->cmdpool_bytes-=vsize;b->cmdpool[victim]=b->cmdpool[--b->cmdpool_n];
    }
    if(b->cmdpool_n<CMDPOOL_MAX&&b->cmdpool_bytes+o->size<=CMDPOOL_BYTES){
        b->cmdpool[b->cmdpool_n].o=o;b->cmdpool[b->cmdpool_n].job=0;b->cmdpool[b->cmdpool_n].busy=1;
        b->cmdpool[b->cmdpool_n].used=++b->cmdpool_clock;b->cmdpool_n++;b->cmdpool_bytes+=o->size;}
    *result=o;return 0;
}
/* Return a command BO after its submission attempt: `job` is the id it was
 * submitted under (0: never submitted). Not pooled (pool full): released. */
static int cmdpool_put(struct bridge *b,struct bo *o,uint64_t job) {
    for(unsigned i=0;i<b->cmdpool_n;i++)if(b->cmdpool[i].o==o) {
        b->cmdpool[i].busy=0;b->cmdpool[i].job=job;
        if(b->broken&&job){ /* its job may never be reaped: let the kernel own it */
            b->cmdpool_bytes-=o->size;int e=drop(b,o);b->cmdpool[i]=b->cmdpool[--b->cmdpool_n];return e;
        }
        return 0;
    }
    return drop(b,o);
}
/* A free clear-colour slot (its last job reaped), or -1 (caller allocates). */
static int clear_slot_get(struct bridge *b) {
    if(!b->clear_slab) {
        struct bo *o=NULL;if(alloc(b,16384,0,0,&o))return -1;
        if(map_bo(b,o)){(void)drop(b,o);return -1;}
        b->clear_slab=o;memset(b->clear_slot_job,0,sizeof(b->clear_slot_job));
    }
    for(unsigned k=0;k<256;k++) {
        unsigned slot=(b->clear_next+k)&255u;
        if(b->clear_slot_job[slot]<=b->reaped){b->clear_next=slot+1;b->clear_slot_job[slot]=UINT64_MAX;return (int)slot;}
    }
    return -1;
}
/* Release idle entries (their last job complete). Returns how many. */
static unsigned cmdpool_reclaim(struct bridge *b) {
    unsigned n=0;
    if(b->clear_slab) {
        unsigned idle=1;for(unsigned k=0;k<256;k++)if(b->clear_slot_job[k]>b->reaped){idle=0;break;}
        if(idle&&!drop(b,b->clear_slab)){b->clear_slab=NULL;n++;}
    }
    for(unsigned i=0;i<b->cmdpool_n;) {
        uint64_t size=b->cmdpool[i].o->size;
        if(!b->cmdpool[i].busy&&b->cmdpool[i].job<=b->reaped&&!drop(b,b->cmdpool[i].o)) {
            b->cmdpool_bytes-=size;b->cmdpool[i]=b->cmdpool[--b->cmdpool_n];n++;continue;
        }
        i++;
    }
    return n;
}
/* The persistent tiler heap. Only the geometry diagnostics read it with the
 * CPU (NORD2_METAL_TRACE, first draws); otherwise it is GPU-only, so the
 * kernel never cleans or invalidates its 2-16 MiB per job (0096: the probe's
 * mapped 16 MiB matrix heap cost ~1 ms each way on every job). */
static int heap_alloc(struct bridge *b){
    uint64_t bytes=b->tiler.heap_bytes>HEAP_DEFAULT?b->tiler.heap_bytes:HEAP_DEFAULT;
    return alloc(b,bytes,b->trace_geometry?0u:ALLOC_GPU_ONLY,1,&b->heap);
}
/* ---- Chained submission (n2mb_chain_begin/end) ---------------------------
 * One kernel job for a run of frames: the kernel starts JS1 at heads[0] and,
 * once that chain completed, JS0 at heads[1]; the hardware follows each job
 * header's Next. Every frame keeps its own blocks and tiler context; its tiler
 * jobs are renumbered after the previous frame's (and depend on its last), its
 * fragment job gets the next JS0 index, a dependency on the previous fragment
 * job and the barrier, so frame k+1's fragments start after frame k's
 * completed. All tiler jobs run before any fragment job: each frame has its
 * own tiler-heap slice (a full HEAP_DEFAULT heap, as an unchained job), and a
 * frame whose vertex stage may read what an earlier frame of the chain writes
 * cannot join (the tiler would read it before that fragment job ran). */
static void chain_put64(uint8_t *p,uint64_t v){memcpy(p,&v,8);}
static uint64_t chain_get64(const uint8_t *p){uint64_t v;memcpy(&v,p,8);return v;}
static int chain_written(const struct bridge *b,uint64_t handle){
    const struct nord2_gpu_submit_request *r=&b->chain.req;
    for(unsigned i=0;b->chain.frames&&i<r->nr_handles;i++)if(r->handles[i]==handle)return (r->write_mask>>i)&1u;
    return 0;
}
static void chain_log(struct bridge *b){
    const uint64_t *st=b->chain.st;char line[240];
    int len=snprintf(line,sizeof(line),"TEGU_PERF_CHAIN mode=%u max=%u chains=%llu frames=%llu dep_links=%llu flush_dep=%llu "
     "flush_handles=%llu flush_full=%llu flush_other=%llu flush_end=%llu most=%llu probe=%llu\n",b->chain.mode,b->chain.max,
     (unsigned long long)st[0],(unsigned long long)st[1],(unsigned long long)st[2],(unsigned long long)st[3],(unsigned long long)st[4],
     (unsigned long long)st[5],(unsigned long long)st[6],(unsigned long long)st[7],(unsigned long long)st[8],(unsigned long long)st[9]);
    if(len>0&&(size_t)len<sizeof(line)){unsigned no=0;size_t bytes=0;
        (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
}
static int chain_flush(struct bridge *b,unsigned reason){
    if(!b->chain.frames)return 0;
    struct nord2_gpu_submit_request req=b->chain.req;unsigned frames=b->chain.frames;
    struct bo *blocks[N2MB_CHAIN_MAX_FRAMES];unsigned nblocks=b->chain.nblocks;memcpy(blocks,b->chain.blocks,sizeof(blocks));
    int slots[2*N2MB_CHAIN_MAX_FRAMES];unsigned nslots=b->chain.nslots;memcpy(slots,b->chain.slots,sizeof(slots));
    /* Reset first: a release inside the submit path must not re-enter. */
    b->chain.frames=0;b->chain.used=0;b->chain.tn=b->chain.fn=0;b->chain.ttail=b->chain.ftail=NULL;
    b->chain.nblocks=b->chain.nslots=0;memset(&b->chain.req,0,sizeof(b->chain.req));
    if(reason<N2MB_CHAIN_STATS)b->chain.st[reason]++;
    uint64_t job=0;int e=jm_submit(b,&req,0,&job);
    if(!e){b->chain.st[0]++;b->chain.st[1]+=frames;if(frames>b->chain.st[8])b->chain.st[8]=frames;}
    b->chain.last_job=e?0:job;
    /* The blocks and colour slots belong to this job; on a refused submit
     * nothing ran and they are free again (unless the bridge broke). */
    uint64_t stamp=e?(b->broken?UINT64_MAX:0):job;
    for(unsigned k=0;k<nblocks;k++)for(unsigned i=0;i<b->cmdpool_n;i++)if(b->cmdpool[i].o==blocks[k])b->cmdpool[i].job=stamp;
    for(unsigned k=0;k<nslots;k++)if(slots[k]>=0&&slots[k]<256)b->clear_slot_job[slots[k]]=stamp;
    return e;
}
/* Before building a frame: the tiler-heap VA it uses and the BO to list.
 * A frame joins only when the chain is open and the frame is plain (no
 * snapshot, thread storage, diagnostics or synchronous trace). */
static struct bo *chain_heap(struct bridge *b,unsigned eligible,uint64_t *heap_va){
    b->chain.slice_now=-1;*heap_va=b->heap->va;
    if(!eligible||!b->chain.open||!b->chain.mode||b->tiler.heap_bytes!=HEAP_DEFAULT)return b->heap;
    if(b->chain.heap&&b->chain.heap->size<(uint64_t)b->chain.max*b->chain.slice) { /* more frames per job since */
        if(chain_flush(b,CHAIN_FLUSH_OTHER))return b->heap;
        (void)drop(b,b->chain.heap);b->chain.heap=NULL;
    }
    if(!b->chain.heap) {
        b->chain.slice=HEAP_DEFAULT;
        if(alloc(b,(uint64_t)b->chain.max*b->chain.slice,ALLOC_GPU_ONLY,1,&b->chain.heap)){b->chain.heap=NULL;b->chain.mode=0;return b->heap;}
    }
    uint32_t full=(1u<<b->chain.max)-1u;
    if((b->chain.used&full)==full&&chain_flush(b,CHAIN_FLUSH_FULL))return b->heap;
    unsigned s=0;while(s<b->chain.max&&(b->chain.used>>s)&1u)s++;
    if(s>=b->chain.max)return b->heap;
    b->chain.slice_now=(int)s;*heap_va=b->chain.heap->va+(uint64_t)s*b->chain.slice;return b->chain.heap;
}
/* After building the frame into `cmd` (req.heads set): 1 = linked into the
 * chain (no kernel job yet), 0 = submit it now (the chain was submitted
 * first, keeping order), <0 = error. vertex_hazard: the frame's vertex stage
 * may read a buffer an earlier frame of the chain writes. */
static int chain_take(struct bridge *b,struct nord2_gpu_submit_request *req,struct bo *cmd,unsigned vertex_hazard){
    int s=b->chain.slice_now;b->chain.slice_now=-1;
    if(s<0){int e=chain_flush(b,CHAIN_FLUSH_OTHER);return e?e:0;}
    unsigned dep=0,extra=0;
    for(unsigned i=0;i<req->nr_handles;i++){
        /* The chain heap is written by every frame, each in its own slice. */
        if(chain_holds(b,req->handles[i])){if(chain_written(b,req->handles[i])&&req->handles[i]!=b->chain.heap->handle)dep=1;}
        else extra++;
    }
    if(b->chain.frames&&(vertex_hazard||(dep&&b->chain.mode==2))){int e=chain_flush(b,CHAIN_FLUSH_DEP);if(e)return e;dep=0;}
    else if(b->chain.frames&&b->chain.req.nr_handles+extra>32){int e=chain_flush(b,CHAIN_FLUSH_HANDLES);if(e)return e;dep=0;}
    if(!cmd||!cmd->cpu)return BAD;
    uint64_t lo=cmd->va,hi=cmd->va+cmd->size;
    if(req->heads[0]<lo||req->heads[0]+32>hi||req->heads[1]<lo||req->heads[1]+32>hi)return BAD;
    /* JS1: renumber this frame's tiler jobs after the chain's. */
    unsigned base=b->chain.tn,top=base;uint8_t *last=NULL;
    for(uint64_t h=req->heads[0],steps=0;h;steps++) {
        if(h<lo||h+32>hi||steps>N2MB_BATCH_CAPACITY+2)return BAD;
        uint8_t *jh=(uint8_t *)(uintptr_t)(cmd->cpu+(h-lo));uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
        unsigned index=(w4>>16)+base,d1=w5&0xffffu,d2=w5>>16;
        if(index>0xffffu)return BAD;
        if(d1)d1+=base;else if(!steps&&base)d1=base;
        if(d2)d2+=base;
        w4=(w4&0xffffu)|(index<<16);w5=(d1&0xffffu)|((d2&0xffffu)<<16);memcpy(jh+16,&w4,4);memcpy(jh+20,&w5,4);
        if(index>top)top=index;
        last=jh;h=chain_get64(jh+24);
    }
    if(!last)return BAD;
    /* JS0: this frame's fragment job after the chain's last one. */
    uint8_t *fj=(uint8_t *)(uintptr_t)(cmd->cpu+(req->heads[1]-lo));uint32_t w4,w5;memcpy(&w4,fj+16,4);
    unsigned fidx=b->chain.fn+1;
    w4=(w4&0xffffu)|(fidx<<16);if(b->chain.fn)w4|=1u<<8; /* barrier */
    w5=b->chain.fn;memcpy(fj+16,&w4,4);memcpy(fj+20,&w5,4);
    if(b->chain.ttail)chain_put64(b->chain.ttail+24,req->heads[0]);else b->chain.req.heads[0]=req->heads[0];
    if(b->chain.ftail)chain_put64(b->chain.ftail+24,req->heads[1]);else b->chain.req.heads[1]=req->heads[1];
    b->chain.ttail=last;b->chain.tn=top;b->chain.ftail=fj;b->chain.fn=fidx;
    if(!b->chain.frames){b->chain.req.version=req->version;b->chain.req.kind=req->kind;b->chain.code_clock=b->code_protect;}
    for(unsigned i=0;i<req->nr_handles;i++) {
        unsigned n=0;for(;n<b->chain.req.nr_handles;n++)if(b->chain.req.handles[n]==req->handles[i])break;
        if(n==b->chain.req.nr_handles){if(n==32)return BAD;b->chain.req.handles[b->chain.req.nr_handles++]=req->handles[i];}
        if((req->write_mask>>i)&1u)b->chain.req.write_mask|=1u<<n;
    }
    b->chain.used|=1u<<s;b->chain.frames++;if(dep)b->chain.st[2]++;
    if(b->chain.nblocks<N2MB_CHAIN_MAX_FRAMES)b->chain.blocks[b->chain.nblocks++]=cmd;
    return 1;
}
static unsigned chain_env(const char *name,unsigned lo,unsigned hi,unsigned fallback);
uintptr_t n2mb_open(const struct n2mb_ops *ops) {
    if(!ops||!ops->call||!ops->map||!ops->unmap||!ops->close||!ops->lock||!ops->unlock)return 0;
    struct bridge *b=calloc(1,sizeof(*b));if(!b)return 0;b->ops=*ops;b->next_pipeline=1;b->code_protect=UINT64_MAX;
    const char *trace=getenv("NORD2_METAL_TRACE");b->trace_draws=trace&&trace[0]=='1'&&!trace[1]?64:0;
    b->trace_geometry=b->trace_draws?8:0;b->trace_batches=b->trace_draws?16:0;
    b->tiler.heap_bytes=HEAP_DEFAULT;
    const char *matrix=getenv("NORD2_METAL_TILER_MATRIX");b->matrix=matrix&&matrix[0]=='1'&&!matrix[1];
    const char *cap=getenv("NORD2_METAL_BATCH_MAX");unsigned capv=0;
    for(const char *c=cap;c&&*c>='0'&&*c<='9'&&capv<1000;c++)capv=capv*10+(unsigned)(*c-'0');
    b->batch_cap=capv>=2&&capv<=N2MB_BATCH_CAPACITY?capv:N2MB_BATCH_MAX;
    const char *preframe=getenv("NORD2_METAL_PREFRAME");
    b->preframe=preframe&&(preframe[0]=='0'||preframe[0]=='1')&&!preframe[1]?(unsigned)(preframe[0]-'0'):NORD2_PREFRAME_DEFAULT;
    const char *extent=getenv("NORD2_METAL_EXTENT");
    b->extent=extent&&(extent[0]=='0'||extent[0]=='1')&&!extent[1]?(unsigned)(extent[0]-'0'):NORD2_EXTENT_DEFAULT;
    const char *async=getenv("NORD2_METAL_ASYNC");b->async=async&&async[0]=='1'&&!async[1]?2:0;
    b->chain.mode=chain_env("NORD2_METAL_CHAIN",0,2,0);b->chain.max=chain_env("NORD2_METAL_CHAIN_FRAMES",2,N2MB_CHAIN_MAX_FRAMES,4);
    b->chain.probe=chain_env("NORD2_METAL_CHAIN_PROBE",0,1,1)?1:2;b->chain.slice_now=-1;
    return (uintptr_t)b;
}
int n2mb_close(uintptr_t value) {
    struct bridge *b=(void *)value;if(!b)return BAD;
    b->ops.lock(b->ops.opaque);
    if(!b->broken)(void)wait_pending(b);
    /* Closing the user client stops/quarantines pending GPU use. A timeout
     * never makes userland free the kernel's backing pages. */
    for(unsigned i=0;i<b->bo_hw;i++)if(b->bos[i].cpu&&!b->bos[i].external_cpu)
        (void)b->ops.unmap(b->ops.opaque,b->bos[i].handle,b->bos[i].cpu);
    int result=b->ops.close(b->ops.opaque);
    b->ops.unlock(b->ops.opaque);
    if(b->ops.destroy)b->ops.destroy(b->ops.opaque);
    free(b);return result;
}
static int capabilities(struct bridge *b,uint64_t *out,unsigned *count) {
    if(!out||!count||*count<4)return BAD;
    struct nord2_gpu_status s={0};size_t size=sizeof(s);unsigned n=0;
    int e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_STATUS,NULL,0,NULL,0,NULL,&n,&s,&size);
    if(e)return e;
    if(size!=sizeof(s)||s.abi_version!=2||!s.enabled||!s.powered||!s.arch_ok)return NOTREADY;
    /* The Nord2 frontend routes generic graphics directly to n2mb_draw.
     * No donor CSF package/batch selector is exposed. */
    out[0]=1;out[1]=8|16|64|256|(1ull<<13)|(1ull<<14)|(1ull<<15)|(1ull<<18)|(1ull<<20);out[2]=s.gpu_id;out[3]=256ull<<20;*count=4;return 0;
}
int n2mb_scalar(uintptr_t value,unsigned sel,const uint64_t *in,unsigned ni,uint64_t *out,unsigned *count) {
    struct bridge *b=(void *)value;if(!b||!count||(ni&&!in))return BAD;
    int e=UNSUPPORTED;b->ops.lock(b->ops.opaque);
    if(b->broken){e=NOTREADY;goto done;}
    /* Selectors other than capabilities/allocation are ordered after an open
     * chain (releases are covered by drop). */
    if(sel>1&&b->chain.frames&&(e=chain_flush(b,CHAIN_FLUSH_OTHER)))goto done;
    switch(sel) {
    case 0:if(ni){e=BAD;break;}e=capabilities(b,out,count);break;
    case 1: {
        /* {bytes} or {bytes, flags}: flags bit1 = GPU-only (kernel 0096) for a
         * client buffer the CPU never touches (private render targets and
         * textures); n2mb_map then refuses it. Other bits are refused. */
        if((ni!=1&&ni!=2)||!out||*count<2||(ni==2&&(in[1]&~(uint64_t)ALLOC_GPU_ONLY))){e=BAD;break;}
        struct bo *o=NULL;e=alloc(b,in[0],ni==2?(unsigned)in[1]:0u,0,&o);
        if(!e){out[0]=o->handle;out[1]=o->size;*count=2;}break;
    }
    case 14:
    case 2: {
        if(ni!=1){e=BAD;break;}struct bo *o=lookup(b,in[0]);
        e=!o||o->internal?BAD:drop(b,o);if(!e)*count=0;break;
    }
    case 6: {
        if(ni!=1){e=BAD;break;}
        e=BAD;
        for(unsigned i=0;i<MAX_PIPE;i++)if(b->pipes[i].id==in[0]&&in[0]) {
            e=drop(b,lookup(b,b->pipes[i].code));
            if(!e){b->pipes[i]=(struct pipeline){0};*count=0;}break;
        }
        break;
    }
    default:break;
    }
done:b->ops.unlock(b->ops.opaque);return e;
}
int n2mb_map(uintptr_t value,unsigned handle,unsigned task,uint64_t *address,uint64_t *size,unsigned options) {
    (void)task;struct bridge *b=(void *)value;if(!b||!address||!size||options!=1)return BAD;
    /* Idempotent: the frontend maps lazily (first CPU access), so the bridge
     * may already hold the mapping (n2mb_copy, feedback snapshot); both share
     * the one kernel mapping of the BO. */
    b->ops.lock(b->ops.opaque);struct bo *o=lookup(b,handle);
    /* CPU access to a buffer of an open chain: submit the chain first. */
    if(o&&!b->broken&&chain_holds(b,o->handle))(void)chain_flush(b,CHAIN_FLUSH_OTHER);
    int e=b->broken?NOTREADY:!o||o->internal?BAD:o->cpu&&!o->external_cpu?0:map_bo(b,o);
    if(!e){*address=o->cpu;*size=o->mapped;}b->ops.unlock(b->ops.opaque);return e;
}
int n2mb_unmap(uintptr_t value,unsigned handle,unsigned task,uint64_t address) {
    (void)task;struct bridge *b=(void *)value;if(!b)return BAD;
    b->ops.lock(b->ops.opaque);struct bo *o=lookup(b,handle);int e=BAD;
    if(o&&!o->internal&&o->cpu==address&&address) {
        e=b->ops.unmap(b->ops.opaque,handle,address);if(!e)o->cpu=o->mapped=0;
    }
    b->ops.unlock(b->ops.opaque);return e;
}
static int import_pipeline(struct bridge *b,const void *bytes,size_t size,uint64_t *out,unsigned *count) {
    struct tegu_pipeline_info info;struct tegu_pipeline_info2 info2={0};unsigned version=0;
    if(!out||*count<1||tegu_pipeline_parse_any(bytes,size,&version,&info,&info2))return BAD;
    struct pipeline *p=NULL;
    for(unsigned i=0;i<MAX_PIPE;i++)if(!b->pipes[i].id){p=&b->pipes[i];break;}
    if(!p||b->next_pipeline>=PIPE_TAG)return NOTREADY;
    struct bo *o=NULL;int e=alloc(b,info.code_bytes,1,1,&o);if(e)return e;
    e=map_bo(b,o);if(e){(void)drop(b,o);return e;}
    size_t code_at=version==2?(size_t)info2.code_offset:TEGU_PIPELINE_HEADER_BYTES;
    memcpy((void *)(uintptr_t)o->cpu,(const unsigned char *)bytes+code_at,info.code_bytes);
    *p=(struct pipeline){.id=PIPE_TAG|b->next_pipeline++,.info=info,.info2=info2,.version=version,.code=o->handle};
    out[0]=p->id;*count=1;return 0;
}
static void put64(unsigned char *p,uint64_t v) {tegu_pipeline_put(p,(uint32_t)v);tegu_pipeline_put(p+4,(uint32_t)(v>>32));}
/* Little-endian helpers shared by the v2 table/wire assembly. */
static void n2c_put32(uint8_t *p,uint32_t v)
{
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static void n2c_put64(uint8_t *p,uint64_t v) {n2c_put32(p,(uint32_t)v);n2c_put32(p+4,(uint32_t)(v>>32));}
/* ---- v2 dispatch -------------------------------------------------------
 * Tables: SRT[1]=table1 textures/images, SRT[2]=table2 samplers; table1[N]
 * and table2[N] use the native SPIR-V binding N. A samplerless texel fetch
 * or query uses the compiler-required internal dummy sampler at table2[256].
 * Descriptor bytes come from the textures owner's common n2t helper; this
 * file owns only the ABI, manifest matching, BO validation and table assembly. */
#define N2C_SRT 768u
#define N2C_TABLE1 832u
#define N2C_TABLE1_CAPACITY 256u
#define N2C_TABLE2 (N2C_TABLE1 + N2C_TABLE1_CAPACITY * 32u)
#define N2C_TABLE2_CAPACITY 257u
#define N2C_PLANES (N2C_TABLE2 + N2C_TABLE2_CAPACITY * 32u)
#define N2C_PLANE_STRIDE 32u
#define N2C_PLANE_CAPACITY 32u
/* The extended buffer table is part of the per-dispatch command allocation,
 * so its lifetime is the command's lifetime and it consumes no extra transport
 * handle. Records are indexed by native set0 binding (16 bytes each). */
#define N2C_BUFFER_TABLE 40960u
#define N2C_BUFFER_TABLE_BYTES (TEGU_PIPELINE2_BUFFER_TABLE_RECORD * TEGU_PIPELINE2_MAX_BUFFERS)
#define N2C_V2_BYTES (N2C_BUFFER_TABLE + N2C_BUFFER_TABLE_BYTES)
/* Preserve the established 16-plane layout for ordinary levels==1 commands.
 * A command with mips or >16 array layers uses the helper-checked 23-plane
 * capacity; the plane-slot stride is part of that command's wire layout. */
#define N2C_BASE_PLANE_SLOTS 16u
#define N2C_MAX_PLANES_PER_DESC 23u
static void n2c_log(struct bridge *b,const char *why)
{
    static unsigned emitted;
    if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=8)return;
    char line[192];int n=snprintf(line,sizeof(line),"NORD2_METAL compute v2 refused: %s\n",why?why:"unspecified");
    if(n>0&&(size_t)n<sizeof(line)) {
        unsigned no=0;size_t bytes=0;
        (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)n,NULL,&no,NULL,&bytes);
    }
}
static uint32_t n2c_word(const unsigned char *p,size_t off) {return tegu_pipeline_word(p+off);}
static uint64_t n2c_qword(const unsigned char *p,size_t off){
    return (uint64_t)n2c_word(p,off)|((uint64_t)n2c_word(p,off+4)<<32);
}
/* The selected translated program owns coordinate normalization and the
 * integer-sampler filter rule.  The wire still carries the original Metal
 * sampler state for specialization/cache decisions, but the native Mali
 * descriptor must be the plan's resolved use, not a copy of the Metal fields. */
static void n2c_resolve_translated_sampler(struct n2t_sampler_state *s,uint32_t flags)
{
    if(!s)return;
    s->normalized=1;
    if(flags&2u) {
        s->min_filter=N2T_FILTER_NEAREST;
        s->mag_filter=N2T_FILTER_NEAREST;
        s->mip_filter=N2T_MIP_NEAREST;
        s->max_anisotropy=1;
    }
    /* Metal ClampToZero with an explicit opaque border colour is the same
     * border value the translator selects.  Encode it as ClampBorder so the
     * shared packer uses border_color_kind instead of its format-dependent
     * transparent-zero fallback.  Transparent-black stays ClampZero, and
     * integer/custom cases remain for the packer's explicit refusal. */
    if(s->border_color_kind==N2T_BORDER_OPAQUE_BLACK||s->border_color_kind==N2T_BORDER_OPAQUE_WHITE) {
        if(s->address_s==N2T_ADDRESS_CLAMP_ZERO)s->address_s=N2T_ADDRESS_CLAMP_BORDER;
        if(s->address_t==N2T_ADDRESS_CLAMP_ZERO)s->address_t=N2T_ADDRESS_CLAMP_BORDER;
        if(s->address_r==N2T_ADDRESS_CLAMP_ZERO)s->address_r=N2T_ADDRESS_CLAMP_BORDER;
    }
    s->lod_algorithm=s->max_anisotropy>1?N2T_LOD_ANISOTROPIC:N2T_LOD_ISOTROPIC;
}
static const struct tegu_pipeline_manifest *n2c_manifest(const struct tegu_pipeline_info2 *m,
 unsigned table,uint32_t entry)
{
    for(unsigned i=0;i<m->manifest_count;i++)
        if(m->manifest[i].table==table&&m->manifest[i].entry==entry)return &m->manifest[i];
    return 0;
}
static uint32_t n2c_manifest_access(const struct tegu_pipeline_info2 *m,unsigned kind,uint32_t binding)
{
    for(unsigned i=0;i<m->manifest_count;i++)
        if(m->manifest[i].kind==kind&&m->manifest[i].binding==binding)return m->manifest[i].access;
    return 0;
}
static int add_ref(struct nord2_gpu_submit_request *req,struct bo *o,unsigned write);
int n2mb_compute_srt_pointer(uint64_t address,uint64_t limit,unsigned entries,uint64_t *out)
{
    if(!out||!address||(address&63)||address>=(1ull<<40)||entries<4||entries>60||
       (entries&3u)||address>limit||(uint64_t)entries*16u>limit-address)return -1;
    *out=(address&~0x3full)|entries;
    return 0;
}

/* The TDG2 descriptor carries one view-relative level origin, one row stride,
 * one slice stride and one layer stride.  This is the explicit contiguous
 * linear-mip convention used by the active producer: level L begins after the
 * full preceding level span and uses the same row/slice/layer strides.  The
 * shared helper still validates every row bound, overlap, alignment and the
 * declared span; a producer that cannot meet this convention must refuse or
 * add explicit per-level wire facts rather than silently truncate to L0. */
static int n2c_fill_levels(struct n2t_texture_layout *l,uint64_t level_offset,
                           uint32_t row,uint32_t slice,uint32_t layer,uint64_t span)
{
    struct n2t_format_info f;
    uint64_t off=level_offset,end;
    if(!l||l->levels==0||l->levels>N2T_MAX_LEVELS)return -1;
    if(level_offset>UINT64_MAX-span)return -1;
    end=level_offset+span; /* span is from level_offset, not resource base */
    if(n2t_format_query(l->view_format?l->view_format:l->format,&f))return -1;
    for(uint32_t i=0;i<l->levels;i++) {
        uint32_t w=(uint32_t)n2t_minify(l->width,i);
        uint32_t h=(uint32_t)n2t_minify(l->height,i);
        uint32_t d=(uint32_t)n2t_minify(l->depth,i);
        uint32_t blocks=(w+f.block_width-1u)/f.block_width;
        uint32_t rows=(h+f.block_height-1u)/f.block_height;
        uint64_t row_bytes=(uint64_t)blocks*f.bytes_per_block;
        uint64_t size;
        if(!w||!h||!d||!blocks||!rows||row<row_bytes)return -1;
        size=(uint64_t)row*rows;
        if(l->kind==N2T_KIND_3D) {
            if(!slice||slice<size)return -1;
            size+=(uint64_t)(d-1)*slice;
        } else if(l->samples>1) {
            if(!slice||slice<size)return -1;
            size+=(uint64_t)(l->samples-1)*slice;
        }
        if(size>UINT32_MAX||off>end||size>end-off)return -1;
        l->level[i].offset=off;
        l->level[i].row_stride=row;
        l->level[i].slice_stride=slice;
        l->level[i].layer_stride=layer;
        l->level[i].width=w;
        l->level[i].height=h;
        l->level[i].depth=d;
        off+=size;
    }
    return 0;
}


/* Pinned-Mesa arch9 TLS/WLS allocation.  TLS size is bytes/thread from the
 * package; Mesa sizes the BO by THREAD_TLS_ALLOC/THREAD_MAX_THREADS, which
 * nord2_gpu_status does not export, so the BO is sized for a conservative
 * 2048 threads/core (4x Mesa's 512 fallback; never undersized for G77)
 * times the present core-ID range.  WLS is packed with the Mesa
 * size scale and an instance count no larger than the command grid and no
 * smaller than the hardware's concurrent workgroups; merging stays off. */
struct n2c_local {struct bo *tls,*wls;uint32_t tls_shift,wls_log,wls_scale;uint64_t tls_va,wls_va;};
static uint32_t n2c_floor_log2(uint64_t v){uint32_t n=0;while(v>1){v>>=1;n++;}return n;}
static uint32_t n2c_ceil_log2(uint64_t v){uint32_t n=0;uint64_t p=1;while(p<v){p<<=1;n++;}return n;}
static uint64_t n2c_next_pow2(uint64_t v){if(v<=1)return 1;uint64_t p=1;while(p<v&&p<=(UINT64_MAX>>1))p<<=1;return p;}
static int n2c_core_range(struct bridge *b,uint32_t *out)
{
    if(!b||!out)return BAD;
    struct nord2_gpu_status st={0};size_t size=sizeof(st);unsigned n=0;
    int e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_STATUS,NULL,0,NULL,0,NULL,&n,&st,&size);
    if(e)return e;
    uint64_t mask=((uint64_t)st.shader_present_hi<<32)|st.shader_present_lo;
    if(!mask){*out=16;return 0;} /* Mesa's worst-case for old kernels */
    uint32_t range=0;while(mask){range++;mask>>=1;}
    *out=range?range:1;return 0;
}
static int n2c_prepare_local(struct bridge *b,const struct tegu_pipeline_info2 *info,
                             const uint32_t groups[3],struct n2c_local *l)
{
    if(!b||!info||!groups||!l)return BAD;
    memset(l,0,sizeof(*l));
    if(!info->tls_size&&!info->wls_size)return 0;
    uint32_t cores=0;if(n2c_core_range(b,&cores))return BAD;
    if(info->tls_size) {
        if(info->tls_size>TEGU_PIPELINE2_MAX_TLS)return BAD;
        uint64_t per=n2c_next_pow2(((uint64_t)info->tls_size+15)&~15ull);
        uint64_t total=per*2048ull*(uint64_t)cores;
        if(total>((1ull<<40)-1)||alloc(b,total,ALLOC_GPU_ONLY,1,&l->tls))return BAD;
        l->tls_va=l->tls->va;
        l->tls_shift=n2c_ceil_log2(((uint64_t)info->tls_size+15)/16);
    }
    if(info->wls_size) {
        if(info->wls_size>TEGU_PIPELINE2_MAX_WLS)return BAD;
        uint64_t adjusted=n2c_next_pow2(info->wls_size>128?info->wls_size:128);
        uint64_t threads=(uint64_t)info->local[0]*info->local[1]*info->local[2];
        if(!threads||threads>256)return BAD;
        /* nord2_gpu_status exposes the core mask but not THREAD_FEATURES, so
         * max_tasks_per_core is not available here.  Bound concurrent WGs by
         * the arch9 maximum threads/core instead of assuming a task count:
         * this can overallocate WLS but can never undersize it. */
        uint64_t per_core=512;
        uint64_t grid=n2c_next_pow2(groups[0]);
        uint64_t g2=n2c_next_pow2(groups[1]),g3=n2c_next_pow2(groups[2]);
        if(grid>(UINT64_MAX/g2)||grid*g2>(UINT64_MAX/g3))return BAD;
        grid*=g2*g3;
        uint64_t cap=(uint64_t)cores*per_core;
        uint64_t instances=grid<cap?grid:cap;
        if(!instances||instances>((uint64_t)1<<30))return BAD;
        /* Both cap and grid are powers of two, so instances is a power of two. */
        uint64_t total=adjusted*instances*(uint64_t)cores;
        if(total>((1ull<<40)-1)||alloc(b,total,ALLOC_GPU_ONLY,1,&l->wls))return BAD;
        l->wls_va=l->wls->va;
        l->wls_log=n2c_floor_log2(instances);
        l->wls_scale=n2c_floor_log2(adjusted)+1;
        if(l->wls_va>>32!=(l->wls_va+total-1)>>32)return BAD;
    }
    return 0;
}
static void n2c_local_pack(uint8_t *out,const struct n2c_local *l)
{
    memset(out,0,32);
    n2c_put32(out,l->tls_shift&31u);
    if(l->tls_va) {
        uint64_t p=l->tls_va>>8;
        n2c_put32(out+8,(uint32_t)p);
        n2c_put32(out+12,((uint32_t)(p>>32)&0xffffu)|(1u<<28));
    } else if(!l->wls_va) {
        /* Preserve the established no-local-memory LOCAL_STORAGE bytes. */
        n2c_put32(out+12,1u<<28);
    }
    if(l->wls_va) {
        n2c_put32(out+4,(l->wls_log&31u)|((l->wls_scale&31u)<<8));
        n2c_put64(out+16,l->wls_va);
    } else {
        /* Mesa pan_emit_tls leaves wls_size_base zero.  Preserve the
         * established base-one byte only when neither local region exists. */
        n2c_put32(out+4,31u|(l->tls_va?0u:(1u<<5)));
    }
}
static void n2c_local_drop(struct bridge *b,struct n2c_local *l)
{
    if(!b||!l)return;
    if(l->tls){(void)drop(b,l->tls);l->tls=0;}
    if(l->wls){(void)drop(b,l->wls);l->wls=0;}
}
static int dispatch_v2(struct bridge *b,uint64_t pipeline,const void *wire,size_t size,uint64_t *out,unsigned *count)
{
    if(!wire||size!=TEGU_PIPELINE_DISPATCH2_BYTES||!out||*count<2)return BAD;
    struct pipeline *p=NULL;
    for(unsigned i=0;i<MAX_PIPE;i++)if(b->pipes[i].id==pipeline&&pipeline){p=&b->pipes[i];break;}
    if(!p||p->version!=2)return BAD;
    const unsigned char *w=wire;
    uint32_t magic=n2c_word(w,0),version=n2c_word(w,4),descriptors=n2c_word(w,8);
    uint32_t groups[3]={n2c_word(w,12),n2c_word(w,16),n2c_word(w,20)};
    uint32_t push_words=n2c_word(w,24);
    struct n2c_local local={0};
    if(magic!=TEGU_PIPELINE_DISPATCH2_MAGIC||version!=TEGU_PIPELINE_DISPATCH2_VERSION||
       descriptors>TEGU_PIPELINE2_MAX_RESOURCES||n2c_word(w,28)||push_words>128||
       push_words!=p->info.push_words)return BAD;
    for(unsigned i=0;i<3;i++)if(!groups[i]||groups[i]>65536)return BAD;
    struct bo *buffers[TEGU_PIPELINE2_MAX_BUFFERS]={0},*textures[256]={0};
    uint32_t buffer_flags[TEGU_PIPELINE2_MAX_BUFFERS]={0},texture_flags[256]={0};
    uint64_t buffer_off[TEGU_PIPELINE2_MAX_BUFFERS]={0},buffer_len[TEGU_PIPELINE2_MAX_BUFFERS]={0};
    uint32_t binding[32]={0},binding_kind[32]={0},max_texture=0,max_sampler=0;
    unsigned used=0,matched_manifest=0;
    uint32_t ext_seen=0;
    char why[160];
    for(unsigned i=0;i<descriptors;i++) {
        const unsigned char *d=w+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+i*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
        uint32_t bind=n2c_word(d,TEGU_PIPELINE_D2_BINDING),kind=n2c_word(d,TEGU_PIPELINE_D2_KIND);
        uint32_t flags=n2c_word(d,TEGU_PIPELINE_D2_FLAGS);
        uint32_t metal_index=n2c_word(d,TEGU_PIPELINE_D2_METAL_INDEX);
        uint32_t sampler_slot=n2c_word(d,TEGU_PIPELINE_D2_SAMPLER_INDEX);
        uint64_t token=n2c_qword(d,TEGU_PIPELINE_D2_TOKEN),offset=n2c_qword(d,TEGU_PIPELINE_D2_OFFSET);
        uint64_t span=n2c_qword(d,TEGU_PIPELINE_D2_SPAN);
        uint64_t level_offset=n2c_qword(d,TEGU_PIPELINE_D2_LEVEL_OFFSET);
        if(bind>255||kind<1||kind>6||(flags&~(TEGU_PIPELINE2_ACCESS_MASK|TEGU_PIPELINE2_DESC_SWIZZLE_VALID)))return BAD;
        if(flags&TEGU_PIPELINE2_ACCESS_ATOMIC) {
            n2c_log(b,"atomic storage transport/write tracking is not implemented");
            return BAD;
        }
        if((flags&TEGU_PIPELINE2_DESC_SWIZZLE_VALID)&&
           kind!=TEGU_PIPELINE2_KIND_TEXTURE&&kind!=TEGU_PIPELINE2_KIND_COMBINED)return BAD;
        if(kind==TEGU_PIPELINE2_KIND_SAMPLER) {
            if(metal_index>=16)return BAD;
        } else {
            if(metal_index>=31)return BAD;
            if(kind==TEGU_PIPELINE2_KIND_COMBINED&&sampler_slot>=16)return BAD;
        }
        for(unsigned j=0;j<used;j++)if(binding[j]==bind) {
            uint32_t prev=binding_kind[j];
            if(prev==TEGU_PIPELINE2_KIND_UBO||prev==TEGU_PIPELINE2_KIND_SSBO||
               kind==TEGU_PIPELINE2_KIND_UBO||kind==TEGU_PIPELINE2_KIND_SSBO){
                return BAD;
            }
            if(kind==TEGU_PIPELINE2_KIND_SAMPLER&&prev!=TEGU_PIPELINE2_KIND_SAMPLER)continue;
            if(prev==TEGU_PIPELINE2_KIND_SAMPLER&&kind!=TEGU_PIPELINE2_KIND_SAMPLER)continue;
            return BAD;
        }
        binding_kind[used]=kind;binding[used++]=bind;
        struct bo *o=token?lookup(b,token):NULL;
        if(token&&(!o||o->internal))return BAD;
        if(kind==TEGU_PIPELINE2_KIND_UBO||kind==TEGU_PIPELINE2_KIND_SSBO) {
            uint32_t access=n2c_manifest_access(&p->info2,kind,bind);
            if(!o||bind>=TEGU_PIPELINE2_MAX_BUFFERS||!access||(flags&access)!=access||!span||span>0xffffffffu||
               offset%4||offset>o->size||span>o->size-offset)return BAD;
            if(bind>=TEGU_PIPELINE2_FAU_BUFFERS)
                ext_seen|=1u<<bind;
            buffers[bind]=o;buffer_flags[bind]|=flags;buffer_off[bind]=offset;buffer_len[bind]=span;
            matched_manifest++;
        } else if(kind==TEGU_PIPELINE2_KIND_SAMPLER) {
            if(o||token||offset||span||bind>255||flags)return BAD;
            const struct tegu_pipeline_manifest *m=n2c_manifest(&p->info2,TEGU_PIPELINE2_TABLE_SAMPLER,bind);
            uint32_t sf=n2c_word(d,TEGU_PIPELINE_D2_SAMPLER_FLAGS);
            if(!m||m->kind!=TEGU_PIPELINE2_KIND_SAMPLER||m->access!=0||
               (m->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||
               (((m->flags&TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE)!=0)!=((sf&1u)!=0)))return BAD;
            max_sampler=bind>max_sampler?bind:max_sampler;
            matched_manifest++;
        } else {
            uint32_t expected=kind==TEGU_PIPELINE2_KIND_TEXTURE?TEGU_PIPELINE2_KIND_TEXTURE:
                kind==TEGU_PIPELINE2_KIND_IMAGE?TEGU_PIPELINE2_KIND_IMAGE:TEGU_PIPELINE2_KIND_TEXTURE;
            const struct tegu_pipeline_manifest *tm=n2c_manifest(&p->info2,TEGU_PIPELINE2_TABLE_TEXTURE,bind);
            if(!tm||tm->kind!=expected||(flags&tm->access)!=tm->access)return BAD;
            if(kind==TEGU_PIPELINE2_KIND_COMBINED) {
                const struct tegu_pipeline_manifest *sm=n2c_manifest(&p->info2,TEGU_PIPELINE2_TABLE_SAMPLER,bind);
                uint32_t sf=n2c_word(d,TEGU_PIPELINE_D2_SAMPLER_FLAGS);
                if(!sm||sm->kind!=TEGU_PIPELINE2_KIND_SAMPLER||sm->access!=0||
                   !(tm->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||
                   !(sm->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||
                   (((sm->flags&TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE)!=0)!=((sf&1u)!=0)))return BAD;
                max_sampler=bind>max_sampler?bind:max_sampler;
                matched_manifest++;
            } else if(tm->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)return BAD;
            if(!o||!span||span>0xffffffffu||tm->entry!=bind)return BAD;
            if(offset>o->size||level_offset>o->size-offset||span>o->size-offset-level_offset)return BAD;
            /* Shape cross-check against the manifest. */
            uint32_t type=n2c_word(d,TEGU_PIPELINE_D2_DIM);
            uint32_t dim=(type==0||type==1)?0u:(type==5||type==6)?3u:type==7?2u:type==9?4u:1u;
            uint32_t arrayed=(type==1||type==3||type==6||type==8)?1u:0u;
            uint32_t ms=(type==4||type==8)?1u:0u;
            uint32_t shape=dim|(arrayed?TEGU_PIPELINE2_SHAPE_ARRAYED:0)|
                (ms?TEGU_PIPELINE2_SHAPE_MULTISAMPLE:0);
            uint32_t want=tm->shape&(TEGU_PIPELINE2_SHAPE_DIM_MASK|TEGU_PIPELINE2_SHAPE_ARRAYED|
                TEGU_PIPELINE2_SHAPE_MULTISAMPLE|TEGU_PIPELINE2_SHAPE_DEPTH_COMPARE);
            if((want&(TEGU_PIPELINE2_SHAPE_DIM_MASK|TEGU_PIPELINE2_SHAPE_ARRAYED|
                TEGU_PIPELINE2_SHAPE_MULTISAMPLE))!=(shape&(TEGU_PIPELINE2_SHAPE_DIM_MASK|
                TEGU_PIPELINE2_SHAPE_ARRAYED|TEGU_PIPELINE2_SHAPE_MULTISAMPLE)))return BAD;
            {uint32_t wvf=n2c_word(d,TEGU_PIPELINE_D2_VIEW_FORMAT_VK);
             if(tm->kind==TEGU_PIPELINE2_KIND_IMAGE){
                if(!wvf)return BAD;
                if(tm->storage_format&&wvf!=tm->storage_format)return BAD;
                struct n2t_format_info fi;
                uint32_t vf=n2c_word(d,TEGU_PIPELINE_D2_VIEW_FORMAT);
                if(!vf)vf=n2c_word(d,TEGU_PIPELINE_D2_FORMAT);
                if(n2t_format_query(vf,&fi)!=N2T_OK)return BAD;
                uint32_t cls=(fi.flags&N2T_FMT_INTEGER)?
                    ((fi.flags&N2T_FMT_SIGNED)?TEGU_PIPELINE2_NUMERIC_SINT:TEGU_PIPELINE2_NUMERIC_UINT):
                    TEGU_PIPELINE2_NUMERIC_FLOAT;
                uint32_t want_numeric=(tm->shape&TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_MASK)>>
                    TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_SHIFT;
                if(cls!=want_numeric)return BAD;
             } else if(tm->kind==TEGU_PIPELINE2_KIND_TEXTURE||tm->kind==TEGU_PIPELINE2_KIND_COMBINED){
                /* The manifest declares the shader-visible numeric class.  Use
                 * the bound view, not the allocation format: an integer view
                 * over a float allocation (or the inverse) is a real class
                 * change and must not be accepted through the allocation. */
                struct n2t_format_info fi;
                uint32_t vf=n2c_word(d,TEGU_PIPELINE_D2_VIEW_FORMAT);
                if(!vf)vf=n2c_word(d,TEGU_PIPELINE_D2_FORMAT);
                if(n2t_format_query(vf,&fi)!=N2T_OK)return BAD;
                uint32_t cls=(fi.flags&N2T_FMT_INTEGER)?
                    ((fi.flags&N2T_FMT_SIGNED)?TEGU_PIPELINE2_NUMERIC_SINT:TEGU_PIPELINE2_NUMERIC_UINT):
                    TEGU_PIPELINE2_NUMERIC_FLOAT;
                uint32_t want_numeric=(tm->shape&TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_MASK)>>
                    TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_SHIFT;
                if(cls!=want_numeric)return BAD;
             }}
            if(textures[bind]&&textures[bind]!=o)return BAD;
            textures[bind]=o;texture_flags[bind]|=flags;
            max_texture=bind>max_texture?bind:max_texture;
            matched_manifest++;
        }
        (void)why;
    }
    {unsigned required=p->info2.manifest_count;
     if(p->info2.flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER)required--;
     if(matched_manifest!=required)return BAD;}
    if(ext_seen!=p->info2.buffer_table_mask)return BAD;
    if(((p->info2.flags&TEGU_PIPELINE2_FLAG_BUFFER_TABLE)!=0)!=(ext_seen!=0))return BAD;
    struct bo *code=lookup(b,p->code),*cmd=NULL;if(!code)return FAILED;
    int e=0;
    if(n2c_prepare_local(b,&p->info2,groups,&local)){n2c_log(b,"TLS/WLS allocation failed");n2c_local_drop(b,&local);return BAD;}
    if((p->info2.tls_size&&!local.tls)||(p->info2.wls_size&&!local.wls)) {
        n2c_log(b,"TLS/WLS allocation missing");n2c_local_drop(b,&local);return BAD;
    }
    e=alloc(b,N2C_V2_BYTES,0,1,&cmd);if(e)goto cleanup;
    e=map_bo(b,cmd);if(e)goto cleanup;
    unsigned char *mem=(void *)(uintptr_t)cmd->cpu;
    memset(mem,0,N2C_V2_BYTES);
    unsigned fau_count=(push_words+1)/2;
    if(p->info2.buffer_table_mask&&fau_count<((TEGU_PIPELINE2_BUFFER_TABLE_FAU+
       TEGU_PIPELINE2_BUFFER_TABLE_RECORD+7u)/8u))
        fau_count=(TEGU_PIPELINE2_BUFFER_TABLE_FAU+
            TEGU_PIPELINE2_BUFFER_TABLE_RECORD+7u)/8u;
    if(fau_count>128){e=BAD;goto cleanup;}
    struct nord2_gpu_compute_chain chain={.chain_pa=cmd->va,.resources=cmd->va+N2C_SRT,
        .shader=cmd->va+128,.thread_storage=cmd->va+192,.fau=cmd->va+256,
        .fau_count=(uint8_t)fau_count,.index=1};
    for(unsigned i=0;i<3;i++){chain.workgroup_size[i]=p->info.local[i];chain.workgroup_count[i]=groups[i];}
    if(!nord2_gpu_jm_build_compute(mem,128,&chain)){e=BAD;goto cleanup;}
    {uint64_t srt=0;if(n2mb_compute_srt_pointer(cmd->va+N2C_SRT,cmd->va+N2C_V2_BYTES,4,&srt)){e=BAD;goto cleanup;}
     n2c_put64(mem+96,srt);}
    nord2_desc_shader_program(mem+128,NORD2_STAGE_COMPUTE,code->va,(uint32_t)(p->info.preload>>48));
    if(p->info.registers<=32)tegu_pipeline_put(mem+128,tegu_pipeline_word(mem+128)|(2u<<30));
    n2c_local_pack(mem+192,&local);
    for(unsigned i=0;i<8;i++)if(buffers[i]) {
        uint8_t *d=mem+256+16*i;
        n2c_put64(d,buffers[i]->va+buffer_off[i]);
        n2c_put32(d+8,(uint32_t)buffer_len[i]);
        n2c_put32(d+12,0);
    }
    if(p->info2.buffer_table_mask) {
        uint8_t *table=mem+N2C_BUFFER_TABLE;
        for(unsigned i=0;i<TEGU_PIPELINE2_MAX_BUFFERS;i++)
            if(p->info2.buffer_table_mask&(1u<<i)) {
                if(!buffers[i]||buffer_len[i]>0xffffffffu){e=BAD;goto cleanup;}
                n2c_put64(table+16u*i,buffers[i]->va+buffer_off[i]);
                n2c_put32(table+16u*i+8,(uint32_t)buffer_len[i]);
                n2c_put32(table+16u*i+12,0);
            }
        n2c_put64(mem+256+TEGU_PIPELINE2_BUFFER_TABLE_FAU,cmd->va+N2C_BUFFER_TABLE);
        n2c_put32(mem+256+TEGU_PIPELINE2_BUFFER_TABLE_FAU+8,N2C_BUFFER_TABLE_BYTES);
        n2c_put32(mem+256+TEGU_PIPELINE2_BUFFER_TABLE_FAU+12,0);
    }
    memcpy(mem+384,w+TEGU_PIPELINE_DISPATCH2_PUSH_OFFSET,128);
    for(unsigned i=0;i<3;i++)n2c_put32(mem+512+4*i,groups[i]);
    uint32_t table1_bytes=(max_texture+1u)*32u;
    uint32_t table2_bytes=(max_sampler+1u)*32u;
    if(p->info2.flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER)table2_bytes=257u*32u;
    if(table1_bytes&&n2t_resource_entry(cmd->va+N2C_TABLE1,table1_bytes,mem+N2C_SRT+16)){e=BAD;goto cleanup;}
    if(table2_bytes&&n2t_resource_entry(cmd->va+N2C_TABLE2,table2_bytes,mem+N2C_SRT+32)){e=BAD;goto cleanup;}
    if(p->info2.flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER) {
        struct n2t_sampler_state d={.version=N2T_TEXTURE_RESOURCES_VERSION,
            .min_filter=N2T_FILTER_NEAREST,.mag_filter=N2T_FILTER_NEAREST,
            .mip_filter=N2T_MIP_NOT_MIPMAPPED,.address_s=N2T_ADDRESS_CLAMP_EDGE,
            .address_t=N2T_ADDRESS_CLAMP_EDGE,.address_r=N2T_ADDRESS_CLAMP_EDGE,
            .normalized=1,.compare_enabled=0,.compare_function=N2T_COMPARE_NEVER,
            .max_anisotropy=1,.reduction=N2T_REDUCTION_AVERAGE,.lod_algorithm=N2T_LOD_ISOTROPIC,
            .clamp_integer_array=1,.seamless_cube=1,.border_color_kind=N2T_BORDER_TRANSPARENT_BLACK,
            .lod_min=0.0f,.lod_max=0.0f,.lod_bias=0.0f};
        if(n2t_sampler_pack(&d,mem+N2C_TABLE2+256u*32u)!=N2T_OK){e=BAD;goto cleanup;}
    }
    unsigned plane_slots=N2C_BASE_PLANE_SLOTS;
    for(unsigned i=0;i<descriptors;i++) {
        const unsigned char *d=w+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+i*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
        uint32_t k=n2c_word(d,TEGU_PIPELINE_D2_KIND);
        if(k==TEGU_PIPELINE2_KIND_TEXTURE||k==TEGU_PIPELINE2_KIND_IMAGE||k==TEGU_PIPELINE2_KIND_COMBINED) {
            if(n2c_word(d,TEGU_PIPELINE_D2_LEVELS)>1||n2c_word(d,TEGU_PIPELINE_D2_LAYERS)>N2C_BASE_PLANE_SLOTS)
                plane_slots=N2C_MAX_PLANES_PER_DESC;
        }
    }
    for(unsigned i=0;i<descriptors;i++) {
        const unsigned char *d=w+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+i*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
        uint32_t kind=n2c_word(d,TEGU_PIPELINE_D2_KIND),bind=n2c_word(d,TEGU_PIPELINE_D2_BINDING);
        uint32_t flags=n2c_word(d,TEGU_PIPELINE_D2_FLAGS);
        if(kind!=TEGU_PIPELINE2_KIND_TEXTURE&&kind!=TEGU_PIPELINE2_KIND_IMAGE&&kind!=TEGU_PIPELINE2_KIND_COMBINED)continue;
        struct bo *o=textures[bind];if(!o){e=BAD;goto cleanup;}
        uint64_t span=n2c_qword(d,TEGU_PIPELINE_D2_SPAN);
        uint64_t base_offset=n2c_qword(d,TEGU_PIPELINE_D2_OFFSET);
        uint64_t level_offset=n2c_qword(d,TEGU_PIPELINE_D2_LEVEL_OFFSET);
        if(base_offset>o->size||level_offset>o->size-base_offset||span>o->size-base_offset-level_offset){e=BAD;goto cleanup;}
        uint64_t declared=level_offset+span;
        uint64_t backing_address=o->va+base_offset,backing_length=declared;
        struct n2t_texture_layout l={0};
        l.version=N2T_TEXTURE_RESOURCES_VERSION;
        l.kind=n2c_word(d,TEGU_PIPELINE_D2_DIM);
        l.format=n2c_word(d,TEGU_PIPELINE_D2_FORMAT);
        l.view_format=n2c_word(d,TEGU_PIPELINE_D2_VIEW_FORMAT);
        l.width=n2c_word(d,TEGU_PIPELINE_D2_WIDTH);l.height=n2c_word(d,TEGU_PIPELINE_D2_HEIGHT);
        l.depth=n2c_word(d,TEGU_PIPELINE_D2_DEPTH);l.layers=n2c_word(d,TEGU_PIPELINE_D2_LAYERS);
        l.levels=n2c_word(d,TEGU_PIPELINE_D2_LEVELS);l.samples=n2c_word(d,TEGU_PIPELINE_D2_SAMPLES);
        l.first_level=n2c_word(d,TEGU_PIPELINE_D2_VIEW_LEVEL);
        l.first_layer=n2c_word(d,TEGU_PIPELINE_D2_VIEW_LAYER);
        l.array_length=l.first_layer?l.first_layer+l.layers:0;
        l.usage=n2c_word(d,TEGU_PIPELINE_D2_USAGE);
        l.storage_mode=n2c_word(d,TEGU_PIPELINE_D2_STORAGE_MODE);
        l.access=kind==TEGU_PIPELINE2_KIND_IMAGE?
            ((flags&TEGU_PIPELINE2_ACCESS_READ?N2T_ACCESS_STORAGE_READ:0)|
             (flags&TEGU_PIPELINE2_ACCESS_WRITE?N2T_ACCESS_STORAGE_WRITE:0)):N2T_ACCESS_SAMPLE;
        if(kind==TEGU_PIPELINE2_KIND_IMAGE&&!l.access)l.access=N2T_ACCESS_STORAGE_READ;
        uint32_t row=n2c_word(d,TEGU_PIPELINE_D2_ROW_STRIDE);
        uint32_t slice=n2c_word(d,TEGU_PIPELINE_D2_SLICE_STRIDE);
        uint32_t layer=n2c_word(d,TEGU_PIPELINE_D2_LAYER_STRIDE);
        l.swizzle_word=n2c_word(d,TEGU_PIPELINE_D2_SWIZZLE);
        if(kind==TEGU_PIPELINE2_KIND_IMAGE&&l.swizzle_word){n2c_log(b,"storage image swizzle is not observable");e=BAD;goto cleanup;}
        l.swizzle_valid=(l.swizzle_word||
            (flags&TEGU_PIPELINE2_DESC_SWIZZLE_VALID))?1u:0u;
        if(level_offset>backing_length||span>backing_length-level_offset){e=BAD;goto cleanup;}
        if(l.layers==0||l.layers>plane_slots||
           (uint64_t)l.layers*l.levels>plane_slots){
            n2c_log(b,"texture plane payload exceeds helper-checked wire bound");e=BAD;goto cleanup;}
        if(n2c_fill_levels(&l,level_offset,row,slice,layer,span)){n2c_log(b,"mip/layer layout cannot be represented");e=BAD;goto cleanup;}
        if(n2t_layout_check(&l,0,backing_length)!=N2T_OK){n2c_log(b,"texture layout refused by shared packer");e=BAD;goto cleanup;}
        uint8_t stage_planes[N2C_MAX_PLANES_PER_DESC*N2C_PLANE_STRIDE];
        struct n2t_packed_texture packed;
        uint8_t *td=mem+N2C_TABLE1+bind*32;
        uint8_t *pd=mem+N2C_PLANES+i*plane_slots*N2C_PLANE_STRIDE;
        int pack_rc=kind==TEGU_PIPELINE2_KIND_IMAGE?
            n2t_layout_pack_storage(&l,backing_address,backing_length,
                cmd->va+N2C_TABLE1+bind*32,cmd->va+N2C_PLANES+i*plane_slots*N2C_PLANE_STRIDE,
                stage_planes,sizeof(stage_planes),&packed):
            n2t_layout_pack_sampled(&l,backing_address,backing_length,
                cmd->va+N2C_TABLE1+bind*32,cmd->va+N2C_PLANES+i*plane_slots*N2C_PLANE_STRIDE,
                stage_planes,sizeof(stage_planes),&packed);
        if(pack_rc!=N2T_OK){n2c_log(b,n2t_error_string(pack_rc));e=BAD;goto cleanup;}
        if(packed.payload_bytes>(uint32_t)sizeof(stage_planes)){e=BAD;goto cleanup;}
        n2t_texture_copy_descriptor(&packed,td);
        memcpy(pd,stage_planes,packed.payload_bytes);
        if(kind==TEGU_PIPELINE2_KIND_COMBINED) {
            struct n2t_sampler_state s={0};
            s.version=N2T_TEXTURE_RESOURCES_VERSION;
            s.min_filter=n2c_word(d,TEGU_PIPELINE_D2_MIN_FILTER);
            s.mag_filter=n2c_word(d,TEGU_PIPELINE_D2_MAG_FILTER);
            s.mip_filter=n2c_word(d,TEGU_PIPELINE_D2_MIP_FILTER);
            s.address_s=n2c_word(d,TEGU_PIPELINE_D2_ADDRESS_S);
            s.address_t=n2c_word(d,TEGU_PIPELINE_D2_ADDRESS_T);
            s.address_r=n2c_word(d,TEGU_PIPELINE_D2_ADDRESS_R);
            s.normalized=n2c_word(d,TEGU_PIPELINE_D2_NORMALIZED);
            uint32_t sf=n2c_word(d,TEGU_PIPELINE_D2_SAMPLER_FLAGS);
            s.compare_enabled=sf&1u;
            s.compare_function=n2c_word(d,TEGU_PIPELINE_D2_COMPARE);
            s.max_anisotropy=n2c_word(d,TEGU_PIPELINE_D2_MAX_ANISOTROPY);
            s.reduction=n2c_word(d,TEGU_PIPELINE_D2_REDUCTION);
            n2c_resolve_translated_sampler(&s,sf);
            s.clamp_integer_array=1;s.seamless_cube=1;
            s.border_color_kind=n2c_word(d,TEGU_PIPELINE_D2_BORDER_COLOR);
            if(s.border_color_kind>N2T_BORDER_OPAQUE_WHITE){e=BAD;goto cleanup;}
            memcpy(&s.lod_min,d+TEGU_PIPELINE_D2_LOD_MIN,4);
            memcpy(&s.lod_max,d+TEGU_PIPELINE_D2_LOD_MAX,4);
            struct n2t_format_info fi;
            uint32_t vf=l.view_format?l.view_format:l.format;
            if(n2t_format_query(vf,&fi)!=N2T_OK){e=BAD;goto cleanup;}
            if(n2t_sampler_layout_check(&s,&l)!=N2T_OK){n2c_log(b,"sampler/texture layout combination refused");e=BAD;goto cleanup;}
            if(n2t_sampler_pack_for_format(&s,&fi,mem+N2C_TABLE2+bind*32)!=N2T_OK){n2c_log(b,"sampler/format combination refused by shared packer");e=BAD;goto cleanup;}
        }
    }
    for(unsigned i=0;i<descriptors;i++) {
        const unsigned char *d=w+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+i*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
        if(n2c_word(d,TEGU_PIPELINE_D2_KIND)!=TEGU_PIPELINE2_KIND_SAMPLER)continue;
        uint32_t bind=n2c_word(d,TEGU_PIPELINE_D2_BINDING);
        struct n2t_sampler_state s={0};
        s.version=N2T_TEXTURE_RESOURCES_VERSION;
        s.min_filter=n2c_word(d,TEGU_PIPELINE_D2_MIN_FILTER);
        s.mag_filter=n2c_word(d,TEGU_PIPELINE_D2_MAG_FILTER);
        s.mip_filter=n2c_word(d,TEGU_PIPELINE_D2_MIP_FILTER);
        s.address_s=n2c_word(d,TEGU_PIPELINE_D2_ADDRESS_S);
        s.address_t=n2c_word(d,TEGU_PIPELINE_D2_ADDRESS_T);
        s.address_r=n2c_word(d,TEGU_PIPELINE_D2_ADDRESS_R);
        s.normalized=n2c_word(d,TEGU_PIPELINE_D2_NORMALIZED);
        uint32_t sf=n2c_word(d,TEGU_PIPELINE_D2_SAMPLER_FLAGS);
        s.compare_enabled=sf&1u;
        s.compare_function=n2c_word(d,TEGU_PIPELINE_D2_COMPARE);
        s.max_anisotropy=n2c_word(d,TEGU_PIPELINE_D2_MAX_ANISOTROPY);
        s.reduction=n2c_word(d,TEGU_PIPELINE_D2_REDUCTION);
        n2c_resolve_translated_sampler(&s,sf);
        s.clamp_integer_array=1;s.seamless_cube=1;
        s.border_color_kind=n2c_word(d,TEGU_PIPELINE_D2_BORDER_COLOR);
        if(s.border_color_kind>N2T_BORDER_OPAQUE_WHITE){e=BAD;goto cleanup;}
        memcpy(&s.lod_min,d+TEGU_PIPELINE_D2_LOD_MIN,4);
        memcpy(&s.lod_max,d+TEGU_PIPELINE_D2_LOD_MAX,4);
        uint8_t stage_sampler[32];
        if(n2t_sampler_pack(&s,stage_sampler)!=N2T_OK){n2c_log(b,"sampler state refused by shared packer");e=BAD;goto cleanup;}
        memcpy(mem+N2C_TABLE2+bind*32,stage_sampler,32);
    }
    struct nord2_gpu_submit_request req={.version=2,.kind=1,.heads={cmd->va,0},.nr_handles=1,
        .handles={cmd->handle}};
    req.write_mask=1u;
    /* The compiled code is read by the job: it is always in the handle list
     * (second, after the command block), never dropped when resources fill
     * the 32-handle request; a request that cannot hold it is refused. */
    if(add_ref(&req,code,0)){e=BAD;goto cleanup;}
    if(local.tls&&add_ref(&req,local.tls,0)){e=BAD;goto cleanup;}
    if(local.wls&&add_ref(&req,local.wls,0)){e=BAD;goto cleanup;}
    for(unsigned i=0;i<TEGU_PIPELINE2_MAX_BUFFERS;i++)if(buffers[i])if(add_ref(&req,buffers[i],
        (buffer_flags[i]&TEGU_PIPELINE2_ACCESS_WRITE)!=0)){e=BAD;goto cleanup;}
    for(unsigned i=0;i<256;i++)if(textures[i])if(add_ref(&req,textures[i],
        (texture_flags[i]&TEGU_PIPELINE2_ACCESS_WRITE)!=0)){e=BAD;goto cleanup;}
    uint64_t job=0;unsigned n=1;
    e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_SUBMIT_JM,NULL,0,&req,sizeof(req),&job,&n,NULL,NULL);
    if(e)goto cleanup;
    if(n!=1||!job){b->broken=1;e=FAILED;goto cleanup;}
    uint64_t in[2]={job,5000},state=0;n=1;
    e=scalar(b,NORD2_GPU_SEL_WAIT,in,2,&state,&n);
    if(e||n!=1||state!=4){b->broken=1;if(!e)e=FAILED;goto cleanup;}
    out[0]=job;out[1]=job;*count=2;
cleanup:
    n2c_local_drop(b,&local);
    if(cmd){int release=drop(b,cmd);if(!e)e=release;}
    return e;
}

static int dispatch(struct bridge *b,uint64_t pipeline,const void *wire,size_t size,uint64_t *out,unsigned *count) {
    if(!wire||size!=TEGU_PIPELINE_DISPATCH_BYTES||!out||*count<2)return BAD;
    struct pipeline *p=NULL;
    for(unsigned i=0;i<MAX_PIPE;i++)if(b->pipes[i].id==pipeline&&pipeline){p=&b->pipes[i];break;}
    if(!p||p->version!=1)return BAD;
    const unsigned char *w=wire;if(tegu_pipeline_word(w)!=1)return BAD;
    uint32_t groups[3];
    for(unsigned i=0;i<3;i++){groups[i]=tegu_pipeline_word(w+4+4*i);if(!groups[i]||groups[i]>65535)return BAD;}
    struct bo *resources[8]={0};uint32_t offsets[8]={0},lengths[8]={0};
    for(unsigned i=0;i<8;i++) {
        const unsigned char *d=w+16+16*i;uint32_t h=tegu_pipeline_word(d);
        offsets[i]=tegu_pipeline_word(d+4);lengths[i]=tegu_pipeline_word(d+8);
        if(tegu_pipeline_word(d+12))return BAD;
        if(!h){if(offsets[i]||lengths[i]||(p->info.bindings&(1u<<i)))return BAD;continue;}
        resources[i]=lookup(b,h);
        if(!resources[i]||resources[i]->internal||!lengths[i]||offsets[i]>resources[i]->size||lengths[i]>resources[i]->size-offsets[i])return BAD;
    }
    struct bo *code=lookup(b,p->code),*cmd=NULL;if(!code)return FAILED;
    int e=alloc(b,640,0,1,&cmd);if(e)return e;
    e=map_bo(b,cmd);if(e)goto cleanup;
    unsigned char *mem=(void *)(uintptr_t)cmd->cpu;
    /* 0: COMPUTE_JOB,128: SHADER_PROGRAM,192: LOCAL_STORAGE,
     * 256: FAU. All are in one owned, bounded GPU mapping. */
    struct nord2_gpu_compute_chain chain={.chain_pa=cmd->va,.shader=cmd->va+128,
        .thread_storage=cmd->va+192,.fau=cmd->va+256,.fau_count=(uint8_t)((p->info.push_words+1)/2),.index=1};
    for(unsigned i=0;i<3;i++){chain.workgroup_size[i]=p->info.local[i];chain.workgroup_count[i]=groups[i];}
    if(!nord2_gpu_jm_build_compute(mem,128,&chain)){e=BAD;goto cleanup;}
    nord2_desc_shader_program(mem+128,NORD2_STAGE_COMPUTE,code->va,(uint32_t)(p->info.preload>>48));
    /* Mesa v9 register allocation enum: 0=64,2=32. */
    if(p->info.registers<=32)tegu_pipeline_put(mem+128,tegu_pipeline_word(mem+128)|(2u<<30));
    nord2_desc_local_storage(mem+192,0,0,0,0);
    struct nord2_gpu_submit_request req={.version=2,.kind=1,.heads={cmd->va,0},.nr_handles=2,.handles={cmd->handle,code->handle}};
    for(unsigned i=0;i<8;i++)if(resources[i]) {
        unsigned char *d=mem+256+16*i;
        put64(d,resources[i]->va+offsets[i]);tegu_pipeline_put(d+8,lengths[i]); /* bounded global: address,size,offset0 */
        unsigned j=0;for(;j<req.nr_handles;j++)if(req.handles[j]==resources[i]->handle)break;
        if(j==req.nr_handles)req.handles[req.nr_handles++]=resources[i]->handle;
        req.write_mask|=1u<<j; /* conservatively invalidate every buffer after completion */
    }
    memcpy(mem+384,w+144,128);
    for(unsigned i=0;i<3;i++)tegu_pipeline_put(mem+512+4*i,groups[i]);
    uint64_t job=0;unsigned n=1;
    e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_SUBMIT_JM,NULL,0,&req,sizeof(req),&job,&n,NULL,NULL);
    if(e)goto cleanup;
    if(n!=1||!job){b->broken=1;e=FAILED;goto cleanup;}
    uint64_t in[2]={job,5000},state=0;n=1;
    e=scalar(b,NORD2_GPU_SEL_WAIT,in,2,&state,&n);
    if(e||n!=1||state!=4) {
        /* Unknown/running/faulted work must not be reused by another dispatch.
         * Kernel owns quarantined references even when RELEASE consumes handle. */
        b->broken=1;if(!e)e=FAILED;goto cleanup;
    }
    out[0]=job;out[1]=job;*count=2;
cleanup:
    if(cmd){int release=drop(b,cmd);if(!e)e=release;}
    return e;
}
int n2mb_method(uintptr_t value,unsigned sel,const uint64_t *in,unsigned ni,const void *bytes,size_t size,
                uint64_t *out,unsigned *count,void *outbytes,size_t *outsize) {
    struct bridge *b=(void *)value;
    if(!b||!count||(ni&&!in)||(size&&!bytes)||outbytes||(outsize&&*outsize))return BAD;
    b->ops.lock(b->ops.opaque);int e=UNSUPPORTED;
    if(b->chain.frames&&!b->broken&&(e=chain_flush(b,CHAIN_FLUSH_OTHER))){b->ops.unlock(b->ops.opaque);return e;}
    e=UNSUPPORTED;
    if(b->broken)e=NOTREADY;
    else if((sel==5||sel==7)&&wait_pending(b))e=b->broken?NOTREADY:FAILED; /* compute runs after every graphics job */
    else if(sel==5&&ni==0)e=import_pipeline(b,bytes,size,out,count);
    else if(sel==7&&ni==1) {
        if(size==TEGU_PIPELINE_DISPATCH2_BYTES)e=dispatch_v2(b,in[0],bytes,size,out,count);
        else if(size==TEGU_PIPELINE_DISPATCH_BYTES)e=dispatch(b,in[0],bytes,size,out,count);
        else e=BAD;
    }
    /* In particular donor selectors 9..20 MUST NOT be forwarded: these
     * numbers collide with Nord2 raw JM/sync and contain CSF packets. */
    b->ops.unlock(b->ops.opaque);return e;
}

int n2mb_import_surface(uintptr_t value,uint32_t sid,uint64_t size,uint64_t cpu,uint64_t *handle){
 struct bridge *b=(void *)value;if(!b||!sid||!size||size>UINT32_MAX||!cpu||!handle)return BAD;
 b->ops.lock(b->ops.opaque);int e=NOTREADY;struct bo *o=NULL;
 if(b->broken)goto end;
 o=bo_slot(b);
 if(!o&&cmdpool_reclaim(b))o=bo_slot(b);
 if(!o)goto end;
 uint64_t args[4]={sid,0,size,0},reply[3]={0};unsigned n=3;
 e=scalar(b,NORD2_GPU_SEL_IMPORT_SURFACE,args,4,reply,&n);if(e)goto end;
 if(n!=3||!reply[0]||reply[0]>UINT32_MAX||lookup(b,reply[0])||!reply[1]||(reply[1]&4095)||reply[1]>=(1ull<<40)||reply[2]>(1ull<<40)-reply[1]||reply[2]<size){b->broken=1;e=FAILED;goto end;}
 *o=(struct bo){.handle=reply[0],.va=reply[1],.size=size,.cpu=cpu,.mapped=size,.external_cpu=1};bo_taken(b,o);*handle=o->handle;
end:b->ops.unlock(b->ops.opaque);return e;
}
/* Ordered shared-storage transfer. Both handles are retained by the caller's
 * command; the same lock serializes completed GPU work and subsequent submits. */
int n2mb_copy(uintptr_t value,uint64_t source,uint64_t offset,uint64_t destination,uint64_t bytes){
 struct bridge *b=(void *)value;if(!b||!bytes||bytes>(64ull<<20)||source==destination)return BAD;
 b->ops.lock(b->ops.opaque);int e=BAD;
 struct bo *src=lookup(b,source),*dst=lookup(b,destination);
 if(b->broken){e=NOTREADY;goto done;}
 if(!src||!dst||src->internal||dst->internal||dst->external_cpu||offset>src->size||bytes>src->size-offset||bytes>dst->size)goto done;
 if((e=wait_pending(b)))goto done; /* CPU copy of GPU-written memory */
 /* An imported IOSurface is not cache-maintained per job at kernel tracking
  * level 2: acquire it (SYNC to CPU invalidates GPU-written lines) before
  * the CPU reads it. Bridge-owned mapped buffers are maintained per job. */
 if(src->external_cpu){uint64_t args[2]={src->handle,0};unsigned n=0;if((e=scalar(b,NORD2_GPU_SEL_SYNC,args,2,NULL,&n)))goto done;}
 if(!src->cpu&&(e=map_bo(b,src)))goto done;
 if(!dst->cpu&&(e=map_bo(b,dst)))goto done;
 memcpy((void *)(uintptr_t)dst->cpu,(const void *)(uintptr_t)(src->cpu+offset),(size_t)bytes);e=0;
done:b->ops.unlock(b->ops.opaque);return e;
}
static int add_ref(struct nord2_gpu_submit_request *req,struct bo *o,unsigned write){
 if(!o)return BAD;unsigned n=0;for(;n<req->nr_handles;n++)if(req->handles[n]==o->handle)break;
 if(n==req->nr_handles){if(n==32)return BAD;req->handles[req->nr_handles++]=o->handle;}
 if(write)req->write_mask|=1u<<n;return 0;
}
static uint64_t resolve(struct bridge *b,const struct tegu_generic_draw_memory *m,struct nord2_gpu_submit_request *req){
 if(!m->token)return m->offset||m->bytes?UINT64_MAX:0;
 struct bo *o=lookup(b,m->token);
 if(!o||o->internal||!m->bytes||m->offset>o->size||m->bytes>o->size-m->offset||add_ref(req,o,0))return UINT64_MAX;
 return o->va+m->offset;
}
static int copy_code(struct bridge *b,const uint8_t *bytes,size_t n,struct bo **o){
 int e=alloc(b,n,1,1,o);if(e)return e;e=map_bo(b,*o);if(!e)memcpy((void *)(uintptr_t)(*o)->cpu,bytes,n);return e;
}
/* Byte-identical shader code is uploaded once per bridge instead of being
 * allocated, mapped, copied and released for every draw (c6gc: the 0.8 MB
 * QuartzCore blur fragment binaries were re-uploaded per draw). */
#define CODE_CACHE_BYTES (8ull<<20)
static int code_equal(const uint8_t *a,const uint8_t *b,size_t n){ /* freestanding: no memcmp */
 for(size_t i=0;i<n;i++)if(a[i]!=b[i])return 0;
 return 1;
}
/* `pin`: the clock value at the start of this draw; entries used since then
 * belong to the job being built and are never evicted. */
static int cached_code(struct bridge *b,const uint8_t *bytes,size_t n,uint64_t pin,struct bo **out){
 if(!bytes||!n)return BAD;
 for(unsigned i=0;i<16;i++){struct bo *o=b->code[i];
  if(o&&o->handle&&o->cpu&&b->code_len[i]==n&&code_equal((const uint8_t *)(uintptr_t)o->cpu,bytes,n)){
   b->code_used[i]=++b->code_clock;*out=o;return 0;}}
 struct bo *o=NULL;int e=copy_code(b,bytes,n,&o);if(e){if(o&&o->handle)(void)drop(b,o);return e;}
 unsigned slot=16;
 if(n<=CODE_CACHE_BYTES) {
  for(;;) {
   unsigned victim=16;slot=16;
   for(unsigned i=0;i<16;i++){if(!b->code[i]){if(slot==16)slot=i;continue;}
    if(b->code_used[i]>pin)continue;
    if(victim==16||b->code_used[i]<b->code_used[victim])victim=i;}
   if(slot!=16&&b->code_total+n<=CODE_CACHE_BYTES)break;
   if(victim==16){slot=16;break;}
   b->code_total-=b->code_len[victim];(void)drop(b,b->code[victim]);b->code[victim]=NULL;b->code_len[victim]=0;
  }
 }
 if(slot<16){b->code[slot]=o;b->code_len[slot]=n;b->code_used[slot]=++b->code_clock;b->code_total+=n;}
 *out=o;return slot<16?0:1; /* 1: caller owns an uncached BO */
}
static void clear_pixel(const struct tegu_generic_draw_request *q,uint8_t pixel[16]){
 uint32_t c=q->target_format?q->clear_words[0]:q->clear_color;
 memset(pixel,0,16);
 /* Raw targets: the tile clear is the texel's own bytes (pan_pack_raw). */
 if(n2r_rt_is_raw(q->target_format)){memcpy(pixel,q->clear_words,n2r_bpp(q->target_format));return;}
 if(q->target_format==1){pixel[0]=c>>24;return;} /* A8: the tile's alpha byte */
 if(q->target_format==115){memcpy(pixel,q->clear_words,8);return;}
 if(q->target_format==10){pixel[0]=c;return;}
 if(q->target_format==43){
  /* Tile word (R5.5 G5.5 B5.5 A1.1, n2r_clear) -> BGR5A1Unorm memory:
   * B bits 0-4, G 5-9, R 10-14, A 15. */
  uint32_t r=(c>>5)&31u,g=(c>>15)&31u,bl=(c>>25)&31u,a=(c>>31)&1u;
  uint16_t v=(uint16_t)(bl|(g<<5)|(r<<10)|(a<<15));memcpy(pixel,&v,2);return;
 }
 if(q->target_format==40){
  /* Tile word R5.5 G6.4 B5.5 A0.2 (n2r_clear) -> B5G6R5Unorm memory
   * (PIPE B5G6R5_UNORM): B bits 0-4, G 5-10, R 11-15. */
  uint32_t r=(c>>5)&31u,g=(c>>14)&63u,bl=(c>>25)&31u;
  uint16_t v=(uint16_t)(bl|(g<<5)|(r<<11));memcpy(pixel,&v,2);return;
 }
 if(q->target_format==42){
  /* Tile word R4.4 G4.4 B4.4 A4.4 -> ABGR4Unorm memory (PIPE A4B4G4R4):
   * A bits 0-3, B 4-7, G 8-11, R 12-15. */
  uint32_t r=(c>>4)&15u,g=(c>>12)&15u,bl=(c>>20)&15u,a=(c>>28)&15u;
  uint16_t v=(uint16_t)(a|(bl<<4)|(g<<8)|(r<<12));memcpy(pixel,&v,2);return;
 }
 if(!q->target_format||q->target_format==80||q->target_format==81){pixel[0]=c>>16;pixel[1]=c>>8;pixel[2]=c;pixel[3]=c>>24;}
 else if(q->target_format==94){
  /* n2r_clear packs R in bits 0-9 (the RGB10A2 tile word); BGR10A2Unorm
   * memory keeps B there. */
  uint32_t v=((c&1023u)<<20)|(c&(1023u<<10))|((c>>20)&1023u)|(c&(3u<<30));memcpy(pixel,&v,4);
 }
 else memcpy(pixel,&c,4);
}
/* Bounded opt-in probe evidence. Completed descriptor writebacks distinguish
 * empty tiling from shader/colour problems without another kernel build. */
static void render_trace(struct bridge *b,const uint8_t *m,const struct n2r_addresses *a,
 const struct tegu_generic_draw_request *q,const struct tegu_generic_draw_shader *s,unsigned preload) {
 if(!b->trace_draws)return;b->trace_draws--;
 uint64_t polygon=0,heap_bottom=0,packet=0;uint32_t vs=0,ps=0,fs=0;
 memcpy(&polygon,m+512,8);memcpy(&heap_bottom,m+768+16,8);
 memcpy(&packet,m+N2R_JOB+128+8,8);memcpy(&vs,m+N2R_JOB,4);memcpy(&fs,m+N2R_FRAGMENT,4);
 if(preload)memcpy(&ps,m+N2R_BLOCK+N2R_JOB,4);
 char line[240];int len=snprintf(line,sizeof(line),
  "NORD2_METAL draw f=%u load=%u vtx=%u attr=%u varying=%u head=%08x/%08x/%08x poly=%llx packet=%llx heap-used=%llu\n",
  q->target_format,q->load_action,q->vertex_count,s->attribute_count,s->varying_bytes,ps,vs,fs,
  (unsigned long long)polygon,(unsigned long long)packet,(unsigned long long)(heap_bottom>=a->heap?heap_bottom-a->heap:0));
 if(len>0&&(size_t)len<sizeof(line)){unsigned no=0;size_t bytes=0;
  (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
}
/* Probe-only reads of owned geometry memory. Never follow GPU pointers.
 * Eight low-level draws cover all four public geometry cases, including
 * their implicit clear/preload passes. Two records plus bin counts keep
 * this bounded well below the kernel's per-client log quota. */
static void geometry_record(struct bridge *b,const uint8_t *p,size_t off) {
 uint32_t words[16];memcpy(words,p+off,sizeof(words));
 char line[240];int len=snprintf(line,sizeof(line),"NORD2_METAL heap+%06zx",off);
 for(unsigned k=0;k<16&&len>0&&(size_t)len<sizeof(line);k++)
  len+=snprintf(line+len,sizeof(line)-(size_t)len," %08x",words[k]);
 if(len>0&&(size_t)len+1<sizeof(line)){line[len++]='\n';unsigned no=0;size_t bytes=0;
  (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
}
static void geometry_before(struct bridge *b,struct bo *heap,uint64_t bytes) {
 if(!b->trace_geometry||!heap||bytes<65536||bytes>heap->size)return;
 if(!heap->cpu&&map_bo(b,heap))return;
 /* Observe the persistent heap without changing it. A nonzero pre-submit
  * sample can now belong to a preceding completed draw. */
 const uint8_t *p=(const void *)(uintptr_t)heap->cpu;
 unsigned nonzero=0;for(size_t k=0;k<65536;k++)nonzero+=p[k]!=0;
 for(size_t k=bytes-256;k<bytes;k++)nonzero+=p[k]!=0;
 char line[160];int len=snprintf(line,sizeof(line),
  "NORD2_METAL geometry-before ordinal=%u sampled=65792 nonzero=%u\n",9-b->trace_geometry,nonzero);
 if(len>0&&(size_t)len<sizeof(line)){unsigned no=0;size_t bytes=0;
  (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
}
static void geometry_trace(struct bridge *b,struct bo *heap,const uint8_t *m,
 const struct n2r_addresses *a) {
 if(!b->trace_geometry||!heap)return;
 unsigned ordinal=9-b->trace_geometry;b->trace_geometry--;
 if(!heap->cpu&&map_bo(b,heap))return;
 uint64_t args[2]={heap->handle,0};unsigned n=0;
 if(scalar(b,NORD2_GPU_SEL_SYNC,args,2,NULL,&n))return;
 uint64_t bottom=0;memcpy(&bottom,m+768+16,8);
 if(bottom<a->heap||bottom-a->heap>a->heap_bytes||a->heap_bytes<65536||a->heap_bytes>heap->size)return;
 size_t used=(size_t)(bottom-a->heap);if(used>65536)used=65536;
 const uint8_t *p=(const void *)(uintptr_t)heap->cpu;
 size_t first=SIZE_MAX,last=SIZE_MAX;unsigned records=0,bins=0;
 for(size_t off=0;off+64<=used;off+=64){
  uint32_t words[16];memcpy(words,p+off,sizeof(words));unsigned any=0;
  for(unsigned k=0;k<16;k++)any|=words[k];if(!any)continue;
  if(first==SIZE_MAX)first=off;last=off;records++;
 }
 uint64_t bin_first=0;size_t bin_offset=0;
 for(size_t off=(size_t)a->heap_bytes-256;off<a->heap_bytes;off+=8){
  uint64_t value=0;memcpy(&value,p+off,8);if(value){if(!bins){bin_first=value;bin_offset=off;}bins++;}
 }
 uint32_t warp=0;memcpy(&warp,m+1536,4);
 char line[240];int len=snprintf(line,sizeof(line),
  "NORD2_METAL geometry-after ordinal=%u records=%u bins=%u first-bin=%zx:%llx position-spd=%08x\n",
  ordinal,records,bins,bin_offset,(unsigned long long)bin_first,warp);
 if(len>0&&(size_t)len<sizeof(line)){unsigned no=0;size_t bytes=0;
  (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
 if(first!=SIZE_MAX)geometry_record(b,p,first);
 if(last!=first&&last!=SIZE_MAX)geometry_record(b,p,last);
}
struct n2c_mrt {
    uint32_t count,version,sample_count,has_resolves;
    const struct tegu_blend_inputs *blends;
    uint32_t samples[N2MB_MAX_RENDER_TARGETS];
    struct n2r_render_target rt[N2MB_MAX_RENDER_TARGETS];
    struct n2r_resolve_target resolve[N2MB_MAX_RENDER_TARGETS];
    struct bo *bo[N2MB_MAX_RENDER_TARGETS];
    struct bo *resolve_bo[N2MB_MAX_RENDER_TARGETS];
    uint64_t token[N2MB_MAX_RENDER_TARGETS];
    uint64_t resolve_token[N2MB_MAX_RENDER_TARGETS];
    uint64_t offset[N2MB_MAX_RENDER_TARGETS];
    uint64_t resolve_offset[N2MB_MAX_RENDER_TARGETS];
    uint64_t length[N2MB_MAX_RENDER_TARGETS];
    uint64_t resolve_length[N2MB_MAX_RENDER_TARGETS];
};

/* Resolve the additive target-token sidecar into the texture builder's
 * resolved-address array.  The caller has not submitted the request yet, so a
 * validation failure has no kernel-visible effect. */
static int n2c_mrt_prepare(struct bridge *b,const struct tegu_generic_draw_request *q,
                           const struct n2mb_render_targets *src,struct n2c_mrt *m,
                           struct nord2_gpu_submit_request *req,unsigned allow_load)
{
    if(!b||!q||!m||!req)return BAD;
    memset(m,0,sizeof(*m));
    int from_src=src!=0;
    struct n2mb_render_target one={0};
    struct n2mb_render_targets one_targets={0};
    if(!src) {
        if(q->target_format!=10)return 0;
        if(!q->target_token||!q->target_length)return BAD;
        one=(struct n2mb_render_target){.token=q->target_token,.offset=q->target_offset,
            .length=q->target_length,.format=10,.load_action=q->load_action,
            .write_mask=15,.row_bytes=q->row_bytes,.width=q->width,.height=q->height};
        memcpy(one.clear_words,q->clear_words,sizeof(one.clear_words));
        one_targets=(struct n2mb_render_targets){.version=N2MB_RENDER_TARGETS_VERSION,
            .count=1,.targets=&one};
        src=&one_targets;
    }
    if(!src->targets||src->count==0||src->count>N2MB_MAX_RENDER_TARGETS||
       (src->version!=N2MB_RENDER_TARGETS_VERSION&&
        src->version!=N2MB_RENDER_TARGETS_BLEND_VERSION&&
        src->version!=N2MB_RENDER_TARGETS_MSAA_VERSION))return BAD;
    if(src->version==N2MB_RENDER_TARGETS_BLEND_VERSION&&!src->blends)return BAD;
    if(src->version==N2MB_RENDER_TARGETS_MSAA_VERSION&&!src->sample_counts)return BAD;
    unsigned samples=1;
    if(src->version==N2MB_RENDER_TARGETS_MSAA_VERSION) {
        samples=src->sample_counts[0];
        if(samples!=1&&samples!=4)return BAD;
        for(uint32_t i=0;i<src->count;i++) {
            if(src->sample_counts[i]!=samples)return BAD;
            m->samples[i]=samples;
        }
    }
    m->sample_count=samples;
    /* v2/v3 blend records are validated before any target BO is retained.
     * v3 may omit blends and keep the shared shader blend. */
    if(src->version==N2MB_RENDER_TARGETS_BLEND_VERSION||
       (src->version==N2MB_RENDER_TARGETS_MSAA_VERSION&&src->blends)) {
        for(uint32_t i=0;i<src->count;i++) {
            uint32_t format=src->targets[i].format?src->targets[i].format:80;
            struct tegu_blend_state checked;
            if(src->blends[i].write_mask!=src->targets[i].write_mask||
               tegu_blend_build_format(format,&src->blends[i],&checked))return BAD;
        }
    }
    for(uint32_t i=0;i<src->count;i++) {
        const struct n2mb_render_target *t=&src->targets[i];
        uint32_t format=t->format?t->format:80;
        uint32_t bpp=n2r_bpp(format);
        uint64_t bytes=(uint64_t)t->row_bytes*t->height*samples;
        if(!bpp||!t->token||t->token>0xffffffffu||(t->reserved&~N2MB_TARGET_DISCARD)||t->load_action>2||
           (from_src&&t->load_action==1&&!allow_load)||(t->write_mask&~15u)||
           !t->width||!t->height||t->width!=q->width||t->height!=q->height||
           (t->row_bytes&63u)||!bytes||bytes>(1ull<<48)||
           t->row_bytes<(uint64_t)t->width*bpp||t->offset>t->length)return BAD;
        struct bo *o=lookup(b,(unsigned)t->token);
        if(!o||o->internal||t->length>o->size||bytes>t->length-t->offset)return BAD;
        uint64_t address=o->va+t->offset;
        if(!address||(src->version==N2MB_RENDER_TARGETS_MSAA_VERSION&&(address&63u))||
           address>=(1ull<<48)||address>(1ull<<48)-1-bytes)return BAD;
        for(uint32_t j=0;j<i;j++)if(m->rt[j].address==address)return BAD;
        if(add_ref(req,o,1))return BAD;
        m->bo[i]=o;m->token[i]=t->token;m->offset[i]=t->offset;m->length[i]=t->length;
        m->rt[i]=(struct n2r_render_target){.format=format,.load_action=t->load_action,
            .write_mask=t->write_mask,.reserved=t->reserved&N2MB_TARGET_DISCARD,.address=address,.row_bytes=t->row_bytes,
            .width=t->width,.height=t->height};
        memcpy(m->rt[i].clear_words,t->clear_words,sizeof(m->rt[i].clear_words));
    }
    if(src->version==N2MB_RENDER_TARGETS_MSAA_VERSION&&src->resolves) {
        m->has_resolves=1;
        for(uint32_t i=0;i<src->count;i++) {
            const struct n2mb_resolve_target *r=&src->resolves[i];
            struct n2r_resolve_target *dst=&m->resolve[i];
            if(!r->token)continue;
            if(samples!=4||r->reserved||!r->token||r->token>0xffffffffu||r->offset>r->length)return BAD;
            uint32_t tf=src->targets[i].format?src->targets[i].format:80;
            uint32_t rf=r->format?r->format:tf;
            uint32_t bpp=n2r_bpp(rf);
            uint64_t bytes=(uint64_t)r->row_bytes*r->height;
            if(rf!=tf||!bpp||!r->width||!r->height||r->width!=q->width||r->height!=q->height||
               (r->row_bytes&63u)||r->row_bytes<(uint64_t)r->width*bpp||
               !bytes||bytes>(1ull<<48)||r->offset>r->length)return BAD;
            struct bo *o=lookup(b,(unsigned)r->token);
            if(!o||o->internal||r->length>o->size||bytes>r->length-r->offset)return BAD;
            uint64_t address=o->va+r->offset;
            if(!address||(address&63u)||address>=(1ull<<48)||address>(1ull<<48)-1-bytes)return BAD;
            for(uint32_t j=0;j<src->count;j++)
                if(address==m->rt[j].address||
                   (j<i&&m->resolve[j].address&&address==m->resolve[j].address))return BAD;
            if(add_ref(req,o,1))return BAD;
            m->resolve_bo[i]=o;m->resolve_token[i]=r->token;
            m->resolve_offset[i]=r->offset;m->resolve_length[i]=r->length;
            dst->address=address;dst->format=rf;dst->row_bytes=r->row_bytes;
            dst->width=r->width;dst->height=r->height;dst->reserved=0;
        }
    }
    m->count=src->count;m->version=src->version;
    m->blends=src->version==N2MB_RENDER_TARGETS_VERSION?NULL:src->blends;
    /* True-MRT LOAD (pre-frame only): single-sampled, no resolves, at most
     * two loaded targets (pre_frame_0/pre_frame_1 DCDs). */
    if(from_src) {
        unsigned loaded=0;
        for(uint32_t i=0;i<src->count;i++)loaded+=src->targets[i].load_action==1;
        /* allow_load 2: a batch member continuing the job (no preload). */
        if(loaded&&allow_load==1&&(samples!=1||m->has_resolves||loaded>2))return BAD;
    }
    return 0;
}
/* True-MRT LOAD: one pre-frame DCD per loaded target, built in consecutive
 * blocks from `m` (GPU va `va`); the second target's DCD is copied into the
 * first block's array as DRAW 1. Each DCD samples its target in place, like
 * the single-target pre-frame preload. */
static int mrt_frame_preload(uint8_t *m,uint64_t va,const struct n2c_mrt *mt,uint64_t code_va,
                             const struct n2r_addresses *base,uint32_t *mask)
{
    unsigned li=0;
    for(uint32_t i=0;i<mt->count;i++) {
        if(mt->rt[i].load_action!=1)continue;
        if(li==2)return BAD;
        struct n2r_render_target rt[N2MB_MAX_RENDER_TARGETS];
        memcpy(rt,mt->rt,sizeof(rt));
        for(uint32_t k=0;k<mt->count;k++)rt[k].write_mask=k==i?15u:0u;
        uint8_t *blk=m+(size_t)li*N2R_BLOCK;uint64_t cmd=va+(uint64_t)li*N2R_BLOCK;
        struct n2r_render_targets rts={.version=N2MB_RENDER_TARGETS_VERSION,.count=mt->count,.targets=rt};
        if(mt->count>2)rts.sample_locations=cmd+12288u;
        const struct n2r_render_target *t=&mt->rt[i];
        struct tegu_generic_draw_request pre={.target_format=preload_format(t->format),.width=t->width,.height=t->height,
            .row_bytes=t->row_bytes,.load_action=1,.vertex_count=3,.scissor={0,0,t->width,t->height},.binding_count=1};
        pre.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,
            .memory={.token=mt->token[i],.offset=mt->offset[i],.bytes=(uint64_t)t->row_bytes*t->height},
            .width=t->width,.height=t->height,.row_bytes=t->row_bytes,.format=preload_format(t->format)};
        struct n2r_addresses pa=*base;pa.command=cmd;pa.vertex_code=0;pa.fragment_code=code_va;
        pa.binding[0]=t->address;pa.target=t->address;pa.frame_shader_dcds=0;pa.frame_shader_mask=0;pa.extent_valid=0;
        /* One float frame-preload program serves every MRT target (code_va);
         * raw targets never reach MRT (mrt_format_ok), refuse if one does. */
        if(n2r_rt_raw_twin(t->format))return BAD;
        if(n2r_build_frame_preload_rt(blk,&n2_frame_preload_shader,&pre,&pa,&rts,i))return BAD;
        if(li==1)memcpy(m+N2R_FRAME_DCDS+128,blk+N2R_FRAME_DCDS,128);
        li++;
    }
    *mask=li==2?3u:li?1u:0u;
    return 0;
}

/* Submitted frame-owner block: its framebuffer size and FRAGMENT_JOB bound. */
static void tile_stat(struct bridge *b,const uint8_t *m){
 uint32_t fb,lo,hi;memcpy(&fb,m+32,4);memcpy(&lo,m+N2R_FRAGMENT+32,4);memcpy(&hi,m+N2R_FRAGMENT+36,4);
 uint64_t w=(fb&0xffffu)+1,h=(fb>>16)+1; /* FRAMEBUFFER_PARAMETERS word 8: width-1 | height-1<<16 */
 uint64_t fx=((hi&0xfffu)>=(lo&0xfffu))?(hi&0xfffu)-(lo&0xfffu)+1:0,fy=(((hi>>16)&0xfffu)>=((lo>>16)&0xfffu))?((hi>>16)&0xfffu)-((lo>>16)&0xfffu)+1:0;
 b->stat_tiles[0]++;b->stat_tiles[1]+=((w+15)/16)*((h+15)/16);b->stat_tiles[2]+=fx*fy;
}
struct diag {unsigned spacer;uint8_t block[2048];};
#define N2MB_FAIL_AT() do{b->bad_site=__LINE__;goto done;}while(0)
static int draw_locked(struct bridge *b,const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q,
 const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts,
 const struct n2mb_render_targets *mrt,uint64_t *out,unsigned *count,struct diag *d){
 struct bo *owned[8]={0}; /* cmd, (unused), VS, FS, snapshot, preloadVS, preloadFS, VA spacer */
 struct bo *tls=NULL,*pooled=NULL;uint64_t pooled_job=0;
 /* code used from here on belongs to this job (and an open chain's) */
 b->code_protect=b->chain.frames?b->chain.code_clock:b->code_clock;
 int e=BAD;if(b->broken){e=NOTREADY;N2MB_FAIL_AT();}
 async_probe(b);
 /* Diagnostics read GPU-written memory around the job: keep them ordered. */
 unsigned sync=d||b->trace_draws||b->trace_geometry;
 if(sync&&(e=wait_pending(b)))N2MB_FAIL_AT();
 e=BAD; /* a successful wait left e=0: later bare refusals must not report success */
 struct nord2_gpu_submit_request req={.version=2,.kind=2};
 unsigned preframe=b->preframe; /* read once: owner and preload blocks agree */
 /* In-pass depth/stencil lives in the tile buffer only: a job that would
  * continue an earlier job's depth (load 1) cannot own a frame. */
 if(opts&&(opts->flags&N2R_DRAW_DEPTH_STENCIL)&&(!opts->zs||opts->zs->load==1)){e=BAD;N2MB_FAIL_AT();}
 struct n2c_mrt mt={0};if(n2c_mrt_prepare(b,q,mrt,&mt,&req,preframe==1))N2MB_FAIL_AT();
 int use_mrt=mt.count!=0;
 unsigned mrt_loaded=0;for(uint32_t i=0;i<mt.count;i++)mrt_loaded+=mt.rt[i].load_action==1;
 struct bo *target=use_mrt?mt.bo[0]:lookup(b,q->target_token);
 uint64_t target_bytes=use_mrt?(uint64_t)mt.rt[0].row_bytes*mt.rt[0].height:
  (uint64_t)q->row_bytes*q->height;
 if(!use_mrt&&(!target||target->internal||q->target_length>target->size||
    q->target_offset>q->target_length||target_bytes>q->target_length-q->target_offset||
    add_ref(&req,target,1)))N2MB_FAIL_AT();
 uint64_t target_va=use_mrt?mt.rt[0].address:target->va+q->target_offset;
 struct n2r_addresses a={.target=target_va,.heap_bytes=2u<<20};
 /* A fully clipped draw keeps the full bound (its job still runs the
  * target's load/store; n2r ignores the extent for Clear and resolves). */
 if(b->extent&&n2r_draw_extent(q,a.extent))a.extent_valid=1;
 a.vertex=resolve(b,&q->vertex,&req);a.index=resolve(b,&q->index,&req);
 if(a.vertex==UINT64_MAX||a.index==UINT64_MAX)N2MB_FAIL_AT();
 /* A CPU snapshot of the target is needed only for feedback: a framebuffer
  * fetch or a texture binding of the target itself, whose fragments may read
  * pixels other tiles have already written back. A plain LOAD preload reads
  * each pixel of the target exactly once, inside the same tile, before that
  * tile's writeback (nearest sampler at texel centres, as Mesa's pre-frame
  * preload reads the RT image in place), so it samples the target directly.
  * c6gc: the copy cost an 11 MB allocation, SYNC and memcpy per LOAD draw on
  * the 1125x2436 display. */
 unsigned snapshot=0,fetch=0,texture_feedback=0;
 for(unsigned i=0;i<q->binding_count;i++){
  a.binding[i]=resolve(b,&q->bindings[i].memory,&req);
  if(a.binding[i]==UINT64_MAX)N2MB_FAIL_AT();
  if(q->bindings[i].kind==TEGU_GENERIC_DRAW_ATTACHMENT) {
   /* A true-MRT job has no per-target snapshot transport yet.  Do not
    * silently feed the ordinary target or a stale texture. */
   if(use_mrt){e=BAD;N2MB_FAIL_AT();}
   /* Framebuffer fetch reads the fragment's own pixel. One draw is one
    * fragment job and a tile is written back only after all of its
    * fragments, so the target memory holds exactly the pre-draw value a
    * copy would (c6hc: the 11 MB copy of the display exhausted the 64 MiB
    * GPU context budget, site 1196). A CLEAR pass still needs the copy
    * filled with the clear colour. */
   if(q->load_action==2)snapshot=1;else fetch=1;
  }
  /* Sampling the target itself at arbitrary coordinates crosses tiles that
   * may already be written back: that read needs the snapshot. */
  /* A binding of another byte range of the target's BO (a blur pyramid
   * sampling level L-1 while rendering level L) reads nothing this job
   * writes; earlier jobs' writes are ordered as for any texture. */
  if(!use_mrt&&q->bindings[i].memory.token==q->target_token&&
     (!q->bindings[i].memory.bytes||(q->bindings[i].memory.offset<q->target_offset+target_bytes&&
      q->target_offset<q->bindings[i].memory.offset+q->bindings[i].memory.bytes)))snapshot=texture_feedback=1;
 }
 if(!b->heap&&(e=heap_alloc(b)))N2MB_FAIL_AT();
 if(b->tiler.heap_bytes>b->heap->size){e=BAD;N2MB_FAIL_AT();}
 a.heap_bytes=b->tiler.heap_bytes;a.heap_chunk_enum1=b->tiler.chunk_enum1;
 a.hierarchy_mask=b->tiler.mask;a.no_secondary=b->tiler.no_secondary;
 if(d&&d->spacer&&(e=alloc(b,65536,0,1,&owned[7])))N2MB_FAIL_AT(); /* shifts the command VA */
 /* Diagnostic draws keep a fresh BO: the VA spacer relies on allocation order. */
 uint64_t blocks=(uint64_t)(mrt_loaded==2?3:2)*N2R_BLOCK;
 if(d){e=alloc(b,blocks,0,1,&owned[0]);if(e)N2MB_FAIL_AT();e=map_bo(b,owned[0]);if(e)N2MB_FAIL_AT();}
 else{e=cmdpool_get(b,blocks,&owned[0]);if(e)N2MB_FAIL_AT();pooled=owned[0];}
 struct bo *code[4]={0};uint64_t pin=b->code_protect;
 e=cached_code(b,s->vertex_code,s->vertex_bytes,pin,&code[0]);if(e==1){owned[2]=code[0];e=0;}if(e)N2MB_FAIL_AT();
 e=cached_code(b,s->fragment_code,s->fragment_bytes,pin,&code[1]);if(e==1){owned[3]=code[1];e=0;}if(e)N2MB_FAIL_AT();
 /* Chained submission: a plain frame takes a tiler-heap slice of the chain. */
 uint64_t heap_va=0;
 struct bo *heap_bo=chain_heap(b,!sync&&!d&&!snapshot&&!(opts&&(opts->thread_storage_bytes||opts->thread_storage_reserved)),&heap_va);
 a.command=a.frame=owned[0]->va;a.heap=heap_va;a.vertex_code=code[0]->va;a.fragment_code=code[1]->va;
 if(opts&&(opts->thread_storage_bytes||opts->thread_storage_reserved)) {
  if(opts->version!=N2R_DRAW_OPTIONS_VERSION||
     opts->thread_storage_bytes>N2R_THREAD_STORAGE_MAX||opts->thread_storage_reserved) {
   e=BAD;N2MB_FAIL_AT();
  }
  uint32_t cores=0;uint64_t total=0;
  if(n2c_core_range(b,&cores)){e=BAD;N2MB_FAIL_AT();}
  if(n2r_thread_storage_total(opts->thread_storage_bytes,2048,cores,&total)||
     !total||total>(64ull<<20)||alloc(b,total,ALLOC_GPU_ONLY,1,&tls)||!tls->va||(tls->va&4095u)) {
   e=BAD;N2MB_FAIL_AT();
  }
  a.tls=tls->va;
  if(add_ref(&req,tls,0)){e=BAD;N2MB_FAIL_AT();}
 }
 if(snapshot){
  if((e=wait_pending(b)))N2MB_FAIL_AT(); /* the CPU copy needs completed GPU writes */
  if(!target->cpu){e=map_bo(b,target);if(e)N2MB_FAIL_AT();}
  uint64_t args[2]={target->handle,0};unsigned n=0;e=scalar(b,NORD2_GPU_SEL_SYNC,args,2,NULL,&n);if(e)N2MB_FAIL_AT();
  /* Separate failure sites: target sampled as a texture vs CLEAR+fetch. */
  if(texture_feedback){e=alloc(b,target->size,0,1,&owned[4]);if(e)N2MB_FAIL_AT();}
  else{e=alloc(b,target->size,0,1,&owned[4]);if(e)N2MB_FAIL_AT();}
  e=map_bo(b,owned[4]);if(e)N2MB_FAIL_AT();
  memcpy((void *)(uintptr_t)owned[4]->cpu,(const void *)(uintptr_t)target->cpu,target->size);
  if(q->load_action==2){
   uint8_t pixel[16];clear_pixel(q,pixel);unsigned bytes=n2r_bpp(q->target_format);
   uint8_t *dst=(void *)(uintptr_t)(owned[4]->cpu+q->target_offset);
   for(unsigned y=0;y<q->height;y++)for(unsigned x=0;x<q->width;x++)memcpy(dst+(size_t)y*q->row_bytes+x*bytes,pixel,bytes);
  }
  a.snapshot=owned[4]->va+q->target_offset;
  for(unsigned i=0;i<q->binding_count;i++)if(q->bindings[i].memory.token==q->target_token)a.binding[i]=owned[4]->va+q->bindings[i].memory.offset;
 }
 if(!snapshot&&fetch)a.snapshot=target_va; /* in-place own-pixel fetch */
 uint8_t *m=(void *)(uintptr_t)owned[0]->cpu;
 /* Pre-frame preload: the preload block (command+N2R_BLOCK) carries the
  * DCD array instead of a chained job; the draw block's framebuffer points
  * at it. Read b->preframe once so both blocks agree. */
 unsigned frame_preload=!use_mrt&&q->load_action==1&&preframe==1;
 if(frame_preload)a.frame_shader_dcds=a.command+N2R_BLOCK+N2R_FRAME_DCDS;
 if(mrt_loaded){a.frame_shader_dcds=a.command+N2R_BLOCK+N2R_FRAME_DCDS;a.frame_shader_mask=mrt_loaded==2?3u:1u;}
 struct tegu_generic_draw_request qb=*q;
 if(use_mrt) {
  qb.target_token=mt.token[0];qb.target_offset=mt.offset[0];qb.target_length=mt.length[0];
  qb.target_format=mt.rt[0].format;qb.row_bytes=mt.rt[0].row_bytes;
  if(qb.row_bytes<qb.width*4u)qb.row_bytes=qb.width*4u; /* generic preflight only */
 }
 if(use_mrt) {
  struct n2r_render_targets rts={.version=mt.version,.count=mt.count,
   .targets=mt.rt,.blends=mt.blends,
   .sample_counts=mt.version==N2MB_RENDER_TARGETS_MSAA_VERSION?mt.samples:NULL,
   .resolves=mt.version==N2MB_RENDER_TARGETS_MSAA_VERSION&&mt.has_resolves?mt.resolve:NULL};
  if(mt.count>2)rts.sample_locations=a.command+12288u;
  /* A true-MRT job never chains a preload job (its LOADs are pre-frame
   * DCDs): its tiler job is always index 1, whatever q->load_action says
   * (it used to take index 2 with dependency 1 on a nonexistent job when
   * the request said Load with Clear/DontCare targets). */
  e=n2r_build_v4(m,s,&qb,&a,tr,opts,&rts,1,0);
 } else e=tr?n2r_build_v3(m,s,q,&a,tr,opts,q->load_action==1&&!frame_preload?2:1,0):
           n2r_build(m,s,q,&a,q->load_action==1&&!frame_preload?2:1,0);
 if(e){e=BAD;N2MB_FAIL_AT();}
 req.heads[0]=a.command+N2R_JOB;req.heads[1]=a.command+N2R_FRAGMENT;
 if(mrt_loaded){
  e=cached_code(b,n2_frame_preload_shader.fragment_code,n2_frame_preload_shader.fragment_bytes,pin,&code[3]);if(e==1){owned[6]=code[3];e=0;}if(e)N2MB_FAIL_AT();
  uint32_t mask=0;struct n2r_addresses base=a;
  if(mrt_frame_preload(m+N2R_BLOCK,a.command+N2R_BLOCK,&mt,code[3]->va,&base,&mask)||mask!=a.frame_shader_mask){e=BAD;N2MB_FAIL_AT();}
 } else if(frame_preload){
  const struct tegu_generic_draw_shader *fps=frame_preload_shader(q->target_format);
  e=cached_code(b,fps->fragment_code,fps->fragment_bytes,pin,&code[3]);if(e==1){owned[6]=code[3];e=0;}if(e)N2MB_FAIL_AT();
  struct tegu_generic_draw_request pre={.target_format=preload_format(q->target_format),.width=q->width,.height=q->height,.row_bytes=q->row_bytes,.load_action=1,.vertex_count=3,.scissor={0,0,q->width,q->height},.binding_count=1};
  pre.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=snapshot?owned[4]->handle:q->target_token,.offset=q->target_offset,.bytes=target_bytes},.width=q->width,.height=q->height,.row_bytes=q->row_bytes,.format=preload_format(q->target_format)};
  struct n2r_addresses pa=a;pa.command=a.command+N2R_BLOCK;pa.vertex_code=0;pa.fragment_code=code[3]->va;pa.binding[0]=snapshot?a.snapshot:target_va;pa.frame_shader_dcds=0;
  e=n2r_build_frame_preload(m+N2R_BLOCK,fps,&pre,&pa);if(e){e=BAD;N2MB_FAIL_AT();}
 } else if(!use_mrt&&q->load_action==1){
  const struct tegu_generic_draw_shader *ps=preload_shader(q->target_format);
  e=cached_code(b,ps->vertex_code,ps->vertex_bytes,pin,&code[2]);if(e==1){owned[5]=code[2];e=0;}if(e)N2MB_FAIL_AT();
  e=cached_code(b,ps->fragment_code,ps->fragment_bytes,pin,&code[3]);if(e==1){owned[6]=code[3];e=0;}if(e)N2MB_FAIL_AT();
  struct tegu_generic_draw_request pre={.target_format=preload_format(q->target_format),.width=q->width,.height=q->height,.row_bytes=q->row_bytes,.load_action=1,.vertex_count=3,.scissor={0,0,q->width,q->height},.binding_count=1};
  pre.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=snapshot?owned[4]->handle:q->target_token,.offset=q->target_offset,.bytes=target_bytes},.width=q->width,.height=q->height,.row_bytes=q->row_bytes,.format=preload_format(q->target_format)};
  struct n2r_addresses pa=a;pa.command=a.command+N2R_BLOCK;pa.vertex_code=code[2]->va;pa.fragment_code=code[3]->va;pa.binding[0]=snapshot?a.snapshot:target_va;
  e=n2r_build(m+N2R_BLOCK,ps,&pre,&pa,1,req.heads[0]);if(e){e=BAD;N2MB_FAIL_AT();}
  req.heads[0]=pa.command+N2R_JOB;
 }
 for(unsigned i=0;i<7;i++)if(owned[i]){e=add_ref(&req,owned[i],i<=1);if(e)N2MB_FAIL_AT();}
 for(unsigned i=0;i<4;i++)if(code[i]){e=add_ref(&req,code[i],0);if(e)N2MB_FAIL_AT();} /* add_ref dedups */
 if((e=add_ref(&req,heap_bo,1)))N2MB_FAIL_AT();
 geometry_before(b,b->heap,a.heap_bytes);
 uint64_t job=0;
 {/* The vertex stage reads the vertex/index buffers and the bindings the
   * encoder did not mark fragment-only. */
  unsigned hazard=0;
  if(b->chain.frames) {
   hazard=(q->vertex.token&&chain_written(b,q->vertex.token))||(q->index.token&&chain_written(b,q->index.token));
   for(unsigned i=0;i<q->binding_count;i++)if(q->bindings[i].memory.token&&chain_written(b,q->bindings[i].memory.token)&&
      (!(q->bindings[i].reserved&N2MB_BINDING_STAGED)||(q->bindings[i].reserved&N2MB_BINDING_VERTEX)))hazard=1;
  }
  int taken=chain_take(b,&req,owned[0],hazard);
  if(taken<0){e=taken;N2MB_FAIL_AT();}
  if(taken)pooled_job=UINT64_MAX; /* stamped with the chain's job id */
  else{e=jm_submit(b,&req,sync,&job);pooled_job=job;if(e)N2MB_FAIL_AT();}
 }
 tile_stat(b,m);
 geometry_trace(b,b->heap,m,&a);
 render_trace(b,m,&a,q,s,q->load_action==1&&!frame_preload);
 if(d)memcpy(d->block,m,sizeof(d->block));
 out[0]=out[1]=job;*count=2;
done:
 if(tls){int r=drop(b,tls);if(!e)e=r;}
 if(pooled){int r=cmdpool_put(b,pooled,pooled_job);if(!e)e=r;owned[0]=NULL;}
 for(unsigned i=0;i<8;i++)if(owned[i]){int r=drop(b,owned[i]);if(!e)e=r;}
 b->code_protect=b->chain.frames?b->chain.code_clock:UINT64_MAX;
 return e;
}
static void tiler_matrix(struct bridge *b);
/* True-MRT batch members: the same single-sampled, resolve-free target set
 * (tokens, spans, formats, layout) as draw 0; members after draw 0 continue
 * every target (Load). Per-draw write masks and blends may differ (each
 * block carries its own BLEND array and render_target_mask). */
static int batch_targets_compatible(const struct n2mb_render_targets *t0,const struct n2mb_render_targets *t,
 const struct tegu_generic_draw_request *q,unsigned k){
 if(!t0||!t||!t->targets||t->count!=t0->count||!t->count||t->count>N2MB_MAX_RENDER_TARGETS)return 0;
 if(t->version!=N2MB_RENDER_TARGETS_VERSION&&t->version!=N2MB_RENDER_TARGETS_BLEND_VERSION&&
    t->version!=N2MB_RENDER_TARGETS_MSAA_VERSION)return 0;
 /* MSAA: one job per pass segment (Metal discards the samples at the end
  * of the pass; they are never loaded), so members repeat draw 0's sample
  * counts and resolve targets exactly; draw 0 must Clear/DontCare. */
 unsigned v3=t->version==N2MB_RENDER_TARGETS_MSAA_VERSION,v30=t0->version==N2MB_RENDER_TARGETS_MSAA_VERSION;
 if(v3!=v30||(v3&&(!t->sample_counts||!t0->sample_counts||!t->resolves!=!t0->resolves)))return 0;
 if(v3)for(uint32_t i=0;i<t->count;i++){
  if(t->sample_counts[i]!=t0->sample_counts[i])return 0;
  if(t->resolves){
   const struct n2mb_resolve_target *a=&t0->resolves[i],*b=&t->resolves[i];
   if(a->token!=b->token||a->offset!=b->offset||a->length!=b->length||a->format!=b->format||
      a->row_bytes!=b->row_bytes||a->width!=b->width||a->height!=b->height||a->reserved!=b->reserved)return 0;
  }
 }
 for(uint32_t i=0;i<t->count;i++){
  const struct n2mb_render_target *a=&t0->targets[i],*b=&t->targets[i];
  if(a->token!=b->token||a->offset!=b->offset||a->length!=b->length||a->format!=b->format||
     a->row_bytes!=b->row_bytes||a->width!=b->width||a->height!=b->height)return 0;
  /* A member continues the frame (Load). A discarded target (store DontCare)
   * whose Metal load is DontCare may also carry load 0: any tile content is
   * a valid DontCare start. The frame stores per draw 0's DISCARD flag, so a
   * member that stores target i cannot join a frame that discards it. */
  if(k&&(a->reserved&N2MB_TARGET_DISCARD)&&!(b->reserved&N2MB_TARGET_DISCARD))return 0;
  if(k&&b->load_action!=1&&!(b->load_action==0&&(b->reserved&N2MB_TARGET_DISCARD)))return 0;
  for(unsigned j=0;j<q->binding_count;j++)if(q->bindings[j].memory.token&&q->bindings[j].memory.token==b->token)return 0;
 }
 for(unsigned j=0;j<q->binding_count;j++)if(q->bindings[j].kind==TEGU_GENERIC_DRAW_ATTACHMENT)return 0;
 return 1;
}
static int batch_compatible(const struct tegu_generic_draw_request *q0,const struct tegu_generic_draw_request *q,unsigned k){
 if(q->target_token!=q0->target_token||q->target_offset!=q0->target_offset||q->target_length!=q0->target_length||
    q->target_format!=q0->target_format||q->width!=q0->width||q->height!=q0->height||q->row_bytes!=q0->row_bytes)return 0;
 if(k&&q->load_action!=1)return 0;
 if(q->target_format==10)return 0; /* v3 R8 preflight differs; keep per-draw */
 for(unsigned i=0;i<q->binding_count;i++){
  unsigned kind=q->bindings[i].kind;
  /* A memory fetch reads the target as it was before this fragment job,
   * missing earlier draws of the batch. A tile fetch (LD_TILE, validated by
   * n2r: TILE_FETCH shader, no memory) reads the tile buffer, which holds the
   * preload/clear plus every earlier draw of the job, as Metal programmable
   * blending requires; it batches. */
  if(kind==TEGU_GENERIC_DRAW_ATTACHMENT)return 0;
  if(q->bindings[i].memory.token&&q->bindings[i].memory.token==q->target_token)return 0;
 }
 return 1;
}
#define N2MB_BATCH_FAIL() do{b->bad_site=__LINE__;goto done;}while(0)
/* Longest prefix whose buffers fit one submission (32 handles): target, heap,
 * command block, shader code (<=2 per draw + preload pair) and every
 * vertex/index/binding buffer, counted exactly as add_ref will; at most the
 * runtime cap, and within the summed geometry budget of the shared heap. */
static unsigned batch_prefix_fits(struct bridge *b,const struct n2mb_batch_draw *dr,const struct n2mb_render_targets *const *tg,unsigned count){
 uint64_t seen[32],geometry=0;unsigned n=0,fit=0;
 if(count>b->batch_cap)count=b->batch_cap;
 #define BATCH_SEEN(tok) do{uint64_t t_=(tok);if(t_){unsigned i_=0;while(i_<n&&seen[i_]!=t_)i_++;if(i_==n){if(n==32)return fit;seen[n++]=t_;}}}while(0)
 BATCH_SEEN(dr[0].request->target_token);BATCH_SEEN(~0ull);BATCH_SEEN(~1ull); /* target, heap, command */
 if(tg&&tg[0])for(uint32_t i=0;i<tg[0]->count&&i<N2MB_MAX_RENDER_TARGETS;i++)BATCH_SEEN(tg[0]->targets[i].token);
 BATCH_SEEN(~2ull);BATCH_SEEN(~3ull); /* preload VS/FS (worst case) */
 for(unsigned k=0;k<count;k++){
  const struct tegu_generic_draw_request *q=dr[k].request;
  BATCH_SEEN(~(4ull+2*(uint64_t)(uintptr_t)dr[k].shader->vertex_code));BATCH_SEEN(~(5ull+2*(uint64_t)(uintptr_t)dr[k].shader->fragment_code));
  BATCH_SEEN(q->vertex.token);BATCH_SEEN(q->index.token);
  for(unsigned i=0;i<q->binding_count;i++)BATCH_SEEN(q->bindings[i].memory.token);
  geometry+=(uint64_t)(16u+dr[k].shader->varying_bytes)*q->vertex_count;
  if(geometry>N2MB_BATCH_GEOMETRY)return fit;
  fit=k+1;
 }
 #undef BATCH_SEEN
 return fit;
}
static int draw_batch_locked(struct bridge *b,const struct n2mb_batch_draw *dr,const struct n2mb_render_targets *const *tg,
 unsigned count,uint64_t *out,unsigned *count_out){
 struct bo *cmd=NULL,*uncached[2*N2MB_BATCH_CAPACITY+2]={0};unsigned nuncached=0;uint64_t pooled_job=0;
 struct n2c_mrt *mts=NULL; /* true-MRT: per-draw prepared target sets */
 b->code_protect=b->chain.frames?b->chain.code_clock:b->code_clock;
 int e=BAD;if(b->broken){e=NOTREADY;N2MB_BATCH_FAIL();}
 async_probe(b);
 const struct tegu_generic_draw_request *q0=dr[0].request;
 struct nord2_gpu_submit_request req={.version=2,.kind=2};
 /* True-MRT batch: draw 0's target set owns the frame; LOADs of it are
  * per-target pre-frame DCDs in the blocks after the draws. */
 unsigned preframe=b->preframe,use_mrt=tg&&tg[0],mrt_loaded=0;
 uint64_t target_va=0,target_bytes=(uint64_t)q0->row_bytes*q0->height;
 if(use_mrt){
  if(!(mts=calloc(count,sizeof(*mts))))N2MB_BATCH_FAIL();
  if(n2c_mrt_prepare(b,q0,tg[0],&mts[0],&req,preframe==1)||!mts[0].count)N2MB_BATCH_FAIL();
  for(uint32_t i=0;i<mts[0].count;i++)mrt_loaded+=mts[0].rt[i].load_action==1;
  target_va=mts[0].rt[0].address;
 } else {
  struct bo *target=lookup(b,q0->target_token);
  if(!target||target->internal||q0->target_length>target->size||q0->target_offset>q0->target_length||
     target_bytes>q0->target_length-q0->target_offset||add_ref(&req,target,1))N2MB_BATCH_FAIL();
  target_va=target->va+q0->target_offset;
 }
 if(!b->heap&&(e=heap_alloc(b)))N2MB_BATCH_FAIL();
 if(b->tiler.heap_bytes>b->heap->size){e=BAD;N2MB_BATCH_FAIL();}
 uint64_t heap_va=0;struct bo *heap_bo=chain_heap(b,!b->trace_batches,&heap_va); /* chained: the frame's slice */
 unsigned preload=!use_mrt&&q0->load_action==1,blocks=count+(mrt_loaded==2?2:1);
 /* Pre-frame preload: block `count` holds the DCD array, not a job. */
 unsigned frame_preload=preload&&preframe==1;
 e=cmdpool_get(b,(uint64_t)blocks*N2R_BLOCK,&cmd);if(e)N2MB_BATCH_FAIL();
 uint8_t *m=(void *)(uintptr_t)cmd->cpu;uint64_t frame=cmd->va;
 /* Execution order: [preload] draw0 draw1 ... ; indices ascend along it and
  * each job depends on its predecessor; draw0's block owns the frame. */
 unsigned base=preload&&!frame_preload?2:1;
 e=BAD;
 /* Fragment-job tile bound: union of the chained draws' extents (Mesa
  * panfrost_batch_union_scissor); owned by draw0's frame block. */
 uint32_t extent[4]={0,0,0,0};unsigned extent_valid=0;
 for(unsigned k=0;b->extent&&k<count;k++){
  uint32_t box[4];if(!n2r_draw_extent(dr[k].request,box))continue;
  if(!extent_valid){memcpy(extent,box,sizeof(extent));extent_valid=1;continue;}
  if(box[0]<extent[0])extent[0]=box[0];if(box[1]<extent[1])extent[1]=box[1];
  if(box[2]>extent[2])extent[2]=box[2];if(box[3]>extent[3])extent[3]=box[3];
 }
 for(unsigned k=count;k-->0;){
  const struct tegu_generic_draw_request *q=dr[k].request;
  struct n2r_addresses a={.target=target_va,.heap_bytes=2u<<20};
  a.vertex=resolve(b,&q->vertex,&req);a.index=resolve(b,&q->index,&req);
  if(a.vertex==UINT64_MAX||a.index==UINT64_MAX){e=BAD;N2MB_BATCH_FAIL();}
  for(unsigned i=0;i<q->binding_count;i++){
   a.binding[i]=resolve(b,&q->bindings[i].memory,&req);
   if(a.binding[i]==UINT64_MAX){e=BAD;N2MB_BATCH_FAIL();}
  }
  a.heap_bytes=b->tiler.heap_bytes;a.heap_chunk_enum1=b->tiler.chunk_enum1;
  a.hierarchy_mask=b->tiler.mask;a.no_secondary=b->tiler.no_secondary;
  struct bo *vs=NULL,*fs=NULL;
  e=cached_code(b,dr[k].shader->vertex_code,dr[k].shader->vertex_bytes,b->code_protect,&vs);
  if(e==1){uncached[nuncached++]=vs;e=0;}if(e)N2MB_BATCH_FAIL();
  e=cached_code(b,dr[k].shader->fragment_code,dr[k].shader->fragment_bytes,b->code_protect,&fs);
  if(e==1){uncached[nuncached++]=fs;e=0;}if(e)N2MB_BATCH_FAIL();
  if(add_ref(&req,vs,0)||add_ref(&req,fs,0)){e=BAD;N2MB_BATCH_FAIL();}
  a.command=frame+(uint64_t)k*N2R_BLOCK;a.frame=frame;a.heap=heap_va;
  a.vertex_code=vs->va;a.fragment_code=fs->va;
  if(!k&&extent_valid){a.extent_valid=1;memcpy(a.extent,extent,sizeof(extent));}
  if(frame_preload&&!k)a.frame_shader_dcds=frame+(uint64_t)count*N2R_BLOCK+N2R_FRAME_DCDS;
  if(mrt_loaded&&!k){a.frame_shader_dcds=frame+(uint64_t)count*N2R_BLOCK+N2R_FRAME_DCDS;a.frame_shader_mask=mrt_loaded==2?3u:1u;}
  uint64_t next=k+1<count?frame+(uint64_t)(k+1)*N2R_BLOCK+N2R_JOB:0;
  uint16_t index=(uint16_t)(base+k);
  if(use_mrt){
   if(k&&(n2c_mrt_prepare(b,q,tg[k],&mts[k],&req,2)||mts[k].count!=mts[0].count)){e=BAD;N2MB_BATCH_FAIL();}
   const struct n2c_mrt *mt=&mts[k];
   struct n2r_render_targets rts={.version=mt->version,.count=mt->count,.targets=mt->rt,.blends=mt->blends,
    .sample_counts=mt->version==N2MB_RENDER_TARGETS_MSAA_VERSION?mt->samples:NULL,
    .resolves=mt->version==N2MB_RENDER_TARGETS_MSAA_VERSION&&mt->has_resolves?mt->resolve:NULL};
   if(mt->count>2)rts.sample_locations=a.command+12288u;
   struct tegu_generic_draw_request qb=*q;
   qb.target_token=mt->token[0];qb.target_offset=mt->offset[0];qb.target_length=mt->length[0];
   qb.target_format=mt->rt[0].format;qb.row_bytes=mt->rt[0].row_bytes;
   if(qb.row_bytes<qb.width*4u)qb.row_bytes=qb.width*4u; /* generic preflight only */
   a.target=mt->rt[0].address;
   e=n2r_build_v4(m+(size_t)k*N2R_BLOCK,dr[k].shader,&qb,&a,dr[k].resources,dr[k].options,&rts,index,next);
  } else
  e=dr[k].resources?n2r_build_v3(m+(size_t)k*N2R_BLOCK,dr[k].shader,q,&a,dr[k].resources,dr[k].options,index,next):
    n2r_build(m+(size_t)k*N2R_BLOCK,dr[k].shader,q,&a,index,next);
  if(e){e=BAD;N2MB_BATCH_FAIL();}
 }
 req.heads[0]=frame+N2R_JOB;req.heads[1]=frame+N2R_FRAGMENT;
 if(mrt_loaded){
  struct bo *pf=NULL;
  e=cached_code(b,n2_frame_preload_shader.fragment_code,n2_frame_preload_shader.fragment_bytes,b->code_protect,&pf);
  if(e==1){uncached[nuncached++]=pf;e=0;}if(e)N2MB_BATCH_FAIL();
  if(add_ref(&req,pf,0)){e=BAD;N2MB_BATCH_FAIL();}
  struct n2r_addresses pa={.target=target_va,.heap_bytes=b->tiler.heap_bytes,.heap_chunk_enum1=b->tiler.chunk_enum1,
   .hierarchy_mask=b->tiler.mask,.no_secondary=b->tiler.no_secondary};
  pa.frame=frame;pa.heap=heap_va;
  uint32_t mask=0;
  if(mrt_frame_preload(m+(size_t)count*N2R_BLOCK,frame+(uint64_t)count*N2R_BLOCK,&mts[0],pf->va,&pa,&mask)||
     mask!=(mrt_loaded==2?3u:1u)){e=BAD;N2MB_BATCH_FAIL();}
 } else if(frame_preload){
  struct bo *pf=NULL;
  const struct tegu_generic_draw_shader *fps=frame_preload_shader(q0->target_format);
  e=cached_code(b,fps->fragment_code,fps->fragment_bytes,b->code_protect,&pf);
  if(e==1){uncached[nuncached++]=pf;e=0;}if(e)N2MB_BATCH_FAIL();
  if(add_ref(&req,pf,0)){e=BAD;N2MB_BATCH_FAIL();}
  struct tegu_generic_draw_request pre={.target_format=preload_format(q0->target_format),.width=q0->width,.height=q0->height,.row_bytes=q0->row_bytes,.load_action=1,.vertex_count=3,.scissor={0,0,q0->width,q0->height},.binding_count=1};
  pre.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=q0->target_token,.offset=q0->target_offset,.bytes=target_bytes},.width=q0->width,.height=q0->height,.row_bytes=q0->row_bytes,.format=preload_format(q0->target_format)};
  struct n2r_addresses pa={.target=target_va,.heap_bytes=b->tiler.heap_bytes,.heap_chunk_enum1=b->tiler.chunk_enum1,
   .hierarchy_mask=b->tiler.mask,.no_secondary=b->tiler.no_secondary};
  pa.command=frame+(uint64_t)count*N2R_BLOCK;pa.frame=frame;pa.heap=heap_va;
  pa.fragment_code=pf->va;pa.binding[0]=target_va;
  e=n2r_build_frame_preload(m+(size_t)count*N2R_BLOCK,fps,&pre,&pa);if(e){e=BAD;N2MB_BATCH_FAIL();}
 } else if(preload){
  struct bo *pv=NULL,*pf=NULL;
  const struct tegu_generic_draw_shader *ps=preload_shader(q0->target_format);
  e=cached_code(b,ps->vertex_code,ps->vertex_bytes,b->code_protect,&pv);
  if(e==1){uncached[nuncached++]=pv;e=0;}if(e)N2MB_BATCH_FAIL();
  e=cached_code(b,ps->fragment_code,ps->fragment_bytes,b->code_protect,&pf);
  if(e==1){uncached[nuncached++]=pf;e=0;}if(e)N2MB_BATCH_FAIL();
  if(add_ref(&req,pv,0)||add_ref(&req,pf,0)){e=BAD;N2MB_BATCH_FAIL();}
  struct tegu_generic_draw_request pre={.target_format=preload_format(q0->target_format),.width=q0->width,.height=q0->height,.row_bytes=q0->row_bytes,.load_action=1,.vertex_count=3,.scissor={0,0,q0->width,q0->height},.binding_count=1};
  pre.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=q0->target_token,.offset=q0->target_offset,.bytes=target_bytes},.width=q0->width,.height=q0->height,.row_bytes=q0->row_bytes,.format=preload_format(q0->target_format)};
  struct n2r_addresses pa={.target=target_va,.heap_bytes=b->tiler.heap_bytes,.heap_chunk_enum1=b->tiler.chunk_enum1,
   .hierarchy_mask=b->tiler.mask,.no_secondary=b->tiler.no_secondary};
  pa.command=frame+(uint64_t)count*N2R_BLOCK;pa.frame=frame;pa.heap=heap_va;
  pa.vertex_code=pv->va;pa.fragment_code=pf->va;pa.binding[0]=target_va;
  e=n2r_build(m+(size_t)count*N2R_BLOCK,ps,&pre,&pa,1,req.heads[0]);if(e){e=BAD;N2MB_BATCH_FAIL();}
  req.heads[0]=pa.command+N2R_JOB;
 }
 if((e=add_ref(&req,cmd,1)))N2MB_BATCH_FAIL();
 if((e=add_ref(&req,heap_bo,1)))N2MB_BATCH_FAIL();
 /* NORD2_METAL_TRACE: the first 16 batches complete in order and report
  * their shape and tiler-heap use (owner block TILER_HEAP bottom), so a
  * probe can see the draws-per-job cap and the heap it needs on hardware. */
 unsigned traced=b->trace_batches!=0;
 uint64_t job=0;
 {unsigned hazard=0;
  for(unsigned k=0;b->chain.frames&&k<count;k++){const struct tegu_generic_draw_request *q=dr[k].request;
   if((q->vertex.token&&chain_written(b,q->vertex.token))||(q->index.token&&chain_written(b,q->index.token)))hazard=1;
   for(unsigned i=0;i<q->binding_count;i++)if(q->bindings[i].memory.token&&chain_written(b,q->bindings[i].memory.token)&&
      (!(q->bindings[i].reserved&N2MB_BINDING_STAGED)||(q->bindings[i].reserved&N2MB_BINDING_VERTEX)))hazard=1;}
  int taken=chain_take(b,&req,cmd,hazard);
  if(taken<0){e=taken;N2MB_BATCH_FAIL();}
  if(taken)pooled_job=UINT64_MAX; /* stamped with the chain's job id */
  else{e=jm_submit(b,&req,traced,&job);pooled_job=job;if(e)N2MB_BATCH_FAIL();}}
 tile_stat(b,m);
 if(traced){
  b->trace_batches--;uint64_t bottom=0;memcpy(&bottom,m+768+16,8);
  char line[160];int len=snprintf(line,sizeof(line),"NORD2_METAL batch draws=%u preload=%u preframe=%u heap-used=%llu heap=%u\n",
   count,preload,frame_preload,(unsigned long long)(bottom>=b->heap->va?bottom-b->heap->va:0),b->tiler.heap_bytes);
  if(len>0&&(size_t)len<sizeof(line)){unsigned no=0;size_t bytes=0;
   (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
 }
 out[0]=out[1]=job;*count_out=2;
done:
 for(unsigned i=0;i<nuncached;i++){int r=drop(b,uncached[i]);if(!e)e=r;}
 if(cmd){int r=cmdpool_put(b,cmd,pooled_job);if(!e)e=r;}
 free(mts);
 b->code_protect=b->chain.frames?b->chain.code_clock:UINT64_MAX;
 return e;
}
int n2mb_draw_batch(uintptr_t value,const struct n2mb_batch_draw *dr,unsigned count,uint64_t *out,unsigned *count_out){
 return n2mb_draw_batch_mrt(value,dr,NULL,count,out,count_out);
}
int n2mb_draw_batch_mrt(uintptr_t value,const struct n2mb_batch_draw *dr,const struct n2mb_render_targets *const *tg,
 unsigned count,uint64_t *out,unsigned *count_out){
 struct bridge *b=(void *)value;
 if(!b||!dr||!count||count>N2MB_BATCH_CAPACITY||!out||!count_out||*count_out<2)return BAD;
 /* A prefix that fits the 32-handle request is submitted; out[1] is the
  * job id and *count_out stays 2. The caller reads n2mb_batch_last_count. */
 for(unsigned k=0;k<count;k++){
  const struct n2mb_batch_draw *x=&dr[k];
  if(!x->shader||!x->request||(x->options&&!x->resources)){b->bad_site=-__LINE__;return BAD;}
  const struct n2mb_render_targets *xt=tg?tg[k]:NULL;
  if(tg&&(tg[0]||xt)){
   /* True-MRT members: v2+ sidecar. LD_TILE is fine: a MULTI_TARGET tile
    * variant reads each attachment's own tile-buffer target (rt=j). */
   if(!x->resources||x->request->width!=dr[0].request->width||
      x->request->height!=dr[0].request->height||!batch_targets_compatible(tg[0],xt,x->request,k)){b->bad_site=-__LINE__;return BAD;}
  } else if(!batch_compatible(dr[0].request,x->request,k)){b->bad_site=-__LINE__;return BAD;}
  if(x->options&&(x->options->thread_storage_bytes||x->options->thread_storage_reserved)){b->bad_site=-__LINE__;return BAD;}
  /* Tile-buffer depth/stencil: draw 0 starts it (Clear/DontCare), every
   * member continues the same pass state (load 1, same format/clears). */
  {const struct n2r_depth_stencil *z0=dr[0].options&&(dr[0].options->flags&N2R_DRAW_DEPTH_STENCIL)?dr[0].options->zs:NULL;
   const struct n2r_depth_stencil *zk=x->options&&(x->options->flags&N2R_DRAW_DEPTH_STENCIL)?x->options->zs:NULL;
   if((x->options&&(x->options->flags&N2R_DRAW_DEPTH_STENCIL)&&!zk)||(!z0)!=(!zk)||
      (z0&&(k?zk->load!=1:z0->load==1))||
      (z0&&k&&(zk->format!=z0->format||zk->clear_depth!=z0->clear_depth||zk->clear_stencil!=z0->clear_stencil))){b->bad_site=-__LINE__;return BAD;}}
  /* The provoking vertex lives in the shared TILER_CONTEXT. */
  unsigned flat=x->options&&(x->options->flags&N2R_DRAW_FLAT_SHADING),flat0=dr[0].options&&(dr[0].options->flags&N2R_DRAW_FLAT_SHADING);
  if(flat!=flat0){b->bad_site=-__LINE__;return BAD;}
  /* MRT members are validated with their target set when built (still
   * before any submission). */
  int v=xt?0:x->options?n2r_validate_v3(x->shader,x->request,x->resources,x->options):
   x->resources?n2r_validate_v2(x->shader,x->request,x->resources):n2r_validate(x->shader,x->request);
  if(v){b->bad_site=-__LINE__;return BAD;}
 }
 unsigned fit=batch_prefix_fits(b,dr,tg,count);
 if(fit<2){b->bad_site=-__LINE__;return BAD;}
 b->ops.lock(b->ops.opaque);
 if(b->matrix==1&&!b->broken)tiler_matrix(b);
 int e=draw_batch_locked(b,dr,tg,fit,out,count_out);
 b->batch_last=e?0:fit;
 b->ops.unlock(b->ops.opaque);return e;
}
unsigned n2mb_gpu_only_state(uintptr_t value){struct bridge *b=(void *)value;return b?b->gpu_only_state:0;}
/* GPU virtual address and size of a client buffer (MTLBuffer.gpuAddress, argument buffers). Unknown and
 * bridge-internal BOs are refused; the VA is fixed for the BO's lifetime (kernel-assigned at allocation). */
int n2mb_buffer_va(uintptr_t value,uint32_t handle,uint64_t *va,uint64_t *size){
 struct bridge *b=(void *)value;if(!b||!va)return BAD;
 b->ops.lock(b->ops.opaque);struct bo *o=lookup(b,handle);
 int e=(o&&!o->internal&&o->va)?0:BAD;
 if(!e){*va=o->va;if(size)*size=o->size;}
 b->ops.unlock(b->ops.opaque);return e;
}
unsigned n2mb_batch_last_count(uintptr_t value){struct bridge *b=(void *)value;return b?b->batch_last:0;}
unsigned n2mb_batch_capacity(uintptr_t value,unsigned cap){
 struct bridge *b=(void *)value;if(!b)return 0;
 b->ops.lock(b->ops.opaque);unsigned old=b->batch_cap;
 if(cap>=2&&cap<=N2MB_BATCH_CAPACITY)b->batch_cap=cap;
 b->ops.unlock(b->ops.opaque);return old;
}
unsigned n2mb_async(uintptr_t value,unsigned mode){
 struct bridge *b=(void *)value;if(!b)return 0;
 b->ops.lock(b->ops.opaque);
 if(b->chain.frames)(void)chain_flush(b,CHAIN_FLUSH_OTHER);
 if(b->async==2)async_probe(b);
 unsigned old=b->async;
 if(mode==0){if(!wait_pending(b))b->async=0;}
 else if(mode==1&&!old){b->async=2;async_probe(b);}
 b->ops.unlock(b->ops.opaque);return old;
}
int n2mb_wait_pending(uintptr_t value,uint64_t *failed_job){
 struct bridge *b=(void *)value;if(!b)return BAD;
 b->ops.lock(b->ops.opaque);int e=wait_pending(b);
 if(failed_job)*failed_job=b->async_failed;
 b->ops.unlock(b->ops.opaque);return e;
}
/* Cross-command pipelining (frontend finish thread): wait for every job with
 * id <= seq WITHOUT holding the bridge lock, so another thread keeps
 * submitting later jobs meanwhile. Jobs of one context complete in order, so
 * this reaps exactly the jobs of the commands up to seq. A failure breaks the
 * bridge as wait_pending does. */
int n2mb_wait_seq(uintptr_t value,uint64_t seq,uint64_t *failed_job){
 struct bridge *b=(void *)value;if(!b)return BAD;
 b->ops.lock(b->ops.opaque);
 if(failed_job)*failed_job=0;
 if(b->chain.frames&&!b->broken)(void)chain_flush(b,CHAIN_FLUSH_OTHER);
 if(b->broken){if(failed_job)*failed_job=b->async_failed;b->ops.unlock(b->ops.opaque);return NOTREADY;}
 if(b->async!=1||!b->pending||!seq){b->ops.unlock(b->ops.opaque);return 0;}
 if(seq>b->pending)seq=b->pending;
 b->ops.unlock(b->ops.opaque);
 uint64_t out[3]={0};unsigned n=3;int e=0;
 for(unsigned tries=0;;tries++){
  uint64_t in[2]={seq,5000};n=3;out[0]=out[1]=out[2]=0;
  e=scalar(b,NORD2_GPU_SEL_WAIT_SEQ,in,2,out,&n);
  if(!e&&n==3&&out[0]==2&&tries<2)continue; /* timeout: nothing reaped */
  break;
 }
 b->ops.lock(b->ops.opaque);
 if(e||n!=3||out[0]!=4){b->broken=1;if(n==3)b->async_failed=out[2];if(failed_job)*failed_job=b->async_failed;e=e?e:FAILED;}
 else {
  if(seq>b->reaped)b->reaped=seq;
  if(b->pending<=seq)b->pending=0; /* nothing newer was submitted meanwhile */
 }
 b->ops.unlock(b->ops.opaque);return e;
}
void n2mb_tile_stats(uintptr_t value,uint64_t out[3]){
 struct bridge *b=(void *)value;if(!out)return;out[0]=out[1]=out[2]=0;if(!b)return;
 b->ops.lock(b->ops.opaque);memcpy(out,b->stat_tiles,sizeof(b->stat_tiles));b->ops.unlock(b->ops.opaque);
}
unsigned n2mb_extent(uintptr_t value,unsigned mode){
 struct bridge *b=(void *)value;if(!b)return 0;
 b->ops.lock(b->ops.opaque);unsigned old=b->extent;if(mode<=1)b->extent=mode;b->ops.unlock(b->ops.opaque);
 return old;
}
unsigned n2mb_preframe(uintptr_t value,unsigned mode){
 struct bridge *b=(void *)value;if(!b)return 0;
 b->ops.lock(b->ops.opaque);unsigned old=b->preframe;if(mode<=1)b->preframe=mode;b->ops.unlock(b->ops.opaque);
 return old;
}
/* Where the last refused draw stopped: bridge source line (negative for an
 * entry validation) and the n2r refusal line (100000+ = resources.inc). */
/* Shader code BOs currently retained by the per-bridge code cache. */
/* Live GPU memory this client holds through the bridge: private allocations
 * (client buffers/textures plus bridge-internal blocks, heap, code cache)
 * and imported IOSurfaces, in bytes, with the BO count. The kernel charges
 * both against one per-context budget. */
void n2mb_memory_stats(uintptr_t value,uint64_t *private_bytes,uint64_t *imported_bytes,
 uint64_t *internal_bytes,uint64_t *code_bytes,unsigned *bos){
 struct bridge *b=(void *)value;uint64_t p=0,im=0,in=0,cb=0;unsigned n=0;
 if(b){b->ops.lock(b->ops.opaque);
  for(unsigned i=0;i<b->bo_hw;i++)if(b->bos[i].handle){n++;
   if(b->bos[i].external_cpu)im+=b->bos[i].size;else{p+=b->bos[i].size;if(b->bos[i].internal)in+=b->bos[i].size;}}
  for(unsigned i=0;i<16;i++)if(b->code[i])cb+=b->code[i]->size;
  b->ops.unlock(b->ops.opaque);}
 if(private_bytes)*private_bytes=p;if(imported_bytes)*imported_bytes=im;if(internal_bytes)*internal_bytes=in;
 if(code_bytes)*code_bytes=cb;if(bos)*bos=n;
}
/* The largest client (non-internal, non-imported) allocations, descending,
 * and how many client allocations of >= 8 MiB / 1-8 MiB / < 1 MiB exist. */
unsigned n2mb_memory_top(uintptr_t value,uint64_t top[],unsigned room,unsigned classes[3]){
 struct bridge *b=(void *)value;unsigned n=0;
 if(classes)classes[0]=classes[1]=classes[2]=0;
 if(!b)return 0;
 b->ops.lock(b->ops.opaque);
 for(unsigned i=0;i<b->bo_hw;i++){const struct bo *o=&b->bos[i];
  if(!o->handle||o->internal||o->external_cpu)continue;
  if(classes)classes[o->size>=(8ull<<20)?0:o->size>=(1ull<<20)?1:2]++;
  if(!room)continue;
  unsigned at=n<room?n++:room;
  if(at==room){if(o->size<=top[room-1])continue;at=room-1;}
  while(at&&top[at-1]<o->size){top[at]=top[at-1];at--;}
  top[at]=o->size;
 }
 b->ops.unlock(b->ops.opaque);return n;
}
void n2mb_cmdpool_stats(uintptr_t value,uint64_t *hits,uint64_t *misses){struct bridge *b=(void *)value;
 if(hits)*hits=b?b->cmdpool_hits:0;if(misses)*misses=b?b->cmdpool_misses:0;}
unsigned n2mb_cmdpool_count(uintptr_t value){struct bridge *b=(void *)value;return b?b->cmdpool_n+(b->clear_slab!=NULL):0;}
unsigned n2mb_code_cache_count(uintptr_t value){
 struct bridge *b=(void *)value;unsigned n=0;if(b)for(unsigned i=0;i<16;i++)n+=b->code[i]!=NULL;return n;
}
int n2mb_last_failure(uintptr_t value,int *n2r_line){
 struct bridge *b=(void *)value;if(n2r_line)*n2r_line=n2r_last_reject_line();return b?b->bad_site:0;
}
int n2mb_draw(uintptr_t value,const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q,uint64_t *out,unsigned *count){
 struct bridge *b=(void *)value;if(!b||!out||!count||*count<2)return BAD;
 if(n2r_validate(s,q)){b->bad_site=-__LINE__;return BAD;}
 b->ops.lock(b->ops.opaque);
 if(b->matrix==1&&!b->broken)tiler_matrix(b);
 int e=draw_locked(b,s,q,NULL,NULL,NULL,out,count,NULL);
 b->ops.unlock(b->ops.opaque);return e;
}

int n2mb_draw_v2(uintptr_t value,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,
 uint64_t *out,unsigned *count){
 struct bridge *b=(void *)value;
 if(!b||!out||!count||*count<2||!tr)return BAD;
 if(n2r_validate_v2(s,q,tr)){b->bad_site=-__LINE__;return BAD;}
 b->ops.lock(b->ops.opaque);
 if(b->matrix==1&&!b->broken)tiler_matrix(b);
 int e=draw_locked(b,s,q,tr,NULL,NULL,out,count,NULL);
 b->ops.unlock(b->ops.opaque);return e;
}
int n2mb_draw_v3(uintptr_t value,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,
 const struct n2r_draw_options *opts,uint64_t *out,unsigned *count){
 struct bridge *b=(void *)value;
 if(!b||!q||!out||!count||*count<2||!tr||!opts)return BAD;
 /* R8 has a one-byte row; the legacy generic preflight hardcodes four bytes.
  * The target-token path below uses n2r_build_v4 with the actual row bytes. */
 if(q&&q->target_format!=10&&n2r_validate_v3(s,q,tr,opts)){b->bad_site=-__LINE__;return BAD;}
 b->ops.lock(b->ops.opaque);
 if(b->matrix==1&&!b->broken)tiler_matrix(b);
 int e=draw_locked(b,s,q,tr,opts,NULL,out,count,NULL);
 b->ops.unlock(b->ops.opaque);return e;
}

int n2mb_draw_v4(uintptr_t value,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,
 const struct n2r_draw_options *opts,const struct n2mb_render_targets *mrt,
 uint64_t *out,unsigned *count){
 struct bridge *b=(void *)value;
 if(!b||!q||!out||!count||*count<2||!tr)return BAD;
 if(!mrt&&q&&q->target_format!=10&&n2r_validate_v3(s,q,tr,opts)){b->bad_site=-__LINE__;return BAD;}
 b->ops.lock(b->ops.opaque);
 if(b->matrix==1&&!b->broken)tiler_matrix(b);
 int e=draw_locked(b,s,q,tr,opts,mrt,out,count,NULL);
 b->ops.unlock(b->ops.opaque);return e;
}

/* A clear-only pass still performs real GPU work, using the same sampled
 * fullscreen shader as LOAD. The single texel is command-order source data. */
int n2mb_clear(uintptr_t value,const struct tegu_generic_draw_request *source,uint64_t *out,unsigned *count){
 struct bridge *b=(void *)value;if(!b)return BAD;
 if(!source||!source->width||!source->height||!n2r_bpp(source->target_format)){b->bad_site=-__LINE__;return BAD;}
 /* The clear draw samples a one-texel texture of the target format through
  * the LOAD shader; a raw target's texel is its own bits, copied through the
  * uint twin (preload_shader/preload_format). */
 if(!n2r_rt_load_exact(source->target_format)){b->bad_site=-__LINE__;return BAD;}
 struct bo *color=NULL;int e=0,slot=-1;uint64_t token=0,offset=0;
 b->ops.lock(b->ops.opaque);
 if(b->broken){b->ops.unlock(b->ops.opaque);return NOTREADY;}
 slot=clear_slot_get(b); /* pooled colour slot; else a fresh 64-byte BO as before */
 if(slot>=0){token=b->clear_slab->handle;offset=(uint64_t)slot*64;}
 else{e=alloc(b,64,0,0,&color);if(e)b->bad_site=__LINE__;else{e=map_bo(b,color);if(e)b->bad_site=__LINE__;}if(!e)token=color->handle;}
 if(!e){uint8_t pixel[16];clear_pixel(source,pixel);
  unsigned texel=n2r_bpp(source->target_format);memcpy((void *)(uintptr_t)(slot>=0?b->clear_slab->cpu+offset:color->cpu),pixel,texel>8?texel:8);}
 b->ops.unlock(b->ops.opaque);
 if(!e){
  struct tegu_generic_draw_request q=*source;
  q.viewport_valid=0;q.load_action=2;q.vertex_count=3;q.primitive=0;q.binding_count=1;
  q.scissor[0]=q.scissor[1]=0;q.scissor[2]=q.width;q.scissor[3]=q.height;
  /* A raw target's clear draw runs entirely in its uint twin (bit copy). */
  if(n2r_rt_raw_twin(q.target_format))q.target_format=n2r_rt_raw_twin(q.target_format);
  q.bindings[0]=(struct tegu_generic_draw_binding){.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=token,.offset=offset,.bytes=64},.width=1,.height=1,.row_bytes=64,.format=preload_format(q.target_format)};
  e=n2mb_draw(value,preload_shader(q.target_format),&q,out,count);
 }
 b->ops.lock(b->ops.opaque);
 /* The slot is reused after its job is reaped; never after an unknown outcome. */
 if(slot>=0)b->clear_slot_job[slot]=!e?out[0]:b->broken?UINT64_MAX:0;
 /* Chained (reply id 0): busy until the chain's job id is known. */
 if(slot>=0&&!e&&!out[0]&&b->chain.frames) {
  b->clear_slot_job[slot]=UINT64_MAX;
  if(b->chain.nslots<2*N2MB_CHAIN_MAX_FRAMES)b->chain.slots[b->chain.nslots++]=slot;
  else{(void)chain_flush(b,CHAIN_FLUSH_FULL);b->clear_slot_job[slot]=b->chain.last_job?b->chain.last_job:UINT64_MAX;}
 }
 if(color){int cleanup=drop(b,color);if(!e)e=cleanup;}b->ops.unlock(b->ops.opaque);return e;
}

/* ---- One-shot tiler differential (NORD2_METAL_TILER_MATRIX=1) ----------
 * Runs once, in this client's GPU context, before the first draw. Each variant
 * draws the internal fullscreen triangle into an owned 64x64 BGRA8 target with
 * RT clear = red and drawn colour = green, so clear success can never pass as
 * rasterisation. Only bridge-owned buffers are read; GPU pointers are never
 * followed. A passing descriptor-only variant is adopted for later draws. */
/* Preload FS with its LD_VAR/TEX prologue replaced by four IADD_IMM writing
 * (0,1,0,1); ATEST/BLEND/return tail byte-identical (Mesa asm/disasm checked). */
static const uint8_t n2_const_fs[128]={
 192,0,0,0,0,192,16,1,192,0,0,128,63,193,16,1,
 192,0,0,0,0,194,16,1,192,0,0,128,63,195,16,1,
 124,3,234,0,2,188,125,72,140,0,60,178,8,64,127,120,
 192,0,0,0,0,240,16,1,192,141,0,0,16,193,47,0};
static void log_line(struct bridge *b,char *line,int len,size_t cap){
 if(len<=0||(size_t)len+1>=cap||len>=239)return;
 line[len++]='\n';unsigned no=0;size_t bytes=0;
 (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);
}
struct tm_variant {const char *name;uint32_t heap_bytes,chunk_enum1,mask,no_secondary,const_fs,spacer;};
#define TM_MESA 0xffffffffu
static const struct tm_variant tm_variants[]={
 {"base",2u<<20,0,0,0,0,0},          /* production packet, persistent heap */
 {"chunk256k",2u<<20,1,0,0,0,0},     /* H-heap: 8 chunks in 2 MiB */
 {"heap16m",16u<<20,0,0,0,0,0},      /* H-heap: 8 x 2 MiB chunks */
 {"mesamask",2u<<20,0,TM_MESA,0,0,0},/* H-mask: pan_select_tiler_hierarchy_mask */
 {"mask01",2u<<20,0,1,0,0,0},        /* single 16 px level */
 {"heap16m-256k-mesa",16u<<20,1,TM_MESA,0,0,0},
 {"constfs",2u<<20,0,0,0,1,0},       /* FS without varying/texture reads */
 {"constfs-nosec",2u<<20,0,0,1,1,0}, /* H-varying: Mesa no-varyings IDVS */
 {"constfs-nosec-16m-256k-mesa",16u<<20,1,TM_MESA,1,1,0},
 {"spacer",2u<<20,0,0,0,0,1},        /* tiler ctx/heap desc at a new VA */
 {"base-repeat",2u<<20,0,0,0,0,0},   /* order/persistence control */
};
#define TM_COUNT (sizeof(tm_variants)/sizeof(tm_variants[0]))
static void tiler_matrix(struct bridge *b){
 b->matrix=2;char line[240];int len;
 if(wait_pending(b))return; /* reads pixels back; runs before the first draw */
 unsigned saved_draws=b->trace_draws;b->trace_draws=b->trace_geometry=0;
 struct nord2_gpu_status st={0};size_t ss=sizeof(st);unsigned n=0;
 if(b->ops.call(b->ops.opaque,NORD2_GPU_SEL_STATUS,NULL,0,NULL,0,NULL,&n,&st,&ss)||ss!=sizeof(st))memset(&st,0,sizeof(st));
 uint32_t mesa=n2r_mesa_hierarchy_mask(64,64,st.tiler_features,2u<<20),mesa16=n2r_mesa_hierarchy_mask(64,64,st.tiler_features,16u<<20);
 len=snprintf(line,sizeof(line),"NORD2_METAL tm id gpu=%08x tiler=%08x l2=%08x core=%08x mem=%08x mmu=%08x js=%08x mesa=%03x/%03x",
  st.gpu_id,st.tiler_features,st.l2_features,st.core_features,st.mem_features,st.mmu_features,st.js_present,mesa,mesa16);
 log_line(b,line,len,sizeof(line));
 struct bo *target=NULL,*texel=NULL;int e=alloc(b,16384,0,0,&target);
 if(!e)e=map_bo(b,target);if(!e)e=alloc(b,64,0,0,&texel);if(!e)e=map_bo(b,texel);
 /* The matrix inspects the heap with the CPU: its own mapped heap, dropped
  * at the end so the persistent heap is allocated GPU-only afterwards. */
 if(!e&&b->heap){(void)drop(b,b->heap);b->heap=NULL;}
 if(!e)e=alloc(b,HEAP_MATRIX,0,1,&b->heap);
 if(!e&&!b->heap->cpu)e=map_bo(b,b->heap);
 if(e){len=snprintf(line,sizeof(line),"NORD2_METAL tm setup err=%d",e);log_line(b,line,len,sizeof(line));goto out;}
 static const uint8_t green[4]={0,255,0,255}; /* BGRA8 bytes */
 memset((void *)(uintptr_t)texel->cpu,0,64);memcpy((void *)(uintptr_t)texel->cpu,green,4);
 struct tegu_generic_draw_shader cs=n2_preload_shader;cs.fragment_code=n2_const_fs;cs.fragment_bytes=sizeof(n2_const_fs);
 unsigned adopt=TM_COUNT;
 for(unsigned v=0;v<TM_COUNT&&!b->broken;v++){
  const struct tm_variant *t=&tm_variants[v];
  uint32_t mask=t->mask==TM_MESA?(t->heap_bytes>(2u<<20)?mesa16:mesa):t->mask;
  if(t->mask==TM_MESA&&!mask){len=snprintf(line,sizeof(line),"NORD2_METAL tm v=%u %s skipped: no TILER_FEATURES levels",v,t->name);log_line(b,line,len,sizeof(line));continue;}
  b->tiler.heap_bytes=t->heap_bytes;b->tiler.chunk_enum1=t->chunk_enum1;b->tiler.mask=mask;b->tiler.no_secondary=t->no_secondary;
  memset((void *)(uintptr_t)target->cpu,0x55,16384);
  memset((void *)(uintptr_t)b->heap->cpu,0,(size_t)b->heap->size);
  struct tegu_generic_draw_request q={.target_format=80,.width=64,.height=64,.row_bytes=256,.load_action=2,.vertex_count=3,
   .scissor={0,0,64,64},.target_token=target->handle,.target_length=16384,.binding_count=1};
  static const double red[4]={1,0,0,1};(void)n2r_clear(80,red,q.clear_words); /* stored BGRA8 = 0xffff0000 */
  q.bindings[0]=(struct tegu_generic_draw_binding){.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=texel->handle,.bytes=64},
   .width=1,.height=1,.row_bytes=64,.format=80};
  struct diag d={.spacer=t->spacer};uint64_t job[2];unsigned jc=2;
  e=draw_locked(b,t->const_fs?&cs:&n2_preload_shader,&q,NULL,NULL,NULL,job,&jc,&d);
  if(e){len=snprintf(line,sizeof(line),"NORD2_METAL tm v=%u %s err=%d broken=%u",v,t->name,e,b->broken);log_line(b,line,len,sizeof(line));continue;}
  uint64_t args[2]={target->handle,0};n=0;e=scalar(b,NORD2_GPU_SEL_SYNC,args,2,NULL,&n);
  if(!e){args[0]=b->heap->handle;n=0;e=scalar(b,NORD2_GPU_SEL_SYNC,args,2,NULL,&n);}
  /* Never interpret stale CPU cache contents as rasterisation. Stop this
   * diagnostic context if either readback cannot be made coherent. */
  if(e){b->broken=1;len=snprintf(line,sizeof(line),"NORD2_METAL tm v=%u %s err=%d broken=%u",v,t->name,e,b->broken);log_line(b,line,len,sizeof(line));break;}
  const uint32_t *px=(const void *)(uintptr_t)target->cpu;unsigned greens=0,reds=0;
  for(unsigned i=0;i<4096;i++){greens+=px[i]==0xff00ff00u;reds+=px[i]==0xffff0000u;}
  const uint8_t *h=(const void *)(uintptr_t)b->heap->cpu;unsigned blocks=0,top=0;size_t first=0,last=0;
  for(size_t off=0;off+64<=t->heap_bytes;off+=64){unsigned any=0;for(unsigned k=0;k<64;k++)any|=h[off+k];
   if(!any)continue;if(!blocks)first=off;last=off;blocks++;if(off>=t->heap_bytes-65536)top++;}
  uint64_t poly,bottom,va0,va1;memcpy(&poly,d.block+512,8);memcpy(&bottom,d.block+768+16,8);
  memcpy(&va0,d.block+N2R_JOB+136,8);memcpy(&va1,d.block+N2R_JOB+144,8); /* DCD Vertex array words 2..5 */
  len=snprintf(line,sizeof(line),"NORD2_METAL tm v=%u %s heap=%uK chunk=%u mask=%03x sec=%u fs=%s green=%u red=%u px=%08x",
   v,t->name,t->heap_bytes>>10,t->chunk_enum1?t->chunk_enum1-1:3,mask?mask:0x82,!t->no_secondary,t->const_fs?"const":"tex",greens,reds,px[32*64+32]);
  log_line(b,line,len,sizeof(line));
  len=snprintf(line,sizeof(line),"NORD2_METAL tm v=%u heapva=%llx poly=%llx bottom=+%llx blocks=%u first=%zx last=%zx top64k=%u vtx=%llx/%llx",
   v,(unsigned long long)b->heap->va,(unsigned long long)poly,(unsigned long long)(bottom-b->heap->va),blocks,first,last,top,
   (unsigned long long)va0,(unsigned long long)va1);
  log_line(b,line,len,sizeof(line));
  len=snprintf(line,sizeof(line),"NORD2_METAL tm v=%u state",v);
  for(unsigned k=0;k<16;k++){uint32_t w;memcpy(&w,d.block+512+128+4*k,4);len+=snprintf(line+len,sizeof(line)-(size_t)len," %08x",w);}
  log_line(b,line,len,sizeof(line));
  if(greens==4096&&!t->const_fs&&!t->no_secondary&&!t->spacer&&adopt==TM_COUNT)adopt=v;
 }
 if(b->broken)adopt=TM_COUNT;
 if(adopt<TM_COUNT){const struct tm_variant *t=&tm_variants[adopt];
  b->tiler.heap_bytes=t->heap_bytes;b->tiler.chunk_enum1=t->chunk_enum1;
  b->tiler.mask=t->mask==TM_MESA?(t->heap_bytes>(2u<<20)?mesa16:mesa):t->mask;b->tiler.no_secondary=0;}
 else{b->tiler.heap_bytes=HEAP_DEFAULT;b->tiler.chunk_enum1=0;b->tiler.mask=0;b->tiler.no_secondary=0;}
 len=snprintf(line,sizeof(line),"NORD2_METAL tm adopt=%d heap=%uK chunk=%u mask=%03x",adopt<TM_COUNT?(int)adopt:-1,
  b->tiler.heap_bytes>>10,b->tiler.chunk_enum1?b->tiler.chunk_enum1-1:3,b->tiler.mask?b->tiler.mask:0x82);
 log_line(b,line,len,sizeof(line));
out:
 if(texel)(void)drop(b,texel);
 if(target)(void)drop(b,target);
 if(b->heap&&!b->broken){(void)wait_pending(b);if(!drop(b,b->heap))b->heap=NULL;}
 /* Keep later per-draw logs well inside the 16 KiB client quota. */
 b->trace_draws=saved_draws>24?24:saved_draws;b->trace_geometry=0;
}

/* ---- Chained submission: API and the visibility probe -------------------- */
static unsigned chain_env(const char *name,unsigned lo,unsigned hi,unsigned fallback){
    const char *v=getenv(name);unsigned x=0;
    if(!v||!*v)return fallback;
    for(const char *c=v;*c;c++){if(*c<'0'||*c>'9'||x>1000)return fallback;x=x*10+(unsigned)(*c-'0');}
    return x>=lo&&x<=hi?x:fallback;
}
/* One GPU check, in one 4-frame chain: clear X to c0, copy X->Y0, clear X to
 * c1, copy X->Y1 (copies: the LOAD preload program sampling X, fragment stage
 * only). Y0==c0 and Y1==c1 (Y1 equal to X's final texel, Y0 different and
 * not X's zero-initialised bytes) shows that a fragment job sees the previous
 * fragment job's writes inside one chain, also for a line its texture cache
 * read before (Y0). Anything else keeps mode 1 off (mode 2). */
static unsigned chain_probe(struct bridge *b,uintptr_t value){
    struct bo *x=NULL,*y[2]={NULL,NULL};unsigned result=3;
    b->ops.lock(b->ops.opaque);
    int e=alloc(b,16384,0,0,&x);if(!e)e=map_bo(b,x);
    for(unsigned k=0;k<2&&!e;k++){e=alloc(b,16384,0,0,&y[k]);if(!e)e=map_bo(b,y[k]);}
    if(!e){memset((void *)(uintptr_t)x->cpu,0,1024);memset((void *)(uintptr_t)y[0]->cpu,0,1024);memset((void *)(uintptr_t)y[1]->cpu,0,1024);}
    unsigned max=b->chain.max,mode=b->chain.mode,open=b->chain.open;
    b->chain.max=max<4?4:max;b->chain.mode=1;b->chain.open=1;
    b->ops.unlock(b->ops.opaque);
    static const float colour[2][4]={{0.0625f,0.25f,0.5f,1.0f},{0.75f,0.5f,0.125f,1.0f}};
    for(unsigned k=0;k<2&&!e;k++) {
        struct tegu_generic_draw_request c={.target_format=80,.width=16,.height=16,.row_bytes=64,.load_action=2,.vertex_count=3,
            .scissor={0,0,16,16},.target_token=x->handle,.target_length=x->size};
        double rgba[4]={colour[k][0],colour[k][1],colour[k][2],colour[k][3]};
        if(n2r_clear(80,rgba,c.clear_words)){e=BAD;break;}
        uint64_t out[2];unsigned n=2;e=n2mb_clear(value,&c,out,&n);if(e)break;
        struct tegu_generic_draw_request q={.target_format=80,.width=16,.height=16,.row_bytes=64,.load_action=2,.vertex_count=3,
            .scissor={0,0,16,16},.target_token=y[k]->handle,.target_length=y[k]->size,.binding_count=1};
        q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,
            .memory={.token=x->handle,.offset=0,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80,.reserved=N2MB_BINDING_STAGED};
        n=2;e=n2mb_draw(value,preload_shader(80),&q,out,&n);
    }
    b->ops.lock(b->ops.opaque);
    unsigned chained=b->chain.frames;
    int f=chain_flush(b,CHAIN_FLUSH_END);if(!e)e=f;
    if(!e)e=wait_pending(b);
    if(!e&&chained==4) {
        uint32_t px,p0,p1;memcpy(&px,(const void *)(uintptr_t)x->cpu,4);memcpy(&p0,(const void *)(uintptr_t)y[0]->cpu,4);
        memcpy(&p1,(const void *)(uintptr_t)y[1]->cpu,4);
        result=p1==px&&p0!=p1&&p0!=0?1u:2u;
        char line[160];int len=snprintf(line,sizeof(line),"NORD2_METAL chain-probe x=%08x y0=%08x y1=%08x %s\n",px,p0,p1,result==1?"visible":"stale");
        if(len>0&&(size_t)len<sizeof(line)){unsigned no=0;size_t bytes=0;
            (void)b->ops.call(b->ops.opaque,NORD2_GPU_SEL_LOG,NULL,0,line,(size_t)len,NULL,&no,NULL,&bytes);}
    }
    b->chain.max=max;b->chain.mode=mode;b->chain.open=open;
    if(x)(void)drop(b,x);
    for(unsigned k=0;k<2;k++)if(y[k])(void)drop(b,y[k]);
    b->ops.unlock(b->ops.opaque);
    return result;
}
unsigned n2mb_chain_begin(uintptr_t value){
    struct bridge *b=(void *)value;if(!b)return 0;
    b->ops.lock(b->ops.opaque);
    unsigned mode=b->chain.mode,probe=b->chain.probe==1&&mode==1&&!b->broken;
    if(probe)b->chain.probe=2;
    b->ops.unlock(b->ops.opaque);
    if(probe) {
        unsigned r=chain_probe(b,value);
        b->ops.lock(b->ops.opaque);b->chain.st[9]=r;if(r!=1&&b->chain.mode==1)b->chain.mode=2;b->ops.unlock(b->ops.opaque);
    }
    b->ops.lock(b->ops.opaque);
    if(b->chain.mode&&!b->broken)b->chain.open=1;
    mode=b->chain.open?b->chain.mode:0;
    b->ops.unlock(b->ops.opaque);return mode;
}
int n2mb_chain_end(uintptr_t value,uint64_t *job){
    struct bridge *b=(void *)value;if(!b)return BAD;
    b->ops.lock(b->ops.opaque);
    unsigned had=b->chain.frames;
    int e=chain_flush(b,CHAIN_FLUSH_END);
    if(job)*job=had&&!e?b->chain.last_job:0;
    b->chain.open=0;b->code_protect=UINT64_MAX;
    if(b->chain.mode&&!(++b->chain.ends%120u))chain_log(b);
    b->ops.unlock(b->ops.opaque);return e;
}
unsigned n2mb_chain_mode(uintptr_t value,unsigned mode,unsigned frames){
    struct bridge *b=(void *)value;if(!b)return 0;
    b->ops.lock(b->ops.opaque);unsigned old=b->chain.mode;
    if(b->chain.frames)(void)chain_flush(b,CHAIN_FLUSH_OTHER);
    if(mode<=2){b->chain.mode=mode;b->chain.probe=2;}
    if(frames>=2&&frames<=N2MB_CHAIN_MAX_FRAMES)b->chain.max=frames;
    b->ops.unlock(b->ops.opaque);return old;
}
void n2mb_chain_stats(uintptr_t value,uint64_t out[N2MB_CHAIN_STATS]){
    struct bridge *b=(void *)value;if(!b||!out)return;
    b->ops.lock(b->ops.opaque);memcpy(out,b->chain.st,sizeof(b->chain.st));out[10]=b->chain.mode;b->ops.unlock(b->ops.opaque);
}
/* Wait for the submitted jobs that list one of these buffer handles (CPU
 * blits, index re-expansion, shadow refresh), not for every queued job. A
 * handle of the open chain submits the chain first. The ring keeps this
 * context's 64 newest jobs; the kernel holds at most 32 jobs at once (a full
 * window answers Busy and jm_submit reaps), so every job that can still be in
 * flight is in the ring. */
int n2mb_wait_handles(uintptr_t value,const uint64_t *handles,unsigned n,uint64_t *failed_job,unsigned *waited){
    struct bridge *b=(void *)value;if(!b||(n&&!handles))return BAD;
    if(failed_job)*failed_job=0;
    if(waited)*waited=0;
    b->ops.lock(b->ops.opaque);
    if(b->broken){if(failed_job)*failed_job=b->async_failed;b->ops.unlock(b->ops.opaque);return NOTREADY;}
    int e=0;
    for(unsigned i=0;i<n&&b->chain.frames;i++)if(handles[i]&&chain_holds(b,handles[i])){e=chain_flush(b,CHAIN_FLUSH_OTHER);break;}
    uint64_t seq=0;
    if(!e&&b->async==1&&b->pending>b->reaped) {
        for(unsigned r=0;r<64;r++){if(!b->recent[r].job||b->recent[r].job<=b->reaped)continue;
            for(unsigned k=0;k<b->recent[r].n;k++){unsigned hit=0;
                for(unsigned i=0;i<n;i++)if(handles[i]==b->recent[r].h[k]){hit=1;break;}
                if(hit){if(b->recent[r].job>seq)seq=b->recent[r].job;break;}}}
    }
    b->ops.unlock(b->ops.opaque);
    if(e)return e;
    if(seq&&waited)*waited=1;
    return seq?n2mb_wait_seq(value,seq,failed_job):0;
}
/* Kernel 0112 display-surface reservation (B1). RESERVE covers the imported
 * surfaces a committed command will write until its jobs are submitted: the
 * framebuffer holds a swap of them as if those jobs were already queued, so
 * the frontend can report the command scheduled at commit. The handles are
 * filtered to this context's imports (other buffers are never display
 * surfaces); none -> id 0. NOTREADY/UNSUPPORTED: no reservation (fall back). */
int n2mb_surface_reserve(uintptr_t value,const uint64_t *handles,unsigned n,uint64_t *id){
    struct bridge *b=(void *)value;if(!b||!id||(n&&!handles))return BAD;
    *id=0;b->ops.lock(b->ops.opaque);int e=0;
    if(b->broken){e=NOTREADY;goto end;}
    if(!b->resv) {
        uint64_t in=0,out=7;unsigned no=1;
        int pe=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_SURFACE_RESERVE,&in,1,NULL,0,&out,&no,NULL,NULL);
        b->resv=!pe&&no==1&&!out?1:2;
    }
    if(b->resv!=1){e=UNSUPPORTED;goto end;}
    uint32_t list[NORD2_GPU_RESERVE_MAX];unsigned m=0;
    for(unsigned i=0;i<n;i++) {
        struct bo *o=handles[i]?lookup(b,handles[i]):NULL;
        if(!o||!o->external_cpu)continue;
        unsigned k=0;while(k<m&&list[k]!=o->handle)k++;
        if(k<m)continue;
        if(m==NORD2_GPU_RESERVE_MAX){e=BAD;b->resv_stats[3]++;goto end;}
        list[m++]=o->handle;
    }
    if(!m){if(n)b->resv_stats[2]++;goto end;}
    uint64_t in=m,out=0;unsigned no=1;
    e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_SURFACE_RESERVE,&in,1,list,m*sizeof(uint32_t),&out,&no,NULL,NULL);
    if(!e&&(no!=1||!out))e=FAILED;
    if(!e){*id=out;b->resv_stats[0]++;}else b->resv_stats[3]++;
end:
    b->ops.unlock(b->ops.opaque);return e;
}
/* RETIRE: seq != 0 keeps the surfaces busy until this context's jobs with id
 * <= seq are done (the command's newest job); seq 0 cancels. id 0: no-op. */
int n2mb_surface_retire(uintptr_t value,uint64_t id,uint64_t seq){
    struct bridge *b=(void *)value;if(!b)return BAD;
    if(!id)return 0;
    b->ops.lock(b->ops.opaque);
    /* The newest job must exist in the kernel: submit an open chain first. */
    if(b->chain.frames&&!b->broken)(void)chain_flush(b,CHAIN_FLUSH_OTHER);
    uint64_t in[2]={id,seq};unsigned no=0;
    int e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_SURFACE_RETIRE,in,2,NULL,0,NULL,&no,NULL,NULL);
    if(!e)b->resv_stats[1]++;
    b->ops.unlock(b->ops.opaque);return e;
}
void n2mb_surface_stats(uintptr_t value,uint64_t out[4]){
    struct bridge *b=(void *)value;if(!b||!out)return;
    b->ops.lock(b->ops.opaque);memcpy(out,b->resv_stats,sizeof(b->resv_stats));b->ops.unlock(b->ops.opaque);
}
/* Tests: forget the cached 0112 probe outcome (the next reserve probes). */
void n2mb_surface_reprobe(uintptr_t value){struct bridge *b=(void *)value;if(!b)return;b->ops.lock(b->ops.opaque);b->resv=0;b->ops.unlock(b->ops.opaque);}
