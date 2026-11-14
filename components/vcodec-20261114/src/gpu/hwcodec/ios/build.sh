#!/bin/bash
# SPDX-License-Identifier: MIT
#
# build.sh [OUT] -- arm64-apple-ios16.0 builds of the phone-test tools of gpu/hwcodec/ios (opus-ios).  OUT default
# /tmp/n2hw-opus-ios/out; nothing is written elsewhere except IV's archive under OUT/iv (IV's own Makefile, `make ios-lib`).
#
#   n2vcd_selftest   links IV's libn2vhost.a with -force_load: the archive's n2vhost.o carries WEAK DEFINITIONS of the shim
#                    tables (host/n2vhost.c:48-51), so a plain archive link never extracts n2shim_thread.o / n2shim_stdio.o
#                    and the vendor set comes up with 77 unresolved imports (verified on the host, see README.md).
#   n2vcd_probe      frozen headers only, libSystem only.
#   vcodecd          IP's daemon + P03's client/hardware back end, -force_load of the same archive, linked against the real SDK
#                    libSystem.tbd (build_ios.sh's hand-kept symbol list goes stale: pthread_key_create/strnlen missing on 11 Nov).
# Both: real iPhoneOS16.5 SDK (gpu/build/apple-toolchain/sdks), ld64.lld, ad-hoc signature (-adhoc_codesign), which the SWEET
# AMFI stub accepts (every binary on the root fs is treated as signed/platform: sweet_amfi.c vnode_check_signature).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$W/../.." && pwd)
OUT=${1:-/tmp/n2hw-opus-ios/out}
case $OUT in /tmp/*) ;; *) echo "build.sh: OUT must be under /tmp" >&2; exit 2 ;; esac
SDK=${N2_IOS_SDK:-$ROOT/gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
[ -d "$SDK/usr/include" ] || { echo "build.sh: no SDK at $SDK" >&2; exit 1; }
mkdir -p "$OUT/obj"
CF=(--target=arm64-apple-ios16.0 -isysroot "$SDK" -std=c11 -O2 -Wall -Wextra -Werror
    -I "$W/include" -I "$W/host" -I "$W/tests/hw")
LD=(ld64.lld -arch arm64 -platform_version ios 16.0 16.5 -syslibroot "$SDK" -adhoc_codesign -dead_strip -lSystem)

echo "== IV archive (make ios-lib) =="
nice -n 10 make -C "$W/host" ios-lib OUT="$OUT/iv" JOBS=2 > "$OUT/iv.log" 2>&1 || { tail -5 "$OUT/iv.log"; exit 1; }
tail -1 "$OUT/iv.log"
LIB=$OUT/iv/ios/libn2vhost.a

echo "== n2vcd_selftest =="
clang -c "${CF[@]}" -o "$OUT/obj/n2vcd_selftest.o" "$HERE/n2vcd_selftest.c"
"${LD[@]}" -o "$OUT/n2vcd_selftest" "$OUT/obj/n2vcd_selftest.o" -force_load "$LIB"

echo "== n2vcd_probe =="
clang -c "${CF[@]}" -o "$OUT/obj/n2vcd_probe.o" "$HERE/n2vcd_probe.c"
"${LD[@]}" -o "$OUT/n2vcd_probe" "$OUT/obj/n2vcd_probe.o"

echo "== n2vcd_dprobe (decode probe: DEC05 client + P03 transport + mm21 detile; opus-decint) =="
DP=(); for f in "$HERE/n2vcd_dprobe.c" "$W/client/dec/n2vcd_dec.c" "$W/client/dec/n2vcd_dec_path.c" "$W/client/n2vcd_transport.c" \
		"$W/mm21/n2_mm21.c"; do
	o=$OUT/obj/dp_$(basename "$f" .c).o
	clang -c "${CF[@]}" -I "$W/client" -I "$W/client/dec" -I "$W/mm21" -o "$o" "$f"
	DP+=("$o")
done
"${LD[@]}" -o "$OUT/n2vcd_dprobe" "${DP[@]}"

echo "== vcodecd (IP daemon + decoder service + P03 hardware back end + IV archive, -force_load, real SDK libSystem) =="
# opus-decint: N2VCD_WITH_DECODER links daemon/dec (DEC05) + ap/n2ap_vdec.c (DEC01); the service is OFF unless vcodecd is
# started with --decoder, so the encode-only launchd job behaves as before.
VC=(); CL=(); for f in "$W/daemon/vcodecd.c" "$W/daemon/n2vcd_srv.c" "$W/daemon/dec/n2vcd_dec_service.c" \
		"$W/daemon/dec/n2vcd_dec_ap_n2ap.c" "$W/ap/n2ap_vdec.c" "$W"/client/*.c; do
	case $(basename "$f") in n2vcd_cli.c) continue ;; esac
	o=$OUT/obj/vc_$(basename "$f" .c).o
	clang -c --target=arm64-apple-ios16.0 -isysroot "$SDK" -std=c11 -O2 -Wall -Wextra -DN2VCD_WITH_DECODER -I "$W/include" \
		-I "$W/daemon" -I "$W/daemon/dec" -I "$W/host" -I "$W" -I "$W/ap" -I "$W/vcu" -I "$W/client" -I "$W/tests/hw" -o "$o" "$f"
	case $(basename "$f") in vcodecd.c|n2vcd_srv.c|n2vcd_dec_service.c|n2vcd_dec_ap_n2ap.c|n2ap_vdec.c) VC+=("$o") ;; *) CL+=("$o") ;; esac
done
# P03's objects as an archive (as build_ios.sh does): only its hardware back end is pulled; its server helpers would clash
rm -f "$OUT/libvc_client.a"; llvm-ar rcs "$OUT/libvc_client.a" "${CL[@]}"
"${LD[@]}" -o "$OUT/vcodecd" "${VC[@]}" "$OUT/libvc_client.a" -force_load "$LIB"
case $(llvm-nm "$OUT/vcodecd") in *" _n2vc_hw_open_fake"*) echo "build.sh: the fake hardware leaked into vcodecd" >&2; exit 1 ;; esac
case $(strings "$OUT/vcodecd") in *--gate*) ;; *) echo "build.sh: vcodecd has no --gate" >&2; exit 1 ;; esac

case $(strings "$OUT/vcodecd") in *--decoder*) ;; *) echo "build.sh: vcodecd has no --decoder" >&2; exit 1 ;; esac
for b in n2vcd_selftest n2vcd_probe n2vcd_dprobe vcodecd; do
	file "$OUT/$b" | grep -q 'Mach-O 64-bit arm64 executable' || { echo "build.sh: $b is not an arm64 Mach-O" >&2; exit 1; }
	# the vendor set must link completely: the shim tables are in (the archive defect above)
	printf '  %-16s %8s bytes  sha256 %s\n' "$b" "$(stat -c %s "$OUT/$b")" "$(sha256sum "$OUT/$b" | cut -c1-16)"
done
for b in n2vcd_selftest vcodecd; do
	SYMS=$(llvm-nm "$OUT/$b")
	case $SYMS in *" _n2shim_thread"*) ;; *) echo "build.sh: n2shim_thread missing from $b" >&2; exit 1 ;; esac
	case $SYMS in *" _n2shim_stdio"*) ;; *) echo "build.sh: n2shim_stdio missing from $b" >&2; exit 1 ;; esac
done
echo "imports (dylibs):"; llvm-objdump --macho --dylibs-used "$OUT/n2vcd_selftest" | tail -n +2 | sed 's/^/  /'
echo "IOS TOOLS OK $OUT"
