#!/bin/bash
# SPDX-License-Identifier: MIT
#
# build_plugins.sh [--base] [OUT] -- the phone's Slim.videoencoder with the hardware encoders in it (lead).
#
#   Slim.videoencoder = gpu/video's plug-in exactly as gpu/video/install/build_plugin.sh builds it (shim, log, n2avc, n2vk)
#                       + P04's vt/{annexb,n2hwenc_vt,n2hwenc_client,n2hwenc_register}.c + P03's client/*.c (as an archive,
#                       only the referenced members are pulled), handed to gpu/video/build_ios.sh by N2VT_EXTRA_OBJS/_TBDS.
#                       The shim calls n2hwenc_register_all() only when /private/var/db/nord2-video-claim-hw exists.
#   Slim.videodecoder   only if vt/dec/*.c defines SLIMRegister (skipped with a message otherwise).
#   --base              today's plug-in without the extra objects (gpu/video/install/build_plugin.sh as is).
#   N2HW_PLUGIN_EXTRA_SRC  (opus-jpeg) optional space-separated .c files compiled with the same flags and added to the plug-in
#                       (e.g. gpu/hwcodec/heicm/n2vt_trace.c); N2HW_PLUGIN_EXTRA_SYMS: their symbols.txt files (same format).
#
# OUT default /tmp/n2hw-opus-lead/plugins (/tmp/n2hw-opus-lead/plugins-base with --base); must resolve under /tmp.
# Prints and checks: Mach-O arm64 dylib, install name, exports, imports covered, P04/P03 defined, ad-hoc signature.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)            # gpu/hwcodec/ios
HW=$(cd "$HERE/.." && pwd)                     # gpu/hwcodec
VIDEO=$(cd "$HW/../video" && pwd)              # gpu/video (read only)
BASE=0
if [ "${1:-}" = "--base" ]; then BASE=1; shift; fi
if [ $BASE -eq 1 ]; then OUT=${1:-/tmp/n2hw-opus-lead/plugins-base}; else OUT=${1:-/tmp/n2hw-opus-lead/plugins}; fi
OUT=$(realpath -m "$OUT")
case $OUT in /tmp/?*) ;; *) echo "build_plugins.sh: OUT must resolve under /tmp (got $OUT)" >&2; exit 2 ;; esac
NM=${LLVM_NM:-llvm-nm}
OBJDUMP=${LLVM_OBJDUMP:-llvm-objdump}
ENC_ID=/System/Library/VideoEncoders/Slim.videoencoder
DEC_ID=/System/Library/VideoDecoders/Slim.videodecoder
WORK=$OUT/work
rm -rf "$WORK" "$OUT/encoder"
mkdir -p "$WORK/obj" "$WORK/tbd"

RES=$(clang -print-resource-dir)
# gpu/hwcodec/build_ios.sh's flags for the plug-in objects (gpu/video's freestanding sysinc, hidden visibility)
CFLAGS=(--target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc
        -isystem "$RES/include" -isystem "$VIDEO/sdk/sysinc" -I "$VIDEO/include" -I "$HW/include" -I "$HW/daemon"
        -I "$HW/jpeg" -I "$HW/host" -I "$HW/ap" -I "$HW/vcu" -I "$HW/client" -I "$HW/vt" -Wall -Wextra -Werror)
cc() { nice -n 10 clang -c "${CFLAGS[@]}" -o "$2" "$1"; }

