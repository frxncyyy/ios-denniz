#!/bin/bash
# Regenerate 0280-call-audio.patch against stage + 0278 + 0279 from src/ (full files) and the sweet_touch.c edits below.
# Usage: make_0280.sh [audiofix dir] [slider dir]
set -euo pipefail
CA=$(cd "$(dirname "$0")" && pwd)
S=$(dirname "$CA")
AF=${1:-$S/audiofix}; SL=${2:-$S/slider}
W=$NORD2_WORK
ST=$W/gpu/build/xnu-stage-backup-pre-p15-20261028/xnu-stage
T=$(mktemp -d "$CA/.mk0280.XXXXXX"); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/a/bsd/dev" "$T/a/iokit/Kernel/arm/hid"
cp "$ST"/bsd/dev/* "$T/a/bsd/dev/" 2>/dev/null || true
cp "$ST/iokit/Kernel/arm/hid/SweetHID.cpp" "$T/a/iokit/Kernel/arm/hid/"
( cd "$T/a" && patch -p1 -s -i "$AF/0278-audio-native-publish-sweet-touch.patch" && patch -p1 -s -i "$SL"/0279-*.patch )
cp -r "$T/a" "$T/b"
cp "$CA"/src/audio_call.c "$CA"/src/audio_call.h "$CA"/src/audio_device.c "$CA"/src/audio_live.h "$CA"/src/audio_stream.h \
   "$CA"/src/audio_hw_native.inc "$T/b/bsd/dev/"
python3 - "$T/b/bsd/dev/sweet_touch.c" <<'EOF'
import sys
p=sys.argv[1]; s=open(p).read()
def rep(o,n):
    global s
    assert s.count(o)==1,(o[:60],s.count(o)); s=s.replace(o,n)
rep('#include "audio_native.h"\n','#include "audio_native.h"\n#include "audio_call.h"\n')
rep('        nord2_gpu_owner_poll(); /* serialized GPU PMIC work */\n',
    '        nord2_gpu_owner_poll(); /* serialized GPU PMIC work */\n        nord2_audio_owner_poll(); /* 0280: serialized MT6359 call-audio work */\n')
rep('\t\t\t\tsweet_hid_key((int)k, state[k]);\n',
    '\t\t\t\tsweet_hid_key((int)k, state[k]);\n\t\t\t\tif (state[k]) { nac_volume_key(k); }   /* 0280: volume keys step the in-call level (no-op outside calls) */\n')
open(p,'w').write(s)
EOF
FL="audio_call.c audio_call.h audio_device.c audio_hw_native.inc audio_live.h audio_stream.h sweet_touch.c"
( cd "$T" && for f in $FL; do diff -uN "a/bsd/dev/$f" "b/bsd/dev/$f" || true; done ) | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$T/new.patch"
mkdir -p "$T/c" && cp -r "$T/a/." "$T/c/" && ( cd "$T/c" && patch -p1 -s -i "$T/new.patch" )
for f in $FL; do cmp "$T/c/bsd/dev/$f" "$T/b/bsd/dev/$f"; done
mv "$T/new.patch" "$CA/0280-call-audio.patch"
rm -rf "$CA/tree"; cp -r "$T/b" "$CA/tree"          # compile tree for checks
sha256sum "$CA/0280-call-audio.patch"
