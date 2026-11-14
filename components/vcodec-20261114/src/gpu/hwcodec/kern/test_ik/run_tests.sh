#!/bin/bash
# Host tests of package IK:
#   - test_ik/t_relay.c: kern/bsd/dev/nord2_vcodec_relay.inc against a host shim (ASan+UBSan, -O2, TSan)
#   - make_patch.sh + compile_check.sh: the 0299 assembly and the staged sweet_touch.c compile (-Werror, llvm-nm)
# While K01/K02 have not written their .inc files, their stand-ins under test_ik/standins are used (and the log says so).
# Builds only under /tmp/n2hw-ik.  Last line on success: N2TEST IK PASS <passed>/<total>
set -euo pipefail

H=$(cd "$(dirname "$0")" && pwd)                 # gpu/hwcodec/kern/test_ik
K=$(cd "$H/.." && pwd)                           # gpu/hwcodec/kern
W=$(cd "$K/../../.." && pwd)
T=/tmp/n2hw-ik/test
INC=$W/gpu/hwcodec/include
CFLAGS=(-std=c11 -O1 -g -Wall -Wextra -Werror -pthread -I "$INC")
rm -rf "$T"; mkdir -p "$T"

pass=0; total=0
run_one() {
	local name=$1; shift
	if ! "$@" > "$T/$name.log" 2>&1; then
		echo "--- $name failed ---"
		tail -30 "$T/$name.log"
		exit 1
	fi
	local line; line=$(grep -E '^N2TEST IK (PASS|FAIL) ' "$T/$name.log" | tail -1)
	echo "$line"
	case "$line" in
	"N2TEST IK PASS "*) ;;
	*) echo "--- $name printed no PASS line ---"; tail -20 "$T/$name.log"; exit 1 ;;
	esac
	local counts=${line#N2TEST IK PASS }
	pass=$((pass + ${counts%/*}))
	total=$((total + ${counts#*/}))
}

nice -n 10 clang "${CFLAGS[@]}" -fsanitize=address,undefined -fno-sanitize-recover=all "$H/t_relay.c" -o "$T/t_relay_asan"
run_one asan "$T/t_relay_asan"

nice -n 10 clang "${CFLAGS[@]}" -O2 "$H/t_relay.c" -o "$T/t_relay_o2"
run_one o2 "$T/t_relay_o2"

nice -n 10 clang "${CFLAGS[@]}" -fsanitize=thread -fno-sanitize-recover=all "$H/t_relay.c" -o "$T/t_relay_tsan"
run_one tsan "$T/t_relay_tsan"

# ---- 0299 + the staged kernel compile ------------------------------------
if [ -f "$K/bsd/dev/nord2_vcodec.inc" ] && [ -f "$K/bsd/dev/nord2_jpegdev.inc" ]; then
	echo "0299: using the real nord2_vcodec.inc and nord2_jpegdev.inc"
else
	echo "0299: K01/K02 files absent, using test_ik/standins (their real files replace these paths)"
	export N2IK_VCODEC_INC=$H/standins/nord2_vcodec.inc
	export N2IK_JPEGDEV_INC=$H/standins/nord2_jpegdev.inc
fi
export N2IK_OUT=$T/0299-under-test.patch

bash "$K/make_patch.sh" > "$T/make_patch.log" 2>&1 || { tail -20 "$T/make_patch.log"; exit 1; }
grep -q '^MAKE_PATCH OK$' "$T/make_patch.log"
echo "0299: $(grep '^  files:' "$T/make_patch.log")"
bash "$K/compile_check.sh" > "$T/compile_check.log" 2>&1 || { tail -40 "$T/compile_check.log"; exit 1; }
grep -q '^KERNEL COMPILE OK, new undefined symbols: 0 unknown$' "$T/compile_check.log"
echo "0299: $(grep '^KERNEL COMPILE OK' "$T/compile_check.log")"
pass=$((pass + 2)); total=$((total + 2))

echo "N2TEST IK PASS $pass/$total"
