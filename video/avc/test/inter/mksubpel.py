#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""WP04 test pictures with genuinely sub-pixel motion.

WP09's frozen 'pan' pattern is an exact integer (+3, -2) translation per
picture, so an integer motion search already reaches SAD 0 on it and
half/quarter-sample interpolation can never improve it.  This generator
(WP04-owned, test data only, never a frozen vector) samples the same
deterministic world picture at a fractional displacement:

    Y(x, y) = bilinear(world, x + ax * f, y + ay * f)

with world(x, y) = ((x*7) ^ (y*13) ^ ((x*y) >> 4)) & 255 (WP09's definition)
and bilinear interpolation of the four neighbouring world samples.  With
ax = 3.25 and ay = -2.5 the displacements hit every luma fractional position
(1/4, 2/4, 3/4) in both axes, diagonal included, which is what the 6-tap
interpolation of clause 8.4.2.2.1 has to reproduce.

Usage: mksubpel.py --size WxH --frames N --dx 3.25 --dy -2.5 --out file.yuv
"""

import argparse
import sys


def world(x, y):
    return ((x * 7) ^ (y * 13) ^ ((x * y) >> 4)) & 255


def sample(px, py):
    """Bilinear sample of the world picture at a fractional position."""
    x0 = px // 1
    y0 = py // 1
    x0 = int(x0)
    y0 = int(y0)
    fx = px - x0
    fy = py - y0
    a = world(x0, y0)
    b = world(x0 + 1, y0)
    c = world(x0, y0 + 1)
    d = world(x0 + 1, y0 + 1)
    v = ((1 - fx) * (1 - fy) * a + fx * (1 - fy) * b
         + (1 - fx) * fy * c + fx * fy * d)
    return int(v + 0.5) & 255


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--dx", type=float, default=3.25)
    ap.add_argument("--dy", type=float, default=-2.5)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    w, h = (int(v) for v in a.size.lower().split("x"))
    if w <= 0 or h <= 0 or a.frames <= 0:
        sys.exit("bad size or frame count")
    cw, ch = (w + 1) // 2, (h + 1) // 2
    with open(a.out, "wb") as fp:
        for f in range(a.frames):
            ox = a.dx * f
            oy = a.dy * f
            for y in range(h):
                fp.write(bytes(sample(x + ox, y + oy) for x in range(w)))
            for _ in range(cw * ch):
                fp.write(b"\x80\x80")
    return 0


if __name__ == "__main__":
    sys.exit(main())
