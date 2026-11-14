#!/bin/bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/fakevenc/run_tests_hevc.sh -- tests of the HEVC part of the fake VENC (owner: opus-hevc).
# Builds t_fakevenc_hevc (ASan+UBSan -O1, TSan -O1, then -O2), runs it, and decodes every stream it writes with
# ffmpeg (black-box oracle): the decoded picture must equal the encoder's own reconstruction byte for byte
# (deblocking switched off by the slice header) and pass tests/check/hevc_check.py (SPS, geometry,
# decode, PSNR >= 30 dB against the NV12 source).  Host only.
#
# Last line: N2TEST fakevenc-hevc PASS n/n
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
hw=$(cd "$here/.." && pwd)
OUT=${N2HW_OUT:-/tmp/n2hw-opus-hevc}/test-hevc
rm -rf "$OUT"
mkdir -p "$OUT"
pass=0
total=0
fail() { echo "N2TEST fakevenc-hevc FAIL $*"; exit 1; }
command -v ffmpeg >/dev/null || fail "ffmpeg not installed"
CFL="-std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -I$here -I$hw/include"
for cfg in san tsan o2; do
	if [ $cfg = san ]; then
		flags="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
	elif [ $cfg = tsan ]; then
		flags="-O1 -g -fsanitize=thread"
	else
		flags="-O2"
	fi
	nice -n 10 clang $CFL $flags -o "$OUT/t_$cfg" "$here/t_fakevenc_hevc.c" "$here/n2venc_fake_hevc.c" -lm -lpthread \
		|| fail "build $cfg"
	mkdir -p "$OUT/$cfg"
	total=$((total + 1))
	if "$OUT/t_$cfg" "$OUT/$cfg" > "$OUT/$cfg/unit.txt" 2>&1; then
		pass=$((pass + 1))
	else
		cat "$OUT/$cfg/unit.txt"
		fail "unit $cfg"
	fi
	tail -1 "$OUT/$cfg/unit.txt"
done
# the two builds must produce the same streams
for f in "$OUT"/san/case*.h265; do
	total=$((total + 1))
	cmp -s "$f" "$OUT/o2/$(basename "$f")" && cmp -s "$f" "$OUT/tsan/$(basename "$f")" && pass=$((pass + 1)) \
		|| fail "san/tsan/o2 differ: $(basename "$f")"
done
for f in "$OUT"/o2/case*.h265; do
	b=${f%.h265}
	read -r w h dbk < "$b.txt"
	ffmpeg -hide_banner -loglevel error -y -i "$f" -f rawvideo -pix_fmt yuv420p "$b.dec" || fail "ffmpeg decode $(basename "$f")"
	if [ "$dbk" = 0 ]; then
		total=$((total + 1))
		if cmp -s "$b.dec" "$b.rec"; then
			pass=$((pass + 1))
			echo "$(basename "$f"): ${w}x${h} decoded == reconstruction ($(wc -c < "$f") bytes)"
		else
			fail "$(basename "$f"): ffmpeg output differs from the encoder reconstruction"
		fi
	fi
	total=$((total + 1))
	if python3 "$hw/tests/check/hevc_check.py" "$f" "$w" "$h" 1 --source "$b.nv12" --quiet > "$b.check" 2>&1; then
		pass=$((pass + 1))
		echo "$(basename "$f"): hevc_check PASS"
	else
		cat "$b.check"
		fail "$(basename "$f"): hevc_check"
	fi
done
rm -f "$OUT"/*/case*.dec "$OUT"/*/case*.nv12 "$OUT"/*/case*.rec
echo "N2TEST fakevenc-hevc PASS $pass/$total"
