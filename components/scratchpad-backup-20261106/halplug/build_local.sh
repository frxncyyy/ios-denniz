#!/bin/bash
# Local (Linux) build of the Nord2 HAL plug-in: clang --target arm64-apple-ios16.0 + ld64.lld against TBD stubs.
# No iPhoneOS SDK: sdk/include holds the few public CoreFoundation/CoreAudioTypes/libc declarations it uses.
# Usage: build_local.sh <source dir with audio_plugin.c, redwood_audio.h, audio_plugin.plist> <out dir>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); SRC=$(cd "$1" && pwd); OUT=$2
RES=$(clang -print-resource-dir)
mkdir -p "$OUT/RedwoodAudio.driver"
clang -c --target=arm64-apple-ios16.0 -O1 -ffreestanding -nostdinc -isystem "$RES/include" -isystem "$HERE/sdk/include" \
  -I "$SRC" -D__unused='__attribute__((unused))' -fno-common -Wall -Wno-four-char-constants -Wno-multichar \
  -o "$OUT/audio_plugin.o" "$SRC/audio_plugin.c"
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -bundle -no_adhoc_codesign -o "$OUT/RedwoodAudio.driver/RedwoodAudio" \
  "$OUT/audio_plugin.o" "$HERE/sdk/lib/CoreFoundation.tbd" "$HERE/sdk/lib/libSystem.tbd"
cp "$SRC/audio_plugin.plist" "$OUT/RedwoodAudio.driver/Info.plist"
