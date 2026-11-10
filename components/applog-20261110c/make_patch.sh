#!/bin/bash
# 0287: applog, on top of 0285+0286 (gpu/build/applog-20261110, applog-20261110b). The SWEET AMFI stub answers
# __mac_syscall("AMFI", 90) with bit 8 too for the programs applog_name names, so libsystem_trace honours
# OS_ACTIVITY_DT_MODE there (os_log on standard error -> /dev/null tee -> kernel log). a/ = sweet_amfi.c of the
# q10h6 stage (series up to 0286), patched-sources-0287/ = patched (also what build_p1_candidate.py checks).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0287-nord2-applog-amfi-dev-env-vars.patch
{ diff -u a/bsd/kern/sweet_amfi.c patched-sources-0287/bsd/kern/sweet_amfi.c || true; } | sed -E 's#^(---|\+\+\+) (a|patched-sources-0287)(/[^\t]*)\t.*#\1 \2\3#; s#^\+\+\+ patched-sources-0287/#+++ b/#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
cmp "$T/bsd/kern/sweet_amfi.c" patched-sources-0287/bsd/kern/sweet_amfi.c
sha256sum "$OUT"
