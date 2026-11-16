/* CPU fixed-function tessellator for MTLRenderCommandEncoder drawPatches.
 *
 * Pure C, no libc headers, no libm. One call generates the vertices
 * (position_in_patch) and the triangle-list indices of one patch from its
 * tessellation factors. The caller adds patch_id / instance_id to each emitted
 * vertex and draws the result as an ordinary indexed triangle list.
 *
 * What is specified here (and tested against an independent oracle in
 * gpu/tests/tessellator_test.py):
 *   - Domains: triangle (u,v,w barycentric, edge[0]: u=0, edge[1]: v=0,
 *     edge[2]: w=0) and quad (u,v, edge[0]: u=0, edge[1]: v=0, edge[2]: u=1,
 *     edge[3]: v=1, inside[0] along u, inside[1] along v).
 *   - Partition modes (MTLTessellationPartitionMode): pow2 (0), integer (1),
 *     fractional_odd (2), fractional_even (3). Clamp: [1,max] ([1,63] odd,
 *     [2,max] even, max <= 64). Segment counts: integer ceil(f), pow2 next
 *     power of two, odd the smallest odd >= f, even the smallest even >= f.
 *   - Fractional placement: the lattice of N segments is interpolated with
 *     weight t=(f-(N-2))/2 from the lattice of N-2 segments, the extra two
 *     points being born at the edge centre (D3D11 reference behaviour).
 *     t=1 is exactly uniform, so integer factors are uniform in every mode.
 *   - Factors <= 0 or NaN cull the patch (no output), as in D3D11.
 *   - Outer ring = edge factors; inner rings shrink by two segments per ring
 *     (inside factors); rings are stitched strip-wise. Every edge of the
 *     output is shared by exactly two triangles except the outer boundary.
 *   - Winding: counter-clockwise (u right, v up) for MTLWindingCounterClockwise,
 *     reversed for clockwise.
 * NOT specified by Apple and therefore not reproduced bit-exactly: the vertex
 * order and which diagonal a strip uses. Only device runs can show how the
 * hardware orders them; the tests assert invariants, not hardware parity. */
#ifndef TEGU_TESSELLATOR_H
#define TEGU_TESSELLATOR_H
#include <stdint.h>
#include <stddef.h>

enum {TESS_TRIANGLE=0, TESS_QUAD=1};
enum {TESS_POW2=0, TESS_INTEGER=1, TESS_FRACTIONAL_ODD=2, TESS_FRACTIONAL_EVEN=3};
enum {TESS_CLOCKWISE=0, TESS_COUNTER_CLOCKWISE=1};
#define TESS_MAX_SEGMENTS 64

typedef struct {float u,v,w;} TessVertex;
typedef struct {
    uint32_t domain, partition, winding;
    float max_factor;            /* MTLRenderPipelineDescriptor.maxTessellationFactor (default 16) */
} TessConfig;
/* Output with capacity; counts keep growing past the capacity (snprintf style)
 * so a first call with zero capacity sizes the buffers. */
typedef struct {
    TessVertex *vertex; uint32_t *index;
    uint32_t vertex_capacity, index_capacity;
    uint32_t vertex_count, index_count;
    uint32_t index_base;         /* added to every emitted index (patch's first vertex) */
    int clockwise;
} TessOut;

typedef struct {int n; float s[TESS_MAX_SEGMENTS+1];} TessAxis;
typedef struct {int n; uint32_t id[TESS_MAX_SEGMENTS+2]; float par[TESS_MAX_SEGMENTS+2];} TessChain;

static inline int tess_ceil_int(float f) { int n=(int)f; if((float)n<f) n++; return n; }

