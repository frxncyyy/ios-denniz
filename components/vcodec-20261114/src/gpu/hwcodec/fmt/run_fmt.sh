#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# opus-fmt: run the phone's vpud (unmodified vendor set) under the interpreter on T02's fake for one clip per decoder
# format and report how far each session gets (VR reached VD-a..VD-g) and which vendor driver it created.
#   bash gpu/hwcodec/fmt/run_fmt.sh [CLIP...]      default: every clip in fmt/streams
#   VR_LOG=1 keeps the vendor log in $OUT/<clip>.log (default on); VR_FAKEIRQ=1 by default (the fake has no VDEC IRQ)
# Host only; builds in $OUT (default /tmp/n2hw-fmt).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$HERE/.." && pwd)
VFS=${N2HW_VENDOR_DIR:-$(cd "$W/../build/cam-vendor-20261109/vfs" && pwd)}
OUT=${OUT:-/tmp/n2hw-fmt}
mkdir -p "$OUT"
if [ "${SKIP_BUILD:-0}" != 1 ]; then
	# Host library from a /tmp COPY of the tree with one pending change applied to the copy only:
	#   ION256: shim/n2shim_android.c N2AND_ION_BUFS 64 -> 256 (vpud holds ~34-53 ion buffers itself; VP9/AV1 sessions
	#   publish 2 x (dpb+2) more; owner IV, board message from opus-fmt).  ION256=0 builds the plain tree.
	mkdir -p "$OUT/tree"
	rsync -a --delete --include='*/' --include='*.c' --include='*.h' --include='Makefile' --exclude='*' --prune-empty-dirs \
		"$W"/{emu,loader,shim,tests,vcu,ap,fakevenc,host,include} "$OUT/tree/"
	[ "${ION256:-1}" = 1 ] && sed -i 's/^#define N2AND_ION_BUFS  64$/#define N2AND_ION_BUFS  256/' "$OUT/tree/shim/n2shim_android.c"
	grep -n '^#define N2AND_ION_BUFS' "$OUT/tree/shim/n2shim_android.c"
	nice -n 10 make -s -C "$OUT/tree/host" lib OUT="$OUT/iv" JOBS="${JOBS:-4}" > "$OUT/iv.log" 2>&1 || { tail "$OUT/iv.log"; exit 1; }
	nice -n 10 clang -std=c11 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -I"$W" -I"$W/include" -I"$W/tests/hw" \
		-I"$W/tests/emu" -I"$W/vcu" -I"$W/ap" -I"$W/host" -I"$W/shim" -I"$W/fakevdec_hevc" -I"$HERE" "$HERE/vdec_fmt.c" "$HERE/n2vdec_fmt_fake.c" \
		"$W/fakevdec_hevc/n2vdec_hevc_fake.c" -o "$OUT/vdec_fmt" \
		-Wl,--whole-archive "$OUT/iv/o2/libn2vhost.a" -Wl,--no-whole-archive -lpthread -lm
fi
[ $# -eq 0 ] && set -- $(ls "$HERE"/streams/*.{ivf,m2v,m4v,h263,h265} 2>/dev/null)
for s in "$@"; do
	b=$(basename "$s")
	VR_LOG=${VR_LOG-1} VR_FAKEIRQ=${VR_FAKEIRQ-1} VR_MIN=${VR_MIN-6} timeout ${TMO:-180} "$OUT/vdec_fmt" "$VFS" "$s" ${MAXF:-10} \
		> "$OUT/$b.log" 2>&1 && r=PASS || r=FAIL
	echo "== $b: $r"
	grep -E '^VR (codec|INIT|header|result|reached|guest)|^VR (START|GCE) au=1 ' "$OUT/$b.log" | sed 's/^/   /'
	grep -aoE '@@ create [A-Z0-9_ ]+|create VDEC_DRV_VIDEO_FORMAT_[A-Z0-9_]+|dlopen [^ ]+ ?[+-]?|\[ERROR\][^"]{0,100}' "$OUT/$b.log" | sort | uniq -c | head -8 | sed 's/^/   vendor: /'
done
