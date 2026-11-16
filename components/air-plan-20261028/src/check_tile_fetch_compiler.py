#!/usr/bin/env python3
"""Qualify tile-fetch lowering on the host; does not establish GPU correctness.

Build the graphics compiler first. The legacy entry must still emit a texture
fetch. The separate tile entry must emit LD_TILE with coherent IO semantics,
report its distinct contract, and refuse unsupported attachment shapes.
"""
import argparse
from pathlib import Path
import os
import subprocess
import json
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--graphics', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
lib = a.graphics.resolve()
root = Path(__file__).resolve().parent
vertex = '''#version 450
layout(location=0) in vec4 position;
layout(location=0) out vec4 color;
void main() { gl_Position=position; color=vec4(0.25,0.5,0.75,1.0); }
'''
fragment = '''#version 450
layout(input_attachment_index=INDEX,set=0,binding=0) uniform TYPE previous;
layout(location=0) in vec4 color;
layout(location=0) out vec4 result;
void main() { result=(FETCH+color)*0.5; }
'''
c = r'''
#include "mali_graphics_compiler.h"
#include <stdio.h>
#include <stdlib.h>
static void *read_file(const char *p,size_t *n) {
 FILE *f=fopen(p,"rb");if(!f)exit(90);fseek(f,0,SEEK_END);long length=ftell(f);
 if(length<20)exit(90);*n=(size_t)length;rewind(f);void *b=malloc(*n);
 if(!b||fread(b,1,*n,f)!=*n)exit(90);fclose(f);return b;
}
int main(int argc,char **argv) {
 if(argc!=5)return 90;size_t vn,fn;void *v=read_file(argv[1],&vn),*f=read_file(argv[2],&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};
 struct tegu_mali_generic_plan plan={0};char error[512]={0};
 int tile=atoi(argv[3]);
 int rc=tile==1?tegu_mali_compile_generic_pair_tile_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,0,error,sizeof(error)):
  tegu_mali_compile_generic_pair_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,0,error,sizeof(error));
 printf("RESULT rc=%d fetch=%u bindings=%u vs=%zu fs=%zu sysvals=%u error=%s\n",
  rc,plan.reads_framebuffer,plan.count,vs.size,fs.size,fs.sysval_mask,error);
 if(!rc) {
  if(tile==2) { if(plan.reads_framebuffer||plan.count)return 91; }
  else if(plan.reads_framebuffer!=(tile?TEGU_MALI_GENERIC_FETCH_TILE:TEGU_MALI_GENERIC_FETCH_MEMORY)||
     plan.count!=1||plan.bindings[0].kind!=TEGU_MALI_GENERIC_INPUT_ATTACHMENT||plan.reserved!=1||
     !(fs.sysval_mask&8))return 91;
  printf("ASSET {\"vr\":%u,\"fr\":%u,\"sr\":%u,\"vfau\":%u,\"ffau\":%u,\"pos\":%u,\"secondary\":%u,\"varying\":%u,\"vp\":%llu,\"fp\":%llu,\"sp\":%llu}\n",
   vs.registers,fs.registers,pair.secondary_registers,vs.fau_words,fs.fau_words,pair.position_offset,
   pair.secondary_offset,pair.generic_size_B,(unsigned long long)vs.preload,(unsigned long long)fs.preload,(unsigned long long)pair.secondary_preload);
  if(pair.vs_attribute_mask!=1)return 94;
  char path[4096];snprintf(path,sizeof(path),"%s.vs",argv[4]);
  FILE *vertex=fopen(path,"wb");if(!vertex)return 92;
  if(fwrite(vs.code,1,vs.size,vertex)!=vs.size)return 92;fclose(vertex);
  FILE *fp=fopen(argv[4],"wb");if(!fp)return 92;
  if(fwrite(fs.code,1,fs.size,fp)!=fs.size)return 92;fclose(fp);
 } else if(vs.code||fs.code||pair.version||plan.version)return 93;
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);free(v);free(f);return rc;
}
'''
(out/'check.c').write_text(c)
subprocess.run(['cc', '-std=c11', '-I'+str(root), str(out/'check.c'), str(lib),
                '-Wl,-rpath,'+str(lib.parent), '-o', str(out/'check')], check=True)
(out/'vertex.vert').write_text(vertex)
subprocess.run(['glslangValidator', '-V', str(out/'vertex.vert'), '-o', str(out/'vertex.spv')], check=True)
wide_vertex = vertex.replace('layout(location=0) out vec4 color;',
    'layout(location=0) out vec4 color; layout(location=1) out vec4 color1;').replace(
    'color=vec4(0.25,0.5,0.75,1.0);', 'color=vec4(0.25,0.5,0.75,1.0); color1=color;')
