#!/bin/bash
# 0481 (memwod-20261113): kernel side of MADVISE for GPU buffers + purge of DONTNEED buffers under pressure, default OFF (kern.n2gpu_madv 0/1).
#  ABI and rules: b/iokit/Kernel/arm/nord2_gpu_madv.h (pure, host-tested); kernel part nord2_gpu_madv_kern.inc; Nord2GPU.cpp: 6 one-line hooks
#  (BufferOwner.madv, forward declaration, free_owner, the two includes, mapBuffer's pageable options, externalMethod selector 16).
#  a/ = the q10jgs stage + 0480 (../wod-0480/b).  No shim change is included: until the shim calls selector 16 nothing is ever marked.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
N=0481; NAME=$N-nord2-gpu-madvise-purge.patch
FILES="iokit/Kernel/arm/Nord2GPU.cpp iokit/Kernel/arm/nord2_gpu_madv.h iokit/Kernel/arm/nord2_gpu_madv_kern.inc"
cmp a/iokit/Kernel/arm/Nord2GPU.cpp ../wod-0480/b/iokit/Kernel/arm/Nord2GPU.cpp
for f in $FILES; do
  if [ -f a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f || true; fi
done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $NAME
T=$(mktemp -d "$H/.apply.XXXX"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-$N; for f in $FILES; do mkdir -p patched-sources-$N/$(dirname $f); cp b/$f patched-sources-$N/$f; done
cc -std=c11 -O2 -Wall -Wextra -Werror -o test/madv_host_test test/madv_host_test.c && ./test/madv_host_test > test/madv_host_test.out
tail -2 test/madv_host_test.out; grep -c '^@@' $NAME; sha256sum $NAME
