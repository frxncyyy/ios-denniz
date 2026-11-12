#!/bin/bash
# SPDX-License-Identifier: MIT
# build_ios.sh [OUT] -- arm64-apple-ios16.0 build of n2jpeg_phone (opus-jpeg), the gpu/hwcodec/ios/build.sh pattern:
# real iPhoneOS16.5 SDK, ld64.lld, libSystem only, ad-hoc signature (the SWEET AMFI stub accepts it).  OUT default
# /tmp/n2hw-opus-jpeg/ios (must be under /tmp).  Sources: jpeg/phone/n2jpeg_phone.c (+ J02 rwjpeg_tool.c included),
# jpeg/rwjpeg.c, jdec/n2jdec_parse.c, jdec/n2jdec_words.c.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$HERE/../.." && pwd)                 # gpu/hwcodec
ROOT=$(cd "$W/../.." && pwd)                 # work
OUT=${1:-/tmp/n2hw-opus-jpeg/ios}
case $OUT in /tmp/*) ;; *) echo "build_ios.sh: OUT must be under /tmp" >&2; exit 2 ;; esac
SDK=${N2_IOS_SDK:-$ROOT/gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
[ -d "$SDK/usr/include" ] || { echo "build_ios.sh: no SDK at $SDK" >&2; exit 1; }
mkdir -p "$OUT/obj"
CF=(--target=arm64-apple-ios16.0 -isysroot "$SDK" -std=c11 -O2 -Wall -Wextra -Werror -I "$W/include" -I "$W/jpeg")
O=()
for f in "$HERE/n2jpeg_phone.c" "$W/jpeg/rwjpeg.c" "$W/jdec/n2jdec_parse.c" "$W/jdec/n2jdec_words.c"; do
	o=$OUT/obj/$(basename "$f" .c).o
	nice -n 10 clang -c "${CF[@]}" -o "$o" "$f"
	O+=("$o")
done
ld64.lld -arch arm64 -platform_version ios 16.0 16.5 -syslibroot "$SDK" -adhoc_codesign -dead_strip -lSystem \
	-o "$OUT/n2jpeg_phone" "${O[@]}"
file "$OUT/n2jpeg_phone" | grep -q 'Mach-O 64-bit arm64 executable' || { echo "build_ios.sh: not an arm64 Mach-O" >&2; exit 1; }
case $(llvm-nm "$OUT/n2jpeg_phone") in *" _n2jpgp_test_ioctl"*|*" _fake_ioctl"*) echo "build_ios.sh: test fake leaked" >&2; exit 1 ;; esac
case $(strings "$OUT/n2jpeg_phone") in *--idle-ms*) ;; *) echo "build_ios.sh: option table missing" >&2; exit 1 ;; esac
printf '  n2jpeg_phone %8s bytes  sha256 %s\n' "$(stat -c %s "$OUT/n2jpeg_phone")" "$(sha256sum "$OUT/n2jpeg_phone" | cut -c1-16)"
echo "imports:"; llvm-objdump --macho --dylibs-used "$OUT/n2jpeg_phone" | tail -n +2 | sed 's/^/  /'
echo "IOS JPEG TOOL OK $OUT/n2jpeg_phone"
