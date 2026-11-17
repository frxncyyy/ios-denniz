#!/bin/bash
# Rebase 0281 (call-audio bb hooks) onto the CURRENT 0277 sources and refresh patched-sources-0281.
# Run after every 0277 regeneration (the builder asserts the highest patched-sources-NNNN <= top of series matches).
# Usage: make_0281.sh [path-to-bb-dir-with-patched-sources-0277]   (default: ../bb1)
set -euo pipefail
CA=$(cd "$(dirname "$0")" && pwd)
BB=${1:-$CA/../bb1}
FILES="bsd/kern/sweet_amfi.c iokit/Kernel/arm/Nord2Baseband.cpp"
T=$(mktemp -d "$CA/.mk0281.XXXXXX"); trap 'rm -rf "$T"' EXIT
for f in $FILES; do mkdir -p "$T/a/$(dirname $f)" "$T/b/$(dirname $f)"; cp "$BB/patched-sources-0277/$f" "$T/a/$f"; cp "$BB/patched-sources-0277/$f" "$T/b/$f"; done
( cd "$T/b" && patch -p1 -i "$CA/0281-call-audio-bb-hooks.patch" )      # fuzz/offset is fine; a reject stops here
( cd "$T" && for f in $FILES; do diff -u "a/$f" "b/$f" || true; done ) | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$T/new.patch"
mv "$T/new.patch" "$CA/0281-call-audio-bb-hooks.patch"
rm -rf "$CA/patched-sources-0281"; mkdir -p "$CA/patched-sources-0281"
( cd "$T/b" && cp --parents $FILES "$CA/patched-sources-0281/" )
grep -c "nord2_call_" "$CA/patched-sources-0281/bsd/kern/sweet_amfi.c" "$CA/patched-sources-0281/iokit/Kernel/arm/Nord2Baseband.cpp"
sha256sum "$CA/0281-call-audio-bb-hooks.patch"