(out/'wide.vert').write_text(wide_vertex)
subprocess.run(['glslangValidator', '-V', str(out/'wide.vert'), '-o', str(out/'wide.spv')], check=True)
medium_vertex = wide_vertex.replace('out vec4 color1;', 'out vec2 color1;').replace('color1=color;', 'color1=color.xy;')
(out/'medium.vert').write_text(medium_vertex)
subprocess.run(['glslangValidator', '-V', str(out/'medium.vert'), '-o', str(out/'medium.spv')], check=True)
results = []
assets = {}
for name, index, kind, fetch, tile, accepted in (
    ('memory', 0, 'subpassInput', 'subpassLoad(previous)', False, True),
    ('tile', 0, 'subpassInput', 'subpassLoad(previous)', True, True),
    ('plain', 0, 'subpassInput', 'color', 2, True),
    ('memory_wide', 0, 'subpassInput', 'subpassLoad(previous)', False, True),
    ('tile_wide', 0, 'subpassInput', 'subpassLoad(previous)', True, True),
    ('memory_medium', 0, 'subpassInput', 'subpassLoad(previous)', False, True),
    ('tile_medium', 0, 'subpassInput', 'subpassLoad(previous)', True, True),
    ('cross_attachment', 1, 'subpassInput', 'subpassLoad(previous)', True, False),
    ('multisample', 0, 'subpassInputMS', 'subpassLoad(previous,0)', True, False),
    ('no_fetch', 0, 'subpassInput', 'vec4(0.0)', True, False),
):
    source = fragment.replace('INDEX', str(index)).replace('TYPE', kind).replace('FETCH', fetch)
    if name.endswith('_wide'):
        source = source.replace('layout(location=0) in vec4 color;',
            'layout(location=0) in vec4 color; layout(location=1) in vec4 color1;')
        source = source.replace('+color)', '+(color+color1)*0.5)')
    if name.endswith('_medium'):
        source = source.replace('layout(location=0) in vec4 color;',
            'layout(location=0) in vec4 color; layout(location=1) in vec2 color1;')
        source = source.replace('+color)', '+(color+vec4(color1,color.zw))*0.5)')
    vertex_spv = out/('medium.spv' if name.endswith('_medium') else 'wide.spv' if name.endswith('_wide') else 'vertex.spv')
    path = out/(name+'.frag'); path.write_text(source)
    spv = out/(name+'.spv')
    subprocess.run(['glslangValidator', '-V', str(path), '-o', str(spv)], check=True)
    env = dict(os.environ, BIFROST_MESA_DEBUG='shaders,internal')
    run = subprocess.run([str(out/'check'), str(vertex_spv), str(spv), str(int(tile)), str(out/(name+'.bin'))],
                         env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out/(name+'.log')).write_text(run.stdout)
    if accepted:
        assert run.returncode == 0, (name, run.stdout[-1500:])
        assert ('LD_TILE' in run.stdout) == (tile == 1), name
        assets[name] = json.loads(re.search(r'ASSET (\{.*\})', run.stdout).group(1))
        if name.endswith('_wide'):
            assert assets[name]['varying'] == 32, (name, assets[name])
        if name.endswith('_medium'):
            assert assets[name]['varying'] == 24, (name, assets[name])
        if tile == 1:
            assert 'fbfetch coherent' in run.stdout and '.wait' in run.stdout, name
    else:
        assert run.returncode == 5, (name, run.returncode, run.stdout[-1500:])
    results.append({'case': name, 'exit': run.returncode, 'accepted': accepted,
                    'tile_load': 'LD_TILE' in run.stdout})
(out/'result.json').write_text(json.dumps({'gpu_executed': False, 'cases': results}, indent=2)+'\n')
print('PASS tile-fetch compiler: LD_TILE/coherent flow, legacy memory fetch, and shape refusals; GPU_EXECUTED=0')

header = ["/* Generated host-compiled GPU probe assets; not Apple shaders. */", "#define TEGU_TILE_PLAIN_CONTROL_ASSET 1", "#define TEGU_TILE_WIDE_CONTROL_ASSET 1", "#define TEGU_TILE_MEDIUM_CONTROL_ASSET 1"]
for name, values in assets.items():
    for stage, suffix in (("vs", ".bin.vs"), ("fs", ".bin")):
        binary = (out/(name+suffix)).read_bytes()
        header.append("static const unsigned char tegu_tile_%s_%s[]={%s};" %
                      (name, stage, ",".join(str(b) for b in binary)))
    fields = {"vertex_registers": "vr", "fragment_registers": "fr", "secondary_registers": "sr",
              "vertex_fau_words": "vfau", "fragment_fau_words": "ffau", "position_offset": "pos",
              "secondary_offset": "secondary", "varying_bytes": "varying",
              "vertex_preload": "vp", "fragment_preload": "fp", "secondary_preload": "sp"}
    init = [".%s=UINT64_C(%s)" % (key, values[value]) for key, value in fields.items()]
    init += [".vertex_code=tegu_tile_%s_vs" % name, ".fragment_code=tegu_tile_%s_fs" % name,
             ".vertex_bytes=sizeof(tegu_tile_%s_vs)" % name, ".fragment_bytes=sizeof(tegu_tile_%s_fs)" % name,
             ".attribute_count=1", ".attributes={{0,0,109,0,16,0,1}}",
             ".flags="+str(int(name in ("tile", "tile_wide", "tile_medium"))), ".blend={1,0,0,1,0,0,1,0,15}"]
    header.append("static const struct tegu_generic_draw_shader tegu_tile_%s_shader={%s};" % (name, ",".join(init)))
(out/'tile-fetch-assets.h').write_text("\n".join(header)+"\n")
