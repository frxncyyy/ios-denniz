#!/bin/bash
# 0285: applog / applog_name (port of ios-topaz 0920+0921). a/ = pristine xnu-stage files
# (gpu/build/xnu-stage-backup-pre-p15-20261028/xnu-stage; no earlier patch of the q10gk series touches them), b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0285-nord2-applog-applog_name.patch
{ diff -u a/bsd/kern/kern_exec.c b/bsd/kern/kern_exec.c || true
  diff -u a/bsd/dev/mem.c b/bsd/dev/mem.c || true; } | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in bsd/kern/kern_exec.c bsd/dev/mem.c; do cmp "$T/$f" "b/$f"; done
sha256sum "$OUT"
