#!/bin/bash
# regenerate piece-0760 from a/ (q10jfinal23bt stage files) and b/ (0760)
set -e; cd "$(dirname "$0")"
rm -rf piece-0760; mkdir -p piece-0760/patched-sources-0760
diff -ruN a b > piece-0760/0760-nord2-scp-sensors-ios.patch || true
cp -r b/* piece-0760/patched-sources-0760/
sha256sum piece-0760/0760-nord2-scp-sensors-ios.patch b/bsd/dev/nord2_scp.inc b/iokit/Kernel/arm/hid/SweetHID.cpp