/* Segments and 1D point placement of one edge / inside axis. -1: culled. */
static inline int tess_axis(float f, uint32_t partition, float max_factor, TessAxis *a)
{
    if(!(f>0.0f)) return -1;                               /* also NaN */
    float hi=max_factor>0.0f&&max_factor<TESS_MAX_SEGMENTS?max_factor:(float)TESS_MAX_SEGMENTS;
    float lo=1.0f;
    if(partition==TESS_FRACTIONAL_ODD&&hi>63.0f) hi=63.0f;
    if(partition==TESS_FRACTIONAL_EVEN) lo=2.0f;
    if(hi<lo) hi=lo;
    if(f<lo) f=lo;
    if(f>hi) f=hi;
    int n=tess_ceil_int(f);
    if(partition==TESS_POW2) { int p=1; while(p<n) p<<=1; n=p; }
    else if(partition==TESS_FRACTIONAL_ODD) { if(!(n&1)) n++; }
    else if(partition==TESS_FRACTIONAL_EVEN) { if(n&1) n++; }
    if(n>TESS_MAX_SEGMENTS) n=TESS_MAX_SEGMENTS;
    a->n=n;
    int fractional=partition==TESS_FRACTIONAL_ODD||partition==TESS_FRACTIONAL_EVEN;
    float t=1.0f; int l=n-2;
    if(fractional&&l>=1) { t=(f-(float)l)/2.0f; if(t>1.0f) t=1.0f; if(t<0.0f) t=0.0f; }
    if(!fractional||l<1||t>=1.0f) {
        for(int j=0;j<=n;j++) a->s[j]=(float)j/(float)n;
        a->s[n]=1.0f; return 0;
    }
    int half=n/2, split=(partition==TESS_FRACTIONAL_EVEN)?n/2-1:(n-3)/2;
    for(int j=0;j<=half;j++) {
        int i=j<=split?j:j-1;
        float lower=(float)i/(float)l, upper=(float)j/(float)n;
        a->s[j]=lower*(1.0f-t)+upper*t;
    }
    if(!(n&1)) a->s[half]=0.5f;
    for(int j=0;j<=half;j++) a->s[n-j]=1.0f-a->s[j];
    a->s[0]=0.0f; a->s[n]=1.0f;
    return 0;
}

static inline uint32_t tess_newv(TessOut *o, float u, float v, float w)
{
    uint32_t id=o->vertex_count++;
    if(id<o->vertex_capacity&&o->vertex) { o->vertex[id].u=u; o->vertex[id].v=v; o->vertex[id].w=w; }
    return id;
}
static inline void tess_tri(TessOut *o, uint32_t a, uint32_t b, uint32_t c)
{
    if(o->clockwise) { uint32_t t=b; b=c; c=t; }
    uint32_t k=o->index_count; o->index_count+=3;
    if(k+3<=o->index_capacity&&o->index) {
        o->index[k]=o->index_base+a; o->index[k+1]=o->index_base+b; o->index[k+2]=o->index_base+c;
    }
}
static inline void tess_stitch(TessOut *o, const TessChain *a, const TessChain *b)
{
    int i=0,j=0;
    while(i<a->n||j<b->n) {
        int adv_a;
        if(i>=a->n) adv_a=0; else if(j>=b->n) adv_a=1; else adv_a=a->par[i+1]<=b->par[j+1];
        if(adv_a) { tess_tri(o,a->id[i],a->id[i+1],b->id[j]); i++; }
        else { tess_tri(o,a->id[i],b->id[j+1],b->id[j]); j++; }
    }
}
static inline void tess_chain_reverse(const TessChain *src, TessChain *dst)
{
    dst->n=src->n;
    for(int i=0;i<=src->n;i++) { dst->id[i]=src->id[src->n-i]; dst->par[i]=1.0f-src->par[src->n-i]; }
}

/* ---- quad ---- */
typedef struct {TessChain side[4];} TessRing;

