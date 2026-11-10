#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run_tests.sh [outdir] -- WP04 host suite.
#
# Builds the three WP04 sources with the WP03 seam as a recording test double
# (avc/test/inter/stub_wp03.c) and the WP08 reference kernel, then runs:
#   1. ASan+UBSan, diamond search only (the production path);
#   2. ASan+UBSan with -DN2AVC_ME_FULL, so n2vk_me16 is the start point;
#   3. TSan with four slice threads on one picture.
# It also compiles the three sources for arm64-apple-ios16.0 with -Werror
# (compile only: no Apple symbol is executed here).
#
# Last line on success: N2TEST WP04 PASS <passed>/<total>
set -euo pipefail
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)
VID=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$VID/build/WP04}
mkdir -p "$OUT/tmp"

CC=${CC:-clang}
COMMON="-std=c11 -O2 -g -Wall -Wextra -I $VID/include -I $VID/avc"
SRCS="$VID/avc/n2avc_me.c $VID/avc/n2avc_inter.c $VID/avc/n2avc_rc.c $HERE/stub_wp03.c $HERE/t_inter.c $VID/kern/n2vk_ref.c $VID/kern/n2vk_dispatch.c"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"

echo "WP04: building ASan+UBSan (diamond) ..."
# shellcheck disable=SC2086
$CC $COMMON $SAN -o "$OUT/t_inter_asan" $SRCS -lpthread -lm

echo "WP04: building ASan+UBSan (n2vk full search) ..."
# shellcheck disable=SC2086
$CC $COMMON $SAN -DN2AVC_ME_FULL -o "$OUT/t_inter_full" $SRCS -lpthread -lm

echo "WP04: building TSan (4 slice threads) ..."
# shellcheck disable=SC2086
$CC $COMMON -fsanitize=thread -o "$OUT/t_inter_tsan" $SRCS -lpthread -lm

total=0
run_one() {
	local bin=$1 name=$2 log=$OUT/${3:-$2}.log
	local line

	if ! "$bin" "$name" >"$log" 2>&1; then
		echo "N2TEST WP04 FAIL $name"
		tail -n 40 "$log"
		exit 1
	fi
	line=$(tail -n 1 "$log")
	case "$line" in
	N2TEST\ WP04\ BIN\ "$name"\ PASS\ */*)
		;;
	*)
		echo "N2TEST WP04 FAIL $name (unexpected last line: $line)"
		exit 1
		;;
	esac
	local frac=${line##* }
	local n=${frac%%/*}
	total=$((total + n))
	echo "  $name: $line"
}

run_one "$OUT/t_inter_asan" asan
run_one "$OUT/t_inter_full" full
run_one "$OUT/t_inter_tsan" tsan

# Compile-for-iOS check (no link: the plug-in link is WP01's build_ios.sh).
IOSINC="$VID/sdk/sysinc"
if [ -d "$IOSINC" ]; then
	RESINC=$($CC -print-resource-dir)/include
	for src in "$VID/avc/n2avc_me.c" "$VID/avc/n2avc_inter.c" "$VID/avc/n2avc_rc.c"; do
		obj="$OUT/tmp/$(basename "$src" .c).ios.o"
		# shellcheck disable=SC2086
		$CC -c --target=arm64-apple-ios16.0 -O2 -fno-common -fvisibility=hidden \
			-ffreestanding -nostdinc -isystem "$RESINC" -isystem "$IOSINC" \
			-I "$VID/include" -I "$VID/avc" -std=c11 -Wall -Wextra -Werror \
			-o "$obj" "$src"
	done
	echo "  ios: n2avc_me.c n2avc_inter.c n2avc_rc.c compile for arm64-apple-ios16.0"
else
	echo "  ios: skipped (no sdk/sysinc)"
fi

# Real-encoder integration with WP03.  It needs WP03's files and CLI; the
# harness above is deliberately independent of them.  WP04_NO_INTEGRATION=1
# gives the fast harness-only run while WP03 is still landing.
if [ "${WP04_NO_INTEGRATION:-0}" != "1" ] && [ -f "$VID/avc/test/core/n2avc_cli.c" ] && { [ -f "$VID/avc2/n2avc_core.c" ] || [ -f "$VID/avc/n2avc_enc.c" ]; }; then
	ilog="$OUT/integration.log"
	if bash "$HERE/integration.sh" --quick "$OUT/integration" >"$ilog" 2>&1; then
		line=$(tail -n 1 "$ilog")
		case "$line" in
		N2TEST\ WP04\ INTEGRATION\ PASS\ */*) ;;
		*) echo "N2TEST WP04 FAIL integration (unexpected last line: $line)"; exit 1 ;;
		esac
		frac=${line##* }
		n=${frac%%/*}
		total=$((total + n))
		echo "  integration: $line"
	else
		echo "N2TEST WP04 FAIL integration"
		tail -n 40 "$ilog"
		exit 1
	fi
else
	echo "  integration: skipped (WP03 CLI absent or WP04_NO_INTEGRATION=1)"
fi

echo "N2TEST WP04 PASS $total/$total"
