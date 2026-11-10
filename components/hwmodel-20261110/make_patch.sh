#!/bin/bash
# 0291: hw.model answered per process name (n2hwmodel / NORD2_HWMODEL_DEFAULT, e.g. "Camera=N104AP"), bsd/kern/kern_mib.c.
# a/ = the pristine xnu-stage file (no earlier patch of the series touches it), b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0291-nord2-hwmodel-per-process.patch
{ diff -u a/bsd/kern/kern_mib.c b/bsd/kern/kern_mib.c || true; } | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
cmp "$T/bsd/kern/kern_mib.c" b/bsd/kern/kern_mib.c
sha256sum "$OUT"
