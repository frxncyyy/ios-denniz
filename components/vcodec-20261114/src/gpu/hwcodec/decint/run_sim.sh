#!/bin/bash
# SPDX-License-Identifier: MIT
# opus-decint (2026-11-11): PC rehearsal of the phone decode run.
#
#   bash gpu/hwcodec/decint/run_sim.sh [STREAM...]     default: the two H.264 streams of payload vc3-decode, in its order
#   ENC=1   first run the encode probe exactly as the boot does (vcodecd --fake-venc), the decode probe waits for its DONE
#   (codec from the extension: .h264 avc1, .h265 hvc1; the decode probe gets every stream, one session each)
#
# Builds (in /tmp/n2hw-decint) the exact product sources of the decode payload for the host:
#   vcodecd  = daemon/vcodecd.c with N2VCD_WITH_DECODER (+ daemon/dec service/adapter + ap/n2ap_vdec.c) and IV's
#              libn2vhost.a (interpreter, loader, shims, vcu, T02's fake), test hooks for --fake/--socket/--dma-dir
#   dprobe   = ios/n2vcd_dprobe.c + DEC05 client + P03 transport + mm21 detile
# and runs the phone's own vendor set under the interpreter against T02's fake, with the phone's conditions:
#   N2A32_MEM_GRANULE=16384  host protection at 16 KiB like iOS (opus-guestmem)
#   the fake refuses what the kernel refuses (flags, windows, per-page power, larb allow-list), and models the larb
#   SLP_CON ack; --fake-vdec-irq answers WAITISR (the fake has no VDEC interrupt: the ONE thing not like the phone)
# Pass: the probe prints DONE ok=1 with every picture displayed, vcodecd logs no hw regs failure and no EACCES, the fake
# records 0 violations.  Pixels are all zero here (no real VDEC): pixel checks are the phone's (compare_dec.sh).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$HERE/.." && pwd)                       # gpu/hwcodec
ROOT=$(cd "$W/../.." && pwd)                    # work
VENDOR=${N2HW_VENDOR_DIR:-$ROOT/gpu/build/cam-vendor-20261109/vfs}
OUT=${OUT:-/tmp/n2hw-decint}
if [ $# -eq 0 ]; then set -- "$HERE/streams/vc14-probe43-640x480-baseline.h264" "$HERE/streams/t1080-high-bf2-10f.h264"; fi
STREAMS=("$@")
ENC=${ENC:-0}
CC=${CC:-clang}
mkdir -p "$OUT"
INC=(-I "$W/include" -I "$W/daemon" -I "$W/daemon/dec" -I "$W/host" -I "$W/ap" -I "$W/vcu" -I "$W/tests/hw" -I "$W" \
     -I "$W/client" -I "$W/client/dec" -I "$W/mm21")
F=(-std=c11 -O2 -g -Wall -Wextra -Werror "${INC[@]}")
PX=(-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE)     # for the sources without their own feature macros

if [ "${SKIP_BUILD:-0}" != 1 ]; then
	echo "== IV library (host) =="
	nice -n 5 make -C "$W/host" lib OUT="$OUT/iv" JOBS="${JOBS:-6}" > "$OUT/iv.log" 2>&1 || { tail -20 "$OUT/iv.log"; exit 1; }
	tail -1 "$OUT/iv.log"
	echo "== vcodecd (decoder build, test hooks) =="
	mkdir -p "$OUT/obj"
	"$CC" "${F[@]}" "${PX[@]}" -c -o "$OUT/obj/n2ap_vdec.o" "$W/ap/n2ap_vdec.c"
	"$CC" "${F[@]}" "${PX[@]}" -I "$W/fakevdec_hevc" -c -o "$OUT/obj/n2vdec_hevc_fake.o" "$W/fakevdec_hevc/n2vdec_hevc_fake.c"
	"$CC" "${F[@]}" -DN2IP_TEST_HOOKS -DN2VCD_WITH_DECODER -DN2IP_FAKE_VENC -DN2IP_FAKE_VDEC_HEVC -I "$W/fakevenc" \
		-I "$W/fakevdec_hevc" -o "$OUT/vcodecd" "$OUT/obj/n2vdec_hevc_fake.o" \
		"$W/daemon/vcodecd.c" "$W/daemon/n2vcd_srv.c" "$W/daemon/test/p03_absent.c" "$W/fakevenc/n2venc_fake.c" \
		"$W/daemon/dec/n2vcd_dec_service.c" "$W/daemon/dec/n2vcd_dec_ap_n2ap.c" "$OUT/obj/n2ap_vdec.o" \
		-Wl,--whole-archive "$OUT/iv/o2/libn2vhost.a" -Wl,--no-whole-archive -pthread -lm
	echo "== n2vcd_probe (host, encode) =="
	"$CC" "${F[@]}" -o "$OUT/probe" "$W/ios/n2vcd_probe.c"
	echo "== n2vcd_dprobe (host) =="
	"$CC" "${F[@]}" -o "$OUT/dprobe" "$W/ios/n2vcd_dprobe.c" "$W/client/dec/n2vcd_dec.c" "$W/client/dec/n2vcd_dec_path.c" \
		"$W/client/n2vcd_transport.c" "$W/mm21/n2_mm21.c" -pthread
fi

NAME=$(basename "${STREAMS[0]}")
[ ${#STREAMS[@]} -gt 1 ] && NAME="$NAME+$((${#STREAMS[@]} - 1))"
[ "$ENC" = 1 ] && NAME="$NAME-enc"
[ "${HEVC:-0}" = 1 ] && NAME="$NAME-hevcmodel"
true
RUN=$OUT/run-$NAME
rm -rf "$RUN"; mkdir -p "$RUN/out"
SOCK=$RUN/s
export N2A32_MEM_GRANULE=${GRANULE-16384} N2VC_FAKE_HANDLE_BASE=${HBASE-0x10000}
FV=(); [ "$ENC" = 1 ] && FV=(--fake-venc)
# HEVC=1: opus-hevcdec's VDEC model raises the real VDEC/VDEC_LAT interrupts (no --fake-vdec-irq); its ops pass the
# kernel-parity checks of T02's fake first.  H.264 streams need --fake-vdec-irq (the model knows only HEVC).
if [ "${HEVC:-0}" = 1 ]; then FV+=(--fake-vdec-hevc); else FV+=(--fake-vdec-irq); fi
"$OUT/vcodecd" --fake "${FV[@]}" --decoder --socket "$SOCK" --dma-dir "$SOCK.buf" --vendor "$VENDOR" \
	--record "$RUN/fake.rec" --no-console --hw-timeout-ms 6000 --guest-log ${GUEST_LOG:-400} ${VCODECD_ARGS:-} > "$RUN/vcodecd.log" 2>&1 &
VP=$!
trap 'kill $VP 2>/dev/null || true' EXIT
mkdir -p "$SOCK.buf"
INS=(); for f in "${STREAMS[@]}"; do INS+=(--in "$f"); done
set +e
if [ "$ENC" = 1 ]; then
	echo "N2VCDP old line from the previous boot: DONE ok=1" > "$RUN/probe.log"     # launchd appends: an old DONE is there
	"$OUT/probe" --socket "$SOCK" --dma-dir "$SOCK.buf" --delay 2 --wait-server 120 --codec avc1 --size 640x480 --frames 10 \
		--out "$RUN/out" --no-console >> "$RUN/probe.log" 2>&1 &
	EP=$!
	timeout 900 "$OUT/dprobe" --socket "$SOCK" "${INS[@]}" --out "$RUN/out" --log "$RUN/dprobe.log" --delay 1 \
		--after "$RUN/probe.log" --after-timeout 240 --wait-server 120 --no-console --mm21-first > "$RUN/dprobe.stdout" 2>&1
	PRC=$?
	wait $EP
	grep -q 'DONE ok=1 frames=10/10' "$RUN/probe.log" || { echo "encode probe failed:"; tail -3 "$RUN/probe.log"; PRC=9; }
	grep -E 'after:|OPEN busy' "$RUN/dprobe.log" || true
else
	timeout 900 "$OUT/dprobe" --socket "$SOCK" "${INS[@]}" --out "$RUN/out" --log "$RUN/dprobe.log" \
		--wait-server 120 --no-console --mm21-first ${DPROBE_ARGS:-} > "$RUN/dprobe.stdout" 2>&1
	PRC=$?
fi
set -e
kill $VP 2>/dev/null || true; wait $VP 2>/dev/null || true
trap - EXIT
tail -n 3 "$RUN/dprobe.log" || true
DONE=$(grep -c 'ALL ok=1' "$RUN/dprobe.log" || true)
REGF=$(grep -c 'hw regs .* -> -' "$RUN/vcodecd.log" || true)
EACC=$(grep -c 'EACCES' "$RUN/fake.rec" || true)
echo "vcodecd: $(grep -c 'dec: START' "$RUN/vcodecd.log" || true) STARTs, hw regs failures $REGF; fake EACCES records $EACC"
grep -E 'dec: START|ev (timeout|proto|hw|fourcc)' "$RUN/vcodecd.log" | head -n 40 || true
if [ "$PRC" = 0 ] && [ "$DONE" = 1 ] && [ "$REGF" = 0 ] && [ "$EACC" = 0 ]; then
	grep -E 'DONE ok=' "$RUN/dprobe.log" | sed 's/ file=.*//' || true
	echo "N2TEST DECINT-SIM PASS $NAME ($RUN)"
else
	echo "N2TEST DECINT-SIM FAIL $NAME probe rc $PRC done $DONE regs_fail $REGF eacces $EACC ($RUN)"
	exit 1
fi
