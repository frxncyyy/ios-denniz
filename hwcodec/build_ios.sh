#!/bin/bash
# SPDX-License-Identifier: MIT
#
# build_ios.sh [outdir] -- cross-builds the user-space products of gpu/hwcodec for arm64-apple-ios16.0.
#
#   outdir default /tmp/n2hw-ip/ios.  Nothing is written outside it.
#
# Products:
#   rwjpeg_tool                       executable (J02's hardware-JPEG command-line tool, links with libSystem only)
#   vcodecd                           executable linked from P03's hardware back end + IV's runtime objects
#   vcodecd.o, n2vcd_srv.o            objects: the daemon itself
#   rwjpeg.o, n2jpeg_hw_vt.o          plug-in objects (gpu/video links them into Slim.videoencoder)
#   annexb.o, n2hwenc_{vt,client,register}.o  P04's VideoToolbox hardware-encoder plug-in objects
#   n2vcd_cli                         P03's client tool, linked when client/*.c is present
#
# Toolchain and SDK stubs are gpu/video's, read only: gpu/video/sdk/sysinc headers and gpu/video/sdk/lib/*.tbd.
# daemon/sdk_symbols.txt supplies the libSystem symbols the stubs lack; the script turns it into a supplemental TBD
# under outdir.  hw-lead has the list to fold into gpu/video/sdk/symbols.txt.
#
# IV/V41 interface: IV's arm64 libn2vhost.a is rebuilt from current sources when the iPhoneOS SDK is present
# (N2VHOST_IOS_LIB overrides).  Its runtime objects are extracted and linked directly, excluding the test-only
# fake hardware/VENC members; the V32/V33 shim-table objects are then really present (checked with llvm-nm).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)                 # gpu/hwcodec
VIDEO=$(cd "$HERE/../video" && pwd)                 # gpu/video (read only)
OUT=${1:-/tmp/n2hw-ip/ios}
OUT_REAL=$(realpath -m "$OUT")
case $OUT_REAL in
	/tmp/?*) ;;
	*) echo "build_ios.sh: outdir must resolve under /tmp (got $OUT -> $OUT_REAL)" >&2; exit 2 ;;
esac
OUT=$OUT_REAL
SDK=$VIDEO/sdk
SYSINC=$SDK/sysinc
RES=$(clang -print-resource-dir)
LLD=${LD64:-ld64.lld}
NM=${LLVM_NM:-llvm-nm}
OBJDUMP=${LLVM_OBJDUMP:-llvm-objdump}
mkdir -p "$OUT/obj" "$OUT/tbd"

[ -d "$SYSINC" ] || { echo "build_ios.sh: $SYSINC is missing" >&2; exit 1; }
[ -e "$SDK/lib/libSystem.tbd" ] || { echo "build_ios.sh: $SDK/lib/libSystem.tbd is missing" >&2; exit 1; }

INCS=(-isystem "$SYSINC" -I "$VIDEO/include" -I "$HERE/include" -I "$HERE/daemon" -I "$HERE/jpeg")
[ -d "$HERE/host" ] && INCS+=(-I "$HERE/host")
[ -d "$HERE/ap" ] && INCS+=(-I "$HERE/ap")
[ -d "$HERE/vcu" ] && INCS+=(-I "$HERE/vcu")
[ -d "$HERE/client" ] && INCS+=(-I "$HERE/client")
[ -d "$HERE/vt" ] && INCS+=(-I "$HERE/vt")
CFLAGS=(-std=c11 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc
        -isystem "$RES/include" "${INCS[@]}" -Wall -Wextra -Werror)

compile() {   # compile <source> <object>
	local src=$1 obj=$2
	clang -c --target=arm64-apple-ios16.0 "${CFLAGS[@]}" -o "$obj" "$src"
}

echo "== supplemental libSystem TBD (from daemon/sdk_symbols.txt) =="
{
	echo "--- !tapi-tbd"
	echo "tbd-version:      4"
	echo "targets:          [ arm64-ios ]"
	echo "install-name:     '/usr/lib/libSystem.B.dylib'"
	echo "current-version:  1336"
	echo "compatibility-version: 1"
	echo "exports:"
	echo "  - targets:      [ arm64-ios ]"
	echo "    symbols:"
	sed -e 's/#.*//' -e '/^[[:space:]]*$/d' -e 's/^[[:space:]]*/      - /' "$HERE/daemon/sdk_symbols.txt"
	echo "..."
} > "$OUT/tbd/libsystem_n2hw.tbd"

