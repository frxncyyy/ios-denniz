#!/bin/bash
# gpuas-20261114: 0500 = dynamic GPU address spaces (contexts decoupled from the 8 Mali AS; LRU lending of AS1..AS7 at job start; kill switch
# n2gctx=0 / n2gdv +32) + "no free context" answered kIOReturnNoResources instead of kIOReturnIOError (libredwoodair retried EIO 100 x 100 ms).
#   a/ = the q10jcg chain before 0500: integ-20261114/chain/s (core .c/.h) and ccperf-20261114/b (Nord2GPU.cpp after 0492); 0491 and 0494 touch
#        other files.  b/ = 0500.
#   r0494/ = 0494 (ccperf-fix-20261114) regenerated fuzz-0 for a chain WITHOUT 0493: same changed lines, hunk 3 four lines earlier (the
#        original applies to q10jia+0492 only with "offset -4", which the builder's preflight refuses).
# Host tests: test/run.sh.  Merge check on a copy of integ-20261114: merge_check.sh.  Nothing here writes outside this directory.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
n=0500; name=nord2-gpu-dynamic-as
files="bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c iokit/Kernel/arm/Nord2GPU.cpp"
out=$H/$n-$name.patch; : > "$out"
for f in $files; do diff -u "a/$f" "b/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; done
mkdir "$T/s"; cp -a a/. "$T/s/"
(cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$H/apply-$n.log"
! grep -E 'offset|fuzz|FAILED|Reversed' "$H/apply-$n.log"
rm -rf "$H/patched-sources-$n"
for f in $files; do cmp "$T/s/$f" "b/$f"; mkdir -p "$H/patched-sources-$n/$(dirname "$f")"; cp "b/$f" "$H/patched-sources-$n/$f"; done
sha256sum "$out"
