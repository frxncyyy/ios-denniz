#!/bin/bash
# SPDX-License-Identifier: MIT
# WP09 host test: regenerate the quick vectors in a temporary directory and
# check every hash, every decoded frame md5, the embedded header, and the
# size budget.  Independent literals for gen_yuv.py are in check_gen_yuv.py.
#
#   bash gpu/video/vectors/check_vectors.sh [outdir]
#
# Last line on success: N2TEST WP09 PASS <passed>/<total>
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
video="$(cd "$here/.." && pwd)"
outdir="${1:-$video/build/WP09}"
tmp="$outdir/tmp/check.$$"
keep="${KEEP:-0}"
pass=0
total=0

pass_check() {
    pass=$((pass + 1))
    total=$((total + 1))
}

fail() {
    echo "N2TEST WP09 FAIL $*" >&2
    exit 1
}

cleanup() {
    if [ "$keep" = "1" ]; then
        echo "check_vectors: kept $tmp" >&2
    else
        rm -rf "$tmp"
    fi
}
trap cleanup EXIT

mkdir -p "$tmp/gen" "$tmp/emb"
echo "check_vectors: temporary directory $tmp"

# ---------------------------------------------------------------- A. gen_yuv
if ! res="$(python3 "$here/check_gen_yuv.py" "$tmp/gen")"; then
    fail "gen_yuv.py literal checks"
fi
n="$(printf '%s\n' "$res" | sed -n 's/^CHECKS \([0-9][0-9]*\)$/\1/p' | tail -1)"
[ -n "$n" ] || fail "check_gen_yuv.py printed no CHECKS line"
pass=$((pass + n))
total=$((total + n))
echo "ok: gen_yuv.py literals ($n checks)"

# ------------------------------------------------- B. regenerate 'quick' once
if ! bash "$here/make_vectors.sh" quick --out "$tmp/data" \
        --manifest "$tmp/manifest.json" > "$tmp/make1.log" 2>&1; then
    cat "$tmp/make1.log" >&2
    fail "make_vectors.sh quick (regeneration 1)"
fi
pass_check
echo "ok: make_vectors.sh quick regenerated into $tmp/data"

if ! res="$(python3 "$here/verify_manifest.py" --manifest "$here/manifest.json" \
        --new "$tmp/manifest.json" --data "$tmp/data")"; then
    fail "manifest and stream verification"
fi
n="$(printf '%s\n' "$res" | sed -n 's/^CHECKS \([0-9][0-9]*\)$/\1/p' | tail -1)"
[ -n "$n" ] || fail "verify_manifest.py printed no CHECKS line"
pass=$((pass + n))
total=$((total + n))
echo "ok: manifest byte-identical, all raw/stream sha256 and md5 ($n checks)"

# ------------------------------------------------------------ C. embedded data
if ! clang -std=c11 -O1 -g -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=undefined -I "$here" \
        "$here/test/dump_embedded.c" -o "$tmp/dump_embedded" 2> "$tmp/clang.log"; then
    cat "$tmp/clang.log" >&2
    fail "embedded_vectors.h does not compile cleanly"
fi
pass_check
echo "ok: embedded_vectors.h compiles with -Wall -Wextra -Werror (included twice)"

"$tmp/dump_embedded" "$tmp/emb" || fail "dump_embedded"
pass_check
info="$tmp/emb/info.txt"

[ "$(sed -n 's/^count=//p' "$info")" = "4" ] || fail "n2vec_count() is not 4"
pass_check
[ "$(sed -n 's/^streams=//p' "$info")" = "2" ] || fail "n2vec_stream_count() is not 2"
pass_check
[ "$(sed -n 's/^images=//p' "$info")" = "2" ] || fail "n2vec_image_count() is not 2"
pass_check
[ "$(sed -n 's/^h264_codec=//p' "$info")" = "61766331" ] || fail "H.264 stream codec is not avc1"
[ "$(sed -n 's/^hevc_codec=//p' "$info")" = "68766331" ] || fail "HEVC stream codec is not hvc1"
[ "$(sed -n 's/^h264_ps=//p' "$info")" = "2" ] && \
    [ "$(sed -n 's/^h264_nal=//p' "$info")" = "4" ] && \
    [ "$(sed -n 's/^hevc_ps=//p' "$info")" = "3" ] && \
    [ "$(sed -n 's/^hevc_nal=//p' "$info")" = "4" ] || fail "stream parameter-set/nal-length description"
pass_check
data_bytes="$(sed -n 's/^data_bytes=//p' "$info")"
[ -n "$data_bytes" ] && [ "$data_bytes" -lt 49152 ] || fail "embedded data ${data_bytes:-?} B is not under 48 KiB"
pass_check
echo "ok: embedded data is $data_bytes B (under 49152)"

