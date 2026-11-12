#!/bin/bash
# Host tests of the M1 kernel code (no phone): the register model under ASan+UBSan and plain -O2.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
clang -std=gnu11 -Wall -Wextra -Wno-unused-function -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all "$H/t_hwcodec.c" -o "$T/a" && "$T/a"
clang -std=gnu11 -Wall -Wextra -Wno-unused-function -O2 "$H/t_hwcodec.c" -o "$T/b" && "$T/b"
# the host tool's copy of the test picture is the kernel's, and its checker accepts a real JPEG of it and rejects a wrong one
"$T/b" dump "$T/c.nv12" > /dev/null; python3 "$H/../tools/hwc.py" pattern 642x482 "$T/py.nv12"; cmp "$T/c.nv12" "$T/py.nv12"
python3 - "$H/../tools" "$T" <<'P'
import sys, subprocess; sys.path.insert(0, sys.argv[1]); import hwc; t = sys.argv[2]
y, u, v = hwc.planes(640, 480); open(t + '/p.yuv', 'wb').write(y + u + v)
subprocess.run(['ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'yuvj420p', '-s', '640x480', '-i', t + '/p.yuv', '-q:v', '4', '-frames:v', '1', t + '/good.jpg'], check=True)
ok, rep = hwc.check(t + '/good.jpg', 640, 480); assert ok and rep['psnr_y'] > 30, rep
open(t + '/z.yuv', 'wb').write(bytes(len(y) + len(u) + len(v)))
subprocess.run(['ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'yuvj420p', '-s', '640x480', '-i', t + '/z.yuv', '-frames:v', '1', t + '/black.jpg'], check=True)
ok, rep = hwc.check(t + '/black.jpg', 640, 480); assert not ok, rep
ok, rep = hwc.check(t + '/good.jpg', 320, 240); assert not ok
open(t + '/cut.jpg', 'wb').write(open(t + '/good.jpg', 'rb').read()[:-500]); ok, rep = hwc.check(t + '/cut.jpg', 640, 480); assert not ok
print('N2TEST hwcodec-M1-tool PASS (pattern identical, checker accepts good / rejects black, wrong size, truncated)')
P
