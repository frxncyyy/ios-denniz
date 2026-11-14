#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# DEC06 host suite: the decoder plug-in object, its parameter-set parsing, the pixel copy and the decode client.
#
#   bash gpu/hwcodec/vt/dec/run_tests.sh
#
# Everything is built and generated under /tmp/n2hw-DEC06 (override with N2HW_DEC06_OUT, still under /tmp).  ffmpeg
# with libx264/libx265 is the independent oracle: it makes the avcC/hvcC records and the length-prefixed samples, and it
# decodes the streams this suite converts back to Annex B.  Three builds: ASan+UBSan at -O1, plain -O2, and TSan for the
# two-thread object test.  The last section compiles the plug-in for arm64-apple-ios16.0 and links the dylib.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)                  # gpu/hwcodec
VIDEO=$(cd "$ROOT/../video" && pwd)              # gpu/video (read only)
OUT=${N2HW_DEC06_OUT:-/tmp/n2hw-DEC06/test}
case $OUT in
	/tmp/*) ;;
	*) echo "run_tests.sh: output directory must be under /tmp (got $OUT)" >&2; exit 2 ;;
esac

CC=${CC:-clang}
NICE=(nice -n 10)
mkdir -p "$OUT" "$OUT/asan" "$OUT/o2" "$OUT/tsan" "$OUT/ios"
rm -f "$OUT"/vectors/* 2>/dev/null || true

LIB=("$HERE/n2vtd_nal.c" "$HERE/n2vtd_pixels.c" "$HERE/n2vtd_client_proto.c" "$HERE/n2vtd_obj.c" "$HERE/n2vtd_register.c" "$HERE/n2vtd_log.c")
TEST=("$HERE/test/t_support.c" "$HERE/test/t_apple_dec.c" "$HERE/test/t_fakeclient_dec.c")
INC=(-I "$VIDEO/include" -I "$ROOT/include" -I "$HERE" -I "$HERE/test")
# opus-mm21's libyuv-verified converter: the object uses its staged fetch path and t_pixels uses it as the differential
# oracle.  If the package is not in the tree, the object falls back to n2vtd_pixels.c and the differential is skipped.
EXTRA_SRC=()
if [ -f "$ROOT/mm21/n2_mm21.c" ] && [ -f "$ROOT/mm21/n2_mm21.h" ]; then
	INC+=(-I "$ROOT/mm21")
	EXTRA_SRC+=("$ROOT/mm21/n2_mm21.c")
fi
TOTAL=0

need() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "run_tests.sh: $1 is required (the independent oracle)" >&2
		exit 1
	fi
}
need ffmpeg
need ffprobe
need python3
need "$CC"
need llvm-nm

fail() {
	echo "N2TEST DEC06 FAIL $*"
	exit 1
}

# add_pass <log>: add the T_* PASS count printed by a test binary.
add_pass() {
	local log=$1 n
	n=$(awk '/^T_[A-Z0-9]+ PASS [0-9]+$/{print $3}' "$log" | tail -n 1)
	[ -n "${n:-}" ] || { cat "$log"; fail "$(basename "$log") has no PASS line"; }
	TOTAL=$((TOTAL + n))
}

build_run() {   # build_run <dir> <san> <name> <sources...> -- <args...>
	local dir=$1 san=$2 name=$3
	shift 3
	local srcs=()
	while [ "$1" != "--" ]; do srcs+=("$1"); shift; done
	shift
	if ! "${NICE[@]}" "$CC" -std=c11 -g -Wall -Wextra -Werror -DN2_HOST_FAKE "${INC[@]}" $san \
	     -o "$OUT/$dir/$name" "${LIB[@]}" "${TEST[@]}" "${EXTRA_SRC[@]}" "${srcs[@]}" >"$OUT/$dir/$name.build.log" 2>&1; then
		cat "$OUT/$dir/$name.build.log"
		fail "build $dir/$name"
	fi
	if ! "$OUT/$dir/$name" "$@" >"$OUT/$dir/$name.log" 2>&1; then
		cat "$OUT/$dir/$name.log"
		fail "run $dir/$name"
	fi
	cat "$OUT/$dir/$name.log"
	add_pass "$OUT/$dir/$name.log"
}

echo "== DEC06: ffmpeg vectors (the independent oracle) =="
mkdir -p "$OUT/vectors"
python3 "$HERE/test/mkvectors.py" "$OUT/vectors" > "$OUT/vectors.json"
python3 - "$OUT/vectors.json" > "$OUT/vectors.txt" <<'PY'
import json, sys
for line in open(sys.argv[1]):
    d = json.loads(line)
    depth = 10 if "10" in (d.get("pix_fmt") or "") else 8
    print(d["name"], d["width"], d["height"], d["full_range"], depth, d["samples"])
PY
cat "$OUT/vectors.txt"

run_vectors() {   # run_vectors <dir> <san>
	local dir=$1 san=$2 name w h full depth samples out
	while read -r name w h full depth samples; do
		out="$OUT/$dir/$name.annexb"
		build_run "$dir" "$san" "t_nal_$name" "$HERE/test/t_nal.c" -- \
			"$OUT/vectors/$name.vec" "$w" "$h" "$full" "$out" "$depth"
		# ffmpeg must decode the converted stream: same frame count, same size.
		local ext=h264
		case $name in hevc*) ext=h265 ;; esac
		local got
		got=$(ffprobe -v error -count_frames -select_streams v:0 \
			-show_entries stream=nb_read_frames,width,height -of csv=p=0 "$out" 2>/dev/null || true)
		[ "$got" = "$w,$h,$samples" ] || fail "$name ffmpeg decode: got '$got', want '$w,$h,$samples'"
		TOTAL=$((TOTAL + 1))
	done < "$OUT/vectors.txt"
}

echo "== DEC06: ASan+UBSan (-O1) =="
SAN=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
run_vectors asan "${SAN[*]}"
build_run asan "${SAN[*]}" t_pixels "$HERE/test/t_pixels.c" --
build_run asan "${SAN[*]}" t_client "$HERE/test/t_client.c" --
build_run asan "${SAN[*]}" t_obj "$HERE/test/t_obj.c" --
build_run asan "${SAN[*]}" t_fallback "$HERE/test/t_fallback.c" --

echo "== DEC06: plain -O2 =="
SAN=(-O2)
run_vectors o2 "${SAN[*]}"
build_run o2 "${SAN[*]}" t_pixels "$HERE/test/t_pixels.c" --
build_run o2 "${SAN[*]}" t_client "$HERE/test/t_client.c" --
build_run o2 "${SAN[*]}" t_obj "$HERE/test/t_obj.c" --
build_run o2 "${SAN[*]}" t_fallback "$HERE/test/t_fallback.c" --

echo "== DEC06: TSan (two sessions on two threads) =="
build_run tsan "-O1 -fsanitize=thread" t_obj "$HERE/test/t_obj.c" --
build_run tsan "-O1 -fsanitize=thread" t_fallback "$HERE/test/t_fallback.c" --


echo "== DEC06: DEC05 client adapter (staged rev-2 header) =="
if [ -f "$ROOT/client/dec/n2vcd_dec.h" ] && [ -f "$ROOT/daemon/dec/n2vcd_proto_dec_rev2.h.txt" ]; then
	STAGE="$OUT/dec05"
	mkdir -p "$STAGE"
	if python3 "$ROOT/daemon/dec/test/stage_proto.py" "$ROOT/include/n2vcd_proto.h" \
		"$ROOT/daemon/dec/n2vcd_proto_dec_rev2.h.txt" "$STAGE/n2vcd_proto.h" >"$STAGE/stage.log" 2>&1; then
		cat "$STAGE/stage.log"
	else
		cat "$STAGE/stage.log"
		fail "stage DEC05 rev-2 header"
	fi
	# Compile DEC05's client and DEC06's adapter against the same staged header; the adapter is the only file that
	# includes n2vcd_dec.h, so a signature drift fails here rather than in the product link.
	if ! "${NICE[@]}" "$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -I "$STAGE" -I "$ROOT/include" \
		-I "$ROOT/client/dec" -c "$ROOT/client/dec/n2vcd_dec.c" -o "$STAGE/n2vcd_dec.o" \
		>"$STAGE/client.log" 2>&1; then
		cat "$STAGE/client.log"
		fail "compile DEC05 client"
	fi
	TOTAL=$((TOTAL + 1))
	if ! "${NICE[@]}" "$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -I "$STAGE" -I "$ROOT/include" \
		-I "$ROOT/client/dec" -I "$HERE" -c "$HERE/n2vtd_client_dec05.c" -o "$STAGE/adapter.o" \
		>"$STAGE/adapter.log" 2>&1; then
		cat "$STAGE/adapter.log"
		fail "compile DEC06 DEC05 adapter"
	fi
	llvm-nm "$STAGE/adapter.o" | grep -q ' T n2vtd_client_dec05' || fail "adapter has no n2vtd_client_dec05"
	TOTAL=$((TOTAL + 1))
	# The adapter may only call n2vcd_dec_* functions DEC05's client defines.
	MISSING=$(llvm-nm --undefined-only "$STAGE/n2vcd_dec.o" "$STAGE/adapter.o" | awk '{print $1}' | sort -u | \
		comm -23 - <(llvm-nm "$STAGE/n2vcd_dec.o" "$STAGE/adapter.o" | awk '$2=="T"{print $3}' | sort -u) | \
		grep -c '^n2vcd_' || true)
	[ "$MISSING" = "0" ] || fail "adapter calls $MISSING n2vcd_* functions DEC05 does not define"
	TOTAL=$((TOTAL + 1))
	echo "DEC06 DEC05 adapter: client and adapter compile against the rev-2 header; every n2vcd_* call resolves"
else
	echo "DEC06 DEC05 adapter: skipped (client/dec/n2vcd_dec.h absent)"
fi

echo "== DEC06: arm64-apple-ios16.0 plug-in dylib =="
if [ -d "$VIDEO/sdk/sysinc" ] && [ -f "$VIDEO/sdk/sysinc/string.h" ]; then
	RES=$(clang -print-resource-dir)
	IOS_OBJS=()
	IOS_EXTRA=(-I "$HERE")
	if [ -f "$ROOT/mm21/n2_mm21.h" ]; then IOS_EXTRA+=(-I "$ROOT/mm21"); fi
	for src in n2vtd_nal.c n2vtd_pixels.c n2vtd_client_proto.c n2vtd_client_dec05.c n2vtd_obj.c n2vtd_register.c n2vtd_log.c; do
		obj="$OUT/ios/${src%.c}.o"
		if ! "${NICE[@]}" "$CC" -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden \
			-ffreestanding -nostdinc -isystem "$RES/include" -isystem "$VIDEO/sdk/sysinc" -I "$VIDEO/include" \
			-I "$ROOT/include" "${IOS_EXTRA[@]}" -Wall -Wextra -Werror -o "$obj" "$HERE/$src" \
			>"$OUT/ios/${src%.c}.log" 2>&1; then
			cat "$OUT/ios/${src%.c}.log"
			fail "ios compile $src"
		fi
		IOS_OBJS+=("$obj")
	done
	if [ -f "$ROOT/mm21/n2_mm21.c" ]; then
		if ! "${NICE[@]}" "$CC" -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden \
			-ffreestanding -nostdinc -isystem "$RES/include" -isystem "$VIDEO/sdk/sysinc" \
			-I "$ROOT/mm21" -Wall -Wextra -Werror -o "$OUT/ios/n2_mm21.o" "$ROOT/mm21/n2_mm21.c" \
			>"$OUT/ios/n2_mm21.log" 2>&1; then
			cat "$OUT/ios/n2_mm21.log"
			fail "ios compile n2_mm21.c"
		fi
		IOS_OBJS+=("$OUT/ios/n2_mm21.o")
	fi
	# The SDK TBDs were trimmed for the encoder packages; the decoder's CoreMedia/VideoToolbox imports go in a
	# supplementary TBD (the build_ios.sh pattern), so the link proves the complete import list.
	mkdir -p "$OUT/ios/tbd"
	{
		echo '--- !tapi-tbd'
		echo 'tbd-version:      4'
		echo 'targets:          [ arm64-ios ]'
		echo "install-name:     '/System/Library/Frameworks/VideoToolbox.framework/VideoToolbox'"
		echo 'current-version:  1'
		echo 'compatibility-version: 1'
		echo 'exports:'
		echo '  - targets:      [ arm64-ios ]'
		echo '    symbols:'
		for s in _VTDecoderSessionCreatePixelBuffer _VTDecoderSessionEmitDecodedFrame \
		         _VTDecoderSessionSetPixelBufferAttributes _VTRegisterVideoDecoderWithInfo \
		         _VTVideoDecoderGetClassID _kVTVideoDecoderInfo_Rating _kVTVideoDecoder_IsHardwareAccelerated \
		         _VTSelectAndCreateVideoDecoderInstance; do
			echo "      - $s"
		done
		echo '...'
	} > "$OUT/ios/tbd/vt_dec.tbd"
	{
		echo '--- !tapi-tbd'
		echo 'tbd-version:      4'
		echo 'targets:          [ arm64-ios ]'
		echo "install-name:     '/System/Library/Frameworks/CoreMedia.framework/CoreMedia'"
		echo 'current-version:  1'
		echo 'compatibility-version: 1'
		echo 'exports:'
		echo '  - targets:      [ arm64-ios ]'
		echo '    symbols:'
		for s in _CFDataGetBytePtr _CFDataGetTypeID _CMBlockBufferCopyDataBytes \
		         _CMFormatDescriptionGetExtension _CMSampleBufferGetPresentationTimeStamp \
		         _CMVideoFormatDescriptionGetH264ParameterSetAtIndex \
		         _CMVideoFormatDescriptionGetHEVCParameterSetAtIndex \
		         _kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms _CMBaseObjectGetVTable; do
			echo "      - $s"
		done
		echo '...'
	} > "$OUT/ios/tbd/cm_dec.tbd"
	# opus-robust: the software fallback's thread guard (gpu/video's trimmed libSystem.tbd lacks these two).
	{
		echo '--- !tapi-tbd'
		echo 'tbd-version:      4'
		echo 'targets:          [ arm64-ios ]'
		echo "install-name:     '/usr/lib/libSystem.B.dylib'"
		echo 'current-version:  1'
		echo 'compatibility-version: 1'
		echo 'exports:'
		echo '  - targets:      [ arm64-ios ]'
		echo '    symbols:'
		for s in _pthread_self _pthread_equal; do echo "      - $s"; done
		echo '...'
	} > "$OUT/ios/tbd/libsys_dec.tbd"
	# Link with dynamic lookup (the plug-in is loaded by VideoToolbox, and P03's client library is statically linked
	# into the product dylib later), then AUDIT the imports: every undefined symbol must be one the SDK/supplementary
	# TBDs cover or one of the two P03 transport entry points.
	if ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -dylib \
		-install_name /System/Library/VideoDecoders/Slim.videodecoder \
		-undefined dynamic_lookup \
		-o "$OUT/ios/Slim.videodecoder" "${IOS_OBJS[@]}" \
		"$OUT/ios/tbd/vt_dec.tbd" "$OUT/ios/tbd/cm_dec.tbd" "$OUT/ios/tbd/libsys_dec.tbd" \
		"$VIDEO/sdk/lib/CoreFoundation.tbd" "$VIDEO/sdk/lib/CoreMedia.tbd" \
		"$VIDEO/sdk/lib/CoreVideo.tbd" "$VIDEO/sdk/lib/VideoToolbox.tbd" \
		"$VIDEO/sdk/lib/libSystem.tbd" >"$OUT/ios/link.log" 2>&1; then
		file "$OUT/ios/Slim.videodecoder"
		file "$OUT/ios/Slim.videodecoder" | grep -q 'Mach-O 64-bit arm64 dynamically linked shared library' ||
			fail "ios dylib is not a Mach-O arm64 dylib"
		TOTAL=$((TOTAL + 1))
		# VideoToolbox dlsym()s this exact name from the file.
		llvm-nm -g "$OUT/ios/Slim.videodecoder" | grep -q ' T _SLIMRegister' ||
			fail "ios dylib does not export SLIMRegister"
		TOTAL=$((TOTAL + 1))
		python3 - "$OUT/ios/Slim.videodecoder" "$VIDEO/sdk/lib" "$OUT/ios/tbd" <<'PY' || fail "ios import audit"
import glob, re, subprocess, sys
binary, sdk, tbd = sys.argv[1], sys.argv[2], sys.argv[3]
covered = set()
for t in glob.glob(sdk + "/*.tbd") + glob.glob(tbd + "/*.tbd"):
    for line in open(t):
        m = re.match(r"\s*-\s+(\S+)\s*$", line)
        if m:
            covered.add(m.group(1))
# Weak by design: P03's client library provides the first two when the product dylib links it; DEC05's adapter is a
# weak function the object uses only when the build has it; n2vt_say is gpu/video/vt/n2vt_log.c (the console channel),
# also weak; dyld_stub_binder is emitted by the linker itself.
p03 = {"_n2vcd_transport_relay", "_n2vcd_transport_socket", "_n2vtd_client_dec05", "_n2vt_say", "dyld_stub_binder"}
und = [l.strip() for l in subprocess.run(["llvm-nm", "--undefined-only", binary],
                                         capture_output=True, text=True, check=True).stdout.splitlines() if l.strip()]
missing = sorted(u for u in und if u not in covered and u not in p03)
if missing:
    print("undefined symbols neither TBD-covered nor P03:", missing)
    sys.exit(1)
print("DEC06 ios imports: %d undefined, all covered (%d TBD, %d weak/linker)" % (len(und), len(und) - len(p03), len(p03)))
PY
	else
		cat "$OUT/ios/link.log"
		fail "ios dylib link"
	fi
	echo "DEC06 ios compile: ${#IOS_OBJS[@]} objects for arm64-apple-ios16.0 (7 plug-in + n2_mm21 when present)"
else
	echo "DEC06 ios compile: skipped ($VIDEO/sdk/sysinc is absent)"
fi

echo "DEC06 checks: $TOTAL"
echo "N2TEST DEC06 PASS $TOTAL/$TOTAL"
