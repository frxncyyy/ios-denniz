#!/usr/bin/env python3
"""Regression for the compute texture/image SIGABRT at pan_nir_lower_tex.c:55.

The TGP1 buffer ABI cannot execute textures/images. Every live operation must
return a precise unsupported-resource error and an empty result; buffer-only
controls must still compile. This is containment, not texture support.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from check_generic_texture_lowering import cases

C = r'''
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
 if(argc!=2)return 90;FILE *f=fopen(argv[1],"rb");if(!f)return 90;
 fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *data=malloc(n);
 if(n<20||!data||fread(data,1,n,f)!=(size_t)n)return 90;fclose(f);
 struct tegu_mali_blob output={0};char error[512]={0};
 int rc=tegu_mali_compile(data,n,&output,error,sizeof(error));free(data);
 printf("RESULT rc=%d bytes=%zu error=%s\n",rc,output.size,error);
 if(rc&&(output.data||output.size))return 91;
 if(!rc&&(!output.data||!output.size))return 92;
 tegu_mali_free(&output);return rc;
}
'''

def compute_cases():
    yield 'buffer_only','', 'outb.value=uv;',0,''
    yield 'unused_texture','layout(set=0,binding=3) uniform sampler2D unused;', 'outb.value=uv;',0,''
    for name,decl,expr,expected in cases():
        if name in {'input_attachment','min_lod','bias_min_lod'} or name.endswith(('_bias','_query_lod')):continue
        yield name,decl,'outb.value='+expr+';',4,'compute texture operations require a texture/sampler descriptor ABI'
    for kind,coord in (('1D','int(uv.x)'),('2D','ivec2(uv.xy)'),('3D','ivec3(uv.xyz)'),('2DArray','ivec3(uv.xyz)')):
        decl='layout(rgba32f,set=0,binding=3) uniform image'+kind+' im;'
        yield kind+'_image_read',decl,'outb.value=imageLoad(im,'+coord+');',4,'compute image operations require an image descriptor ABI'
        yield kind+'_image_write',decl,'imageStore(im,'+coord+',uv);',4,'compute image operations require an image descriptor ABI'
        yield kind+'_image_size',decl,'outb.value=vec4(float(imageSize(im)'+('' if kind=='1D' else '.x')+'));',4,'compute image operations require an image descriptor ABI'
    yield 'image_atomic','layout(r32ui,set=0,binding=3) uniform uimage2D im;','outb.value=vec4(imageAtomicAdd(im,ivec2(uv.xy),1u));',4,'compute image operations require an image descriptor ABI'
    yield 'image_ms','layout(rgba32f,set=0,binding=3) uniform image2DMS im;','outb.value=imageLoad(im,ivec2(uv.xy),0);',4,'compute image operations require an image descriptor ABI'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--allow-failures',action='store_true')
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    lib=a.compiler.resolve();root=Path(__file__).resolve().parent
    (out/'check.c').write_text(C)
    subprocess.run(['cc','-std=c11','-I'+str(root),str(out/'check.c'),str(lib),'-Wl,-rpath,'+str(lib.parent),'-o',str(out/'check')],check=True)
    rows=[]
    for name,decl,body,expected,reason in compute_cases():
        source=out/(name+'.comp');spv=out/(name+'.spv')
        source.write_text('#version 450\n'+decl+'\nlayout(local_size_x=1) in;\nlayout(set=0,binding=0) buffer Out { vec4 value; } outb;\nvoid main() { vec4 uv=vec4(gl_GlobalInvocationID,1); '+body+' }\n')
        subprocess.run(['glslangValidator','-V',str(source),'-o',str(spv)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.0',str(spv)],check=True,capture_output=True)
        run=subprocess.run([str(out/'check'),str(spv)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
        (out/(name+'.log')).write_text(run.stdout)
        row=dict(case=name,exit=run.returncode,expected=expected,passed=run.returncode==expected and reason in run.stdout,
                 diagnostic=run.stdout.strip(),spirv_sha256=hashlib.sha256(spv.read_bytes()).hexdigest())
        rows.append(row);print(json.dumps(row),flush=True)
    (out/'result.json').write_text(json.dumps(dict(gpu_executed=False,texture_support=False,
        compiler_sha256=hashlib.sha256(lib.read_bytes()).hexdigest(),cases=rows),indent=2)+'\n')
    passed=sum(r['passed'] for r in rows)
    print(f'{passed}/{len(rows)} compute boundary cases passed; TEXTURE_SUPPORT=0 GPU_EXECUTED=0')
    if passed!=len(rows) and not a.allow_failures:raise SystemExit(1)

if __name__=='__main__':main()
