#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# DEC04 host tests: rev-3 whole-VDEC register window + guest-store write-combining.
#
#   bash gpu/hwcodec/vcu/test_dec04/run_tests.sh
#
# What it does, and why exactly this:
#   1. copies the small source set (include, vcu, tests/hw, emu) into /tmp/n2hw-DEC04/mirror;
#   2. stages the proposed redwood_vcodec.h rev-3 enum line *in the mirror only* when the tree
#      header is still rev 2 (DEC04's patch uses RWVC_WIN_VDEC_ALL, the real ABI name, never a
#      literal 12; the tree header is hw-lead's and is not touched);
#   3. applies dec04.patch (vcu/n2vcu.c + n2vcu.h) and dec04-v40-adapt.patch (V40's own t_v40.c
#      needs the wider window in its in-file fake and one expectation changed: 0x16001000 is a
#      real VDEC page after rev 3, so it is no longer skipped -- see the board post to hw-v40);
#   4. builds and runs t_dec04.c three times (ASan+UBSan -O1, plain -O2, TSan -O1);
#   5. runs V40's untouched runner on the patched copy in the mirror
#      (N2HW_TMP and N2HW_KERNEL are passed through; its last line is parsed).
# Last line: N2TEST DEC04 PASS <passed>/<total> or N2TEST DEC04 FAIL <what>.
set -euo pipefail
trap 'echo "N2TEST DEC04 FAIL unexpected failure (line $LINENO)" >&2' ERR

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
tmp=${N2HW_TMP:-/tmp/n2hw-DEC04}
mkdir -p "$tmp"
tmp=$(cd "$tmp" && pwd)
mirror=$tmp/mirror
warn="-std=c11 -Wall -Wextra -Werror"

if ! command -v patch >/dev/null 2>&1; then
	echo "N2TEST DEC04 FAIL patch(1) not found" >&2
	exit 1
fi

# ---- 1. mirror --------------------------------------------------------------------------------
rm -rf "$mirror"
mkdir -p "$mirror/gpu/hwcodec/tests"
cp -a "$root/gpu/hwcodec/include" "$mirror/gpu/hwcodec/"
cp -a "$root/gpu/hwcodec/vcu" "$mirror/gpu/hwcodec/"
cp -a "$root/gpu/hwcodec/emu" "$mirror/gpu/hwcodec/"
cp -a "$root/gpu/hwcodec/tests/hw" "$mirror/gpu/hwcodec/tests/"
hw=$mirror/gpu/hwcodec

# ---- 2. stage the rev-3 header line in the mirror --------------------------------------------
python3 - "$hw/include/redwood_vcodec.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
if 'RWVC_WIN_VDEC_ALL' in s:
    print("DEC04 staging: tree header already has RWVC_WIN_VDEC_ALL")
else:
    old = "RWVC_WIN_VDEC_SOC_GCON, RWVC_WIN_COUNT };"
    new = "RWVC_WIN_VDEC_SOC_GCON, RWVC_WIN_VDEC_ALL, RWVC_WIN_COUNT };"
    if old not in s:
        sys.exit("DEC04 staging: cannot find the window enum to extend")
    s = s.replace(old, new, 1)
    if "RWVC_WIN_VDEC_ALL: 0x16000000+0x30000" not in s:
        s = s.replace("VDEC_SOC_GCON 0x1600f000 (0x1000 each unless given)",
                      "VDEC_SOC_GCON 0x1600f000 (0x1000 each unless given); "
                      "rev 3 (DEC04): VDEC_ALL 0x16000000+0x30000", 1)
    open(p, "w").write(s)
    print("DEC04 staging: header rev 3 line added in the /tmp mirror only (tree header is rev 2)")
PY

# ---- 3. apply the two patches (unless the owner already merged them) --------------------------
prod_applied=0
if grep -q 'RWVC_WIN_VDEC_ALL' "$hw/vcu/n2vcu.c" &&
   grep -q 'n2vcu_set_write_combining' "$hw/vcu/n2vcu.c"; then
	prod_applied=1
	echo "DEC04: production patch already in the tree (vcu/n2vcu.c), not re-applying"
