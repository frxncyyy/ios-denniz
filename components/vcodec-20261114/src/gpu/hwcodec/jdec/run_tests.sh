#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# J04 host test: parser + register words for the MT6893 hybrid JPEG decoder.
# Everything is built and generated under /tmp/n2hw-J04; nothing is written into the tree.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
out=/tmp/n2hw-J04/test
cc=${CC:-clang}

fail() {
	echo "N2TEST J04 FAIL $*"
	exit 1
}

check_grep() {
	grep -q "$1" "$2" || fail "$3"
}

rm -rf "$out"
mkdir -p "$out/corpus" "$out/bin"

echo "== generating the JPEG corpus (cjpeg/ffmpeg) and the djpeg/ffprobe expectations =="
python3 "$here/gen_jpegs.py" "$out/corpus" || fail "corpus generation"

common="-std=c11 -g -Wall -Wextra -Werror -I $root/include -I $here"
san="$common -O1 -fsanitize=address,undefined -fno-sanitize-recover=all"
opt="$common -O2"

echo "== building (ASan+UBSan -O1 and -O2, two jobs) =="
nice -n 10 $cc $san "$here/n2jdec_parse.c" "$here/n2jdec_words.c" "$here/t_jdec.c" \
	-o "$out/bin/t_jdec_asan" &
p1=$!
nice -n 10 $cc $opt "$here/n2jdec_parse.c" "$here/n2jdec_words.c" "$here/t_jdec.c" \
	-o "$out/bin/t_jdec_o2" &
p2=$!
wait $p1 || fail "build ASan+UBSan"
wait $p2 || fail "build -O2"
nice -n 10 $cc $san "$here/n2jdec_parse.c" "$here/n2jdec_words.c" "$here/n2jdec_tool.c" \
	-o "$out/bin/n2jdec_tool" || fail "build tool"

echo "== t_jdec ASan+UBSan =="
"$out/bin/t_jdec_asan" corpus "$out/corpus/corpus.txt" "$out/corpus" > "$out/asan.log" 2>&1 \
	|| { cat "$out/asan.log"; fail "t_jdec ASan+UBSan"; }
cat "$out/asan.log"
n1=$(sed -n 's/^T_JDEC checks=\([0-9]*\) failures=0$/\1/p' "$out/asan.log")
f1=$(sed -n 's/^T_JDEC checks=[0-9]* failures=\([0-9]*\)$/\1/p' "$out/asan.log")
[ -n "$n1" ] && [ "${f1:-1}" = "0" ] || fail "t_jdec ASan+UBSan result"

echo "== t_jdec -O2 =="
"$out/bin/t_jdec_o2" corpus "$out/corpus/corpus.txt" "$out/corpus" > "$out/o2.log" 2>&1 \
	|| { cat "$out/o2.log"; fail "t_jdec -O2"; }
cat "$out/o2.log"
n2=$(sed -n 's/^T_JDEC checks=\([0-9]*\) failures=0$/\1/p' "$out/o2.log")
f2=$(sed -n 's/^T_JDEC checks=[0-9]* failures=\([0-9]*\)$/\1/p' "$out/o2.log")
[ -n "$n2" ] && [ "${f2:-1}" = "0" ] || fail "t_jdec -O2 result"

echo "== n2jdec_tool words (hand420, fixed addresses) =="
"$out/bin/n2jdec_tool" words "$out/corpus/hand420.jpg" \
	--src-pa 0xd8000000 --out-y 0xd8800000 --out-c 0xd8830000 \
	--index-pa 0xd8f00000 --index-size 0x4000 > "$out/tool.log" 2>&1 \
	|| { cat "$out/tool.log"; fail "tool words"; }
check_grep '^N2JDEC data\[02\]=0x00000500 ' "$out/tool.log" "tool data[2]"
check_grep '^N2JDEC data\[08\]=0x01020300 ' "$out/tool.log" "tool data[8]"
check_grep '^N2JDEC data\[11\]=0x7fff5924 ' "$out/tool.log" "tool data[11]"
sz=$(stat -c %s "$out/corpus/hand420.jpg")
al=$(( (sz + 127) / 128 * 128 ))
check_grep "^N2JDEC data\[12\]=$(printf '0x%08x' "$al") " "$out/tool.log" "tool data[12]"
check_grep '^N2JDEC index 0x338=0xd8f00000 0x36c=0xd8f00000 0x370=0xd8f04000 0x374=0xd8f08000$' \
	"$out/tool.log" "tool index pointers"
check_grep '^N2JDEC unknown_words=13$' "$out/tool.log" "tool unknown count"
check_grep '^N2JDEC words_supported=1$' "$out/tool.log" "tool supported"
check_grep '^N2JDEC unknown off=0x330 slot=15 value=0x00000020 ' "$out/tool.log" "tool 0x330"
check_grep '^N2JDEC unknown off=0x334 slot=16 value=0x00000010 ' "$out/tool.log" "tool 0x334"
n3=9

echo "== n2jdec_tool refuses progressive and unsupported files =="
if "$out/bin/n2jdec_tool" info "$out/corpus/progressive.jpg" > "$out/prog.log" 2>&1; then
	fail "progressive file accepted"
fi
check_grep 'not a baseline' "$out/prog.log" "progressive message"
if "$out/bin/n2jdec_tool" words "$out/corpus/c_411_17x19.jpg" --src-pa 0xd8000000 \
	--out-y 0xd8800000 --out-c 0xd8830000 --index-pa 0xd8f00000 > "$out/411.log" 2>&1; then
	fail "4:1:1 accepted for words"
fi
check_grep 'not supported' "$out/411.log" "411 message"
n3=$((n3 + 4))

cross="$root/../video/build/R1/oracle"
if [ -d "$cross" ]; then
	echo "== optional read-only cross-check against real JPEGs in gpu/video/build/R1/oracle =="
	ccout=$(python3 "$here/crosscheck_video.py" "$out/bin/n2jdec_tool" "$cross") \
		|| { echo "$ccout"; fail "external JPEG cross-check"; }
	echo "$ccout"
	nc=$(echo "$ccout" | sed -n 's/^N2JDEC crosscheck \([0-9]*\)\/\([0-9]*\)$/\1/p')
	[ -n "$nc" ] || fail "cross-check output"
	# Not added to the counted total: these files belong to another package and may disappear.
else
	echo "== external JPEG oracle absent: cross-check skipped (not counted) =="
fi

echo "== freestanding compile (no libc, kernel-style) =="
nice -n 10 $cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-I "$here" -c "$here/n2jdec_parse.c" -o "$out/bin/parse_free.o" || fail "freestanding parse"
nice -n 10 $cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-I "$here" -c "$here/n2jdec_words.c" -o "$out/bin/words_free.o" || fail "freestanding words"
n3=$((n3 + 2))

sdk="$root/../build/apple-toolchain/sdks/iPhoneOS16.5.sdk"
if [ -d "$sdk" ]; then
	echo "== arm64-apple-ios16.0 compile check (compiled, not run) =="
	for f in n2jdec_parse.c n2jdec_words.c n2jdec_tool.c; do
		nice -n 10 $cc -target arm64-apple-ios16.0 -isysroot "$sdk" -std=c11 -O2 \
			-Wall -Wextra -Werror -I "$here" -c "$here/$f" \
			-o "$out/bin/${f%.c}_ios.o" || fail "iOS compile $f"
		n3=$((n3 + 1))
	done
else
	echo "== iOS SDK absent: compile check skipped (not counted) =="
fi

echo "== sizes =="
du -sh "$out"
total=$((n1 + n2 + n3))
echo "N2TEST J04 PASS $total/$total"
