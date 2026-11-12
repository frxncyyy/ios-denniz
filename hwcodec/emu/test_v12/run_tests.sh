#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V12 host tests: A32 load/store, load/store multiple, exclusives; branches, system, hints.
# Builds everything under /tmp/n2hw-v12 (RAM-backed), as the worker rules require.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)          # gpu/hwcodec
B=/tmp/n2hw-v12/build

rm -rf "$B"
mkdir -p "$B"

MC="llvm-mc -triple=armv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8"
MC_MP="llvm-mc -triple=armv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8,+mp"

# 1. Assembly text -> llvm-mc encodings -> C header.  No hand-typed hex anywhere.
python3 "$here/gen_enc.py" asm > "$B/main.s"
$MC --show-encoding "$B/main.s" > "$B/main.enc" 2> "$B/main.err"
python3 "$here/gen_enc.py" asm-mp > "$B/mp.s"
$MC_MP --show-encoding "$B/mp.s" > "$B/mp.enc" 2> "$B/mp.err"
python3 "$here/gen_enc.py" header "$B/main.enc" "$B/mp.enc" > "$B/enc.h"

# 2. Build the unit tests twice: ASan+UBSan, then -O2 without sanitizers.
CFLAGS="-std=c11 -g -Wall -Wextra -Werror -I $root/include -I $B"
SRCS="$here/t_v12.c $root/emu/n2a32_arm_mem.c $root/emu/n2a32_arm_misc.c"

clang $CFLAGS -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	-o "$B/t_v12_asan" $SRCS
clang $CFLAGS -O2 -o "$B/t_v12_o2" $SRCS

# 3. Run both.  The last line printed is the N2TEST verdict.
"$B/t_v12_asan"
"$B/t_v12_o2"
