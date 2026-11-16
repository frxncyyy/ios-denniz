#!/usr/bin/env python3
"""Colour render-target formats: n2r/blend_state against an oracle derived
from the pinned Mesa 81daea66 sources themselves (not from our tables).

For every blendable colour target n2r admits, the expected values are
computed here from
  src/panfrost/lib/pan_format.c   pan_blendable_formats (BFMT*), MALI_BLEND_*
  src/panfrost/genxml/v9.xml      enum values (internal/writeback/pixel format,
                                  component order)
  src/util/format/u_format.yaml   channel layout and swizzle
  src/panfrost/lib/pan_clear.c    tib_layouts (clear packing)
following get_rt_formats (pan_desc.c: inverted swizzle,
pan_translate_swizzle_4), pan_format_tib_size, pan_dithered_format_from_pipe_
format with the v7+ "fixed function does not like RGB1" rewrite and BLEND
cfg.srgb, and pan_pack_color (not dithered).  A C harness prints what
n2r_rt_format / n2r_rt_srgb / n2r_tib_bytes / n2r_bpp / tegu_blend_build_format
/ n2r_clear produce, and every field is compared.  Host-only: no GPU claim.
"""
import json, re, struct, subprocess, sys, os


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]
from pathlib import Path

G = Path(__file__).resolve().parents[1]
MESA = G / 'build/mesa-src/src'
OUT = G / 'build/texture-coverage-20261026/host'
OUT.mkdir(parents=True, exist_ok=True)

# Metal pixel format -> PIPE format (translator metal/format: Metal packed
# formats name channels MSB-first, PIPE packed formats LSB-first).
METAL_PIPE = {10: 'R8_UNORM', 25: 'R16_FLOAT', 40: 'B5G6R5_UNORM', 42: 'A4B4G4R4_UNORM',
              43: 'B5G5R5A1_UNORM', 65: 'R16G16_FLOAT', 70: 'R8G8B8A8_UNORM', 71: 'R8G8B8A8_SRGB',
              80: 'B8G8R8A8_UNORM', 81: 'B8G8R8A8_SRGB', 90: 'R10G10B10A2_UNORM',
              92: 'R11G11B10_FLOAT', 94: 'B10G10R10A2_UNORM', 115: 'R16G16B16A16_FLOAT',
              1: 'A8_UNORM', 11: 'R8_SRGB', 30: 'R8G8_UNORM', 31: 'R8G8_SRGB'}
# Raw (non-blendable) targets: opaque output only.
RAW_PIPE = {12: 'R8_SNORM', 13: 'R8_UINT', 14: 'R8_SINT', 20: 'R16_UNORM', 22: 'R16_SNORM', 23: 'R16_UINT',
            24: 'R16_SINT', 32: 'R8G8_SNORM', 33: 'R8G8_UINT', 34: 'R8G8_SINT', 53: 'R32_UINT', 54: 'R32_SINT',
            55: 'R32_FLOAT', 60: 'R16G16_UNORM', 62: 'R16G16_SNORM', 63: 'R16G16_UINT', 64: 'R16G16_SINT',
            72: 'R8G8B8A8_SNORM', 73: 'R8G8B8A8_UINT', 74: 'R8G8B8A8_SINT', 91: 'R10G10B10A2_UINT',
            103: 'R32G32_UINT', 104: 'R32G32_SINT', 105: 'R32G32_FLOAT', 110: 'R16G16B16A16_UNORM',
            112: 'R16G16B16A16_SNORM', 113: 'R16G16B16A16_UINT', 114: 'R16G16B16A16_SINT',
            123: 'R32G32B32A32_UINT', 124: 'R32G32B32A32_SINT', 125: 'R32G32B32A32_FLOAT'}
# Tile-word bits the writeback stores, where the format has fewer channels
# than the R8G8B8A8 / R5G6B5A0 tile word.
CLEAR_MASK = {10: 0xff, 11: 0xff, 1: 0xff000000, 30: 0xffff, 31: 0xffff, 40: 0x3fffffff}

# ---- v9.xml enums ----
v9 = (MESA / 'panfrost/genxml/v9.xml').read_text()


def enum(name):
    body = v9.split(f'<enum name="{name}">', 1)[1].split('</enum>', 1)[0]
    return {m.group(1): int(m.group(2), 0) for m in re.finditer(r'<value name="([^"]+)" value="([^"]+)"', body)}


INTERNAL = enum('Color Buffer Internal Format')
COLOR = enum('Color Format')
FLOAT_COLOR = enum('Float Color Format')
ORDER = enum('RGB Component Order')
PIXEL = enum('Format')