static inline void tess_quad_chain(TessOut *o, TessChain *c, uint32_t c0, uint32_t c1,
    int n, const float *s, int first, float fixed, int horizontal, float lo, float hi)
{
    /* points j = first .. first+n of s, mapped linearly onto [lo,hi] along the side. */
    c->n=n; c->id[0]=c0; c->id[n]=c1;
    float s0=s[first], span=s[first+n]-s0;
    c->par[0]=0.0f; if(n>0) c->par[n]=1.0f;
    for(int t=1;t<n;t++) {
        float p=(s[first+t]-s0)/span;
        c->par[t]=p;
        float x=lo+(hi-lo)*p;
        c->id[t]=horizontal?tess_newv(o,x,fixed,0.0f):tess_newv(o,fixed,x,0.0f);
    }
}

static inline int tess_quad(const TessConfig *cfg, const float *edge, const float *inside, TessOut *o)
{
    TessAxis ax[4], iu, iv;
    for(int k=0;k<4;k++) if(tess_axis(edge[k],cfg->partition,cfg->max_factor,&ax[k])) return 1;
    if(tess_axis(inside[0],cfg->partition,cfg->max_factor,&iu)) return 1;
    if(tess_axis(inside[1],cfg->partition,cfg->max_factor,&iv)) return 1;
    uint32_t c0=tess_newv(o,0,0,0), c1=tess_newv(o,1,0,0), c2=tess_newv(o,1,1,0), c3=tess_newv(o,0,1,0);
    if(ax[0].n==1&&ax[1].n==1&&ax[2].n==1&&ax[3].n==1&&iu.n==1&&iv.n==1) {
        tess_tri(o,c0,c1,c2); tess_tri(o,c0,c2,c3); return 0;
    }
    TessRing a;
    /* bottom: edge1 at v=0, u 0->1; right: edge2 at u=1, v 0->1; top: edge3 at v=1, u 1->0;
     * left: edge0 at u=0, v 1->0. The symmetric placement makes the reversal exact. */
    tess_quad_chain(o,&a.side[0],c0,c1,ax[1].n,ax[1].s,0,0.0f,1,0.0f,1.0f);
    tess_quad_chain(o,&a.side[1],c1,c2,ax[2].n,ax[2].s,0,1.0f,0,0.0f,1.0f);
    tess_quad_chain(o,&a.side[2],c2,c3,ax[3].n,ax[3].s,0,1.0f,1,1.0f,0.0f);
    tess_quad_chain(o,&a.side[3],c3,c0,ax[0].n,ax[0].s,0,0.0f,0,1.0f,0.0f);
    for(int r=1;;r++) {
        int ku=iu.n-2*r, kv=iv.n-2*r;
        int du=ku<=0, dv=kv<=0;
        float ua=du?0.5f:iu.s[r], ub=du?0.5f:iu.s[iu.n-r];
        float va=dv?0.5f:iv.s[r], vb=dv?0.5f:iv.s[iv.n-r];
        uint32_t b0=tess_newv(o,ua,va,0), b1, b2, b3;
        if(du&&dv) { b1=b2=b3=b0; }
        else if(du) { b1=b0; b2=tess_newv(o,ua,vb,0); b3=b2; }
        else if(dv) { b1=tess_newv(o,ub,va,0); b2=b1; b3=b0; }
        else { b1=tess_newv(o,ub,va,0); b2=tess_newv(o,ub,vb,0); b3=tess_newv(o,ua,vb,0); }
        TessRing b;
        /* bottom */
        if(du) { b.side[0].n=0; b.side[0].id[0]=b0; b.side[0].par[0]=0.0f; }
        else tess_quad_chain(o,&b.side[0],b0,b1,ku,iu.s,r,va,1,ua,ub);
        /* right */
        if(dv) { b.side[1].n=0; b.side[1].id[0]=b1; b.side[1].par[0]=0.0f; }
        else tess_quad_chain(o,&b.side[1],b1,b2,kv,iv.s,r,ub,0,va,vb);
        /* top */
        if(dv) { tess_chain_reverse(&b.side[0],&b.side[2]); }
        else if(du) { b.side[2].n=0; b.side[2].id[0]=b2; b.side[2].par[0]=0.0f; }
        else tess_quad_chain(o,&b.side[2],b2,b3,ku,iu.s,r,vb,1,ub,ua);
        /* left */
        if(du) { tess_chain_reverse(&b.side[1],&b.side[3]); }
        else if(dv) { b.side[3].n=0; b.side[3].id[0]=b3; b.side[3].par[0]=0.0f; }
        else tess_quad_chain(o,&b.side[3],b3,b0,kv,iv.s,r,ua,0,vb,va);
        /* top/left of a collapsed axis reuse the opposite side's points; when both
         * are collapsed every chain is the single centre point. */
        if(du&&dv) { for(int k=0;k<4;k++) { b.side[k].n=0; b.side[k].id[0]=b0; b.side[k].par[0]=0.0f; } }
        for(int k=0;k<4;k++) tess_stitch(o,&a.side[k],&b.side[k]);
        if(du||dv) break;
        if(ku==1&&kv==1) { tess_tri(o,b0,b1,b2); tess_tri(o,b0,b2,b3); break; }
        a=b;
    }
    return 0;
}

