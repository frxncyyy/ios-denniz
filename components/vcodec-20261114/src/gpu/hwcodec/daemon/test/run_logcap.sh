#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run_logcap.sh -- host test of the vcodecd.log size cap (daemon/n2vcd_logcap.h, opus-robust 12 Nov 2026).
# Output under /tmp/n2hw-logcap (override N2HW_LOGCAP_OUT, must be under /tmp).
#
#   1. t_logcap (unit): ASan+UBSan -O1, -O2, TSan.
#   2. the real daemon (test build of vcodecd.c with the fake hardware, ASan+UBSan), started the way launchd starts it
#      (fd 1/2 appended to vcodecd.log): a 5 MiB log is rotated at start (old bytes in .1, the daemon serves and logs
#      into the fresh file); with --log-max 256 the daemon's own start-up lines rotate it while running; --log-max 0
#      and a terminal-like fd 2 (a pipe) leave the file alone.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)           # gpu/hwcodec/daemon/test
DAEMON=$(cd "$HERE/.." && pwd)
W=$(cd "$DAEMON/.." && pwd)                   # gpu/hwcodec
OUT=${N2HW_LOGCAP_OUT:-/tmp/n2hw-logcap}
case $OUT in /tmp/?*) ;; *) echo "run_logcap.sh: OUT must be under /tmp" >&2; exit 2 ;; esac
rm -rf "$OUT"; mkdir -p "$OUT"
CC=${CC:-clang}
TOTAL=0
fail() { echo "N2TEST LOGCAP FAIL $*"; exit 1; }

