#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# extract_dec02_trace.sh -- regenerate dec02_au1.inc: DEC02's measured H.264 register access
# sequence for one steady-state access unit, as the test oracle replays it.
#
#   bash gpu/hwcodec/vcu/test_dec04/tools/extract_dec02_trace.sh [OUT]
#
# Host only, /tmp only, no phone.  It uses DEC02's own harness read-only:
#   facts/vdec_trace/tools/stage.py   prepares a /tmp copy of the tree and of DEC1's driver with
#                                    n2tr, the VDEC_ALL window and MARK phases (unmodified)
#   facts/vdec_trace/probe/n2tr.[ch]  the recording wrapper (unmodified)
# and the phone's own vendor libraries from gpu/build/cam-vendor-20261109/vfs at run time.
# The only edit to the staged copy (in /tmp) is one n2tr_mark("au<index>") before each frame
# START, so the trace can be split per access unit.
#
# Output: <OUT>/dec02_au1.inc (default: this package directory).  The committed file is one
# capture; a re-capture is byte-identical in the per-thread order but the *interleaving* of the
# vendor's two decode threads is scheduling-dependent, so a fresh run may order the same ops
# differently.  The test's literals (1032 reads, 542 writes, one call per read with the merged
# store) are properties of that capture, not of the interleaving.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
pkg=$(cd "$here/.." && pwd)
hwd=$(cd "$pkg/../.." && pwd)                    # gpu/hwcodec
root=$(cd "$hwd/../.." && pwd)                   # the work tree root
vfs=$root/gpu/build/cam-vendor-20261109/vfs
out=${1:-$pkg}
work=/tmp/n2hw-DEC04/dec02
vec=$work/vec
jobs=2
nice="nice -n 10"

[ -d "$vfs/lib" ] || { echo "DEC04 extract: vendor directory missing: $vfs" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "DEC04 extract: ffmpeg missing" >&2; exit 1; }
mkdir -p "$work" "$vec" "$out"

echo "DEC04 extract: stage DEC02's tree and driver"
python3 "$hwd/facts/vdec_trace/tools/stage.py" "$work/tree" "$work/vdec_ref.c"

python3 - "$work/vdec_ref.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "\t\trc = start_au(sbuf + off, n, 1, 1000u + frames_in);"
new = ("\t\t{ char _t[32]; snprintf(_t, sizeof _t, \"au%u\", frames_in); n2tr_mark(tr, _t); }\n" + old)
if s.count(old) != 1:
    sys.exit("DEC04 extract: per-AU start_au anchor not found")
open(p, "w").write(s.replace(old, new, 1))
print("DEC04 extract: per-AU marker added to the /tmp driver only")
PY

echo "DEC04 extract: build libn2vhost (-j$jobs, $nice)"
$nice make -s -C "$work/tree/host" "$work/o2/libn2vhost.a" OUT="$work" JOBS="$jobs" -j"$jobs" \
	>"$work/build.log" 2>&1 || { tail -20 "$work/build.log"; exit 1; }
grep -E '^N2VHOST build:' "$work/build.log" | tail -1

inc="-I$work/tree -I$work/tree/include -I$work/tree/tests/hw -I$work/tree/tests/emu -I$work/tree/vcu"
inc="$inc -I$work/tree/ap -I$work/tree/host -I$work/tree/shim -I$hwd/facts/vdec_trace/probe"
$nice clang -std=c11 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L $inc \
	"$work/vdec_ref.c" "$hwd/facts/vdec_trace/probe/n2tr.c" -o "$work/vdec_ref" \
	-Wl,--whole-archive "$work/o2/libn2vhost.a" -Wl,--no-whole-archive -lpthread -lm \
	>"$work/build-driver.log" 2>&1 || { tail -20 "$work/build-driver.log"; exit 1; }

[ -s "$vec/t320b.h264" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 \
	-frames:v 12 -pix_fmt yuv420p -c:v libx264 -profile:v main -bf 2 -g 12 "$vec/t320b.h264"

echo "DEC04 extract: one 320x240 session, 12 AUs"
N2DEC02_TRACE="$work/t320.trace" VR_REC="$work/t02.txt" VR_FAKEIRQ=1 \
	timeout 300 "$work/vdec_ref" "$vfs" "$vec/t320b.h264" 1000 >"$work/t320.log" 2>&1 \
	|| { tail -30 "$work/t320.log"; exit 1; }
grep -E "VR result" "$work/t320.log"

python3 - "$work/t320.trace" "$out/dec02_au1.inc" <<'PY'
import sys
trace, dst = sys.argv[1], sys.argv[2]
base = {2: 0x1602f000, 3: 0x16000000, 4: 0x16020000, 5: 0x16021000, 6: 0x16023000,
        7: 0x16025000, 8: 0x16010000, 9: 0x16011000, 10: 0x16004000, 11: 0x1600f000,
        12: 0x16000000}
au, seq, total, session = -1, [], {}, {0: 0, 1: 0}
for ln in open(trace):
    f = ln.split()
    if f[0] == "MARK":
        if f[1].startswith("au"):
            au = int(f[1][2:])
        continue
    if f[0] in ("R", "W") and au >= 0:
        w, off, val = int(f[1]), int(f[2], 0), int(f[3], 0)
        if w not in base or off % 4:
            sys.exit("DEC04 extract: unexpected access win %d off 0x%x" % (w, off))
        total[au] = total.get(au, 0) + 1
        session[0 if f[0] == "R" else 1] += 1
        if au == 1:
            seq.append((base[w] + off, val if f[0] == "W" else 0, f[0]))
if not seq:
    sys.exit("DEC04 extract: no AU 1 in the trace")
reads = sum(1 for s in seq if s[2] == "R")
writes = len(seq) - reads
lines = ["/* SPDX-License-Identifier: MIT */", "/*"]
lines += [" * dec02_au1.inc -- DEC02's measured H.264 register access sequence, one steady-state access",
          " * unit (AU 1 of 12) of the 320x240 -bf 2 session, in the order it was recorded.",
          " *",
          " * Source: gpu/hwcodec/facts/vdec_trace/ (DEC02), generated by",
          " * vcu/test_dec04/tools/extract_dec02_trace.sh from a /tmp run of DEC02's own stage.py and",
          " * n2tr recorder; the session totals are %d R / %d W, matching PAGES.md." % (session[0], session[1]),
          " * Each pair is { (absolute VDEC register offset << 1) | write, value }; reads carry value 0",
          " * (the harness ignores it).  %d ops: %d reads, %d writes.  Every access is a VDEC" % (len(seq), reads, writes),
          " * window and every write is 4-aligned, so the replay is exactly what the vendor did.",
          " * Session ops per AU: " + " ".join(str(total[a]) for a in sorted(total)) + ".",
          " */",
          "static const uint32_t dec02_au1_ops[] = {"]
for i in range(0, len(seq), 4):
    row = []
    for a, v, op in seq[i:i + 4]:
        row.append("0x%08xu, 0x%08xu," % ((a << 1) | (1 if op == "W" else 0), v))
    lines.append("\t" + " ".join(row))
lines += ["};", "#define DEC02_AU1_OPS %du" % len(seq),
          "#define DEC02_AU1_WRITES %du" % writes, "#define DEC02_AU1_READS %du" % reads]
open(dst, "w").write("\n".join(lines) + "\n")
print("DEC04 extract: %s, %d ops (%d R, %d W)" % (dst, len(seq), reads, writes))
PY
