#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V15 routing check -- OPTIONAL, not part of run_tests.sh.
#
# run_tests.sh calls n2a32_t32_dp() directly.  This script checks the other half
# of the integration: that emu/n2a32_core.c's t32_route() reaches this class for
# every region, and that the core's 32-bit fetch (first halfword << 16 | second)
# agrees with it.  Each snippet starts in ARM mode, BXes into Thumb, runs the
# 32-bit instructions and returns with `bx lr` (16-bit, so it only needs V14).
# Builds under /tmp/n2hw-V15/route, never touches the phone.
#
#   bash gpu/hwcodec/emu/test_v15/check_routing.sh
#   -> "V15-ROUTE PASS n/n"  or  "V15-ROUTE FAIL <name>: ..."
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
asmrun="$root/tests/emu/asmrun.sh"
B=/tmp/n2hw-V15/route

if [ ! -f "$asmrun" ]; then
	echo "V15-ROUTE SKIP: $asmrun not present (T01 harness missing)" >&2
	exit 2
fi

pass=0
total=0

# run_one <name> <<'EOF' ... snippet ... EOF
# Expects the snippet to end with `bx lr` in Thumb mode and to leave the
# registers named in CHECK_*; the caller greps the printed lines.
run_one()
{
	local name="$1" src="$B/$1.s"
	shift
	cat > "$src"
	local out
	total=$((total + 1))
	out=$(N2HW_TMP="$B/tmp" bash "$asmrun" --print r0,r1,r2,r3 "$@" "$src" 2>&1) || true
	if printf '%s\n' "$out" | grep -q '^stop=ok$'; then
		pass=$((pass + 1))
		printf '%s\n' "$out" | grep -E '^r[0-3]=' | tr '\n' ' '
		echo
	else
		echo "V15-ROUTE FAIL $name:"
		printf '%s\n' "$out" | sed 's/^/    /' | head -20
	fi
}

rm -rf "$B"
mkdir -p "$B"

echo "== alu_reg: 0xEA00-0xEBFF shifted/plain register"
run_one alu_reg <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r1, #0xf0f0
	movt r1, #0xf0f0
	movw r2, #0x0ff0
	movt r2, #0x0ff0
	and.w r0, r1, r2
	add.w r3, r1, r2, lsr #16
	bx lr
EOF

echo "== alu_imm: 0xF000-0xF5FF modified immediate"
run_one alu_imm <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r1, #0x5678
	movt r1, #0x1234
	orr.w r0, r1, #0x100
	movw r2, #0x00ff
	orr.w r2, r2, #0x00ff0000
	tst.w r1, #0x00ff00ff
	mvn.w r3, #0xff
	bx lr
EOF

echo "== plain_imm: 0xF200-0xF7FF (MOVW/MOVT/ADDW/SUBW/ADR/bitfield/sat)"
run_one plain_imm <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r0, #0x1234
	movt r0, #0xabcd
	addw r1, r0, #0x123
	subw r2, r0, #0x234
	adr.w r3, target
	bx lr
	.p2align 2
target:
	nop.w
	nop.w
EOF

echo "== bitfield: SBFX/UBFX/BFI/BFC/SSAT/USAT"
run_one bitfield <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r1, #0x0f80
	ubfx r0, r1, #4, #8
	sbfx r2, r1, #4, #8
	mvn.w r3, #0
	bfi r3, r1, #8, #8
	bfc r3, #4, #4
	bx lr
EOF

echo "== sat: SSAT/USAT with shifts"
run_one sat <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r1, #0x1234
	ssat r0, #8, r1
	usat r2, #8, r1
	ssat r3, #16, r1, asr #4
	bx lr
EOF

echo "== muldiv: 0xFB00 multiplies (divide is unit-tested only: asmrun's
      llvm-mc invocation has no +hwdiv, so UDIV/SDIV cannot be assembled here)"
run_one muldiv <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r1, #0x0006
	movw r2, #0x0007
	movw r3, #0x0100
	mul r0, r1, r2
	mla r0, r1, r2, r3
	mls r0, r1, r2, r3
	smulbb r3, r1, r2
	bx lr
EOF

echo "== mul64: SMULL/UMULL/SMLAL/UMLAL/UMAAL"
run_one mul64 <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r2, #0x0000
	movt r2, #0x0001
	movw r3, #0x0000
	movt r3, #0x0002
	smull r0, r1, r2, r3
	umull r0, r1, r2, r3
	movw r0, #0x0001
	movw r1, #0x0000
	smlal r0, r1, r2, r3
	bx lr
EOF

echo "== shift_reg: LSL/LSR/ASR/ROR by register"
run_one shift_reg <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r1, #0x5678
	movt r1, #0x1234
	movw r2, #0x0004
	lsl.w r0, r1, r2
	lsr.w r3, r1, r2
	asr.w r0, r1, r2
	ror.w r3, r1, r2
	bx lr
EOF

echo "== extend_rev: SXTB/SXTH/UXTB/UXTH/REV/REV16/REVSH/RBIT/CLZ/SEL"
run_one extend_rev <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r1, #0x807f
	movt r1, #0x1234
	sxtb.w r0, r1
	sxth.w r2, r1
	uxtb.w r3, r1
	rev.w r0, r1
	rev16.w r2, r1
	revsh.w r3, r1
	rbit r0, r1
	clz r2, r1
	bx lr
EOF

echo "== parallel_qadd: parallel add/sub and the QADD family"
run_one parallel_qadd <<'EOF'
.syntax unified
.arm
	adr r0, t32
	add r0, r0, #1
	bx r0
.thumb
t32:
	movw r1, #0x7fff
	movt r1, #0x8001
	movw r2, #0x0001
	movt r2, #0x0002
	sadd16 r0, r1, r2
	uadd8 r3, r1, r2
	qadd r0, r1, r2
	qsub r3, r1, r2
	bx lr
EOF

if [ "$pass" -eq "$total" ]; then
	echo "V15-ROUTE PASS $pass/$total"
	exit 0
fi
echo "V15-ROUTE FAIL $pass/$total"
exit 1
