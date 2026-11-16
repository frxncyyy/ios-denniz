#!/usr/bin/env python3
"""Host test for gpu/metal/tegu/ycbcr_decode.h (frontend YCbCr decode shadows).

A Python reference written from the measured Metal model (metal2vulkan
bind/ycbcr_convert.rs module docs, re-stated in ycbcr_decode.h) decodes random
two-plane surfaces for every implemented format (500-545 converted, sRGB and
code formats; 8-bit, 10-bit and packed codes; 4:2:0/4:2:2/4:4:4), both ranges
and three matrices, on odd sizes, and must match the C decoder bit for bit.
Floating-point sanity anchors (video-range BT.601 black/white/grey and full
range primaries within a 1/2048 step of the textbook matrices) guard against a
reference and implementation sharing one error. Host only.
"""
import random
import struct
import subprocess
import sys
from pathlib import Path

G = Path(__file__).resolve().parents[1]
OUT = G / 'build/texture-coverage-20261026/host'
OUT.mkdir(parents=True, exist_ok=True)

COEF = {
    (0, 0): [(-9, 13123), (-3218, -6686), (16591, 9)],
    (0, 1): [(-1, 14743), (-1753, -4383), (17373, 1)],
    (0, 2): [(0, 13806), (-1540, -5349), (17614, -1)],
    (1, 0): [(-8, 11483), (-2816, -5850), (14518, 8)],
    (1, 1): [(-1, 12900), (-1534, -3835), (15201, 1)],
    (1, 2): [(0, 12080), (-1348, -4680), (15412, -1)],
}
FORMATS = {500: (8, 0, 0, 0), 502: (8, 0, 1, 0), 503: (8, 0, 2, 0), 505: (10, 0, 0, 0), 506: (10, 0, 1, 0),
           507: (10, 0, 2, 0), 508: (10, 1, 0, 0), 509: (10, 1, 1, 0), 510: (10, 1, 2, 0),
           520: (8, 0, 0, 1), 522: (8, 0, 1, 1), 523: (8, 0, 2, 1), 525: (10, 0, 0, 1), 526: (10, 0, 1, 1),
           527: (10, 0, 2, 1), 528: (10, 1, 0, 1), 529: (10, 1, 1, 1), 530: (10, 1, 2, 1),
           540: (8, 0, 0, 2), 541: (8, 0, 1, 2), 542: (8, 0, 2, 2), 543: (10, 0, 0, 2), 544: (10, 0, 1, 2),
           545: (10, 0, 2, 2)}


