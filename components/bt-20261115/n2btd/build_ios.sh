#!/bin/bash
# SPDX-License-Identifier: MIT
# builds n2btd (piece 0723) for iOS 16 arm64 with the pinned iPhoneOS16.5 SDK, ad-hoc signed by ld64.lld.  -> build/n2btd
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"; mkdir -p build
SDK=$H/../../apple-toolchain/sdks/iPhoneOS16.5.sdk
clang -c --target=arm64-apple-ios16.0 -isysroot "$SDK" -std=gnu11 -O2 -fblocks -Wall -Wextra -Werror -Wno-unused-parameter -o build/n2btd.o n2btd.c
ld64.lld -arch arm64 -platform_version ios 16.0 16.5 -syslibroot "$SDK" -adhoc_codesign -o build/n2btd build/n2btd.o -lSystem -F "$SDK/System/Library/Frameworks" -framework CoreFoundation
file build/n2btd | grep -q 'Mach-O 64-bit arm64 executable'
llvm-nm -u build/n2btd
S=$(sha256sum build/n2btd | cut -d' ' -f1); ls -l build/n2btd; echo "N2BTD BUILD OK $S"
