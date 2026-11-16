#!/bin/bash
# gfxall7: extracts rw_pipe_acc from pan_jm.c and runs gpupipe_acc_host_test.c
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 -I - "$D/pan_jm.c" "$T/acc_extract.inc" <<'PY'
import sys
s = open(sys.argv[1]).read()
a = s.index('static uint8_t\nrw_pipe_acc'); b = s.index('static unsigned\nrw_pipe_batch_idx')
open(sys.argv[2], 'w').write(s[a:b])
PY
cc -O1 -g -Wall -fsanitize=address,undefined -I"$T" -o "$T/t" "$D/gpupipe_acc_host_test.c"
"$T/t"