# ---- pan_format.c blendable table ----
pf = (MESA / 'panfrost/lib/pan_format.c').read_text()
blend_defs = {}
for m in re.finditer(r'#define (MALI_BLEND_\w+)\s+\((MALI_\w+) << 12\)\s*\|\s*(MALI_\w+)', pf):
    blend_defs[m.group(1)] = (m.group(2)[5:], m.group(3))
table = pf.split('GENX(pan_blendable_formats)[PIPE_FORMAT_COUNT] = {', 1)[1].split('};', 1)[0]
table = re.sub(r'#if PAN_ARCH < 6.*?#endif', '', table, flags=re.S)
table = re.sub(r'#if PAN_ARCH >= 9\n', '', table).replace('#endif', '')
BLENDABLE = {}
for m in re.finditer(r'(BFMT_SRGB|BFMT_FLOAT|BFMT2|BFMT)\(([^)]*)\)', table):
    kind, args = m.group(1), [a.strip() for a in m.group(2).split(',')]
    if kind == 'BFMT_SRGB':
        BLENDABLE[args[0] + '_UNORM'] = ('R8G8B8A8', args[1], False, 0)
        BLENDABLE[args[0] + '_SRGB'] = ('R8G8B8A8', args[1], False, 1)
    elif kind == 'BFMT2':
        BLENDABLE[args[0]] = (args[1], args[2], False, int(args[3]))
    elif kind == 'BFMT':
        BLENDABLE[args[0]] = (args[1], args[1], False, 0)
    else:
        BLENDABLE[args[0]] = (args[1], args[1], True, 0)


def pixel_value(name):
    return PIXEL[name.replace('_', ' ')]


def blend_memory_format(internal, is_float, srgb, fixed_function):
    key = ('MALI_BLEND_' + internal) if is_float else ('MALI_BLEND_PU_' + internal)
    fmt, swz = blend_defs[key]
    order = ORDER['RGB1'] if swz == 'MALI_RGB1_SWIZZLE' else ORDER['RGBA']
    if fixed_function and order == ORDER['RGB1']:
        order = ORDER['RGBA']
    return (pixel_value(fmt) << 12) | order | ((1 << 20) if srgb else 0)


# ---- u_format.yaml ----
yaml = (MESA / 'util/format/u_format.yaml').read_text()


def pipe_desc(name):
    body = yaml.split(f'- name: {name}\n', 1)[1].split('\n- name:', 1)[0]
    chans = re.search(r'channels: \[([^\]]*)\]', body).group(1).split(',')
    swz = re.search(r'swizzles: \[([^\]]*)\]', body).group(1).split(',')
    bits = sum(int(re.sub(r'[A-Z]', '', c.strip())) for c in chans)
    return [s.strip() for s in swz], bits, [c.strip() for c in chans]


SW = {'X': 0, 'Y': 1, 'Z': 2, 'W': 3, '0': 4, '1': 5}


def rt_swizzle(swz):
    out = [4, 4, 4, 4]  # PIPE_SWIZZLE_0
    for c in range(4):
        i = SW[swz[c]]
        if i > 3:
            continue
        if out[i] == 4:
            out[i] = c
    return sum(v << (3 * i) for i, v in enumerate(out))


# ---- pan_clear.c tib layouts ----
pc = (MESA / 'panfrost/lib/pan_clear.c').read_text()
LAYOUT = {}
for m in re.finditer(r'\[MALI_COLOR_BUFFER_INTERNAL_FORMAT_(\w+)\]\s*=\s*\{([^}]*)\}', pc):
    LAYOUT[m.group(1)] = [int(x) for x in m.group(2).split(',')]


def roundeven(x):
    f = int(x)
    d = x - f
    return f + 1 if d > 0.5 or (d == 0.5 and f & 1) else f


def srgb_encode(l):
    return l * 12.92 if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055


PACKED16 = {'B5G6R5_UNORM', 'A4B4G4R4_UNORM', 'B5G5R5A1_UNORM'}


def metal_unorm(x, bits, through_half):
    # metal2vulkan-runtime clear_value.rs unorm(): through float (and half for
    # the packed 16-bit formats), NaN 0, clamp, nearest-even.
    v = f32(x)
    if through_half:
        v = (float('inf') if v > 0 else float('-inf')) if abs(v) >= 65520.0 else struct.unpack('<e', struct.pack('<e', v))[0]
    v = min(max(v, 0.0), 1.0)
    return roundeven(v * ((1 << bits) - 1))


def metal_srgb(x):
    v = min(max(f32(x), 0.0), 1.0)
    return roundeven(srgb_encode(v) * 255)


