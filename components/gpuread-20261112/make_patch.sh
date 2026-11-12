#!/bin/bash
# 0361 (gpuread-20261112), on top of the q10jq series (.., 0340, 0360): measure the GPU's view of imported YCbCr surfaces in the kernel
# (IMPCHK at import, SUBMITCHK at MobileSlideShow's job submits, boot-arg n2impat=1 writes a 0xAA pattern) and publish new GPU mappings
# with AS FLUSH_MEM (L2 clean+invalidate + TLB) instead of FLUSH_PT (boot-arg n2mapfm=0 restores FLUSH_PT).
# a/ = the q10jq stage files, b/ = patched; patched-sources-0361 = b/ (the build's source check).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0361-nord2-gpu-import-check-flushmem.patch
FILES="bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c iokit/Kernel/arm/Nord2GPU.cpp"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0361; for f in $FILES; do mkdir -p patched-sources-0361/$(dirname $f); cp b/$f patched-sources-0361/$f; done
sha256sum "$OUT"
