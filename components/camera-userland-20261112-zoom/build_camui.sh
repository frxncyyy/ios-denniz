#!/bin/bash
# Builds libredwoodcamui.dylib (src/rwcam_camui.c: Camera.app offers the ultra-wide and the wide camera as two devices, "0.5x" / "1x") into
# ./out/camui. It goes to /usr/lib on the root (../apps-20261104/guest-camuw.sh with payload-camuw; undo: guest-camuw-undo.sh) and into the Camera
# process by DYLD_INSERT_LIBRARIES, which the kernel adds at exec (see the head of src/rwcam_camui.c). Nothing of Camera.app is changed.
#
# usage: build_camui.sh [out dir]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/out/camui}
RES=$(clang -print-resource-dir)
NAME=/usr/lib/libredwoodcamui.dylib
mkdir -p "$OUT"
for f in rwcam_log rwcam_loglimit rwcam_camui; do
  clang -c --target=arm64-apple-ios16.0 -O2 -ffreestanding -nostdinc -isystem "$RES/include" -isystem "$HERE/sysinc" -I "$HERE/src" \
    -fno-common -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -o "$OUT/$f.o" "$HERE/src/$f.c"
done
# libobjc is a plain import here: Camera.app links it itself
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -dylib -no_adhoc_codesign -install_name "$NAME" -o "$OUT/libredwoodcamui.dylib" \
  "$OUT/rwcam_log.o" "$OUT/rwcam_loglimit.o" "$OUT/rwcam_camui.o" "$HERE/sdk/lib/libSystem.tbd" "$HERE/sdk/lib/libobjc.A.tbd"
chmod 755 "$OUT/libredwoodcamui.dylib"
sha256sum "$OUT/libredwoodcamui.dylib"
