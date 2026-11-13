#!/bin/bash
# 0452 (bsd/dev) and 0453 (iokit) = work/b (gen.py) minus the tree after 0450+0451 (work/a + ../patched-sources-0450/0451, frozen: q10jhe).
set -euo pipefail
cd "$(dirname "$0")"; ./regen.sh > /dev/null; rm -rf prev; cp -a a prev; cp -a ../patched-sources-0450/. prev/; cp -a ../patched-sources-0451/. prev/
mk() { n=$1; name=$2; top=$3; out=../$n-$name.patch; : > $out; rm -rf ../patched-sources-$n
  for f in $(cd b && find $top -type f | sort); do cmp -s b/$f prev/$f 2>/dev/null && continue
    mkdir -p ../patched-sources-$n/$(dirname $f); cp b/$f ../patched-sources-$n/$f
    if [ -f prev/$f ]; then diff -u --label a/$f --label b/$f prev/$f b/$f >> $out || true; else diff -u --label /dev/null --label b/$f /dev/null b/$f >> $out || true; fi; done
  echo "$out: $(grep -c '^+++ ' $out) files, +$(grep -c '^+[^+]' $out) -$(grep -c '^-[^-]' $out)"; }
mk 0452 nord2-wifi-8021x-ptk-rekey bsd
mk 0453 nord2-wifi-8021x-handshake-message iokit
rm -rf prev
