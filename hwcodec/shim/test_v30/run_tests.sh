#!/bin/bash
# V30 host tests: string/memory shims, printf/scanf, the guest heap, qsort, mmap.
# Builds only under /tmp/n2hw-v30.  Last line on success: N2TEST V30 PASS <passed>/<total>
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$H/.." && pwd)
W=$(cd "$S/.." && pwd)
OUT=/tmp/n2hw-v30
rm -rf "$OUT"
mkdir -p "$OUT"

SRC="$S/n2shim_format.c $S/n2shim_string.c $S/n2shim_heap.c \
     $H/t_support.c $H/t_main.c $H/t_string.c $H/t_format.c $H/t_fuzz.c $H/t_heap.c $H/t_qsort.c $H/t_mmap.c"
INC="-I$W/include -I$S -I$H"
# -Wno-format-nonliteral/-Wno-format-security: the differential test deliberately formats with a runtime string
# (the format comes from the case table), which is the whole point of the test.
CFLAGS="-std=c11 -Wall -Wextra -Werror -Wno-format-nonliteral -Wno-format-security $INC"

run() {
	local name=$1
	shift
	if ! nice -n 10 "$@" > "$OUT/$name.log" 2>&1; then
		echo "N2TEST V30 FAIL $name"
		tail -n 30 "$OUT/$name.log"
		exit 1
	fi
	tail -n 1 "$OUT/$name.log"
}

echo "== build: ASan+UBSan -O1"
nice -n 10 clang $CFLAGS -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	$SRC -o "$OUT/t_asan" -lpthread -lm
run asan "$OUT/t_asan"

echo "== build: -O2 (no sanitizers)"
nice -n 10 clang $CFLAGS -O2 $SRC -o "$OUT/t_o2" -lpthread -lm
run o2 "$OUT/t_o2"

echo "== build: TSan -O1 (four guest heap threads)"
nice -n 10 clang $CFLAGS -O1 -g -fsanitize=thread $SRC -o "$OUT/t_tsan" -lpthread -lm
run tsan "$OUT/t_tsan"

echo "== build: -O2 without V33's n2vfs_mmap hook (weak-default path)"
nice -n 10 clang $CFLAGS -O2 -DV30_NO_VFS $SRC -o "$OUT/t_novfs" -lpthread -lm
run novfs "$OUT/t_novfs"

echo "== real-core qsort comparator (V02's interpreter + llvm-mc Thumb code)"
# The comparator is embedded in t_qsort_core.c; re-assemble it and require the exact same bytes.
cat > "$OUT/cmp.s" <<'EOF'
.syntax unified
.thumb
.text
.global cmp_u32
.thumb_func
cmp_u32:
    ldr r2, [r0]
    ldr r3, [r1]
    cmp r2, r3
    blt 1f
    bgt 2f
    movs r0, #0
    bx lr
1:  movs r0, #1
    rsbs r0, r0, #0
    bx lr
2:  movs r0, #1
    bx lr
EOF
nice -n 10 llvm-mc -triple=thumbv8a -mattr=+neon,+vfp4 -filetype=obj "$OUT/cmp.s" -o "$OUT/cmp.o"
nice -n 10 llvm-objcopy -O binary --only-section=.text "$OUT/cmp.o" "$OUT/cmp.bin"
python3 - "$H/t_qsort_core.c" "$OUT/cmp.bin" <<'PYEOF'
import re, sys
src = open(sys.argv[1]).read()
m = re.search(r'CMP_U32\[\] = \{(.*?)\};', src, re.S)
if not m:
    sys.exit("run_tests: cannot find CMP_U32 in t_qsort_core.c")
embedded = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1)))
assembled = open(sys.argv[2], 'rb').read()
if embedded != assembled:
    sys.exit("run_tests: embedded comparator differs from llvm-mc output (%d vs %d bytes)" % (len(embedded), len(assembled)))
PYEOF
# V02's core needs POSIX (sigaction/sigjmp_buf): -std=gnu11 applies only to this binary.
nice -n 10 clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -I"$W/include" -I"$S" -I"$H" \
	"$H/t_qsort_core.c" \
	"$W/emu/n2a32_core.c" "$W/emu/n2a32_mem.c" "$W/emu/n2a32_excl.c" "$W/emu/n2a32_stub_classes.c" "$W/emu/n2a32_t16.c" \
	"$S/n2shim_string.c" "$S/n2shim_format.c" "$S/n2shim_heap.c" \
	-o "$OUT/t_qsort_core" -lpthread -lm
run core "$OUT/t_qsort_core"

# Every build must have run the same number of checks; a difference means a check was compiled out.
res_asan=$(tail -n 1 "$OUT/asan.log" | sed -n 's/V30RESULT \([0-9]*\/[0-9]*\)/\1/p')
res_o2=$(tail -n 1 "$OUT/o2.log" | sed -n 's/V30RESULT \([0-9]*\/[0-9]*\)/\1/p')
res_tsan=$(tail -n 1 "$OUT/tsan.log" | sed -n 's/V30RESULT \([0-9]*\/[0-9]*\)/\1/p')
res_no=$(tail -n 1 "$OUT/novfs.log" | sed -n 's/V30RESULT \([0-9]*\/[0-9]*\)/\1/p')
if [ -z "$res_o2" ] || [ "$res_asan" != "$res_o2" ] || [ "$res_tsan" != "$res_o2" ] || [ "$res_no" != "$res_o2" ]; then
	echo "N2TEST V30 FAIL check counts differ: asan=$res_asan o2=$res_o2 tsan=$res_tsan novfs=$res_no"
	exit 1
fi
p=${res_o2%/*}
t=${res_o2#*/}
echo "N2TEST V30 PASS $p/$t"