def clear_word(pipe, internal, rgba):
    # Tile word layout from Mesa pan_clear.c tib_layouts (value << frac,
    # channels from bit 0); channel values are Metal's conversion.
    l = LAYOUT[internal]
    swz, _, _ = pipe_desc(pipe)
    present = [c in 'XYZW' for c in swz]
    word, at = 0, 0
    for i in range(4):
        bi, bf = l[2 * i], l[2 * i + 1]
        if pipe.endswith('_SRGB') and i < 3:
            k = metal_srgb(rgba[i])
        elif i == 3 and not present[3]:
            # Absent alpha: Mesa's packer stores alpha 1.0 (int bits only).
            k = (1 << bi) - 1
        else:
            k = metal_unorm(rgba[i], bi, pipe in PACKED16)
        word |= (k << bf) << at
        at += bi + bf
    return word


def uf(val, mant):
    # clear_value.rs ufloat_value + ufloat_bits: truncated mantissa, negatives
    # 0, finite values past the largest clamp to it.
    import math
    v = f32(val)
    if v <= 0:
        return 0
    largest = (2 - 2.0 ** -mant) * 2 ** 15
    v = min(v, largest)
    e = max(math.frexp(v)[1] - 1, -14)
    scaled = int(v / 2.0 ** e * (1 << mant))
    if scaled < (1 << mant):
        return scaled
    return ((e + 15) << mant) | (scaled - (1 << mant))


# Includes values whose half / float steps matter (ABGR4 alpha 0.1 -> 1).
COLORS = ([.25, .5, .75, 1.0], [1.0, 0.0, 0.3, 0.6], [0.002, 0.04, 0.9, 0.1], [0.7, 1e-5, 70000.0, -1.0])
expect = {}
for metal, pipe in METAL_PIPE.items():
    internal, writeback, is_float, srgb = BLENDABLE[pipe]
    swz, bits, _ = pipe_desc(pipe)
    blocksize = bits // 8
    tib = 4 if not is_float else 1 << (blocksize - 1).bit_length()
    e = {'internal': INTERNAL[internal],
         'writeback': (FLOAT_COLOR if is_float else COLOR)[writeback],
         'swizzle': rt_swizzle(swz), 'srgb': srgb, 'tib': tib, 'bpp': blocksize,
         'opaque_fmt': blend_memory_format(internal, is_float, srgb, False),
         'ff_fmt': blend_memory_format(internal, is_float, srgb, True),
         'srgb_bit': srgb}
    if not is_float and internal in LAYOUT:
        e['clear'] = [clear_word(pipe, internal, c) for c in COLORS]
    if pipe == 'R11G11B10_FLOAT':
        e['clear'] = [uf(c[0], 6) | (uf(c[1], 6) << 11) | (uf(c[2], 5) << 22) for c in COLORS]
    expect[metal] = e

FMT = {}
for m in re.finditer(r'FMT\((\w+),\s*(\w+),\s*(\w+),\s*(\w),\s*(\w+)\)', pf):
    FMT.setdefault(m.group(1), (m.group(2), m.group(3)))
ORDER_ALIAS = {'R001': 'RGB1', 'RG01': 'RGB1'}


def metal_snorm(x, bits):
    m = (1 << (bits - 1)) - 1
    v = min(max(f32(x), -1.0), 1.0)
    k = v * m
    return (-roundeven(-k) if k < 0 else roundeven(k)) & ((1 << bits) - 1)


def metal_uint(x, bits):
    mx = (1 << bits) - 1
    return 0 if x <= 0 else mx if x >= mx else int(x)


def metal_sint(x, bits):
    mx, mn = (1 << (bits - 1)) - 1, -(1 << (bits - 1))
    k = mx if x >= mx else mn if x <= mn else int(x)
    return k & ((1 << bits) - 1)


def raw_clear(pipe, rgba):
    _, bits, chans = pipe_desc(pipe)
    texel, at = 0, 0
    for i, ch in enumerate(chans):
        kind, b = ch[:2] if ch[:2] in ('UN', 'SN', 'UP', 'SP') else ch[:1], int(re.sub(r'[A-Z]', '', ch))
        x = rgba[i]
        if ch.startswith('UN'):
            v = metal_unorm(x, b, False)
        elif ch.startswith('SN'):
            v = metal_snorm(x, b)
        elif ch.startswith('UP'):
            v = metal_uint(x, b)
        elif ch.startswith('SP'):
            v = metal_sint(x, b)
        elif ch.startswith('F'):
            v = struct.unpack('<I', struct.pack('<f', f32(x)))[0]
        else:
            raise AssertionError(ch)
        texel |= v << at
        at += b
    size = bits // 8
    tb = texel.to_bytes(size, 'little')
    if size == 1:
        tb = tb * 4
    elif size == 2:
        tb = tb * 2
    word = int.from_bytes(tb[:4], 'little')
    return word