def srgb(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


KNOTS = [round(8192 * srgb(16 * j / 8192)) for j in range(513)]


def half_bits(value, ties_away):
    if ties_away:  # exact value on a fine grid: round half away from zero by hand
        import math
        if value == 0:
            return 0
        e = math.floor(math.log2(value))
        scaled = value / 2 ** (e - 10)
        m = math.floor(scaled + 0.5)
        if m >= 2048:
            m //= 2
            e += 1
        return ((e + 15) << 10) | (m & 0x3ff)
    return struct.unpack('<H', struct.pack('<e', value))[0]


def codes_row(codes, bits, packed):
    if packed:
        out = bytearray()
        for i in range(0, len(codes), 3):
            w = 0
            for j, c in enumerate(codes[i:i + 3]):
                w |= c << (10 * j)
            out += struct.pack('<I', w)
        return bytes(out)
    if bits == 8:
        return bytes(codes)
    return b''.join(struct.pack('<H', (c << 6) | random.randrange(64)) for c in codes)


def reference(fmt, full, matrix, w, h, Y, CB, CR):
    bits, packed, sub, output = FORMATS[fmt]
    cw = w if sub == 2 else (w + 1) // 2
    ch = (h + 1) // 2 if sub == 0 else h
    u, m = 10 - bits, (1 << bits) - 1
    texels = []
    for y in range(h):
        for x in range(w):
            def taps(v, size, halve):
                if not halve:
                    return [(min(v, size - 1), 4)]
                near = v // 2
                far = near + 1 if v & 1 else near - 1
                return [(min(near, size - 1), 3), (min(max(far, 0), size - 1), 1)]
            tx, ty = taps(x, cw, sub != 2), taps(y, ch, sub == 0)
            c = []
            for plane in (CB, CR):
                s = sum(wx * wy * plane[cy][cx] for cx, wx in tx for cy, wy in ty)
                c.append((s + 8) >> 4)
            yy = Y[y][x]
            if output == 2:
                texels.append((c[1], yy, c[0]))
                continue
            L = 256 * ((yy * 32768 + m // 2) // m) if full else 9576 * ((yy << u) - 64)
            cb, cr = (c[0] << u) - 512, (c[1] << u) - 512
            px = []
            for kb, kr in COEF[(full, matrix)]:
                T = L + kb * cb + kr * cr
                if output == 0:
                    v = min(max((T + 2048) >> 12, 0), 2048)
                    px.append(half_bits(v / 2048, False))
                else:
                    q = min(max((T + 512) >> 10, 0), 8192)
                    i = min(q >> 4, 511)
                    f = q - 16 * i
                    num = 16 * KNOTS[i] + (KNOTS[i + 1] - KNOTS[i]) * f
                    step, rem = num >> 4, num & 15
                    if rem > 8 or (rem == 8 and step & 1):
                        step += 1
                    px.append(half_bits(step / 8192, True))
            texels.append(tuple(px))
    return texels, cw, ch


C_MAIN = r'''
#include <stdio.h>
#include <stdlib.h>
#include "xr_decode_table.h"
#include "ycbcr_decode.h"
int main(int argc,char **argv){
 FILE *f=argc<2?0:fopen(argv[1],"rb");unsigned hdr[10];if(!f)return 1;
 while(fread(hdr,4,10,f)==10){
  unsigned fmt=hdr[0],full=hdr[1],matrix=hdr[2],w=hdr[3],h=hdr[4],lrow=hdr[5],crow=hdr[6],lbytes=hdr[7],cbytes=hdr[8];
  unsigned char *luma=malloc(lbytes),*chroma=malloc(cbytes);
  if(fread(luma,1,lbytes,f)!=lbytes||fread(chroma,1,cbytes,f)!=cbytes)return 2;
  struct tegu_ycbcr_layout l;if(!tegu_ycbcr_format(fmt,&l))return 3;l.full_range=full;l.matrix=matrix;
  unsigned cw=l.sub==TEGU_YCBCR_SUB_444?w:(w+1)/2,ch=l.sub==TEGU_YCBCR_SUB_420?(h+1)/2:h;
  size_t texel=l.output==TEGU_YCBCR_OUT_CODES?4:8;unsigned char *row=malloc(w*texel);
  for(unsigned y=0;y<h;y++){tegu_ycbcr_decode_row(&l,luma,lrow,chroma,crow,w,cw,ch,y,row,tegu_srgb_knots_8192);fwrite(row,texel,w,stdout);}
  free(row);free(luma);free(chroma);
 }
 return 0;
}
'''


def main():
    random.seed(20261026)
    src = OUT / 'ycbcr_decode_main.c'
    src.write_text(C_MAIN)
    exe = OUT / 'ycbcr_decode_main'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(G / 'metal/tegu'), str(src), '-o', str(exe)], check=True)
    cases, blob = [], bytearray()
    for fmt in sorted(FORMATS):
        bits, packed, sub, output = FORMATS[fmt]
        for full in (0, 1):
            for matrix in (0, 1, 2):
                w, h = random.choice([(7, 5), (9, 4), (1, 1), (13, 3)])
                cw = w if sub == 2 else (w + 1) // 2
                ch = (h + 1) // 2 if sub == 0 else h
                m = (1 << bits) - 1
                pick = lambda: random.choice([0, m, m // 2, 16 << (bits - 8), 235 << (bits - 8), random.randrange(m + 1)])
                Y = [[pick() for _ in range(w)] for _ in range(h)]
                CB = [[pick() for _ in range(cw)] for _ in range(ch)]
                CR = [[pick() for _ in range(cw)] for _ in range(ch)]
                lrows = [codes_row(r, bits, packed) for r in Y]
                crows = [codes_row([v for pair in zip(cb, cr) for v in pair], bits, packed) for cb, cr in zip(CB, CR)]
                lrow = len(lrows[0]) + random.choice([0, 3, 16])
                crow = len(crows[0]) + random.choice([0, 5, 16])
                luma = b''.join(r.ljust(lrow, b'\x5a') for r in lrows)
                chroma = b''.join(r.ljust(crow, b'\xa5') for r in crows)
                blob += struct.pack('<10I', fmt, full, matrix, w, h, lrow, crow, len(luma), len(chroma), 0) + luma + chroma
                cases.append((fmt, full, matrix, w, h, Y, CB, CR, output))
    data_path = OUT / 'ycbcr_cases.bin'
    data_path.write_bytes(bytes(blob))
    got = subprocess.run([str(exe), str(data_path)], check=True, stdout=subprocess.PIPE).stdout
    at, texels_checked = 0, 0
    for fmt, full, matrix, w, h, Y, CB, CR, output in cases:
        want, _, _ = reference(fmt, full, matrix, w, h, Y, CB, CR)
        for i, t in enumerate(want):
            if output == 2:
                v = struct.unpack_from('<I', got, at)[0]; at += 4
                if FORMATS[fmt][0] == 8:
                    exp = t[0] | (t[1] << 8) | (t[2] << 16) | 0xff000000
                else:
                    exp = t[0] | (t[1] << 10) | (t[2] << 20) | (3 << 30)
                assert v == exp, (fmt, full, matrix, i, hex(v), hex(exp))
            else:
                v = struct.unpack_from('<4H', got, at); at += 8
                assert v == t + (0x3c00,), (fmt, full, matrix, i, v, t)
            texels_checked += 1
    assert at == len(got)
    # Floating anchors: video-range BT.601 black, white, mid grey; full-range red.
    def converted(fmt, full, matrix, y, cb, cr):
        want, _, _ = reference(fmt, full, matrix, 1, 1, [[y]], [[cb]], [[cr]])
        return [struct.unpack('<e', struct.pack('<H', c))[0] for c in want[0]]
    assert converted(500, 0, 0, 16, 128, 128) == [0, 0, 0]
    assert converted(500, 0, 0, 235, 128, 128) == [1, 1, 1]
    for got_c, exp in zip(converted(500, 1, 0, 76, 85, 255), (1.0, 0.0, 0.0)):  # BT.601 full-range red
        assert abs(got_c - exp) <= 2 / 255, converted(500, 1, 0, 76, 85, 255)
    for got_c, exp in zip(converted(500, 1, 1, 54, 99, 255), (1.0, 0.0, 0.0)):  # BT.709 full-range red
        assert abs(got_c - exp) <= 2 / 255, converted(500, 1, 1, 54, 99, 255)
    print(f'PASS ycbcr_decode: {len(cases)} surfaces, {texels_checked} texels bit-exact vs reference; anchors ok')


if __name__ == '__main__':
    sys.exit(main())
