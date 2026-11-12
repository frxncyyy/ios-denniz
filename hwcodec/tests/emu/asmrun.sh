#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Assemble one A32/T32 snippet with llvm-mc, extract .text, run it at 0x10000 under the core.
#
#   gpu/hwcodec/tests/emu/asmrun.sh [asmrun options] snippet.s
#
# The snippet is the LAST argument; everything before it is passed to the runner.  All code
# and constants must be in .text (literal pools and jump tables emitted by the assembler are
# fine).  The program stops at `udf #254` or at `bx lr` with lr = N2A32_RETURN_ADDR.
# Examples:
#   asmrun.sh add.s
#   asmrun.sh --thumb --reg r0=7 --print r0,r1 --mem 0x30000:16 shift.s
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"             # gpu/hwcodec
tmp="${N2HW_TMP:-/tmp/n2hw-T01/asmrun}"

if [ "$#" -lt 1 ]; then
	sed -n '2,15p' "${BASH_SOURCE[0]}"
	exit 2
fi
src="${!#}"
if [ ! -f "$src" ]; then
	echo "asmrun: no such snippet: $src" >&2
	exit 2
fi

thumb=0
armv8=0
for a in "$@"; do
	case "$a" in
	--thumb) thumb=1 ;;
	--armv8) armv8=1 ;;
	esac
done

if [ "$thumb" = 1 ]; then
	triple="thumbv7a-none-eabi"
	mattr="+neon,+vfp4"
	if [ "$armv8" = 1 ]; then triple="thumbv8a-none-eabi"; mattr="+neon"; fi
else
	triple="armv7a-none-eabi"
	mattr="+neon,+vfp4"
	if [ "$armv8" = 1 ]; then triple="armv8a-none-eabi"; mattr="+neon"; fi
fi

mkdir -p "$tmp"
base="$(basename "$src")"
obj="$tmp/${base%.*}.o"
bin="$tmp/${base%.*}.bin"

if ! llvm-mc -triple="$triple" -mattr="$mattr" -filetype=obj -o "$obj" "$src"; then
	echo "asmrun: assembly failed: $src" >&2
	exit 2
fi
if ! llvm-objcopy -O binary --only-section=.text "$obj" "$bin" 2>/dev/null; then
	echo "asmrun: no .text section in $obj" >&2
	exit 2
fi
if [ ! -s "$bin" ]; then
	echo "asmrun: empty .text in $src" >&2
	exit 2
fi

# Build the runner once per source change.  Only the top-level emu/*.c files are the core and
# instruction classes; test_*/ subdirectories are deliberately excluded.
runner="$tmp/asmrun"
sources=("$here/asmrun.c" "$root/emu/n2a32_trace.c")
for f in "$root"/emu/n2a32_*.c; do
	[ -f "$f" ] || continue
	case "$(basename "$f")" in n2a32_trace.c) continue ;; esac
	sources+=("$f")
done
if [ ! -f "$root/emu/n2a32_core.c" ]; then
	echo "asmrun: the interpreter core (emu/n2a32_core.c, V02) is not in the tree yet" >&2
	exit 3
fi

# Weak UNDEF fallbacks for instruction classes that have not landed yet.  A snippet that uses
# one stops with UNDEF instead of failing to link; the note below says which are missing.
missing_classes=()
for f in n2a32_t32_dp.c n2a32_t32_mem.c n2a32_t32_misc.c n2a32_vfp.c \
	n2a32_neon_3same.c n2a32_neon_shift.c n2a32_neon_misc.c n2a32_neon_ldst.c n2a32_excl.c; do
	[ -f "$root/emu/$f" ] || missing_classes+=("${f%.c}")
done
if [ "${#missing_classes[@]}" -ne 0 ]; then
	echo "asmrun: note: no implementation yet for ${missing_classes[*]} (weak UNDEF stubs)" >&2
fi
cat > "$tmp/weak_classes.c.new" <<'EOF'
#include "n2a32.h"
#define N2WEAK __attribute__((weak))
N2WEAK int n2a32_t32_dp(struct n2a32_cpu *c, uint32_t i) { (void)c; (void)i; return N2A32_UNDEF; }
N2WEAK int n2a32_t32_mem(struct n2a32_cpu *c, uint32_t i) { (void)c; (void)i; return N2A32_UNDEF; }
N2WEAK int n2a32_t32_misc(struct n2a32_cpu *c, uint32_t i) { (void)c; (void)i; return N2A32_UNDEF; }
N2WEAK int n2a32_vfp(struct n2a32_cpu *c, uint32_t i) { (void)c; (void)i; return N2A32_UNDEF; }
N2WEAK int n2a32_neon_3same(struct n2a32_cpu *c, uint32_t i) { (void)c; (void)i; return N2A32_UNDEF; }
N2WEAK int n2a32_neon_shift(struct n2a32_cpu *c, uint32_t i) { (void)c; (void)i; return N2A32_UNDEF; }
N2WEAK int n2a32_neon_misc(struct n2a32_cpu *c, uint32_t i) { (void)c; (void)i; return N2A32_UNDEF; }
N2WEAK int n2a32_neon_ldst(struct n2a32_cpu *c, uint32_t i) { (void)c; (void)i; return N2A32_UNDEF; }
N2WEAK void n2a32_excl_mark(struct n2a32_cpu *c, uint32_t a, unsigned b, uint64_t v) { (void)c; (void)a; (void)b; (void)v; }
N2WEAK int n2a32_excl_store(struct n2a32_cpu *c, uint32_t a, unsigned b, uint64_t v) { (void)c; (void)a; (void)b; (void)v; return 1; }
EOF
if ! cmp -s "$tmp/weak_classes.c.new" "$tmp/weak_classes.c"; then
	mv "$tmp/weak_classes.c.new" "$tmp/weak_classes.c"
else
	rm -f "$tmp/weak_classes.c.new"
fi
sources+=("$tmp/weak_classes.c")

need=0
if [ ! -x "$runner" ]; then
	need=1
else
	for f in "${sources[@]}" "$root/include/n2a32.h" "$here/n2a32_trace.h"; do
		[ "$f" -nt "$runner" ] && need=1
	done
fi
if [ "$need" = 1 ]; then
	clang -std=c11 -O2 -g -Wall -Wextra -pthread \
		-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
		-Wno-error=tautological-constant-out-of-range-compare \
		-I "$root/include" -I "$here" \
		"${sources[@]}" -lm -o "$runner"
fi

args=("$@")
unset 'args[${#args[@]}-1]'
exec "$runner" --load "$bin" "${args[@]}"