for metal, pipe in RAW_PIPE.items():
    swz, bits, _ = pipe_desc(pipe)
    size = bits // 8
    mali, order = FMT[pipe]
    e = {'raw': True, 'internal': INTERNAL['RAW%d' % (8 << (size - 1).bit_length())],
         'writeback': COLOR['RAW%d' % bits], 'swizzle': 0x688, 'srgb': 0,
         'tib': 1 << (size - 1).bit_length(), 'bpp': size,
         'opaque_fmt': (pixel_value(mali) << 12) | ORDER[ORDER_ALIAS.get(order, order)],
         'clear': [raw_clear(pipe, c) for c in COLORS]}
    expect[metal] = e

harness = OUT / 'rt_format_oracle.c'
harness.write_text(r'''
#include "nord2_metal_render.c"
#include <stdio.h>
int main(void){
 static const unsigned f[]={%s};
 static const double colors[4][4]={{.25,.5,.75,1.0},{1.0,0.0,0.3,0.6},{0.002,0.04,0.9,0.1},{0.7,1e-5,70000.0,-1.0}};
 for(unsigned i=0;i<sizeof(f)/sizeof(f[0]);i++){
  unsigned internal=0,writeback=0,swizzle=0;int rc=n2r_rt_format(f[i],&internal,&writeback,&swizzle);
  struct tegu_blend_inputs opaque={1,0,0,1,0,0,1,0,15},over={1,1,0,1,5,0,1,5,15};
  struct tegu_blend_state o={{0}},b={{0}};int ro=tegu_blend_build_format(f[i],&opaque,&o),rb=tegu_blend_build_format(f[i],&over,&b);
  printf("{\"f\":%%u,\"rc\":%%d,\"internal\":%%u,\"writeback\":%%u,\"swizzle\":%%u,\"srgb\":%%u,\"tib\":%%u,\"bpp\":%%u,"
   "\"ro\":%%d,\"o0\":%%u,\"o2\":%%u,\"o3\":%%u,\"rb\":%%d,\"b0\":%%u,\"b2\":%%u,\"b3\":%%u,\"clear\":[",
   f[i],rc,internal,writeback,swizzle,n2r_rt_srgb(f[i]),n2r_tib_bytes(f[i]),n2r_bpp(f[i]),ro,o.words[0],o.words[2],o.words[3],rb,b.words[0],b.words[2],b.words[3]);
  for(unsigned c=0;c<4;c++){uint32_t w[4]={0};int rcc=n2r_clear(f[i],colors[c],w);printf("%%s%%ld",c?",":"",rcc?-1L:(long)w[0]);}
  printf("]}\n");
 }
 return 0;
}
''' % ','.join(str(f) for f in sorted(expect)))
exe = OUT / 'rt_format_oracle'
cmd = ['cc', '-std=c11', '-O1', '-fsanitize=address,undefined', '-I' + str(G / 'metal'), '-I' + str(G / 'metal/tegu'),
       '-I' + str(G / 'build/tegu-frontend'), str(harness), str(G / 'metal/tegu/blend_state.c'), '-o', str(exe)]
subprocess.run(cmd, check=True)
got = {}
for line in subprocess.run([str(exe)], check=True, stdout=subprocess.PIPE).stdout.decode().splitlines():
    d = json.loads(line)
    got[d['f']] = d
