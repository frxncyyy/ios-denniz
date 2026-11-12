#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# All T01 host tests: trace ring/file, asmrun smoke tests, 2M-case fuzz, differential kernels.
#
#   bash gpu/hwcodec/tests/emu/run_tests.sh
#
# Last line: N2TEST T01 PASS <suites>/<suites>   or   N2TEST T01 FAIL <suite>
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
tmp="${N2HW_TMP:-/tmp/n2hw-T01}/all"
mkdir -p "$tmp"

passed=0
total=5
fail() { echo "N2TEST T01 FAIL $*"; exit 1; }

echo "== trace (ASan+UBSan)"
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
	-fno-sanitize-recover=all -I "$root/include" -I "$here" \
	"$root/emu/n2a32_trace.c" "$here/t_trace.c" -o "$tmp/t_trace_asan" \
	|| fail "trace ASan+UBSan build"
"$tmp/t_trace_asan" || fail "trace ASan+UBSan run"

echo "== trace (-O2)"
clang -std=c11 -O2 -g -Wall -Wextra -Werror -I "$root/include" -I "$here" \
	"$root/emu/n2a32_trace.c" "$here/t_trace.c" -o "$tmp/t_trace_o2" \
	|| fail "trace -O2 build"
"$tmp/t_trace_o2" || fail "trace -O2 run"

echo "== trace (TSan)"
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=thread -I "$root/include" -I "$here" \
	"$root/emu/n2a32_trace.c" "$here/t_trace.c" -o "$tmp/t_trace_tsan" \
	|| fail "trace TSan build"
"$tmp/t_trace_tsan" || fail "trace TSan run"
echo "== trace symboliser through V03"
"$here/test_trace_elf.sh" || fail "trace symboliser through V03"
passed=$((passed + 1))

echo "== support_arm.c (ASan+UBSan)"
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fno-builtin -fno-stack-protector \
	-fsanitize=address,undefined -fno-sanitize-recover=all -I "$root/include" \
	"$here/support_arm.c" "$here/t_support.c" -o "$tmp/t_support_asan" \
	|| fail "support ASan+UBSan build"
"$tmp/t_support_asan" || fail "support ASan+UBSan run"
echo "== support_arm.c (-O2)"
clang -std=c11 -O2 -g -Wall -Wextra -Werror -fno-builtin -fno-stack-protector \
	-I "$root/include" "$here/support_arm.c" "$here/t_support.c" -o "$tmp/t_support_o2" \
	|| fail "support -O2 build"
"$tmp/t_support_o2" || fail "support -O2 run"
passed=$((passed + 1))

echo "== asmrun"
"$here/test_asmrun.sh" || fail "asmrun smoke tests"
passed=$((passed + 1))

echo "== fuzz (1M+1M, same as the acceptance run)"
"$here/run_fuzz.sh" || fail "fuzz"
passed=$((passed + 1))

echo "== differential"
"$here/run_diff.sh" || fail "differential"
passed=$((passed + 1))

echo "N2TEST T01 PASS $passed/$total"
