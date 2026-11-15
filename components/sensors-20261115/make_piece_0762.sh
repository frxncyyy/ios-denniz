#!/bin/bash
# regenerate piece-0762 from r3/a (= 0761 result = q10jfinal29bt stage files) and r3/b (0762)
set -e; cd "$(dirname "$0")"
rm -rf piece-0762; mkdir -p piece-0762/patched-sources-0762
(cd r3 && diff -ruN a b) > piece-0762/0762-nord2-scp-light-summary.patch || true
cp -r r3/b/* piece-0762/patched-sources-0762/
sha256sum piece-0762/0762-nord2-scp-light-summary.patch r3/b/bsd/dev/nord2_scp.inc r3/b/iokit/Kernel/arm/hid/SweetHID.cpp
