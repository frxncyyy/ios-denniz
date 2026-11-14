#!/bin/bash
# gzsize.sh KERNEL...: stripped kernel size and gzip -9 size (fast proxy for the zopfli image delta)
ST=$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/toolchains/cctools/bin/aarch64-apple-darwin-strip
T=$(mktemp -d); trap 'rm -rf $T' EXIT
for k in "$@"; do $ST "$k" -o $T/k 2>/dev/null; printf "%s raw %d gz9 %d\n" "$(basename $k)" $(stat -c %s $T/k) $(gzip -9 -c $T/k | wc -c); done
