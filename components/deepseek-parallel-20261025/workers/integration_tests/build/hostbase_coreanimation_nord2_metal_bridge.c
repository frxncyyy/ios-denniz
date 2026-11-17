#include "nord2_metal_bridge.h"
#include "tegu/pipeline_format.h"
#include "nord2_gpu_transport.h"
#include "nord2_gpu_jm.h"
#include "nord2_metal_render.h"
#include "nord2_preload.h"
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
#define MAX_BO 256
#define MAX_PIPE 64
#define PIPE_TAG (1ull<<63)
struct bo {uint32_t handle;uint64_t va,size,cpu,mapped;unsigned internal,external_cpu;};
struct pipeline {uint64_t id;struct tegu_pipeline_info info;struct tegu_pipeline_info2 info2;unsigned version;uint32_t code;};
struct bridge {
    struct n2mb_ops ops;
    struct bo bos[MAX_BO];
    struct pipeline pipes[MAX_PIPE];
    uint64_t next_pipeline;
    unsigned broken,trace_draws,trace_geometry;
    /* Keep the heap mapped until context close, following Mesa's lifetime.
     * Hardware traces suggest state survives between job chains; they do
     * not establish whether that state is keyed by context or descriptor VA. */
    struct bo *heap;
    struct {uint32_t heap_bytes,chunk_enum1,mask,no_secondary;} tiler;
    unsigned matrix; /* 1: NORD2_METAL_TILER_MATRIX pending, 2: done */
};
#define HEAP_DEFAULT (2u<<20)
#define HEAP_MATRIX (16u<<20)
static struct bo *lookup(struct bridge *b,uint64_t h) {
    if(!h||h>UINT32_MAX)return NULL;
    for(unsigned i=0;i<MAX_BO;i++)if(b->bos[i].handle==h)return &b->bos[i];
    return NULL;
}
static int scalar(struct bridge *b,unsigned sel,const uint64_t *in,unsigned ni,uint64_t *out,unsigned *no) {
    return b->ops.call(b->ops.opaque,sel,in,ni,NULL,0,out,no,NULL,NULL);
}
static int drop(struct bridge *b,struct bo *o) {
    if(!o||!o->handle)return BAD;
    if(o->cpu&&!o->external_cpu) {
        int e=b->ops.unmap(b->ops.opaque,o->handle,o->cpu);
        if(e)return e;
        o->cpu=o->mapped=0;
    }
    uint64_t h=o->handle;unsigned n=0;
    int e=scalar(b,NORD2_GPU_SEL_RELEASE,&h,1,NULL,&n);
    if(!e)*o=(struct bo){0};
    return e;
}
static int alloc(struct bridge *b,uint64_t size,unsigned exec,unsigned internal,struct bo **result) {
    struct bo *o=NULL;
    for(unsigned i=0;i<MAX_BO;i++)if(!b->bos[i].handle){o=&b->bos[i];break;}
    if(!o)return NOTREADY;
    uint64_t in[2]={size,exec},out[3]={0};unsigned n=3;
    int e=scalar(b,NORD2_GPU_SEL_ALLOC,in,2,out,&n);if(e)return e;
    if(n!=3||!out[0]||out[0]>UINT32_MAX||lookup(b,out[0])||out[2]<size||
       !out[1]||(out[1]&16383)||out[1]>=(1ull<<40)||out[2]>(1ull<<40)-out[1]) {
        /* Unexpected ABI output: do not attempt a release using an untrusted
         * handle that might belong to another allocation. Close owns recovery. */
        b->broken=1;return FAILED;
    }
    *o=(struct bo){.handle=(uint32_t)out[0],.va=out[1],.size=out[2],.internal=internal};
    *result=o;return 0;
}
static int map_bo(struct bridge *b,struct bo *o) {
    if(o->cpu)return BAD;
    uint64_t address=0,size=0;
    int e=b->ops.map(b->ops.opaque,o->handle,&address,&size);if(e)return e;
    if(!address||size<o->size){if(address)(void)b->ops.unmap(b->ops.opaque,o->handle,address);return FAILED;}
    o->cpu=address;o->mapped=size;return 0;
}
uintptr_t n2mb_open(const struct n2mb_ops *ops) {
    if(!ops||!ops->call||!ops->map||!ops->unmap||!ops->close||!ops->lock||!ops->unlock)return 0;
    struct bridge *b=calloc(1,sizeof(*b));if(!b)return 0;b->ops=*ops;b->next_pipeline=1;
    const char *trace=getenv("NORD2_METAL_TRACE");b->trace_draws=trace&&trace[0]=='1'&&!trace[1]?64:0;
    b->trace_geometry=b->trace_draws?8:0;
    b->tiler.heap_bytes=HEAP_DEFAULT;
    const char *matrix=getenv("NORD2_METAL_TILER_MATRIX");b->matrix=matrix&&matrix[0]=='1'&&!matrix[1];
    return (uintptr_t)b;
}
int n2mb_close(uintptr_t value) {
    struct bridge *b=(void *)value;if(!b)return BAD;
    b->ops.lock(b->ops.opaque);
    /* Closing the user client stops/quarantines pending GPU use. A timeout
     * never makes userland free the kernel's backing pages. */
    for(unsigned i=0;i<MAX_BO;i++)if(b->bos[i].cpu&&!b->bos[i].external_cpu)
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
    switch(sel) {
    case 0:if(ni){e=BAD;break;}e=capabilities(b,out,count);break;
    case 1: {
        if(ni!=1||!out||*count<2){e=BAD;break;}
        struct bo *o=NULL;e=alloc(b,in[0],0,0,&o);
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
    b->ops.lock(b->ops.opaque);struct bo *o=lookup(b,handle);int e=b->broken?NOTREADY:!o||o->internal?BAD:map_bo(b,o);
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
        if(total>((1ull<<40)-1)||alloc(b,total,0,1,&l->tls))return BAD;
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
        if(total>((1ull<<40)-1)||alloc(b,total,0,1,&l->wls))return BAD;
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
    if(local.tls&&add_ref(&req,local.tls,0)){e=BAD;goto cleanup;}
    if(local.wls&&add_ref(&req,local.wls,0)){e=BAD;goto cleanup;}
    for(unsigned i=0;i<TEGU_PIPELINE2_MAX_BUFFERS;i++)if(buffers[i])if(add_ref(&req,buffers[i],
        (buffer_flags[i]&TEGU_PIPELINE2_ACCESS_WRITE)!=0)){e=BAD;goto cleanup;}
    for(unsigned i=0;i<256;i++)if(textures[i])if(add_ref(&req,textures[i],
        (texture_flags[i]&TEGU_PIPELINE2_ACCESS_WRITE)!=0)){e=BAD;goto cleanup;}
    /* The compile code is bridge-owned and remains retained until wait.  Add
     * it when the 32-handle transport request has room; the extended-buffer
     * maximum may consume all resource slots. */
    if(req.nr_handles<32){(void)add_ref(&req,code,0);}
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
    if(b->broken)e=NOTREADY;
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
 for(unsigned i=0;i<MAX_BO;i++)if(!b->bos[i].handle){o=&b->bos[i];break;}
 if(!o)goto end;
 uint64_t args[4]={sid,0,size,0},reply[3]={0};unsigned n=3;
 e=scalar(b,NORD2_GPU_SEL_IMPORT_SURFACE,args,4,reply,&n);if(e)goto end;
 if(n!=3||!reply[0]||reply[0]>UINT32_MAX||lookup(b,reply[0])||!reply[1]||(reply[1]&4095)||reply[1]>=(1ull<<40)||reply[2]>(1ull<<40)-reply[1]||reply[2]<size){b->broken=1;e=FAILED;goto end;}
 *o=(struct bo){.handle=reply[0],.va=reply[1],.size=size,.cpu=cpu,.mapped=size,.external_cpu=1};*handle=o->handle;
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
static void clear_pixel(const struct tegu_generic_draw_request *q,uint8_t pixel[8]){
 uint32_t c=q->target_format?q->clear_words[0]:q->clear_color;
 memset(pixel,0,8);
 if(q->target_format==115){memcpy(pixel,q->clear_words,8);return;}
 if(q->target_format==10){pixel[0]=c;return;}
 if(!q->target_format||q->target_format==80){pixel[0]=c>>16;pixel[1]=c>>8;pixel[2]=c;pixel[3]=c>>24;}
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
                           struct nord2_gpu_submit_request *req)
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
        if(!bpp||!t->token||t->token>0xffffffffu||t->reserved||t->load_action>2||
           (from_src&&t->load_action==1)||(t->write_mask&~15u)||
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
            .write_mask=t->write_mask,.reserved=0,.address=address,.row_bytes=t->row_bytes,
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
    return 0;
}

struct diag {unsigned spacer;uint8_t block[2048];};
static int draw_locked(struct bridge *b,const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q,
 const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts,
 const struct n2mb_render_targets *mrt,uint64_t *out,unsigned *count,struct diag *d){
 struct bo *owned[8]={0}; /* cmd, (unused), VS, FS, snapshot, preloadVS, preloadFS, VA spacer */
 struct bo *tls=NULL;
 int e=BAD;if(b->broken){e=NOTREADY;goto done;}
 struct nord2_gpu_submit_request req={.version=2,.kind=2};
 struct n2c_mrt mt={0};if(n2c_mrt_prepare(b,q,mrt,&mt,&req))goto done;
 int use_mrt=mt.count!=0;
 struct bo *target=use_mrt?mt.bo[0]:lookup(b,q->target_token);
 uint64_t target_bytes=use_mrt?(uint64_t)mt.rt[0].row_bytes*mt.rt[0].height:
  (uint64_t)q->row_bytes*q->height;
 if(!use_mrt&&(!target||target->internal||q->target_length>target->size||
    q->target_offset>q->target_length||target_bytes>q->target_length-q->target_offset||
    add_ref(&req,target,1)))goto done;
 uint64_t target_va=use_mrt?mt.rt[0].address:target->va+q->target_offset;
 struct n2r_addresses a={.target=target_va,.heap_bytes=2u<<20};
 a.vertex=resolve(b,&q->vertex,&req);a.index=resolve(b,&q->index,&req);
 if(a.vertex==UINT64_MAX||a.index==UINT64_MAX)goto done;
 unsigned snapshot=!use_mrt&&q->load_action==1;
 for(unsigned i=0;i<q->binding_count;i++){
  a.binding[i]=resolve(b,&q->bindings[i].memory,&req);
  if(a.binding[i]==UINT64_MAX)goto done;
  if(q->bindings[i].kind==TEGU_GENERIC_DRAW_ATTACHMENT) {
   /* A true-MRT job has no per-target snapshot transport yet.  Do not
    * silently feed the ordinary target or a stale texture. */
   if(use_mrt){e=BAD;goto done;}
   snapshot=1;
  }
  /* Read/write feedback through an ordinary texture must use the snapshot. */
  if(!use_mrt&&q->bindings[i].memory.token==q->target_token)snapshot=1;
 }
 if(!b->heap&&(e=alloc(b,b->matrix?HEAP_MATRIX:HEAP_DEFAULT,0,1,&b->heap)))goto done;
 if(b->tiler.heap_bytes>b->heap->size){e=BAD;goto done;}
 a.heap_bytes=b->tiler.heap_bytes;a.heap_chunk_enum1=b->tiler.chunk_enum1;
 a.hierarchy_mask=b->tiler.mask;a.no_secondary=b->tiler.no_secondary;
 if(d&&d->spacer&&(e=alloc(b,65536,0,1,&owned[7])))goto done; /* shifts the command VA */
 e=alloc(b,2*N2R_BLOCK,0,1,&owned[0]);if(e)goto done;e=map_bo(b,owned[0]);if(e)goto done;
 e=copy_code(b,s->vertex_code,s->vertex_bytes,&owned[2]);if(e)goto done;
 e=copy_code(b,s->fragment_code,s->fragment_bytes,&owned[3]);if(e)goto done;
 a.command=a.frame=owned[0]->va;a.heap=b->heap->va;a.vertex_code=owned[2]->va;a.fragment_code=owned[3]->va;
 if(opts&&(opts->thread_storage_bytes||opts->thread_storage_reserved)) {
  if(opts->version!=N2R_DRAW_OPTIONS_VERSION||
     opts->thread_storage_bytes>N2R_THREAD_STORAGE_MAX||opts->thread_storage_reserved) {
   e=BAD;goto done;
  }
  uint32_t cores=0;uint64_t total=0;
  if(n2c_core_range(b,&cores)){e=BAD;goto done;}
  if(n2r_thread_storage_total(opts->thread_storage_bytes,2048,cores,&total)||
     !total||total>(64ull<<20)||alloc(b,total,0,1,&tls)||!tls->va||(tls->va&4095u)) {
   e=BAD;goto done;
  }
  a.tls=tls->va;
  if(add_ref(&req,tls,0)){e=BAD;goto done;}
 }
 if(snapshot){
  if(!target->cpu){e=map_bo(b,target);if(e)goto done;}
  uint64_t args[2]={target->handle,0};unsigned n=0;e=scalar(b,NORD2_GPU_SEL_SYNC,args,2,NULL,&n);if(e)goto done;
  e=alloc(b,target->size,0,1,&owned[4]);if(e)goto done;e=map_bo(b,owned[4]);if(e)goto done;
  memcpy((void *)(uintptr_t)owned[4]->cpu,(const void *)(uintptr_t)target->cpu,target->size);
  if(q->load_action==2){
   uint8_t pixel[8];clear_pixel(q,pixel);unsigned bytes=n2r_bpp(q->target_format);
   uint8_t *dst=(void *)(uintptr_t)(owned[4]->cpu+q->target_offset);
   for(unsigned y=0;y<q->height;y++)for(unsigned x=0;x<q->width;x++)memcpy(dst+(size_t)y*q->row_bytes+x*bytes,pixel,bytes);
  }
  a.snapshot=owned[4]->va+q->target_offset;
  for(unsigned i=0;i<q->binding_count;i++)if(q->bindings[i].memory.token==q->target_token)a.binding[i]=owned[4]->va+q->bindings[i].memory.offset;
 }
 uint8_t *m=(void *)(uintptr_t)owned[0]->cpu;
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
  e=n2r_build_v4(m,s,&qb,&a,tr,opts,&rts,qb.load_action==1?2:1,0);
 } else e=tr?n2r_build_v3(m,s,q,&a,tr,opts,q->load_action==1?2:1,0):
           n2r_build(m,s,q,&a,q->load_action==1?2:1,0);
 if(e){e=BAD;goto done;}
 req.heads[0]=a.command+N2R_JOB;req.heads[1]=a.command+N2R_FRAGMENT;
 if(!use_mrt&&q->load_action==1){
  e=copy_code(b,n2_preload_shader.vertex_code,n2_preload_shader.vertex_bytes,&owned[5]);if(e)goto done;
  e=copy_code(b,n2_preload_shader.fragment_code,n2_preload_shader.fragment_bytes,&owned[6]);if(e)goto done;
  struct tegu_generic_draw_request pre={.target_format=q->target_format,.width=q->width,.height=q->height,.row_bytes=q->row_bytes,.load_action=1,.vertex_count=3,.scissor={0,0,q->width,q->height},.binding_count=1};
  pre.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=owned[4]->handle,.offset=q->target_offset,.bytes=target_bytes},.width=q->width,.height=q->height,.row_bytes=q->row_bytes,.format=q->target_format?q->target_format:80};
  struct n2r_addresses pa=a;pa.command=a.command+N2R_BLOCK;pa.vertex_code=owned[5]->va;pa.fragment_code=owned[6]->va;pa.binding[0]=a.snapshot;
  e=n2r_build(m+N2R_BLOCK,&n2_preload_shader,&pre,&pa,1,req.heads[0]);if(e){e=BAD;goto done;}
  req.heads[0]=pa.command+N2R_JOB;
 }
 for(unsigned i=0;i<7;i++)if(owned[i]){e=add_ref(&req,owned[i],i<=1);if(e)goto done;}
 if((e=add_ref(&req,b->heap,1)))goto done;
 geometry_before(b,b->heap,a.heap_bytes);
 uint64_t job=0;unsigned n=1;e=b->ops.call(b->ops.opaque,NORD2_GPU_SEL_SUBMIT_JM,NULL,0,&req,sizeof(req),&job,&n,NULL,NULL);if(e)goto done;
 if(n!=1||!job){b->broken=1;e=FAILED;goto done;}
 uint64_t wait[2]={job,5000},state=0;n=1;e=scalar(b,NORD2_GPU_SEL_WAIT,wait,2,&state,&n);
 if(e||n!=1||state!=4){b->broken=1;if(!e)e=FAILED;goto done;}
 geometry_trace(b,b->heap,m,&a);
 render_trace(b,m,&a,q,s,q->load_action==1);
 if(d)memcpy(d->block,m,sizeof(d->block));
 out[0]=out[1]=job;*count=2;
done:
 if(tls){int r=drop(b,tls);if(!e)e=r;}
 for(unsigned i=0;i<8;i++)if(owned[i]){int r=drop(b,owned[i]);if(!e)e=r;}
 return e;
}
static void tiler_matrix(struct bridge *b);
int n2mb_draw(uintptr_t value,const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q,uint64_t *out,unsigned *count){
 struct bridge *b=(void *)value;if(!b||!out||!count||*count<2||n2r_validate(s,q))return BAD;
 b->ops.lock(b->ops.opaque);
 if(b->matrix==1&&!b->broken)tiler_matrix(b);
 int e=draw_locked(b,s,q,NULL,NULL,NULL,out,count,NULL);
 b->ops.unlock(b->ops.opaque);return e;
}

