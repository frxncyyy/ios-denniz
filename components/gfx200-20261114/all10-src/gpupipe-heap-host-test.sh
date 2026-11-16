#!/bin/bash
# gfxall6: extracts rw_pipe_batch_idx / rw_pipe_note_desc / rw_pipe_heap_of (+ the table) from pan_jm.c and runs gpupipe_heap_host_test.c
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 -I - "$D/pan_jm.c" "$T/heap_extract.inc" <<'PY'
import sys
s = open(sys.argv[1]).read()
a = s.index('#define RW_PIPE_CTXS'); b = s.index('/* every heap of this device')
open(sys.argv[2], 'w').write(s[a:b])
PY
cc -O1 -g -Wall -fsanitize=address,undefined -I"$T" -o "$T/t" "$D/gpupipe_heap_host_test.c" -lpthread
"$T/t"
