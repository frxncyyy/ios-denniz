#!/usr/bin/env python3
"""Compare the shared native blend packer with real Mesa and independent math.

Host-only. Requires the pinned Mesa source/build used by the shader compiler.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import random
import subprocess


class Inputs(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'enabled', 'rgb_op', 'rgb_src',
        'rgb_dst', 'alpha_op', 'alpha_src', 'alpha_dst', 'write_mask')]


class State(C.Structure):
    _fields_ = [('words', C.c_uint32 * 4), ('reads_destination', C.c_uint32), ('opaque', C.c_uint32)]


class Guarded(C.Structure):
    _fields_ = [('before', C.c_uint64), ('state', State), ('after', C.c_uint64)]


REFERENCE = r'''
#include <stdio.h>
#include "pan_blend.h"
int main(void) {
 const enum pipe_blendfactor f[]={PIPE_BLENDFACTOR_ZERO,PIPE_BLENDFACTOR_ONE,
  PIPE_BLENDFACTOR_SRC_COLOR,PIPE_BLENDFACTOR_INV_SRC_COLOR,PIPE_BLENDFACTOR_SRC_ALPHA,
  PIPE_BLENDFACTOR_INV_SRC_ALPHA,PIPE_BLENDFACTOR_DST_COLOR,PIPE_BLENDFACTOR_INV_DST_COLOR,
  PIPE_BLENDFACTOR_DST_ALPHA,PIPE_BLENDFACTOR_INV_DST_ALPHA};
 const enum pipe_blend_func op[]={PIPE_BLEND_ADD,PIPE_BLEND_SUBTRACT,PIPE_BLEND_REVERSE_SUBTRACT};
 for(unsigned o=0;o<3;o++)for(unsigned s=0;s<10;s++)for(unsigned d=0;d<10;d++)for(unsigned alpha=0;alpha<2;alpha++) {
  struct pan_blend_equation e={0};e.blend_enable=1;e.color_mask=15;
  e.rgb_func=alpha?PIPE_BLEND_ADD:op[o];e.rgb_src_factor=alpha?f[4]:f[s];e.rgb_dst_factor=alpha?f[5]:f[d];
  e.alpha_func=alpha?op[o]:PIPE_BLEND_ADD;e.alpha_src_factor=alpha?f[s]:f[4];e.alpha_dst_factor=alpha?f[d]:f[5];
  pan_blend_optimize_equation(&e,PIPE_FORMAT_R8G8B8A8_UNORM,NULL);
  int ff=pan_blend_can_fixed_function(11,e,1);
  printf("%u %u %u %u %d %d %d %08x\n",o,s,d,alpha,ff,pan_blend_reads_dest(e),pan_blend_is_opaque(e),ff?pan_pack_blend(e):0);
 }
}
'''


def metal(op, sf, df, src, dst, channel):
    def factor(value):
        if value == 0:return 0
        if value == 1:return 1
        v = {2: src[channel], 4: src[3], 6: dst[channel], 8: dst[3]}
        return v[value] if value % 2 == 0 else 1-v[value-1]
    a, b = src[channel] * factor(sf), dst[channel] * factor(df)
    return (a+b, a-b, b-a)[op]


def mali(word, src, dst, channel):
    s, d = src[channel], dst[channel]
    a = {1: 0, 2: s, 3: d}[word & 3]
    b = {0: s-d, 1: s+d, 2: s, 3: d}[(word >> 4) & 3]
    c = {1: 0, 2: s, 3: d, 4: 2*s, 5: src[3], 6: dst[3]}[(word >> 8) & 7]
    if word & (1 << 11):c = 1-c
    return (-a if word & 8 else a) + (-b if word & 128 else b)*c


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('mesa-source', 'mesa-build', 'out'):p.add_argument('--'+n, type=Path, required=True)
    a = p.parse_args();a.out.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).resolve().parent
    native = a.out / 'blend.so'
    subprocess.run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
        str(source/'blend_state.c'), '-o', str(native)], check=True)
    lib = C.CDLL(str(native.resolve()));build = lib.tegu_blend_build
    build.argtypes = [C.POINTER(Inputs), C.POINTER(State)];build.restype = C.c_int
    guard = Guarded();guard.before = guard.after = 0xa5a5a5a5deadbeef
    def run(inputs):
        prior = bytes(guard.state)
        result = build(C.byref(inputs), C.byref(guard.state))
        assert guard.before == guard.after == 0xa5a5a5a5deadbeef
        if result:assert bytes(guard.state) == prior
        return result, guard.state
    includes = [a.mesa_source/'include', a.mesa_source/'src', a.mesa_source/'src/gallium/include',
        a.mesa_source/'src/panfrost/lib', a.mesa_source/'src/panfrost', a.mesa_source/'src/compiler/nir',
        a.mesa_source/'src/compiler', a.mesa_build/'src', a.mesa_build/'src/compiler', a.mesa_build/'src/compiler/nir',
        a.mesa_build/'src/panfrost/lib', a.mesa_build/'src/panfrost', a.mesa_build/'src/util', a.mesa_source/'src/util']
    ref = a.out/'mesa-reference.c';ref.write_text(REFERENCE);exe = a.out/'mesa-reference'
    subprocess.run(['cc', '-O1', '-DUTIL_ARCH_LITTLE_ENDIAN=1', '-DUTIL_ARCH_BIG_ENDIAN=0',
        '-DHAVE_PTHREAD=1', '-DHAVE_ENDIAN_H=1', str(ref),
        str(a.mesa_build/'src/panfrost/lib/libpanfrost_lib.a.p/pan_blend.c.o'),
        str(a.mesa_build/'src/util/libmesa_util.a'), str(a.mesa_build/'src/c11/impl/libmesa_util_c11.a'),
        *['-I'+str(i) for i in includes], '-lm', '-lpthread', '-o', str(exe)], check=True)
    rows = subprocess.check_output([str(exe.resolve())], text=True).splitlines()
    assert len(rows) == 600
    randoms = random.Random(68);samples = [[randoms.random() for _ in range(4)] for _ in range(12)]
    accepted = refused = mathematical = 0
    for line in rows:
        o, s, d, alpha, ff, rd, opaque, equation = line.split()
        o, s, d, alpha, ff, rd, opaque = map(int, (o, s, d, alpha, ff, rd, opaque))
        rgb = (0,4,5) if alpha else (o,s,d);ach = (o,s,d) if alpha else (0,4,5)
        status, state = run(Inputs(1,1,*rgb,*ach,15))
        if status:
            assert not ff, line
            refused += 1;continue
        accepted += 1
        assert ff and state.words[1] == int(equation,16), (line,hex(state.words[1]))
        assert state.reads_destination == rd and state.opaque == opaque
        assert tuple(state.words) == (0xa00|rd,int(equation,16),0x18|(1 if opaque else 2),237<<12)
        for i, src in enumerate(samples):
            dst = samples[-1-i]
            for channel in range(4):
                packed = (state.words[1] >> (12 if channel==3 else 0)) & 0xfff
                want = metal(*(ach if channel==3 else rgb),src,dst,channel)
                assert abs(mali(packed,src,dst,channel)-want) < 1e-9
                mathematical += 1
    # Disabled state ignores the equations but still requires bounded enums.
    status, state = run(Inputs(1,0,0,1,0,0,1,0,15))
    assert not status and tuple(state.words) == (0xa00,0xf0122122,0x19,237<<12)
    valid = Inputs(1,1,0,4,5,0,1,5,15)
    for field, _ in Inputs._fields_:
        bad = Inputs.from_buffer_copy(bytes(valid));setattr(bad,field,0xffffffff)
        assert run(bad)[0]
    before = bytes(guard.state)
    assert build(None,C.byref(guard.state)) and bytes(guard.state)==before
    assert build(C.byref(valid),None)
    result = dict(gpu_executed=False,mesa_rows=600,accepted=accepted,refused=refused,
                  independent_channel_checks=mathematical,output_guards=True,refusal_preserves_output=True)
    (a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS native blend packer: Mesa600 + independent arithmetic + ABI/refusal guards',result)


if __name__ == '__main__':main()
