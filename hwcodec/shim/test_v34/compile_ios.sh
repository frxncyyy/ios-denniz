#!/bin/bash
# V34 extra check: cross-compile the two shim sources for arm64 iOS 16.5 with the project's SDK.
# Compile only (V34 is host-side code that the daemon will link; no iOS runtime test exists yet).
# Separate from run_tests.sh so the acceptance test does not depend on the SDK being present.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$H/.." && pwd)
W=$(cd "$S/.." && pwd)
ROOT=$(cd "$W/../.." && pwd)
SDK=${N2_IOS_SDK:-$ROOT/gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
OUT=/tmp/n2hw-v34
mkdir -p "$OUT"
if [ ! -d "$SDK" ]; then
	echo "N2TEST V34-IOS FAIL no SDK at $SDK (set N2_IOS_SDK)"
	exit 1
fi
n=0
for f in n2shim_android n2shim_props; do
	nice -n 10 clang -target arm64-apple-ios16.5 -isysroot "$SDK" -std=c11 -O2 \
		-Wall -Wextra -Werror -I"$W/include" -I"$S" -c "$S/$f.c" -o "$OUT/$f.ios.o"
	n=$((n + 1))
done
echo "N2TEST V34-IOS PASS $n/2 (arm64-apple-ios16.5, compile only)"
