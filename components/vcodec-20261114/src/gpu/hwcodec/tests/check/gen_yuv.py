#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""gen_yuv.py -- deterministic NV12 test sequences (T02).

The luma/chroma formula is the one of the kernel's n2j_pattern (and of
gpu/hwcodec/tools/hwc.py): a horizontal gradient, a 16x16 checkerboard and a
small diagonal dither.  With --pattern static (or frame 0 of moving) it is
byte-identical to hwc.py's pattern for a tight (stride == width) picture.
"moving" shifts the gradient and checkerboard by a few pixels per frame so a
decoder cannot pass by repeating one frame.

Usage:
  gen_yuv.py OUT WxH [--frames N] [--start K] [--align16]
                    [--pattern moving|static|black]

Output is frame-major NV12: h luma rows of `stride` bytes, then h/2 chroma
rows of `stride` bytes (Cb,Cr interleaved).  --align16 rounds the stride up to
16 bytes and pads the row ends by repeating the last pixel (what the JPEG
kernel source layout uses); the row count is h, not padded.
"""

import argparse
import sys
from pathlib import Path


def luma(x, y, w, h, phase):
    xx = (x + 2 * phase) % w
    yy = (y + phase) % h
    return (16 + xx * 180 // w
            + (40 if (((xx >> 4) + (yy >> 4)) & 1) else 0)
            + ((xx * 7 + yy * 13) & 7)) & 0xFF


def chroma(x, y, w, h, phase):
    xx = (2 * x + 2 * phase) % w
    yy = (2 * y + phase) % h
    return (78 + yy * 100 // h) & 0xFF, (78 + xx * 100 // w) & 0xFF


def frame_nv12(w, h, stride, pattern, phase):
    rows = h
    crows = h // 2
    out = bytearray(stride * rows + stride * crows)
    if pattern == "black":
        return bytes(out)
    for y in range(rows):
        row = y * stride
        for x in range(w):
            out[row + x] = luma(x, y, w, h, phase)
        if stride > w:
            last = out[row + w - 1]
            for x in range(w, stride):
                out[row + x] = last
    base = stride * rows
    for y in range(crows):
        row = base + y * stride
        for x in range(w // 2):
            cb, cr = chroma(x, y, w, h, phase)
            out[row + 2 * x] = cb
            out[row + 2 * x + 1] = cr
        if stride > w:
            for x in range(w, stride, 2):
                out[row + x] = out[row + w - 2]
                if x + 1 < stride:
                    out[row + x + 1] = out[row + w - 1]
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="deterministic NV12 test sequence (T02)")
    ap.add_argument("out")
    ap.add_argument("size", help="WxH (both even)")
    ap.add_argument("--frames", type=int, default=1)
    ap.add_argument("--start", type=int, default=0)
    ap.add_argument("--align16", action="store_true", help="round stride up to 16 and pad rows")
    ap.add_argument("--pattern", choices=("moving", "static", "black"), default="moving")
    a = ap.parse_args(argv)
    try:
        w, h = (int(v) for v in a.size.lower().split("x"))
    except ValueError:
        print("size must be WxH", file=sys.stderr)
        return 2
    if w < 2 or h < 2 or w % 2 or h % 2:
        print("width and height must be even and at least 2", file=sys.stderr)
        return 2
    if a.frames < 1 or a.start < 0:
        print("--frames must be >= 1 and --start >= 0", file=sys.stderr)
        return 2
    stride = (w + 15) & ~15 if a.align16 else w
    data = bytearray()
    for n in range(a.frames):
        phase = 0 if a.pattern == "static" else a.start + n
        data += frame_nv12(w, h, stride, a.pattern, phase)
    Path(a.out).write_bytes(data)
    print(f"gen_yuv: {w}x{h} frames={a.frames} stride={stride} bytes={len(data)} pattern={a.pattern} -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
