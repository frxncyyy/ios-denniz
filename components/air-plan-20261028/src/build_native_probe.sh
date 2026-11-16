#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: build_native_probe.sh OUTPUT_DIRECTORY}
mkdir -p "$out"
# Minimal linker import description for public libSystem ABI, not Apple code.
cat > "$out/libSystem.tbd" <<'TBD'
--- !tapi-tbd-v3
archs: [ arm64 ]
platform: ios
install-name: /usr/lib/libSystem.B.dylib
exports:
  - archs: [ arm64 ]
    symbols: [ _printf, _memset, _puts, _dlopen, _dlsym, _dlerror, _write, _strlen, _memcpy, _memset_pattern16, _malloc, _free, __NSConcreteStackBlock ]
...
TBD
extra=()
objects=()
if [[ ${2:-} == --bundle-loader ]]; then
    extra=(-DTEGU_BUNDLE_LOADER)
elif [[ ${2:-} == --system-render ]]; then
    python3 - "${3:?system-render requires authored AIR fixture}" "$out/tegu-render-air.h" <<'PYDATA'
import sys
from pathlib import Path
b=Path(sys.argv[1]).read_bytes()
if not 0<len(b)<=1024*1024:raise SystemExit('Invalid render AIR size')
Path(sys.argv[2]).write_text('static const unsigned char tegu_render_air[]={'+','.join(map(str,b))+'};\n')
PYDATA
    extra=(-DTEGU_SYSTEM_DEVICE -DTEGU_RENDER_PROBE -I"$out")
    positional=0
    has_varying=0
    has_blend=0
    has_bgra=0
    has_iosurface=0
    has_texture=0
    has_extended_texture=0
    arg=4
    while (( arg <= $# )); do
        token=${!arg}
        case "$token" in
            --texture)
                next=$((arg+1))
                (( next <= $# )) || { echo '--texture requires an authored AIR fixture' >&2; exit 1; }
                python3 - "${!next}" "$out/tegu-texture-air.h" <<'PYTEXTURE'
import sys
from pathlib import Path
b=Path(sys.argv[1]).read_bytes()
if not 0<len(b)<=1024*1024:raise SystemExit('Invalid texture AIR size')
Path(sys.argv[2]).write_text('static const unsigned char tegu_texture_air[]={'+','.join(map(str,b))+'};\n')
PYTEXTURE
                extra+=(-DTEGU_TEXTURE_RENDER_PROBE)
                has_texture=1
                arg=$((arg+2))
                ;;
            --extended-textures)
                [[ $has_extended_texture == 0 ]] || { echo '--extended-textures may appear once' >&2; exit 1; }
                extra+=(-DTEGU_EXTENDED_TEXTURE_RENDER_PROBE)
                has_extended_texture=1
                arg=$((arg+1))
                ;;
            --blend)
                [[ $has_varying == 1 && $has_blend == 0 ]] || { echo '--blend requires a varying AIR fixture and may appear once' >&2; exit 1; }
                extra+=(-DTEGU_BLEND_RENDER_PROBE)
                has_blend=1
                arg=$((arg+1))
                ;;
            --bgra)
                [[ $has_bgra == 0 ]] || { echo '--bgra may appear once' >&2; exit 1; }
                extra+=(-DTEGU_BGRA_BLEND_RENDER_PROBE)
                has_bgra=1
                arg=$((arg+1))
                ;;
            --iosurface)
                [[ $has_iosurface == 0 ]] || { echo '--iosurface may appear once' >&2; exit 1; }
                extra+=(-DTEGU_IOSURFACE_RENDER_PROBE)
                has_iosurface=1
                arg=$((arg+1))
                ;;
            *)
                case "$positional" in
                    0)
                        python3 - "$token" "$out/tegu-vertex-air.h" <<'PYVERTEX'
