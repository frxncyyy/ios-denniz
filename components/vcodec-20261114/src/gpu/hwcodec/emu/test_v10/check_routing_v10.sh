#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V10 routing check -- OPTIONAL, not part of run_tests.sh.
#
# run_tests.sh unit-tests n2a32_arm_dp() without the core.  This script checks the other
# half of integration: that emu/n2a32_core.c's a32_route() sends the V10 forms to
# n2a32_arm_dp, by running small A32 snippets through T01's asmrun (which builds the real
# core + all class sources).  It is expected to FAIL until hw-v02 applies the corrected
# a32_route case 0/1 table (see the board messages); it never touches the phone and builds
# only under /tmp/n2hw-v10.
#
#   bash gpu/hwcodec/emu/test_v10/check_routing_v10.sh
#   -> "V10-ROUTE PASS 13/13"  or  "V10-ROUTE FAIL <name>: <stop line>"
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
asmrun="$root/tests/emu/asmrun.sh"
B=/tmp/n2hw-V10/route

if [ ! -f "$asmrun" ]; then
	echo "V10-ROUTE SKIP: $asmrun not present (T01 harness missing)" >&2
	exit 2
fi

rm -rf "$B"
mkdir -p "$B"

pass=0
total=0

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
		echo "V10-ROUTE FAIL $name:"
		printf '%s\n' "$out" | grep -E '^stop|insn=' | sed 's/^/    /' | head -4
	fi
}

snippet()
{
	cat > "$B/$1.s"
}

snippet alu_imm <<'EOF'
.syntax unified
.arm
and r0, r1, #0xff
eor r2, r0, #0x0000000f
sub r3, r2, #0x7f000000
rsb r4, r3, #0
add r5, r4, #0x80000000
adc r6, r5, #1
sbc r7, r6, #1
rsc r8, r7, #1
orr r9, r8, #0xf0
bic r10, r9, #0x0f
mvn r11, #0
mov r12, #0x1
bx lr
EOF

snippet alu_reg <<'EOF'
.syntax unified
.arm
and r0, r1, r2
eor r3, r0, r2
add r4, r3, r1
sub r5, r4, r2
rsb r6, r5, r1
orr r7, r6, r2
bic r8, r7, r1
mvn r9, r8
mov r10, r9
bx lr
EOF

snippet shift_imm <<'EOF'
.syntax unified
.arm
add r0, r1, r2, lsl #3
add r0, r0, r1, lsr #7
add r0, r0, r1, asr #5
add r0, r0, r1, ror #11
add r0, r0, r2, rrx
add r0, r0, r1, lsr #32
add r0, r0, r1, asr #32
add r0, r0, r1, lsl #0
bx lr
EOF

snippet shift_reg <<'EOF'
.syntax unified
.arm
add r0, r1, r2, lsl r3
add r0, r0, r1, lsr r3
add r0, r0, r1, asr r3
add r0, r0, r1, ror r3
bx lr
EOF

snippet movw_movt <<'EOF'
.syntax unified
.arm
movw r0, #0x1234
movt r0, #0xabcd
movw r1, #0xffff
movt r1, #0xffff
bx lr
EOF

snippet mul32 <<'EOF'
.syntax unified
.arm
mul r0, r1, r2
mla r3, r0, r1, r2
mls r4, r3, r1, r2
muls r5, r4, r0
bx lr
EOF

snippet mul64 <<'EOF'
.syntax unified
.arm
umull r0, r1, r2, r3
umlal r0, r1, r2, r3
smull r4, r5, r2, r3
smlal r4, r5, r2, r3
umaal r0, r1, r2, r3
bx lr
EOF

snippet psr <<'EOF'
.syntax unified
.arm
mrs r0, cpsr
msr apsr_nzcvq, r0
msr apsr_g, r0
msr apsr_nzcvqg, r0
bx lr
EOF

snippet clz <<'EOF'
.syntax unified
.arm
clz r0, r1
clz r2, r3
bx lr
EOF

snippet adr <<'EOF'
.syntax unified
.arm
adr r0, target
add r1, pc, #4
sub r2, pc, #4
target:
bx lr
EOF

snippet alu_pc <<'EOF'
.syntax unified
.arm
add pc, pc, #0
udf #1
bx lr
EOF

snippet cmp_test <<'EOF'
.syntax unified
.arm
tst r0, r1
teq r0, r1
cmp r0, r1
cmn r0, r1
tst r0, #0xf0
cmp r0, #0x10
bx lr
EOF

run_one alu_imm   "$B/alu_imm.s"   --reg r1=0x12345678
run_one alu_reg   "$B/alu_reg.s"   --reg r1=0x12345678 --reg r2=0x0000000f
run_one shift_imm "$B/shift_imm.s" --reg r1=0x80000001 --reg r2=0x0000000f
run_one shift_reg "$B/shift_reg.s" --reg r1=0x80000001 --reg r2=0x0000000f --reg r3=5
run_one movw      "$B/movw_movt.s"
run_one mul32     "$B/mul32.s"     --reg r1=7 --reg r2=9
run_one mul64     "$B/mul64.s"     --reg r2=7 --reg r3=9
run_one psr       "$B/psr.s"
run_one clz       "$B/clz.s"       --reg r1=0x0000ffff --reg r3=0
run_one adr       "$B/adr.s"
run_one alu_pc    "$B/alu_pc.s"
run_one cmp_test  "$B/cmp_test.s"  --reg r0=0x12345678 --reg r1=0x0000000f

if [ "$pass" -eq "$total" ]; then
	echo "V10-ROUTE PASS $pass/$total"
	exit 0
fi
echo "V10-ROUTE FAIL $pass/$total"
exit 1
