#!/bin/bash
# V32 host tests: the thread/synchronisation shims.
#
# Three host builds (ASan+UBSan -O1, plain -O2, TSan -O1) of two binaries:
#   t_direct -- drives n2shim_thread[] with a fake cpu: table coverage, errno paths, attrs, signals, side table.
#   t_e2e    -- loads a REAL ARM shared object (arm/t_v32_guest.c) with V03's loader and runs it on V02's
#               interpreter: 8 threads x 100000 increments on a zero-initialised mutex, producer/consumer with
#               mutex+cond, semaphore ping-pong, pthread_once from 8 threads, rwlocks, lifecycle, and (except
#               under TSan, which cannot follow V02's siglongjmp out of a guest fault) failure propagation.
# Everything is built under /tmp/n2hw-v32 with at most two compiler jobs at a time.
# Last line on success: N2TEST V32 PASS <passed>/<total>
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$H/.." && pwd)
W=$(cd "$S/.." && pwd)
OUT=/tmp/n2hw-v32
rm -rf "$OUT"
mkdir -p "$OUT"

CORE="$W/emu/n2a32_core.c $W/emu/n2a32_mem.c $W/emu/n2a32_excl.c $W/emu/n2a32_stub_classes.c"
LOADER="$W/loader/n2elf.c $W/loader/n2elf_reloc.c"
CLASSES="$W/emu/n2a32_arm_dp.c $W/emu/n2a32_arm_media.c $W/emu/n2a32_arm_mem.c $W/emu/n2a32_arm_misc.c"
# -std=gnu11 for V02's core and V03's loader (POSIX signal interfaces); the shim is C11 + _GNU_SOURCE.
CFLAGS="-std=gnu11 -Wall -Wextra -Werror -I$W/include -I$S -I$H"

echo "== guest: real ARM shared object importing the pthread/sem/sched/signal shims"
nice -n 10 clang --target=armv7a-linux-androideabi -marm -march=armv8-a -mfloat-abi=soft \
	-fPIC -shared -nostdlib -fuse-ld=lld -O1 -fno-stack-protector -fno-unwind-tables \
	-Wall -Wextra "$H/arm/t_v32_guest.c" -o "$OUT/libv32guest.so"

# one_build <tag> <sanitizer flags> <e2e extra args>
one_build() {
	local tag=$1 san=$2 e2e_args=$3

	echo "== build: $tag"
	# shellcheck disable=SC2086
	nice -n 10 clang $CFLAGS $san $CORE "$S/n2shim_thread.c" "$H/t_direct.c" \
		-o "$OUT/t_direct_$tag" -lpthread -lm
	if ! "$OUT/t_direct_$tag" > "$OUT/direct_$tag.log" 2>&1; then
		echo "N2TEST V32 FAIL direct_$tag"
		tail -n 30 "$OUT/direct_$tag.log"
		return 1
	fi
	tail -n 1 "$OUT/direct_$tag.log"
	# shellcheck disable=SC2086
	nice -n 10 clang $CFLAGS $san $CORE $LOADER $CLASSES "$S/n2shim_thread.c" "$H/t_e2e.c" \
		-o "$OUT/t_e2e_$tag" -lpthread -lm
	# shellcheck disable=SC2086
	if ! "$OUT/t_e2e_$tag" "$OUT/libv32guest.so" $e2e_args > "$OUT/e2e_$tag.log" 2>&1; then
		echo "N2TEST V32 FAIL e2e_$tag"
		tail -n 40 "$OUT/e2e_$tag.log"
		return 1
	fi
	tail -n 1 "$OUT/e2e_$tag.log"
}

# Two compiler jobs at a time: ASan and -O2 together, then TSan.
one_build asan "-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all" "" \
	> "$OUT/build_asan.log" 2>&1 &
pid_asan=$!
one_build o2 "-O2" "" > "$OUT/build_o2.log" 2>&1 &
pid_o2=$!
rc_asan=0; rc_o2=0
wait "$pid_asan" || rc_asan=$?
wait "$pid_o2" || rc_o2=$?
cat "$OUT/build_asan.log" "$OUT/build_o2.log"
if [ "$rc_asan" != 0 ] || [ "$rc_o2" != 0 ]; then
	echo "N2TEST V32 FAIL a build/run failed (asan rc=$rc_asan, o2 rc=$rc_o2)"
	exit 1
fi
if ! one_build tsan "-O1 -g -fsanitize=thread" "--no-fault" > "$OUT/build_tsan.log" 2>&1; then
	cat "$OUT/build_tsan.log"
	echo "N2TEST V32 FAIL a build/run failed (tsan)"
	exit 1
fi
cat "$OUT/build_tsan.log"

res_direct() { sed -n 's/^V32DIRECTRESULT \([0-9]*\)\/\([0-9]*\)$/\1 \2/p' "$OUT/$1.log"; }
res_e2e() { sed -n 's/^V32RESULT \([0-9]*\)\/\([0-9]*\)$/\1 \2/p' "$OUT/$1.log"; }

read -r da_pt da_tt <<<"$(res_direct direct_asan)"
read -r do_pt do_tt <<<"$(res_direct direct_o2)"
if [ -z "${da_pt:-}" ] || [ "${da_pt}/${da_tt}" != "${do_pt}/${do_tt}" ]; then
	echo "N2TEST V32 FAIL direct check counts differ: asan=$da_pt/$da_tt o2=$do_pt/$do_tt"
	exit 1
fi
read -r ea_pt ea_tt <<<"$(res_e2e e2e_asan)"
read -r eo_pt eo_tt <<<"$(res_e2e e2e_o2)"
if [ -z "${ea_pt:-}" ] || [ "${ea_pt}/${ea_tt}" != "${eo_pt}/${eo_tt}" ]; then
	echo "N2TEST V32 FAIL e2e check counts differ: asan=$ea_pt/$ea_tt o2=$eo_pt/$eo_tt"
	exit 1
fi
read -r et_pt et_tt <<<"$(res_e2e e2e_tsan)"
if [ -z "${et_pt:-}" ]; then
	echo "N2TEST V32 FAIL the TSan run has no result line"
	exit 1
fi
echo "  checks: direct=$do_pt/$do_tt (asan==o2, tsan also $(( $(res_direct direct_tsan 2>/dev/null | cut -d' ' -f1) + 0 )) )"
echo "  checks: e2e full=$eo_pt/$eo_tt, e2e TSan --no-fault=$et_pt/$et_tt"

pt=$((do_pt + eo_pt))
tt=$((do_tt + eo_tt))
if [ "$pt" != "$tt" ]; then
	echo "N2TEST V32 FAIL $((tt - pt)) checks failed"
	exit 1
fi
echo "N2TEST V32 PASS $pt/$tt"
