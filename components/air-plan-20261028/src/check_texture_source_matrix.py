#!/usr/bin/env python3
"""Enumerate the pinned NIR source/op contract against production C checks.

Compiles a host-only test translation unit that includes the production bridge
and reuses the existing Mesa archive objects. No Mesa/native rebuild, device,
or Apple input. Unknown enum additions require an explicit test classification.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

CORE = {'coord','comparator','offset','bias','lod','min_lod','ms_index','ddx','ddy',
        'texture_offset','sampler_offset','texture_handle','sampler_handle'}
LOWER = {'projector','texture_deref','sampler_deref'}
REFUSE = {'max_lod_kk','lod_bias_min_agx','ms_mcs_intel','texture_heap_offset','sampler_heap_offset',
          'sampler_deref_intrinsic','texture_deref_intrinsic','plane','backend1','backend2','ref_coord',
          'texture_2_deref','sampler_2_deref','texture_2_handle','sampler_2_handle','block_size','box_size'}
OPS = {'tex','txb','txl','txd','txf','txf_ms','tg4','lod','txs','query_levels','texture_samples'}
REJECT_OPS = {'txf_ms_fb','txf_ms_mcs_intel','samples_identical','tex_prefetch','lod_bias',
              'fragment_fetch_amd','fragment_mask_fetch_amd','descriptor_amd','sampler_descriptor_amd',
              'image_min_lod_agx','has_custom_border_color_agx','custom_border_color_agx','hdr_dim_nv',
              'tex_type_nv','sample_pos_nv','gradient_pan','sample_weighted_qcom','box_filter_qcom',
              'block_match_sad_qcom','block_match_ssd_qcom','resinfo_intel','sparse_residency_intel',
              'sparse_residency_txf_intel'}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mesa-source',type=Path,required=True)
    p.add_argument('--mesa-build',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();source=a.mesa_source.resolve();build=a.mesa_build.resolve()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    production=Path(__file__).resolve().with_name('mali_graphics_compiler.c')
    nir=(source/'src/compiler/nir/nir.h').read_text()
    def enum(prefix,name):
        part=nir.split('typedef enum '+name+' {',1)[1].split('} '+name+';',1)[0]
        part=re.sub(r'/\*.*?\*/','',part,flags=re.S)
        return re.findall(r'\b'+prefix+r'([A-Za-z0-9_]+)\b',part)
    sources=enum('nir_tex_src_','nir_tex_src_type');ops=enum('nir_texop_','nir_texop')
    assert set(sources)==CORE|LOWER|REFUSE, set(sources)^(CORE|LOWER|REFUSE)
    assert set(ops)==OPS|REJECT_OPS, set(ops)^(OPS|REJECT_OPS)
    text='#include "'+str(production)+'"\n#include <assert.h>\n'
    text+=r'''static void rejected_at_compile_boundary(nir_tex_src_type type) {
 nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
  pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),"source boundary");
 nir_def *value=nir_build_tex(&b,nir_texop_tex,.dim=GLSL_SAMPLER_DIM_2D,
  .dest_type=nir_type_float32,.coord=nir_imm_vec2(&b,0.25,0.75),
  .texture_handle=nir_imm_int(&b,pan_res_handle(1,3)),
  .sampler_handle=nir_imm_int(&b,pan_res_handle(2,5)));
 nir_tex_instr *tex=nir_instr_as_tex(nir_def_instr(value));
 nir_tex_instr_add_src(tex,type,nir_imm_int(&b,0));
 struct stage_job job={.requested=TEGU_GRAPHICS_FRAGMENT,.stage=MESA_SHADER_FRAGMENT,.resources=true};
 struct tegu_mali_graphics result={0};const char *reason=NULL;
 int rc=compile_stage(b.shader,&job,&result,&reason);
 assert(rc&&reason&&!result.code&&!result.size);
 assert(!strcmp(reason,graphics_texture_source_error(type,GRAPHICS_TEX_RESOURCES)));
 ralloc_free(b.shader);
}
static void rejected_op_at_compile_boundary(nir_texop op) {
 nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
  pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),"operation boundary");
 nir_def *value=nir_build_tex(&b,nir_texop_tex,.dim=GLSL_SAMPLER_DIM_2D,
  .dest_type=nir_type_float32,.coord=nir_imm_vec2(&b,0.25,0.75),
  .texture_handle=nir_imm_int(&b,pan_res_handle(1,3)),
  .sampler_handle=nir_imm_int(&b,pan_res_handle(2,5)));
 nir_instr_as_tex(nir_def_instr(value))->op=op;
 struct stage_job job={.requested=TEGU_GRAPHICS_FRAGMENT,.stage=MESA_SHADER_FRAGMENT,.resources=true};
 struct tegu_mali_graphics result={0};const char *reason=NULL;
 assert(compile_stage(b.shader,&job,&result,&reason));
 assert(reason&&!result.code&&!strcmp(reason,graphics_texture_op_error(op)));
 ralloc_free(b.shader);
}
static void compile_dimension(enum glsl_sampler_dim dim) {
 nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
  pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),"dimension boundary");
 bool ms=dim==GLSL_SAMPLER_DIM_MS||dim==GLSL_SAMPLER_DIM_SUBPASS_MS;
 bool fetch=ms||dim==GLSL_SAMPLER_DIM_BUF||dim==GLSL_SAMPLER_DIM_SUBPASS;
 bool array=dim==GLSL_SAMPLER_DIM_SUBPASS||dim==GLSL_SAMPLER_DIM_SUBPASS_MS;
 unsigned n=dim==GLSL_SAMPLER_DIM_1D||dim==GLSL_SAMPLER_DIM_BUF?1:
  dim==GLSL_SAMPLER_DIM_3D||dim==GLSL_SAMPLER_DIM_CUBE?3:2;
 nir_def *coord=fetch?nir_imm_zero(&b,n+array,32):nir_imm_floatN_t(&b,0.5,32);
 if(!fetch)coord=nir_replicate(&b,coord,n);
 nir_def *value=nir_build_tex(&b,ms?nir_texop_txf_ms:fetch?nir_texop_txf:nir_texop_txl,
  .dim=ms?GLSL_SAMPLER_DIM_MS:dim,.is_array=array,.dest_type=nir_type_float32,.coord=coord,
  .lod=ms?NULL:fetch?nir_imm_int(&b,0):nir_imm_float(&b,0),
  .ms_index=ms?nir_imm_int(&b,0):NULL,
  .texture_handle=nir_imm_int(&b,pan_res_handle(1,3)),
  .sampler_handle=nir_imm_int(&b,pan_res_handle(2,5)));
 nir_instr_as_tex(nir_def_instr(value))->sampler_dim=dim;
 nir_variable *out=nir_variable_create(b.shader,nir_var_shader_out,glsl_vec4_type(),"color");
 out->data.location=FRAG_RESULT_DATA0;nir_store_var(&b,out,value,15);
 nir_shader_gather_info(b.shader,nir_shader_get_entrypoint(b.shader));
 struct stage_job job={.requested=TEGU_GRAPHICS_FRAGMENT,.stage=MESA_SHADER_FRAGMENT,.resources=true};
 struct tegu_mali_graphics result={0};const char *reason=NULL;
 int rc=compile_stage(b.shader,&job,&result,&reason);
 if(rc)fprintf(stderr,"dimension %u refused: %s\n",dim,reason);
 assert(!rc&&result.code&&result.size&&!result.tls);
 tegu_mali_graphics_free(&result);ralloc_free(b.shader);
}
int main(void) {
 glsl_type_singleton_init_or_ref();
'''
    for name in sources:
        for phase in ('AUTHORED','RESOURCES','BACKEND'):
            ok = name in CORE or (name=='projector' and phase!='BACKEND') or (name.endswith('_deref') and name in LOWER and phase=='AUTHORED')
            text+=f' assert((graphics_texture_source_error(nir_tex_src_{name},GRAPHICS_TEX_{phase})==NULL)=={int(ok)});\n'
        if name in REFUSE or name in {'texture_deref','sampler_deref'}:
            text+=f' rejected_at_compile_boundary(nir_tex_src_{name});\n'
    for name in ops:
        text+=f' assert((graphics_texture_op_error(nir_texop_{name})==NULL)=={int(name in OPS)});\n'
        if name in REJECT_OPS:text+=f' rejected_op_at_compile_boundary(nir_texop_{name});\n'
    for dim in ('1D','2D','3D','CUBE','RECT','BUF','EXTERNAL','MS','SUBPASS','SUBPASS_MS'):
        text+=f' compile_dimension(GLSL_SAMPLER_DIM_{dim});\n'
    text+='''assert(graphics_texture_source_error((nir_tex_src_type)999,GRAPHICS_TEX_BACKEND));
 assert(graphics_texture_op_error((nir_texop)999));
 glsl_type_singleton_decref();return 0;
}
'''
    test=out/'source_matrix.c';test.write_text(text)
    commands=json.loads((build/'compile_commands.json').read_text())
    entry=next(c for c in commands if c['file'].endswith('/mali_graphics_compiler.c'))
    args=shlex.split(entry['command']);result=[];i=0
    while i<len(args):
        arg=args[i]
        if arg in ('-o','-MF','-MQ'):i+=2;continue
        if arg=='-MD':i+=1;continue
        if arg=='-c':result+=['-c',str(test)];i+=2;continue
        result.append(arg);i+=1
    obj=out/'source_matrix.o';result+=['-o',str(obj)]
    subprocess.run(result,cwd=build,check=True)
    target='src/panfrost/clc/libnord2_mali_runtime.so'
    link=shlex.split(subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True).splitlines()[-1])
    cleaned=[];i=0
    while i<len(link):
        arg=link[i]
        if arg=='-o':i+=2;continue
        if arg.endswith('.o') or arg in ('-shared','-fPIC') or arg.startswith('-Wl,-soname,'):i+=1;continue
        cleaned.append(arg);i+=1
    cleaned[1:1]=['-o',str(out/'source_matrix'),str(obj)]
    subprocess.run(cleaned,cwd=build,check=True)
    subprocess.run([str(out/'source_matrix')],check=True)
    report=dict(gpu_executed=False,sources=sources,operations=ops,source_phase_checks=len(sources)*3,
                boundary_refusals=len(REFUSE)+2+len(REJECT_OPS),dimensions_compiled=10,unknown_enum_refused=True)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS {len(sources)} sources x 3 phases, {len(ops)} operations, {len(REFUSE)+2+len(REJECT_OPS)} real compile-boundary refusals, 10 dimensions compiled; GPU_EXECUTED=0')

if __name__=='__main__':main()
