#!/bin/bash
# builds n2simcache for iOS 16 arm64 (pinned iPhoneOS16.5 SDK, ad-hoc signed by ld64.lld), as bt-20261115/n2btd/build_ios.sh
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"; mkdir -p build
SDK=$H/../../apple-toolchain/sdks/iPhoneOS16.5.sdk
clang -c --target=arm64-apple-ios16.0 -isysroot "$SDK" -std=gnu11 -O2 -Wall -Wextra -Werror -o build/n2simcache.o n2simcache.c
ld64.lld -arch arm64 -platform_version ios 16.0 16.5 -syslibroot "$SDK" -adhoc_codesign -o build/n2simcache build/n2simcache.o -lSystem \
  -framework CoreFoundation
file build/n2simcache | grep -q 'Mach-O 64-bit arm64 executable'
llvm-nm -u build/n2simcache
S=$(sha256sum build/n2simcache | cut -d' ' -f1); ls -l build/n2simcache; echo "N2SIMCACHE BUILD OK $S"
