#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# R1 host suite: the JPEG encoder unit tests (ASan+UBSan) and the black-box oracle.
#
# unit   : jpeg/test/t_jpeg.c -- literal Annex K tables, the independent Huffman decoder, coefficient exactness against
#          n2vk_ref_fdct8_quant, restart markers, 0xFF stuffing, the buffer contract and the argument checks.
# oracle : djpeg and ffmpeg must decode every output with exit 0 and empty error streams; luma PSNR at quality 90 must
#          reach 38 dB where baseline JPEG can (smooth content) and must match cjpeg -dct int within 0.3 dB on
#          noise-like content; the file must be within 15 % of cjpeg -quality 90 -dct int; the DQT/DHT maps must be
#          identical to cjpeg's at several qualities; restart intervals and APPn/COM pass-through are exercised.
#
# usage: bash gpu/video/jpeg/test/run_tests.sh [outdir]      (default gpu/video/build/R1)
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-"$ROOT/build/R1"}
CC=${CC:-clang}

mkdir -p "$OUT/unit" "$OUT/oracle"
TOTAL=0

fail() {
	echo "N2TEST R1 FAIL $*"
	exit 1
}
pass() {
	TOTAL=$((TOTAL + 1))
}

COMMON=(-std=c11 -O2 -g -Wall -Wextra -Werror -I "$ROOT/include" -I "$ROOT/jpeg")
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
SRC=("$ROOT/jpeg/n2jpeg.c" "$ROOT/kern/n2vk_ref.c" "$ROOT/kern/n2vk_dispatch.c")

echo "== R1 unit: ASan+UBSan =="
if ! "$CC" "${COMMON[@]}" "${SAN[@]}" -o "$OUT/unit/t_jpeg" "$ROOT/jpeg/test/t_jpeg.c" "${SRC[@]}" -lm \
	>"$OUT/unit/build.log" 2>&1; then
	cat "$OUT/unit/build.log"
	fail "unit build"
fi
"$OUT/unit/t_jpeg" >"$OUT/unit/run.log" 2>&1 || { cat "$OUT/unit/run.log"; fail "unit run"; }
cat "$OUT/unit/run.log"
N=$(awk '/^T_JPEG PASS [0-9]+$/{print $3}' "$OUT/unit/run.log" | tail -n 1)
[ -n "${N:-}" ] || fail "unit output has no T_JPEG PASS line"
TOTAL=$((TOTAL + N))

echo "== R1 unit: plain -O2 (second compiler configuration) =="
if ! "$CC" "${COMMON[@]}" -o "$OUT/unit/t_jpeg_plain" "$ROOT/jpeg/test/t_jpeg.c" "${SRC[@]}" -lm \
	>"$OUT/unit/build_plain.log" 2>&1; then
	cat "$OUT/unit/build_plain.log"
	fail "plain build"
fi
"$OUT/unit/t_jpeg_plain" >"$OUT/unit/run_plain.log" 2>&1 || { cat "$OUT/unit/run_plain.log"; fail "plain run"; }
tail -n 1 "$OUT/unit/run_plain.log"
N=$(awk '/^T_JPEG PASS [0-9]+$/{print $3}' "$OUT/unit/run_plain.log" | tail -n 1)
TOTAL=$((TOTAL + N))

echo "== R1 oracle: build the command-line encoder =="
if ! "$CC" "${COMMON[@]}" -o "$OUT/n2jpeg_cli" "$ROOT/jpeg/test/n2jpeg_cli.c" "${SRC[@]}" -lm \
	>"$OUT/oracle/build.log" 2>&1; then
	cat "$OUT/oracle/build.log"
	fail "cli build"
fi
CLI=$OUT/n2jpeg_cli
ORACLE=$HERE/oracle_check.py
GEN=$ROOT/vectors/gen_yuv.py
[ -f "$GEN" ] || fail "vectors/gen_yuv.py is missing (WP09)"
for tool in djpeg cjpeg ffmpeg python3; do
	command -v "$tool" >/dev/null || fail "oracle tool $tool is not installed"
done

TMP=$OUT/oracle
cd "$TMP"

decode() {   # decode <file.jpg> <tag>
	local file=$1 tag=$2
	djpeg -outfile "$tag.ppm" "$file" >/dev/null 2>"$tag.dj.err" || { cat "$tag.dj.err"; fail "djpeg $file"; }
	[ -s "$tag.dj.err" ] && { cat "$tag.dj.err"; fail "djpeg wrote to stderr for $file"; }
	ffmpeg -v error -y -i "$file" -pix_fmt yuvj420p -f rawvideo "$tag.yuv" >/dev/null 2>"$tag.ff.err" \
		|| { cat "$tag.ff.err"; fail "ffmpeg $file"; }
	[ -s "$tag.ff.err" ] && { cat "$tag.ff.err"; fail "ffmpeg wrote to stderr for $file"; }
	python3 "$ORACLE" structure "$file" >"$tag.structure.json" || fail "structure $file"
	pass
}

