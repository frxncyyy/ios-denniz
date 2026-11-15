#!/bin/bash
# regenerate piece-0761 from r2/a (= 0760 result = q10jfinal26bt stage files) and r2/b (0761)
set -e; cd "$(dirname "$0")"
rm -rf piece-0761; mkdir -p piece-0761/patched-sources-0761
(cd r2 && diff -ruN a b) > piece-0761/0761-nord2-scp-orientation-light.patch || true
cp -r r2/b/* piece-0761/patched-sources-0761/
sha256sum piece-0761/0761-nord2-scp-orientation-light.patch r2/b/bsd/dev/nord2_scp.inc r2/b/iokit/Kernel/arm/hid/SweetHID.cpp
