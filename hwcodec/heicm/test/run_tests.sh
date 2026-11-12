#!/bin/bash
# SPDX-License-Identifier: MIT
# Host test of heicm/n2vt_trace.c (opus-jpeg) with gpu/video's fake Apple runtime; ASan+UBSan.  Last line: N2TEST HEICM PASS.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); HW=$(cd "$HERE/../.." && pwd); V=$(cd "$HW/../video" && pwd)
O=/tmp/n2hw-opus-jpeg/heicm-test; rm -rf "$O"; mkdir -p "$O"
nice -n 10 clang -std=c11 -O1 -g -Wall -Wextra -Werror -DN2_HOST_FAKE -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-I "$V/include" -I "$V/fake" -I "$V/vt" -I "$V/jpeg" -I "$V" -o "$O/t_trace" "$HERE/t_trace.c" "$HW/heicm/n2vt_trace.c" \
	"$V/vt/n2jpeg_vt.c" "$V/jpeg/n2jpeg.c" "$V/kern/n2vk_ref.c" "$V/kern/n2vk_dispatch.c" "$V/fake/fake_apple.c" "$V/fake/fake_vt.c" -lm \
	|| { echo "N2TEST HEICM FAIL build"; exit 1; }
mkdir -p "$O/a" "$O/b"; touch "$O/switch"
N2T_SWITCH=$O/switch N2T_DIR=$O/a "$O/t_trace" && N2T_SWITCH=$O/switch N2T_DIR=$O/b T_MODE=tiles "$O/t_trace"
r=$?; [ $r = 0 ] && echo "N2TEST HEICM PASS" || { echo "N2TEST HEICM FAIL"; cat "$O"/a/*.log "$O"/b/*.log 2>/dev/null | head -40; }
exit $r
