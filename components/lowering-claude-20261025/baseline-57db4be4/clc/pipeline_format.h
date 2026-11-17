/* Native compute pipeline wire formats. Little-endian words, then code.
 * Shared by compiler and native pipeline import. This validates layout and
 * supported resources, NOT shader instruction safety or address isolation.
 *
 * v1 (TGP1) is unchanged: 64-byte header, buffers only, dispatch wire 272
 * bytes.  v2 (TGP2) is additive and never reinterpreted as v1: it declares
 * the exact set-0 resources the compiled code reads/writes, so the transport
 * can require a complete bound-resource wire and pack descriptor tables.
 */
#ifndef TEGU_PIPELINE_FORMAT_H
#define TEGU_PIPELINE_FORMAT_H
#include <stdint.h>
#include <stddef.h>
#define TEGU_PIPELINE_MAGIC 0x31504754u /* TGP1 */
#define TEGU_PIPELINE_HEADER_BYTES 64u
#define TEGU_PIPELINE_MAX_CODE 4096u
/* TGP2 code is a separate ABI from the 4KiB TGP1 bound; real compute
 * kernels exceed 4KiB (the frozen compiler emitted 5760 bytes for a P1
 * CoreRE kernel).  Keep the 1MiB limit aligned with the compiler's SPIR-V
 * input bound. */
#define TEGU_PIPELINE2_MAX_CODE (1u<<20)
#include "nord2_target.h"
#define TEGU_PIPELINE_GPU NORD2_MALI_PIPELINE_ID
struct tegu_pipeline_info {
    uint32_t code_bytes, registers, push_words, bindings, local[3];
    uint64_t preload;
};
static inline uint32_t tegu_pipeline_word(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static inline void tegu_pipeline_put(unsigned char *p,uint32_t value)
{
    p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);
    p[2]=(unsigned char)(value>>16);p[3]=(unsigned char)(value>>24);
}
/* Exact-length parser accepts unaligned input. Leaves output untouched on error.
 * ABI1:8 bounded descriptors in FAU bytes0..127, application pushes128..255,
 * num_workgroups256..267. No TLS/WLS, descriptor arrays, or executable addresses.
 */
static inline int tegu_pipeline_parse(const void *bytes,size_t size,
                                     struct tegu_pipeline_info *out)
{
    if(!bytes || !out || size<TEGU_PIPELINE_HEADER_BYTES)return -1;
    const unsigned char *p=(const unsigned char *)bytes;
    uint32_t w[16];
    for(unsigned i=0;i<16;i++)w[i]=tegu_pipeline_word(p+4*i);
    if(w[0]!=TEGU_PIPELINE_MAGIC || w[1]!=1 || w[3]!=TEGU_PIPELINE_GPU ||
       !w[2] || (w[2]&7) || w[2]>TEGU_PIPELINE_MAX_CODE ||
       size!=TEGU_PIPELINE_HEADER_BYTES+(size_t)w[2] || !w[4] || w[4]>64 ||
       w[5]>128 || (w[8]&~255u) || w[12] || w[13] || w[14]!=1 || w[15])return -2;
    uint64_t preload=((uint64_t)w[7]<<32)|w[6];
    if(preload&~0x7f80000000000000ULL)return -3;
    uint32_t product=1;
    for(unsigned i=9;i<12;i++) {
        if(!w[i] || w[i]>256 || product>256/w[i])return -4;
        product*=w[i];
    }
    struct tegu_pipeline_info info={0};
    info.code_bytes=w[2];info.registers=w[4];info.push_words=w[5];
    info.preload=preload;info.bindings=w[8];
    for(unsigned i=0;i<3;i++)info.local[i]=w[i+9];
    *out=info;return 0;
}
/* Kernel-only submission: no user pointers or GPU addresses. All PAs have
 * been resolved from retained owned descriptors by the user-client. */
struct tegu_pipeline_job {
    struct tegu_pipeline_info info;
    uint64_t code_pa, pages[8];
    uint32_t offsets[8], lengths[8], groups[3], push[32];
};
#define TEGU_PIPELINE_DISPATCH_BYTES 272u
/* Wire dispatch: version, groupsXYZ,8x{handle,byteOffset,length,reserved},
 * then128bytes application push constants. Scalar input is pipeline handle. */

