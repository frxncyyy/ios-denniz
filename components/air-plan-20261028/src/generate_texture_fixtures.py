#!/usr/bin/env python3
"""Writes the authored texture fixtures (fixtures/tex_*.spvasm). One accepted
handwritten FS plus negatives that each differ in one property."""
import os, sys
FIX = sys.argv[2]
T = """; SPIR-V
; Version: 1.0
; Authored bounded texture compiler fixture: {note}
               OpCapability Shader
{caps}               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %main "main" %in_color %out_color
               OpExecutionMode %main OriginUpperLeft
               OpDecorate %in_color Location 0
               OpDecorate %out_color Location 0
               OpDecorate %tex DescriptorSet {set}
               OpDecorate %tex Binding {binding}
{deco}       %void = OpTypeVoid
         %fn = OpTypeFunction %void
      %float = OpTypeFloat 32
        %v2f = OpTypeVector %float 2
        %v3f = OpTypeVector %float 3
        %v4f = OpTypeVector %float 4
       %uint = OpTypeInt 32 0
        %int = OpTypeInt 32 1
        %v2i = OpTypeVector %int 2
        %v4u = OpTypeVector %uint 4
         %f0 = OpConstant %float 0
         %f1 = OpConstant %float 1
         %i0 = OpConstant %int 0
         %i1 = OpConstant %int 1
        %off = OpConstantComposite %v2i %i1 %i0
        %dz2 = OpConstantComposite %v2f %f0 %f0
        %img = OpTypeImage {stype} {dim} {depth} {arrayed} {ms} 1 Unknown
       %simg = OpTypeSampledImage %img
    %ptr_tex = OpTypePointer UniformConstant %simg
        %tex = OpVariable %ptr_tex UniformConstant
     %ptr_in = OpTypePointer Input %v4f
    %ptr_out = OpTypePointer Output %v4f
   %in_color = OpVariable %ptr_in Input
  %out_color = OpVariable %ptr_out Output
{decl}       %main = OpFunction %void None %fn
      %entry = OpLabel
          %c = OpLoad %v4f %in_color
         %uv = OpVectorShuffle %v2f %c %c 0 1
        %uvw = OpVectorShuffle %v3f %c %c 0 1 2
         %si = OpLoad %simg %tex
{body}               OpStore %out_color %r
               OpReturn
               OpFunctionEnd
"""
D = dict(caps="", set=0, binding=0, deco="", stype="%float", dim="2D", depth=0, arrayed=0, ms=0, decl="")
def L(*lines):
    return "".join("               %s\n" % l for l in lines)