psnr_of() {   # psnr_of <source.nv12> <decoded.yuv> <w> <h>
	python3 "$ORACLE" psnr "$1" "$2" "$3" "$4"
}

echo "== R1 oracle: q90 vectors, cjpeg size and PSNR parity =="
# pattern  size     floor_dB   (0 = noise-like content: only parity with cjpeg is required)
VECTORS=(
	"gradient 320x240 38"
	"squares 640x480 38"
	"text 640x480 38"
	"pan 640x480 0"
	"static 178x146 0"
)
for spec in "${VECTORS[@]}"; do
	set -- $spec
	pat=$1 size=$2 floor=$3
	w=${size%x*}; h=${size#*x}
	python3 "$GEN" --pattern "$pat" --size "$size" --frames 1 --format nv12 --out "src_$pat.nv12" --ppm "src_$pat.ppm" >/dev/null
	[ -s "src_$pat.nv12" ] || fail "generator produced no NV12 for $pat"
	"$CLI" --size "$size" --quality 90 --in "src_$pat.nv12" --out "$pat.q90.jpg" >/dev/null || fail "encode $pat q90"
	decode "$pat.q90.jpg" "$pat.q90"
	python3 "$ORACLE" ppmsize "$pat.q90.ppm" "$w" "$h" >/dev/null || fail "PPM size $pat"
	pass

	ours=$(psnr_of "src_$pat.nv12" "$pat.q90.yuv" "$w" "$h" | awk '{print $2}')
	echo "  $pat q90: $ours dB"
	if [ "$floor" != "0" ]; then
		awk -v v="$ours" -v f="$floor" 'BEGIN { exit !(v >= f) }' || fail "PSNR $ours dB below ${floor} dB for $pat"
		pass
	fi

	cjpeg -quality 90 -dct int -baseline -outfile "$pat.cj90.jpg" "src_$pat.ppm" >/dev/null 2>&1 || fail "cjpeg $pat"
	pass
	python3 "$ORACLE" tables "$pat.q90.jpg" "$pat.cj90.jpg" >/dev/null || fail "DQT/DHT differ from cjpeg at q90 for $pat"
	pass
	size_ours=$(stat -c%s "$pat.q90.jpg")
	size_cj=$(stat -c%s "$pat.cj90.jpg")
	echo "  $pat q90 size: ours $size_ours, cjpeg $size_cj, ratio $(awk -v a="$size_ours" -v b="$size_cj" 'BEGIN{printf "%.3f", a/b}')"
	awk -v a="$size_ours" -v b="$size_cj" 'BEGIN { r = a / b; exit !(r <= 1.15 && r >= 0.85) }' \
		|| fail "size ratio $size_ours/$size_cj is outside 15 % for $pat"
	pass

	ffmpeg -v error -y -i "$pat.cj90.jpg" -pix_fmt yuvj420p -f rawvideo "$pat.cj90.yuv" >/dev/null 2>&1 \
		|| fail "ffmpeg could not decode cjpeg's $pat"
	theirs=$(psnr_of "src_$pat.nv12" "$pat.cj90.yuv" "$w" "$h" | awk '{print $2}')
	echo "  $pat q90: cjpeg -dct int gives $theirs dB"
	awk -v a="$ours" -v b="$theirs" 'BEGIN { exit !(a >= b - 0.3) }' || fail "PSNR $ours is more than 0.3 dB below cjpeg's $theirs for $pat"
	pass

	if [ "$floor" = "0" ]; then
		# Noise-like content: q90's 36 dB is the quantisation of the format (cjpeg matches it).  At q100 the only
		# error left is the encoder's own arithmetic; require it to be far below 38 dB, i.e. prove the encoder is
		# not what limits q90.
		"$CLI" --size "$size" --quality 100 --in "src_$pat.nv12" --out "$pat.q100.jpg" >/dev/null || fail "encode $pat q100"
		decode "$pat.q100.jpg" "$pat.q100"
		hi=$(psnr_of "src_$pat.nv12" "$pat.q100.yuv" "$w" "$h" | awk '{print $2}')
		echo "  $pat q100: $hi dB (the encoder's own error)"
		awk -v v="$hi" 'BEGIN { exit !(v >= 45) }' || fail "PSNR $hi dB at q100 is below 45 dB for $pat"
		pass
	fi
done

echo "== R1 oracle: quality sweep on gradient (tables and decoding) =="
for q in 1 25 50 75 95 100; do
	"$CLI" --size 320x240 --quality "$q" --in src_gradient.nv12 --out "gradient.q$q.jpg" >/dev/null || fail "encode q$q"
	decode "gradient.q$q.jpg" "gradient.q$q"
	cjpeg -quality "$q" -dct int -baseline -outfile "gradient.cj$q.jpg" src_gradient.ppm >/dev/null 2>&1 || fail "cjpeg q$q"
	python3 "$ORACLE" tables "gradient.q$q.jpg" "gradient.cj$q.jpg" >/dev/null || fail "DQT/DHT differ from cjpeg at q$q"
	pass
done

echo "== R1 oracle: restart intervals on squares (same picture, same bytes after decoding) =="
"$CLI" --size 640x480 --quality 85 --in src_squares.nv12 --out "squares.r0.jpg" >/dev/null || fail "encode r0"
decode "squares.r0.jpg" "squares.r0"
REF=$(md5sum squares.r0.ppm | awk '{print $1}')
for ri in 1 3 7 100; do
	"$CLI" --size 640x480 --quality 85 --restart "$ri" --in src_squares.nv12 --out "squares.r$ri.jpg" >/dev/null \
		|| fail "encode r$ri"
	decode "squares.r$ri.jpg" "squares.r$ri"
	SUM=$(md5sum "squares.r$ri.ppm" | awk '{print $1}')
	[ "$SUM" = "$REF" ] || fail "restart interval $ri changed the decoded picture"
	pass
	python3 "$ORACLE" structure "squares.r$ri.jpg" | grep -q "\"restart_interval\": $ri" \
		|| fail "DRI does not say $ri"
	pass
done

echo "== R1 oracle: APP1 (EXIF-like) and COM segments =="
python3 - "exif.bin" <<'PY'
import sys
payload = b"Exif\0\0" + b"MM\0*" + (8).to_bytes(4, "big") + b"\x00\x00" + (0).to_bytes(4, "big")
open(sys.argv[1], "wb").write(payload)
PY
printf 'n2jpeg comment payload' > com.bin
"$CLI" --size 320x240 --quality 90 --app 225:exif.bin --app 254:com.bin --in src_gradient.nv12 \
	--out gradient.segments.jpg >/dev/null || fail "encode with APP1/COM"
decode "gradient.segments.jpg" "gradient.segments"
python3 - "gradient.segments.jpg" "exif.bin" "com.bin" <<'PY' || fail "APP1/COM payloads are not in the file"
import sys
data = open(sys.argv[1], "rb").read()
exif = open(sys.argv[2], "rb").read()
com = open(sys.argv[3], "rb").read()
ok = (b"\xff\xe1" + bytes([(len(exif) + 2) >> 8, (len(exif) + 2) & 255]) + exif) in data
ok = ok and (b"\xff\xfe" + bytes([(len(com) + 2) >> 8, (len(com) + 2) & 255]) + com) in data
ok = ok and data.count(b"JFIF\0") == 1
sys.exit(0 if ok else 1)
PY
pass

echo "== R1 oracle: 1280x720 gradient at q90 =="
python3 "$GEN" --pattern gradient --size 1280x720 --frames 1 --format nv12 --out big.nv12 >/dev/null
start=$(date +%s%N)
"$CLI" --size 1280x720 --quality 90 --in big.nv12 --out big.q90.jpg >/dev/null || fail "encode 1280x720"
end=$(date +%s%N)
echo "  1280x720 q90: $(( (end - start) / 1000000 )) ms, $(stat -c%s big.q90.jpg) bytes"
decode "big.q90.jpg" "big.q90"
big=$(psnr_of big.nv12 big.q90.yuv 1280 720 | awk '{print $2}')
echo "  1280x720 q90: $big dB"
awk -v v="$big" 'BEGIN { exit !(v >= 38) }' || fail "PSNR $big dB below 38 dB at 1280x720"
pass

echo "== R1 oracle: short input is refused, not read past =="
head -c 1000 src_gradient.nv12 > short.nv12
if "$CLI" --size 320x240 --in short.nv12 --out short.jpg >/dev/null 2>&1; then
	fail "the CLI accepted a truncated NV12 file"
fi
pass

echo "N2TEST R1 PASS $TOTAL/$TOTAL"