h264_frames="$(sed -n 's/^h264_frames=//p' "$info")"
hevc_frames="$(sed -n 's/^hevc_frames=//p' "$info")"
h264_md5="$(sed -n 's/^h264_md5=//p' "$info")"
hevc_md5="$(sed -n 's/^hevc_md5=//p' "$info")"
[ "$(sed -n 's/^h264_keyframe0=//p' "$info")" = "1" ] || fail "H.264 frame 0 is not marked keyframe"
pass_check
[ "$(sed -n 's/^hevc_keyframe0=//p' "$info")" = "1" ] || fail "HEVC frame 0 is not marked keyframe"
pass_check

# Reconstructed Annex B from the embedded AVCC units must decode, with the
# stored frame count and the stored first-frame luma md5.
ffmpeg -v error -y -i "$tmp/emb/h264.264" -f rawvideo -pix_fmt yuv420p \
    "$tmp/emb/h264-all.yuv" 2> "$tmp/h264.log" || { cat "$tmp/h264.log" >&2; fail "embedded H.264 stream does not decode"; }
pass_check
[ "$(( $(stat -c %s "$tmp/emb/h264-all.yuv") / (176 * 144 * 3 / 2) ))" = "$h264_frames" ] || fail "embedded H.264 frame count"
pass_check
ffmpeg -v error -y -i "$tmp/emb/h264.264" -frames:v 1 -f rawvideo -pix_fmt yuv420p \
    "$tmp/emb/h264-first.yuv" 2>/dev/null || fail "embedded H.264 first frame"
luma="$(head -c $((176 * 144)) "$tmp/emb/h264-first.yuv" | md5sum | cut -d' ' -f1)"
[ "$luma" = "$h264_md5" ] || fail "embedded H.264 luma md5 ($luma != $h264_md5)"
pass_check

ffmpeg -v error -y -i "$tmp/emb/hevc.265" -f rawvideo -pix_fmt yuv420p \
    "$tmp/emb/hevc-all.yuv" 2> "$tmp/hevc.log" || { cat "$tmp/hevc.log" >&2; fail "embedded HEVC stream does not decode"; }
pass_check
[ "$(( $(stat -c %s "$tmp/emb/hevc-all.yuv") / (176 * 144 * 3 / 2) ))" = "$hevc_frames" ] || fail "embedded HEVC frame count"
pass_check
ffmpeg -v error -y -i "$tmp/emb/hevc.265" -frames:v 1 -f rawvideo -pix_fmt yuv420p \
    "$tmp/emb/hevc-first.yuv" 2>/dev/null || fail "embedded HEVC first frame"
luma="$(head -c $((176 * 144)) "$tmp/emb/hevc-first.yuv" | md5sum | cut -d' ' -f1)"
[ "$luma" = "$hevc_md5" ] || fail "embedded HEVC luma md5 ($luma != $hevc_md5)"
pass_check

for img in jpeg.jpg heic.heic; do
    dims="$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height \
        -of csv=p=0 "$tmp/emb/$img" 2>/dev/null | head -1)"
    [ "$dims" = "176,144" ] || fail "embedded $img dimensions ($dims)"
    ffmpeg -v error -y -i "$tmp/emb/$img" -frames:v 1 -f rawvideo -pix_fmt yuv420p \
        "$tmp/emb/$img.yuv" 2>/dev/null || fail "embedded $img does not decode"
    pass_check
    echo "ok: embedded $img decodes at $dims"
done

# The committed header must be exactly what the regenerated streams produce.
if ! python3 "$here/make_embedded.py" --data "$tmp/data" --manifest "$tmp/manifest.json" \
        --out "$tmp/embedded_vectors.h" > "$tmp/embedded.log" 2>&1; then
    cat "$tmp/embedded.log" >&2
    fail "make_embedded.py generation"
fi
cmp "$tmp/embedded_vectors.h" "$here/embedded_vectors.h" || fail "embedded_vectors.h is not reproducible"
pass_check
echo "ok: embedded_vectors.h is byte-identical to the regenerated one"

if ! python3 "$here/make_embedded.py" --verify --data "$tmp/data" \
        --manifest "$tmp/manifest.json" --out "$here/embedded_vectors.h" \
        > "$tmp/verify.log" 2>&1; then
    cat "$tmp/verify.log" >&2
    fail "make_embedded.py --verify"
fi
pass_check
echo "ok: $(cat "$tmp/verify.log")"

# ------------------------------------- D. regenerate a second time and compare
rm -rf "$tmp/data"
if ! bash "$here/make_vectors.sh" quick --out "$tmp/data2" \
        --manifest "$tmp/manifest2.json" > "$tmp/make2.log" 2>&1; then
    cat "$tmp/make2.log" >&2
    fail "make_vectors.sh quick (regeneration 2)"
fi
cmp "$tmp/manifest.json" "$tmp/manifest2.json" || fail "two regenerations differ"
pass_check
echo "ok: two regenerations in a row produce the same manifest (and hashes)"

# --------------------------------------------------------------- E. size cap
bytes="$(du -sb "$here" | cut -f1)"
[ "$bytes" -lt 40000000 ] || fail "vectors/ is $bytes bytes, not under 40000000"
pass_check
echo "ok: whole vectors/ directory is $bytes bytes (under 40000000)"

echo "N2TEST WP09 PASS $pass/$total"
