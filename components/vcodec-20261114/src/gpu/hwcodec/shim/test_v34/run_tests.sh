#!/bin/bash
# V34 host tests: the Android-runtime shim table (log, properties, atrace, ion, dl*, math, startup, data imports).
#
# Builds only under /tmp/n2hw-v34.  Runs:
#   t_unit   ASan+UBSan -O1, then plain -O2, then TSan; every check count must agree
#   t_core   ASan+UBSan -O1 against V03's real loader and the phone's libvcodec_utility.so
# The property-key list is produced with the real `strings` from the five vendor objects.
# Compiles run two at a time (-j2) and nice -n 10.
# Last line on success: N2TEST V34 PASS <passed>/<total>
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$H/.." && pwd)
W=$(cd "$S/.." && pwd)
ROOT=$(cd "$W/../.." && pwd)
OUT=/tmp/n2hw-v34
VENDOR=${N2HW_VENDOR_DIR:-$ROOT/gpu/build/cam-vendor-20261109/vfs}
OBJ="$VENDOR/bin/vpud $VENDOR/lib/libvpud_vcodec.so $VENDOR/lib/libvcodecdrv.so \
     $VENDOR/lib/libvcodec_utility.so $VENDOR/lib/libvcodec_oal.so"
rm -rf "$OUT"
mkdir -p "$OUT"

INC="-I$W/include -I$S -I$H -I$OUT"
WARN="-Wall -Wextra -Werror"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"

SHIM="$S/n2shim_android.c $S/n2shim_props.c $S/n2shim_format.c"
CORE="$W/emu/n2a32_core.c $W/emu/n2a32_mem.c $W/emu/n2a32_excl.c $W/emu/n2a32_stub_classes.c $W/emu/n2a32_t16.c"
TEST="$H/t_support.c $H/t_unit.c"

fail() {
	echo "N2TEST V34 FAIL $1"
	exit 1
}

# compile_two <lang-flag> <extra-flags> <outdir> <sources...>   -- two clang jobs at a time
compile_two() {
	local lang=$1
	local extra=$2
	local o=$3
	shift 3
	local n=0 rc=0
	mkdir -p "$o"
	for f in "$@"; do
		nice -n 10 clang $lang $WARN $INC $extra -c "$f" -o "$o/$(basename "$f" .c).o" &
		n=$((n + 1))
		if [ $((n % 2)) -eq 0 ]; then
			if ! wait -n; then rc=1; fi
		fi
	done
	if ! wait; then rc=1; fi
	return $rc
}

# ---- 1. guest Thumb code (llvm-mc), symbol offsets into a C header ------------------------------------------------
llvm-mc -triple=thumbv8a -filetype=obj "$H/guest_thumb.s" -o "$OUT/guest_thumb.o" \
	|| fail llvm-mc
llvm-objcopy -O binary --only-section=.text "$OUT/guest_thumb.o" "$OUT/guest_thumb.bin" \
	|| fail llvm-objcopy
python3 "$H/gen_blob.py" "$OUT/guest_thumb.o" "$OUT/guest_thumb.bin" > "$OUT/t_guest_blob.h" \
	|| fail gen_blob

# ---- 2. the property keys the vendor objects really contain, via the real `strings` --------------------------------
for f in $OBJ; do
	if [ ! -f "$f" ]; then
		fail "vendor-object-missing $f"
	fi
	strings -a "$f" || fail strings
done | grep -E '^(ro|persist|vendor)\.[A-Za-z0-9_.-]+$' | sort -u > "$OUT/vendor_keys.txt" || true
keys=$(wc -l < "$OUT/vendor_keys.txt")
if [ "$keys" -lt 200 ]; then
	fail "property-key-scan produced only $keys keys"
fi