fi
if [ "$prod_applied" -eq 0 ]; then
	if ! patch -p1 -s -d "$mirror" < "$here/dec04.patch" 2>"$tmp/patch1.err"; then
		cat "$tmp/patch1.err" >&2
		echo "N2TEST DEC04 FAIL dec04.patch does not apply (did V40 change n2vcu.c/n2vcu.h?)" >&2
		exit 1
	fi
fi
adapt_applied=0
if grep -q 'FHW_WIN_WORDS 0xc000u' "$hw/vcu/test_v40/t_v40.c"; then
	adapt_applied=1
	echo "DEC04: V40 test adaptation already in the tree (t_v40.c), not re-applying"
fi
if [ "$adapt_applied" -eq 0 ]; then
	if ! patch -p1 -s -d "$mirror" < "$here/dec04-v40-adapt.patch" 2>"$tmp/patch2.err"; then
		cat "$tmp/patch2.err" >&2
		echo "N2TEST DEC04 FAIL dec04-v40-adapt.patch does not apply (did V40 change t_v40.c?)" >&2
		exit 1
	fi
fi

# ---- kernel tree for V40's layout probe --------------------------------------------------------
kcands=()
[ -n "${N2HW_KERNEL:-}" ] && kcands+=("$N2HW_KERNEL")
kcands+=("$root/../../../../../../nord2-current-kernel")
kcands+=("$root/../../../../../nord2-current-kernel")
kcands+=("$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel")
kernel=""
for k in "${kcands[@]}"; do
	if [ -f "$k/include/uapi/linux/mtk_vcu_controls.h" ]; then
		kernel=$(cd "$k" && pwd)
		break
	fi
done
if [ -z "$kernel" ]; then
	echo "N2TEST DEC04 FAIL cannot find nord2-current-kernel (set N2HW_KERNEL)" >&2
	exit 1
fi

inc="-I$hw/include -I$hw/vcu -I$hw/tests/hw"
srcs="$hw/vcu/test_dec04/t_dec04.c $hw/vcu/n2vcu.c $hw/emu/n2a32_mem.c"

passed=0
total=0

run_one() {
	local name=$1; shift
	echo "== DEC04 t_dec04: $name"
	# shellcheck disable=SC2086
	nice -n 10 clang $warn "$@" $inc -o "$tmp/t_dec04_$name" $srcs -lpthread
	if ! "$tmp/t_dec04_$name" >"$tmp/out_dec04_$name.txt" 2>&1; then
		cat "$tmp/out_dec04_$name.txt"
		echo "N2TEST DEC04 FAIL $name run" >&2
		exit 1
	fi
	tail -n 1 "$tmp/out_dec04_$name.txt"
}

run_one asan -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
run_one o2 -O2
run_one tsan -O1 -g -fsanitize=thread

for n in asan o2 tsan; do
	p=$(awk '/^SUMMARY/ {print $2}' "$tmp/out_dec04_$n.txt")
	t=$(awk '/^SUMMARY/ {print $3}' "$tmp/out_dec04_$n.txt")
	[ -n "$p" ] && [ -n "$t" ] || { echo "N2TEST DEC04 FAIL no SUMMARY in $n" >&2; exit 1; }
	passed=$((passed + p))
	total=$((total + t))
done
grep '^DEC04 IOCTLS' "$tmp/out_dec04_o2.txt" || true

# ---- 4. V40's suite on the patched mirror ------------------------------------------------------
echo "== DEC04: V40 suite on the patched mirror"
if ! N2HW_TMP="$tmp/v40" N2HW_KERNEL="$kernel" \
	bash "$hw/vcu/test_v40/run_tests.sh" >"$tmp/out_v40.txt" 2>&1; then
	tail -n 40 "$tmp/out_v40.txt"
	echo "N2TEST DEC04 FAIL V40 suite on the patched mirror" >&2
	exit 1
fi
v40line=$(tail -n 1 "$tmp/out_v40.txt")
echo "$v40line"
case "$v40line" in
	N2TEST\ V40\ PASS\ */*)
		vp=${v40line##* }
		passed=$((passed + ${vp%%/*}))
		total=$((total + ${vp##*/}))
		;;
	*)
		echo "N2TEST DEC04 FAIL unexpected V40 last line: $v40line" >&2
		exit 1
		;;
esac

if [ "$passed" -ne "$total" ]; then
	echo "N2TEST DEC04 FAIL $((total - passed)) checks" >&2
	exit 1
fi
echo "N2TEST DEC04 PASS $passed/$total"
