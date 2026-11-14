#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""hevc_check.py -- validate an HEVC Annex B stream (T02).

Usage:
  hevc_check.py FILE WIDTH HEIGHT FRAMES [--source raw.yuv] [--min-psnr 30]
                [--min-psnr-chroma 30] [--json out.json] [--quiet]

Exit 0 = PASS, 1 = FAIL, 2 = usage/tool/parse error.  See n2vcheck.py for the
checks (SPS profile/level, ffprobe geometry/frame count, ffmpeg decode, key
frames, per-frame PSNR against a raw NV12 (default) or I420 source).
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True

sys.path.insert(0, str(Path(__file__).resolve().parent))
import n2vcheck  # noqa: E402

if __name__ == "__main__":
    sys.exit(n2vcheck.main("hevc"))
