#!/bin/bash
set -euo pipefail
# X01: host tests for n2vc_hw_log.c, log2script.py and diff_logs.py.
# Builds only under /tmp/n2hw-X01: ASan+UBSan, -O2, TSan, and a cap-0 build
# for the truncation path. The literal expected log uses the test clock.
ID=X01
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SB="/tmp/n2hw-X01"
INC="$ROOT/include"
HW="$ROOT/tests/hw"
SRC="$HERE/tests/t_hwlog.c"
LOG="$HERE/n2vc_hw_log.c"
FAKE="$HW/n2vc_hw_fake.c"
PY="${PYTHON:-python3}"

PASSED=0
CHECKS=0

fail() { echo "N2TEST $ID FAIL $*"; exit 1; }

unset N2VC_FAKE_SCRIPT || true
export PYTHONDONTWRITEBYTECODE=1
rm -rf "$SB"
mkdir -p "$SB/py"

CFLAGS=(-std=c11 -g -Wall -Wextra -Werror -I "$INC" -I "$HW" -I "$HERE" -pthread)

build() {
	local name="$1"
	shift
	echo "== build $name"
	# shellcheck disable=SC2086
	nice -n 10 clang "${CFLAGS[@]}" "$@" "$SRC" "$LOG" "$FAKE" -o "$SB/$name" ||
		fail "$name build"
}

# run_c BINARY TAG ARGS... : the binary must exit 0 and end with X01 C PASS n/n.
run_c() {
	local bin="$1" tag="$2"
	shift 2
	local out="$SB/$tag.out" rc line p t
	set +e
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	TSAN_OPTIONS=halt_on_error=1 \
		"$SB/$bin" "$@" >"$out" 2>&1
	rc=$?
	set -e
	cat "$out"
	[ "$rc" -eq 0 ] || fail "$tag exit $rc"
	line=$(tail -n 1 "$out")
	p=$(printf '%s\n' "$line" | sed -nE 's/^X01 C PASS ([0-9]+)\/([0-9]+)$/\1/p')
	t=$(printf '%s\n' "$line" | sed -nE 's/^X01 C PASS ([0-9]+)\/([0-9]+)$/\2/p')
	[ -n "$p" ] && [ -n "$t" ] || fail "$tag last line: $line"
	PASSED=$((PASSED + p))
	CHECKS=$((CHECKS + t))
}

run_cmd() {
	local tag="$1"
	shift
	local out="$SB/$tag.out" rc
	set +e
	"$@" >"$out" 2>&1
	rc=$?
	set -e
	cat "$out"
	[ "$rc" -eq 0 ] || fail "$tag exit $rc"
	PASSED=$((PASSED + 1))
	CHECKS=$((CHECKS + 1))
	echo "X01 SH CK ok: $tag"
}

run_py() {
	local tag="$1"
	shift
	local out="$SB/$tag.out" rc line p t
	set +e
	"$PY" "$@" >"$out" 2>&1
	rc=$?
	set -e
	cat "$out"
	[ "$rc" -eq 0 ] || fail "$tag exit $rc"
	line=$(tail -n 1 "$out")
	p=$(printf '%s\n' "$line" | sed -nE 's/^X01 PY PASS ([0-9]+)\/([0-9]+)$/\1/p')
	t=$(printf '%s\n' "$line" | sed -nE 's/^X01 PY PASS ([0-9]+)\/([0-9]+)$/\2/p')
	[ -n "$p" ] && [ -n "$t" ] || fail "$tag last line: $line"
	PASSED=$((PASSED + p))
	CHECKS=$((CHECKS + t))
}

shell_check() {
	local name="$1" rc="$2"
	CHECKS=$((CHECKS + 1))
	if [ "$rc" -eq 0 ]; then
		PASSED=$((PASSED + 1))
		echo "X01 SH CK ok: $name"
	else
		echo "X01 SH CK FAIL: $name"
		fail "$name"
	fi
}

diff_check() {
	local name="$1" a="$2" b="$3" rc
	set +e
	diff -u "$a" "$b"
	rc=$?
	set -e
	if [ "$rc" -ne 0 ]; then
		echo "X01 SH CK FAIL: $name (diff above)"
	fi
	shell_check "$name" "$rc"
}

