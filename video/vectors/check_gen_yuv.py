#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Literal checks of gen_yuv.py's command line and picture definitions.

Expected samples are hand-computed from the definitions in the work package,
not read back from the generator's own code.  Run by check_vectors.sh; prints
CHECKS <n> as its last line on success.
"""

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "gen_yuv.py")
CHECKS = 0


def ok():
    global CHECKS
    CHECKS += 1


def need(cond, what):
    if not cond:
        raise SystemExit("check_gen_yuv: FAIL: %s" % what)
    ok()


def run(out, pattern, w, h, frames=1, fmt="i420", extra=()):
    argv = [sys.executable, GEN, "--pattern", pattern, "--size", "%dx%d" % (w, h),
            "--frames", str(frames), "--format", fmt, "--out", out]
    argv.extend(extra)
    proc = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
        raise SystemExit("check_gen_yuv: FAIL: %s" % " ".join(argv))
    return open(out, "rb").read()


def frame_size(w, h):
    return w * h + 2 * ((w + 1) // 2) * ((h + 1) // 2)


def world(x, y):
    return ((x * 7) ^ (y * 13) ^ ((x * y) >> 4)) & 255


def main(tmp):
    os.makedirs(tmp, exist_ok=True)

    # gradient: Y = (x + 2y + 3f) & 255; the first four luma bytes are 0..3,
    # Y(5,7) = 5 + 14 = 19.  Cb(0,0) = 128 + 0, Cr(0,3) = 128 - 3 = 125.
    d = run(os.path.join(tmp, "gradient.yuv"), "gradient", 176, 144)
    need(list(d[0:4]) == [0, 1, 2, 3], "gradient first four luma bytes")
    need(d[7 * 176 + 5] == 19, "gradient Y(5,7) == 19")
    ysize = 176 * 144
    csize = 88 * 72
    need(d[ysize] == 128, "gradient Cb(0,0) == 128")
    need(d[ysize + csize + 3 * 88] == 125, "gradient Cr(0,3) == 125")
    need(len(d) == frame_size(176, 144), "gradient frame size")

    # noise: LCG from seed 1; the three literal first states give 60, 94, 129.
    d = run(os.path.join(tmp, "noise.yuv"), "noise", 176, 144)
    need(list(d[0:3]) == [60, 94, 129], "noise first three samples")
    need(len(d) == frame_size(176, 144), "noise frame size")

    # pan frame 0 samples world(x, y + 64): world(0,64) = (0 ^ 832 ^ 0) & 255
    # = 64; world(1,64) = (7 ^ 832 ^ (64 >> 4)) & 255 = (7 ^ 832 ^ 4) & 255 = 67.
    d = run(os.path.join(tmp, "pan.yuv"), "pan", 176, 144)
    need(d[0] == 64, "pan Y(0,0) == world(0,64) == 64")
    need(d[1] == 67, "pan Y(1,0) == world(1,64) == 67")
    need(d[ysize] == 128, "pan chroma is neutral")

    # static is pan frame 0 repeated.
    d = run(os.path.join(tmp, "static.yuv"), "static", 176, 144, frames=3)
    f0 = d[:frame_size(176, 144)]
    need(f0 == d[frame_size(176, 144):2 * frame_size(176, 144)] and
         f0 == d[2 * frame_size(176, 144):], "static repeats frame 0")

    # squares: frame 1, squares at (20,18)=235, (125,25)=16, (88,94)=128;
    # the background is gradient frame 0, so Y(0,0) = 0.
    d = run(os.path.join(tmp, "squares.yuv"), "squares", 176, 144, frames=2)
    f1 = d[frame_size(176, 144):]
    need(f1[18 * 176 + 20] == 235, "squares first square == 235 at frame 1")
    need(f1[25 * 176 + 125] == 16, "squares second square == 16 at frame 1")
    need(f1[94 * 176 + 88] == 128, "squares third square == 128 at frame 1")
    need(f1[0] == 0, "squares background is gradient frame 0")
    need(d[ysize] == 128, "squares chroma is gradient frame 0")

    # text: cell (0,0) is 235 ((0*31+0*17+0) % 7 == 0 < 3), cell (1,0) is 16
    # (31 % 7 == 3), cell (0,1) is 16 (17 % 7 == 3).
    d = run(os.path.join(tmp, "text.yuv"), "text", 176, 144)
    need(d[0] == 235, "text cell (0,0) == 235")
    need(d[8] == 16, "text cell (1,0) == 16")
    need(d[8 * 176] == 16, "text cell (0,1) == 16")
    need(d[ysize] == 128, "text chroma is neutral")

    # Every pattern accepts both formats and the odd size 18x18; the file size
    # is frames * (w*h + 2 * ceil(w/2) * ceil(h/2)) for both.
    for pattern in ("gradient", "squares", "pan", "static", "noise", "text"):
        want = 2 * (18 * 18 + 2 * 9 * 9)
        for fmt in ("i420", "nv12"):
            p = os.path.join(tmp, "%s-%s.yuv" % (pattern, fmt))
            d = run(p, pattern, 18, 18, frames=2, fmt=fmt)
            need(len(d) == want, "%s/%s 18x18 size" % (pattern, fmt))
    # NV12 really interleaves Cb and Cr: for gradient frame 0, chroma (0,0)
    # is Cb=128 at byte 0 and Cr=128 at byte 1 of the chroma plane.
    d = run(os.path.join(tmp, "gradient-nv12-2.yuv"), "gradient", 176, 144, fmt="nv12")
    need(d[ysize] == 128 and d[ysize + 1] == 128, "NV12 chroma order")

    # A 1920x1080 frame takes under 3 s (the task's requirement).
    t0 = time.monotonic()
    d = run(os.path.join(tmp, "big.yuv"), "gradient", 1920, 1080)
    elapsed = time.monotonic() - t0
    need(len(d) == frame_size(1920, 1080), "1920x1080 frame size")
    need(elapsed < 3.0, "1920x1080 frame in %.3f s (< 3 s)" % elapsed)
    print("  gen_yuv: 1920x1080 frame took %.3f s" % elapsed)

    # PPM helper: P6, 176x144, BT.601 full-range integer conversion.
    # Frame 0 of squares: Y(0,0)=0 with neutral chroma -> (0,0,0);
    # (64,64) is gradient Y=192 with chroma (32,32) neutral -> (192,192,192);
    # (16,16) is square luma 235 with chroma (8,8) Cb=136, Cr=120 -> (224,238,249).
    ppm = os.path.join(tmp, "squares.ppm")
    run(os.path.join(tmp, "squares-1.yuv"), "squares", 176, 144, extra=("--ppm", ppm))
    raw = open(ppm, "rb").read()
    header = b"P6\n176 144\n255\n"
    need(raw.startswith(header), "PPM header")
    pix = raw[len(header):]
    need(len(pix) == 176 * 144 * 3, "PPM payload size")
    need(tuple(pix[0:3]) == (0, 0, 0), "PPM (0,0) black")
    off = (64 * 176 + 64) * 3
    need(tuple(pix[off:off + 3]) == (192, 192, 192), "PPM (64,64) gray")
    off = (16 * 176 + 16) * 3
    need(tuple(pix[off:off + 3]) == (224, 238, 249), "PPM (16,16) BT.601 conversion")

    print("CHECKS %d" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/n2vec-gen"))