/* ---- v2: complete native resource manifest -------------------------------
 * Native descriptor identity is the SPIR-V (set,binding,array_element), never
 * a Metal slot. First v2 supports set0, array_element0, binding N in 0..255:
 * textures and storage images use resource-table entry table1[N], samplers
 * table2[N]; buffer bindings 0..7 use FAU+16*N. A samplerless texel fetch or
 * query uses the compiler-required internal dummy sampler at table2[256].
 * The manifest is authoritative: the transport must match the selected
 * bound-variant plan exactly before it writes a descriptor byte.
 */
#define TEGU_PIPELINE2_MAGIC 0x32504754u /* TGP2 */
#define TEGU_PIPELINE2_VERSION 2u
#define TEGU_PIPELINE2_HEADER_BYTES 128u
#define TEGU_PIPELINE2_MAX_MANIFEST 64u
#define TEGU_PIPELINE2_MANIFEST_BYTES 64u
#define TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY 256u
#define TEGU_PIPELINE2_MAX_BINDING 255u
#define TEGU_PIPELINE2_DUMMY_BINDING 0xffffffffu
#define TEGU_PIPELINE2_MAX_RESOURCES 32u
#define TEGU_PIPELINE2_MAX_TEXTURES 31u
#define TEGU_PIPELINE2_MAX_SAMPLERS 16u
#define TEGU_PIPELINE2_MAX_BUFFERS 8u
#define TEGU_PIPELINE2_MAX_REGIONS 8u
#define TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER 1u
#define TEGU_PIPELINE2_FLAG_DECLARED_COMBINED 2u
#define TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE 4u
#define TEGU_PIPELINE2_TABLE_FAU 0u
#define TEGU_PIPELINE2_TABLE_TEXTURE 1u
#define TEGU_PIPELINE2_TABLE_SAMPLER 2u
#define TEGU_PIPELINE2_KIND_UBO 1u
#define TEGU_PIPELINE2_KIND_SSBO 2u
#define TEGU_PIPELINE2_KIND_TEXTURE 3u
#define TEGU_PIPELINE2_KIND_IMAGE 4u
#define TEGU_PIPELINE2_KIND_SAMPLER 5u
/* Dispatch-wire only: a combined image/sampler entry covers manifest kind3
 * (table1[N]) plus kind5 (table2[N]) at the same native binding N. */
#define TEGU_PIPELINE2_KIND_COMBINED 6u
#define TEGU_PIPELINE2_ACCESS_READ 0x100u
#define TEGU_PIPELINE2_ACCESS_WRITE 0x200u
#define TEGU_PIPELINE2_ACCESS_SAMPLE 0x400u
#define TEGU_PIPELINE2_ACCESS_STORAGE 0x800u
#define TEGU_PIPELINE2_ACCESS_ATOMIC 0x1000u
#define TEGU_PIPELINE2_ACCESS_MASK 0x1f00u
/* Additive TDG2 descriptor flag: word SWIZZLE is an explicit component
 * mapping, including the valid all-zero (RRRR) mapping.  Without this flag a
 * nonzero SWIZZLE is still accepted as an implicit mapping for compatibility;
 * zero without the flag means the format's default swizzle.  Allowed only on
 * TEXTURE/COMBINED descriptors, where the mapping is observable. */
