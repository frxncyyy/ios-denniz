#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Host tests for package V17 (VFP scalar floating point, A32 and T32).
# Builds only under /tmp/n2hw-V17.  No arguments.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hw="$(cd "$here/../.." && pwd)"
inc="$hw/include"
emu="$hw/emu"
out=/tmp/n2hw-V17

fail() { echo "N2TEST V17 FAIL $*"; exit 1; }

mkdir -p "$out"

python3 "$here/gen_enc.py" "$out/enc_v17.h" "$out" || fail "llvm-mc encoding generation"

common=(-std=c11 -g -Wall -Wextra -Werror "-I$inc" "-I$emu" "-I$out")

nice -n 10 clang "${common[@]}" -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	"$emu/n2a32_vfp.c" "$here/t_vfp.c" -lm -o "$out/t_vfp_asan" \
	|| fail "clang -O1 ASan+UBSan build"
ASAN_OPTIONS=detect_leaks=0 nice -n 10 "$out/t_vfp_asan" || fail "ASan+UBSan run"

nice -n 10 clang "${common[@]}" -O2 \
	"$emu/n2a32_vfp.c" "$here/t_vfp.c" -lm -o "$out/t_vfp_o2" \
	|| fail "clang -O2 build"
nice -n 10 "$out/t_vfp_o2" || fail "-O2 run"
