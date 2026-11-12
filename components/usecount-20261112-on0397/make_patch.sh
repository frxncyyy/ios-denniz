#!/bin/bash
# 0398 (usecount-20261112): the real IOSurface use count (IOSurfaceIsInUse), shared across processes, with kernel holders.
#  - sweet_surface_use.h (new, pure): protocol (IOSurface.framework 16.7: traps 0/1 on a process's 0->1 / 1->0 step per category,
#    global counts at state page +0x18/+0x1c), per-mapping holds, kernel display/GPU holds, stats, GPU (job, surface) table.
#    Host test: test/usecount_host_test.c (ASan/UBSan + TSan).
#  - SweetSurface.h: SweetSurface::useKern[2]; sweet_surface_use_kernel().
#  - SweetSurface.cpp: traps 0/1 split and real (were one no-op); mapping release drops leftover holds; free() leak check;
#    kern.n2surfuse (boot-arg n2su, default 2) / kern.n2surfuse_report / _live/_inc/_dec; "NORD2 SURF USE:" reports.
#  - SweetFramebuffer.cpp: sweet_fb_retain_surface / sweet_fb_release_surface = the display's hold (every swap reference).
#  - Nord2GPU.cpp: every IOSurface import a job lists is held from submit until the job left QUEUED/RUNNING/STOPPING (tick reaps).
# VARIANT on 0397: a/ = q10jfi stage + 0397 (fbpace-20261112, SweetFramebuffer.cpp only), b/ = a/ + the same 0398 changes as ../usecount-20261112.  Apply after 0397.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0398-nord2-iosurface-use-count.patch
A=iokit/Kernel/arm
FILES="$A/SweetSurface.h $A/sweet_surface_use.h $A/SweetSurface.cpp $A/SweetFramebuffer.cpp $A/Nord2GPU.cpp"
for f in $FILES; do diff -uN a/$f b/$f || true; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d -p "$H" .chk.XXXXXX); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0398; for f in $FILES; do mkdir -p patched-sources-0398/$(dirname $f); cp b/$f patched-sources-0398/$f; done
sha256sum "$OUT"
