#!/usr/bin/env python3
"""Host test for gpu/metal/tegu/mip_codec.h (generateMipmaps CPU box filter).

For every supported format a random 13x7 level is downsampled to 6x3 (odd
footprints) and 1x1 by the C codec and by an independent Python reference
(decode -> average -> round-to-nearest encode; sRGB in linear space via the
exact transfer function). Results must match exactly except sRGB/half/float
edge rounding, which may differ by one code. A BGR5A1 regression proves the
old half-float decode of 16-bit packed texels is gone."""
import random
import struct
import subprocess
import sys
from pathlib import Path

G = Path(__file__).resolve().parents[1]
OUT = G / 'build/texture-coverage-20261026/host'
OUT.mkdir(parents=True, exist_ok=True)

FORMATS = {1: ('u8', 1), 10: ('u8', 1), 11: ('s8', 1), 12: ('n8', 1), 20: ('u16', 1), 22: ('n16', 1), 25: ('h', 1),
           30: ('u8', 2), 31: ('s8', 2), 32: ('n8', 2), 40: ('p16', (5, 6, 5)), 42: ('p16', (4, 4, 4, 4)),
           43: ('p16', (5, 5, 5, 1)), 55: ('f', 1), 60: ('u16', 2), 62: ('n16', 2), 65: ('h', 2), 70: ('u8', 4),
           71: ('s8', 4), 72: ('n8', 4), 80: ('u8', 4), 81: ('s8', 4), 90: ('p32', (10, 10, 10, 2)),
           94: ('p32', (10, 10, 10, 2)), 105: ('f', 2), 110: ('u16', 4), 112: ('n16', 4), 115: ('h', 4), 125: ('f', 4)}


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(v):
    return v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055


def texel_bytes(fmt):
    kind, n = FORMATS[fmt]
    if kind in ('p16',): return 2
    if kind in ('p32',): return 4
    return {'u8': 1, 's8': 1, 'n8': 1, 'u16': 2, 'n16': 2, 'h': 2, 'f': 4}[kind] * n


def decode(fmt, b):
    kind, n = FORMATS[fmt]
    if kind in ('p16', 'p32'):
        w = int.from_bytes(b, 'little'); out = []; shift = 0
        for bits in n:
            m = (1 << bits) - 1; out.append(((w >> shift) & m) / m); shift += bits
        return out
    if kind == 'u8': return [x / 255 for x in b]
    if kind == 's8': return [srgb_to_linear(x / 255) if i != 3 else x / 255 for i, x in enumerate(b)]
    if kind == 'n8': return [max(struct.unpack('b', bytes([x]))[0] / 127, -1) for x in b]
    if kind == 'u16': return [x / 65535 for x in struct.unpack('<%dH' % n, b)]
    if kind == 'n16': return [max(x / 32767, -1) for x in struct.unpack('<%dh' % n, b)]
    if kind == 'h': return list(struct.unpack('<%de' % n, b))
    return list(struct.unpack('<%df' % n, b))


def encode(fmt, v):
    kind, n = FORMATS[fmt]
    un = lambda x, m: 0 if not x > 0 else m if x >= 1 else int(x * m + 0.5)
    sn = lambda x, m: -m if not x > -1 else m if x >= 1 else int(x * m + (0.5 if x * m >= 0 else -0.5))
    if kind in ('p16', 'p32'):
        w = 0; shift = 0
        for i, bits in enumerate(n):
            w |= un(v[i], (1 << bits) - 1) << shift; shift += bits
        return w.to_bytes(2 if kind == 'p16' else 4, 'little')
    if kind == 'u8': return bytes(un(x, 255) for x in v[:n])
    if kind == 's8': return bytes(un(linear_to_srgb(x), 255) if i != 3 else un(x, 255) for i, x in enumerate(v[:n]))
    if kind == 'n8': return bytes(sn(x, 127) & 255 for x in v[:n])
    if kind == 'u16': return struct.pack('<%dH' % n, *[un(x, 65535) for x in v[:n]])
    if kind == 'n16': return struct.pack('<%dh' % n, *[sn(x, 32767) for x in v[:n]])
    if kind == 'h': return struct.pack('<%de' % n, *v[:n])
    return struct.pack('<%df' % n, *v[:n])


def random_texel(fmt):
    kind, n = FORMATS[fmt]
    if kind == 'h': return struct.pack('<%de' % n, *[random.uniform(-4, 4) for _ in range(n)])
    if kind == 'f': return struct.pack('<%df' % n, *[random.uniform(-4, 4) for _ in range(n)])
    return bytes(random.randrange(256) for _ in range(texel_bytes(fmt)))


def reference(fmt, level, sw, sh, dw, dh):
    tb = texel_bytes(fmt); out = bytearray()
    for y in range(dh):
        for x in range(dw):
            x0, x1 = x * sw // dw, max((x + 1) * sw // dw, x * sw // dw + 1)
            y0, y1 = y * sh // dh, max((y + 1) * sh // dh, y * sh // dh + 1)
            acc = [0.0] * 4; cnt = 0
            for sy in range(y0, y1):
                for sx in range(x0, x1):
                    d = decode(fmt, level[(sy * sw + sx) * tb:(sy * sw + sx + 1) * tb])
                    for i, val in enumerate(d): acc[i] += val
                    cnt += 1
            out += encode(fmt, [a / cnt for a in acc])
    return bytes(out)


