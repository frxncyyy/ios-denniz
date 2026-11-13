#!/bin/bash
# 0420 (memgov-20261113): NORD2 MEMGOV, a low-memory governor for our non-jetsam XNU (policy + rationale: b/bsd/kern/nord2_memgov.h).
#  kern_exec.c: the governor thread (started by the first execve = launchd), sysctls kern.n2memgov (0 off / 1 log only = default / 2 kill),
#  kern.n2memgov_low / _crit (permille of gPhysSize, 200 / 120), kern.n2memgov_maxband (30); kills via memorystatus_kill_on_zone_map_exhaustion(pid).
#  SweetSurface.cpp: sweet_surface_purge_kick_memgov() = one 0417/0418 purge pass now (third thread call, label "memgov").
#  a/ = the q10jgs stage (kern_exec.c untouched by 0394..0418; SweetSurface.cpp = memfix-20261113b/patched-sources-0418).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
NAME=0420-nord2-memgov.patch
FILES="bsd/kern/kern_exec.c bsd/kern/nord2_memgov.h iokit/Kernel/arm/SweetSurface.cpp"
for f in $FILES; do
  if [ -f a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f  || true; fi
done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $NAME
T=$(mktemp -d "$H/.apply.XXXX"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-0420; for f in $FILES; do mkdir -p patched-sources-0420/$(dirname $f); cp b/$f patched-sources-0420/$f; done
cc -std=c11 -Wall -Wextra -Werror -o test/memgov_host_test test/memgov_host_test.c && ./test/memgov_host_test > test/memgov_host_test.out
sha256sum $NAME
