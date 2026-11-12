#!/bin/bash
# SPDX-License-Identifier: MIT
# opus-fmt: PC rehearsal of the PRODUCT decode path (vcodecd --decoder + n2vcd_dprobe over the socket transport) for every
# decoder format, with fmt/patches/*.patch applied to a /tmp copy of the tree (the tree itself is not modified).
#
#   bash gpu/hwcodec/fmt/run_sim_fmt.sh [STREAM...]     default: one clip per format from fmt/streams, ONE vcodecd, one dprobe
#
# Same conditions as decint/run_sim.sh (16 KiB granule, kernel-parity fake, --fake-vdec-irq answers WAITISR) plus the
# HEVC model and this directory's format model (--fake-vdec-hevc --fake-vdec-fmt, test build N2IP_FAKE_VDEC_FMT) and the
# pending shim change patches/0002-shim-ion256.patch (N2AND_ION_BUFS 64 -> 256: an AV1 session after VP9 overflows 64; ION256=0 skips it).  Pixels are zero (no real VDEC).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$SRC/../.." && pwd)
VENDOR=${N2HW_VENDOR_DIR:-$ROOT/gpu/build/cam-vendor-20261109/vfs}
OUT=${OUT:-/tmp/n2hw-fmt/sim}
W=$OUT/wtree
mkdir -p "$OUT"
if [ $# -eq 0 ]; then
	set -- "$SRC/decint/streams/vc14-probe43-640x480-baseline.h264" "$HERE"/streams/f320-vp8.ivf "$HERE"/streams/f320-vp9.ivf \
		"$HERE"/streams/f320-av1.ivf "$HERE"/streams/f320-mpeg2.m2v "$HERE"/streams/f320-mpeg4.m4v "$HERE"/streams/f352-h263.h263
fi
STREAMS=("$@")
if [ "${SKIP_BUILD:-0}" != 1 ]; then
	rm -rf "$W"; mkdir -p "$W"
	rsync -a --include='*/' --include='*.c' --include='*.h' --include='Makefile' --include='*.mk' --exclude='*' --prune-empty-dirs \
		--exclude='dsh-state/' --exclude='runs/' --exclude='build/' "$SRC/" "$W/"
	for p in; do   # vcodec-20261114: patches already applied in this tree
		case "$p" in *ion256*) [ "${ION256:-1}" = 1 ] || continue;; esac     # ION256=0: the shim's 64-slot table as in the tree
		(cd "$W" && patch -s -p1 < "$p") && echo "applied $(basename "$p")"
	done
	grep -n "^#define N2AND_ION_BUFS" "$W/shim/n2shim_android.c"
	INC=(-I "$W/include" -I "$W/daemon" -I "$W/daemon/dec" -I "$W/host" -I "$W/ap" -I "$W/vcu" -I "$W/tests/hw" -I "$W" \
	     -I "$W/client" -I "$W/client/dec" -I "$W/mm21" -I "$W/fakevenc" -I "$W/fakevdec_hevc" -I "$W/fmt")
	F=(-std=c11 -O2 -g -Wall -Wextra -Werror "${INC[@]}")
	PX=(-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE)
	nice -n 10 make -C "$W/host" lib OUT="$OUT/iv" JOBS="${JOBS:-4}" > "$OUT/iv.log" 2>&1 || { tail -20 "$OUT/iv.log"; exit 1; }
	mkdir -p "$OUT/obj"
	for s in ap/n2ap_vdec.c fakevdec_hevc/n2vdec_hevc_fake.c fmt/n2vdec_fmt_fake.c; do
		nice -n 10 clang "${F[@]}" "${PX[@]}" -c -o "$OUT/obj/$(basename "$s" .c).o" "$W/$s"
	done
	nice -n 10 clang "${F[@]}" -DN2IP_TEST_HOOKS -DN2VCD_WITH_DECODER -DN2IP_FAKE_VENC -DN2IP_FAKE_VDEC_HEVC -DN2IP_FAKE_VDEC_FMT \
		-o "$OUT/vcodecd" "$OUT"/obj/*.o "$W/daemon/vcodecd.c" "$W/daemon/n2vcd_srv.c" "$W/daemon/test/p03_absent.c" \
		"$W/fakevenc/n2venc_fake.c" "$W/daemon/dec/n2vcd_dec_service.c" "$W/daemon/dec/n2vcd_dec_ap_n2ap.c" \
		-Wl,--whole-archive "$OUT/iv/o2/libn2vhost.a" -Wl,--no-whole-archive -pthread -lm
	nice -n 10 clang "${F[@]}" -o "$OUT/dprobe" "$W/ios/n2vcd_dprobe.c" "$W/client/dec/n2vcd_dec.c" "$W/client/dec/n2vcd_dec_path.c" \
		"$W/client/n2vcd_transport.c" "$W/mm21/n2_mm21.c" -pthread
	echo "built vcodecd + dprobe (patched copy $W)"
fi
RUN=$OUT/run; rm -rf "$RUN"; mkdir -p "$RUN/out"
SOCK=$RUN/s; mkdir -p "$SOCK.buf"
export N2A32_MEM_GRANULE=${GRANULE-16384} N2VC_FAKE_HANDLE_BASE=${HBASE-0x10000}
"$OUT/vcodecd" --fake --fake-vdec-irq --fake-vdec-hevc --fake-vdec-fmt --decoder --socket "$SOCK" --dma-dir "$SOCK.buf" \
	--vendor "$VENDOR" --record "$RUN/fake.rec" --no-console --hw-timeout-ms 6000 --guest-log ${GUEST_LOG:-400} \
	${VCODECD_ARGS:-} > "$RUN/vcodecd.log" 2>&1 &
VP=$!
trap 'kill $VP 2>/dev/null || true' EXIT
INS=(); for f in "${STREAMS[@]}"; do INS+=(--in "$f"); done
set +e
timeout 1200 "$OUT/dprobe" --socket "$SOCK" "${INS[@]}" --out "$RUN/out" --log "$RUN/dprobe.log" --wait-server 120 --no-console \
	--mm21-first ${DPROBE_EXTRA:-} > "$RUN/dprobe.stdout" 2>&1
PRC=$?
set -e
kill $VP 2>/dev/null || true; wait $VP 2>/dev/null || true; trap - EXIT
grep -E 'stream [0-9]+:|DONE ok=|ALL ok=' "$RUN/dprobe.log" | sed 's/ file=.*//'
EACC=$(grep -c 'EACCES' "$RUN/fake.rec" || true)
REGF=$(grep -c 'hw regs .* -> -' "$RUN/vcodecd.log" || true)
if [ "$PRC" = 0 ] && grep -q 'ALL ok=1' "$RUN/dprobe.log" && [ "$EACC" = 0 ] && [ "$REGF" = 0 ]; then
	echo "N2TEST FMT-SIM PASS ${#STREAMS[@]} streams ($RUN)"
else
	echo "N2TEST FMT-SIM FAIL probe rc $PRC eacces $EACC regs_fail $REGF ($RUN)"; exit 1
fi