C = r'''
#include <stdio.h>
#include <stdlib.h>
#include "xr_decode_table.h"
#include "mip_codec.h"
int main(int argc,char **argv){
 FILE *f=argc>1?fopen(argv[1],"rb"):0;unsigned h[5];if(!f)return 1;
 while(fread(h,4,5,f)==5){
  struct tegu_mip_format m;if(!tegu_mip_codec(h[0],&m))return 2;
  size_t n=(size_t)h[1]*h[2]*m.bytes;unsigned char *src=malloc(n),*dst=malloc((size_t)h[3]*h[4]*m.bytes);
  if(fread(src,1,n,f)!=n)return 3;
  tegu_mip_downsample(&m,src,(size_t)h[1]*m.bytes,h[1],h[2],dst,(size_t)h[3]*m.bytes,h[3],h[4]);
  fwrite(dst,m.bytes,(size_t)h[3]*h[4],stdout);free(src);free(dst);
 }
 return 0;
}
'''


def main():
    random.seed(271026)
    src = OUT / 'mip_codec_main.c'; src.write_text(C)
    exe = OUT / 'mip_codec_main'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(G / 'metal/tegu'), str(src), '-o', str(exe)], check=True)
    cases, blob = [], bytearray()
    for fmt in sorted(FORMATS):
        for sw, sh, dw, dh in ((13, 7, 6, 3), (13, 7, 1, 1), (4, 4, 2, 2)):
            level = b''.join(random_texel(fmt) for _ in range(sw * sh))
            blob += struct.pack('<5I', fmt, sw, sh, dw, dh) + level
            cases.append((fmt, level, sw, sh, dw, dh))
    (OUT / 'mip_cases.bin').write_bytes(bytes(blob))
    got = subprocess.run([str(exe), str(OUT / 'mip_cases.bin')], check=True, stdout=subprocess.PIPE).stdout
    at = 0; worst = {}
    for fmt, level, sw, sh, dw, dh in cases:
        want = reference(fmt, level, sw, sh, dw, dh)
        mine = got[at:at + len(want)]; at += len(want)
        kind, n = FORMATS[fmt]
        if kind in ('h', 'f'):
            a = struct.unpack('<%d%s' % (len(want) // (2 if kind == 'h' else 4), 'e' if kind == 'h' else 'f'), mine)
            b = struct.unpack('<%d%s' % (len(want) // (2 if kind == 'h' else 4), 'e' if kind == 'h' else 'f'), want)
            for x, y in zip(a, b):
                assert abs(x - y) <= 1e-3 * max(1.0, abs(y)), (fmt, x, y)
            continue
        step = 1 if kind in ('p16', 'p32', 'u8', 's8', 'n8') else 1
        unit = {'u16': 2, 'n16': 2}.get(kind, 1)
        for i in range(0, len(want), unit):
            x = int.from_bytes(mine[i:i + unit], 'little', signed=kind.startswith('n'))
            y = int.from_bytes(want[i:i + unit], 'little', signed=kind.startswith('n'))
            if kind in ('p16', 'p32'):
                x, y = mine[i:i + unit], want[i:i + unit]
                continue
            d = abs(x - y); worst[fmt] = max(worst.get(fmt, 0), d)
            assert d <= step, (fmt, i, x, y)
        if kind in ('p16', 'p32'):
            tb = 2 if kind == 'p16' else 4
            for i in range(0, len(want), tb):
                x = int.from_bytes(mine[i:i + tb], 'little'); y = int.from_bytes(want[i:i + tb], 'little'); shift = 0
                for bits in n:
                    m = (1 << bits) - 1; d = abs(((x >> shift) & m) - ((y >> shift) & m)); shift += bits
                    worst[fmt] = max(worst.get(fmt, 0), d)
                    assert d <= 1, (fmt, i, hex(x), hex(y))  # float32 vs exact rounding at .5
    assert at == len(got)
    # BGR5A1 regression: white (0xffff) and black (0x0000) average to mid-grey,
    # not to a half-float average of the bit patterns.
    lvl = struct.pack('<4H', 0xffff, 0x0000, 0xffff, 0x0000)
    blob = struct.pack('<5I', 43, 2, 2, 1, 1) + lvl
    (OUT / 'mip_43.bin').write_bytes(blob)
    out = subprocess.run([str(exe), str(OUT / 'mip_43.bin')], check=True, stdout=subprocess.PIPE).stdout
    w = struct.unpack('<H', out)[0]
    assert (w & 31, (w >> 5) & 31, (w >> 10) & 31, w >> 15) == (16, 16, 16, 1), hex(w)
    print(f'PASS mip_codec: {len(cases)} levels over {len(FORMATS)} formats vs reference; max 8/16-bit code delta {max(worst.values())}; BGR5A1 regression ok')


if __name__ == '__main__':
    sys.exit(main())
