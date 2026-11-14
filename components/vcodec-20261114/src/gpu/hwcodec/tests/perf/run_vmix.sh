#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Guest instruction count and mix of the vendor encoder stack per phase (opus-perf).  Host only.
#
#   bash gpu/hwcodec/tests/perf/run_vmix.sh [--tree HWCODEC_DIR] [--size WxH] [--seconds N]
#
# Copies the track V/T sources of HWCODEC_DIR (default: this tree) to /tmp/n2hw-perf/vmix, swaps T01's
# trace for tests/perf/n2perf_mix.c, makes every guest thread count (attach on n2a32_run entry), puts phase
# marks around the AP steps of host/n2vhost.c (venc_open, start, SEQ_HDR, frame), runs IV's run_vhost.sh on
# that copy and prints tests/perf/mix_report.py tables.  Nothing in the tree is modified.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
hw=$(cd "$here/../.." && pwd)
work=$(cd "$hw/../.." && pwd)
tree=$hw
size=320x240
seconds=8
while [ $# -gt 0 ]; do
	case $1 in
	--tree) shift; tree=$(cd "$1" && pwd) ;;
	--size) shift; size=$1 ;;
	--seconds) shift; seconds=$1 ;;
	*) echo "usage: $0 [--tree DIR] [--size WxH] [--seconds N]" >&2; exit 2 ;;
	esac
	shift
done
vm=${N2HW_TMP:-/tmp/n2hw-perf}/vmix
rm -rf "$vm/hw"
mkdir -p "$vm/hw/tests"
for d in emu include loader shim vcu ap host fakevenc; do
	[ -d "$tree/$d" ] || continue
	cp -r "$tree/$d" "$vm/hw/$d"
done
cp -r "$tree/tests/hw" "$tree/tests/emu" "$tree/tests/check" "$vm/hw/tests/" 2>/dev/null || true
cp "$here/n2perf_mix.c" "$vm/hw/emu/n2a32_trace.c"
cp "$hw/emu/n2a32_fast.h" "$vm/hw/emu/n2a32_fast.h" 2>/dev/null || true
python3 -I - "$vm/hw" <<'PY'
import sys, re
root = sys.argv[1]
p = root + '/emu/n2a32_core.c'
s = open(p).read()
anchor = 'enum n2a32_stop n2a32_run(struct n2a32_cpu *c)\n{'
assert anchor in s, 'n2a32_run not found'
s = s.replace(anchor, 'void n2a32_trace_attach(struct n2a32_cpu *c);\n' + anchor, 1)
a2 = '\tc->stop_addr = 0;\n'
i = s.index(anchor)
j = s.index(a2, i)
s = s[:j + len(a2)] + '\tif (c->trace == NULL)\n\t\tn2a32_trace_attach(c);   /* n2perf: count every guest thread */\n' + s[j + len(a2):]
open(p, 'w').write(s)
p = root + '/host/n2vhost.c'
s = open(p).read()
s = s.replace('#include "n2a32_trace.h"', '#include "n2a32_trace.h"\nvoid n2perf_mark(const char *label);', 1)
marks = [
    ('\t\trc = n2ap_venc_open(&e,', 'enc_init'),
    ('\t\trc = n2ap_venc_start(e, &p);', 'set_param'),
    ('\t\t\trc = ap_encode(h, e, 0, bs.handle, N2AP_ENC_SEQ_HDR', 'seq_hdr'),
    ('\t\trc = n2ap_venc_encode(e, 0, bs.handle, N2AP_ENC_SEQ_HDR, &r);', 'seq_hdr'),
    ('\t\t\trc = ap_encode(h, e, frame.handle, bs.handle, N2AP_ENC_FRAME', 'frame1'),
    ('\t\trc = n2ap_venc_encode(e, frame.handle, bs.handle, N2AP_ENC_FRAME, &r);', 'frame1'),
    ('\t\t(void)n2ap_venc_close(e);\n\t}\n#else', 'close'),
]
found = set()
for pat, label in marks:
    if pat in s:
        s = s.replace(pat, '\t\tn2perf_mark("%s");\n%s' % (label, pat), 1)
        found.add(label)
