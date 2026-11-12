#!/bin/bash
# 0395 (gpufence-20261112, Track B of the HW display composition): Nord2GPU accepts display-pool surfaces and keeps per-surface GPU write fences.
#  - importSurface: pages of the display pool (sweet_fb_hw_pool.h, default 0xC0000000+256 MiB) are accepted besides XNU RAM; a surface is never
#    mixed pool/RAM; every other check unchanged (valid_ram itself and its 5 other callers untouched).  Boot-arg n2gpool=0 = refuse again.
#  - write fences (nord2_gpu_fence.h, new): every successful submit (SUBMIT_JM, compute) records its job id per written IOSurface import in a
#    64-slot table; pending = live scan of core->jobs[]; completed = low-water mark.  API for SweetFramebuffer: nord2_gpu_surface_write_seq,
#    _write_pending, _writes_done(id, seq), nord2_gpu_completed_seq, nord2_gpu_surface_wait_idle(id, us).
#  - diagnostics: NORD2 GPU FENCE (boot line, pool imports, counters in the 2 s stats window).
# a/ = q10jfe stage (Nord2GPU.cpp sha 4cdd9ff2..., identical in q10jfh), b/ = patched; patched-sources-0395 = b/.  Host test: test/gpufence_host_test.c.
# The contract copy of the header lives at ../hwcomp-20261112/nord2_gpu_fence.h (kept identical by this script).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0395-nord2-gpu-pool-fence.patch
FILES="iokit/Kernel/arm/Nord2GPU.cpp iokit/Kernel/arm/nord2_gpu_fence.h"
for f in $FILES; do if [ -e a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f | sed "1s#^--- /dev/null.*#--- /dev/null#" || true; fi; done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0395; for f in $FILES; do mkdir -p patched-sources-0395/$(dirname $f); cp b/$f patched-sources-0395/$f; done
cp b/iokit/Kernel/arm/nord2_gpu_fence.h ../hwcomp-20261112/nord2_gpu_fence.h
(cd test && cc -std=c11 -O2 -Wall -Wextra -Werror -I../b/iokit/Kernel/arm gpufence_host_test.c -o gft && ./gft > gpufence_host_test.out && tail -1 gpufence_host_test.out)
sha256sum "$OUT"