run_config() {
	local cfg="$1" bin="$2"

	echo "== $cfg: literal sequence and observations"
	run_c "$bin" "$cfg.basic" basic "$HERE/tests/scripts/ops.txt" \
		"$SB/$cfg.phone.log" "$SB/$cfg.reads.txt"

	echo "== $cfg: log2script"
	run_cmd "$cfg.log2script" "$PY" "$HERE/log2script.py" "$SB/$cfg.phone.log" \
		--script "$SB/$cfg.script.txt" --summary "$SB/$cfg.summary.txt" --quiet

	echo "== $cfg: replay through the generated script"
	run_c "$bin" "$cfg.replay" replay "$SB/$cfg.script.txt" \
		"$SB/$cfg.replay.log" "$SB/$cfg.reads2.txt"
	diff_check "$cfg reads round trip" "$SB/$cfg.reads.txt" "$SB/$cfg.reads2.txt"
	diff_check "$cfg full log round trip" "$SB/$cfg.phone.log" "$SB/$cfg.replay.log"

	echo "== $cfg: mutated write for diff_logs"
	run_c "$bin" "$cfg.mutate" mutate "$HERE/tests/scripts/ops.txt" \
		"$SB/$cfg.mutant.log" "$SB/$cfg.mutant.reads.txt"

	echo "== $cfg: error forwarding"
	mkdir -p "$SB/$cfg.err"
	run_c "$bin" "$cfg.errors" errors "$SB/$cfg.err"

	echo "== $cfg: threads"
	run_c "$bin" "$cfg.threads" threads "$SB/$cfg.threads.log"

	diff_check "$cfg literal expected log" "$HERE/tests/expected.log" \
		"$SB/$cfg.phone.log"
}

# ---- builds ---------------------------------------------------------------

build asan -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
build o2 -O2
build tsan -O1 -fsanitize=thread
build cap0-asan -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	-DN2HW_HWLOG_MAX_MB=0
build cap0-o2 -O2 -DN2HW_HWLOG_MAX_MB=0

# ---- ASan+UBSan and -O2: full suite ---------------------------------------

run_config asan asan
run_config o2 o2

# ---- TSan: full literal sequence plus the threaded run --------------------

echo "== tsan: literal sequence"
run_c tsan tsan.basic basic "$HERE/tests/scripts/ops.txt" \
	"$SB/tsan.phone.log" "$SB/tsan.reads.txt"
diff_check "tsan literal expected log" "$HERE/tests/expected.log" \
	"$SB/tsan.phone.log"
echo "== tsan: threads"
run_c tsan tsan.threads threads "$SB/tsan.threads.log"

# ---- cap 0: truncation line is the last line ------------------------------

echo "== cap0-asan: truncation"
run_c cap0-asan cap0.asan.trunc trunc "$SB/cap0.asan.log"
echo "== cap0-o2: truncation"
run_c cap0-o2 cap0.o2.trunc trunc "$SB/cap0.o2.log"

# ---- Python-side unit checks ---------------------------------------------

run_py py "$HERE/tests/t_check.py" "$HERE" "$SB/py" "$SB/asan.phone.log" \
	"$SB/asan.script.txt" "$SB/asan.summary.txt" "$SB/asan.mutant.log"

# ---- optional: the same source for the real target ------------------------

SYSINC="$ROOT/../video/sdk/sysinc"
RES=$(clang -print-resource-dir)
if [ -d "$SYSINC" ]; then
	echo "== iOS compile with build_ios.sh's freestanding flags"
	if nice -n 10 clang -c --target=arm64-apple-ios16.0 -std=c11 -O2 \
		-fno-common -fvisibility=hidden -ffreestanding -nostdinc \
		-isystem "$RES/include" -isystem "$SYSINC" -I "$INC" -I "$HERE" \
		-Wall -Wextra -Werror -o "$SB/log_ios.o" "$LOG"; then
		rc=0
	else
		rc=1
	fi
	shell_check "n2vc_hw_log.c compiles for arm64-apple-ios16.0" "$rc"
else
	echo "== iOS compile skipped (no gpu/video/sdk/sysinc in this tree)"
fi

# ---- cleanup: keep the text logs, drop the executables --------------------

rm -f "$SB"/asan "$SB"/o2 "$SB"/tsan "$SB"/cap0-asan "$SB"/cap0-o2 "$SB"/log_ios.o

echo "N2TEST $ID PASS $PASSED/$CHECKS"
