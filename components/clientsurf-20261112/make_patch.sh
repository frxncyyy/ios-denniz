#!/bin/bash
# 0360 (clientsurf-20261112), on top of the q10jm series (.., 0340): client-memory IOSurfaces wrap the creator's pages (wired) instead
# of a snapshot copy, so the GPU and other processes see what the creator writes after the creation; every client-memory creation
# logged; importSurface logs client-backed imports. a/ = the q10jm stage files, b/ = patched. patched-sources-0360 = b/ (source check).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0360-nord2-surface-client-wrap.patch
FILES="iokit/Kernel/arm/SweetSurface.h iokit/Kernel/arm/sweet_surface_abi.h iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/Nord2GPU.cpp"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0360; mkdir -p patched-sources-0360/iokit/Kernel/arm; for f in $FILES; do cp b/$f patched-sources-0360/$f; done
sha256sum "$OUT"
