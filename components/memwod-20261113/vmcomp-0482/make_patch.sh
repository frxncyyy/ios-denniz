#!/bin/bash
# 0482 (memwod-20261113): VM compressor mode 2 (no swap) by default, in code (no boot-arg: 12 characters free).  b/bsd/kern/nord2_vmcomp.h
#  (rationale + the STARTUP(KMEM, RANK_LAST) hook that runs before vm_fault_init); sweet_amfi.c only includes it (one line, before the final #endif).
#  a/ = the q10jgs stage (sweet_amfi.c untouched by 0394..0418 and by the integration chain 0420..0470).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
N=0482; NAME=$N-nord2-vm-compressor-no-swap.patch
FILES="bsd/kern/nord2_vmcomp.h bsd/kern/sweet_amfi.c"
for f in $FILES; do
  if [ -f a/$f ]; then diff -u a/$f b/$f || true; else diff -u /dev/null b/$f || true; fi
done | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > $NAME
T=$(mktemp -d "$H/.apply.XXXX"); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -p1 --fuzz=0 --batch -i "$H/$NAME") > apply.log
! grep -E 'offset|fuzz|FAILED|Reversed' apply.log
for f in $FILES; do cmp "$T/$f" "b/$f"; done
rm -rf patched-sources-$N; for f in $FILES; do mkdir -p patched-sources-$N/$(dirname $f); cp b/$f patched-sources-$N/$f; done
cc -std=c11 -Wall -Wextra -Werror -o test/vmcomp_host_test test/vmcomp_host_test.c && ./test/vmcomp_host_test > test/vmcomp_host_test.out
tail -1 test/vmcomp_host_test.out; grep -c '^@@' $NAME; sha256sum $NAME
