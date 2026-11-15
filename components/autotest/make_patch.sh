#!/bin/bash
# 0296 on top of the q10hq series (.., 0294): kernel/a = the files of that stage, kernel/b = patched (nord2_autotest.inc is new).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H/kernel"
{ diff -ruN a b || true; } | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$H/0296-nord2-autotest.patch"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$H/0296-nord2-autotest.patch"); diff -r "$T" b && sha256sum "$H/0296-nord2-autotest.patch"
