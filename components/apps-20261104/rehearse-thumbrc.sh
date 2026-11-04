#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-thumbrc.sh / guest-thumbrc-undo.sh on a FAKE root (the scripts' own Python with /work/root and /src replaced), inside
# `unshare -r` so chown(0,0) works. The fake root carries the REAL Nord2Metal.bundle entries and receipt.json read from the rootfs
# mirror (read-only, autocycle/hfsx.py). Nothing touches the mirror or a phone.   usage: rehearse-thumbrc.sh
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-/tmp/n2hw-opus-ios/rehearse-thumbrc}; rm -rf "$F"; mkdir -p "$F/root"
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
if [ "$M" = 947f50db9b31 ]; then run guest-vt22.sh "$A/payload-vt22" | grep -E '^VT22_INSTALLED'; M=dc40f3eb7fa8; fi
if [ "$M" = dc40f3eb7fa8 ]; then run guest-vt23.sh "$A/payload-vt23" | grep -E '^VT23_INSTALLED'; M=59212d387b54; fi
if [ "$M" = 59212d387b54 ]; then run guest-thumbstage.sh "$A/payload-thumbstage" | grep -E '^THUMBSTAGE_INSTALLED'; fi
if [ "$M" = e1f82e2034be ]; then echo "mirror is at vt20: installing vt21 first (the phone's state)"; run guest-vt21.sh "$A/payload-vt21" | grep -E '^VT21_INSTALLED'; fi
if [ "$M" = 0d06e6854825 ]; then run guest-vt20.sh "$A/payload-vt20" | grep -E '^VT20_INSTALLED'; run guest-vt21.sh "$A/payload-vt21" | grep -E '^VT21_INSTALLED'; fi
if [ "$M" = 6482590ef28a ]; then echo "mirror is at thumbstage: installing thumbsync first"; run guest-thumbsync.sh "$A/payload-thumbsync" | grep -E '^THUMBSYNC_INSTALLED'; fi
S0=$(st); echo "start: $S0 Nord2Metal $(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12) bundle: $NAMES"
echo "== install"; run guest-thumbrc.sh "$A/payload-thumbrc" | grep -E '^THUMBRC_|^CC_'
[ "$(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-64)" = "$(python3 -c "import json;print(json.load(open('$A/payload-thumbrc/plan.json'))['new_sha256']['libredwoodair.dylib'])")" ] && echo "  libredwoodair.dylib = y2s66-thumbrc: ok"
[ -f "$R/private/var/db/nord2-metal/libredwoodair.dylib.pre-thumbrc" ] && echo "  backup present: ok"
echo "== again (idempotent)"; run guest-thumbrc.sh "$A/payload-thumbrc" | grep -E '^THUMBRC_ALREADY|^CC_'
echo "== undo"; run guest-thumbrc-undo.sh "$A/payload-thumbrc-undo" | grep -E '^THUMBRC_|^CC_'
echo "== undo again"; run guest-thumbrc-undo.sh "$A/payload-thumbrc-undo" | grep -E '^THUMBRC_UNDONE|^CC_'
S1=$(st); echo "after undo: $S1"
echo "== a foreign libredwoodair.dylib must stop the install and change nothing"
cp "$R/$BU/libredwoodair.dylib" "$F/n.ours"; echo foreign > "$R/$BU/libredwoodair.dylib"
if run guest-thumbrc.sh "$A/payload-thumbrc" > "$F/foreign.log" 2>&1; then echo "REHEARSAL FAILED: foreign accepted"; exit 1; fi
tail -1 "$F/foreign.log"; [ "$(cat "$R/$BU/libredwoodair.dylib")" = foreign ] && [ ! -e "$R/private/var/db/nord2-metal/libredwoodair.dylib.pre-thumbrc" ] && echo "  nothing changed: ok"
cp "$F/n.ours" "$R/$BU/libredwoodair.dylib"
[ "$S0" = "$S1" ] && echo "REHEARSAL_OK root byte-identical after install+undo ($S0)" || { echo "REHEARSAL FAILED: $S0 != $S1"; exit 1; }