EXTRA_OBJS=()
EXTRA_TBDS=()
if [ $BASE -eq 0 ]; then
	echo "== P04 hardware encoder objects =="
	for s in annexb n2hwenc_vt n2hwenc_client n2hwenc_register; do
		cc "$HW/vt/$s.c" "$WORK/obj/p04_$s.o"
		EXTRA_OBJS+=("$WORK/obj/p04_$s.o")
	done
	for f in ${N2HW_PLUGIN_EXTRA_SRC:-}; do
		echo "== extra plug-in source $f (N2HW_PLUGIN_EXTRA_SRC) =="
		cc "$f" "$WORK/obj/x_$(basename "$f" .c).o"
		EXTRA_OBJS+=("$WORK/obj/x_$(basename "$f" .c).o")
	done
	echo "== P03 client archive =="
	CL=()
	for f in "$HW"/client/*.c; do
		case $(basename "$f") in n2vcd_cli.c) continue ;; esac      # the command-line tool has a main
		o=$WORK/obj/p03_$(basename "$f" .c).o
		cc "$f" "$o"
		CL+=("$o")
	done
	[ ${#CL[@]} -gt 0 ] || { echo "build_plugins.sh: no client/*.c (P03)" >&2; exit 1; }
	llvm-ar rcs "$WORK/libn2vcd_client.a" "${CL[@]}"
	EXTRA_OBJS+=("$WORK/libn2vcd_client.a")

	echo "== supplemental TBDs (names from vt/symbols.txt + daemon/sdk_symbols.txt the extra objects import and sdk/lib lacks) =="
	N2X_SYMS="${N2HW_PLUGIN_EXTRA_SYMS:-}" python3 -I - "$VIDEO/sdk" "$HW/vt/symbols.txt" "$HW/daemon/sdk_symbols.txt" "$WORK/tbd" "${EXTRA_OBJS[@]}" <<'PY'
import glob, importlib.util, os, subprocess, sys
sdk, p04, libsys, out = sys.argv[1:5]
extra_syms = os.environ.get('N2X_SYMS', '').split()
used = set(subprocess.run(['llvm-nm', '-u', '-j'] + sys.argv[5:], capture_output=True, text=True, check=True).stdout.split())
spec = importlib.util.spec_from_file_location('make_tbds', os.path.join(sdk, 'make_tbds.py'))
mt = importlib.util.module_from_spec(spec); spec.loader.exec_module(mt)
have = set()
for tbd in glob.glob(os.path.join(sdk, 'lib', '*.tbd')):
    have.update(l[8:].strip() for l in open(tbd) if l.startswith('      - '))
want = {}
for f in [p04] + extra_syms:
  for line in open(f):
    line = line.split('#')[0].strip()
    if line:
        owner, sym = [x.strip() for x in line.split('\t')]
        want.setdefault(mt.fold_owner(owner), set()).add(sym)
for line in open(libsys):
    line = line.split('#')[0].strip()
    if line:
        want.setdefault(mt.LIBSYSTEM, set()).add(line)
for owner, syms in sorted(want.items()):
    new = (syms & used) - have
    if not new:
        continue
    path = os.path.join(out, mt.INFO[owner][0] + '_n2hw.tbd')
    open(path, 'w').write(mt.tbd_text(owner, new))
    print('   %-28s %s' % (os.path.basename(path), ' '.join(sorted(new))))
PY
	for t in "$WORK"/tbd/*.tbd; do [ -f "$t" ] && EXTRA_TBDS+=("$t"); done
fi

echo "== Slim.videoencoder (gpu/video/install/build_plugin.sh -> build_ios.sh) =="
N2VT_EXTRA_OBJS="${EXTRA_OBJS[*]:-}" N2VT_EXTRA_TBDS="${EXTRA_TBDS[*]:-}" \
	nice -n 10 bash "$VIDEO/install/build_plugin.sh" "$OUT/encoder" > "$WORK/encoder.log" 2>&1 || {
	tail -30 "$WORK/encoder.log"; cat "$OUT/encoder/build.log" 2>/dev/null | grep -v warning | tail -30; exit 1; }
cp "$OUT/encoder/build.log" "$OUT/build-encoder.log"
P=$OUT/Slim.videoencoder
cp "$OUT/encoder/Slim.videoencoder" "$P"

# ---- checks ---------------------------------------------------------------------------------------------------------
fail() { echo "build_plugins.sh: $*" >&2; exit 1; }
check_dylib() {   # check_dylib <binary> <install name> <expected exports...>
	local b=$1 id=$2; shift 2
	file "$b" | grep -q 'Mach-O 64-bit arm64 dynamically linked shared library' || fail "$b is not a Mach-O arm64 dylib"
	"$OBJDUMP" --macho --dylib-id "$b" | grep -qx "$id" || fail "$b install name is not $id"
	[ "$("$NM" -gU "$b" | awk '{print $NF}' | sort)" = "$(printf '%s\n' "$@" | sort)" ] || fail "$b exports: $("$NM" -gU "$b")"
	"$OBJDUMP" --macho --private-headers "$b" | grep -q LC_CODE_SIGNATURE || fail "$b is not signed"
	echo "   $(basename "$b"): Mach-O arm64 dylib, id $id, exports: $*, LC_CODE_SIGNATURE (ad-hoc)"
}
check_dylib "$P" "$ENC_ID" _SLIMEncoderRegister _n2vt_version
grep -E 'undefined symbols|absent' "$OUT/build-encoder.log" | sed 's/^ */   /'
SYMS=$("$NM" "$P")
if [ $BASE -eq 0 ]; then
	for s in _n2hwenc_register_all _n2vcd_connect_path _n2vcd_set_timeout _n2hwavc_vt_create _n2hwhevc_vt_create _n2vt_say; do
		printf '%s\n' "$SYMS" | grep -Eq " [tT] $s\$" || fail "$s is not defined in $P"
	done
	echo "   defined: _n2hwenc_register_all _n2vcd_connect_path _n2vcd_set_timeout _n2hwavc_vt_create _n2hwhevc_vt_create"
	printf '%s\n' "$SYMS" | grep -Eq ' [tT] _n2vc_hw_open_(dev|fake)$' && fail "P03's hardware back end leaked into $P"
	printf '%s\n' "$SYMS" | grep -Eq ' U _n2hwenc_register_all$' && fail "_n2hwenc_register_all is still an import"
