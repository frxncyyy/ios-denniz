#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-libmerge2.sh / guest-libmerge2-undo.sh (y2s70-libmerge2: libredwoodair.dylib), over y2s63-vt23: from the base and from camstats1 on a FAKE root (the scripts' own Python with /work/root and /src replaced), inside
# `unshare -r` so chown(0,0) works. The fake root carries the REAL Nord2Metal.bundle entries and receipt.json read from the rootfs
# mirror (read-only, autocycle/hfsx.py). Nothing touches the mirror or a phone.   usage: rehearse-libmerge2.sh
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-/tmp/n2hw-opus-ios/rehearse-libmerge2}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"; BU=System/Library/Extensions/Nord2Metal.bundle
mkdir -p "$R/$BU" "$R/private/var/db/nord2-metal"
$H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" receipt.json >/dev/null 2>&1
NAMES=$(python3 -c "import json;inv=json.load(open('$R/private/var/db/nord2-metal/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
$H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
chmod 755 "$R/$BU/Nord2Metal"
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|" > "$F/$1.py"
        unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f ! -path './private/var/db/nord2-metal/receipt.json' -exec sha256sum {} + | sort; sha256sum private/var/db/nord2-metal/receipt.json) | sha256sum | cut -c1-16; }
M=$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)
if [ "$M" = 947f50db9b31 ]; then run guest-vt22.sh "$A/payload-vt22" | grep -E '^VT22_INSTALLED'; fi
if [ "$M" = e1f82e2034be ]; then echo "mirror is at vt20: installing vt21 first (the phone's state)"; run guest-vt21.sh "$A/payload-vt21" | grep -E '^VT21_INSTALLED'; fi
if [ "$M" = 0d06e6854825 ]; then run guest-vt20.sh "$A/payload-vt20" | grep -E '^VT20_INSTALLED'; run guest-vt21.sh "$A/payload-vt21" | grep -E '^VT21_INSTALLED'; fi
if [ "$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)" != 59212d387b54 ]; then run guest-vt23.sh "$A/payload-vt23" | grep -E '^VT23_INSTALLED'; fi
L="$R/$BU/libredwoodair.dylib"; NEW=$(python3 -c "import json;print(json.load(open('$A/payload-libmerge2/plan.json'))['new_sha256']['libredwoodair.dylib'])")
S0=$(st); echo "start (vt23): $S0 Nord2Metal $(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12) lib $(sha256sum < "$L" | cut -c1-12)"
echo "== 1. from the base: install"; run guest-libmerge2.sh "$A/payload-libmerge2" | grep -E '^LIBMERGE2_(FROM|INSTALLED|GNR1)|^CC_'
[ "$(sha256sum < "$L" | cut -c1-64)" = "$NEW" ] && echo "  library = y2s70-libmerge2: ok" || { echo "REHEARSAL FAILED: not installed"; exit 1; }
echo "== again (idempotent)"; run guest-libmerge2.sh "$A/payload-libmerge2" | grep -E '^LIBMERGE2_ALREADY'
echo "== undo"; run guest-libmerge2-undo.sh "$A/payload-libmerge2-undo" | grep -E '^LIBMERGE2_UNDONE|^CC_RECEIPT'
echo "== undo again"; run guest-libmerge2-undo.sh "$A/payload-libmerge2-undo" | grep -E '^LIBMERGE2_UNDONE'
S1=$(st); [ "$S0" = "$S1" ] && echo "  root byte-identical after install+undo: ok" || { echo "REHEARSAL FAILED: $S0 != $S1"; exit 1; }
echo "== 2. from camstats1"; run guest-camstats1.sh "$A/payload-camstats1" | grep -E '^CAMSTATS1_INSTALLED'
S2=$(st)
run guest-libmerge2.sh "$A/payload-libmerge2" | grep -E '^LIBMERGE2_(FROM|INSTALLED)'
[ "$(sha256sum < "$L" | cut -c1-64)" = "$NEW" ] && echo "  library = y2s70-libmerge2 over camstats1: ok" || { echo "REHEARSAL FAILED"; exit 1; }
run guest-libmerge2-undo.sh "$A/payload-libmerge2-undo" | grep -E '^LIBMERGE2_UNDONE'
S3=$(st); [ "$S2" = "$S3" ] && echo "  back to camstats1 byte-identical: ok" || { echo "REHEARSAL FAILED: $S2 != $S3"; exit 1; }
run guest-camstats1-undo.sh "$A/payload-camstats1-undo" | grep -E '^CAMSTATS1_UNDONE'
S4=$(st); [ "$S0" = "$S4" ] && echo "  camstats1 undone after: back to the start byte-identical: ok" || { echo "REHEARSAL FAILED: $S0 != $S4"; exit 1; }
echo "== 3. a foreign library must stop the install and change nothing"
cp "$L" "$F/l.ours"; echo foreign > "$L"
if run guest-libmerge2.sh "$A/payload-libmerge2" > "$F/foreign.log" 2>&1; then echo "REHEARSAL FAILED: foreign accepted"; exit 1; fi
tail -1 "$F/foreign.log" | cut -c1-140; [ "$(cat "$L")" = foreign ] && [ ! -e "$R/private/var/db/nord2-metal/libredwoodair.dylib.pre-libmerge2" ] && echo "  nothing changed: ok"
cp "$F/l.ours" "$L"
echo "REHEARSAL_OK libmerge2 over vt23: from the base and from camstats1 ($S0)"
