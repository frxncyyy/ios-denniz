#!/bin/bash
# 0288: the SWEET Sandbox stand-in issues and consumes sandbox extensions (ops 5 and 6 answered ENOENT; tccd could not complete the
# camera/microphone authorization of Camera). On top of the q10h7 series (..,0285,0286,0287). a/ = sweet_amfi.c of the q10h7 stage,
# patched-sources-0288/ = patched (also what build_p1_candidate.py checks).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0288-nord2-sandbox-extension-issue-consume.patch
{ diff -u a/bsd/kern/sweet_amfi.c patched-sources-0288/bsd/kern/sweet_amfi.c || true; } | sed -E 's#^(---|\+\+\+) (a|patched-sources-0288)(/[^\t]*)\t.*#\1 \2\3#; s#^\+\+\+ patched-sources-0288/#+++ b/#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
cmp "$T/bsd/kern/sweet_amfi.c" patched-sources-0288/bsd/kern/sweet_amfi.c
sha256sum "$OUT"
