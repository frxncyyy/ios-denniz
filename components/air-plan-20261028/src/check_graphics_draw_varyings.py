#!/usr/bin/env python3
"""Host checks for the additive generic options entry and abort-to-refusal gates.

Real glslang SPIR-V through the real Mali compiler library, each case in an
isolated process (a backend assert is a FAIL, never a pass). Covers:
  * draw parameters: VertexIndex/BaseVertex/InstanceIndex/BaseInstance FAU
    words (sysval bits, FAU word count, the IADD with FAU word16 in the ISA);
  * extended varyings: flat int/uint/float, noperspective, >4 locations,
    pair flags/masks and the linked slot list; refusals for 16-bit, centroid,
    stage type mismatch;
  * legacy identity: options without flags == specialized(NULL) == default
    entry bytes/metadata, SELECT_RT == _rt; optional --baseline library gives
    old-vs-new byte identity for every default-path case;
  * invalid options return 1 and leave outputs untouched;
  * former aborts now refused: BaseVertex on default entries, large private
    arrays in graphics and compute (bi_store_tl offset<0x8000), and optional
    real corpus inputs (--corpus-graphics run dirs, --compute-spv files).
No GPU execution and no pixel-correctness claim.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

C = r'''
#include "mali_graphics_compiler.h"
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *load(const char *p,size_t *n) {
 FILE *f=fopen(p,"rb");if(!f)exit(90);fseek(f,0,SEEK_END);long z=ftell(f);if(z<0)exit(90);rewind(f);
 *n=(size_t)z;unsigned char *d=malloc(*n+1);if(!d||fread(d+1,1,*n,f)!=*n)exit(90);fclose(f);return d+1;
}
static void save(const char *pre,const char *suf,const void *p,size_t n) {
 char b[4096];snprintf(b,sizeof b,"%s.%s",pre,suf);FILE *f=fopen(b,"wb");if(!f||fwrite(p,1,n,f)!=n)exit(91);fclose(f);
}
static void hex(const void *p,size_t n,char *out) {
 const unsigned char *b=p;for(size_t i=0;i<n&&i<32;i++)sprintf(out+2*i,"%02x",b[i]);
}
int main(int argc,char **argv) {
 if(argc<8)return 90;
 if(!strcmp(argv[1],"compute")) {
  size_t n;void *d=load(argv[2],&n);struct tegu_mali_blob b={0};char e[400]={0};
  int rc=tegu_mali_compile_compute_v2(d,n,NULL,0,&b,e,sizeof e);
  printf("RESULT {\"rc\":%d,\"size\":%zu,\"error\":\"%s\"}\n",rc,b.size,e);
  tegu_mali_free(&b);return 0;
 }
 size_t vn,fn;void *v=load(argv[1],&vn),*f=load(argv[2],&fn);
 int mode=atoi(argv[3]);unsigned flags=strtoul(argv[4],0,0),rt=strtoul(argv[5],0,0),version=strtoul(argv[6],0,0);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};
 struct tegu_mali_generic_plan plan={0};struct tegu_mali_varying_info vi={0};char err[512]={0};int rc;
 struct tegu_mali_generic_options o={version,flags,rt,mode==6?1u:0u};
 int ext=(flags&TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS)!=0;
 if(mode==7)ext=!ext; /* deliberately mismatched varyings pointer */
 memset(err,0x5a,sizeof(err)-1);err[sizeof(err)-1]=0;
 char sentinel[512];memcpy(sentinel,err,sizeof err);
 if(mode==0)rc=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&pair,&plan,err,sizeof err);
 else if(mode==1)rc=tegu_mali_compile_generic_pair_specialized(v,vn,f,fn,NULL,0,NULL,0,&vs,&fs,&pair,&plan,err,sizeof err);
 else if(mode==3)rc=tegu_mali_compile_generic_pair_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,rt,err,sizeof err);
#ifndef TEGU_NO_OPTIONS
 else if(mode==5)rc=tegu_mali_compile_generic_pair_options(v,vn,f,fn,NULL,0,NULL,0,NULL,&vs,&fs,&pair,&plan,NULL,err,sizeof err);
 else if(mode!=8)rc=tegu_mali_compile_generic_pair_options(v,vn,f,fn,NULL,0,NULL,0,&o,&vs,&fs,&pair,&plan,ext?&vi:NULL,err,sizeof err);
 /* mode 8: version2 options with TEGU_MRT="count,f0,f1,f2,f3,r0,r1,r2" (r = reserved2). */
 else {
  struct tegu_mali_generic_options2 o2={TEGU_MALI_GENERIC_OPTIONS2_VERSION,flags,rt,0,0,{0},{0}};
  const char *m=getenv("TEGU_MRT");unsigned w[8]={0};
  if(m)sscanf(m,"%u,%u,%u,%u,%u,%u,%u,%u",&w[0],&w[1],&w[2],&w[3],&w[4],&w[5],&w[6],&w[7]);
  o2.target_count=w[0];for(int k=0;k<4;k++)o2.target_formats[k]=w[1+k];for(int k=0;k<3;k++)o2.reserved2[k]=w[5+k];
  rc=tegu_mali_compile_generic_pair_options(v,vn,f,fn,NULL,0,NULL,0,(const struct tegu_mali_generic_options *)&o2,
   &vs,&fs,&pair,&plan,ext?&vi:NULL,err,sizeof err);
 }
#else
 else {(void)o;(void)ext;return 93;}
#endif
 int untouched=!vs.code&&!vs.size&&!fs.code&&!fs.size&&!pair.version&&!plan.version&&!vi.version&&!memcmp(err,sentinel,sizeof err);
 if(rc==1&&mode>=4)err[0]=0;
 char vh[65]={0},fh[65]={0};
 printf("RESULT {\"vtls\":%u,\"ftls\":%u,\"rc\":%d,\"untouched\":%d,\"vs\":%zu,\"fs\":%zu,\"vsys\":%u,\"fsys\":%u,\"vfau\":%u,\"ffau\":%u,"
  "\"pflags\":%u,\"nopersp\":%u,\"vcount\":%u,\"gsize\":%u,\"fs_ld_var\":%u,\"bindings\":%u,\"reserved\":%u,"
  "\"ext_version\":%u,\"ext_count\":%u,\"ext_vs\":%u,\"ext_fs\":%u,\"flat\":%u,\"ext_np\":%u,\"uses_flat\":%u,\"ext_gsize\":%u,\"slots\":[",
  vs.tls,fs.tls,rc,untouched,vs.size,fs.size,vs.sysval_mask,fs.sysval_mask,vs.fau_words,fs.fau_words,pair.flags,pair.noperspective_mask,
  pair.varying_count,pair.generic_size_B,pair.fs_uses_ld_var,plan.count,plan.reserved,vi.version,vi.count,vi.vs_mask,vi.fs_mask,
  vi.flat_mask,vi.noperspective_mask,vi.uses_flat_shading,vi.generic_size_B);
 for(unsigned i=0;i<vi.count&&i<TEGU_MALI_EXT_MAX_VARYINGS;i++)printf("%s[%u,%u,%u,%u,%u]",i?",":"",vi.slots[i].location,
  vi.slots[i].offset,vi.slots[i].components,vi.slots[i].alu_type,vi.slots[i].interpolation);
 printf("],\"error\":\"%s\"}\n",rc?err:"");
 (void)hex;(void)vh;(void)fh;
 if(!rc) {
  save(argv[7],"vs",vs.code,vs.size);save(argv[7],"fs",fs.code,fs.size);
  struct tegu_mali_graphics a=vs,b=fs;a.code=b.code=NULL;save(argv[7],"vm",&a,sizeof a);save(argv[7],"fm",&b,sizeof b);
  save(argv[7],"pair",&pair,sizeof pair);save(argv[7],"plan",&plan,sizeof plan);
 } else if(vs.code||fs.code||pair.version||plan.version||vi.version)return 92;
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);return 0;
}
'''

VS_HEAD = '#version 450\n#extension GL_ARB_shader_draw_parameters : require\n'


def vs(body, outs='layout(location=0) out vec4 uv;', ins='layout(location=0) in vec4 position;'):
    return VS_HEAD + ins + '\n' + outs + '\nvoid main(){ gl_Position=position; ' + body + ' }\n'


FS_UV = '#version 450\nlayout(location=0) in vec4 uv;\nlayout(location=0) out vec4 result;\nvoid main(){ result=uv; }\n'
FS_MRT = ('#version 450\nlayout(location=0) in vec4 uv;\nlayout(location=0) out vec4 a;\n'
          'layout(location=1) out vec4 b;\nvoid main(){ a=uv; b=uv.wzyx; }\n')
DRAW = {
    'plain': vs('uv=position;'),
    'vertex_index': vs('uv=position+float(gl_VertexIndex);'),
    'base_vertex': vs('uv=position+float(gl_BaseVertexARB);'),
    'instance_index': vs('uv=position+float(gl_InstanceIndex);'),
    'base_instance': vs('uv=position+float(gl_BaseInstanceARB);'),
    'draw_id': vs('uv=position+float(gl_DrawIDARB);'),
    'all_parameters': vs('uv=position+vec4(gl_VertexIndex,gl_BaseVertexARB,gl_InstanceIndex,gl_BaseInstanceARB);'),
    'vertex_pulling': VS_HEAD + 'layout(set=0,binding=0) readonly buffer P { vec4 p[]; };\n'
                      'layout(location=0) out vec4 uv;\nvoid main(){ gl_Position=p[gl_VertexIndex]; uv=p[gl_VertexIndex+1]; }\n',
}
# (bits expected in vs sysval mask with draw parameters, default entry rc==0?)
DRAW_EXPECT = {'plain': (0, True), 'vertex_index': (16, True), 'base_vertex': (16, False),
               'instance_index': (32, True), 'base_instance': (32, True), 'draw_id': (0, True),
               'all_parameters': (48, False), 'vertex_pulling': (16, True)}

V5 = ('layout(location=0) out vec4 a; layout(location=1) out vec2 b; layout(location=2) out uvec2 c;'
      'layout(location=3) out vec4 d; layout(location=4) out int e;')
VARY = {
    # name: (vs, fs, expected ext rc, expected flat mask, expected noperspective mask)
    'flat_uint': (vs('uv=position; k=uint(gl_VertexIndex);', 'layout(location=0) out vec4 uv; layout(location=1) out uint k;'),
                  '#version 450\nlayout(location=0) in vec4 uv; layout(location=1) flat in uint k;\n'
                  'layout(location=0) out vec4 r;\nvoid main(){ r=uv*float(k); }\n', 0, 2, 0),
    'flat_float_noperspective': (vs('uv=position; w=position.zw; q=position.x;',
                                    'layout(location=0) out vec4 uv; layout(location=1) out vec2 w; layout(location=2) out float q;'),
                                 '#version 450\nlayout(location=0) flat in vec4 uv; layout(location=1) noperspective in vec2 w;'
                                 ' layout(location=2) in float q;\nlayout(location=0) out vec4 r;\nvoid main(){ r=uv+vec4(w,q,1); }\n',
                                 0, 1, 2),
    'five_locations': (vs('a=position; b=position.xy; c=uvec2(position.xy); d=position.wzyx; e=int(position.x);', V5),
                       '#version 450\nlayout(location=0) noperspective in vec4 a; layout(location=1) in vec2 b;'
                       ' layout(location=2) flat in uvec2 c; layout(location=3) flat in vec4 d; layout(location=4) flat in int e;\n'
                       'layout(location=0) out vec4 r;\nvoid main(){ r=a+vec4(b,c)+d+float(e); }\n', 0, 28, 1),
    'vs_only_int': (vs('uv=position; k=7u;', 'layout(location=0) out vec4 uv; layout(location=1) out uint k;'),
                    FS_UV, 0, 0, 0),
    'type_mismatch': (vs('uv=position; k=int(position.x);', 'layout(location=0) out vec4 uv; layout(location=1) out int k;'),
                      '#version 450\nlayout(location=0) in vec4 uv; layout(location=1) flat in uint k;\n'
                      'layout(location=0) out vec4 r;\nvoid main(){ r=uv*float(k); }\n', 5, 0, 0),
    'centroid': (vs('uv=position;'), '#version 450\nlayout(location=0) centroid in vec4 uv;\nlayout(location=0) out vec4 r;\n'
                 'void main(){ r=uv; }\n', 5, 0, 0),
    'half_varying': ('#version 450\n#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n'
                     '#extension GL_EXT_shader_16bit_storage : require\nlayout(location=0) in vec4 position;\n'
                     'layout(location=0) out f16vec4 uv;\nvoid main(){ gl_Position=position; uv=f16vec4(position); }\n',
                     '#version 450\n#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n'
                     '#extension GL_EXT_shader_16bit_storage : require\nlayout(location=0) in f16vec4 uv;\n'
                     'layout(location=0) out vec4 r;\nvoid main(){ r=vec4(uv); }\n', 5, 0, 0),
}
PRIVATE_FS = ('#version 450\nlayout(location=0) in vec4 uv;\nlayout(location=0) out vec4 r;\n'
              'void main(){ vec4 t[2304]; for(int i=0;i<2304;i++) t[i]=uv*float(i);'
              ' r=t[int(uv.x*2303.0)]; }\n')
PRIVATE_SMALL_FS = ('#version 450\nlayout(location=0) in vec4 uv;\nlayout(location=0) out vec4 r;\n'
                    'void main(){ vec4 t[16]; for(int i=0;i<16;i++) t[i]=uv*float(i); r=t[int(uv.x*15.0)]; }\n')
PRIVATE_CS = ('#version 450\nlayout(local_size_x=8) in;\nlayout(set=0,binding=0) buffer B { vec4 o[]; };\n'
              'void main(){ vec4 t[2304]; for(int i=0;i<2304;i++) t[i]=vec4(i);'
              ' o[gl_GlobalInvocationID.x]=t[gl_GlobalInvocationID.x%2304u]; }\n')


def glsl(out, name, text, stage, target='vulkan1.0'):
    src = out/f'{name}.{stage}'; src.write_text(text); spv = out/f'{name}.{stage}.spv'
    subprocess.run(['glslangValidator', '--target-env', target, '-V', str(src), '-o', str(spv)], check=True, capture_output=True)
    subprocess.run(['spirv-val', '--target-env', target, str(spv)], check=True, capture_output=True)
    return spv


def run(binary, args, out, tag, env=None):
    r = subprocess.run([str(binary)] + [str(a) for a in args] + [str(out/tag)], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, timeout=120, env=env)
    (out/(tag+'.log')).write_text(r.stdout)
    line = [x for x in r.stdout.splitlines() if x.startswith('RESULT ')]
    res = json.loads(line[-1][7:]) if line and r.returncode == 0 else {'rc': None}
    res['exit'] = r.returncode
    return res


def artifacts(out, tag):
    blob = b''
    for suf in ('vs', 'fs', 'vm', 'fm', 'pair', 'plan'):
        p = out/f'{tag}.{suf}'
        blob += p.read_bytes() if p.exists() else b'<missing>'
    return hashlib.sha256(blob).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--graphics', type=Path, required=True)
    p.add_argument('--baseline', type=Path, help='older library for default-path byte identity')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--corpus-graphics', type=Path, nargs='*', default=[], help='run dirs with vertex.spv/fragment.spv')
    p.add_argument('--compute-spv', type=Path, nargs='*', default=[])
    p.add_argument('--mrt-spv', action='append', default=[], help='NAME=VERT:FRAG:count,f0,f1,f2,f3')
    a = p.parse_args()
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parent
    (out/'check.c').write_text(C)
    bins = {}
    for tag, lib in (('new', a.graphics), ('old', a.baseline)):
        if not lib: continue
        lib = lib.resolve(); b = out/f'check-{tag}'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', *(['-DTEGU_NO_OPTIONS'] if tag == 'old' else []),
                        '-I'+str(root), str(out/'check.c'), str(lib),
                        '-Wl,-rpath,'+str(lib.parent), '-o', str(b)], check=True)
        bins[tag] = b
    new = bins['new']; rows = []; failures = []

    def check(name, ok, **detail):
        rows.append(dict(case=name, passed=bool(ok), **detail)); print(json.dumps(rows[-1]), flush=True)
        if not ok: failures.append(name)

    fs_uv = glsl(out, 'fs_uv', FS_UV, 'frag'); fs_mrt = glsl(out, 'fs_mrt', FS_MRT, 'frag')
    # Draw parameters.
    for name, text in DRAW.items():
        v = glsl(out, 'draw_'+name, text, 'vert'); bits, default_ok = DRAW_EXPECT[name]
        d = run(new, [v, fs_uv, 0, 0, 0, 1], out, f'draw_{name}.default')
        s = run(new, [v, fs_uv, 1, 0, 0, 1], out, f'draw_{name}.specialized')
        o0 = run(new, [v, fs_uv, 4, 0, 0, 1], out, f'draw_{name}.options0')
        dp = run(new, [v, fs_uv, 4, 1, 0, 1], out, f'draw_{name}.draw', env=dict(os.environ, BIFROST_MESA_DEBUG='shaders'))
        check(f'draw/{name}/default', d['exit'] == 0 and (d['rc'] == 0) == default_ok and
              (default_ok or 'draw-parameter' in d.get('error', '')) and not (d.get('vsys', 0) & 48), result=d)
        same = d['rc'] != 0 or (artifacts(out, f'draw_{name}.default') == artifacts(out, f'draw_{name}.specialized') ==
                                artifacts(out, f'draw_{name}.options0'))
        check(f'draw/{name}/options0_identity', s['rc'] == d['rc'] == o0['rc'] and same)
        log = (out/f'draw_{name}.draw.log').read_text()
        isa = bits == 0 or ('u8.w0' if bits & 16 else 'u8.w1') in log
        check(f'draw/{name}/draw_parameters', dp['exit'] == 0 and dp['rc'] == 0 and (dp['vsys'] & 48) == bits and
              (bits == 0 or dp['vfau'] >= (17 if bits == 16 else 18)) and isa and
              (bits != 0 or artifacts(out, f'draw_{name}.draw') == artifacts(out, f'draw_{name}.options0' if o0['rc'] == 0 else f'draw_{name}.draw')),
              result=dp, isa_fau_word=isa)
    v_plain = out/'draw_plain.vert.spv'
    # SELECT_RT identity with _rt.
    r_rt = run(new, [v_plain, fs_mrt, 3, 0, 1, 1], out, 'mrt.rt1')
    r_op = run(new, [v_plain, fs_mrt, 4, 2, 1, 1], out, 'mrt.options_rt1')
    check('options/select_rt_identity', r_rt['rc'] == 0 and r_op['rc'] == 0 and r_op['reserved'] == 2 and
          artifacts(out, 'mrt.rt1') == artifacts(out, 'mrt.options_rt1'))
    # Invalid options: rc1, nothing written.
    for name, args in (('null_options', [5, 0, 0, 1]), ('version0', [4, 0, 0, 0]), ('version2', [4, 0, 0, 2]),
                       ('unknown_flag', [4, 64, 0, 1]), ('rt_without_select', [4, 0, 1, 1]), ('rt8', [4, 2, 8, 1]),
                       ('tile_without_select', [4, 4, 0, 1]), ('reserved', [6, 0, 0, 1]),
                       ('ext_without_output', [7, 8, 0, 1]), ('output_without_ext', [7, 0, 0, 1])):
        r = run(new, [v_plain, fs_uv] + args, out, 'invalid_'+name)
        check(f'options/invalid/{name}', r['exit'] == 0 and r['rc'] == 1 and r['untouched'] == 1, result=r)
    # Extended varyings.
    for name, (vtext, ftext, rc, flat, np) in VARY.items():
        v = glsl(out, 'vary_'+name, vtext, 'vert'); f = glsl(out, 'vary_'+name, ftext, 'frag')
        d = run(new, [v, f, 0, 0, 0, 1], out, f'vary_{name}.default')
        x = run(new, [v, f, 4, 8, 0, 1], out, f'vary_{name}.ext')
        ok = x['exit'] == 0 and x['rc'] == rc
        if rc == 0:
            ok = ok and x['ext_version'] == 1 and x['flat'] == flat and x['ext_np'] == np and x['nopersp'] == np and \
                 x['vcount'] == 0 and x['fs_ld_var'] == 0 and bool(x['pflags'] & 2) == bool(x['uses_flat']) and \
                 x['ext_gsize'] == x['gsize'] and x['ext_count'] == bin(x['ext_vs']).count('1') and \
                 all(s[4] == (3 if (flat >> s[0]) & 1 else 2 if (np >> s[0]) & 1 else (1 if (x['ext_fs'] >> s[0]) & 1 else 0))
                     for s in x['slots'])
        else:
            ok = ok and x['ext_version'] == 0
        check(f'varyings/{name}', ok and d['exit'] == 0, default=d, extended=x)
    # Default entries keep refusing what they refused (no silent widening).
    for name in ('flat_uint', 'flat_float_noperspective', 'five_locations'):
        d = json.loads((out/f'vary_{name}.default.log').read_text().splitlines()[-1][7:])
        check(f'varyings/{name}/default_still_refuses', d['rc'] == 5)
    # Graphics TLS (THREAD_STORAGE option, flag 16): refused by default, compiled
    # with the option, per-thread stack bytes reported; .tl accesses in the ISA.
    f = glsl(out, 'private_small', PRIVATE_SMALL_FS, 'frag')
    r0 = run(new, [v_plain, f, 4, 9, 0, 1], out, 'tls_off')
    r1 = run(new, [v_plain, f, 4, 25, 0, 1], out, 'tls_on', env=dict(os.environ, BIFROST_MESA_DEBUG='shaders'))
    check('tls/refused_without_option', r0['exit'] == 0 and r0['rc'] != 0 and 'private memory' in r0['error'], result=r0)
    check('tls/compiled_with_option', r1['exit'] == 0 and r1['rc'] == 0 and r1['ftls'] > 0 and r1['vtls'] == 0 and
          '.tl' in (out/'tls_on.log').read_text(), result=r1)
    # Undefined VS output that the FS never reads: Mesa drops its slot; the
    # generic pair must still compile.  An FS read of a location whose every VS
    # store is undefined (Metal: an unassigned member of the returned struct,
    # e.g. QuartzCore std_vert0_lpf texcoord0) now gets a zero store and a
    # layout slot (mali_graphics_compiler.c zero_undef_read_varyings): any value
    # is a valid realisation of an undefined varying.
    v = glsl(out, 'undef_out', vs('uv=position; float u; k=u;', 'layout(location=0) out vec4 uv; layout(location=1) out float k;'), 'vert')
    r = run(new, [v, fs_uv, 4, 9, 0, 1], out, 'undef_unread')
    check('varyings/undefined_unread_output', r['exit'] == 0 and r['rc'] == 0 and r['ext_vs'] == 1, result=r)
    f = glsl(out, 'undef_read', '#version 450\nlayout(location=0) in vec4 uv; layout(location=1) in float k;\n'
             'layout(location=0) out vec4 r;\nvoid main(){ r=uv*k; }\n', 'frag')
    r = run(new, [v, f, 4, 9, 0, 1], out, 'undef_read')
    check('varyings/undefined_read_output_zeroed', r['exit'] == 0 and r['rc'] == 0 and r['ext_count'] == 2 and
          r['ext_fs'] == 3, result=r)
    # Raster order groups / pixel interlock: explicit refusal, never a partial lowering.
    f = glsl(out, 'interlock', '#version 450\n#extension GL_ARB_fragment_shader_interlock : require\n'
             'layout(pixel_interlock_ordered) in;\nlayout(location=0) in vec4 uv;\nlayout(location=0) out vec4 r;\n'
             'void main(){ beginInvocationInterlockARB(); r=uv; endInvocationInterlockARB(); }\n', 'frag')
    r = run(new, [v_plain, f, 4, 25, 0, 1], out, 'interlock')
    check('refusal/pixel_interlock', r['exit'] == 0 and r['rc'] != 0 and 'interlock' in r['error'], result=r)
    # True MRT (MULTI_TARGET=32, options version 2): full fragment, per-target
    # BLEND with FAU words 48+8*rt (u6,u7,u8), written mask in plan.reserved.
    mrt_fs = glsl(out, 'mrt3', '#version 450\nlayout(location=0) in vec4 uv;\nlayout(location=0) out vec4 a;\n'
                  'layout(location=1) out vec4 b;\nlayout(location=2) out vec4 c;\n'
                  'void main(){ a=uv; b=uv.wzyx; c=vec4(uv.x); }\n', 'frag')
    def mrt(tag, spec, flags=41, vtx=None, frag=None, env_debug=False):
        env = dict(os.environ, TEGU_MRT=spec)
        if env_debug: env['BIFROST_MESA_DEBUG'] = 'shaders'
        return run(new, [vtx or v_plain, frag or mrt_fs, 8, flags, 0, 2], out, 'mrt_'+tag, env=env)
    r = mrt('80_80_10', '3,80,80,10,0', env_debug=True); log = (out/'mrt_80_80_10.log').read_text()
    check('mrt/three_targets', r['exit'] == 0 and r['rc'] == 0 and r['reserved'] == 0x80000007 and (r['fsys'] & 8) and
          all(x in log for x in ('BLEND', ' u6,', ' u7,', ' u8,')), result=r)
    r = mrt('unbound_rt1', '3,80,0,10,0')
    check('mrt/unbound_slot_discarded', r['exit'] == 0 and r['rc'] == 0 and r['reserved'] == 0x80000005, result=r)
    r = mrt('beyond_list', '2,80,80,0,0')
    check('mrt/output_beyond_list_refused', r['exit'] == 0 and r['rc'] == 5 and 'beyond' in r['error'], result=r)
    f = glsl(out, 'mrt_int', '#version 450\nlayout(location=0) in vec4 uv;\nlayout(location=0) out vec4 a;\n'
             'layout(location=1) out uvec4 b;\nvoid main(){ a=uv; b=uvec4(uv*255.0); }\n', 'frag')
    r = mrt('int_output', '2,80,70,0,0', frag=f)
    check('mrt/integer_output_refused', r['exit'] == 0 and r['rc'] == 5 and 'float' in r['error'], result=r)
    f = glsl(out, 'mrt_dual', '#version 450\nlayout(location=0) in vec4 uv;\nlayout(location=0,index=0) out vec4 a;\n'
             'layout(location=0,index=1) out vec4 b;\nvoid main(){ a=uv; b=uv.wzyx; }\n', 'frag')
    r = mrt('dual_source', '1,80,0,0,0', frag=f)
    check('mrt/dual_source_refused', r['exit'] == 0 and r['rc'] == 5 and 'dual-source' in r['error'], result=r)
    for name, spec, flags in (('count0', '0,0,0,0,0', 41), ('count5', '5,80,80,80,80', 41), ('bad_format', '1,1,0,0,0', 41),
                              ('tail_format', '1,80,80,0,0', 41), ('reserved2', '1,80,0,0,0,1', 41),
                              ('v2_without_mrt', '1,80,0,0,0', 9), ('mrt_with_select_rt', '1,80,0,0,0', 43),
                              ('mrt_with_tile', '1,80,0,0,0', 45)):
        r = mrt('invalid_'+name, spec, flags)
        check(f'mrt/invalid/{name}', r['exit'] == 0 and r['rc'] == 1 and r['untouched'] == 1, result=r)
    r = run(new, [v_plain, mrt_fs, 4, 41, 0, 1], out, 'mrt_invalid_v1_struct')
    check('mrt/invalid/v1_struct', r['exit'] == 0 and r['rc'] == 1 and r['untouched'] == 1, result=r)
    for item in a.mrt_spv:
        name, rest = item.split('=', 1); vpath, fpath, spec = rest.split(':')
        r = mrt('corpus_'+name, spec, vtx=Path(vpath).resolve(), frag=Path(fpath).resolve(), env_debug=True)
        mask = sum(1 << i for i, x in enumerate(spec.split(',')[1:5]) if x != '0')
        check(f'mrt/corpus/{name}', r['exit'] == 0 and r['rc'] == 0 and (r['reserved'] & 0x80000000) and
              (r['reserved'] & 0xf) and not (r['reserved'] & 0xf & ~mask) and 'BLEND' in (out/f'mrt_corpus_{name}.log').read_text(), result=r)
    # Private memory: formerly abort class, now explicit refusal.
    f = glsl(out, 'private', PRIVATE_FS, 'frag')
    r = run(new, [v_plain, f, 0, 0, 0, 1], out, 'private_graphics')
    check('abort/graphics_private_memory', r['exit'] == 0 and r['rc'] != 0 and 'private memory' in r['error'], result=r)
    c = glsl(out, 'private', PRIVATE_CS, 'comp')
    r = run(new, ['compute', c, 0, 0, 0, 0], out, 'private_compute')
    check('abort/compute_private_memory', r['exit'] == 0 and r['rc'] == 4 and 'private' in r['error'], result=r)
    for spv in a.compute_spv:
        r = run(new, ['compute', spv.resolve(), 0, 0, 0, 0], out, 'corpus_compute_'+spv.parent.name)
        check(f'corpus/compute/{spv.parent.name}', r['exit'] == 0 and r['rc'] is not None, result=r)
    for d in a.corpus_graphics:
        vs_spv, fs_spv = d.resolve()/'vertex.spv', d.resolve()/'fragment.spv'
        res = {}
        for tag, args in (('default', [0, 0, 0, 1]), ('ext', [4, 8, 0, 1]), ('ext_draw', [4, 9, 0, 1]),
                          ('ext_draw_rt0', [4, 11, 0, 1])):
            res[tag] = run(new, [vs_spv, fs_spv] + args, out, f'corpus_{d.name}.{tag}')
        check(f'corpus/graphics/{d.name}', all(x['exit'] == 0 and x['rc'] is not None for x in res.values()),
              **{k: dict(rc=v['rc'], error=v.get('error', ''), flags=v.get('pflags'), flat=v.get('flat'),
                         nopersp=v.get('ext_np'), vsys=v.get('vsys')) for k, v in res.items()})
    # Old-vs-new identity of every default-path artifact.
    if 'old' in bins:
        for log in sorted(out.glob('*.default.log')):
            tag = log.name[:-4]; spv_args = None
            name = tag.split('.')[0]
            if name.startswith('draw_'): spv_args = [out/f'{name}.vert.spv', fs_uv]
            elif name.startswith('vary_'): spv_args = [out/f'{name}.vert.spv', out/f'{name}.frag.spv']
            if not spv_args: continue
            old = run(bins['old'], spv_args + [0, 0, 0, 1], out, tag+'.old')
            newr = json.loads(log.read_text().splitlines()[-1][7:])
            same_rc = old['rc'] == newr['rc']
            expected_change = name == 'draw_base_vertex' or name == 'draw_all_parameters'
            check(f'identity/{tag}', (old['exit'] != 0 and expected_change) or
                  (same_rc and (newr['rc'] != 0 or artifacts(out, tag) == artifacts(out, tag+'.old'))),
                  old_exit=old['exit'], old_rc=old['rc'], new_rc=newr['rc'])
    lib = a.graphics.resolve()
    report = dict(gpu_executed=False, compiler=str(lib), compiler_sha256=hashlib.sha256(lib.read_bytes()).hexdigest(),
                  baseline_sha256=hashlib.sha256(a.baseline.resolve().read_bytes()).hexdigest() if a.baseline else None,
                  cases=rows, failed=failures)
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{len(rows)-len(failures)}/{len(rows)} host checks passed; GPU_EXECUTED=0')
    if failures: raise SystemExit(1)


if __name__ == '__main__':
    main()
