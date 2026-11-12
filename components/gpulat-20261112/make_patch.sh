#!/bin/bash
# 0400 (gpulat-20261112): GPU job latency and allocation stalls.
#  - nord2_gpu_core.c/.h: a completion seen by n2c_poll drops its buffer references AFTER try_start (the next job is on the GPU before the
#    teardown of released buffers: MMU unmap, AS flush, owner release); latency accounting struct n2c_lat / n2c_lat_take / n2c_lat_irq
#    (irq->poll, handoff predecessor-done->start, vertex->fragment phase gap, queue depth, kinds, same-slot starts, busy time, release time).
#  - Nord2GPU.cpp: bottom-half kernel thread (prio 92) woken by the job interrupt runs n2c_poll at once (was timer -> thread call -> workloop);
#    buffer owners (unwire/release of MiBs) released by a thread call outside the GPU lock; externalMethod lock wait/hold maxima;
#    fresh pageable (flags=4) allocations not zeroed a second time after prepare() (65 sampled reads, the first 4 read back fully, any
#    non-zero = full zero pass and the skip is off for the boot); "NORD2 GPU LAT:" / "NORD2 GPU LAT2:" per 2 s; sysctl kern.n2gpu_lat /
#    boot-arg n2lat = off-mask (1 BH thread, 2 late release, 4 zero skip, 8 deferred owner free).
# a/ = q10jfh stage core files + 0398's Nord2GPU.cpp (usecount-20261112/patched-sources-0398, which contains 0395); b/ = patched.
# Apply after 0398 (0397 and 0399 touch other files).  Host tests: test/core_test_0400.c (+ the 27 production core scenarios).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0400-nord2-gpu-latency.patch
FILES="bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c iokit/Kernel/arm/Nord2GPU.cpp"
for f in $FILES; do diff -u a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d -p "$H" .chk.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0400; for f in $FILES; do mkdir -p patched-sources-0400/$(dirname $f); cp b/$f patched-sources-0400/$f; done
S=$H/../device-mmu-q10jfh-candidate/k/kernel/stage/bsd/dev
(cd test && cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-unused-parameter -I../b/bsd/dev -I"$S" core_test_0400.c ../b/bsd/dev/nord2_gpu_core.c "$S/nord2_gpu_mmu.c" "$S/nord2_gpu_va.c" -o core0400 && ./core0400 > core_test_0400.out 2>&1 && tail -2 core_test_0400.out)
sha256sum "$OUT"
