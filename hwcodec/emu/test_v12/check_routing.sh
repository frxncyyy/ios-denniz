#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V12 routing check -- OPTIONAL, not part of run_tests.sh.
#
# The V12 class functions are unit-tested by run_tests.sh without the core.  This script checks
# the other half of integration: that emu/n2a32_core.c's a32_route sends the V12 forms to
# n2a32_arm_mem / n2a32_arm_misc, using T01's asmrun (which builds the core and runs a snippet
# to N2A32_STOP_RETURN).  It is expected to FAIL until hw-v02 applies the corrected table
# (see the board messages); it never touches the phone and builds only under /tmp/n2hw-v12.
#
#   bash gpu/hwcodec/emu/test_v12/check_routing.sh
#   -> "V12-ROUTE PASS 7/7"  or  "V12-ROUTE FAIL <name>: <stop line>"
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
asmrun="$root/tests/emu/asmrun.sh"
B=/tmp/n2hw-v12/route

if [ ! -f "$asmrun" ]; then
	echo "V12-ROUTE SKIP: $asmrun not present (T01 harness missing)" >&2
	exit 2
fi

rm -rf "$B"
mkdir -p "$B"

pass=0
total=0

# run_one NAME SNIPPETFILE ASMRUN-ARGS...
run_one()
{
	local name="$1" src="$2"
	shift 2
	local out
	total=$((total + 1))
	out=$(N2HW_TMP="$B/tmp" bash "$asmrun" "$@" "$src" 2>&1) || true
	if printf '%s\n' "$out" | grep -q '^stop=ok$'; then
		pass=$((pass + 1))
	else
		echo "V12-ROUTE FAIL $name:"
		printf '%s\n' "$out" | grep -E '^stop|^stop_insn' | sed 's/^/    /'
	fi
}

cat > "$B/prologue.s" <<'EOF'
.syntax unified
.arm
push {r4, r5, r11, lr}
mov r11, sp
sub sp, sp, #8
mov r4, r0
add r0, r4, #1
add sp, sp, #8
pop {r4, r5, r11, pc}
EOF

cat > "$B/movshift.s" <<'EOF'
.syntax unified
.arm
mov r0, r1, lsl r2
bx lr
EOF

cat > "$B/postreg.s" <<'EOF'
.syntax unified
.arm
ldr r1, [r0], r2
bx lr
EOF

cat > "$B/extraneg.s" <<'EOF'
.syntax unified
.arm
ldrh r1, [r0, #-2]
bx lr
EOF

cat > "$B/ldrdpost.s" <<'EOF'
.syntax unified
.arm
ldrd r2, r3, [r0], #8
bx lr
EOF

cat > "$B/strdneg.s" <<'EOF'
.syntax unified
.arm
strd r2, r3, [r0, #-8]!
bx lr
EOF

cat > "$B/hints.s" <<'EOF'
.syntax unified
.arm
wfe
sev
yield
dbg #0
bx lr
EOF

run_one prologue    "$B/prologue.s"  --reg r0=41
run_one movshift    "$B/movshift.s"  --reg r1=6 --reg r2=3
run_one postreg     "$B/postreg.s"   --reg r0=0x30000 --reg r2=0
run_one extraneg    "$B/extraneg.s"  --reg r0=0x30000
run_one ldrdpost    "$B/ldrdpost.s"  --reg r0=0x30000
run_one strdneg     "$B/strdneg.s"   --reg r0=0x30000 --reg r2=0x77 --reg r3=0x88
run_one hints       "$B/hints.s"

if [ "$pass" -eq "$total" ]; then
	echo "V12-ROUTE PASS $pass/$total"
	exit 0
fi
echo "V12-ROUTE FAIL $pass/$total"
exit 1