SAMPLE = L("%r = OpImageSampleExplicitLod %v4f %si %uv Lod %f0")
CASES = {
 "tex_ok": ("accepted: sampler2D set0 binding0, explicit Lod 0.0", {}, SAMPLE),
 "tex_set1": ("DescriptorSet 1", {"set": 1}, SAMPLE),
 "tex_binding1": ("Binding 1", {"binding": 1}, SAMPLE),
 "tex_implicit": ("implicit-LOD sample", {}, L("%r = OpImageSampleImplicitLod %v4f %si %uv")),
 "tex_lod1": ("explicit Lod 1.0", {}, L("%r = OpImageSampleExplicitLod %v4f %si %uv Lod %f1")),
 "tex_lod_dynamic": ("Lod from varying", {}, L("%l = OpCompositeExtract %float %c 2",
                     "%r = OpImageSampleExplicitLod %v4f %si %uv Lod %l")),
 "tex_offset": ("ConstOffset", {}, L("%r = OpImageSampleExplicitLod %v4f %si %uv Lod|ConstOffset %f0 %off")),
 "tex_offset_zero": ("ConstOffset (0,0): folded away by Mesa, refused as authored", {"decl": L("%zoff = OpConstantComposite %v2i %i0 %i0")},
     L("%r = OpImageSampleExplicitLod %v4f %si %uv Lod|ConstOffset %f0 %zoff")),
 "tex_grad": ("Grad with zero derivatives: folded to txl by Mesa, refused as authored", {}, L("%r = OpImageSampleExplicitLod %v4f %si %uv Grad %dz2 %dz2")),
 "tex_bias": ("Bias", {}, L("%r = OpImageSampleImplicitLod %v4f %si %uv Bias %f1")),
 "tex_minlod": ("MinLod", {"caps": L("OpCapability MinLod")}, L("%r = OpImageSampleImplicitLod %v4f %si %uv MinLod %f1")),
 "tex_gather": ("gather", {}, L("%r = OpImageGather %v4f %si %uv %i0")),
 "tex_fetch": ("texel fetch (read)", {}, L("%im = OpImage %img %si", "%r = OpImageFetch %v4f %im %off Lod %i0")),
 "tex_query_live": ("live ImageQuerySizeLod", {"caps": L("OpCapability ImageQuery")},
     L("%im = OpImage %img %si", "%sz = OpImageQuerySizeLod %v2i %im %i0", "%szf = OpConvertSToF %v2f %sz",
       "%s2 = OpFDiv %v2f %uv %szf", "%r = OpImageSampleExplicitLod %v4f %si %s2 Lod %f0")),
 "tex_query_levels": ("live ImageQueryLevels", {"caps": L("OpCapability ImageQuery")},
     L("%im = OpImage %img %si", "%nl = OpImageQueryLevels %int %im", "%nf = OpConvertSToF %float %nl",
       "%s0 = OpImageSampleExplicitLod %v4f %si %uv Lod %f0", "%r = OpVectorTimesScalar %v4f %s0 %nf")),
 "tex_compare": ("depth compare", {"depth": 1},
     L("%d = OpImageSampleDrefExplicitLod %float %si %uv %f0 Lod %f0", "%r = OpCompositeConstruct %v4f %d %d %d %d")),
 "tex_array": ("2D array", {"arrayed": 1}, L("%r = OpImageSampleExplicitLod %v4f %si %uvw Lod %f0")),
 "tex_3d": ("3D", {"dim": "3D"}, L("%r = OpImageSampleExplicitLod %v4f %si %uvw Lod %f0")),
 "tex_cube": ("cube", {"dim": "Cube"}, L("%r = OpImageSampleExplicitLod %v4f %si %uvw Lod %f0")),
 "tex_msaa": ("multisampled fetch", {"ms": 1}, L("%im = OpImage %img %si", "%r = OpImageFetch %v4f %im %off Sample %i0")),
 "tex_uint": ("usampler2D", {"stype": "%uint"},
     L("%u = OpImageSampleExplicitLod %v4u %si %uv Lod %f0", "%r = OpConvertUToF %v4f %u")),
 "tex_two_samples": ("two samples of one texture", {},
     L("%a = OpImageSampleExplicitLod %v4f %si %uv Lod %f0", "%b = OpImageSampleExplicitLod %v4f %si %dz2 Lod %f0",
       "%r = OpFAdd %v4f %a %b")),
 "tex_unused": ("texture declared but never sampled", {}, L("%r = OpCopyObject %v4f %c")),
 "tex_two_bindings": ("second combined sampler at binding1",
     {"deco": L("OpDecorate %tex2 DescriptorSet 0", "OpDecorate %tex2 Binding 1"),
      "decl": L("%tex2 = OpVariable %ptr_tex UniformConstant")},
     L("%si2 = OpLoad %simg %tex2", "%a = OpImageSampleExplicitLod %v4f %si %uv Lod %f0",
       "%b = OpImageSampleExplicitLod %v4f %si2 %uv Lod %f0", "%r = OpFAdd %v4f %a %b")),
 "tex_plus_ubo": ("texture plus uniform buffer",
     {"deco": L("OpDecorate %Blk Block", "OpMemberDecorate %Blk 0 Offset 0", "OpDecorate %ubo DescriptorSet 0",
                "OpDecorate %ubo Binding 1"),
      "decl": L("%Blk = OpTypeStruct %v4f", "%pubo = OpTypePointer Uniform %Blk", "%pum = OpTypePointer Uniform %v4f",
                "%ubo = OpVariable %pubo Uniform")},
     L("%up = OpAccessChain %pum %ubo %i0", "%u = OpLoad %v4f %up", "%a = OpImageSampleExplicitLod %v4f %si %uv Lod %f0",
       "%r = OpFAdd %v4f %a %u")),
 "tex_plus_storage": ("texture plus storage image read",
     {"deco": L("OpDecorate %st DescriptorSet 0", "OpDecorate %st Binding 1"),
      "decl": L("%simgs = OpTypeImage %float 2D 0 0 0 2 Rgba8", "%psto = OpTypePointer UniformConstant %simgs",
                "%st = OpVariable %psto UniformConstant")},
     L("%sv = OpLoad %simgs %st", "%u = OpImageRead %v4f %sv %off", "%a = OpImageSampleExplicitLod %v4f %si %uv Lod %f0",
       "%r = OpFAdd %v4f %a %u")),
 "tex_separate": ("separate texture and sampler objects (set0 binding0 texture, binding1 sampler)",
     {"deco": L("OpDecorate %smp DescriptorSet 0", "OpDecorate %smp Binding 1", "OpDecorate %timg DescriptorSet 0",
                "OpDecorate %timg Binding 0"),
      "decl": L("%styp = OpTypeSampler", "%psmp = OpTypePointer UniformConstant %styp", "%smp = OpVariable %psmp UniformConstant",
                "%pimg = OpTypePointer UniformConstant %img", "%timg = OpVariable %pimg UniformConstant")},
     L("%ti = OpLoad %img %timg", "%sv = OpLoad %styp %smp", "%cs = OpSampledImage %simg %ti %sv",
       "%r = OpImageSampleExplicitLod %v4f %cs %uv Lod %f0")),
}
VS_TEX = """; SPIR-V
; Version: 1.0
; Authored bounded texture compiler fixture: vertex stage samples a texture (VS must stay resource free)
               OpCapability Shader
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %main "main" %in_pos %in_color %out_pos %out_color
               OpDecorate %in_pos Location 0
               OpDecorate %in_color Location 1
               OpDecorate %out_pos BuiltIn Position
               OpDecorate %out_color Location 0
               OpDecorate %tex DescriptorSet 0
               OpDecorate %tex Binding 0
       %void = OpTypeVoid
         %fn = OpTypeFunction %void
      %float = OpTypeFloat 32
        %v2f = OpTypeVector %float 2
        %v4f = OpTypeVector %float 4
         %f0 = OpConstant %float 0
        %img = OpTypeImage %float 2D 0 0 0 1 Unknown
       %simg = OpTypeSampledImage %img
    %ptr_tex = OpTypePointer UniformConstant %simg
        %tex = OpVariable %ptr_tex UniformConstant
     %ptr_in = OpTypePointer Input %v4f
    %ptr_out = OpTypePointer Output %v4f
     %in_pos = OpVariable %ptr_in Input
   %in_color = OpVariable %ptr_in Input
    %out_pos = OpVariable %ptr_out Output
  %out_color = OpVariable %ptr_out Output
       %main = OpFunction %void None %fn
      %entry = OpLabel
          %p = OpLoad %v4f %in_pos
          %c = OpLoad %v4f %in_color
         %uv = OpVectorShuffle %v2f %c %c 0 1
         %si = OpLoad %simg %tex
          %t = OpImageSampleExplicitLod %v4f %si %uv Lod %f0
               OpStore %out_pos %p
               OpStore %out_color %t
               OpReturn
               OpFunctionEnd
"""
for name, (note, over, body) in CASES.items():
    d = dict(D, **over, note=note, body=body)
    with open(os.path.join(FIX, name + ".spvasm"), "w") as f:
        f.write(T.format(**d))
with open(os.path.join(FIX, "tex_vs_sampling.spvasm"), "w") as f:
    f.write(VS_TEX)
# Actual AIR FS with only the SpecId0 DEFAULT changed to 7 (unnormalized + pixel
# rounding): the ImageQuerySizeLod stays live and must be refused. The AIR file
# itself is never edited; this is a separate negative fixture.
src = open(sys.argv[1]).read()
assert src.count("%16 = OpSpecConstant %2 0\n") == 1
with open(os.path.join(FIX, "air_spec7_unnormalized.spvasm"), "w") as f:
    f.write(src.replace("%16 = OpSpecConstant %2 0\n", "%16 = OpSpecConstant %2 7\n"))
# Unmodified disassembly of the actual AIR FS: must reassemble byte-identically.
with open(os.path.join(FIX, "air_fs_reassembled.spvasm"), "w") as f:
    f.write(src)
