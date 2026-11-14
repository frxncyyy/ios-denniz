#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V40 host tests: the /dev/vcu device model.
#
#   bash gpu/hwcodec/vcu/test_v40/run_tests.sh
#
# Builds and runs, in this order:
#   1. layout_probe.c for armv7a-linux-gnueabi against the *kernel* uapi header, and extracts its
#      table (probe_values.h) with llvm-objcopy -- the independent layout oracle;
#   2. t_v40.c + n2vcu.c + T02's n2vc_hw_fake.c with ASan+UBSan -O1;
#   3. the same with -O2 and no sanitizers;
#   4. the same with -fsanitize=thread (the GET_OBJECT/wait tests use a real second thread).
# Last line: N2TEST V40 PASS <passed>/<total> or N2TEST V40 FAIL <what>.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
tmp=${N2HW_TMP:-/tmp/n2hw-V40}
mkdir -p "$tmp"
tmp=$(cd "$tmp" && pwd)

kcands=()
[ -n "${N2HW_KERNEL:-}" ] && kcands+=("$N2HW_KERNEL")
kcands+=("$root/../../../../../../nord2-current-kernel")
kcands+=("$root/../../../../../nord2-current-kernel")
kcands+=("$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel")
kernel=""
for k in "${kcands[@]}"; do
	if [ -f "$k/include/uapi/linux/mtk_vcu_controls.h" ]; then
		kernel=$(cd "$k" && pwd)
		break
	fi
done
if [ -z "$kernel" ]; then
	echo "N2TEST V40 FAIL cannot find nord2-current-kernel (set N2HW_KERNEL)" >&2
	exit 1
fi

inc="-I$root/gpu/hwcodec/include -I$root/gpu/hwcodec/vcu -I$root/gpu/hwcodec/tests/hw -I$tmp"
warn="-std=c11 -Wall -Wextra -Werror"
srcs="$here/t_v40.c $root/gpu/hwcodec/vcu/n2vcu.c $root/gpu/hwcodec/tests/hw/n2vc_hw_fake.c $root/gpu/hwcodec/emu/n2a32_mem.c"

# 1. The layout oracle: compile the kernel's own uapi header for 32-bit ARM.
echo "== V40 layout probe: $kernel/include/uapi/linux/mtk_vcu_controls.h (armv7a AAPCS)"
nice -n 10 clang --target=armv7a-linux-gnueabi -c -I "$kernel/include/uapi" \
	-o "$tmp/layout_probe.o" "$here/layout_probe.c"
python3 "$here/probe_extract.py" "$tmp/layout_probe.o" "$tmp/probe_values.h"

# 2-4. Three builds of the same suite.
run_one() {
	local name=$1; shift
	echo "== V40 build/run: $name"
	# shellcheck disable=SC2086
	nice -n 10 clang $warn "$@" $inc -o "$tmp/t_v40_$name" $srcs -lpthread
	N2V40_TMP="$tmp" "$tmp/t_v40_$name" >"$tmp/out_$name.txt" 2>&1 || {
		cat "$tmp/out_$name.txt"
		echo "N2TEST V40 FAIL $name run" >&2
		exit 1
	}
	tail -n 1 "$tmp/out_$name.txt"
}

run_one asan -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
run_one o2 -O2
run_one tsan -O1 -g -fsanitize=thread

passed=0
total=0
for n in asan o2 tsan; do
	read -r p t < <(awk '/^SUMMARY/ {print $2, $3}' "$tmp/out_$n.txt")
	passed=$((passed + p))
	total=$((total + t))
done
if [ "$passed" -ne "$total" ]; then
	for n in asan o2 tsan; do cat "$tmp/out_$n.txt"; done
	echo "N2TEST V40 FAIL $((total - passed)) checks" >&2
	exit 1
fi
echo "N2TEST V40 PASS $passed/$total"