/* ---- triangle ---- */
static inline void tess_tri_pos(float *x, float *y, float *z, const float *p, const float *q, float s)
{
    *x=p[0]*(1.0f-s)+q[0]*s; *y=p[1]*(1.0f-s)+q[1]*s; *z=p[2]*(1.0f-s)+q[2]*s;
}
static inline void tess_tri_chain(TessOut *o, TessChain *c, uint32_t i0, uint32_t i1,
    const float *p, const float *q, int n, const float *s, int first)
{
    c->n=n; c->id[0]=i0; c->id[n]=i1; c->par[0]=0.0f; if(n>0) c->par[n]=1.0f;
    float s0=s[first], span=n>0?s[first+n]-s0:1.0f;
    for(int t=1;t<n;t++) {
        float pr=(s[first+t]-s0)/span, x,y,z;
        tess_tri_pos(&x,&y,&z,p,q,pr);
        c->par[t]=pr; c->id[t]=tess_newv(o,x,y,z);
    }
}

static inline int tess_triangle(const TessConfig *cfg, const float *edge, const float *inside, TessOut *o)
{
    TessAxis ax[3], in;
    for(int k=0;k<3;k++) if(tess_axis(edge[k],cfg->partition,cfg->max_factor,&ax[k])) return 1;
    if(tess_axis(inside[0],cfg->partition,cfg->max_factor,&in)) return 1;
    static const float A[3]={1,0,0}, B[3]={0,1,0}, C[3]={0,0,1};
    uint32_t ia=tess_newv(o,1,0,0), ib=tess_newv(o,0,1,0), ic=tess_newv(o,0,0,1);
    if(ax[0].n==1&&ax[1].n==1&&ax[2].n==1&&in.n==1) { tess_tri(o,ia,ib,ic); return 0; }
    TessRing a;
    /* A->B is w=0 (edge[2]), B->C is u=0 (edge[0]), C->A is v=0 (edge[1]). */
    tess_tri_chain(o,&a.side[0],ia,ib,A,B,ax[2].n,ax[2].s,0);
    tess_tri_chain(o,&a.side[1],ib,ic,B,C,ax[0].n,ax[0].s,0);
    tess_tri_chain(o,&a.side[2],ic,ia,C,A,ax[1].n,ax[1].s,0);
    for(int r=1;;r++) {
        int k=in.n-2*r;
        TessRing b;
        if(k<=0) {
            uint32_t m=tess_newv(o,1.0f/3.0f,1.0f/3.0f,1.0f/3.0f);
            for(int i=0;i<3;i++) { b.side[i].n=0; b.side[i].id[0]=m; b.side[i].par[0]=0.0f; }
            for(int i=0;i<3;i++) tess_stitch(o,&a.side[i],&b.side[i]);
            break;
        }
        float lam=1.0f-2.0f*in.s[r], third=1.0f/3.0f;
        float pa[3],pb[3],pc[3];
        const float *src[3]={A,B,C}; float *dst[3]={pa,pb,pc};
        for(int i=0;i<3;i++) for(int j=0;j<3;j++) dst[i][j]=third+lam*(src[i][j]-third);
        uint32_t ja=tess_newv(o,pa[0],pa[1],pa[2]), jb=tess_newv(o,pb[0],pb[1],pb[2]), jc=tess_newv(o,pc[0],pc[1],pc[2]);
        tess_tri_chain(o,&b.side[0],ja,jb,pa,pb,k,in.s,r);
        tess_tri_chain(o,&b.side[1],jb,jc,pb,pc,k,in.s,r);
        tess_tri_chain(o,&b.side[2],jc,ja,pc,pa,k,in.s,r);
        for(int i=0;i<3;i++) tess_stitch(o,&a.side[i],&b.side[i]);
        if(k==1) { tess_tri(o,ja,jb,jc); break; }
        a=b;
    }
    return 0;
}