int n2mb_draw_v2(uintptr_t value,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,
 uint64_t *out,unsigned *count){
 struct bridge *b=(void *)value;
 if(!b||!out||!count||*count<2||!tr||n2r_validate_v2(s,q,tr))return BAD;
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
 if(q&&q->target_format!=10&&n2r_validate_v3(s,q,tr,opts))return BAD;
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
 if(!mrt&&q&&q->target_format!=10&&n2r_validate_v3(s,q,tr,opts))return BAD;
 b->ops.lock(b->ops.opaque);
 if(b->matrix==1&&!b->broken)tiler_matrix(b);
 int e=draw_locked(b,s,q,tr,opts,mrt,out,count,NULL);
 b->ops.unlock(b->ops.opaque);return e;
}

/* A clear-only pass still performs real GPU work, using the same sampled
 * fullscreen shader as LOAD. The single texel is command-order source data. */
int n2mb_clear(uintptr_t value,const struct tegu_generic_draw_request *source,uint64_t *out,unsigned *count){
 struct bridge *b=(void *)value;if(!b||!source||!source->width||!source->height||!n2r_bpp(source->target_format))return BAD;
 struct bo *color=NULL;int e;
 b->ops.lock(b->ops.opaque);
 if(b->broken){b->ops.unlock(b->ops.opaque);return NOTREADY;}
 e=alloc(b,64,0,0,&color);if(!e)e=map_bo(b,color);
 if(!e){uint8_t pixel[8];clear_pixel(source,pixel);memcpy((void *)(uintptr_t)color->cpu,pixel,8);}
 b->ops.unlock(b->ops.opaque);
 if(!e){
  struct tegu_generic_draw_request q=*source;
  q.viewport_valid=0;q.load_action=2;q.vertex_count=3;q.primitive=0;q.binding_count=1;
  q.scissor[0]=q.scissor[1]=0;q.scissor[2]=q.width;q.scissor[3]=q.height;
  q.bindings[0]=(struct tegu_generic_draw_binding){.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=color->handle,.bytes=64},.width=1,.height=1,.row_bytes=64,.format=q.target_format?q.target_format:80};
  e=n2mb_draw(value,&n2_preload_shader,&q,out,count);
 }
 b->ops.lock(b->ops.opaque);if(color){int cleanup=drop(b,color);if(!e)e=cleanup;}b->ops.unlock(b->ops.opaque);return e;
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
 unsigned saved_draws=b->trace_draws;b->trace_draws=b->trace_geometry=0;
 struct nord2_gpu_status st={0};size_t ss=sizeof(st);unsigned n=0;
 if(b->ops.call(b->ops.opaque,NORD2_GPU_SEL_STATUS,NULL,0,NULL,0,NULL,&n,&st,&ss)||ss!=sizeof(st))memset(&st,0,sizeof(st));
 uint32_t mesa=n2r_mesa_hierarchy_mask(64,64,st.tiler_features,2u<<20),mesa16=n2r_mesa_hierarchy_mask(64,64,st.tiler_features,16u<<20);
 len=snprintf(line,sizeof(line),"NORD2_METAL tm id gpu=%08x tiler=%08x l2=%08x core=%08x mem=%08x mmu=%08x js=%08x mesa=%03x/%03x",
  st.gpu_id,st.tiler_features,st.l2_features,st.core_features,st.mem_features,st.mmu_features,st.js_present,mesa,mesa16);
 log_line(b,line,len,sizeof(line));
 struct bo *target=NULL,*texel=NULL;int e=alloc(b,16384,0,0,&target);
 if(!e)e=map_bo(b,target);if(!e)e=alloc(b,64,0,0,&texel);if(!e)e=map_bo(b,texel);
 if(!e&&!b->heap)e=alloc(b,HEAP_MATRIX,0,1,&b->heap);
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
 /* Keep later per-draw logs well inside the 16 KiB client quota. */
 b->trace_draws=saved_draws>24?24:saved_draws;b->trace_geometry=0;
}