# ---- 3. unit tests: ASan+UBSan -O1, -O2, TSan ---------------------------------------------------------------------
echo "== build: unit, ASan+UBSan -O1"
compile_two "-std=c11" "-O1 -g $SAN" "$OUT/obj-asan" $SHIM $TEST
compile_two "-std=gnu11" "-O1 -g $SAN" "$OUT/obj-asan" $CORE
nice -n 10 clang $SAN "$OUT"/obj-asan/*.o -o "$OUT/t_unit_asan" -lpthread -lm || fail link-unit-asan
echo "== run: unit, ASan+UBSan"
nice -n 10 "$OUT/t_unit_asan" "$OUT/vendor_keys.txt" > "$OUT/unit_asan.log" 2>&1 \
	|| { tail -n 25 "$OUT/unit_asan.log"; fail unit-asan; }
tail -n 2 "$OUT/unit_asan.log"

echo "== build: unit, -O2 (no sanitizers)"
compile_two "-std=c11" "-O2" "$OUT/obj-o2" $SHIM $TEST
compile_two "-std=gnu11" "-O2" "$OUT/obj-o2" $CORE
nice -n 10 clang "$OUT"/obj-o2/*.o -o "$OUT/t_unit_o2" -lpthread -lm || fail link-unit-o2
echo "== run: unit, -O2"
nice -n 10 "$OUT/t_unit_o2" "$OUT/vendor_keys.txt" > "$OUT/unit_o2.log" 2>&1 \
	|| { tail -n 25 "$OUT/unit_o2.log"; fail unit-o2; }
tail -n 2 "$OUT/unit_o2.log"

echo "== build: unit, TSan -O1 (four guest threads; the deliberate fault test is compiled out)"
compile_two "-std=c11" "-O1 -g -fsanitize=thread -DT_V34_NO_FAULT" "$OUT/obj-tsan" $SHIM $TEST
compile_two "-std=gnu11" "-O1 -g -fsanitize=thread -DT_V34_NO_FAULT" "$OUT/obj-tsan" $CORE
nice -n 10 clang -fsanitize=thread "$OUT"/obj-tsan/*.o -o "$OUT/t_unit_tsan" -lpthread -lm || fail link-unit-tsan
echo "== run: unit, TSan"
nice -n 10 "$OUT/t_unit_tsan" "$OUT/vendor_keys.txt" > "$OUT/unit_tsan.log" 2>&1 \
	|| { tail -n 40 "$OUT/unit_tsan.log"; fail unit-tsan; }
tail -n 2 "$OUT/unit_tsan.log"

# every build exercises the same suite (the TSan one minus the fault case)
res_asan=$(sed -n 's/^T34RESULT \([0-9]*\/[0-9]*\)$/\1/p' "$OUT/unit_asan.log")
res_o2=$(sed -n 's/^T34RESULT \([0-9]*\/[0-9]*\)$/\1/p' "$OUT/unit_o2.log")
res_tsan=$(sed -n 's/^T34RESULT \([0-9]*\/[0-9]*\)$/\1/p' "$OUT/unit_tsan.log")
if [ -z "$res_asan" ] || [ "$res_asan" != "$res_o2" ]; then
	fail "check counts differ: asan=$res_asan o2=$res_o2"
fi
if [ -z "$res_tsan" ]; then
	fail "tsan build produced no result line"
fi

# ---- 4. the real loader + the phone's libvcodec_utility.so --------------------------------------------------------
echo "== build: core, ASan+UBSan -O1 (V03 loader + real vendor object)"
compile_two "-std=c11" "-O1 -g $SAN" "$OUT/obj-core" $SHIM "$H/t_support.c" "$H/t_core.c"
compile_two "-std=gnu11" "-O1 -g $SAN" "$OUT/obj-core" $CORE "$W/loader/n2elf.c" "$W/loader/n2elf_reloc.c"
nice -n 10 clang $SAN "$OUT"/obj-core/*.o -o "$OUT/t_core" -lpthread -lm || fail link-core

echo "== run: core against $VENDOR"
N2HW_VENDOR_DIR="$VENDOR" ASAN_OPTIONS=detect_leaks=0 nice -n 10 "$OUT/t_core" > "$OUT/core.log" 2>&1 \
	|| { tail -n 25 "$OUT/core.log"; fail core; }
tail -n 2 "$OUT/core.log"

passed=${res_asan%/*}
total=${res_asan#*/}
echo "N2TEST V34 PASS $passed/$total"
