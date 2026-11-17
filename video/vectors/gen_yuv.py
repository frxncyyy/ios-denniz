#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deterministic YUV test pictures for the Nord 2 video-codec work (WP09).

Pure Python 3, no numpy, no Pillow.  The definitions are frozen in
gpu/video/work_packages.json (WP09); this docstring repeats them so that a
reader of the generated files can reproduce them without the JSON.

For every frame f (0-based), luma sample Y(x, y) of a WxH picture:

  gradient: Y = (x + 2*y + 3*f) & 255
            Cb(cx, cy) = 128 + ((cx + f) & 31)
            Cr(cx, cy) = 128 - (cy & 31)
  squares:  the gradient of frame 0 as a still background, plus three 32x32
            squares of luma 235, 16 and 128 at
              (16 + 4*f, 16 + 2*f), (W - 48 - 3*f, 24 + f), (W//2, H - 48 - 2*f)
            clipped to the picture.  Chroma is the gradient of frame 0.
  pan:      a fixed world picture, world(x, y) = ((x*7) ^ (y*13) ^ ((x*y) >> 4)) & 255,
            sampled at (x + 3*f, y - 2*f + 64).  Chroma 128.
  static:   frame 0 of pan repeated.  Chroma 128.
  noise:    state = 1 at the start of the file; for every sample
            state = (state * 1664525 + 1013904223) mod 2**32, value = state >> 24.
            Sample order per frame: the whole Y plane, then Cb, then Cr.
  text:     8x8 cells; cell (cx, cy) is 235 when
            ((cx*31 + cy*17 + f//5) % 7 < 3) and 16 otherwise.  Chroma 128.

Chroma samples are at half resolution, (W+1)//2 x (H+1)//2 for odd sizes (the
codecs themselves take even sizes).

The `--ppm` option writes one frame as a binary PPM (P6) using BT.601
full-range integer arithmetic, defined exactly here and used by
make_vectors.sh to make the golden JPEG/HEIC files:

    kr = 1436, kg = 352, kb = 731, kb2 = 1815     (scaled by 1024)
    cr = Cr - 128, cb = Cb - 128
    R = clamp(Y + ((1436*cr + 512) >> 10))
    G = clamp(Y - (( 352*cb +  731*cr + 512) >> 10))
    B = clamp(Y + ((1815*cb + 512) >> 10))

Everything is integer; the shifts are arithmetic (Python's >> on negative
integers is a floor shift, as this definition intends).
"""

import argparse
import os
import sys

MASK32 = 0xFFFFFFFF
LCG_A = 1664525
LCG_C = 1013904223


def parse_size(text):
    parts = text.lower().split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("size must be WxH, e.g. 176x144")
    try:
        w, h = int(parts[0]), int(parts[1])
    except ValueError:
        raise argparse.ArgumentTypeError("size must be WxH with integers")
    if w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return w, h


def world(x, y):
    """The fixed pan world picture (luma only; see the module docstring)."""
    return ((x * 7) ^ (y * 13) ^ ((x * y) >> 4)) & 255


def _write_gradient(fp, w, h, frames, fmt):
    cw, ch = (w + 1) // 2, (h + 1) // 2
    make_row = bytes
    for f in range(frames):
        for y in range(h):
            base = 2 * y + 3 * f
            fp.write(make_row(((x + base) & 255 for x in range(w))))
        if fmt == "i420":
            for cy in range(ch):
                fp.write(make_row((128 + ((cx + f) & 31) for cx in range(cw))))
            for cy in range(ch):
                fp.write(make_row((128 - (cy & 31) for _ in range(cw))))
        else:  # nv12: interleaved Cb, Cr pairs
            for cy in range(ch):
                cr = 128 - (cy & 31)
                row = bytearray(2 * cw)
                for cx in range(cw):
                    row[2 * cx] = 128 + ((cx + f) & 31)
                    row[2 * cx + 1] = cr
                fp.write(row)


def _gradient_row0(y, w):
    return bytearray(((x + 2 * y) & 255 for x in range(w)))


def _write_squares(fp, w, h, frames, fmt):
    cw, ch = (w + 1) // 2, (h + 1) // 2
    for f in range(frames):
        squares = ((16 + 4 * f, 16 + 2 * f, 235),
                   (w - 48 - 3 * f, 24 + f, 16),
                   (w // 2, h - 48 - 2 * f, 128))
        for y in range(h):
            row = _gradient_row0(y, w)
            for sx, sy, value in squares:
                if sy <= y < sy + 32:
                    x0 = max(0, sx)
                    x1 = min(w, sx + 32)
                    if x1 > x0:
                        row[x0:x1] = bytes((value,)) * (x1 - x0)
            fp.write(row)
        if fmt == "i420":
            for cy in range(ch):
                fp.write(bytes((128 + (cx & 31) for cx in range(cw))))
            for cy in range(ch):
                fp.write(bytes((128 - (cy & 31),) * cw))
        else:
            for cy in range(ch):
                cr = 128 - (cy & 31)
                row = bytearray(2 * cw)
                for cx in range(cw):
                    row[2 * cx] = 128 + (cx & 31)
                    row[2 * cx + 1] = cr
                fp.write(row)


def _write_pan(fp, w, h, frames, fmt, static=False):
    cw, ch = (w + 1) // 2, (h + 1) // 2
    neutral = bytes((128,)) * cw
    for f in range(frames):
        ff = 0 if static else f
        for y in range(h):
            sy = y - 2 * ff + 64
            sx = 3 * ff
            fp.write(bytes((world(x + sx, sy) for x in range(w))))
        if fmt == "i420":
            for _ in range(ch):
                fp.write(neutral)
            for _ in range(ch):
                fp.write(neutral)
        else:
            row = bytes((128,)) * (2 * cw)
            for _ in range(ch):
                fp.write(row)


def _write_noise(fp, w, h, frames, fmt):
    cw, ch = (w + 1) // 2, (h + 1) // 2
    state = 1
    mul, add, mask = LCG_A, LCG_C, MASK32
    for _ in range(frames):
        for _y in range(h):
            row = bytearray(w)
            for x in range(w):
                state = (state * mul + add) & mask
                row[x] = state >> 24
            fp.write(row)
        cb = bytearray(cw * ch)
        for i in range(cw * ch):
            state = (state * mul + add) & mask
            cb[i] = state >> 24
        cr = bytearray(cw * ch)
        for i in range(cw * ch):
            state = (state * mul + add) & mask
            cr[i] = state >> 24
        if fmt == "i420":
            fp.write(cb)
            fp.write(cr)
        else:
            for cy in range(ch):
                row = bytearray(2 * cw)
                off = cy * cw
                for cx in range(cw):
                    row[2 * cx] = cb[off + cx]
                    row[2 * cx + 1] = cr[off + cx]
                fp.write(row)


def _write_text(fp, w, h, frames, fmt):
    cw, ch = (w + 1) // 2, (h + 1) // 2
    cells_x = (w + 7) // 8
    neutral_row = bytes((128,)) * cw
    for f in range(frames):
        phase = f // 5
        values = [235 if ((cx * 31 + cy * 17 + phase) % 7 < 3) else 16
                  for cy in range((h + 7) // 8) for cx in range(cells_x)]
        for y in range(h):
            crow = values[(y // 8) * cells_x:(y // 8 + 1) * cells_x]
            row = bytearray()
            for cx, value in enumerate(crow):
                n = min(8, w - 8 * cx)
                row.extend(bytes((value,)) * n)
            fp.write(row)
        if fmt == "i420":
            for _ in range(ch):
                fp.write(neutral_row)
            for _ in range(ch):
                fp.write(neutral_row)
        else:
            row = bytes((128,)) * (2 * cw)
            for _ in range(ch):
                fp.write(row)


GENERATORS = {
    "gradient": _write_gradient,
    "squares": _write_squares,
    "pan": lambda fp, w, h, n, fmt: _write_pan(fp, w, h, n, fmt, False),
    "static": lambda fp, w, h, n, fmt: _write_pan(fp, w, h, n, fmt, True),
    "noise": _write_noise,
    "text": _write_text,
}


def generate(pattern, w, h, frames, fmt, out_path):
    if pattern not in GENERATORS:
        raise ValueError("unknown pattern %r" % (pattern,))
    if frames <= 0:
        raise ValueError("frames must be positive")
    tmp = out_path + ".tmp.%d" % os.getpid()
    with open(tmp, "wb") as fp:
        GENERATORS[pattern](fp, w, h, frames, fmt)
    os.replace(tmp, out_path)


def yuv_frame_size(w, h, fmt):
    if fmt == "i420":
        return w * h + 2 * ((w + 1) // 2) * ((h + 1) // 2)
    return w * h + 2 * ((w + 1) // 2) * ((h + 1) // 2)


def _clamp8(v):
    return 0 if v < 0 else (255 if v > 255 else v)


def yuv_to_ppm(yuv_path, w, h, frame, fmt, ppm_path):
    """Convert one frame to binary PPM with the integer BT.601 maths above."""
    cw, ch = (w + 1) // 2, (h + 1) // 2
    ysize = w * h
    csize = cw * ch
    with open(yuv_path, "rb") as fp:
        fp.seek(frame * yuv_frame_size(w, h, fmt))
        yplane = fp.read(ysize)
        if len(yplane) != ysize:
            raise ValueError("short YUV file")
        if fmt == "i420":
            cb = fp.read(csize)
            cr = fp.read(csize)
            if len(cb) != csize or len(cr) != csize:
                raise ValueError("short YUV file")
            cbp = lambda cx, cy: cb[cy * cw + cx]
            crp = lambda cx, cy: cr[cy * cw + cx]
        else:
            uv = fp.read(2 * csize)
            if len(uv) != 2 * csize:
                raise ValueError("short YUV file")
            cbp = lambda cx, cy: uv[2 * (cy * cw + cx)]
            crp = lambda cx, cy: uv[2 * (cy * cw + cx) + 1]
    out = bytearray()
    out += b"P6\n%d %d\n255\n" % (w, h)
    for y in range(h):
        cy = y >> 1
        yrow = y * w
        for x in range(w):
            cx = x >> 1
            yy = yplane[yrow + x]
            cbv = cbp(cx, cy) - 128
            crv = crp(cx, cy) - 128
            r = _clamp8(yy + ((1436 * crv + 512) >> 10))
            g = _clamp8(yy - ((352 * cbv + 731 * crv + 512) >> 10))
            b = _clamp8(yy + ((1815 * cbv + 512) >> 10))
            out += bytes((r, g, b))
    with open(ppm_path, "wb") as fp:
        fp.write(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pattern", required=True, choices=sorted(GENERATORS))
    ap.add_argument("--size", required=True, type=parse_size)
    ap.add_argument("--frames", type=int, default=1)
    ap.add_argument("--format", choices=("i420", "nv12"), default="i420")
    ap.add_argument("--out", required=True)
    ap.add_argument("--ppm", help="also write frame --ppm-frame of --out as binary PPM")
    ap.add_argument("--ppm-frame", type=int, default=0)
    args = ap.parse_args(argv)
    w, h = args.size
    try:
        generate(args.pattern, w, h, args.frames, args.format, args.out)
        if args.ppm:
            yuv_to_ppm(args.out, w, h, args.ppm_frame, args.format, args.ppm)
    except (ValueError, OSError) as exc:
        print("gen_yuv.py: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
