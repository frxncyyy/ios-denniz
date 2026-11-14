#!/bin/bash
# Builds RedwoodAudio.driver WITH the camera userland (rwcam_*.c + camera_isp.c) into ./out (a new directory; the audio-only
# halplug/out-h is not touched), then packages it as out/audio.pkg the way out-h/audio.pkg is made (tools/nord2-audio-package.py build).
#
#   audio_plugin.c   compiled from halplug/d exactly as halplug/build_local.sh does (the object is byte-identical to out-h/audio_plugin.o: checked)
#   camera TUs       clang --target=arm64-apple-ios16.0, hand-written headers (sysinc/, src/rwcam_abi.h), no Apple SDK
#   link             ld64.lld -bundle against the TBD stubs in sdk/lib (tools/make_tbds.py) -- only install names of system libraries
#
# usage: build_cam.sh [out dir]      (default: ./out)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/out}
W=$NORD2_WORK
HP=$W/gpu/build/scratchpad-backup-20261106/halplug
PKG=$(cd "$W/.." 2>/dev/null && pwd)/tools/nord2-audio-package.py
[ -f "$PKG" ] || PKG=$NORD2_REPO/tools/nord2-audio-package.py
RES=$(clang -print-resource-dir)
mkdir -p "$OUT/obj" "$OUT/RedwoodAudio.driver"

# 1. the audio plug-in, unchanged (same flags as halplug/build_local.sh)
clang -c --target=arm64-apple-ios16.0 -O1 -ffreestanding -nostdinc -isystem "$RES/include" -isystem "$HP/sdk/include" \
  -I "$HP/d" -D__unused='__attribute__((unused))' -fno-common -Wall -Wno-four-char-constants -Wno-multichar \
  -o "$OUT/obj/audio_plugin.o" "$HP/d/audio_plugin.c"

# 2. the camera
OBJS=()
for f in rwcam_log rwcam_loglimit rwcam_align rwcam_core rwcam_flash rwcam_pair rwcam_render rwcam_device rwcam_init rwcam_diag camera_isp; do
  # (the package is 256 KiB at most: what runs once in a while is built for size, what makes the pictures for speed)
  # (q10jn: rwcam_core and rwcam_align for size -- the reader's logic, not the pixels -- the package had crossed a page)
  case $f in camera_isp|rwcam_render) OPT=-O2 ;; *) OPT=-Os ;; esac
  clang -c --target=arm64-apple-ios16.0 $OPT -ffreestanding -nostdinc -isystem "$RES/include" -isystem "$HERE/sysinc" -I "$HERE/src" \
    -fno-common -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -o "$OUT/obj/$f.o" "$HERE/src/$f.c"
  OBJS+=("$OUT/obj/$f.o")
done

# 3. link (camflash: -x, the local symbols out of the LINKEDIT: the flash code took __TEXT over a 16 KiB page and the package over 256 KiB). The camera-only libraries (CoreMedia, CoreVideo, libobjc) are WEAK imports: the bundle uses chained fixups, i.e. every import is bound when
#    the bundle is loaded, and a symbol that is missing in some iOS would otherwise keep the AUDIO plug-in from loading. With weak imports a missing
#    one is NULL and only the camera path (never armed without /dev/redwood-camera) could fault.
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -bundle -no_adhoc_codesign -x -o "$OUT/RedwoodAudio.driver/RedwoodAudio" \
  "$OUT/obj/audio_plugin.o" "${OBJS[@]}" \
  "$HERE/sdk/lib/CoreFoundation.tbd" "$HERE/sdk/lib/libSystem.tbd" \
  -weak_library "$HERE/sdk/lib/CoreMedia.tbd" -weak_library "$HERE/sdk/lib/CoreVideo.tbd" -weak_library "$HERE/sdk/lib/libobjc.A.tbd"
cp "$HP/d/audio_plugin.plist" "$OUT/RedwoodAudio.driver/Info.plist"

# 4. package (N2AP: binary + Info.plist, <= 256 KiB)
rm -f "$OUT/audio.pkg"
python3 "$PKG" build "$OUT/RedwoodAudio.driver" "$OUT/audio.pkg"
python3 "$PKG" verify "$OUT/audio.pkg"
