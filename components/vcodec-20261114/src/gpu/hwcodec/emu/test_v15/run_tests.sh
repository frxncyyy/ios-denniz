#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Host tests for package V15 (T32 32-bit data processing).
# Builds under /tmp/n2hw-V15 by default; no files are written to the work tree.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
emu="$(dirname "$here")"
inc="$(dirname "$emu")/include"
out="${N2HW_TMP:-/tmp/n2hw-V15}"
mkdir -p "$out"

python3 "$here/gen_enc.py" "$out" "$out/enc_gen.h"

cflags=(-std=c11 -g -Wall -Wextra -Werror "-I$inc" "-I$out")

clang "${cflags[@]}" -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	"$emu/n2a32_t32_dp.c" "$here/t_dp.c" -o "$out/t_dp_asan"
clang "${cflags[@]}" -O2 "$emu/n2a32_t32_dp.c" "$here/t_dp.c" -o "$out/t_dp_o2"

"$out/t_dp_asan"
"$out/t_dp_o2"
