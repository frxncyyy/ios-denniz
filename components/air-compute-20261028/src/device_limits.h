/* Donor iOS16 _MTLDevice limits ABI. Runtime encoding/span must match before
 * writing the superclass ivar; no hardcoded object offset or Apple GPU profile.
 * These are implemented compute/storage limits, not target hardware maxima.
 */
#ifndef TEGU_DEVICE_LIMITS_H
#define TEGU_DEVICE_LIMITS_H
#include <stddef.h>
#include <stdint.h>
#include "device_render_query_caps.h"
struct tegu_device_limits {
    unsigned int maxFramebufferStorageBits;
    unsigned int linearTextureArrayAlignmentBytes;
    unsigned int linearTextureArrayAlignmentSlice;
    unsigned int maxTileBuffers;
    unsigned int maxTileTextures;
    unsigned int maxTileSamplers;
    unsigned int maxTileInlineDataSize;
    unsigned int minTilePixels;
    unsigned int maxColorAttachments;
    unsigned int maxVertexAttributes;
    unsigned int maxVertexBuffers;
    unsigned int maxVertexTextures;
    unsigned int maxVertexSamplers;
    unsigned int maxVertexInlineDataSize;
    unsigned int maxInterpolants;
    unsigned int maxFragmentBuffers;
    unsigned int maxFragmentTextures;
    unsigned int maxFragmentSamplers;
    unsigned int maxFragmentInlineDataSize;
    unsigned int maxComputeBuffers;
    unsigned int maxComputeTextures;
    unsigned int maxComputeSamplers;
    unsigned int maxComputeInlineDataSize;
    unsigned int maxComputeLocalMemorySizes;
    unsigned int maxTotalComputeThreadsPerThreadgroup;
    unsigned int maxComputeThreadgroupMemory;
    float maxLineWidth;
    float maxPointSize;
    unsigned int maxVisibilityQueryOffset;
    unsigned int padmaxBufferLength;
    unsigned int minConstantBufferAlignmentBytes;
    unsigned int minBufferNoCopyAlignmentBytes;
    unsigned int maxTextureWidth1D;
    unsigned int maxTextureWidth2D;
    unsigned int maxTextureHeight2D;
    unsigned int maxTextureWidth3D;
    unsigned int maxTextureHeight3D;
    unsigned int maxTextureDepth3D;
    unsigned int maxTextureDimensionCube;
    unsigned int maxTextureLayers;
    unsigned int linearTextureAlignmentBytes;
    unsigned int iosurfaceTextureAlignmentBytes;
    unsigned int iosurfaceReadOnlyTextureAlignmentBytes;
    unsigned int deviceLinearTextureAlignmentBytes;
    unsigned int deviceLinearReadOnlyTextureAlignmentBytes;
    unsigned int maxFunctionConstantIndices;
    unsigned int maxComputeThreadgroupMemoryAlignmentBytes;
    unsigned int maxInterpolatedComponents;
    unsigned int maxTessellationFactor;
    unsigned int maxIndirectBuffers;
    unsigned int maxIndirectTextures;
    unsigned int maxIndirectSamplers;
    unsigned int maxIndirectSamplersPerDevice;
    unsigned int maxFenceInstances;
    unsigned int maxViewportCount;
    unsigned int maxCustomSamplePositions;
    unsigned int maxVertexAmplificationFactor;
    unsigned int maxVertexAmplificationCount;
    unsigned int maxTextureBufferWidth;
    unsigned int maxComputeAttributes;
    unsigned int maxIOCommandsInFlight;
    unsigned int maxPredicatedNestingDepth;
    unsigned int maxConstantBufferArguments;
    unsigned long long maxBufferLength;
};
_Static_assert(sizeof(struct tegu_device_limits)==264,"donor limits size");
_Static_assert(offsetof(struct tegu_device_limits,maxBufferLength)==256,"donor buffer limit");
/* Accept optional quoted field names, but no nested/unknown field types.
 * The donor has 63 four-byte scalars (26/27 float) and one uint64 scalar. */
static inline int tegu_limits_encoding(const char *s)
{
    if(!s || *s++!='{')return 0;
    while(*s && *s!='=') {if(*s=='{' || *s=='}')return 0;s++;}
    if(*s++!='=')return 0;
    for(unsigned i=0;i<64;i++) {
        if(*s=='"') {s++;while(*s && *s!='"')s++;if(*s++!='"')return 0;}
        char expected=i==63?'Q':(i==26||i==27?'f':'I');
        if(*s++!=expected)return 0;
    }
    return s[0]=='}' && s[1]==0;
}
static inline int tegu_limits_span(const char *encoding, long offset,
                                  long next_offset, size_t instance_size)
{
    return tegu_limits_encoding(encoding) && offset>=(long)sizeof(void *) &&
        (size_t)offset%_Alignof(struct tegu_device_limits)==0 &&
        (size_t)offset<=instance_size &&
        sizeof(struct tegu_device_limits)<=instance_size-(size_t)offset &&
        next_offset>=offset &&
        (size_t)(next_offset-offset)==sizeof(struct tegu_device_limits);
}
static inline struct tegu_device_limits tegu_compute_limits(void)
{
    struct tegu_device_limits limits={0};
#ifdef TEGU_GRAPHICS_BRIDGE
    /* Exact index/count envelope of the qualified compositor and reflection-
     * driven TGJ1 paths: BGRA8/RGBA8/R8/RGBA16F, 1x sampling, up to four
     * bound color targets, 4 active vertex attributes, buffer indices through VS3/FS6,
     * texture index3 and sampler index0 in the captured compositor class.
     * The generic path admits vertex texture/sampler slots 0..7 (one VS
     * combined descriptor maps to one native binding); published fragment
     * values remain the qualified compositor envelope. One TGJ1 job still has
     * at most 16 native bindings including buffers, so these per-stage maxima
     * are not simultaneously reachable with buffers.
     * This is not an Apple GPU family claim. */
    limits.maxColorAttachments=4;
    limits.maxVertexAttributes=4;
    limits.maxVertexBuffers=4;
    limits.maxVertexTextures=8;
    limits.maxVertexSamplers=8;
    limits.maxVertexInlineDataSize=4096;
    limits.maxFragmentBuffers=7;
    limits.maxFragmentTextures=4;
    limits.maxFragmentSamplers=1;
    limits.maxFragmentInlineDataSize=4096;
    /* These are the implemented allocation minima, not Mali hardware maxima:
     * R52 has exact 4096x1 shared textures and R56 has exact 64^3 R8 storage.
     * The general sampled/bound path is 2D single-level; 1D, 3D and MSAA
     * creation is limited to those exact allocation classes. */
    limits.maxTextureWidth1D=4096;
    limits.maxTextureWidth2D=4096;
    limits.maxTextureHeight2D=4096;
    limits.maxTextureWidth3D=64;
    limits.maxTextureHeight3D=64;
    limits.maxTextureDepth3D=64;
#endif
    limits.maxComputeBuffers=8; /* native descriptor slots */
    limits.maxTotalComputeThreadsPerThreadgroup=256; /* TGP1 validator */
    limits.minConstantBufferAlignmentBytes=16; /* native binding offsets */
    limits.minBufferNoCopyAlignmentBytes=16384;
    limits.maxBufferLength=16ULL<<20; /* allocation, not GPU binding span */
    /* Tile, vertex/fragment inline data, unsupported shader stages, indirect,
     * threadgroup-memory and setBytes fields stay zero. */
    return limits;
}
#endif
