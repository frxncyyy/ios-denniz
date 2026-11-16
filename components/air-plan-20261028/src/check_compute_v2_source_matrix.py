#!/usr/bin/env python3
"""Pinned complete source/op classification and TGP2 storage-format map oracle.

Builds one small host test TU against existing Mesa archives, never Mesa itself.
Unsupported inputs are passed through the actual resource-lowering boundary.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
from check_texture_source_matrix import CORE,LOWER,REFUSE,OPS,REJECT_OPS

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mesa-source',type=Path,required=True);p.add_argument('--mesa-build',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 source=a.mesa_source.resolve();build=a.mesa_build.resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);production=Path(__file__).resolve().with_name('mali_compute_compiler.c')
 nir=(source/'src/compiler/nir/nir.h').read_text()
 def enum(prefix,name):
  part=nir.split('typedef enum '+name+' {',1)[1].split('} '+name+';',1)[0];part=re.sub(r'/\*.*?\*/','',part,flags=re.S);return re.findall(r'\b'+prefix+r'([A-Za-z0-9_]+)\b',part)
 sources=enum('nir_tex_src_','nir_tex_src_type');ops=enum('nir_texop_','nir_texop');assert set(sources)==CORE|LOWER|REFUSE;assert set(ops)==OPS|REJECT_OPS
 text='#include "'+str(production)+'"\n#include <assert.h>\n'
 text+=r'''
static void boundary(unsigned type,bool operation) {
 nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
  pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),"compute source boundary");
 nir_def *value=nir_build_tex(&b,nir_texop_txl,.dim=GLSL_SAMPLER_DIM_2D,
  .dest_type=nir_type_float32,.coord=nir_imm_vec2(&b,0.25,0.75),.lod=nir_imm_float(&b,0),
  .texture_handle=nir_imm_int(&b,pan_res_handle(1,3)),
  .sampler_handle=nir_imm_int(&b,pan_res_handle(2,5)));
 nir_tex_instr *tex=nir_instr_as_tex(nir_def_instr(value));
 if(operation)tex->op=type;else nir_tex_instr_add_src(tex,type,nir_imm_int(&b,0));
 char error[256]={0};struct context ctx={.v2=true,.error=error,.error_size=sizeof(error)};
 assert(!compute_lower_v2_resources(b.shader,&ctx));assert(ctx.failed&&error[0]);
 const char *expected=operation?compute_texture_op_error(type):compute_texture_source_error(type,COMPUTE_TEX_AUTHORED);
 assert(expected&&!strcmp(error,expected));ralloc_free(b.shader);
}
int main(void) {
 glsl_type_singleton_init_or_ref();
'''
 for name in sources:
  for phase in ('AUTHORED','RESOURCES','BACKEND'):
   ok=name in CORE or(name=='projector' and phase!='BACKEND')or(name in ('texture_deref','sampler_deref') and phase=='AUTHORED')
   text+=f' assert((compute_texture_source_error(nir_tex_src_{name},COMPUTE_TEX_{phase})==NULL)=={int(ok)});\n'
  if name in REFUSE:text+=f' boundary(nir_tex_src_{name},false);\n'
 for name in ops:
  text+=f' assert((compute_texture_op_error(nir_texop_{name})==NULL)=={int(name in OPS)});\n'
  if name in REJECT_OPS:text+=f' boundary(nir_texop_{name},true);\n'
 mapping=[('NONE',0),('R32G32B32A32_FLOAT',109),('R16G16B16A16_FLOAT',97),('R32_FLOAT',100),('R8G8B8A8_UNORM',37),('R8G8B8A8_SNORM',38),('R32G32_FLOAT',103),('R16G16_FLOAT',83),('R11G11B10_FLOAT',122),('R16_FLOAT',76),('R16G16B16A16_UNORM',91),('R10G10B10A2_UNORM',64),('R16G16_UNORM',77),('R8G8_UNORM',16),('R16_UNORM',70),('R8_UNORM',9),('R16G16B16A16_SNORM',92),('R16G16_SNORM',78),('R8G8_SNORM',17),('R16_SNORM',71),('R8_SNORM',10),('R32G32B32A32_SINT',108),('R16G16B16A16_SINT',96),('R8G8B8A8_SINT',42),('R32_SINT',99),('R32G32_SINT',102),('R16G16_SINT',82),('R8G8_SINT',21),('R16_SINT',75),('R8_SINT',14),('R32G32B32A32_UINT',107),('R16G16B16A16_UINT',95),('R8G8B8A8_UINT',41),('R32_UINT',98),('R10G10B10A2_UINT',68),('R32G32_UINT',101),('R16G16_UINT',81),('R8G8_UINT',20),('R16_UINT',74),('R8_UINT',13),('R64_UINT',110),('R64_SINT',111)]
 for fmt,vk in mapping:text+=f' assert(compute_storage_format(PIPE_FORMAT_{fmt})=={vk});\n'
 text+=' assert(compute_storage_format(PIPE_FORMAT_B8G8R8A8_UNORM)==UINT32_MAX);\n'
 text+=' assert(compute_texture_source_error((nir_tex_src_type)999,COMPUTE_TEX_BACKEND));\n assert(compute_texture_op_error((nir_texop)999));\n glsl_type_singleton_decref();return 0;\n}\n'
 test=out/'matrix.c';test.write_text(text);commands=json.loads((build/'compile_commands.json').read_text());entry=next(c for c in commands if c['file'].endswith('/mali_compute_compiler.c'));args=shlex.split(entry['command']);cmd=[];i=0
 while i<len(args):
  arg=args[i]
  if arg in ('-o','-MF','-MQ'):i+=2;continue
  if arg=='-MD':i+=1;continue
  if arg=='-c':cmd+=['-c',str(test)];i+=2;continue
  cmd.append(arg);i+=1
 obj=out/'matrix.o';cmd+=['-o',str(obj)];subprocess.run(cmd,cwd=build,check=True)
 link=shlex.split(subprocess.check_output(['ninja','-C',str(build),'-t','commands','src/panfrost/clc/libnord2_mali_runtime.so'],text=True).splitlines()[-1]);cmd=[];i=0
 while i<len(link):
  arg=link[i]
  if arg=='-o':i+=2;continue
  if arg.endswith('.o')or arg in ('-shared','-fPIC')or arg.startswith('-Wl,-soname,'):i+=1;continue
  cmd.append(arg);i+=1
 cmd[1:1]=['-o',str(out/'matrix'),str(obj)];subprocess.run(cmd,cwd=build,check=True);subprocess.run([str(out/'matrix')],check=True)
 report={'source_phase_checks':len(sources)*3,'operations':len(ops),'boundary_refusals':len(REFUSE)+len(REJECT_OPS),'format_mapping_checks':43,'gpu_executed':False};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