else
	printf '%s\n' "$SYMS" | grep -Eq ' [tT] _n2hwenc_' && fail "--base build contains P04"
fi
echo "   imports by image:"; "$OBJDUMP" --macho --dylibs-used "$P" | tail -n +2 | sed 's/^/     /'

# ---- optional decoder (DEC06) ---------------------------------------------------------------------------------------
PRODUCTS=(Slim.videoencoder)
DEC_SRC=()
for f in "$HW"/vt/dec/*.c; do [ -f "$f" ] && DEC_SRC+=("$f"); done
if [ $BASE -eq 0 ] && [ ${#DEC_SRC[@]} -gt 0 ] && grep -Eq 'void[[:space:]]+SLIMRegister[[:space:]]*\(void\)' "${DEC_SRC[@]}"; then
	# opus-cam (12 Nov 2026): the old pattern (^SLIMRegister) never matched DEC06's one-line definition, so no decoder
	# product was ever built.  The product now also links DEC05's decode client (client/dec, the path the vc22 dprobe ran
	# bit-exact on the phone; DEC06's weak n2vtd_client_dec05 adapter becomes the default seam) and opus-mm21's staged
	# MM21 converter, plus supplementary TBDs for the decoder-side VideoToolbox/CoreMedia imports (as DEC06's suite).
	echo "== Slim.videodecoder (vt/dec/*.c defines SLIMRegister) =="
	DO=()
	DCF=(-I "$HW/vt/dec" -I "$HW/client/dec" -I "$HW/mm21")
	for f in "${DEC_SRC[@]}" "$HW"/client/dec/n2vcd_dec.c "$HW"/client/dec/n2vcd_dec_path.c "$HW"/mm21/n2_mm21.c \
	         "$VIDEO"/vt/n2vt_log.c; do   # n2vt_log.c: the N2VIDEO console channel (n2vt_say)
		[ -f "$f" ] || continue
		o=$WORK/obj/dec_$(basename "$f" .c).o
		nice -n 10 clang -c "${CFLAGS[@]}" "${DCF[@]}" -o "$o" "$f"
		DO+=("$o")
	done
	tbd() {   # tbd <file> <install name> <symbols...>
		local f=$1 id=$2; shift 2
		{ echo '--- !tapi-tbd'; echo 'tbd-version:      4'; echo 'targets:          [ arm64-ios ]'
		  echo "install-name:     '$id'"; echo 'current-version:  1'; echo 'compatibility-version: 1'; echo 'exports:'
		  echo '  - targets:      [ arm64-ios ]'; echo '    symbols:'; for x in "$@"; do echo "      - $x"; done; echo '...'; } > "$f"
	}
	tbd "$WORK/tbd/vt_dec.tbd" /System/Library/Frameworks/VideoToolbox.framework/VideoToolbox \
		_VTDecoderSessionCreatePixelBuffer _VTDecoderSessionEmitDecodedFrame _VTDecoderSessionSetPixelBufferAttributes \
		_VTRegisterVideoDecoderWithInfo _VTVideoDecoderGetClassID _kVTVideoDecoderInfo_Rating \
		_kVTVideoDecoder_IsHardwareAccelerated _VTSelectAndCreateVideoDecoderInstance
	tbd "$WORK/tbd/cm_dec.tbd" /System/Library/Frameworks/CoreMedia.framework/CoreMedia \
		_CMBlockBufferCopyDataBytes _CMFormatDescriptionGetExtension _CMSampleBufferGetPresentationTimeStamp \
		_CMVideoFormatDescriptionGetH264ParameterSetAtIndex _CMVideoFormatDescriptionGetHEVCParameterSetAtIndex \
		_kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms _CMBaseObjectGetVTable
	tbd "$WORK/tbd/cf_dec.tbd" /System/Library/Frameworks/CoreFoundation.framework/CoreFoundation \
		_CFDataGetBytePtr _CFDataGetTypeID _CFDataGetLength
	# opus-robust (12 Nov 2026): the decoder's software-fallback thread guard (pthread_self/pthread_equal are not in
	# gpu/video's trimmed libSystem.tbd).  Same install name as libSystem: classified STRONG below.
	tbd "$WORK/tbd/libsys_dec.tbd" /usr/lib/libSystem.B.dylib _pthread_self _pthread_equal
	STRONG=(); WEAK=()
	for t in "$OUT"/encoder/tree/sdk/lib/*.tbd "${EXTRA_TBDS[@]}" "$WORK/tbd/vt_dec.tbd" "$WORK/tbd/cm_dec.tbd" "$WORK/tbd/cf_dec.tbd" \
	         "$WORK/tbd/libsys_dec.tbd"; do
		case $(grep -m1 '^install-name:' "$t") in
			*/usr/lib/libSystem.B.dylib*|*/CoreFoundation.framework/CoreFoundation*) STRONG+=("$t") ;;
			*) WEAK+=(-weak_library "$t") ;;
		esac
	done
	ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -dylib -install_name "$DEC_ID" -adhoc_codesign \
		-exported_symbol _SLIMRegister -dead_strip_dylibs -o "$OUT/Slim.videodecoder" "${DO[@]}" \
		"$WORK/libn2vcd_client.a" "${STRONG[@]}" "${WEAK[@]}"
	check_dylib "$OUT/Slim.videodecoder" "$DEC_ID" _SLIMRegister
	PRODUCTS+=(Slim.videodecoder)
else
	echo "== Slim.videodecoder: skipped (no vt/dec/*.c defines SLIMRegister yet: DEC06 not landed) =="
	rm -f "$OUT/Slim.videodecoder"
fi

# ---- products -------------------------------------------------------------------------------------------------------
rm -rf "$OUT/encoder/tree" "$OUT/encoder/products/obj" "$WORK/obj"     # the intermediates (copied sdk, objects)
( cd "$OUT" && sha256sum "${PRODUCTS[@]}" > SHA256SUMS && cat SHA256SUMS && ls -l "${PRODUCTS[@]}" | awk '{print "   " $5 " bytes  " $NF}' )
echo "PLUGINS OK $OUT"
