#!/usr/bin/env python3
"""Real host compilation of per-stage specialization, with isolated failures.

Compares native code/metadata and live binding reflection. Invalid declarations
are deliberately not passed through spirv-val: the compiler must refuse them
before Mesa. Valid modules are validated. No GPU execution is implied.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

C = r'''
#include "mali_graphics_compiler.h"
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *load(const char *path,size_t *n) {
 if(path[0]=='!'){*n=0;return NULL;}
 FILE *f=fopen(path,"rb");if(!f)exit(90);fseek(f,0,SEEK_END);
 long size=ftell(f);if(size<0)exit(90);rewind(f);*n=size;
 unsigned char *b=malloc(*n+1);if(!b||fread(b+1,1,*n,f)!=*n)exit(90);
 fclose(f);return b+1; /* Exercise unaligned SPIR-V and table input. */
}
static void save(const char *prefix,const char *suffix,const void *p,size_t n) {
 char path[4096];snprintf(path,sizeof(path),"%s.%s",prefix,suffix);
 FILE *f=fopen(path,"wb");if(!f||fwrite(p,1,n,f)!=n)exit(91);fclose(f);
}
int main(int argc,char **argv) {
 if(argc!=9)return 90;size_t vn,fn,vc_bytes,fc_bytes;
 void *v=load(argv[1],&vn),*f=load(argv[2],&fn);
 void *vc_data=load(argv[3],&vc_bytes),*fc_data=load(argv[4],&fc_bytes);
 const struct tegu_mali_specialization *vc=vc_data,*fc=fc_data;
 size_t nc=vc_bytes/16,nf=fc_bytes/16;
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};
 struct tegu_mali_generic_plan plan={0};char error[512]={0};
 if(!strcmp(argv[3],"!null"))nc=1;
 if(!strcmp(argv[3],"!many"))nc=65;
 if(!strcmp(argv[3],"!wrap")){vc=(const void *)(UINTPTR_MAX-4);nc=1;}
 if(!strcmp(argv[3],"!alias")){vc=(const void *)&vs;nc=1;}
 int mode=atoi(argv[5]);unsigned rt=strtoul(argv[6],NULL,0);int rc;
 if(mode==0)rc=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&pair,&plan,error,sizeof(error));
 else if(mode==1)rc=tegu_mali_compile_generic_pair_specialized(v,vn,f,fn,vc,nc,fc,nf,&vs,&fs,&pair,&plan,error,sizeof(error));
 else if(mode==2)rc=tegu_mali_compile_generic_pair_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,rt,error,sizeof(error));
 else if(mode==3)rc=tegu_mali_compile_generic_pair_specialized_rt(v,vn,f,fn,vc,nc,fc,nf,&vs,&fs,&pair,&plan,rt,error,sizeof(error));
 else if(mode==4)rc=tegu_mali_compile_generic_pair_tile_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,rt,error,sizeof(error));
 else rc=tegu_mali_compile_generic_pair_specialized_tile_rt(v,vn,f,fn,vc,nc,fc,nf,&vs,&fs,&pair,&plan,rt,error,sizeof(error));
 printf("RESULT {\"rc\":%d,\"bindings\":%u,\"fetch\":%u,\"vs\":%zu,\"fs\":%zu,\"error\":\"%s\"}\n",rc,plan.count,plan.reads_framebuffer,vs.size,fs.size,error);
 if(rc) {if(vs.code||fs.code||vs.size||fs.size||pair.version||plan.version)return 92;}
 else {
  save(argv[7],"vs",vs.code,vs.size);save(argv[7],"fs",fs.code,fs.size);
  struct tegu_mali_graphics vm=vs,fm=fs;vm.code=NULL;fm.code=NULL;
  save(argv[7],"vm",&vm,sizeof(vm));save(argv[7],"fm",&fm,sizeof(fm));
  save(argv[7],"pair",&pair,sizeof(pair));save(argv[7],"plan",&plan,sizeof(plan));
 }
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);
 free((unsigned char *)v-1);free((unsigned char *)f-1);
 if(vc_data)free((unsigned char *)vc_data-1);if(fc_data)free((unsigned char *)fc_data-1);
 return 0;
}
'''

VERTEX = '''#version 450
layout(constant_id=7) const int mode=0;
layout(constant_id=9) const float shift=0.0;
layout(location=0) in vec4 position;
layout(location=0) out vec4 uv;
void main(){gl_Position=position+vec4(float(mode)+shift,0,0,0);uv=position*0.4+0.5;}
'''
FRAGMENT = '''#version 450
layout(constant_id=7) const bool sampled=false;
layout(constant_id=8) const float weight=0.25;
layout(set=0,binding=3) uniform sampler2D image;
layout(location=0) in vec4 uv;
layout(location=0) out vec4 result;
void main(){result=sampled?texture(image,uv.xy)*weight:vec4(weight,0.5,0.75,1);}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--graphics', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    root = Path(__file__).resolve().parent
    out = a.out.resolve();out.mkdir(parents=True, exist_ok=True)
    lib = a.graphics.resolve()
    (out/'check.c').write_text(C)
    subprocess.run(['cc','-std=c11','-I'+str(root),str(out/'check.c'),str(lib),
                    '-Wl,-rpath,'+str(lib.parent),'-o',str(out/'check')],check=True)

    def shader(name, source, stage):
        path=out/(name+'.'+stage);path.write_text(source)
        spv=out/(name+'.spv')
        subprocess.run(['glslangValidator','-V',str(path),'-o',str(spv)],check=True,capture_output=True)
        subprocess.run(['spirv-val',str(spv)],check=True,capture_output=True)
        return spv

    v=shader('vertex',VERTEX,'vert');f=shader('fragment',FRAGMENT,'frag')
    rows=[];compiled={}

    def table(name, entries):
        if isinstance(entries,str):return entries
        path=out/(name+'.table')
        path.write_bytes(b''.join(struct.pack('<II',i,len(data))+data.ljust(8,b'\0') for i,data in entries))
        return str(path)

    def run(name, vc=(), fc=(), mode=1, rt=0, vertex=None, fragment=None, expected=0, error=None):
        prefix=out/name
        command=[str(out/'check'),str(vertex or v),str(fragment or f),table(name+'-vs',vc),
                 table(name+'-fs',fc),str(mode),str(rt),str(prefix),'reserved']
        r=subprocess.run(command,env=dict(os.environ,BIFROST_MESA_DEBUG='shaders,internal'),
                         stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
        (out/(name+'.log')).write_text(r.stdout)
        result=next((json.loads(line[7:]) for line in r.stdout.splitlines() if line.startswith('RESULT ')),{})
        passed=r.returncode==0 and result.get('rc')==expected and (not error or error in result.get('error',''))
        row=dict(case=name,passed=passed,process_exit=r.returncode,expected=expected,**result)
        rows.append(row)
        if passed and expected==0:
            compiled[name]={s:(out/(name+'.'+s)).read_bytes() for s in ('vs','fs','vm','fm','pair','plan')}
        print(json.dumps(row),flush=True)
        return result

    def require(name, condition):
        rows.append(dict(case=name,passed=bool(condition)))
        print(json.dumps(rows[-1]),flush=True)

    u=lambda value:struct.pack('<I',value)
    fl=lambda value:struct.pack('<f',value)
    run('legacy',mode=0);run('empty');run('defaults',vc=[(7,u(0)),(9,fl(0))],fc=[(7,u(0)),(8,fl(.25))])
    require('empty_legacy_bytes',compiled['legacy']==compiled['empty'])
    require('explicit_default_bytes',compiled['legacy']==compiled['defaults'])
    run('vertex_change',vc=[(7,u(2))]);run('fragment_change',fc=[(7,u(1)),(8,fl(.75))])
    run('both_stages_id7',vc=[(7,u(2))],fc=[(7,u(1)),(8,fl(.75))])
    require('separate_stage_namespace',compiled['both_stages_id7']['fs']==compiled['fragment_change']['fs'] and
            compiled['vertex_change']['fs']==compiled['empty']['fs'] and
            compiled['vertex_change']['vs']!=compiled['empty']['vs'])
    require('live_binding_reflection',next(x for x in rows if x['case']=='fragment_change')['bindings']==1)
    require('sample_survives_only_when_selected','TEX_SINGLE' in (out/'fragment_change.log').read_text() and
            'TEX_SINGLE' not in (out/'empty.log').read_text())
    run('boolean_nonzero',fc=[(7,u(2)),(8,fl(.75))])
    require('vkbool32_nonzero',compiled['boolean_nonzero']==compiled['fragment_change'])
    run('reordered',vc=[(9,fl(0)),(7,u(2))],fc=[(8,fl(.75)),(7,u(1))])
    require('table_order_independent',compiled['reordered']==compiled['both_stages_id7'])
    run('rt_legacy',mode=2);run('rt_empty',mode=3)
    require('rt_empty_legacy_bytes',compiled['rt_legacy']==compiled['rt_empty'])
    run('rt_specialized',mode=3,fc=[(7,u(1))]);run('rt_outside',mode=3,rt=8,expected=1)
    run('rt_unwritten',mode=3,rt=1,expected=5,error='Selected colour output')
    tile=shader('tile',FRAGMENT.replace('uniform sampler2D image','uniform subpassInput image')
                .replace('layout(set=0,binding=3)','layout(input_attachment_index=0,set=0,binding=3)')
                .replace('sampled=false','sampled=true').replace('texture(image,uv.xy)','subpassLoad(image)'), 'frag')
    run('tile_legacy',mode=4,fragment=tile);run('tile_empty',mode=5,fragment=tile)
    require('tile_empty_legacy_bytes',compiled['tile_legacy']==compiled['tile_empty'])
    run('tile_default',mode=5,fragment=tile,fc=[(7,u(1))])
    require('tile_default_bytes',compiled['tile_default']==compiled['tile_empty'])
    run('tile_no_fetch',mode=5,fragment=tile,fc=[(7,u(0))],expected=5,error='actual attachment read')
    live=shader('default_live',FRAGMENT.replace('sampled=false','sampled=true'),'frag')
    before=run('default_live',fragment=live);after=run('specialized_dead_texture',fragment=live,fc=[(7,u(0))])
    require('specialization_removes_texture',before['bindings']==1 and after['bindings']==0)
    buffers=shader('default_live_buffers',FRAGMENT.replace('sampled=false','sampled=true')
                   .replace('layout(set=0,binding=3) uniform sampler2D image;',
                            'layout(set=0,binding=3) uniform Data {vec4 data;} buf;\n'
                            'layout(set=0,binding=4) readonly buffer Other {vec4 data;} other;')
                   .replace('texture(image,uv.xy)','(buf.data+other.data)'),'frag')
    before=run('default_live_buffers',fragment=buffers)
    after=run('specialized_dead_buffers',fragment=buffers,fc=[(7,u(0))])
    run('specialized_live_buffers',fragment=buffers,fc=[(8,fl(.5))])
    require('specialization_removes_buffers',before['bindings']==2 and after['bindings']==0 and
            next(x for x in rows if x['case']=='specialized_live_buffers')['bindings']==2)
    mrt=shader('mrt',FRAGMENT.replace('layout(location=0) out vec4 result;',
                                     'layout(location=0) out vec4 result;layout(location=1) out vec4 second;')
               .replace('result=sampled?texture(image,uv.xy)*weight:vec4(weight,0.5,0.75,1);',
                        'result=vec4(weight);second=texture(image,uv.xy)*weight;'),'frag')
    first=run('mrt_first',mode=3,rt=0,fragment=mrt,fc=[(8,fl(.5))])
    second=run('mrt_second',mode=3,rt=1,fragment=mrt,fc=[(8,fl(.5))])
    require('selected_rt_resource_reflection',first['bindings']==0 and second['bindings']==1)
    vertex_texture=shader('vertex_texture',VERTEX.replace('layout(location=0) in vec4 position;',
            'layout(set=0,binding=4) uniform sampler2D vertex_image;layout(location=0) in vec4 position;')
            .replace('position+vec4(float(mode)+shift,0,0,0)',
                     'position+(mode==0?textureLod(vertex_image,position.xy,0.0):vec4(shift))'),'vert')
    both=run('both_stage_textures',vertex=vertex_texture,fc=[(7,u(1))])
    fs_only=run('vertex_texture_removed',vertex=vertex_texture,vc=[(7,u(1))],fc=[(7,u(1))])
    fs_binding=struct.unpack_from('<III',compiled['vertex_texture_removed']['plan'],16)
    require('per_stage_dead_resource_reflection',both['bindings']==2 and fs_only['bindings']==1 and fs_binding==(3,5,2))
    for name,vc,fc,message in (
        ('duplicate',[(7,u(0)),(7,u(1))],[],'Duplicate specialization ID'),
        ('unknown',[(123,u(1))],[],'Unknown specialization ID'),
        ('wrong_stage',[(8,fl(.5))],[],'Unknown specialization ID'),
        ('bool_width',[],[(7,b'\1')],'byte size differs'),
        ('int_width',[(7,struct.pack('<Q',1))],[],'byte size differs'),
        ('zero_width',[(7,b'')],[],'Invalid specialization scalar size'),
        ('three_bytes',[],[(8,b'123')],'Invalid specialization scalar size'),
    ):
        run(name,vc=vc,fc=fc,expected=2,error=message)
    for bad in ('null','many','wrap','alias'):
        run('invalid_table_'+bad,vc='!'+bad,expected=1)

    # Every scalar width accepted by the public record is checked against its
    # declaration, including booleans above. Folding removes unsupported wide
    # GPU arithmetic: these exercise specialization, not 64-bit GPU support.
    for name,extension,typ,size,value in (
        ('i8','GL_EXT_shader_explicit_arithmetic_types_int8','int8_t',1,3),
        ('i16','GL_EXT_shader_explicit_arithmetic_types_int16','int16_t',2,3),
        ('i64','GL_ARB_gpu_shader_int64','int64_t',8,3),
        ('f16','GL_EXT_shader_explicit_arithmetic_types_float16','float16_t',2,0x3800),
        ('f64','GL_ARB_gpu_shader_fp64','double',8,0x3fe0000000000000),
    ):
        source=FRAGMENT.replace('#version 450','#version 450\n#extension '+extension+' : require')
        source=source.replace('layout(constant_id=8) const float weight=0.25;',
                              'layout(constant_id=8) const '+typ+' scale='+typ+'(1);\n#define weight float(scale)')
        spv=shader(name,source,'frag');run(name,fragment=spv,fc=[(8,value.to_bytes(size,'little'))])
        run(name+'_wrong_width',fragment=spv,fc=[(8,bytes(4))],expected=2,error='byte size differs')

    # Deliberate malformed declarations remain subprocess-isolated.
    words=list(struct.unpack('<%dI'%(v.stat().st_size//4),v.read_bytes()))
    instructions=[];pos=5
    while pos<len(words):
        n=words[pos]>>16;instructions.append((pos,words[pos]&65535,n));pos+=n
    decor=next(pos for pos,op,n in instructions if op==71 and n==4 and words[pos+2]==1 and words[pos+3]==7)
    target=words[decor+1]
    constant=next(pos for pos,op,n in instructions if op==50 and words[pos+2]==target)
    typ=words[constant+1]
    type_pos=next(pos for pos,op,n in instructions if op==21 and words[pos+1]==typ)
    mutants={
        'duplicate_decoration':words[:decor]+words[decor:decor+4]+words[decor:],
        'duplicate_type':words[:type_pos]+words[type_pos:type_pos+4]+words[type_pos:],
        'zero_instruction_length':words[:5]+[0]+words[6:],
        'invalid_integer_width':words[:type_pos+2]+[24]+words[type_pos+3:],
        'non_scalar_target':words[:constant]+[(words[constant]&0xffff0000)|51]+words[constant+1:],
    }
    for name,mutated in mutants.items():
        spv=out/(name+'.spv');spv.write_bytes(struct.pack('<%dI'%len(mutated),*mutated))
        run(name,vertex=spv,vc=[(7,u(1))],expected=2)
    report=dict(gpu_executed=False,compiler=str(lib),compiler_sha256=hashlib.sha256(lib.read_bytes()).hexdigest(),cases=rows)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    failed=[r['case'] for r in rows if not r['passed']]
    print(f'{len(rows)-len(failed)}/{len(rows)} checks passed; GPU_EXECUTED=0')
    if failed:raise SystemExit(1)


if __name__=='__main__':main()
