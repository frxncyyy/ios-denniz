#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# 2 million random instruction trials through the core with every instruction class linked,
# once under ASan+UBSan and once at -O2 without sanitizers.
#
#   bash gpu/hwcodec/tests/emu/run_fuzz.sh
#
# Last line: N2TEST T01-fuzz PASS   or   N2TEST T01-fuzz FAIL <reason>
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
tmp="${N2HW_TMP:-/tmp/n2hw-T01}/fuzz"

fail() { echo "N2TEST T01-fuzz FAIL $*"; exit 1; }

classes=(
	n2a32_arm_dp.c n2a32_arm_media.c n2a32_arm_mem.c n2a32_arm_misc.c
	n2a32_t16.c n2a32_t32_dp.c n2a32_t32_mem.c n2a32_t32_misc.c
	n2a32_vfp.c n2a32_neon_3same.c n2a32_neon_shift.c n2a32_neon_misc.c
	n2a32_neon_ldst.c
)
missing=()
for f in n2a32_core.c n2a32_mem.c n2a32_excl.c "${classes[@]}"; do
	[ -f "$root/emu/$f" ] || missing+=("$f")
done
if [ "${#missing[@]}" -ne 0 ]; then
	fail "missing interpreter sources (V02/V1x): ${missing[*]}"
fi

mkdir -p "$tmp"
na="${N2FUZZ_A32:-1000000}"
nt="${N2FUZZ_T32:-1000000}"
sources=()
for f in "$root"/emu/n2a32_*.c; do
	[ -f "$f" ] && sources+=("$f")
done
sources+=("$here/fuzz.c")

echo "== building ASan+UBSan fuzz binary"
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
	-fno-sanitize-recover=all -pthread -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
	-Wno-error=tautological-constant-out-of-range-compare -I "$root/include" -I "$here" \
	"${sources[@]}" -lm -o "$tmp/fuzz_asan" \
	|| fail "ASan+UBSan build failed"
echo "== building -O2 fuzz binary"
clang -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -D_POSIX_C_SOURCE=200809L \
	-D_DEFAULT_SOURCE -Wno-error=tautological-constant-out-of-range-compare \
	-I "$root/include" -I "$here" \
	"${sources[@]}" -lm -o "$tmp/fuzz_o2" \
	|| fail "-O2 build failed"

run_one() {
	local bin="$1" log="$2"
	if ! "$bin" --a32 "$na" --t32 "$nt" > "$log" 2>&1; then
		echo "---- $log (tail) ----"
		tail -30 "$log" || true
		fail "random instruction run failed ($bin)"
	fi
	grep '^fuzz:' "$log" || true
}

echo "== 2M trials, ASan+UBSan"
run_one "$tmp/fuzz_asan" "$tmp/asan.log"
echo "== 2M trials, -O2"
run_one "$tmp/fuzz_o2" "$tmp/o2.log"

echo "N2TEST T01-fuzz PASS"
