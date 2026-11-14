#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run_tests.sh -- host end-to-end test of package IP (vcodecd).  No arguments; everything goes to /tmp/n2hw-ip.
#
#   config 1: clang -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror
#   config 2: clang -O2 -Wall -Wextra -Werror
#   config 3: clang -O1 -g -fsanitize=thread (the test build links T02's fake hardware, which uses a mutex)
#
# The daemon and the test driver are built from the sources in this package plus T02's fake n2vc_hw
# (tests/hw/n2vc_hw_fake.c, unmodified) and daemon/test/fake_vhost.c (the IV/V41 contract model).  The canned H.264
# stream is made here by ffmpeg and split into access units by split_au.py; it lives in /tmp and is never committed.
#
# The last section links the same vcodecd against IV's real host/n2vhost library (libn2vhost.a, built by IV's own
# Makefile into /tmp/n2hw-ip/test/iv) and the phone's extracted vendor set read at run time.  That is the oracle's
# "against the real n2vhost as far as IV's milestone allows" half: HELLO, OPEN (OPEN_OK or a documented bounded
# error at IV's current milestone), still-serving, clean stop.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)          # gpu/hwcodec/daemon/test
DAEMON=$(cd "$HERE/.." && pwd)               # gpu/hwcodec/daemon
W=$(cd "$DAEMON/.." && pwd)                  # gpu/hwcodec
ROOT=$(cd "$W/../.." && pwd)                 # work tree root
OUT=/tmp/n2hw-ip/test
VENDOR=${N2HW_VENDOR_DIR:-$ROOT/gpu/build/cam-vendor-20261109/vfs}
rm -rf "$OUT"
mkdir -p "$OUT"

CC=${CC:-clang}
FLAGS=(-std=c11 -Wall -Wextra -Werror -I "$W/include" -I "$DAEMON" -I "$W/host" -I "$W/ap" -I "$W/tests/hw")
SRC=("$DAEMON/vcodecd.c" "$DAEMON/n2vcd_srv.c" "$W/tests/hw/n2vc_hw_fake.c")
# vcodecd.c carries --selftest, so the test binary links the interpreter core as well (the product binary gets
# the same objects from IV's archive).
EMU_SRC=("$W"/emu/n2a32_*.c)
TEST=("$DAEMON/test/fake_vhost.c" "$DAEMON/test/t_daemon.c")
TOTAL=0

trap 'rc=$?; echo "N2TEST IP FAIL command failed (rc $rc): $BASH_COMMAND" >&2; exit 1' ERR

count_pass() {  # count_pass <log> <name>
	local line n
	line=$(grep -E '^N2TEST IP(-REAL)? (PASS|FAIL)' "$1" | tail -n 1) || {
		echo "N2TEST IP FAIL $2 produced no verdict"; exit 1; }
	case $line in
		*" PASS "*) ;;
		*) echo "$line"; echo "N2TEST IP FAIL $2"; exit 1 ;;
	esac
	n=${line##* }; n=${n%%/*}
	TOTAL=$((TOTAL + n))
}

echo "== canned H.264 stream =="
command -v ffmpeg >/dev/null || { echo "N2TEST IP FAIL ffmpeg not found"; exit 1; }
command -v python3 >/dev/null || { echo "N2TEST IP FAIL python3 not found"; exit 1; }
ffmpeg -v error -y -f lavfi -i "testsrc2=size=64x48:rate=30" -frames:v 12 \
	-c:v libx264 -profile:v baseline -g 6 -keyint_min 6 -sc_threshold 0 -pix_fmt yuv420p \
	-f h264 "$OUT/stream.h264"
python3 "$HERE/split_au.py" "$OUT/stream.h264" "$OUT/stream.au"

run_config() {
	local name=$1
	shift
	local extra=("$@")
	local dir=$OUT/$name
	mkdir -p "$dir"
	echo "== $name: $CC ${extra[*]} =="
	"$CC" "${FLAGS[@]}" "${extra[@]}" -DN2IP_TEST_HOOKS -o "$dir/vcodecd" "${SRC[@]}" "${EMU_SRC[@]}" \
		"$DAEMON/test/fake_vhost.c" -pthread -lm
	"$CC" "${FLAGS[@]}" "${extra[@]}" -o "$dir/t_daemon" "${TEST[@]}" -pthread
	mkdir -p "$dir/run"
	cp "$OUT/stream.au" "$dir/run/stream.au"
	local log=$dir/test.log
	"$dir/t_daemon" "$dir/vcodecd" "$dir/run" "${P03_CLI:-}" | tee "$log"
	count_pass "$log" "$name"
}

echo "== P03 client tool =="
P03_CLI=""
if [ -f "$W/client/n2vcd_cli.c" ] && [ -f "$W/client/n2vcd_client.c" ] && [ -f "$W/client/n2vcd_transport.c" ]; then
	"$CC" "${FLAGS[@]}" -O2 -I "$W/client" -o "$OUT/p03_cli" \
		"$W/client/n2vcd_cli.c" "$W/client/n2vcd_client.c" "$W/client/n2vcd_transport.c" -pthread
	P03_CLI=$OUT/p03_cli
else
	echo "WARNING P03 client sources are incomplete; the client-library end-to-end check will be skipped"
fi

run_config asan -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
run_config o2 -O2
run_config tsan -O1 -g -fsanitize=thread

echo "== selftest guest blobs (llvm-mc oracle) =="
bash "$HERE/gen_selftest_prog.sh" > "$OUT/n2vcd_selftest_prog.h"
diff -u "$DAEMON/n2vcd_selftest_prog.h" "$OUT/n2vcd_selftest_prog.h"
echo "selftest_a32.s / selftest_t32.s match the committed byte arrays"

