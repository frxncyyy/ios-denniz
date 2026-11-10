#!/bin/bash
# SPDX-License-Identifier: MIT
#
# build_ios.sh [outdir] -- builds the two products of gpu/video for iOS 16 arm64:
#
#   build/ios/Slim.videoencoder   the VideoToolbox encoder plug-in (a Mach-O dylib at N2VT_ENCODER_PLUGIN_PATH), from
#                                 vt/*.c avc/*.c kern/n2vk_ref.c kern/n2vk_dispatch.c jpeg/*.c -- each only if it exists;
#   build/ios/n2vt_probe          the probe executable, from probe/*.c and (unless PROBE_NO_AV=1) probe/av/*.c, if probe/ exists.
#
# The system libraries come from the hand-written TBD stubs that sdk/make_tbds.py writes out of every package's symbols.txt.
# libSystem and CoreFoundation are linked normally; every other framework weakly (so a missing one gives a NULL weak import,
# not a load failure). The dylib exports exactly _SLIMEncoderRegister and _n2vt_version and every undefined symbol must be
# covered by a TBD -- both are checked here and the build fails if either is wrong.
#
# usage: build_ios.sh [outdir]        (default gpu/video/build/ios)
# env:   PROBE_NO_AV=1  build the probe without probe/av/*.c
#        N2VT_EXTRA_OBJS   opt-in: space-separated objects/archives added to the plug-in link (gpu/hwcodec/ios/build_plugins.sh)
#        N2VT_EXTRA_TBDS   opt-in: space-separated extra TBD stubs for those objects (linked and counted by the import check;
#                          libSystem/CoreFoundation by install name normally, any other image weakly). Unset: unchanged build.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/build/ios}
INCLUDE=$HERE/include
SYSINC=$HERE/sdk/sysinc
OBJ=$OUT/obj
RES=$(clang -print-resource-dir)
LLD=${LD64:-ld64.lld}
NM=${LLVM_NM:-llvm-nm}
OBJDUMP=${LLVM_OBJDUMP:-llvm-objdump}
PLUGIN=$OUT/Slim.videoencoder
PROBE=$OUT/n2vt_probe
mkdir -p "$OUT" "$OBJ"

[ -d "$SYSINC" ] || { echo "build_ios: $SYSINC is missing" >&2; exit 1; }

CFLAGS=(-std=c11 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc
        -isystem "$RES/include" -isystem "$SYSINC" -I "$INCLUDE"
        -I "$HERE" -I "$HERE/vt" -I "$HERE/avc" -I "$HERE/kern" -I "$HERE/gpu" -I "$HERE/probe"
        -Wall -Wextra)

compile() {   # compile <source> <object> [extra compiler flags...]
	local src=$1 obj=$2
	shift 2
	[ -f "$src" ] || return 0
	clang -c --target=arm64-apple-ios16.0 "${CFLAGS[@]}" "$@" -o "$obj" "$src"
}

echo "== packaging stubs =="
python3 "$HERE/sdk/make_tbds.py"
TBD_ALL=("$HERE"/sdk/lib/*.tbd)
[ -e "${TBD_ALL[0]}" ] || { echo "build_ios: no TBD stubs in sdk/lib" >&2; exit 1; }

# libSystem and CoreFoundation normally, the rest as -weak_library (PLAN.md 4.3): a missing framework must leave a NULL weak
# import, not stop the plug-in from loading. -dead_strip_dylibs then drops the libraries nothing in the link references.
TBD_STRONG=()
TBD_WEAK=()
for tbd in "${TBD_ALL[@]}"; do
	case $(basename "$tbd") in
		libSystem.tbd|CoreFoundation.tbd) TBD_STRONG+=("$tbd") ;;
		*) TBD_WEAK+=("$tbd") ;;
	esac
done
TBD_EXTRA=()
for tbd in ${N2VT_EXTRA_TBDS:-}; do
	[ -f "$tbd" ] || { echo "build_ios: N2VT_EXTRA_TBDS: $tbd is missing" >&2; exit 1; }
	TBD_EXTRA+=("$tbd")
	case $(grep -m1 '^install-name:' "$tbd") in
		*/usr/lib/libSystem.B.dylib*|*/CoreFoundation.framework/CoreFoundation*) TBD_STRONG+=("$tbd") ;;
		*) TBD_WEAK+=("$tbd") ;;
	esac
