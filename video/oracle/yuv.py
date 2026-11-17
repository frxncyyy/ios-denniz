#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pure-Python helpers for the WP10 video oracle.  No numpy, no Pillow.

An I420 ("yuv420p") frame is laid out as
    Y  : width  x height          bytes, row-major, stride = width
    Cb : ceil(w/2) x ceil(h/2)     bytes
    Cr : ceil(w/2) x ceil(h/2)     bytes
An NV12 ("420v"/"420f") frame is
    Y  : width  x height
    UV : ceil(w/2) * ceil(h/2) * 2 bytes, interleaved Cb,Cr per sample.

Everything here works on whole planes (bytes objects / memoryviews); the
functions that walk a file stream one frame at a time so that a 1920x1080
10-frame sequence never has to sit in RAM twice.
"""

from __future__ import annotations

import hashlib
import math
import sys

__all__ = [
    "chroma_dim", "frame_size", "plane_sizes", "iter_i420", "read_i420",
    "iter_nv12", "read_nv12", "nv12_to_i420", "i420_to_nv12",
    "psnr", "psnr_frames", "first_diff", "first_diff_frames",
    "frame_md5s", "md5_hex", "luma_md5s", "count_frames",
]


def chroma_dim(n: int) -> int:
    """Chroma samples along one axis for 4:2:0 (rounded up, as H.264 crops)."""
    return (n + 1) // 2


def frame_size(w: int, h: int) -> int:
    """Bytes of one tight I420/NV12 frame (both formats have the same size)."""
    return w * h + 2 * chroma_dim(w) * chroma_dim(h)


def plane_sizes(w: int, h: int):
    """[(name, width, height), ...] for Y, Cb, Cr of a 4:2:0 frame."""
    cw, ch = chroma_dim(w), chroma_dim(h)
    return [("Y", w, h), ("Cb", cw, ch), ("Cr", cw, ch)]


def _read_exact(f, n: int, what: str) -> bytes:
    b = f.read(n)
    if len(b) != n:
        raise ValueError("%s: truncated: wanted %d bytes, got %d" % (what, n, len(b)))
    return b


def iter_i420(path: str, w: int, h: int, count: int | None = None):
    """Yield (Y, Cb, Cr) bytes triples, each a tight plane, frame by frame."""
    cw, ch = chroma_dim(w), chroma_dim(h)
    ys, cs = w * h, cw * ch
    with open(path, "rb") as f:
        i = 0
        while count is None or i < count:
            y = f.read(ys)
            if not y:
                break
            if len(y) != ys:
                raise ValueError("%s: frame %d: luma truncated (%d/%d)" % (path, i, len(y), ys))
            cb = _read_exact(f, cs, "%s frame %d Cb" % (path, i))
            cr = _read_exact(f, cs, "%s frame %d Cr" % (path, i))
            yield y, cb, cr
            i += 1


def read_i420(path: str, w: int, h: int, count: int | None = None):
    return list(iter_i420(path, w, h, count))


def iter_nv12(path: str, w: int, h: int, count: int | None = None):
    """Yield (Y, UV) pairs from an NV12 file (UV is interleaved Cb,Cr)."""
    ys = w * h
    uvs = 2 * chroma_dim(w) * chroma_dim(h)
    with open(path, "rb") as f:
        i = 0
        while count is None or i < count:
            y = f.read(ys)
            if not y:
                break
            if len(y) != ys:
                raise ValueError("%s: frame %d: luma truncated (%d/%d)" % (path, i, len(y), ys))
            uv = _read_exact(f, uvs, "%s frame %d UV" % (path, i))
            yield y, uv
            i += 1


def read_nv12(path: str, w: int, h: int, count: int | None = None):
    return list(iter_nv12(path, w, h, count))


def nv12_to_i420(nv12: bytes, w: int, h: int) -> bytes:
    """Convert one NV12 frame (Y + interleaved UV) to tight I420 (Y Cb Cr)."""
    ys = w * h
    if len(nv12) != frame_size(w, h):
        raise ValueError("nv12_to_i420: expected %d bytes, got %d" % (frame_size(w, h), len(nv12)))
    uv = nv12[ys:]
    cb = bytearray(len(uv) // 2)
    cr = bytearray(len(uv) // 2)
    cb[:] = uv[0::2]
    cr[:] = uv[1::2]
    return nv12[:ys] + bytes(cb) + bytes(cr)


def i420_to_nv12(i420: bytes, w: int, h: int) -> bytes:
    """Convert one tight I420 frame to NV12 (Y + interleaved UV)."""
    ys = w * h
    cs = chroma_dim(w) * chroma_dim(h)
    if len(i420) != frame_size(w, h):
        raise ValueError("i420_to_nv12: expected %d bytes, got %d" % (frame_size(w, h), len(i420)))
    cb = i420[ys:ys + cs]
    cr = i420[ys + cs:ys + 2 * cs]
    uv = bytearray(2 * cs)
    uv[0::2] = cb
    uv[1::2] = cr
    return i420[:ys] + bytes(uv)


def _sse(a, b) -> int:
    """Sum of squared differences as an integer (inputs are equal length)."""
    s = 0
    for x, y in zip(a, b):
        d = x - y
        s += d * d
    return s


def psnr_integer(a, b, w: int, h: int) -> float:
    """Reference PSNR from an explicit integer SSE (slow; used by the tests)."""
    if len(a) != w * h or len(b) != w * h:
        raise ValueError("psnr: plane size mismatch (%d, %d vs %d)" % (len(a), len(b), w * h))
    sse = _sse(a, b)
    if sse == 0:
        return float("inf")
    return 10.0 * math.log10((255.0 * 255.0) / (sse / float(w * h)))


def psnr(a, b, w: int, h: int) -> float:
    """PSNR in dB between two planes of w*h samples; inf when identical.

    math.dist computes the Euclidean distance in C.  For 8-bit samples every
    partial sum of squared differences is an integer below 2^53, so the double
    accumulation is exact; squaring the distance gives the integer SSE and the
    only floating-point operation is the final log.  psnr_integer() is the
    explicit-loop reference and the test compares the two.
    """
    if len(a) != w * h or len(b) != w * h:
        raise ValueError("psnr: plane size mismatch (%d, %d vs %d)" % (len(a), len(b), w * h))
    d = math.dist(a, b)
    if d == 0.0:
        return float("inf")
    mse = (d * d) / float(w * h)
    return 10.0 * math.log10((255.0 * 255.0) / mse)


def psnr_frames(ref_path: str, test_path: str, w: int, h: int, count: int | None = None):
    """Yield (y_psnr, cb_psnr, cr_psnr) for each frame of two I420 files."""
    g1 = iter_i420(ref_path, w, h, count)
    g2 = iter_i420(test_path, w, h, count)
    for i, (f1, f2) in enumerate(zip(g1, g2)):
        cw, ch = chroma_dim(w), chroma_dim(h)
        yield (psnr(f1[0], f2[0], w, h),
               psnr(f1[1], f2[1], cw, ch),
               psnr(f1[2], f2[2], cw, ch))


def _first_diff_plane(a, b, w: int, h: int):
    """(x, y) of the first differing sample of two equal-size planes, or None."""
    row = w
    for y in range(h):
        o = y * row
        ra = a[o:o + row]
        rb = b[o:o + row]
        if ra != rb:
            for x in range(row):
                if ra[x] != rb[x]:
                    return x, y
    return None


def first_diff(ref_path: str, test_path: str, w: int, h: int, count: int | None = None):
    """First differing sample of two I420 files.

    Returns (frame, plane, x, y) with plane 0=Y, 1=Cb, 2=Cr and x,y in that
    plane's own coordinates, or None when the files are identical for
    `count` frames.
    """
    dims = plane_sizes(w, h)
    g1 = iter_i420(ref_path, w, h, count)
    g2 = iter_i420(test_path, w, h, count)
    for i, (f1, f2) in enumerate(zip(g1, g2)):
        for p, (_name, pw, ph) in enumerate(dims):
            d = _first_diff_plane(f1[p], f2[p], pw, ph)
            if d is not None:
                return i, p, d[0], d[1]
    return None


def first_diff_frames(frame_a, frame_b, w: int, h: int):
    """Same as first_diff but on two (Y,Cb,Cr) triples."""
    for p, (_name, pw, ph) in enumerate(plane_sizes(w, h)):
        d = _first_diff_plane(frame_a[p], frame_b[p], pw, ph)
        if d is not None:
            return p, d[0], d[1]
    return None


def md5_hex(data) -> str:
    return hashlib.md5(data).hexdigest()


def frame_md5s(path: str, w: int, h: int, count: int | None = None):
    """MD5 of every tight I420 frame (Y+Cb+Cr), in file order."""
    out = []
    for y, cb, cr in iter_i420(path, w, h, count):
        out.append(hashlib.md5(y + cb + cr).hexdigest())
    return out


def luma_md5s(path: str, w: int, h: int, count: int | None = None):
    """MD5 of every luma plane (width bytes per row, no padding)."""
    return [hashlib.md5(y).hexdigest() for y, _cb, _cr in iter_i420(path, w, h, count)]


def count_frames(path: str, w: int, h: int):
    """Number of whole I420 frames in a file; raises on a partial frame."""
    fs = frame_size(w, h)
    import os
    n = os.path.getsize(path)
    if n % fs:
        raise ValueError("%s: %d bytes is not a whole number of %dx%d I420 frames (%d)"
                         % (path, n, w, h, fs))
    return n // fs


def _main(argv):
    import argparse
    p = argparse.ArgumentParser(description="I420/NV12 helpers (WP10 oracle)")
    sub = p.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("md5", help="md5 per I420 frame")
    m.add_argument("file")
    m.add_argument("size", help="WxH")
    m.add_argument("--frames", type=int, default=None)
    m = sub.add_parser("psnr", help="luma PSNR per frame of two I420 files")
    m.add_argument("ref")
    m.add_argument("test")
    m.add_argument("size", help="WxH")
    m = sub.add_parser("firstdiff", help="first differing sample of two I420 files")
    m.add_argument("ref")
    m.add_argument("test")
    m.add_argument("size", help="WxH")
    a = p.parse_args(argv)
    w, h = (int(v) for v in a.size.lower().split("x"))
    if a.cmd == "md5":
        for i, d in enumerate(frame_md5s(a.file, w, h, a.frames)):
            print("%d %s" % (i, d))
    elif a.cmd == "psnr":
        for i, (py, pcb, pcr) in enumerate(psnr_frames(a.ref, a.test, w, h)):
            print("%d Y %.4f Cb %.4f Cr %.4f" % (i, py, pcb, pcr))
    else:
        d = first_diff(a.ref, a.test, w, h)
        print("identical" if d is None else "first_diff frame=%d plane=%d x=%d y=%d" % d)
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
