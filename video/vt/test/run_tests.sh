#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Host tests of package WP01: the VideoToolbox plug-in shim (vt/n2vt_shim.c) and the console log (vt/n2vt_log.c), built with
# -DN2_HOST_FAKE and with access/dlopen/dlsym/open/write/close/getpid/getprogname/VTRegisterVideoEncoderWithInfo renamed to
# the doubles of vt/test/t_shim.c. Four binaries, because SLIMEncoderRegister is once per process and one of them has to run
# under TSan:
#
#   t_shim          ASan+UBSan  the full matrix (all factories, off, no-hevc, dlopen failure, claim-hw, log budget/quiet/prefix)
#   t_shim_noavc    ASan+UBSan  no n2avc_vt_create in the link -> hvc1 + hev1 only
#   t_shim_novt     ASan+UBSan  no VTRegisterVideoEncoderWithInfo in the link -> nothing registered, said so, no crash
#   t_shim_threads  TSan        SLIMEncoderRegister 3 times from each of 4 threads -> registered once
#
# Last line: "N2TEST WP01 PASS <passed>/<total>", or "... FAIL ..." and exit 1.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
VIDEO=$(cd "$HERE/../.." && pwd)
OUT=${1:-$VIDEO/build/WP01}
CC=${CC:-clang}
mkdir -p "$OUT"

COMMON=(-std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -DN2_HOST_FAKE
        -I "$VIDEO/include" -I "$VIDEO/vt" -I "$HERE")
RENAME=(-Daccess=t_access -Ddlopen=t_dlopen -Ddlsym=t_dlsym -Dopen=t_open -Dwrite=t_write -Dclose=t_close
        -Dgetpid=t_getpid -Dgetprogname=t_getprogname -DVTRegisterVideoEncoderWithInfo=t_vt_register)
SOURCES=("$VIDEO/vt/n2vt_shim.c" "$VIDEO/vt/n2vt_log.c" "$HERE/t_shim.c")

passed_total=0
total_total=0

run_suite() {   # run_suite <binary> <extra compiler flags...>
	local binary=$1
	shift
	local log status line p t

	if ! "$CC" "${COMMON[@]}" "${RENAME[@]}" "$@" -o "$OUT/$binary" "${SOURCES[@]}" -lpthread; then
		echo "N2TEST WP01 FAIL $binary: compile failed"
		exit 1
	fi
	set +e
	log=$("$OUT/$binary" 2>&1)
	status=$?
	set -e
	printf '%s\n' "$log"
	line=$(printf '%s\n' "$log" | grep '^N2RESULT ' | tail -n 1 || true)
	if [ -z "$line" ]; then
		echo "N2TEST WP01 FAIL $binary: no N2RESULT line (exit $status)"
		exit 1
	fi
	p=$(printf '%s\n' "$line" | awk '{print $(NF-1)}')
	t=$(printf '%s\n' "$line" | awk '{print $NF}')
	passed_total=$((passed_total + p))
	total_total=$((total_total + t))
	if [ "$status" -ne 0 ] || [ "$p" != "$t" ]; then
		echo "N2TEST WP01 FAIL $binary reported $p/$t (exit $status)"
		exit 1
	fi
}

# ---- the SDK headers: every one must be self-contained under the freestanding iOS flags --------------------------------
echo "== sdk/sysinc: every header compiles on its own (arm64-apple-ios16.0, -ffreestanding -nostdinc) =="
RES=$(clang -print-resource-dir)
headers_passed=0
headers_total=0
while IFS= read -r header; do
	headers_total=$((headers_total + 1))
	printf '#include <%s>\nint main(void) { return 0; }\n' "${header#"$VIDEO/sdk/sysinc/"}" > "$OUT/header_check.c"
	if "$CC" -c --target=arm64-apple-ios16.0 -std=c11 -Wall -Wextra -ffreestanding -nostdinc \
		-isystem "$RES/include" -isystem "$VIDEO/sdk/sysinc" -o "$OUT/header_check.o" "$OUT/header_check.c" 2>"$OUT/header_check.err"; then
		headers_passed=$((headers_passed + 1))
	else
		echo "not ok header ${header#"$VIDEO/sdk/sysinc/"}"
		cat "$OUT/header_check.err"
	fi
done < <(find "$VIDEO/sdk/sysinc" -name '*.h' | sort)
echo "N2RESULT t_headers $headers_passed $headers_total"
passed_total=$((passed_total + headers_passed))
total_total=$((total_total + headers_total))
if [ "$headers_passed" != "$headers_total" ]; then
	echo "N2TEST WP01 FAIL sdk/sysinc header check"
	exit 1
fi

run_suite t_shim         -fsanitize=address,undefined -fno-sanitize-recover=undefined
run_suite t_shim_noavc   -fsanitize=address,undefined -fno-sanitize-recover=undefined -DT_NO_AVC_FACTORY
run_suite t_shim_novt    -fsanitize=address,undefined -fno-sanitize-recover=undefined -DT_NO_VT_REGISTER
run_suite t_shim_threads -fsanitize=thread -DT_ONLY_THREADS

echo "N2TEST WP01 PASS $passed_total/$total_total"
