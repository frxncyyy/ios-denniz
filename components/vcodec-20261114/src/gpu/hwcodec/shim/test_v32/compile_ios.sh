#!/bin/bash
# V32 extra check: cross-compile the shim for arm64 iOS 16.5 with the project's SDK (compile only, no link into
# the daemon yet).  Separate from run_tests.sh so the acceptance run does not depend on the SDK.
# The Darwin-specific paths taken here: no gettid() (pthread_self instead), pthread_setname_np(name) for self,
# and no pthread_mutex_timedlock()/pthread_rwlock_timed*lock() (a trylock poll in n2s_*_timedlock()).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$H/.." && pwd)
W=$(cd "$S/.." && pwd)
ROOT=$(cd "$W/../.." && pwd)
SDK=${N2_IOS_SDK:-$ROOT/gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk}
OUT=/tmp/n2hw-v32
mkdir -p "$OUT"
if [ ! -d "$SDK" ]; then
	echo "N2TEST V32-IOS FAIL no SDK at $SDK (set N2_IOS_SDK)"
	exit 1
fi
nice -n 10 clang -target arm64-apple-ios16.5 -isysroot "$SDK" -std=c11 -O2 -Wall -Wextra -Werror \
	-I"$W/include" -I"$S" -c "$S/n2shim_thread.c" -o "$OUT/n2shim_thread.ios.o"
echo "N2TEST V32-IOS PASS 1/1 (arm64-apple-ios16.5, compile only)"
