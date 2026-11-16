#!/usr/bin/env python3
"""TGP2 private (TLS) and workgroup (WLS) memory: real host compiles.

Each case runs in an isolated process; a backend assert is a FAIL. With
--expect on (pipeline_format.h defines TEGU_PIPELINE2_WORD_TLS/WLS) packets
must carry header word17 = per-thread tls bytes and word18 = per-workgroup
wls bytes, and the Valhall ISA must use the matching segment (STORE/LOAD .wls,
.tl) and BARRIER. With --expect off the same inputs must refuse cleanly.
Optional --spv NAME=PATH[:mode] adds real corpus modules (mode compile,
refuse or any). Host only; no GPU execution or dispatch correctness claim.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

C = r'''
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned w(const unsigned char *p,int i){p+=4*i;return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24;}
int main(int c,char **v) {
 if(c<2)return 90;
 FILE *f=fopen(v[1],"rb");if(!f)return 90;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 void *d=malloc(n);if(!d||fread(d,1,n,f)!=(size_t)n)return 90;fclose(f);
 /* Optional ID=VALUE uint32 specialization pairs (Metal threadgroup lengths,
  * local sizes) exactly as the planner's per-region constant table. */
 struct tegu_mali_specialization sp[16];unsigned k=0;
 for(int i=2;i<c&&k<16;i++){unsigned id,val;if(sscanf(v[i],"%u=%u",&id,&val)!=2)return 90;
  sp[k].id=id;sp[k].size=4;memset(sp[k].data,0,8);memcpy(sp[k].data,&val,4);k++;}
 struct tegu_mali_blob b={0};char e[300]={0};int rc=tegu_mali_compile_compute_v2(d,n,k?sp:NULL,k,&b,e,sizeof e);
 printf("RESULT {\"rc\":%d,\"size\":%zu,\"tls\":%u,\"wls\":%u,\"local\":[%u,%u,%u],\"error\":\"%s\"}\n",rc,b.size,
  rc?0:w(b.data,17),rc?0:w(b.data,18),rc?0:w(b.data,9),rc?0:w(b.data,10),rc?0:w(b.data,11),e);
 if(rc&&(b.data||b.size))return 92;
 tegu_mali_free(&b);return 0;
}
'''
HEAD = '#version 450\nlayout(local_size_x=64) in;\nlayout(set=0,binding=0) buffer B { %s o[]; };\n'
CASES = {
    # name: (source, target env, expected tls, expected wls, isa markers) ; None = must refuse in both modes
    'shared_barrier': (HEAD % 'float' + 'shared float tile[64];\nvoid main(){ uint i=gl_LocalInvocationIndex;'
                       ' tile[i]=o[gl_GlobalInvocationID.x]; barrier(); o[gl_GlobalInvocationID.x]=tile[63u-i]+tile[(i+1u)&63u]; }\n',
                       'vulkan1.0', 0, 256, ('.wls', 'BARRIER')),
    'shared_atomic': (HEAD % 'uint' + 'shared uint cnt;\nvoid main(){ if(gl_LocalInvocationIndex==0u) cnt=0u; barrier();'
                      ' atomicAdd(cnt,1u); barrier(); o[gl_WorkGroupID.x]=cnt; }\n', 'vulkan1.0', 0, 4, ('.wls', 'BARRIER')),
    'shared_float_atomic': ('#version 450\n#extension GL_EXT_shader_atomic_float : require\n' + (HEAD % 'float').split('\n', 1)[1] +
                            'shared float acc;\nvoid main(){ if(gl_LocalInvocationIndex==0u) acc=0.0; barrier();'
                            ' atomicAdd(acc,o[gl_GlobalInvocationID.x]); barrier(); if(gl_LocalInvocationIndex==0u)'
                            ' o[gl_WorkGroupID.x]=acc; }\n', 'vulkan1.1', 0, 4, ('.wls', 'BARRIER')),
    'shared_struct_vec3': (HEAD % 'vec4' + 'struct S { vec3 a; float b; uint c[3]; };\nshared S s[64];\nvoid main(){'
                           ' uint i=gl_LocalInvocationIndex; s[i].a=o[i].xyz; s[i].b=o[i].w; s[i].c[i%3u]=i; barrier();'
                           ' o[i]=vec4(s[63u-i].a,s[63u-i].b+float(s[63u-i].c[0])); }\n', 'vulkan1.0', 0, None, ('.wls',)),
    'private_array': ('#version 450\nlayout(local_size_x=8) in;\nlayout(set=0,binding=0) buffer B { uint o[]; };\n'
                      'void main(){ uint t[16]; for(uint i=0u;i<16u;i++) t[i]=o[i]*i;'
                      ' o[gl_GlobalInvocationID.x]=t[o[gl_GlobalInvocationID.x]&15u]; }\n', 'vulkan1.0', 64, 0, ('.tl',)),
    'shared_over_32k': (HEAD % 'vec4' + 'shared vec4 big[2049];\nvoid main(){ big[gl_LocalInvocationIndex]=o[gl_GlobalInvocationID.x];'
                        ' barrier(); o[gl_GlobalInvocationID.x]=big[2048u-gl_LocalInvocationIndex]; }\n', 'vulkan1.0', None, None, ()),
    'private_over_tls_immediate': ('#version 450\nlayout(local_size_x=8) in;\nlayout(set=0,binding=0) buffer B { vec4 o[]; };\n'
                                   'void main(){ vec4 t[2304]; for(int i=0;i<2304;i++) t[i]=vec4(i);'
                                   ' o[gl_GlobalInvocationID.x]=t[gl_GlobalInvocationID.x%2304u]; }\n', 'vulkan1.0', None, None, ()),
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--expect', choices=('on', 'off'), required=True)
    p.add_argument('--spv', action='append', default=[])
    p.add_argument('--tg-spv', action='append', default=[], help='NAME=PATH:ID=V,ID=V:TGSPECID:len,len,...')
    a = p.parse_args(); out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    lib = a.compiler.resolve(); root = Path(__file__).resolve().parent
    (out/'check.c').write_text(C)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-I'+str(root), str(out/'check.c'), str(lib),
                    '-Wl,-rpath,'+str(lib.parent), '-o', str(out/'check')], check=True)
    rows = []; on = a.expect == 'on'

    def run(name, spv, spec=()):
        r = subprocess.run([str(out/'check'), str(spv)] + list(spec), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           timeout=120, env=dict(os.environ, BIFROST_MESA_DEBUG='shaders'))
        (out/(name+'.log')).write_text(r.stdout)
        line = [x for x in r.stdout.splitlines() if x.startswith('RESULT ')]
        res = json.loads(line[-1][7:]) if line and r.returncode == 0 else {'rc': None}
        res['exit'] = r.returncode; return res, r.stdout

    for name, (src, env, tls, wls, markers) in CASES.items():
        (out/(name+'.comp')).write_text(src); spv = out/(name+'.spv')
        subprocess.run(['glslangValidator', '--target-env', env, '-V', str(out/(name+'.comp')), '-o', str(spv)],
                       check=True, capture_output=True)
        subprocess.run(['spirv-val', '--target-env', env, str(spv)], check=True, capture_output=True)
        res, log = run(name, spv)
        if tls is None and wls is None:
            ok = res['exit'] == 0 and res['rc'] == 4
        elif on:
            isa = [l for l in log.splitlines() if l.startswith(('   ', '\t'))]
            ok = res['exit'] == 0 and res['rc'] == 0 and res['tls'] == tls and (wls is None and res['wls'] > 0 or res['wls'] == wls) \
                and all(any(m in l for l in isa) for m in markers)
        else:
            ok = res['exit'] == 0 and res['rc'] == 4 and 'private or workgroup' in res['error']
        rows.append(dict(case=name, passed=ok, result=res)); print(json.dumps(rows[-1]), flush=True)
    for item in a.spv:
        name, rest = item.split('=', 1); path, _, mode = rest.partition(':'); mode = mode or 'any'
        res, _ = run('spv_'+name, Path(path).resolve())
        ok = res['exit'] == 0 and res['rc'] is not None and (mode == 'any' or (mode == 'compile') == (res['rc'] == 0))
        rows.append(dict(case='spv/'+name, passed=ok, mode=mode, spirv_sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(),
                         result=res)); print(json.dumps(rows[-1]), flush=True)
    # Metal dynamic threadgroup arguments (setThreadgroupMemoryLength): the
    # translator sizes the Workgroup array from SpecKey::TgLength; the planner
    # passes the encoder length per region. Expected WLS = max(32, align16(len))
    # (translator Select(len/4 < 8, 8, len/4) uint words), > 32KiB refused.
    for item in a.tg_spv:
        name, rest = item.split('=', 1); path, ids, tg, lens = rest.split(':')
        base = [x for x in ids.split(',') if x]
        for ln in [int(x) for x in lens.split(',')]:
            res, _ = run(f'tg_{name}_{ln}', Path(path).resolve(), base + [f'{tg}={ln}'])
            want = max(32, (ln + 15) // 16 * 16)
            ok = res['exit'] == 0 and ((res['rc'] == 0 and res['wls'] == want) if want <= 32768 else
                                       (res['rc'] == 4 and 'exceeds' in res['error']))
            rows.append(dict(case=f'tg/{name}/{ln}', passed=ok, expected_wls=want, result=res)); print(json.dumps(rows[-1]), flush=True)
    failed = [r['case'] for r in rows if not r['passed']]
    (out/'result.json').write_text(json.dumps(dict(gpu_executed=False, expect=a.expect, compiler=str(lib),
        compiler_sha256=hashlib.sha256(lib.read_bytes()).hexdigest(), cases=rows, failed=failed), indent=2)+'\n')
    print(f'{len(rows)-len(failed)}/{len(rows)} TGP2 memory checks passed (expect {a.expect}); GPU_EXECUTED=0')
    if failed: raise SystemExit(1)


if __name__ == '__main__':
    main()
