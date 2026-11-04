#!/bin/bash
# rehearsal of guest-locfix2.sh / -undo on a fake root with the phone's current state from the mirror (locfix1 installed:
# /usr/libexec/locationd = bea79d17 + /private/var/db/nord2-locfix/{locationd.orig,receipt.json}); also a fresh original root.
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d -p "$A" .rehearse-locfix2.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
$H get /usr/libexec $F/usr/libexec locationd >/dev/null 2>&1; mkdir -p $F/private/var/db
$H get /private/var/db/nord2-locfix $F/private/var/db/nord2-locfix >/dev/null 2>&1 || true
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$A/payload-locfix2/|g; s|^import os, json, hashlib\(.*\)$|import os, json, hashlib\1\nos.chown=lambda *a: None|" | python3 -; }
st() { echo "locationd $(sha256sum $F/usr/libexec/locationd | cut -c1-12) db $(ls $F/private/var/db | tr '\n' ' ') orig $( [ -f $F/private/var/db/nord2-locfix/locationd.orig ] && sha256sum $F/private/var/db/nord2-locfix/locationd.orig | cut -c1-12)"; }
S0=$(st); echo "mirror:  $S0"; echo "$S0" | grep -q 'locationd bea79d177a24 db nord2-locfix  orig 75066240cfcc'
run guest-locfix2.sh; S1=$(st); echo "install: $S1"; echo "$S1" | grep -q 'locationd 01eb60f06601'
run guest-locfix2.sh; S2=$(st); [ "$S1" = "$S2" ]
run guest-locfix2-undo.sh; S3=$(st); echo "undo:    $S3"; echo "$S3" | grep -q 'locationd 75066240cfcc db  orig $'
run guest-locfix2-undo.sh; S4=$(st); [ "$S3" = "$S4" ]
echo "--- fresh original root"; run guest-locfix2.sh; S5=$(st); echo "install: $S5"; echo "$S5" | grep -q 'locationd 01eb60f06601 db nord2-locfix  orig 75066240cfcc'
run guest-locfix2-undo.sh; S6=$(st); [ "$S6" = "$S3" ]
echo "--- an unknown locationd: must refuse and change nothing"; printf 'x' >> $F/usr/libexec/locationd; B7=$(st)
run guest-locfix2.sh 2>&1 | tail -1 || true; S7=$(st); [ "$S7" = "$B7" ]
rm -rf "$F"; echo REHEARSAL_OK
