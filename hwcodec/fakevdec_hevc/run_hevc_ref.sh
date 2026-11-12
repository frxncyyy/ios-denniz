#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# run_hevc_ref.sh -- opus-hevcdec: build the host library from a /tmp COPY of the tree and run the phone's vpud + vendor
# libraries on HEVC streams through fakevdec_hevc/ (VDEC model wrapped around T02's fake).  Host only; never touches the
# phone; writes only under $OUT.
#
#   bash gpu/hwcodec/fakevdec_hevc/run_hevc_ref.sh          # 320x240 (bf 2) + 1920x1080 HEVC, display order checked
#   VR_TRACE=1 bash ...                                     # also a VDEC register trace per stream ($OUT/<v>.trace)
#
# Staged in the /tmp copy only (remove each line once its owner lands it in the tree):
#   DEC04/DEC07  vcu/n2vcu.c: RWVC_WIN_VDEC_ALL window { 0x16000000, 0x30000, 12 } (HEVC uses 0x16015000 LAT HEVC_VLD,
#                0x16012000.. outside the ten frozen windows; same staging text as DEC02 tools/stage.py)
#   V34          shim/n2shim_props.c vendor.mtk.vdec.frame.layout.mode = "0" (MM21, no UFO) as dec1/run_ref.sh
set -euo pipefail
HWD="$(cd "$(dirname "$0")/.." && pwd)"
VFS="$(cd "$HWD/../build/cam-vendor-20261109/vfs" && pwd)"
OUT="${OUT:-/tmp/n2hw-opus-hevcdec/hevc}"
mkdir -p "$OUT/tree" "$OUT/vec"
rsync -a --delete --include='*/' --include='*.c' --include='*.h' --include='Makefile' --exclude='*' --prune-empty-dirs \
	"$HWD"/{emu,loader,shim,tests,vcu,ap,fakevenc,host,include} "$OUT/tree/"
python3 - "$OUT/tree" <<'EOF'
import sys, os
t = sys.argv[1]
p = os.path.join(t, 'shim/n2shim_props.c'); s = open(p).read()
old = '{ "vendor.mtk.vdec.frame.layout.mode", NULL,'
if old in s:
    s = s.replace(old, '{ "vendor.mtk.vdec.frame.layout.mode", "0",'); open(p, 'w').write(s); print('staged V34 layout.mode=0')
p = os.path.join(t, 'vcu/n2vcu.c'); s = open(p).read()
if 'RWVC_WIN_VDEC_ALL' in s or '0x30000u, 12u' in s:
    print('VDEC_ALL window already in vcu/n2vcu.c: not staged')
else:
    old = '\t{ 0x1602f000u, 0x1000u, RWVC_WIN_VDEC_SYS },\n'
    assert old in s, 'n2vcu.c vcu_windows[] changed: update the staging text'
    s = s.replace(old, old + '\t{ 0x16000000u, 0x30000u, 12u },   /* staged: RWVC_WIN_VDEC_ALL (DEC04/DEC07) */\n', 1)
    open(p, 'w').write(s); print('staged VDEC_ALL window (n2vcu copy)')
EOF
nice -n 10 make -s -C "$OUT/tree/host" "$OUT/o2/libn2vhost.a" OUT="$OUT" JOBS=4 -j4 2>&1 | grep -E 'N2VHOST build|error' || true
T="$OUT/tree"
nice -n 10 clang -std=c11 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -I"$T" -I"$T/include" -I"$T/tests/hw" \
	-I"$T/tests/emu" -I"$T/vcu" -I"$T/ap" -I"$T/host" -I"$T/shim" -I"$HWD/fakevdec_hevc" \
	"$HWD/fakevdec_hevc/vdec_hevc_ref.c" "$HWD/fakevdec_hevc/n2vdec_hevc_fake.c" -o "$OUT/vdec_hevc_ref" \
	-Wl,--whole-archive "$OUT/o2/libn2vhost.a" -Wl,--no-whole-archive -lpthread -lm
[ -s "$OUT/vec/h320.h265" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 -frames:v 12 \
	-pix_fmt yuv420p -c:v libx265 -x265-params log-level=error:bframes=2:keyint=12 "$OUT/vec/h320.h265"
[ -s "$OUT/vec/h1080.h265" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=1920x1080:rate=30 -frames:v 6 \
	-pix_fmt yuv420p -c:v libx265 -x265-params log-level=error:bframes=2:keyint=6 "$OUT/vec/h1080.h265"
# Test jobs run inside the project CPU slice (CLAUDE.md); compiles above are exempt (user, 2026-11-10).
RUN=()
if command -v systemd-run >/dev/null && systemctl --user cat nord2-metal.slice >/dev/null 2>&1; then
	RUN=(systemd-run --user --quiet --scope --slice=nord2-metal.slice taskset -c 0,1 nice -n 10)
fi
pass=0; total=1
# model unit test (no vendor code involved), ASan+UBSan
nice -n 10 clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -D_POSIX_C_SOURCE=200809L -I"$T/include" \
	-I"$HWD/fakevdec_hevc" "$HWD/fakevdec_hevc/t_fakevdec_hevc.c" "$HWD/fakevdec_hevc/n2vdec_hevc_fake.c" -o "$OUT/t_fakevdec_hevc" -lpthread
"$OUT/t_fakevdec_hevc" | tail -1 | tee "$OUT/t_fakevdec_hevc.log" | grep -q ' PASS ' && pass=1
cat "$OUT/t_fakevdec_hevc.log"
for v in ${VR_STREAMS:-h320 h1080}; do
	total=$((total + 1))
	# expected display order = decode-order packet indices of the frames ffprobe outputs (matched by byte position)
	exp=$(python3 -I - "$OUT/vec/$v.h265" <<'PY'
import subprocess, sys
q = lambda what: subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', what, '-of', 'csv=p=0',
	sys.argv[1]], capture_output=True, text=True, check=True).stdout.replace(',', ' ').split()
pk = {int(p): i for i, p in enumerate(q('packet=pos'))}
print(','.join(str(pk[int(f)]) for f in q('frame=pkt_pos')))
PY
)
	tr=; [ -n "${VR_TRACE:-}" ] && tr="$OUT/$v.trace"
	if VR_EXPECT="$exp" VR_HEVC=1 VR_TRACE="$tr" VR_MIN=7 timeout "${VR_TIMEOUT:-300}" "${RUN[@]}" "$OUT/vdec_hevc_ref" "$VFS" \
		"$OUT/vec/$v.h265" "${VR_NMAX:-1000}" > "$OUT/$v.log" 2>&1; then pass=$((pass + 1)); fi
	grep -E '^VR (header|result|reached|order|guest|model)' "$OUT/$v.log" | sed "s/^/$v: /"
done
echo "N2TEST HEVCDEC-REF $([ $pass = $total ] && echo PASS || echo FAIL) $pass/$total"