fail = 0
for f, e in sorted(expect.items()):
    g = got[f]
    if e.get('raw'):
        checks = [('rt rc', g['rc'], 0), ('internal', g['internal'], e['internal']),
                  ('writeback', g['writeback'], e['writeback']), ('swizzle', g['swizzle'], e['swizzle']),
                  ('srgb', g['srgb'], 0), ('tib', g['tib'], e['tib']), ('bpp', g['bpp'], e['bpp']),
                  ('blend opaque rc', g['ro'], 0), ('opaque mode', g['o2'] & 3, 1),
                  ('opaque memory format', g['o3'], e['opaque_fmt']),
                  ('blending refused', g['rb'] != 0, True), ('clear', g['clear'], e['clear'])]
        for name, a, b in checks:
            if a != b:
                fail += 1
                print(f'FAIL raw format {f} {name}: n2r {a} != Mesa/Metal-derived {b}')
        continue
    checks = [('rt rc', g['rc'], 0), ('internal', g['internal'], e['internal']),
              ('writeback', g['writeback'], e['writeback']), ('srgb', g['srgb'], e['srgb']),
              ('tib', g['tib'], e['tib']), ('bpp', g['bpp'], e['bpp']),
              ('blend opaque rc', g['ro'], 0), ('blend over rc', g['rb'], 0),
              ('opaque mode', g['o2'] & 3, 1), ('over mode', g['b2'] & 3, 2),
              ('opaque memory format', g['o3'], e['opaque_fmt']), ('over memory format', g['b3'], e['ff_fmt']),
              ('blend srgb bit', (g['b0'] >> 10) & 1, e['srgb_bit']), ('opaque srgb bit', (g['o0'] >> 10) & 1, e['srgb_bit'])]
    # Single-channel/two-channel targets: only the swizzle entries of the
    # channels the writeback stores are meaningful (Mesa fills absent ones
    # with PIPE_SWIZZLE_0; n2r keeps identity there).
    used = {10: 1, 25: 1, 65: 2, 11: 1, 30: 2, 31: 2}.get(f, 4)
    mask = (1 << (3 * used)) - 1
    checks.append(('swizzle', g['swizzle'] & mask, e['swizzle'] & mask))
    if 'clear' in e:
        mask = CLEAR_MASK.get(f, 0xffffffff)
        checks.append(('clear', [w & mask if w >= 0 else w for w in g['clear']], [w & mask for w in e['clear']]))
    for name, a, b in checks:
        if a != b:
            fail += 1
            print(f'FAIL format {f} {name}: n2r {a} != Mesa-derived {b}')
# LOAD / clear preload selection (nord2_metal_bridge.c): raw targets run the
# uint twin preload programs with the twin as target and binding format (a bit
# copy), blendable ones the float programs with their own format.
pre = OUT / 'preload_select.c'
pre.write_text(r'''
#include "nord2_metal_bridge.c"
#include <stdio.h>
int main(void){
 static const unsigned f[]={%s};unsigned bad=0;
 for(unsigned i=0;i<sizeof(f)/sizeof(f[0]);i++){
  unsigned raw=n2r_rt_is_raw(f[i]),twin=n2r_rt_raw_twin(f[i]);
  struct n2t_format_info a,b;
  int ok=raw?(twin&&preload_format(f[i])==twin&&preload_shader(f[i])==&n2_preload_uint_shader&&
             frame_preload_shader(f[i])==&n2_frame_preload_uint_shader&&n2r_bpp(twin)==n2r_bpp(f[i])&&
             !n2t_format_query(twin,&a)&&!n2t_format_query(f[i],&b)&&(a.flags&N2T_FMT_INTEGER)&&!(a.flags&N2T_FMT_SIGNED)&&
             a.bytes_per_block==b.bytes_per_block&&n2r_rt_is_raw(twin)&&n2r_tib_bytes(twin)==n2r_tib_bytes(f[i])):
           (!twin&&preload_format(f[i])==f[i]&&preload_shader(f[i])==&n2_preload_shader&&frame_preload_shader(f[i])==&n2_frame_preload_shader);
  if(!ok){printf("PRELOAD_BAD %%u\n",f[i]);bad++;}
 }
 /* The uint programs are their own code. */
 if(sizeof(n2_preload_uint_fs)==sizeof(n2_preload_fs)&&!memcmp(n2_preload_uint_fs,n2_preload_fs,sizeof(n2_preload_fs)))bad++;
 printf("PRELOAD_SELECT bad=%%u\n",bad);return bad!=0;
}
''' % ','.join(str(f) for f in sorted(expect)))
pexe = OUT / 'preload_select'
subprocess.run(['cc', '-std=c11', '-pthread', '-O1', '-fsanitize=address,undefined', '-I' + str(G / 'include'), '-I' + str(G / 'metal'),
                '-I' + str(G / 'build/tegu-frontend'), str(pre), str(G / 'src/nord2_gpu_desc.c'), str(G / 'src/nord2_gpu_jm.c'),
                str(G / 'metal/nord2_metal_render.c'), str(G / 'metal/tegu/blend_state.c'), '-o', str(pexe)], check=True)
r = subprocess.run([str(pexe)], stdout=subprocess.PIPE, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
print(r.stdout.decode().strip())
if r.returncode:
    fail += 1
print(f'rt formats checked: {len(expect)} failures: {fail}')
if fail:
    sys.exit(1)
print('rt_format_oracle_test: PASS (host descriptor words only; no GPU claim)')
