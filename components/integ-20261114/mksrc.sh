#!/bin/bash
# mksrc.sh NNNN name file...: author an integration patch src-NNNN/NNNN-name.patch from src-NNNN/a/<file> (chain state before it,
# saved by hand after `merge.sh <previous>`) to chain/s/<file> (edited in place); fuzz-0 check + patched-sources-NNNN.
set -euo pipefail; cd "$(dirname "$0")"; n=$1; name=$2; shift 2; D=src-$n; out=$D/$n-$name.patch; : > $out
for f in "$@"; do diff -u $D/a/$f chain/s/$f | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> $out || true; done
T=$(mktemp -d); trap 'rm -rf $T' EXIT; for f in "$@"; do mkdir -p $T/$(dirname $f); cp $D/a/$f $T/$f; done
(cd $T && patch -p1 --fuzz=0 --batch -i $OLDPWD/$out) > $D/apply.log; ! grep -E 'offset|fuzz|FAILED|Reversed' $D/apply.log
rm -rf $D/patched-sources-$n; for f in "$@"; do cmp $T/$f chain/s/$f; mkdir -p $D/patched-sources-$n/$(dirname $f); cp chain/s/$f $D/patched-sources-$n/$f; done
sha256sum $out