TBD_LINK=("$SDK/lib/libSystem.tbd" "$OUT/tbd/libsystem_n2hw.tbd")
# Framework stubs as weak libraries: nothing here needs CoreFoundation, and a weak import stays NULL instead of failing.
for tbd in "$SDK"/lib/*.tbd; do
	case $(basename "$tbd") in
		libSystem.tbd) ;;
		*) TBD_LINK+=(-weak_library "$tbd") ;;
	esac
done

BINARIES=()
OBJECTS=()
DAEMON_OBJS=()
PLUGIN_OBJS=()

echo "== compiling the daemon =="
compile "$HERE/daemon/vcodecd.c" "$OUT/obj/vcodecd.o"
compile "$HERE/daemon/n2vcd_srv.c" "$OUT/obj/n2vcd_srv.o"
OBJECTS+=("$OUT/obj/vcodecd.o" "$OUT/obj/n2vcd_srv.o")
DAEMON_OBJS+=("$OUT/obj/vcodecd.o" "$OUT/obj/n2vcd_srv.o")

echo "== compiling the hardware-JPEG plug-in objects =="
if [ -f "$HERE/jpeg/rwjpeg.c" ]; then
	compile "$HERE/jpeg/rwjpeg.c" "$OUT/obj/rwjpeg.o"
	OBJECTS+=("$OUT/obj/rwjpeg.o")
	PLUGIN_OBJS+=("$OUT/obj/rwjpeg.o")
fi
if [ -f "$HERE/jpeg/n2jpeg_hw_vt.c" ]; then
	compile "$HERE/jpeg/n2jpeg_hw_vt.c" "$OUT/obj/n2jpeg_hw_vt.o"
	OBJECTS+=("$OUT/obj/n2jpeg_hw_vt.o")
	PLUGIN_OBJS+=("$OUT/obj/n2jpeg_hw_vt.o")
fi

echo "== compiling the VideoToolbox hardware encoder objects (P04) =="
for src in annexb.c n2hwenc_vt.c n2hwenc_client.c n2hwenc_register.c; do
	if [ -f "$HERE/vt/$src" ]; then
		compile "$HERE/vt/$src" "$OUT/obj/${src%.c}.o"
		OBJECTS+=("$OUT/obj/${src%.c}.o")
		PLUGIN_OBJS+=("$OUT/obj/${src%.c}.o")
	else
		echo "   vt/$src missing: skipped"
	fi
done

echo "== linking rwjpeg_tool =="
if [ -f "$HERE/jpeg/rwjpeg_tool.c" ] && [ -f "$HERE/jpeg/rwjpeg.c" ]; then
	compile "$HERE/jpeg/rwjpeg_tool.c" "$OUT/obj/rwjpeg_tool.o"
	"$LLD" -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -dead_strip_dylibs \
		-o "$OUT/rwjpeg_tool" "$OUT/obj/rwjpeg_tool.o" "$OUT/obj/rwjpeg.o" "${TBD_LINK[@]}"
	file "$OUT/rwjpeg_tool" | grep -q 'Mach-O 64-bit arm64 executable'
	BINARIES+=("$OUT/rwjpeg_tool")
else
	echo "   jpeg/rwjpeg_tool.c or jpeg/rwjpeg.c missing: skipped"
fi

echo "== compiling the client objects (P03) =="
CLI_OBJ=()
CLIENT_OBJS=()
CLIENT_LIB=""
shopt -s nullglob
for src in "$HERE"/client/*.c; do
	base=$(basename "$src" .c)
	compile "$src" "$OUT/obj/client_$base.o"
	CLI_OBJ+=("$OUT/obj/client_$base.o")
	# The daemon links these as an archive, so only the members it references (the hardware
	# back end) are pulled; P03's own command-line or server files cannot duplicate vcodecd.
	if [ "$base" != "n2vcd_cli" ]; then CLIENT_OBJS+=("$OUT/obj/client_$base.o"); fi
done
shopt -u nullglob
if [ ${#CLIENT_OBJS[@]} -gt 0 ]; then
	CLIENT_LIB=$OUT/libn2vcd_client.a
	rm -f "$CLIENT_LIB"
	llvm-ar rcs "$CLIENT_LIB" "${CLIENT_OBJS[@]}"
fi
if [ -f "$HERE/client/n2vcd_cli.c" ]; then
	"$LLD" -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -dead_strip_dylibs \
		-o "$OUT/n2vcd_cli" "${CLI_OBJ[@]}" "${TBD_LINK[@]}"
	file "$OUT/n2vcd_cli" | grep -q 'Mach-O 64-bit arm64 executable'
	BINARIES+=("$OUT/n2vcd_cli")
elif [ ${#CLIENT_OBJS[@]} -gt 0 ]; then
	echo "   client/n2vcd_cli.c missing: P03 objects compiled but no client tool yet"
else
	echo "   client/ is empty: skipped (P03 has not landed)"
fi

echo "== linking vcodecd =="
VHOST_LIB=${N2VHOST_IOS_LIB:-$HERE/host/build-ios/libn2vhost.a}
if [ -z "${N2VHOST_IOS_LIB:-}" ]; then
	IV_SDK=${N2_IOS_SDK:-$HERE/../../gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
	if [ -d "$IV_SDK/usr/include" ]; then
		# Always rebuild from the current IV sources: a stale archive silently hides newly referenced symbols.
		echo "== building IV's arm64 archive (make ios-lib, SDK $(basename "$IV_SDK")) =="
		if nice -n 10 make -C "$HERE/host" ios-lib OUT="$OUT/../iv-ios" JOBS=2 >"$OUT/iv-ios.log" 2>&1 &&
		   [ -f "$OUT/../iv-ios/ios/libn2vhost.a" ]; then
			VHOST_LIB=$OUT/../iv-ios/ios/libn2vhost.a
			tail -2 "$OUT/iv-ios.log"
		else
			echo "   IV archive build failed (dependency; see $OUT/iv-ios.log); vcodecd stays object-only" >&2
			tail -3 "$OUT/iv-ios.log" >&2
			VHOST_LIB=""
		fi
	elif [ -f "$OUT/../iv-ios/ios/libn2vhost.a" ]; then
		VHOST_LIB=$OUT/../iv-ios/ios/libn2vhost.a      # no SDK here: use a prebuilt archive
	fi
fi
VHOST_OBJS=()
if [ -n "$VHOST_LIB" ] && [ -f "$VHOST_LIB" ]; then
	# A plain static-archive link does not extract the shim members: n2vhost.o references them through weak
	# empty stand-ins.  Extract IV's runtime objects and link them directly, excluding the test-only fake
	# hardware member so it cannot enter the phone binary.
	rm -rf "$OUT/vhost-obj"
	mkdir -p "$OUT/vhost-obj"
	if ! (cd "$OUT/vhost-obj" && llvm-ar x "$VHOST_LIB"); then
		echo "build_ios.sh: cannot extract $VHOST_LIB" >&2
		exit 1
	fi
	while IFS= read -r obj; do
		case $(basename "$obj") in
			*fake*) ;;
			*) VHOST_OBJS+=("$obj") ;;
		esac
	done < <(find "$OUT/vhost-obj" -name '*.o' | sort)
	if [ ${#VHOST_OBJS[@]} -eq 0 ]; then
		echo "build_ios.sh: $VHOST_LIB has no runtime objects" >&2
		exit 1
	fi
else
	VHOST_LIB=""
fi
HAVE_P03=0
VCODEC_STATE="object only (host set archive missing)"
if [ -n "$CLIENT_LIB" ] && [ -f "$CLIENT_LIB" ]; then
	CLIENT_NM=$(llvm-nm -g --defined-only "$CLIENT_LIB" 2>/dev/null) || CLIENT_NM=""
	case "$CLIENT_NM" in
		*" _n2vc_hw_open_dev"*) HAVE_P03=1 ;;
	esac
fi
if [ -f "$VHOST_LIB" ] && [ "$HAVE_P03" -eq 1 ]; then
	"$LLD" -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -dead_strip_dylibs \
		-o "$OUT/vcodecd" "${DAEMON_OBJS[@]}" "$CLIENT_LIB" "${VHOST_OBJS[@]}" "${TBD_LINK[@]}"
	file "$OUT/vcodecd" | grep -q 'Mach-O 64-bit arm64 executable'
	VCODEC_NM=$("$NM" "$OUT/vcodecd") || { echo "build_ios.sh: llvm-nm vcodecd failed" >&2; exit 1; }
	case "$VCODEC_NM" in
		*" _n2shim_thread"*) ;;
		*) echo "build_ios.sh: vcodecd is missing the V32 thread shim (archive member not linked)" >&2; exit 1 ;;
	esac
	case "$VCODEC_NM" in
		*" _n2shim_stdio"*) ;;
		*) echo "build_ios.sh: vcodecd is missing the V33 stdio shim (archive member not linked)" >&2; exit 1 ;;
	esac
	case "$VCODEC_NM" in
		*n2vc_hw_open_fake*|*n2venc_fake*)
			echo "build_ios.sh: test fake hardware/fake VENC leaked into vcodecd" >&2; exit 1 ;;
	esac
	BINARIES+=("$OUT/vcodecd")
	VCODEC_STATE="linked"
elif [ -f "$VHOST_LIB" ]; then
	echo "   vcodecd: IV archive present; product link needs P03's n2vc_hw_open_dev (client/** is absent)."
	echo "   verifying every other symbol with a throwaway -ENODEV stub (the result is NOT a product and is deleted):"
	compile "$HERE/daemon/test/p03_absent.c" "$OUT/obj/p03_absent.o"
	if "$LLD" -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -dead_strip_dylibs \
		-o "$OUT/vcodecd.linkcheck" "${DAEMON_OBJS[@]}" "$OUT/obj/p03_absent.o" "${VHOST_OBJS[@]}" "${TBD_LINK[@]}"; then
		echo "   link check OK: the only missing product symbol was _n2vc_hw_open_dev"
		rm -f "$OUT/vcodecd.linkcheck"
		VCODEC_STATE="object only (P03 n2vc_hw_open_dev missing; link check OK)"
	else
		echo "build_ios.sh: vcodecd link check failed for a reason other than P03" >&2
		exit 1
	fi
else
	echo "   vcodecd: objects built; executable waits for the hosted set ($VHOST_LIB missing)."
	echo "   Build IV's arm64 archive (needs the iPhoneOS SDK):"
	echo "     make -C gpu/hwcodec/host ios-lib OUT=/tmp/n2hw-ip/iv-ios"
	echo "     N2VHOST_IOS_LIB=/tmp/n2hw-ip/iv-ios/ios/libn2vhost.a bash gpu/hwcodec/build_ios.sh"
fi

# ---- mechanical promises ---------------------------------------------------------------------------------------
for obj in "${OBJECTS[@]}"; do
	[ -s "$obj" ] || { echo "build_ios.sh: $obj is empty" >&2; exit 1; }
done
for bin in "${BINARIES[@]}"; do
	# every undefined symbol must be in one of the TBDs
	python3 - "$bin" "$OUT/tbd/libsystem_n2hw.tbd" "$SDK/lib/libSystem.tbd" <<'PY'
import re, subprocess, sys
binary, extra, libsystem = sys.argv[1], sys.argv[2], sys.argv[3]
covered = set()
for path in (extra, libsystem):
    for line in open(path):
        m = re.match(r'\s*-\s+(\S+)\s*$', line)
        if m:
            covered.add(m.group(1))
out = subprocess.run(['llvm-nm', '-u', binary], capture_output=True, text=True, check=True).stdout
missing = sorted(line.strip() for line in out.splitlines() if line.strip() and line.strip() not in covered)
if missing:
    sys.stderr.write('build_ios.sh: %s imports symbols no TBD covers:\n  %s\n' % (binary, '\n  '.join(missing)))
    sys.stderr.write('add them to gpu/hwcodec/daemon/sdk_symbols.txt (and tell hw-lead)\n')
    sys.exit(1)
PY
done

echo "== sizes =="
for f in "${OBJECTS[@]}" "${BINARIES[@]}"; do
	printf '   %8s  %s\n' "$(stat -c %s "$f")" "${f#"$OUT"/}"
done

echo "IOS BUILD OK"
if [ ${#BINARIES[@]} -gt 0 ]; then
	echo "binaries:"
	for bin in "${BINARIES[@]}"; do
		echo "  ${bin#"$OUT"/} ($(stat -c %s "$bin") bytes, $(sha256sum "$bin" | cut -c1-12))"
	done
fi
if [ ${#OBJECTS[@]} -gt 0 ]; then
	echo "objects:"
	for obj in "${OBJECTS[@]}"; do
		echo "  ${obj#"$OUT"/} ($(stat -c %s "$obj") bytes)"
	done
fi
echo "vcodecd: $VCODEC_STATE"
