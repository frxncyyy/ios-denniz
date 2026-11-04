#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-camzc2.sh / guest-camzc2-undo.sh (camera zero-copy switch /private/var/db/nord2-camera-zerocopy) on a FAKE root, inside
# `unshare -r` so chown(0,0) and the owner check work. The fake root carries the mirror's /private/var/db/nord2-camera-* files, receipt.json and
# gnr1 switch (read-only, autocycle/hfsx.py). Nothing touches the mirror or a phone.
# usage: rehearse-camzc2.sh      (scratch in $N2_REHEARSE_DIR or gpu/build/tmp-y2s75/rehearse-camzc2, removed at the end)
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-$B/tmp-y2s75/rehearse-camzc2}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"; DB=private/var/db/nord2-metal; G=private/var/db/nord2-camera-zerocopy
mkdir -p "$R/$DB" "$R/$DB/kept" "$R/$DB/refused"; chmod 1777 "$R/$DB/kept" "$R/$DB/refused"
$H get /$DB "$R/$DB" receipt.json >/dev/null 2>&1 || true
CAMS=$($H ls /private/var/db 2>/dev/null | awk '{print $NF}' | grep '^nord2-camera-' | tr '\n' ' ' || true)
$H get /private/var/db "$R/private/var/db" nord2-gpu-gnr1-trace $CAMS >/dev/null 2>&1 || true
[ -f "$R/$DB/receipt.json" ] || { echo "REHEARSAL FAILED: no receipt from the mirror"; exit 1; }
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
# state: every path with type, mode, owner (as seen as root in the namespace), size and content hash
st() { (cd "$R" && unshare -r find . -printf '%p %y %m %U:%G %s\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
fail() { echo "REHEARSAL FAILED: $*"; exit 1; }
S0=$(st); echo "start (mirror): $S0 camera switches on the mirror: ${CAMS:-none}"
[ -e "$R/$G" ] && fail "the mirror already has $G: the rehearsal expects it absent"
echo "== 1. install"; run guest-camzc2.sh | grep -E '^CAMZC2_|^CC_'
[ "$(cd "$R" && unshare -r stat -c '%s %a %u:%g %F' $G)" = "0 644 0:0 regular empty file" ] && echo "  $G: empty, 644, root:wheel: ok" || fail "switch wrong: $(cd "$R" && unshare -r stat -c '%s %a %u:%g %F' $G)"
echo "== again (idempotent)"; SI=$(st); run guest-camzc2.sh | grep -E '^CAMZC2_SWITCH'
[ "$(st)" = "$SI" ] && echo "  reinstall changed nothing: ok" || fail "reinstall changed the root"
echo "== undo"; run guest-camzc2-undo.sh | grep -E '^CAMZC2_'
[ "$(st)" = "$S0" ] && echo "  root byte-identical after install+undo: ok" || fail "undo differs"
echo "== undo again"; run guest-camzc2-undo.sh | grep -E '^CAMZC2_UNDONE'
[ "$(st)" = "$S0" ] && echo "  root byte-identical after the second undo: ok" || fail "second undo differs"
echo "== 2. a foreign switch (guest-vcodec-camzc.sh's, with its marker text) must be left alone by install and undo"
printf 'nord2 camzc switch (guest-vcodec-camzc.sh)\n' > "$R/$G"; chmod 644 "$R/$G"; SF=$(st)
run guest-camzc2.sh | grep -E '^CAMZC2_SWITCH'; [ "$(st)" = "$SF" ] && echo "  install left it: ok" || fail "install touched the foreign switch"
run guest-camzc2-undo.sh | grep -E '^CAMZC2_UNDONE'; [ "$(st)" = "$SF" ] && echo "  undo left it: ok" || fail "undo removed the foreign switch"
rm "$R/$G"; [ "$(st)" = "$S0" ] || fail "not back to the mirror state"
echo "== 3. our switch changed afterwards (not empty) must stop the undo and change nothing"
run guest-camzc2.sh | grep -E '^CAMZC2_SWITCH'; echo changed > "$R/$G"; SC=$(st)
if run guest-camzc2-undo.sh > "$F/changed.log" 2>&1; then fail "undo removed a changed switch"; fi
tail -1 "$F/changed.log" | cut -c1-140; [ "$(st)" = "$SC" ] && echo "  nothing changed: ok" || fail "refused undo changed something"
: > "$R/$G"; run guest-camzc2-undo.sh | grep -E '^CAMZC2_UNDONE'; [ "$(st)" = "$S0" ] && echo "  emptied again, undo byte-identical: ok" || fail "final undo differs"
echo "== 4. a stray marker without the switch must stop the install and change nothing"
printf 'camzc2 created /private/var/db/nord2-camera-zerocopy\n' > "$R/$DB/camera-zerocopy.made-by-camzc2"; SM=$(st)
if run guest-camzc2.sh > "$F/marker.log" 2>&1; then fail "install accepted a stray marker"; fi
tail -1 "$F/marker.log" | cut -c1-140; [ "$(st)" = "$SM" ] && echo "  nothing changed: ok" || fail "refused install changed something"
rm "$R/$DB/camera-zerocopy.made-by-camzc2"; [ "$(st)" = "$S0" ] || fail "not back to the mirror state"
echo "REHEARSAL_OK camzc2: mirror state ($S0) install (empty 644 root:wheel)/idempotent/undo x2 byte-identical; foreign switch left alone; changed switch and stray marker refused"
rm -rf "$F"; rmdir "$(dirname "$F")" 2>/dev/null || true