#define TEGU_PIPELINE2_DESC_SWIZZLE_VALID 0x2000u
/* Explicit shape domains: not GLSL/SPIR-V/Metal numeric values. */
#define TEGU_PIPELINE2_DIM_1D 0u
#define TEGU_PIPELINE2_DIM_2D 1u
#define TEGU_PIPELINE2_DIM_3D 2u
#define TEGU_PIPELINE2_DIM_CUBE 3u
#define TEGU_PIPELINE2_DIM_BUFFER 4u
#define TEGU_PIPELINE2_NUMERIC_FLOAT 0u
#define TEGU_PIPELINE2_NUMERIC_SINT 1u
#define TEGU_PIPELINE2_NUMERIC_UINT 2u
struct tegu_pipeline_manifest {
    uint32_t set, binding, kind, array_count;
    uint32_t access, table, entry, flags;
    uint32_t storage_format;
    uint32_t reserved_view[3];
    uint32_t shape, reserved[3];
};
#define TEGU_PIPELINE2_SHAPE_DIM_MASK 15u
#define TEGU_PIPELINE2_SHAPE_ARRAYED (1u<<4)
#define TEGU_PIPELINE2_SHAPE_MULTISAMPLE (1u<<5)
#define TEGU_PIPELINE2_SHAPE_DEPTH_COMPARE (1u<<6)
#define TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_SHIFT 7u
#define TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_MASK (3u<<7)
#define TEGU_PIPELINE2_SHAPE_NUMERIC(n) ((((uint32_t)(n))&3u)<<TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_SHIFT)
#if defined(__cplusplus)
static_assert(sizeof(struct tegu_pipeline_manifest)==TEGU_PIPELINE2_MANIFEST_BYTES,"TGP2 manifest record ABI");
#else
_Static_assert(sizeof(struct tegu_pipeline_manifest)==TEGU_PIPELINE2_MANIFEST_BYTES,"TGP2 manifest record ABI");
#endif
struct tegu_pipeline_info2 {
    uint32_t code_bytes, registers, push_words, buffer_mask, local[3];
    uint32_t manifest_offset, manifest_count, manifest_record_bytes, code_offset;
    uint32_t flags, reserved[3];
    uint64_t preload;
    struct tegu_pipeline_manifest manifest[TEGU_PIPELINE2_MAX_MANIFEST];
};
/* v2 package header word map: 0 magic, 1 version, 2 code_bytes, 3 GPU,
 * 4 registers, 5 push_words, 6..7 preload, 8 buffer_fau_mask, 9..11 local,
 * 12 manifest_offset, 13 manifest_count, 14 manifest_record_bytes,
 * 15 code_offset, 16 flags, 17..31 zero. Manifest at 128, code after it. */
static inline int tegu_pipeline2_shape_valid(uint32_t shape)
{
    uint32_t dim=shape&TEGU_PIPELINE2_SHAPE_DIM_MASK;
    uint32_t sampled=(shape&TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_MASK)>>
        TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_SHIFT;
    return dim<=4&&sampled<=2&&(shape&~0x1ffu)==0;
}
static inline int tegu_pipeline2_valid_access(uint32_t kind,uint32_t access)
{
    if(access&~TEGU_PIPELINE2_ACCESS_MASK)return 0;
    switch(kind) {
    case TEGU_PIPELINE2_KIND_UBO:
        return access==TEGU_PIPELINE2_ACCESS_READ;
    case TEGU_PIPELINE2_KIND_SSBO:
        if(!(access&(TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE)))return 0;
        if(access&(TEGU_PIPELINE2_ACCESS_SAMPLE|TEGU_PIPELINE2_ACCESS_STORAGE))return 0;
        if((access&TEGU_PIPELINE2_ACCESS_ATOMIC)&&
           (access&(TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE))!=(TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE))return 0;
        return 1;
    case TEGU_PIPELINE2_KIND_TEXTURE:
        return access==(TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE);
    case TEGU_PIPELINE2_KIND_IMAGE:
        if(!(access&TEGU_PIPELINE2_ACCESS_STORAGE))return 0;
        if(access&TEGU_PIPELINE2_ACCESS_SAMPLE)return 0;
        if(!(access&(TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC)))return 0;
        if((access&TEGU_PIPELINE2_ACCESS_ATOMIC)&&
           (access&(TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE))!=(TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE))return 0;
        return 1;
    case TEGU_PIPELINE2_KIND_SAMPLER:
        return access==0;
    default:return 0;
    }
}
/* Canonical Vulkan storage formats for SPIR-V OpTypeImage ImageFormat values
 * 1..41, as pinned by the compiler owner's source-derived map. 0 means
 * ImageFormatUnknown and is allowed only for IMAGE. */
