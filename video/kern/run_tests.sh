#!/bin/bash
# Host tests of WP08's reference and dispatch (kern/).  Builds with clang's address and
# undefined-behaviour sanitisers, then again with the thread sanitiser for the threaded
# dispatch test.  Products under gpu/video/build/WP08/ unless an output directory is given.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
VIDEO=$(cd "$HERE/.." && pwd)
OUT=${1:-$VIDEO/build/WP08}
CC=${CC:-clang}
mkdir -p "$OUT/tmp"

CFLAGS=(-std=c11 -O2 -g -Wall -Wextra -Werror -I "$VIDEO/include")
SRC=("$HERE/n2vk_ref.c" "$HERE/n2vk_dispatch.c" "$HERE/test/t_kern.c")

echo "== WP08 kern: ASan + UBSan =="
"$CC" "${CFLAGS[@]}" -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	"${SRC[@]}" -o "$OUT/t_kern_asan" -lm -pthread
set +e
ASAN_OPTIONS=detect_leaks=1 "$OUT/t_kern_asan" 2000 2>&1 | tee "$OUT/t_kern_asan.log"
ASAN_RC=${PIPESTATUS[0]}
set -e
ASAN_LINE=$(tail -1 "$OUT/t_kern_asan.log")
if [ "$ASAN_RC" -ne 0 ] || [[ "$ASAN_LINE" != *" PASS "* ]]; then
	echo "N2TEST WP08 FAIL ASan/UBSan suite: ${ASAN_LINE:-no output}"
	exit 1
fi
ASAN_PASSED=${ASAN_LINE##* }
ASAN_PASSED=${ASAN_PASSED%/*}
ASAN_TOTAL=${ASAN_LINE##*/}

echo "== WP08 kern: TSan =="
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -I "$VIDEO/include" \
	-fsanitize=thread -fno-omit-frame-pointer "${SRC[@]}" -o "$OUT/t_kern_tsan" -lm -pthread
set +e
N2VK_ONLY_THREADS=1 TSAN_OPTIONS=halt_on_error=1 "$OUT/t_kern_tsan" 1000 2>&1 | tee "$OUT/t_kern_tsan.log"
TSAN_RC=${PIPESTATUS[0]}
set -e
TSAN_LINE=$(tail -1 "$OUT/t_kern_tsan.log")
if [ "$TSAN_RC" -ne 0 ] || [[ "$TSAN_LINE" != *" PASS "* ]]; then
	echo "N2TEST WP08 FAIL TSan suite: ${TSAN_LINE:-no output}"
	exit 1
fi
TSAN_PASSED=${TSAN_LINE##* }
TSAN_PASSED=${TSAN_PASSED%/*}
TSAN_TOTAL=${TSAN_LINE##*/}

TOTAL=$((ASAN_TOTAL + TSAN_TOTAL))
PASSED=$((ASAN_PASSED + TSAN_PASSED))
echo "N2TEST WP08 PASS $PASSED/$TOTAL"