echo "== vcodecd --selftest (no hardware, no vendor files) =="
# ASan+UBSan and -O2.  Not TSan: the interpreter's fault bridge siglongjmp()s out of a SIGSEGV handler, which
# ThreadSanitizer rejects ("can't find longjmp buf"); that is a TSan limitation, not a selftest failure.
for cfg in asan o2; do
	"$OUT/$cfg/vcodecd" --selftest > "$OUT/$cfg/selftest.log" 2>&1 || {
		cat "$OUT/$cfg/selftest.log"; echo "N2TEST IP FAIL selftest ($cfg)"; exit 1; }
	grep -q '^N2VCD selftest PASS 20/20' "$OUT/$cfg/selftest.log" || {
		cat "$OUT/$cfg/selftest.log"; echo "N2TEST IP FAIL selftest verdict ($cfg)"; exit 1; }
	echo "$cfg: $(tail -n 1 "$OUT/$cfg/selftest.log")"
	TOTAL=$((TOTAL + 1))
done

echo "== proposed RWVC_DMA_INFO compile check =="
mkdir -p "$OUT/dma_compat"
{
	echo "#include \"$W/include/redwood_vcodec.h\""
	echo "#ifndef RWVC_DMA_INFO"
	echo "struct rwvc_dma_info { uint32_t handle, bytes, out_pa, reserved; };"
	echo "#define RWVC_DMA_INFO RWVC_IOC(0xc0000000u, 12, struct rwvc_dma_info)"
	echo "#endif"
} > "$OUT/dma_compat/redwood_vcodec.h"
"$CC" -I "$OUT/dma_compat" "${FLAGS[@]}" -O1 -fsyntax-only -DN2IP_TEST_HOOKS \
	"$DAEMON/vcodecd.c" "$DAEMON/n2vcd_srv.c"
echo "RWVC_DMA_INFO branch compiles"

echo "== real n2vhost (IV) library =="
skip_real() {
	echo "N2TEST IP-REAL SKIP $1"
	if [ "${N2IP_ALLOW_SKIP_REAL:-0}" = "1" ]; then
		echo "WARNING real-vhost section skipped: $1 (allowed by N2IP_ALLOW_SKIP_REAL=1)"
		return 0
	fi
	echo "N2TEST IP FAIL real-vhost section skipped: $1 (set N2IP_ALLOW_SKIP_REAL=1 to allow)" >&2
	exit 1
}
if [ ! -d "$VENDOR" ]; then
	skip_real "no vendor directory $VENDOR"
elif ! nice -n 10 make -C "$W/host" lib OUT="$OUT/iv" JOBS=2 >"$OUT/iv-build.log" 2>&1; then
	# IV/V41 are other packages and are being edited in parallel; a transient compile error there must not
	# silently turn this package's suite green.
	echo "WARNING IV library build failed (dependency, not this package):" >&2
	tail -5 "$OUT/iv-build.log" >&2
	skip_real "IV library build failed"
elif [ ! -f "$OUT/iv/o2/libn2vhost.a" ]; then
	skip_real "no $OUT/iv/o2/libn2vhost.a"
else
	echo "== real: link vcodecd against libn2vhost.a and run it on the phone's vendor set =="
	mkdir -p "$OUT/real"
	"$CC" "${FLAGS[@]}" -O2 -DN2IP_TEST_HOOKS -DN2IP_FAKE_VENC -I "$W/fakevenc" -o "$OUT/real/vcodecd" \
		"$DAEMON/vcodecd.c" "$DAEMON/n2vcd_srv.c" "$DAEMON/test/p03_absent.c" "$W/fakevenc/n2venc_fake.c" \
		-Wl,--whole-archive "$OUT/iv/o2/libn2vhost.a" -Wl,--no-whole-archive -pthread -lm
	"$CC" "${FLAGS[@]}" -O2 -o "$OUT/real/t_real" "$DAEMON/test/t_real.c"
	"$OUT/real/t_real" "$OUT/real/vcodecd" "$OUT/real/run" "$VENDOR" | tee "$OUT/real/test.log"
	count_pass "$OUT/real/test.log" real

	# opus-lead2's gate: the real-vhost path must produce two frames, the first starting with SPS+PPS and a slice,
	# and ffmpeg must decode them.  t_real writes the AUs there; a missing file is a failure, not a skip.
	FRAMES=$OUT/real/run/frames.h264
	[ -s "$FRAMES" ] || { echo "N2TEST IP FAIL real-vhost produced no frames"; exit 1; }
	ffmpeg -v error -f h264 -i "$FRAMES" -f null - 2>"$OUT/real/ffmpeg.log" || {
		cat "$OUT/real/ffmpeg.log"; echo "N2TEST IP FAIL ffmpeg rejected the real-vhost frames"; exit 1; }
	GEOM=$(ffprobe -v error -count_frames -select_streams v:0 \
		-show_entries stream=width,height,nb_read_frames -of csv=p=0 "$FRAMES")
	case $GEOM in
		64,48,*) ;;
		*) echo "N2TEST IP FAIL real-vhost frames geometry/count: $GEOM"; exit 1 ;;
	esac
	NF=${GEOM##*,}
	[ "${NF:-0}" -ge 2 ] || { echo "N2TEST IP FAIL real-vhost decoded only $NF frame(s)"; exit 1; }
	echo "real-vhost frames: $GEOM (ffmpeg decoded, first AU 00000001 67 ... slice)"
fi

echo "N2TEST IP PASS $TOTAL/$TOTAL"
