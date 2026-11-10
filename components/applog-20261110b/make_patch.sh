#!/bin/bash
# 0286: applog diagnostics on top of 0285 (gpu/build/applog-20261110): one "NORD2 APPLOG: exec ..." line per exec of a program
# applog_name names (environment string present? what are fd 1 and 2?), fd 1/2 of those programs opened on /dev/null when they are
# anything else, one "NORD2 APPLOG: first /dev/null write ..." line per such process. a/ = the files after 0285
# (applog-20261110/b), b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0286-nord2-applog-report-fd-null.patch
for f in bsd/kern/kern_exec.c bsd/dev/mem.c; do cmp "a/$f" "../applog-20261110/b/$f"; done
{ diff -u a/bsd/kern/kern_exec.c b/bsd/kern/kern_exec.c || true
  diff -u a/bsd/dev/mem.c b/bsd/dev/mem.c || true; } | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in bsd/kern/kern_exec.c bsd/dev/mem.c; do cmp "$T/$f" "b/$f"; done
sha256sum "$OUT"
