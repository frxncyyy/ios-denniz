#!/bin/bash
# 0441 (gfxheap-20261113/gpuheap-0441, needs 0440): Nord2GPU and the graphics heap: client GPU-only buffers (ALLOC flags == 2) >= 64 KiB from the heap
#  (sweet_heap_gpu_take, lock dropped; fallback = normal memory after the global check), given back in free_owner (after the GPU unmap); heap bytes
#  left out of privateTotal() (so out of the 0417/0418 GPU budget); imports of heap surfaces accepted (sweet_heap_gpu_page_ok).  "NORD2 GPU HEAP:" lines,
#  "NORD2 MEM GPU:" gets heap=bos/MiB fallback= imports=.
# a/ = q10jgs stage Nord2GPU.cpp (0440 does not touch it).  Checks: clean apply (fuzz 0) on a/, result == b/.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
NAME=0441-nord2-gpu-heap.patch
FILES="iokit/Kernel/arm/Nord2GPU.cpp"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$NAME"
T=$(mktemp -d -p "$H"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"
(cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log || { echo "not clean"; exit 1; }
rm -rf patched-sources-0441
for f in $FILES; do cmp "$T/$f" "b/$f"; mkdir -p patched-sources-0441/$(dirname $f); cp "$T/$f" patched-sources-0441/$f; done
sha256sum "$NAME"
