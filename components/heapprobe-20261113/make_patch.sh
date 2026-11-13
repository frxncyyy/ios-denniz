#!/bin/bash
# 0418 (heapprobe-20261113): run-time probe of the low-bank graphics-heap regions (sweet_fb_heap.h, 9 regions, 996.5 MiB):
#  kern.n2heap_probe / boot-arg n2hp (0 off, 1 read only, 2 + canary write/readback/restore; compiled default 2), steps at boot+60/180/600 s,
#  "NORD2 HEAP PROBE:" lines + sysctls kern.n2heap_probe_step/_free_mib/_bad.  Nothing allocated; XNU's window and the display pool are refused.
#  Files: SweetSurface.cpp (include + arm call), sweet_fb_heap.h (new, pure, host test test/heap_host_test.c), sweet_fb_heap_probe.inc (new).
# a/ = q10jgp stage (= memfix jgn patched-sources-0417).  Output: 0418-nord2-heap-probe.patch + patched-sources-0418/.
# Checks: host test, clean apply (no offset/fuzz) on a/, result == b/.  Compile-checked with gpu/tools/xnu_staged_compile.py (chk/, rc 0).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
NAME=0418-nord2-heap-probe.patch
FILES="iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/sweet_fb_heap.h iokit/Kernel/arm/sweet_fb_heap_probe.inc"
cc -std=c11 -Wall -Wextra -Werror -o test/heap_host_test.bin test/heap_host_test.c && ./test/heap_host_test.bin | tail -1 && rm test/heap_host_test.bin
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$NAME"
T=$(mktemp -d -p "$H"); cp -a a/. "$T/"
(cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log || { echo "not clean"; exit 1; }
rm -rf patched-sources-0418
for f in $FILES; do cmp "$T/$f" "b/$f"; mkdir -p patched-sources-0418/$(dirname $f); cp "$T/$f" patched-sources-0418/$f; done
rm -rf "$T"
sha256sum "$NAME"
