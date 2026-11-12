#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V40 extra check: compile the device model for arm64 iOS 16.5 with the project's SDK.
# Compile only (the daemon links it later).  Kept out of run_tests.sh so the acceptance test does
# not depend on the SDK being present.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
hc=$(cd "$here/../.." && pwd)          # gpu/hwcodec
root=$(cd "$hc/../.." && pwd)          # work tree
sdk=${N2_IOS_SDK:-$root/gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
out=${N2HW_TMP:-/tmp/n2hw-V40}
mkdir -p "$out"
if [ ! -d "$sdk" ]; then
	echo "N2TEST V40-IOS FAIL no SDK at $sdk (set N2_IOS_SDK)" >&2
	exit 1
fi
nice -n 10 clang -target arm64-apple-ios16.5 -isysroot "$sdk" -std=c11 -O2 \
	-Wall -Wextra -Werror -I"$hc/include" -I"$hc/vcu" \
	-c "$hc/vcu/n2vcu.c" -o "$out/n2vcu.ios.o"
echo "N2TEST V40-IOS PASS 1/1 (arm64-apple-ios16.5, compile only)"
