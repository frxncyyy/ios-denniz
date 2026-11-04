#!/bin/sh
# Regenerate 0084 from the mutable CI buildtree stage (which already carries
# 0082/0083).  The .nord2orig copies are the pre-0084 versions of edited files.
set -eu
cd "$(dirname "$0")/../build/xnu-stage"
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT
for f in bsd/conf/files iokit/conf/files.arm64 iokit/KernelConfigTables.cpp bsd/dev/sweet_touch.c; do
  diff -u --label "a/$f" --label "b/$f" "$f.nord2orig" "$f" >> "$OUT" || true
done
for f in bsd/dev/nord2_gpu_*.h bsd/dev/nord2_gpu_*.c iokit/Kernel/arm/Nord2GPU.cpp; do
  diff -u --label /dev/null --label "b/$f" /dev/null "$f" >> "$OUT" || true
done
cp "$OUT" ../../../xnu/patches/0084-nord2-gpu.patch
patch -p1 -R --dry-run < ../../../xnu/patches/0084-nord2-gpu.patch >/dev/null
echo "regenerated xnu/patches/0084-nord2-gpu.patch"
