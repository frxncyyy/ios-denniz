#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Smoke tests for asmrun.sh.  Requires V02's emu/n2a32_core.c; reports a clear skip until it
# is in the tree (the differential and fuzz scripts fail loudly in that case instead, because
# their acceptance depends on it).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
if [ ! -f "$root/emu/n2a32_core.c" ]; then
	echo "T01 asmrun: SKIP (emu/n2a32_core.c, V02, not in the tree yet)"
	exit 0
fi

checks=0
failures=0
check_contains() {
	local text="$1" want="$2" what="$3"
	checks=$((checks + 1))
	if ! grep -qF -- "$want" <<<"$text"; then
		failures=$((failures + 1))
		echo "FAIL asmrun: $what (wanted '$want')" >&2
		echo "$text" >&2
	fi
}

out="$("$here/asmrun.sh" "$here/asm/add.s")"
check_contains "$out" "r2=0x0000000c" "A32 add"
check_contains "$out" "r3=0x00000030" "A32 shift"
check_contains "$out" "stop=ok" "A32 sentinel"

out="$("$here/asmrun.sh" --mem 0x30000:4 "$here/asm/add.s")"
check_contains "$out" "mem[0x00030000]=30000000" "A32 store"

out="$("$here/asmrun.sh" --thumb "$here/asm/t16.s")"
check_contains "$out" "r1=0x00000100" "T16 lsl"
check_contains "$out" "r2=0x00000010" "T16 lsr"
check_contains "$out" "r3=0x00000055" "T16 IT block"
check_contains "$out" "thumb=1" "T32 flag"

if [ -f "$root/emu/n2a32_t32_dp.c" ]; then
	out="$("$here/asmrun.sh" --thumb "$here/asm/shift.s")"
	check_contains "$out" "r1=0x00000100" "T32 lsl"
	check_contains "$out" "r2=0x00000010" "T32 ror"
	check_contains "$out" "r3=0x00000055" "T32 IT block"
fi

out="$("$here/asmrun.sh" --reg r0=3 --reg r1=4 --print r0,r1,r2 "$here/asm/regs.s")"
check_contains "$out" "r0=0x00000003" "register override"
check_contains "$out" "r2=0x00000007" "register add"
if grep -q '^r3=' <<<"$out"; then
	checks=$((checks + 1)); failures=$((failures + 1))
	echo "FAIL asmrun: --print emitted an unrequested register" >&2
fi

echo "T01 asmrun: $checks checks, $failures failures"
[ "$failures" -eq 0 ]