for label in ('enc_init', 'set_param', 'seq_hdr', 'frame1', 'close'):
    if label not in found:
        print('run_vmix: mark %s: pattern not found (phase merged into the previous one)' % label)
# more frames (steady state): only for the runner variant with n2ap_venc_wait_output (asynchronous done)
anchor = '\t\tprintf("N2VHOST encode frame bytes=%u key=%u sync=%u\\n", r.bytes, r.is_key_frm, p.sync_mode);\n'
if anchor in s and 'n2ap_venc_wait_output' in s:
    loop = anchor + (
        '\t\t{\n'
        '\t\t\tconst char *nf = getenv("N2PERF_FRAMES");\n'
        '\t\t\tint nfr = nf ? atoi(nf) : 6;\n'
        '\t\t\tfor (int k = 2; k <= nfr; k++) {\n'
        '\t\t\t\tchar lb[24];\n'
        '\t\t\t\tstruct n2ap_enc_result r2;\n'
        '\t\t\t\tint rc2;\n'
        '\t\t\t\tsnprintf(lb, sizeof lb, "frame%d", k);\n'
        '\t\t\t\tn2perf_mark(lb);\n'
        '\t\t\t\tmemset(&r2, 0, sizeof r2);\n'
        '\t\t\t\trc2 = n2ap_venc_encode(e, frame.handle, bs.handle, N2AP_ENC_FRAME, &r2);\n'
        '\t\t\t\tif (rc2 == 0 && r2.bytes == 0)\n'
        '\t\t\t\t\trc2 = n2ap_venc_wait_output(e, bs.handle, ap_timeout_ms(), &r2);\n'
        '\t\t\t\tprintf("N2VHOST perf frame %d rc=%d bytes=%u key=%u\\n", k, rc2, r2.bytes, r2.is_key_frm);\n'
        '\t\t\t\tif (rc2 != 0)\n'
        '\t\t\t\t\tbreak;\n'
        '\t\t\t}\n'
        '\t\t\tn2perf_mark("final");\n'
        '\t\t}\n')
    s = s.replace(anchor, loop, 1)
else:
    print('run_vmix: no multi-frame loop (runner variant without n2ap_venc_wait_output)')
open(p, 'w').write(s)
PY
export N2PERF_OUT=$vm/mix.txt
rm -f "$N2PERF_OUT"
N2HW_OUT=$vm/out N2HW_VENDOR_DIR=${N2HW_VENDOR_DIR:-$work/gpu/build/cam-vendor-20261109/vfs} \
	bash "$vm/hw/host/run_vhost.sh" --size "$size" --seconds "$seconds" | tee "$vm/report.txt" | tail -14 || true
objs=()
vendor=${N2HW_VENDOR_DIR:-$work/gpu/build/cam-vendor-20261109/vfs}
while read -r _ _ name base _; do
	b=${base#base=0x}
	case $name in vpud) p=$vendor/bin/vpud ;; *) p=$vendor/lib/$name ;; esac
	objs+=(--obj "$name:$b:$p")
done < <(grep '^N2VHOST object ' "$vm/out/run/run-o2.log")
[ -s "$N2PERF_OUT" ] || { echo "run_vmix: no mix dump (runner crashed?)"; exit 1; }
python3 -I "$here/mix_report.py" "$N2PERF_OUT" "${objs[@]}" --top 15 --exclude-obj vpud
# The runner log can reach hundreds of MB (vpud's busy GET_LOG poll prints every turn): keep only its tail.
log=$vm/out/run/run-o2.log
if [ -f "$log" ]; then
	tail -c 200000 "$log" > "$log.tail" && mv "$log.tail" "$log"
fi
rm -f "$vm"/out/run/trace.txt "$vm"/out/run/*.h264 "$vm"/out/run/*.nv12
