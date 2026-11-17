#!/bin/bash
# Private CI proposal. SOURCE is pinned upstream userland/audio (three files).
set -euo pipefail
[[ $# == 2 ]] || { echo 'usage: build-plugin.sh SOURCE NEW_OUTPUT_DIR' >&2; exit 2; }
source_dir=$(cd "$1" && pwd)
mkdir "$2"
out_dir=$(cd "$2" && pwd)
# Keep source/plist unchanged. SDK support headers are public iPhoneOS headers.
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -O1 \
  -bundle -framework CoreFoundation -Wl,-no_uuid \
  -o "$out_dir/RedwoodAudio" "$source_dir/audio_plugin.c"
cp "$source_dir/audio_plugin.plist" "$out_dir/Info.plist"
plutil -lint "$out_dir/Info.plist"
xcrun lipo -verify_arch arm64 "$out_dir/RedwoodAudio"
xcrun vtool -show-build "$out_dir/RedwoodAudio" > "$out_dir/platform.txt"
grep -q 'platform IOS' "$out_dir/platform.txt"
xcrun nm -gU "$out_dir/RedwoodAudio" > "$out_dir/exports.txt"
grep -Eq ' _RedwoodAudio_Create$' "$out_dir/exports.txt"
xcrun otool -hv "$out_dir/RedwoodAudio" > "$out_dir/header.txt"
grep -q BUNDLE "$out_dir/header.txt"
xcrun otool -L "$out_dir/RedwoodAudio" > "$out_dir/dependencies.txt"
python3 - "$source_dir" "$out_dir" <<'PY'
import hashlib,json,pathlib,sys
src,out=map(pathlib.Path,sys.argv[1:])
names=['audio_plugin.c','audio_plugin.plist','redwood_audio.h']
manifest={'sources':{n:hashlib.sha256((src/n).read_bytes()).hexdigest() for n in names},'outputs':{n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in ['RedwoodAudio','Info.plist']}}
(out/'provenance.json').write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
PY
