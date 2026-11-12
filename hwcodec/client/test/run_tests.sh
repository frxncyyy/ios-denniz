#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run_tests.sh -- host acceptance test of package P03 (gpu/hwcodec/client).
#
# Builds the client sources + the fake-ioctl/fake-server test three times (ASan+UBSan -O1, plain -O2, TSan -O1), runs
# each, then exercises the real n2vcd_cli binary against a scripted Python vcodecd over SOCK_SEQPACKET.  Finally, when
# gpu/video/sdk/sysinc is present, compiles every client source for arm64-apple-ios16.0 with -Werror.
#
# Everything is written under /tmp/n2hw-P03/test.  No phone, no real device, no network.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
HW=$(cd "$HERE/../.." && pwd)                 # gpu/hwcodec
OUT=/tmp/n2hw-P03/test
mkdir -p "$OUT"
rm -rf "$OUT"/*
trap 'echo "N2TEST P03 FAIL unexpected error at line $LINENO"; exit 1' ERR

SRCS=(
	"$HERE/t_client.c"
	"$HW/client/n2vc_hw_dev.c"
	"$HW/client/n2vcd_transport.c"
	"$HW/client/n2vcd_server.c"
	"$HW/client/n2vcd_client.c"
)
CFLAGS_BASE=(-std=c11 -Wall -Wextra -Werror -I "$HW/include" -I "$HW/client" -DN2VC_TEST_HOOKS -pthread)

passed=0
total=0

run_variant() {   # run_variant <tag> <extra flags...>
	local tag=$1
	shift
	local bin="$OUT/t_client_$tag"
	local log="$OUT/$tag.log"
	echo "== build $tag =="
	nice -n 10 clang "${CFLAGS_BASE[@]}" "$@" "${SRCS[@]}" -o "$bin"
	echo "== run $tag =="
	local rc
	if nice -n 10 "$bin" >"$log" 2>&1; then rc=0; else rc=$?; fi
	tail -2 "$log"
	if [ $rc -ne 0 ]; then
		echo "N2TEST P03 FAIL $tag (exit $rc; log $log)"
		exit 1
	fi
	local line
	line=$(grep -E '^N2TEST P03-TEST (PASS|FAIL)' "$log" | tail -1 || true)
	if [[ ! $line =~ ^N2TEST\ P03-TEST\ PASS\ ([0-9]+)/([0-9]+)$ ]]; then
		echo "N2TEST P03 FAIL $tag (no PASS line)"
		exit 1
	fi
	passed=$((passed + BASH_REMATCH[1]))
	total=$((total + BASH_REMATCH[2]))
}

run_variant asan -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
run_variant o2 -O2
run_variant tsan -O1 -g -fsanitize=thread

# ---- n2vcd_cli, end to end against a scripted daemon ---------------------------------------------------------------
echo "== build n2vcd_cli =="
CLI="$OUT/n2vcd_cli"
nice -n 10 clang -std=c11 -O2 -Wall -Wextra -Werror -I "$HW/include" -I "$HW/client" \
	"$HW/client/n2vcd_cli.c" "$HW/client/n2vcd_client.c" "$HW/client/n2vcd_transport.c" -o "$CLI"

python3 - "$OUT/frames.yuv" <<'PY'
import sys
w, h = 64, 48
out = bytearray()
for f in range(2):
    for y in range(h):
        for x in range(w):
            out.append((x * 3 + y * 7 + f * 11 + 1) & 255)
    for y in range(h // 2):
        for x in range(w):
            out.append((x * 9 + y * 5 + f * 13 + 3) & 255)
open(sys.argv[1], "wb").write(out)
PY

start_server() {   # start_server <mode> <sock> <result> <ready>
	python3 "$HERE/cli_fake_server.py" --socket "$2" --mode "$1" --result "$3" --ready "$4" \
		--yuv "$OUT/frames.yuv" &
	SRV_PID=$!
	for _ in $(seq 1 500); do
		[ -f "$4" ] && return 0
		kill -0 "$SRV_PID" 2>/dev/null || break
		sleep 0.01
	done
	echo "N2TEST P03 FAIL the scripted daemon did not start"
	exit 1
}

CLI_CHECKS=0
CLI_FAILS=0
cli_check() {   # cli_check <condition-result 0/1> <description>
	CLI_CHECKS=$((CLI_CHECKS + 1))
	if [ "$1" -ne 0 ]; then
		CLI_FAILS=$((CLI_FAILS + 1))
		echo "FAIL cli: $2"
	fi
}

check_run() {   # check_run <description> <command...>
	local desc=$1
	shift
	if "$@" >/dev/null 2>&1; then
		cli_check 0 "$desc"
	else
		cli_check 1 "$desc"
	fi
}

echo "== n2vcd_cli info =="
rm -f "$OUT/info.sock" "$OUT/info.json" "$OUT/info.ready"
start_server info "$OUT/info.sock" "$OUT/info.json" "$OUT/info.ready"
if "$CLI" info --socket "$OUT/info.sock" >"$OUT/info.out" 2>"$OUT/info.err"; then IRC=0; else IRC=$?; fi
if wait "$SRV_PID"; then SRC=0; else SRC=$?; fi
check_run "info exit $IRC" test "$IRC" -eq 0
check_run "info server exit $SRC" test "$SRC" -eq 0
check_run "info stdout" test "$(cat "$OUT/info.out")" = "N2VCD CLI info version=1 enc=avc1,hvc1 dec=none max=4096x2160"
check_run "info server result" python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d["ok"] else 1)' "$OUT/info.json"

echo "== n2vcd_cli enc =="
rm -f "$OUT/enc.sock" "$OUT/enc.json" "$OUT/enc.ready" "$OUT/out.h264"
start_server enc "$OUT/enc.sock" "$OUT/enc.json" "$OUT/enc.ready"
if "$CLI" enc "$OUT/frames.yuv" 64x48 30 1000000 "$OUT/out.h264" --socket "$OUT/enc.sock" \
	>"$OUT/enc.out" 2>"$OUT/enc.err"; then ERC=0; else ERC=$?; fi
if wait "$SRV_PID"; then SRC=0; else SRC=$?; fi
cat >"$OUT/enc.expected" <<'EXPECTED'
N2VCD CLI frame 0 bytes=7 key=1 hw_us=1000
N2VCD CLI frame 1 bytes=7 key=0 hw_us=1001
N2VCD CLI done frames=2 bytes=14 key=1 avg_hw_us=1000
EXPECTED
check_run "enc exit $ERC" test "$ERC" -eq 0
check_run "enc server exit $SRC" test "$SRC" -eq 0
check_run "enc stdout" diff -q "$OUT/enc.expected" "$OUT/enc.out"
check_run "enc output bytes" python3 -c 'import sys; au0=b"\x00\x00\x00\x01\x09\x10\x30"; au1=b"\x00\x00\x00\x01\x09\x10\x31"; sys.exit(0 if open(sys.argv[1],"rb").read()==au0+au1 else 1)' "$OUT/out.h264"
check_run "enc server result" python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d["ok"] and d["frames"]==2 else 1)' "$OUT/enc.json"
check_run "enc stderr is empty" test ! -s "$OUT/enc.err"

echo "== n2vcd_cli usage =="
if "$CLI" bogus >"$OUT/usage.out" 2>"$OUT/usage.err"; then URC=0; else URC=$?; fi
check_run "usage exit $URC" test "$URC" -eq 2
check_run "usage text" grep -q '^usage: n2vcd_cli' "$OUT/usage.err"

passed=$((passed + CLI_CHECKS - CLI_FAILS))
total=$((total + CLI_CHECKS))

# ---- iOS compile gate ---------------------------------------------------------------------------------------------
SYSINC="$HW/../video/sdk/sysinc"
if [ -d "$SYSINC" ]; then
	echo "== arm64-apple-ios16.0 compile check =="
	RES=$(clang -print-resource-dir)
	IOS_FAIL=0
	for src in "$HW/client/n2vc_hw_dev.c" "$HW/client/n2vcd_transport.c" "$HW/client/n2vcd_server.c" \
	           "$HW/client/n2vcd_client.c" "$HW/client/n2vcd_cli.c"; do
		obj="$OUT/ios_$(basename "$src" .c).o"
		if ! nice -n 10 clang -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -ffreestanding -nostdinc \
			-isystem "$RES/include" -isystem "$SYSINC" -I "$HW/include" -I "$HW/client" \
			-Wall -Wextra -Werror -o "$obj" "$src"; then
			echo "FAIL ios compile: $(basename "$src")"
			IOS_FAIL=1
		fi
	done
	cli_check "$IOS_FAIL" "iOS compile"
	passed=$((passed + 1 - IOS_FAIL))
	total=$((total + 1))
else
	echo "== no gpu/video/sdk/sysinc: iOS compile check skipped =="
fi

if [ "$CLI_FAILS" -ne 0 ]; then
	echo "N2TEST P03 FAIL $CLI_FAILS CLI/compile check(s)"
	exit 1
fi
echo "N2TEST P03 PASS $passed/$total"
