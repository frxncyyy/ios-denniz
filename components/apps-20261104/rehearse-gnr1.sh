#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-gnr1.sh / guest-gnr1-undo.sh on a FAKE root (the scripts' own Python with /work/root and /src replaced), inside
# `unshare -r`, modelled on rehearse-vt23.sh. The fake root carries the REAL Nord2Metal.bundle entries and receipt.json read from the
# rootfs mirror (read-only); where the mirror is older than the phone (vt23) the vt22/vt23 installs are replayed first.  usage: rehearse-gnr1.sh
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-/tmp/n2hw-opus-ios/rehearse-gnr1}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"; BU=System/Library/Extensions/Nord2Metal.bundle
mkdir -p "$R/$BU" "$R/private/var/db/nord2-metal"
$H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" receipt.json >/dev/null 2>&1
NAMES=$(python3 -c "import json;inv=json.load(open('$R/private/var/db/nord2-metal/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
$H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
chmod 755 "$R/$BU/Nord2Metal"
run() { local g=$1; case $g in /*) ;; *) g=$A/$1;; esac
        sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$g" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|" > "$F/$(basename $1).py"
        unshare -r python3 "$F/$(basename $1).py"; }
st() { (cd "$R" && find . -type f ! -path './private/var/db/nord2-metal/receipt.json' -exec sha256sum {} + | sort; sha256sum private/var/db/nord2-metal/receipt.json) | sha256sum | cut -c1-16; }
M=$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)
echo "mirror Nord2Metal $M libredwoodair $(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-12)"
if [ "$M" = 947f50db9b31 ]; then run guest-vt22.sh "$A/payload-vt22" | grep -E '^VT22_INSTALLED'; M=$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12); fi
if [ "$M" = dc40f3eb7fa8 ]; then run guest-vt23.sh "$A/payload-vt23" | grep -E '^VT23_INSTALLED'; rm -f "$R/private/var/db/nord2-metal/Nord2Metal.pre-vt23.keep"; fi
[ "$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)" = 59212d387b54 ] || { echo "REHEARSAL FAILED: fake root not at vt23 (Nord2Metal $(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12))"; exit 1; }
S0=$(st); echo "start (= phone state vt23): $S0 bundle: $NAMES"
echo "== install"; run guest-gnr1.sh "$A/payload-gnr1" | grep -E '^GNR1_|^CC_'
[ "$(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-64)" = "$(python3 -c "import json;print(json.load(open('$A/payload-gnr1/plan.json'))['new_sha256']['libredwoodair.dylib'])")" ] && echo "  libredwoodair.dylib = y2s63-gnr1: ok"
[ -f "$R/private/var/db/nord2-metal/libredwoodair.dylib.pre-gnr1" ] && echo "  backup present: ok"
[ -f "$R/private/var/db/nord2-gpu-gnr1-trace" ] && [ -f "$R/private/var/db/nord2-metal/gnr1-trace.made-by-gnr1" ] && echo "  trace switch + marker: ok"
echo "== again (idempotent)"; run guest-gnr1.sh "$A/payload-gnr1" | grep -E '^GNR1_ALREADY|^GNR1_SWITCH|^CC_'
echo "== undo"; run guest-gnr1-undo.sh "$A/payload-gnr1-undo" | grep -E '^GNR1_|^CC_'
echo "== undo again"; run guest-gnr1-undo.sh "$A/payload-gnr1-undo" | grep -E '^GNR1_UNDONE|^CC_'
S1=$(st); echo "after undo: $S1"
echo "== a switch made by someone else survives install+undo"
touch "$R/private/var/db/nord2-gpu-gnr1-trace"
run guest-gnr1.sh "$A/payload-gnr1" | grep -E '^GNR1_SWITCH'; run guest-gnr1-undo.sh "$A/payload-gnr1-undo" | grep -E '^GNR1_UNDONE'
[ -f "$R/private/var/db/nord2-gpu-gnr1-trace" ] && echo "  foreign switch kept: ok"; rm -f "$R/private/var/db/nord2-gpu-gnr1-trace"
echo "== a foreign libredwoodair.dylib must stop the install and change nothing"
cp "$R/$BU/libredwoodair.dylib" "$F/l.ours"; echo foreign > "$R/$BU/libredwoodair.dylib"
if run guest-gnr1.sh "$A/payload-gnr1" > "$F/foreign.log" 2>&1; then echo "REHEARSAL FAILED: foreign accepted"; exit 1; fi
tail -1 "$F/foreign.log"; [ "$(cat "$R/$BU/libredwoodair.dylib")" = foreign ] && [ ! -e "$R/private/var/db/nord2-metal/libredwoodair.dylib.pre-gnr1" ] && [ ! -e "$R/private/var/db/nord2-gpu-gnr1-trace" ] && echo "  nothing changed: ok"
cp "$F/l.ours" "$R/$BU/libredwoodair.dylib"
S2=$(st)
echo "== on y2s64-vt24 (a343b4f1's Nord2Metal): vt24 first, then gnr1, undo gnr1, undo vt24"
V24=guest-vt24.sh
if ! sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/guest-vt24.sh" | sed '1d;$d' | python3 -c "import sys,ast; ast.parse(sys.stdin.read())" 2>/dev/null; then
  echo "  WARNING: guest-vt24.sh's Python does not parse (owner a343b4f1; an apostrophe in a print string?): rehearsing on a copy with apostrophes inside print strings removed"
  mkdir -p "$F/v24"; sed "/print('CC_JOB_ADDED/s/GPU's/GPU/" "$A/guest-vt24.sh" > "$F/v24/guest-vt24.sh"; V24=$F/v24/guest-vt24.sh
fi
run "$V24" "$A/payload-vt24" | grep -E '^VT24_INSTALLED'
run guest-gnr1.sh "$A/payload-gnr1" | grep -E '^GNR1_INSTALLED|^GNR1_NORD2METAL'
run guest-gnr1-undo.sh "$A/payload-gnr1-undo" | grep -E '^GNR1_UNDONE'
run guest-vt24-undo.sh "$A/payload-vt24-undo" | grep -E '^VT24_UNDONE'
S3=$(st); [ "$S0" = "$S3" ] && echo "  vt24 + gnr1 + undo both: byte-identical: ok" || { echo "REHEARSAL FAILED: vt24 path $S3"; exit 1; }
[ "$S0" = "$S1" ] && [ "$S0" = "$S2" ] && echo "REHEARSAL_OK root byte-identical after install+undo ($S0)" || { echo "REHEARSAL FAILED: $S0 / $S1 / $S2"; exit 1; }