echo "== t_logcap (unit) =="
for cfg in "asan:-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all" "o2:-O2" "tsan:-O1 -g -fsanitize=thread"; do
	name=${cfg%%:*}; flags=${cfg#*:}
	# shellcheck disable=SC2086
	nice -n 10 "$CC" -std=c11 -Wall -Wextra -Werror $flags -I "$DAEMON" -o "$OUT/t_logcap_$name" "$HERE/t_logcap.c" -pthread
	"$OUT/t_logcap_$name" "$OUT/unit-$name" > "$OUT/t_logcap_$name.log" 2>&1 || { cat "$OUT/t_logcap_$name.log"; fail "unit $name"; }
	line=$(tail -n 1 "$OUT/t_logcap_$name.log")
	echo "$name: $line"
	case $line in "N2TEST LOGCAP PASS "*) n=${line##* }; TOTAL=$((TOTAL + ${n%%/*})) ;; *) fail "unit $name verdict" ;; esac
done

echo "== vcodecd (test build, fake hardware, ASan+UBSan) =="
FLAGS=(-std=c11 -Wall -Wextra -Werror -I "$W/include" -I "$DAEMON" -I "$W/host" -I "$W/ap" -I "$W/tests/hw")
nice -n 10 "$CC" "${FLAGS[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DN2IP_TEST_HOOKS \
	-o "$OUT/vcodecd" "$DAEMON/vcodecd.c" "$DAEMON/n2vcd_srv.c" "$W/tests/hw/n2vc_hw_fake.c" "$W"/emu/n2a32_*.c \
	"$DAEMON/test/fake_vhost.c" -pthread -lm

E=$OUT/e2e; mkdir -p "$E/fake" "$E/dma"
echo "not a real vendor library: the fake vhost only checks the directory" > "$E/vendor-lib.so"
export N2IP_FAKE_DIR=$E/fake

# start_daemon <log> [extra args...]: fd 1/2 appended to <log> (launchd StandardOutPath/StandardErrorPath), wait until it
# serves, stop it with SIGTERM.
start_stop() {
	local log=$1; shift
	rm -f "$E/sock"
	( exec "$OUT/vcodecd" --fake --socket "$E/sock" --vendor "$E" --dma-dir "$E/dma" --no-console \
		--hw-timeout-ms 2000 "$@" >>"$log" 2>&1 </dev/null ) &
	local pid=$! t=0
	while [ $t -lt 100 ]; do
		grep -aq 'N2VCD .*serving' "$log" 2>/dev/null && break
		[ -f "$log.1" ] && grep -aq 'N2VCD .*serving' "$log.1" 2>/dev/null && break
		sleep 0.1; t=$((t + 1))
	done
	kill -TERM $pid 2>/dev/null || true
	wait $pid 2>/dev/null || true
}

# (a) 5 MiB old log: rotated at start
L=$E/a.log
head -c $((5 * 1024 * 1024 + 7)) /dev/zero | tr '\0' 'x' > "$L"
start_stop "$L" --log-file "$L"
[ "$(stat -c %s "$L.1")" = "$((5 * 1024 * 1024 + 7))" ] || fail "(a) .1 is not the old 5 MiB file"
grep -aq 'N2VCD log rotated: previous 5242887 bytes' "$L" || fail "(a) no rotation marker in the new log"
grep -aq 'rotated 5242887 bytes at start' "$L" || fail "(a) the daemon did not log the start rotation"
grep -aq 'N2VCD .*serving' "$L" || fail "(a) the daemon did not serve / log into the new file"
[ "$(stat -c %s "$L")" -lt 65536 ] || fail "(a) new log too big"
echo "(a) 5 MiB log rotated at start: .1 $(stat -c %s "$L.1") bytes, new log $(stat -c %s "$L") bytes, daemon served"
TOTAL=$((TOTAL + 5))

# (b) tiny cap: the daemon's own start-up lines rotate it while running; no line torn
L=$E/b.log
: > "$L"
start_stop "$L" --log-file "$L" --log-max 256
[ -f "$L.1" ] || fail "(b) no rotation while running"
[ "$(stat -c %s "$L")" -le $((256 + 800)) ] || fail "(b) live log over the cap: $(stat -c %s "$L")"
[ "$(stat -c %s "$L.1")" -le $((256 + 800)) ] || fail "(b) .1 over the cap"
bad=$(cat "$L.1" "$L" | grep -avcE '^N2V(CD|HOST) ' || true)
[ "$bad" = "0" ] || fail "(b) $bad lines without an N2VCD/N2VHOST prefix (torn?)"
echo "(b) 256 B cap while running: live $(stat -c %s "$L") B, .1 $(stat -c %s "$L.1") B, all lines intact"
TOTAL=$((TOTAL + 4))

# (c) --log-max 0: never rotated
L=$E/c.log
head -c $((5 * 1024 * 1024)) /dev/zero | tr '\0' 'x' > "$L"
start_stop "$L" --log-file "$L" --log-max 0
[ ! -f "$L.1" ] || fail "(c) rotated with --log-max 0"
[ "$(stat -c %s "$L")" -gt $((5 * 1024 * 1024)) ] || fail "(c) log truncated with --log-max 0"
TOTAL=$((TOTAL + 2))

# (d) fd 2 is a pipe (not the file): the file named by --log-file is left alone
L=$E/d.log
head -c $((5 * 1024 * 1024)) /dev/zero | tr '\0' 'x' > "$L"
rm -f "$E/sock"
( exec "$OUT/vcodecd" --fake --socket "$E/sock" --vendor "$E" --dma-dir "$E/dma" --no-console --log-file "$L" \
	2>&1 </dev/null | cat > "$E/d.pipe.log" ) &
pp=$!
for _ in $(seq 1 100); do grep -aq 'serving' "$E/d.pipe.log" 2>/dev/null && break; sleep 0.1; done
pkill -TERM -f "$OUT/vcodecd --fake --socket $E/sock" 2>/dev/null || true
wait $pp 2>/dev/null || true
[ ! -f "$L.1" ] && [ "$(stat -c %s "$L")" = "$((5 * 1024 * 1024))" ] || fail "(d) a file that is not fd 2 was rotated"
grep -aq 'log cap off: fd 2 is not' "$E/d.pipe.log" || fail "(d) no 'log cap off' line"
echo "(c) --log-max 0 and (d) fd 2 = pipe: the file is left alone"
TOTAL=$((TOTAL + 2))

echo "N2TEST LOGCAP PASS $TOTAL/$TOTAL"
