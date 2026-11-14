#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V16 host tests: T32 32-bit load/store, exclusive, table branch, branches and system.
# Builds everything under /tmp/n2hw-v16 (RAM-backed), as the worker rules require.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)          # gpu/hwcodec
B=/tmp/n2hw-v16/build

rm -rf "$B"
mkdir -p "$B"

MC="llvm-mc -triple=thumbv7a-linux-gnueabi -mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8"

# 1. Assembly text -> llvm-mc encodings -> C header.  No hand-typed hex anywhere.
python3 "$here/gen_enc.py" asm > "$B/main.s"
if ! $MC --show-encoding "$B/main.s" > "$B/main.enc" 2> "$B/main.err"; then
	echo "N2TEST V16 FAIL llvm-mc rejected the test assembly"
	cat "$B/main.err"
	exit 1
fi
python3 "$here/gen_enc.py" asm-mp > "$B/mp.s"
if ! $MC --show-encoding "$B/mp.s" > "$B/mp.enc" 2> "$B/mp.err"; then
	# PLDW needs the MP extensions feature; retry with it (documented in gen_enc.py).
	if ! llvm-mc -triple=thumbv7a-linux-gnueabi \
		-mattr=+neon,+vfp4,+hwdiv,+hwdiv-arm,+fp-armv8,+mp \
		--show-encoding "$B/mp.s" > "$B/mp.enc" 2> "$B/mp.err"; then
		echo "N2TEST V16 FAIL llvm-mc rejected the PLDW assembly"
		cat "$B/mp.err"
		exit 1
	fi
fi
python3 "$here/gen_enc.py" header "$B/main.enc" "$B/mp.enc" > "$B/enc.h"

# 2. Build the unit tests twice: ASan+UBSan, then -O2 without sanitizers.
CFLAGS="-std=c11 -g -Wall -Wextra -Werror -I $root/include -I $B"
SRCS="$here/t_v16.c $root/emu/n2a32_t32_mem.c $root/emu/n2a32_t32_misc.c $root/emu/n2a32_excl.c"

clang $CFLAGS -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -o "$B/t_v16_asan" $SRCS -pthread
clang $CFLAGS -O2 -o "$B/t_v16_o2" $SRCS -pthread

# 3. Run both.  The last line printed by each is the N2TEST verdict.
"$B/t_v16_asan"
"$B/t_v16_o2"
