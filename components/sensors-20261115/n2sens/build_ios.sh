#!/bin/bash
# SPDX-License-Identifier: MIT
# builds n2sens for iOS 16 arm64 with the pinned iPhoneOS16.5 SDK, ad-hoc signed by ld64.lld -> build/n2sens (same recipe as n2btd)
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"; mkdir -p build
SDK=$H/../../apple-toolchain/sdks/iPhoneOS16.5.sdk
clang -c --target=arm64-apple-ios16.0 -isysroot "$SDK" -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -o build/n2sens.o n2sens.c
ld64.lld -arch arm64 -platform_version ios 16.0 16.5 -syslibroot "$SDK" -adhoc_codesign -o build/n2sens build/n2sens.o -lSystem -lobjc -F "$SDK/System/Library/Frameworks" -framework CoreFoundation
file build/n2sens | grep -q 'Mach-O 64-bit arm64 executable'
S=$(sha256sum build/n2sens | cut -d' ' -f1); ls -l build/n2sens; echo "N2SENS BUILD OK $S"
