#!/bin/bash
# Host tests (Linux): the ISP on synthetic RAW10 frames, and the sensor-switching reader against a fake /dev/redwood-camera.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
T=${1:-$HERE/out/test}
mkdir -p "$T"
CC=${CC:-clang}
$CC -O2 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I "$HERE/src" -DISP_LINES -o "$T/t_isp" "$HERE/test/t_isp.c" "$HERE/src/camera_isp.c" -lm
"$T/t_isp"
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=thread -I "$HERE/src" -o "$T/t_core" "$HERE/test/t_core.c" "$HERE/src/rwcam_core.c" "$HERE/src/rwcam_align.c" "$HERE/src/camera_isp.c" -lm -lpthread
"$T/t_core"
# the two readers of the pair of back cameras against a fake kernel with the two nodes (KERNEL_ABI_DUAL.md)
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=thread -I "$HERE/src" -o "$T/t_dual" "$HERE/test/t_dual.c" "$HERE/src/rwcam_core.c" "$HERE/src/rwcam_align.c" "$HERE/src/camera_isp.c" -lm -lpthread
"$T/t_dual"
# the synchronized streams group's policy on the real reader and the fake two-node kernel
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=thread -I "$HERE/src" -o "$T/t_pair" "$HERE/test/t_pair.c" "$HERE/src/rwcam_pair.c" "$HERE/src/rwcam_core.c" "$HERE/src/rwcam_align.c" "$HERE/src/camera_isp.c" -lm -lpthread
"$T/t_pair"
# the pictures' two backends (the processor, and the GPU through a made-up rwa_isp library)
$CC -O2 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I "$HERE/src" -o "$T/t_render" "$HERE/test/t_render.c" "$HERE/src/rwcam_render.c" "$HERE/src/camera_isp.c" -lm
"$T/t_render"
# the dylib of the Camera process (src/rwcam_camui.c) against a made-up Objective-C runtime; the program's name, the markers and dlsym are the test's
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -ffreestanding -I "$HERE/src" \
  -Dgetprogname=t_getprogname -Daccess=t_access -Ddlsym=t_dlsym -o "$T/t_camui" "$HERE/test/t_camui.c" "$HERE/src/rwcam_camui.c"
"$T/t_camui"
# the log's limits (how much the bundle may write to the console: by shape of line and by budget)
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I "$HERE/src" -o "$T/t_log" "$HERE/test/t_log.c" "$HERE/src/rwcam_loglimit.c"
"$T/t_log"
# the place of the wide camera's picture in the ultra-wide one's
$CC -O2 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I "$HERE/src" -o "$T/t_align" "$HERE/test/t_align.c" "$HERE/src/rwcam_align.c" -lm
"$T/t_align"
# the pictures thread (camera-userland-20261112-pipe): frames kept by the kernel one at a time, pictures slower than a frame less the read
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=thread -I "$HERE/src" -Daccess=t_access -o "$T/t_pipe" "$HERE/test/t_pipe.c" "$HERE/src/rwcam_core.c" "$HERE/src/rwcam_align.c" "$HERE/src/camera_isp.c" -lm -lpthread
"$T/t_pipe"
PIPE_SERIAL=1 "$T/t_pipe"
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I "$HERE/src" -Daccess=t_access -o "$T/t_pipe_asan" "$HERE/test/t_pipe.c" "$HERE/src/rwcam_core.c" "$HERE/src/rwcam_align.c" "$HERE/src/camera_isp.c" -lm -lpthread
"$T/t_pipe_asan"
# (q10jm) the main camera's sensor modes: REDWOOD_CAMERA_MODE given on rwcam_core_set_mode(), the frames' new size, an older kernel's EINVAL
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=thread -I "$HERE/src" -Daccess=t_access -o "$T/t_mode" "$HERE/test/t_mode.c" "$HERE/src/rwcam_core.c" "$HERE/src/rwcam_align.c" "$HERE/src/camera_isp.c" -lm -lpthread
"$T/t_mode"
NOMODES=1 "$T/t_mode"
# (q10jn) the readers' buffers in a frame memory (zero copy), and the renderer's use of the nodes' contexts
FRAMEMEM=1 "$T/t_mode"
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I "$HERE/src" -o "$T/t_zerocopy" "$HERE/test/t_zerocopy.c" "$HERE/src/rwcam_render.c" "$HERE/src/camera_isp.c" -lm -lpthread
"$T/t_zerocopy"