/* One patch. 0 emitted, 1 culled (a factor <= 0 or NaN), -1 bad configuration.
 * edge[4] / inside[2] are already scaled; triangle uses edge[0..2], inside[0]. */
static inline int tess_patch(const TessConfig *cfg, const float *edge, const float *inside, TessOut *o)
{
    if(!cfg||!edge||!inside||!o||cfg->partition>3||cfg->domain>1) return -1;
    o->clockwise=cfg->winding==TESS_CLOCKWISE;
    uint32_t v0=o->vertex_count,i0=o->index_count;
    int r=cfg->domain==TESS_QUAD?tess_quad(cfg,edge,inside,o):tess_triangle(cfg,edge,inside,o);
    if(r) { o->vertex_count=v0; o->index_count=i0; }
    return r;
}

/* Metal's tessellation-factor structs (half precision) in the factor buffer. */
static inline float tess_half_to_float(uint16_t h)
{
    uint32_t sign=(uint32_t)(h&0x8000u)<<16, exp=(h>>10)&0x1f, man=h&0x3ffu, bits;
    if(exp==0) {
        if(!man) bits=sign;
        else { exp=113; while(!(man&0x400u)) { man<<=1; exp--; } man&=0x3ffu; bits=sign|(exp<<23)|(man<<13); }
    } else if(exp==31) bits=sign|0x7f800000u|(man<<13);
    else bits=sign|((exp+112)<<23)|(man<<13);
    union {uint32_t u; float f;} c; c.u=bits; return c.f;
}
#define TESS_TRIANGLE_FACTORS_SIZE 8u   /* MTLTriangleTessellationFactorsHalf: edge[3], inside */
#define TESS_QUAD_FACTORS_SIZE 12u      /* MTLQuadTessellationFactorsHalf: edge[4], inside[2] */
static inline size_t tess_factors_size(uint32_t domain) { return domain==TESS_QUAD?TESS_QUAD_FACTORS_SIZE:TESS_TRIANGLE_FACTORS_SIZE; }
static inline void tess_read_factors(uint32_t domain, const uint8_t *p, float scale, float edge[4], float inside[2])
{
    uint16_t h[6]; unsigned n=domain==TESS_QUAD?6:4;
    for(unsigned i=0;i<n;i++) h[i]=(uint16_t)(p[2*i]|(p[2*i+1]<<8));
    edge[3]=1.0f; inside[0]=inside[1]=1.0f;
    if(domain==TESS_QUAD) {
        for(int i=0;i<4;i++) edge[i]=tess_half_to_float(h[i])*scale;
        inside[0]=tess_half_to_float(h[4])*scale; inside[1]=tess_half_to_float(h[5])*scale;
    } else {
        for(int i=0;i<3;i++) edge[i]=tess_half_to_float(h[i])*scale;
        inside[0]=tess_half_to_float(h[3])*scale;
    }
}
#endif