static inline int tegu_pipeline2_storage_format_allowed(uint32_t format)
{
    static const uint32_t allowed[]={109,97,100,37,38,103,83,122,76,91,64,77,16,70,9,92,78,17,71,10,
        108,96,42,99,102,82,21,75,14,107,95,41,98,68,101,81,20,74,13,110,111};
    if(!format)return 1;
    for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)if(allowed[i]==format)return 1;
    return 0;
}
static inline uint32_t tegu_pipeline2_storage_class(uint32_t format)
{
    switch(format) {
    /* SPIR-V ImageFormat 1..20: float / normalized float */
    case 109:case 97:case 100:case 37:case 38:case 103:case 83:case 122:case 76:case 91:
    case 64:case 77:case 16:case 70:case 9:case 92:case 78:case 17:case 71:case 10:
        return TEGU_PIPELINE2_NUMERIC_FLOAT;
    /* ImageFormat 21..29 and 41: signed integer */
    case 108:case 96:case 42:case 99:case 102:case 82:case 21:case 75:case 14:case 111:
        return TEGU_PIPELINE2_NUMERIC_SINT;
    /* ImageFormat 30..40: unsigned integer */
    case 107:case 95:case 41:case 98:case 68:case 101:case 81:case 20:case 74:case 13:case 110:
        return TEGU_PIPELINE2_NUMERIC_UINT;
    default:return 0xffffffffu;
    }
}
static inline int tegu_pipeline2_shape_check(uint32_t kind,uint32_t shape,uint32_t storage_format)
{
    uint32_t dim=shape&TEGU_PIPELINE2_SHAPE_DIM_MASK;
    uint32_t numeric=(shape&TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_MASK)>>TEGU_PIPELINE2_SHAPE_SAMPLED_TYPE_SHIFT;
    uint32_t arrayed=(shape&TEGU_PIPELINE2_SHAPE_ARRAYED)!=0;
    uint32_t ms=(shape&TEGU_PIPELINE2_SHAPE_MULTISAMPLE)!=0;
    uint32_t depth=(shape&TEGU_PIPELINE2_SHAPE_DEPTH_COMPARE)!=0;
    if(shape&~0x1ffu)return 0;
    if(dim>TEGU_PIPELINE2_DIM_BUFFER||numeric>TEGU_PIPELINE2_NUMERIC_UINT)return 0;
    if(kind==TEGU_PIPELINE2_KIND_UBO||kind==TEGU_PIPELINE2_KIND_SSBO||kind==TEGU_PIPELINE2_KIND_SAMPLER)
        return shape==0&&storage_format==0;
    if(kind!=TEGU_PIPELINE2_KIND_TEXTURE&&kind!=TEGU_PIPELINE2_KIND_IMAGE)return 0;
    if(arrayed&&dim!=TEGU_PIPELINE2_DIM_1D&&dim!=TEGU_PIPELINE2_DIM_2D&&dim!=TEGU_PIPELINE2_DIM_CUBE)return 0;
    if(ms&&dim!=TEGU_PIPELINE2_DIM_2D)return 0;
    if(dim==TEGU_PIPELINE2_DIM_BUFFER&&(arrayed||ms||depth))return 0;
    if(depth&&(kind==TEGU_PIPELINE2_KIND_IMAGE||numeric!=TEGU_PIPELINE2_NUMERIC_FLOAT||
       dim==TEGU_PIPELINE2_DIM_3D||dim==TEGU_PIPELINE2_DIM_BUFFER||ms))return 0;
    if(kind==TEGU_PIPELINE2_KIND_IMAGE) {
        if(depth)return 0;
        if(storage_format&&tegu_pipeline2_storage_class(storage_format)!=numeric)return 0;
    } else if(storage_format)return 0;
    return 1;
}
/* Exact-length v2 parser. Output is unchanged on refusal. */
static inline int tegu_pipeline_parse2(const void *bytes,size_t size,
                                       struct tegu_pipeline_info2 *out)
{
    if(!bytes||!out||size<TEGU_PIPELINE2_HEADER_BYTES)return -1;
    const unsigned char *p=(const unsigned char *)bytes;
    uint32_t w[32];
    for(unsigned i=0;i<32;i++)w[i]=tegu_pipeline_word(p+4*i);
    if(w[0]!=TEGU_PIPELINE2_MAGIC||w[1]!=TEGU_PIPELINE2_VERSION||w[3]!=TEGU_PIPELINE_GPU||
       !w[2]||(w[2]&7)||w[2]>TEGU_PIPELINE2_MAX_CODE||!w[4]||w[4]>64||w[5]>128||
       (w[8]&~255u)||w[12]!=TEGU_PIPELINE2_HEADER_BYTES||
       w[13]>TEGU_PIPELINE2_MAX_MANIFEST||w[14]!=TEGU_PIPELINE2_MANIFEST_BYTES||
       (w[16]&~TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER)||
       w[17]||w[18]||w[19]||w[20]||w[21]||w[22]||w[23]||w[24]||
       w[25]||w[26]||w[27]||w[28]||w[29]||w[30]||w[31])return -2;
    uint64_t preload=((uint64_t)w[7]<<32)|w[6];
    if(preload&~0x7f80000000000000ULL)return -3;
    uint32_t product=1;
    for(unsigned i=9;i<12;i++) {
        if(!w[i]||w[i]>256||product>256/w[i])return -4;
        product*=w[i];
    }
    uint64_t manifest_bytes=(uint64_t)w[13]*TEGU_PIPELINE2_MANIFEST_BYTES;
    uint64_t code_offset=TEGU_PIPELINE2_HEADER_BYTES+manifest_bytes;
    if(code_offset!=w[15]||(code_offset&7u)||code_offset>size||
       (uint64_t)w[2]!=size-code_offset)return -5;
    struct tegu_pipeline_info2 info={0};
    info.code_bytes=w[2];info.registers=w[4];info.push_words=w[5];info.preload=preload;
    info.buffer_mask=w[8];info.manifest_offset=w[12];info.manifest_count=w[13];
    info.manifest_record_bytes=w[14];info.code_offset=w[15];info.flags=w[16];
    for(unsigned i=0;i<3;i++)info.local[i]=w[9+i];
    uint32_t buffer_mask=0,dummy=0;
    for(unsigned i=0;i<info.manifest_count;i++) {
        const unsigned char *r=p+TEGU_PIPELINE2_HEADER_BYTES+i*TEGU_PIPELINE2_MANIFEST_BYTES;
        struct tegu_pipeline_manifest m={0};
        m.set=tegu_pipeline_word(r);m.binding=tegu_pipeline_word(r+4);
        m.kind=tegu_pipeline_word(r+8);m.array_count=tegu_pipeline_word(r+12);
        m.access=tegu_pipeline_word(r+16);m.table=tegu_pipeline_word(r+20);
        m.entry=tegu_pipeline_word(r+24);m.flags=tegu_pipeline_word(r+28);
        m.storage_format=tegu_pipeline_word(r+32);
        for(unsigned k=0;k<3;k++)m.reserved_view[k]=tegu_pipeline_word(r+36+4*k);
        m.shape=tegu_pipeline_word(r+48);
        for(unsigned k=0;k<3;k++)m.reserved[k]=tegu_pipeline_word(r+52+4*k);
        if(m.set||m.array_count!=1||
           (m.flags&~(TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER|TEGU_PIPELINE2_FLAG_DECLARED_COMBINED|
                      TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE))||
           m.reserved_view[0]||m.reserved_view[1]||m.reserved_view[2]||
           m.reserved[0]||m.reserved[1]||m.reserved[2])return -6;
        if(!tegu_pipeline2_shape_check(m.kind,m.shape,m.storage_format))return -6;
        if((m.flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER)!=0) {
            if(dummy||m.kind!=TEGU_PIPELINE2_KIND_SAMPLER||m.binding!=TEGU_PIPELINE2_DUMMY_BINDING||
               m.table!=TEGU_PIPELINE2_TABLE_SAMPLER||m.entry!=TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY||
               m.access!=0||m.flags!=TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER)return -7;
            dummy=1;
        } else {
            if(m.binding>TEGU_PIPELINE2_MAX_BINDING)return -8;
            if(!tegu_pipeline2_valid_access(m.kind,m.access))return -9;
            if(m.kind==TEGU_PIPELINE2_KIND_UBO||m.kind==TEGU_PIPELINE2_KIND_SSBO) {
                if(m.flags||m.shape||m.storage_format)return -9;
            } else if(m.kind==TEGU_PIPELINE2_KIND_IMAGE) {
                if(m.flags)return -9;
                if(!tegu_pipeline2_storage_format_allowed(m.storage_format))return -9;
            } else if(m.kind==TEGU_PIPELINE2_KIND_TEXTURE) {
                if(m.storage_format||(m.flags&~(TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)))return -9;
            } else if(m.kind==TEGU_PIPELINE2_KIND_SAMPLER) {
                if(m.storage_format||(m.flags&~(TEGU_PIPELINE2_FLAG_DECLARED_COMBINED|
                   TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE)))return -9;
            }
            switch(m.kind) {
            case TEGU_PIPELINE2_KIND_UBO:case TEGU_PIPELINE2_KIND_SSBO:
                if(m.table!=TEGU_PIPELINE2_TABLE_FAU||m.entry!=m.binding||m.binding>=8)return -10;
                buffer_mask|=1u<<m.binding;break;
            case TEGU_PIPELINE2_KIND_TEXTURE:case TEGU_PIPELINE2_KIND_IMAGE:
                if(m.table!=TEGU_PIPELINE2_TABLE_TEXTURE||m.entry!=m.binding)return -11;
                break;
            case TEGU_PIPELINE2_KIND_SAMPLER:
                if(m.table!=TEGU_PIPELINE2_TABLE_SAMPLER||m.entry!=m.binding)return -12;
                break;
            default:return -9;
            }
        }
        for(unsigned j=0;j<i;j++) {
            const struct tegu_pipeline_manifest *q=&info.manifest[j];
            if(q->table==m.table&&q->entry==m.entry)return -13;
            if(q->binding==m.binding&&q->binding!=TEGU_PIPELINE2_DUMMY_BINDING) {
                /* Only a genuine combined texture+sampler pair may share a
                 * native binding; a buffer/image with anything else is not a
                 * combined Vulkan descriptor. */
                int pair=((q->table==TEGU_PIPELINE2_TABLE_TEXTURE&&q->kind==TEGU_PIPELINE2_KIND_TEXTURE&&
                           q->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED&&
                           m.table==TEGU_PIPELINE2_TABLE_SAMPLER&&m.kind==TEGU_PIPELINE2_KIND_SAMPLER&&
                           m.flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||
                          (q->table==TEGU_PIPELINE2_TABLE_SAMPLER&&q->kind==TEGU_PIPELINE2_KIND_SAMPLER&&
                           q->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED&&
                           m.table==TEGU_PIPELINE2_TABLE_TEXTURE&&m.kind==TEGU_PIPELINE2_KIND_TEXTURE&&
                           m.flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED));
                if(!pair)return -17;
            }
        }
        info.manifest[i]=m;
    }
    if(buffer_mask!=info.buffer_mask)return -15;
    if((info.flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER)!=dummy)return -16;
    for(unsigned i=0;i<info.manifest_count;i++) {
        const struct tegu_pipeline_manifest *m=&info.manifest[i];
        if(!(m->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED))continue;
        unsigned want=m->kind==TEGU_PIPELINE2_KIND_TEXTURE?TEGU_PIPELINE2_KIND_SAMPLER:
                     m->kind==TEGU_PIPELINE2_KIND_SAMPLER?TEGU_PIPELINE2_KIND_TEXTURE:0;
        if(!want)return -17;
        unsigned found=0;
        for(unsigned j=0;j<info.manifest_count;j++) {
            const struct tegu_pipeline_manifest *q=&info.manifest[j];
            if(q->binding==m->binding&&q->kind==want&&
               (q->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED))found=1;
        }
        if(!found)return -17;
    }
    *out=info;return 0;
}
static inline uint32_t tegu_pipeline_package_version(const void *bytes,size_t size)
{
    if(!bytes||size<4)return 0;
    uint32_t magic=tegu_pipeline_word((const unsigned char *)bytes);
    return magic==TEGU_PIPELINE_MAGIC?1u:magic==TEGU_PIPELINE2_MAGIC?2u:0u;
}
/* Version-dispatching parser. `v1` is always filled with the fields the older
 * importer needs; v2 callers also receive the complete manifest. v2
 * information is zero for a v1 package and vice versa. */
static inline int tegu_pipeline_parse_any(const void *bytes,size_t size,
 uint32_t *version,struct tegu_pipeline_info *v1,struct tegu_pipeline_info2 *v2)
{
    if(!version||!v1||!v2)return -1;
    uint32_t found=tegu_pipeline_package_version(bytes,size);
    struct tegu_pipeline_info a={0};struct tegu_pipeline_info2 b={0};
    int result;
    if(found==1)result=tegu_pipeline_parse(bytes,size,&a);
    else if(found==2)result=tegu_pipeline_parse2(bytes,size,&b);
    else return -2;
    if(result)return result;
    if(found==2) {
        a.code_bytes=b.code_bytes;a.registers=b.registers;a.push_words=b.push_words;
        a.preload=b.preload;a.bindings=b.buffer_mask;
        for(unsigned i=0;i<3;i++)a.local[i]=b.local[i];
    }
    *version=found;*v1=a;*v2=b;return 0;
}

/* v2 dispatch wire. One wire is one COMPUTE_JOB; an exact dispatchThreads
 * region becomes one wire/job with its own groups, push bytes and compiled
 * specialization. `binding` is the native SPIR-V binding N (the table entry
 * for textures/samplers); `metal_index` is planner identity only and must
 * never be used as a native table index. Every descriptor is matched against
 * the package manifest before any descriptor byte is produced. */
#define TEGU_PIPELINE_DISPATCH2_MAGIC 0x32474454u /* TDG2 */
#define TEGU_PIPELINE_DISPATCH2_VERSION 2u
#define TEGU_PIPELINE_DISPATCH2_DESC_BYTES 192u
#define TEGU_PIPELINE_DISPATCH2_DESC_OFFSET 32u
#define TEGU_PIPELINE_DISPATCH2_PUSH_OFFSET \
    (TEGU_PIPELINE_DISPATCH2_DESC_OFFSET + TEGU_PIPELINE2_MAX_RESOURCES * TEGU_PIPELINE_DISPATCH2_DESC_BYTES)
#define TEGU_PIPELINE_DISPATCH2_BYTES (TEGU_PIPELINE_DISPATCH2_PUSH_OFFSET + 128u)
struct tegu_pipeline_dispatch2_header {
    uint32_t magic, version, descriptor_count, groups[3], push_words, reserved;
};
/* Descriptor wire fields, by byte offset from the descriptor start. */
enum {
    TEGU_PIPELINE_D2_BINDING=0, TEGU_PIPELINE_D2_KIND=4,
    TEGU_PIPELINE_D2_METAL_INDEX=8, TEGU_PIPELINE_D2_FLAGS=12,
    TEGU_PIPELINE_D2_TOKEN=16, TEGU_PIPELINE_D2_OFFSET=24, TEGU_PIPELINE_D2_SPAN=32,
    TEGU_PIPELINE_D2_FORMAT=40, TEGU_PIPELINE_D2_VIEW_FORMAT=44,
    TEGU_PIPELINE_D2_DIM=48, TEGU_PIPELINE_D2_WIDTH=52, TEGU_PIPELINE_D2_HEIGHT=56,
    TEGU_PIPELINE_D2_DEPTH=60, TEGU_PIPELINE_D2_LAYERS=64, TEGU_PIPELINE_D2_LEVELS=68,
    TEGU_PIPELINE_D2_SAMPLES=72, TEGU_PIPELINE_D2_USAGE=76, TEGU_PIPELINE_D2_ROW_STRIDE=80,
    TEGU_PIPELINE_D2_STORAGE_MODE=84, TEGU_PIPELINE_D2_SAMPLER_INDEX=88,
    TEGU_PIPELINE_D2_SAMPLER_FLAGS=92, TEGU_PIPELINE_D2_LEVEL_OFFSET=96,
    TEGU_PIPELINE_D2_SLICE_STRIDE=104, TEGU_PIPELINE_D2_LAYER_STRIDE=108,
    TEGU_PIPELINE_D2_MIN_FILTER=112, TEGU_PIPELINE_D2_MAG_FILTER=116,
    TEGU_PIPELINE_D2_MIP_FILTER=120, TEGU_PIPELINE_D2_ADDRESS_S=124,
    TEGU_PIPELINE_D2_ADDRESS_T=128, TEGU_PIPELINE_D2_ADDRESS_R=132,
    TEGU_PIPELINE_D2_BORDER_COLOR=136, TEGU_PIPELINE_D2_NORMALIZED=140,
    TEGU_PIPELINE_D2_COMPARE=144, TEGU_PIPELINE_D2_MAX_ANISOTROPY=148,
    TEGU_PIPELINE_D2_REDUCTION=152, TEGU_PIPELINE_D2_LOD_AVERAGE=156,
    TEGU_PIPELINE_D2_LOD_MIN=160, TEGU_PIPELINE_D2_LOD_MAX=164,
    TEGU_PIPELINE_D2_VIEW_LEVEL=168, TEGU_PIPELINE_D2_VIEW_LAYER=172,
    TEGU_PIPELINE_D2_SWIZZLE=176, TEGU_PIPELINE_D2_VIEW_FORMAT_VK=180
};
#endif
