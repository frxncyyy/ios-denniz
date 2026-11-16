#!/usr/bin/env python3
"""Exercise real generic Mali texture compilation in isolated host processes.

Authored GLSL/SPIR-V only; no device or pixel correctness claim. Logs retain
Mesa's final NIR/ISA for semantic review. --allow-failures records a baseline.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

C = r'''
#include "mali_graphics_compiler.h"
#include <stdio.h>
#include <stdlib.h>
static void *read_file(const char *path,size_t *size) {
 FILE *f=fopen(path,"rb"); if(!f)exit(90); fseek(f,0,SEEK_END);
 long n=ftell(f); if(n<20)exit(90); *size=(size_t)n; rewind(f);
 void *data=malloc(*size); if(!data||fread(data,1,*size,f)!=*size)exit(90);
 fclose(f); return data;
}
int main(int argc,char **argv) {
 if(argc!=3)return 90;
 size_t vn,fn;void *v=read_file(argv[1],&vn),*f=read_file(argv[2],&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};
 struct tegu_mali_generic_plan plan={0};char error[512]={0};
 int rc=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&pair,&plan,error,sizeof(error));
 printf("RESULT rc=%d vs=%zu fs=%zu count=%u fetch=%u error=%s\n",rc,vs.size,fs.size,plan.count,plan.reads_framebuffer,error);
 if(!rc&&(!vs.code||!fs.code||vs.tls||fs.tls))return 91;
 if(rc&&(vs.code||fs.code||pair.version||plan.version))return 92;
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);free(v);free(f);return rc;
}
'''
VERTEX = '''#version 450
layout(location=0) in vec4 position;
layout(location=0) out vec4 uv;
void main() { gl_Position=position; uv=position*0.4+0.5; }
'''

def cases():
    calls = {
        'implicit': 'texture(s,uv.xy)',
        'bias': 'texture(s,uv.xy,uv.z)',
        'lod': 'textureLod(s,uv.xy,uv.z)',
        'grad': 'textureGrad(s,uv.xy,uv.zw,uv.wx)',
        'offset': 'textureOffset(s,uv.xy,ivec2(1,-1))',
        'lod_offset': 'textureLodOffset(s,uv.xy,uv.z,ivec2(-1,1))',
        'grad_offset': 'textureGradOffset(s,uv.xy,uv.zw,uv.wx,ivec2(1,-1))',
        'project': 'textureProj(s,uv.xyz)',
        'project_lod': 'textureProjLod(s,uv.xyz,uv.w)',
        'project_grad': 'textureProjGrad(s,uv.xyz,uv.zw,uv.wx)',
        'fetch': 'texelFetch(s,ivec2(uv.xy*32),int(uv.z))',
        'fetch_offset': 'texelFetchOffset(s,ivec2(uv.xy*32),int(uv.z),ivec2(1,-1))',
        'gather': 'textureGather(s,uv.xy,0)',
        'gather_offset': 'textureGatherOffset(s,uv.xy,ivec2(1,-1),1)',
        'gather_offsets': 'textureGatherOffsets(s,uv.xy,ivec2[4](ivec2(1,0),ivec2(0,1),ivec2(-1,0),ivec2(0,-1)),2)',
        'size': 'vec4(textureSize(s,int(uv.z)),0,1)',
        'query_lod': 'vec4(textureQueryLod(s,uv.xy),0,1)',
        'query_levels': 'vec4(textureQueryLevels(s))',
    }
    for mode in ('combined', 'separate'):
        decl = 'layout(set=0,binding=3) uniform sampler2D s;'
        if mode == 'separate':
            decl = ('layout(set=0,binding=3) uniform texture2D tex;\n'
                    'layout(set=0,binding=5) uniform sampler samp;\n'
                    '#define s sampler2D(tex,samp)')
        for name,expr in calls.items():
            yield mode+'_'+name,decl,expr,0
    for kind,coord in (('1D','uv.x'),('3D','uv.xyz'),('Cube','uv.xyz'),('2DArray','uv.xyz')):
        decl='layout(set=0,binding=3) uniform sampler'+kind+' s;'
        yield kind+'_lod',decl,'textureLod(s,'+coord+',uv.w)',0
        if kind in ('Cube','3D'):
            yield kind+'_grad',decl,'textureGrad(s,'+coord+',uv.yzw,uv.wxy)',0
    yield 'array_fetch','layout(set=0,binding=3) uniform sampler2DArray s;','texelFetch(s,ivec3(uv.xyz*32),int(uv.w))',0
    yield 'shadow','layout(set=0,binding=3) uniform sampler2DShadow s;','vec4(texture(s,uv.xyz))',0
    yield 'multisample_fetch','layout(set=0,binding=3) uniform sampler2DMS s;','texelFetch(s,ivec2(uv.xy*32),int(uv.z))',0
    yield 'multisample_query','layout(set=0,binding=3) uniform sampler2DMS s;','vec4(textureSamples(s))',0
    yield 'input_attachment','layout(input_attachment_index=0,set=0,binding=3) uniform subpassInput s;','subpassLoad(s)',0
    clamp='#extension GL_ARB_sparse_texture_clamp : require\nlayout(set=0,binding=3) uniform sampler2D s;'
    yield 'min_lod',clamp,'textureClampARB(s,uv.xy,uv.z)',0
    yield 'bias_min_lod',clamp,'textureClampARB(s,uv.xy,uv.z,uv.w)',0
    yield 'gradient_min_lod',clamp,'textureGradClampARB(s,uv.xy,uv.zw,uv.wx,uv.z)',5
    yield 'sparse','#extension GL_ARB_sparse_texture2 : require\nlayout(set=0,binding=3) uniform sampler2D s; vec4 fetched;', 'vec4(sparseTextureARB(s,uv.xy,fetched))+fetched',5
    yield 'buffer_fetch','layout(set=0,binding=3) uniform samplerBuffer s;','texelFetch(s,int(uv.x*32))',0
    yield 'buffer_size','layout(set=0,binding=3) uniform samplerBuffer s;','vec4(textureSize(s))',0
    yield 'int_fetch','layout(set=0,binding=3) uniform isampler2D s;','vec4(texelFetch(s,ivec2(uv.xy*32),int(uv.z)))',0
    yield 'uint_gather','layout(set=0,binding=3) uniform usampler2D s;','vec4(textureGather(s,uv.xy))',0
    yield 'descriptor_array','layout(set=0,binding=3) uniform sampler2D s[2];','texture(s[int(uv.z)&1],uv.xy)',5


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--graphics',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--allow-failures',action='store_true')
    a=p.parse_args(); out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    lib=a.graphics.resolve();root=Path(__file__).resolve().parent
    (out/'check.c').write_text(C)
    subprocess.run(['cc','-std=c11','-I'+str(root),str(out/'check.c'),str(lib),'-Wl,-rpath,'+str(lib.parent),'-o',str(out/'check')],check=True)
    (out/'vertex.vert').write_text(VERTEX)
    subprocess.run(['glslangValidator','-V',str(out/'vertex.vert'),'-o',str(out/'vertex.spv')],check=True,capture_output=True)
    rows=[]
    for name,decl,expr,expected in cases():
        shader='#version 450\n'+decl+'\nlayout(location=0) in vec4 uv;\nlayout(location=0) out vec4 result;\nvoid main() { result='+expr+'; }\n'
        source=out/(name+'.frag');source.write_text(shader);spv=out/(name+'.spv')
        subprocess.run(['glslangValidator','-V',str(source),'-o',str(spv)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.0',str(spv)],check=True,capture_output=True)
        result=subprocess.run([str(out/'check'),str(out/'vertex.spv'),str(spv)],env=dict(os.environ,BIFROST_MESA_DEBUG='shaders,internal'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
        (out/(name+'.log')).write_text(result.stdout)
        last=[line for line in result.stdout.splitlines() if line.startswith('RESULT ') or 'Assertion' in line or 'Unsupported texture source' in line]
        semantic_ok=name!='array_fetch' or any('TEX_FETCH.slot' in line and '.array_enable' in line for line in result.stdout.splitlines())
        row=dict(case=name,exit=result.returncode,expected=expected,passed=result.returncode==expected and semantic_ok,semantic_check=semantic_ok,diagnostic=last,spirv_sha256=hashlib.sha256(spv.read_bytes()).hexdigest())
        rows.append(row); print(json.dumps(row),flush=True)
    for name,vertex in (
        ('instance_index',VERTEX.replace('uv=position*0.4+0.5;','uv=position*0.4+0.5; uv.x+=float(gl_InstanceIndex);')),
        ('base_instance',VERTEX.replace('#version 450','#version 450\n#extension GL_ARB_shader_draw_parameters : require').replace('uv=position*0.4+0.5;','uv=position*0.4+0.5; uv.x+=float(gl_BaseInstanceARB);')),
    ):
        src=out/(name+'.vert');src.write_text(vertex);spv=out/(name+'.spv')
        subprocess.run(['glslangValidator','-V',str(src),'-o',str(spv)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.0',str(spv)],check=True,capture_output=True)
        run=subprocess.run([str(out/'check'),str(spv),str(out/'combined_implicit.spv')],env=dict(os.environ,BIFROST_MESA_DEBUG='shaders,internal'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
        (out/(name+'.log')).write_text(run.stdout)
        row=dict(case=name,exit=run.returncode,expected=0,passed=run.returncode==0 and 'load_base_instance' not in run.stdout)
        rows.append(row);print(json.dumps(row),flush=True)
    report=dict(gpu_executed=False,compiler=str(lib),compiler_sha256=hashlib.sha256(lib.read_bytes()).hexdigest(),cases=rows)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    failed=[r['case'] for r in rows if not r['passed']]
    print(f'{len(rows)-len(failed)}/{len(rows)} host cases passed; GPU_EXECUTED=0')
    if failed and not a.allow_failures: raise SystemExit(1)

if __name__=='__main__':main()