done
TBD_WEAK_ARGS=()
for tbd in "${TBD_WEAK[@]}"; do
	TBD_WEAK_ARGS+=(-weak_library "$tbd")
done

# The H.264 VideoToolbox glue (vt/n2avc_vt.c, WP05) calls the encoder library (avc/*.c, WP03/WP04). Until that library is in
# the tree the glue object cannot link, so it is left out and the shim registers HEVC only -- that is the stage-0 plug-in v0 of
# PLAN.md section 3, not a broken link. Every other vt/*.c is always compiled.
have_avc=0
for src in "$HERE"/avc/*.c; do
	[ -f "$src" ] && have_avc=1
done

PLUGIN_SOURCES=()
for src in "$HERE"/vt/*.c; do
	[ -f "$src" ] || continue
	if [ "$(basename "$src")" = "n2avc_vt.c" ] && [ $have_avc -eq 0 ]; then
		echo "   vt/n2avc_vt.c: left out (no avc/*.c yet; the H.264 glue needs the encoder library)"
		continue
	fi
	PLUGIN_SOURCES+=("$src")
done
for src in "$HERE"/avc/*.c; do [ -f "$src" ] && PLUGIN_SOURCES+=("$src"); done
for src in "$HERE"/jpeg/*.c; do [ -f "$src" ] && PLUGIN_SOURCES+=("$src"); done
# The n2vk kernels exist for the encoder (n2avc calls them through n2vk.h); with no avc/*.c in the link nothing references them,
# and a half-finished kernel file from another package must not block the stage-0 (HEVC-only) C1 payload. They are compiled in
# as soon as avc/*.c is there, which is when they matter.
if [ $have_avc -eq 1 ]; then
	for src in "$HERE"/kern/n2vk_ref.c "$HERE"/kern/n2vk_dispatch.c; do [ -f "$src" ] && PLUGIN_SOURCES+=("$src"); done
else
	echo "   kern/*.c: left out (no avc/*.c yet; nothing calls the kernels)"
fi
[ ${#PLUGIN_SOURCES[@]} -gt 0 ] || { echo "build_ios: no plug-in sources under $HERE" >&2; exit 1; }

echo "== compiling the plug-in (${#PLUGIN_SOURCES[@]} source files) =="
PLUGIN_OBJECTS=()
for src in "${PLUGIN_SOURCES[@]}"; do
	rel=${src#"$HERE"/}
	obj=$OBJ/${rel//\//__}
	obj=${obj%.c}.o
	compile "$src" "$obj"
	PLUGIN_OBJECTS+=("$obj")
done
EXTRA_OBJECTS=()
for obj in ${N2VT_EXTRA_OBJS:-}; do
	[ -f "$obj" ] || { echo "build_ios: N2VT_EXTRA_OBJS: $obj is missing" >&2; exit 1; }
	EXTRA_OBJECTS+=("$obj")
done
[ ${#EXTRA_OBJECTS[@]} -eq 0 ] || echo "   extra objects: ${#EXTRA_OBJECTS[@]} (N2VT_EXTRA_OBJS), extra TBDs: ${#TBD_EXTRA[@]}"

# The two factories of the other packages are weak imports: while their source is not in the link, the linker is told that the
# undefined name is allowed (ld64 -U). As soon as the definition exists it is bound locally, with no -U.
ALLOW=()
if ! "$NM" -g --defined-only "${PLUGIN_OBJECTS[@]}" 2>/dev/null | grep -q ' _n2avc_vt_create$'; then
	ALLOW+=(-U _n2avc_vt_create)
	echo "   n2avc_vt_create: absent (weak import)"
fi
if ! "$NM" -g --defined-only "${PLUGIN_OBJECTS[@]}" 2>/dev/null | grep -q ' _n2jpeg_vt_create$'; then
	ALLOW+=(-U _n2jpeg_vt_create)
	echo "   n2jpeg_vt_create: absent (weak import)"
fi
# gpu/hwcodec's hardware encoders (P04): only in the link through N2VT_EXTRA_OBJS; the shim calls them only with claim-hw.
if ! "$NM" -g --defined-only "${PLUGIN_OBJECTS[@]}" "${EXTRA_OBJECTS[@]}" 2>/dev/null | grep -q ' _n2hwenc_register_all$'; then
	ALLOW+=(-U _n2hwenc_register_all)
	echo "   n2hwenc_register_all: absent (weak import)"
fi
# opus-jpeg (12 Nov 2026): the HEIC-measurement trace proxy (gpu/hwcodec/heicm/n2vt_trace.c), only via N2VT_EXTRA_OBJS.
if ! "$NM" -g --defined-only "${PLUGIN_OBJECTS[@]}" "${EXTRA_OBJECTS[@]}" 2>/dev/null | grep -q ' _n2vt_trace_wrap$'; then
	ALLOW+=(-U _n2vt_trace_wrap)
	echo "   n2vt_trace_wrap: absent (weak import)"
fi

echo "== linking $PLUGIN =="
# The install name is N2VT_ENCODER_PLUGIN_PATH from the frozen include/n2vt_abi.h; it is written out here because ld64 needs the
# literal and the acceptance checks it with llvm-objdump --dylib-id.
"$LLD" -arch arm64 -platform_version ios 16.0 17.0 -dylib \
	-install_name /System/Library/VideoEncoders/Slim.videoencoder -adhoc_codesign \
	-exported_symbol _SLIMEncoderRegister -exported_symbol _n2vt_version -dead_strip_dylibs \
	"${ALLOW[@]}" -o "$PLUGIN" "${PLUGIN_OBJECTS[@]}" "${EXTRA_OBJECTS[@]}" "${TBD_STRONG[@]}" "${TBD_WEAK_ARGS[@]}"

# ---- the plug-in's three mechanical promises -------------------------------------------------------------------------
file "$PLUGIN" | grep -q 'Mach-O 64-bit arm64 dynamically linked shared library' || {
	echo "build_ios: $PLUGIN is not a Mach-O arm64 dylib" >&2; exit 1; }
"$OBJDUMP" --macho --dylib-id "$PLUGIN" | grep -qx '/System/Library/VideoEncoders/Slim.videoencoder' || {
	echo "build_ios: $PLUGIN does not name its install path" >&2; exit 1; }

EXPORTS=$("$NM" -gU "$PLUGIN" | awk '{print $NF}' | sort)
EXPECTED=$(printf '_SLIMEncoderRegister\n_n2vt_version\n' | sort)
if [ "$EXPORTS" != "$EXPECTED" ]; then
	echo "build_ios: exports of $PLUGIN are not exactly the two entries:" >&2
	echo "$EXPORTS" >&2
	exit 1
fi

# every undefined symbol must be in one of the TBDs (or be one of the factories that is allowed to be absent above)
python3 - "$HERE" "$PLUGIN" "${ALLOW[*]:-}" "${TBD_EXTRA[@]}" <<'PY'
import glob, os, re, subprocess, sys
video, binary, allow = sys.argv[1], sys.argv[2], sys.argv[3].split()
covered = set()
for tbd in glob.glob(os.path.join(video, 'sdk', 'lib', '*.tbd')) + sys.argv[4:]:
    for line in open(tbd):
        if line.startswith('      - '):
            covered.add(line[len('      - '):].strip())
covered.update(a for a in allow if a.startswith('_'))
out = subprocess.run(['llvm-nm', '-u', binary], capture_output=True, text=True, check=True).stdout
missing = [line.strip() for line in out.splitlines() if line.strip() and line.strip() not in covered]
if missing:
    sys.stderr.write('build_ios: %s imports symbols no TBD covers:\n  %s\n' % (binary, '\n  '.join(sorted(missing))))
    sys.stderr.write('add each to the symbols.txt of the package that uses it (make_tbds reads them all)\n')
    sys.exit(1)
print('   undefined symbols: %d, all covered by sdk/lib/*.tbd' % len([l for l in out.splitlines() if l.strip()]))
PY

sha256sum "$PLUGIN"
ls -l "$PLUGIN" | awk '{print "   size:", $5, "bytes"}'

# ---- the probe (optional, and later than the plug-in) -----------------------------------------------------------------
PROBE_SOURCES=()
probe_no_av=${PROBE_NO_AV:-0}
if [ "$probe_no_av" != "1" ]; then
	for src in "$HERE"/probe/av/*.c; do [ -f "$src" ] && PROBE_SOURCES+=("$src"); done
fi
if [ ${#PROBE_SOURCES[@]} -eq 0 ] && [ "$probe_no_av" != "1" ]; then
	echo "   probe/av/: no sources yet, the probe is built with -DPROBE_NO_AV"
	probe_no_av=1
fi
PROBE_EXTRA=()
[ "$probe_no_av" = "1" ] && PROBE_EXTRA+=(-DPROBE_NO_AV)
for src in "$HERE"/probe/*.c; do [ -f "$src" ] && PROBE_SOURCES+=("$src"); done
if [ ${#PROBE_SOURCES[@]} -eq 0 ]; then
	echo "== no probe/ sources yet; skipping $PROBE =="
	exit 0
fi

echo "== compiling the probe (${#PROBE_SOURCES[@]} source files) =="
PROBE_OBJECTS=()
for src in "${PROBE_SOURCES[@]}"; do
	rel=${src#"$HERE"/}
	obj=$OBJ/${rel//\//__}
	obj=${obj%.c}.o
	compile "$src" "$obj" "${PROBE_EXTRA[@]}"
	PROBE_OBJECTS+=("$obj")
done
if ! "$NM" -g --defined-only "${PROBE_OBJECTS[@]}" 2>/dev/null | grep -q ' _main$'; then
	echo "build_ios: probe/ has sources but no main" >&2
	exit 1
fi

echo "== linking $PROBE =="
"$LLD" -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -dead_strip_dylibs \
	-o "$PROBE" "${PROBE_OBJECTS[@]}" "${TBD_STRONG[@]}" "${TBD_WEAK_ARGS[@]}"

python3 - "$HERE" "$PROBE" <<'PY'
import glob, os, subprocess, sys
video, binary = sys.argv[1], sys.argv[2]
covered = set()
for tbd in glob.glob(os.path.join(video, 'sdk', 'lib', '*.tbd')):
    for line in open(tbd):
        if line.startswith('      - '):
            covered.add(line[len('      - '):].strip())
out = subprocess.run(['llvm-nm', '-u', binary], capture_output=True, text=True, check=True).stdout
missing = [line.strip() for line in out.splitlines() if line.strip() and line.strip() not in covered]
if missing:
    sys.stderr.write('build_ios: %s imports symbols no TBD covers:\n  %s\n' % (binary, '\n  '.join(sorted(missing))))
    sys.exit(1)
print('   undefined symbols: %d, all covered by sdk/lib/*.tbd' % len([l for l in out.splitlines() if l.strip()]))
PY

file "$PROBE" | grep -q 'Mach-O 64-bit arm64 executable' || {
	echo "build_ios: $PROBE is not a Mach-O arm64 executable" >&2; exit 1; }
sha256sum "$PROBE"
ls -l "$PROBE" | awk '{print "   size:", $5, "bytes"}'
