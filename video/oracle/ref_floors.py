#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build content-aware PSNR floors from an x264 reference encode.

  ref_floors.py SOURCE.yuv REFERENCE.yuv WxH FRAMES OUT.json [MARGIN]

For every frame, the floor is max(10.0, x264_luma_psnr - MARGIN).  The x264
reference is produced by run_oracle.sh with the same input, size and rate
(constant QP or target bitrate); x264 is a black-box quality oracle here,
never linked into anything that goes onto the phone.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import yuv  # noqa: E402


def main(argv):
    if len(argv) < 5:
        print("usage: ref_floors.py SOURCE REF WxH FRAMES OUT [MARGIN]", file=sys.stderr)
        return 2
    src, ref, size, frames, out = argv[:5]
    margin = float(argv[5]) if len(argv) > 5 else 6.0
    try:
        w, h = (int(v) for v in size.lower().split("x"))
    except ValueError:
        print("ref_floors: bad size %r" % size, file=sys.stderr)
        return 2
    floors = []
    for i, (py, _pcb, _pcr) in enumerate(yuv.psnr_frames(src, ref, w, h, int(frames))):
        floors.append(round(max(10.0, py - margin), 3))
        if i + 1 >= int(frames):
            break
    if len(floors) != int(frames):
        print("ref_floors: reference has %d frames, expected %d"
              % (len(floors), int(frames)), file=sys.stderr)
        return 1
    with open(out, "w", encoding="utf-8") as f:
        json.dump(floors, f)
    print("FLOORS %s margin=%.1f min=%.2f max=%.2f"
          % (out, margin, min(floors), max(floors)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