import sys
from pathlib import Path
b=Path(sys.argv[1]).read_bytes()
if not 0<len(b)<=1024*1024:raise SystemExit('Invalid vertex AIR size')
Path(sys.argv[2]).write_text('static const unsigned char tegu_vertex_air[]={'+','.join(map(str,b))+'};\n')
PYVERTEX
                        extra+=(-DTEGU_VERTEX_RENDER_PROBE)
                        ;;
                    1)
                        python3 - "$token" "$out/tegu-varying-air.h" <<'PYVARYING'
import sys
from pathlib import Path
b=Path(sys.argv[1]).read_bytes()
if not 0<len(b)<=1024*1024:raise SystemExit('Invalid varying AIR size')
Path(sys.argv[2]).write_text('static const unsigned char tegu_varying_air[]={'+','.join(map(str,b))+'};\n')
PYVARYING
                        extra+=(-DTEGU_VARYING_RENDER_PROBE -I"$source_dir/../../../shim/tegu")
                        has_varying=1
                        ;;
                    *) echo "unexpected system-render argument: $token" >&2; exit 1 ;;
                esac
                positional=$((positional+1))
                arg=$((arg+1))
                ;;
        esac
    done
    [[ $has_extended_texture == 0 || $has_texture == 1 ]] || { echo '--extended-textures requires --texture AIR' >&2; exit 1; }
    [[ $has_bgra == 0 || $has_blend == 1 ]] || { echo '--bgra requires --blend' >&2; exit 1; }
    [[ $has_iosurface == 0 || $has_blend == 1 ]] || { echo '--iosurface requires --blend' >&2; exit 1; }
elif [[ ${2:-} == --system-device ]]; then
    extra=(-DTEGU_SYSTEM_DEVICE)
elif [[ $# -ge 2 ]]; then
    python3 - "$2" "$out/tegu-pipeline-fixture.h" <<'PY'
import sys
from pathlib import Path
p=Path(sys.argv[1]).read_bytes()
if not 64 < len(p) <= 4160:
    raise SystemExit("Invalid pipeline fixture size")
Path(sys.argv[2]).write_text("static const unsigned char tegu_pipeline_fixture[]={"+",".join(str(b) for b in p)+"};\n")
PY
    extra=(-DTEGU_PIPELINE_FIXTURE -I"$out")
    clang -fblocks --target=arm64-apple-ios16.0 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-objc-arc "${extra[@]}" -c "$source_dir/compute_frontend.m" -o "$out/compute_frontend.o"
    cat > "$out/Foundation.tbd" <<'TBD'
--- !tapi-tbd-v3
archs: [ arm64 ]
platform: ios
install-name: /System/Library/Frameworks/Foundation.framework/Foundation
exports:
  - archs: [ arm64 ]
    symbols: [ ___CFConstantStringClassReference ]
    objc-classes: [ NSObject, NSError ]
...
TBD
    cat > "$out/objc.tbd" <<'TBD'
--- !tapi-tbd-v3
archs: [ arm64 ]
platform: ios
install-name: /usr/lib/libobjc.A.dylib
exports:
  - archs: [ arm64 ]
    symbols: [ _objc_msgSend, _objc_msgSendSuper2, __objc_empty_cache, _objc_retain, _objc_release, _objc_autorelease, _objc_alloc, _objc_alloc_init ]
...
TBD
    objects=("$out/compute_frontend.o" "$out/Foundation.tbd" "$out/objc.tbd")
fi
clang -fblocks --target=arm64-apple-ios16.0 "${extra[@]}" -O2 -Wall -Wextra -Werror -fno-stack-protector \
    -c "$source_dir/native_probe.c" -o "$out/native_probe.o"
clang -fblocks --target=arm64-apple-ios16.0 -O2 -Wall -Wextra -Werror -fno-stack-protector \
    -c "$source_dir/transport_probe.c" -o "$out/transport_probe.o"
ld64.lld -arch arm64 -platform_version ios 16.0 16.0 -adhoc_codesign \
    -e _main "$out/native_probe.o" "$out/transport_probe.o" "$out/libSystem.tbd" "${objects[@]}" -o "$out/tegu-metal-probe"
echo "Built $out/tegu-metal-probe (not device-tested)"
