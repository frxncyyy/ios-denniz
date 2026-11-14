#!/usr/bin/env python3
"""afbc_model.py -- rough AFBC-like lossless compression estimate for RGBA8 frames (UI screenshots).

  python3 afbc_model.py IMG.png [IMG2.png ...]

Model (an approximation of Arm's AFBC 1.x, NOT the real encoder):
  * 16x16 superblocks, 16-byte header each; a superblock of one colour is header-only (AFBC 'solid colour' block).
  * Otherwise 16 subblocks of 4x4; reversible colour transform (YTR: Y=(R+2G+B)>>2, U=B-G, V=R-G, as AFBC_FORMAT_MOD_YTR);
    per channel: 8-bit base + 16 x ceil(log2(max-min+1)) bits; subblock capped at its uncompressed 64 bytes, rounded up to
    whole bytes; alpha channel costs 0 bits when the whole subblock is opaque.
  * Body sizes are summed exactly (no 16-byte payload granularity), so real AFBC is somewhat worse than this model.
Prints bytes per pixel and the ratio vs linear RGBA8 (4 B/px).  Pure Python (PIL only): a few seconds per 1080x2400 frame.
"""
import sys
from PIL import Image


def bits_for(rng):
    return 0 if rng <= 0 else rng.bit_length()


def estimate(path):
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    px = im.tobytes()
    stride = w * 4
    total = 0
    solid_sb = 0
    nsb = 0
    for sy in range(0, h - h % 16, 16):
        for sx in range(0, w - w % 16, 16):
            nsb += 1
            # solid superblock check
            first = px[sy * stride + sx * 4: sy * stride + sx * 4 + 4]
            solid = True
            for y in range(sy, sy + 16):
                row = px[y * stride + sx * 4: y * stride + sx * 4 + 64]
                if row != first * 16:
                    solid = False
                    break
            if solid:
                solid_sb += 1
                total += 16
                continue
            body = 0
            for by in range(sy, sy + 16, 4):
                for bx in range(sx, sx + 16, 4):
                    ys, us, vs, as_ = [], [], [], []
                    for y in range(by, by + 4):
                        o = y * stride + bx * 4
                        for k in range(4):
                            r, g, b, a = px[o + 4 * k], px[o + 4 * k + 1], px[o + 4 * k + 2], px[o + 4 * k + 3]
                            ys.append((r + 2 * g + b) >> 2)
                            us.append(b - g)
                            vs.append(r - g)
                            as_.append(a)
                    bits = 0
                    for ch in (ys, us, vs):
                        bits += 8 + 16 * bits_for(max(ch) - min(ch))
                    if min(as_) != 255:
                        bits += 8 + 16 * bits_for(max(as_) - min(as_))
                    body += min(64, (bits + 7) // 8)
            total += 16 + body
    pixels = nsb * 256
    bpp = total / pixels
    return w, h, bpp, solid_sb / max(nsb, 1)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    print('%-28s %5s %5s %7s %7s %6s' % ('image', 'w', 'h', 'B/px', 'ratio', 'solid'))
    for p in sys.argv[1:]:
        w, h, bpp, solid = estimate(p)
        print('%-28s %5d %5d %7.3f %6.1f%% %5.0f%%' % (p.rsplit('/', 1)[-1][:28], w, h, bpp, 100 * bpp / 4.0, 100 * solid))
    return 0


if __name__ == '__main__':
    sys.exit(main())
