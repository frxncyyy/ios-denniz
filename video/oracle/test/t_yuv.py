#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Self-tests for oracle/yuv.py (pure-Python I420/NV12 helpers)."""

import hashlib
import math
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import yuv  # noqa: E402

passed = 0
failed = 0


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print("YUVCHECK %-28s PASS" % name)
    else:
        failed += 1
        print("YUVCHECK %-28s FAIL %s" % (name, detail))


def main():
    # 1. sizes, including odd dimensions (chroma rounds up)
    check("frame_size.176x144", yuv.frame_size(176, 144) == 38016,
          str(yuv.frame_size(176, 144)))
    check("plane_sizes.178x147",
          yuv.plane_sizes(178, 147) == [("Y", 178, 147), ("Cb", 89, 74), ("Cr", 89, 74)],
          str(yuv.plane_sizes(178, 147)))
    check("frame_size.178x147",
          yuv.frame_size(178, 147) == 178 * 147 + 2 * 89 * 74)

    # 2. NV12 <-> I420, literal bytes
    w, h = 4, 2
    yy = bytes(range(8))
    uv = bytes([1, 2, 3, 4])
    nv12 = yy + uv
    i420 = yuv.nv12_to_i420(nv12, w, h)
    check("nv12_to_i420", i420 == yy + bytes([1, 3]) + bytes([2, 4]),
          i420.hex())
    check("i420_to_nv12", yuv.i420_to_nv12(i420, w, h) == nv12)

    # 3. PSNR: infinite on identity, 48.1308 dB for a difference of 1
    a = bytes(16)
    b = bytes([1] * 16)
    check("psnr.identity", yuv.psnr(a, a, 4, 4) == float("inf"))
    p = yuv.psnr(a, b, 4, 4)
    check("psnr.diff1", abs(p - 48.1308036086791) < 1e-9, "%.12f" % p)
    check("psnr.integer-agrees", abs(p - yuv.psnr_integer(a, b, 4, 4)) < 1e-12)
    check("psnr.diff2", abs(yuv.psnr(a, bytes([2] * 16), 4, 4)
                            - (48.1308036086791 - 20 * math.log10(2))) < 1e-9)

    # 4. first_diff on files (4x4: Y 16, Cb 4, Cr 4 = 24 bytes per frame)
    with tempfile.TemporaryDirectory() as td:
        f0 = (bytes(16), bytes([1, 2, 3, 4]), bytes([5, 6, 7, 8]))
        f1 = (bytes(16), bytes([1, 2, 3, 4]), bytes([5, 6, 7, 8]))
        p0 = os.path.join(td, "a.yuv")
        p1 = os.path.join(td, "b.yuv")
        with open(p0, "wb") as f:
            for fr in (f0, f1):
                f.write(b"".join(fr))
        with open(p1, "wb") as f:
            for fr in (f0, f1):
                f.write(b"".join(fr))
        check("first_diff.identical", yuv.first_diff(p0, p1, 4, 4) is None)

        # flip Cb sample (x=1, y=0) of frame 1
        with open(p1, "r+b") as f:
            fs = yuv.frame_size(4, 4)
            f.seek(fs + 16 + 1)
            f.write(b"\xaa")
        check("first_diff.position", yuv.first_diff(p0, p1, 4, 4) == (1, 1, 1, 0),
              repr(yuv.first_diff(p0, p1, 4, 4)))

        # 5. md5 per frame
        want = [hashlib.md5(b"".join(fr)).hexdigest() for fr in (f0, f1)]
        check("frame_md5s", yuv.frame_md5s(p0, 4, 4) == want,
              str(yuv.frame_md5s(p0, 4, 4)))

        # 6. luma md5 is the md5 of the luma plane alone
        check("luma_md5s", yuv.luma_md5s(p0, 4, 4)
              == [hashlib.md5(f0[0]).hexdigest(), hashlib.md5(f1[0]).hexdigest()])

    print("YUVCHECK SUMMARY %d/%d" % (passed, passed + failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
