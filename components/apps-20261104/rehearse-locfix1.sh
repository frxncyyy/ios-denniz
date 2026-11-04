#!/bin/bash
# rehearsal of guest-locfix1.sh / -undo on a fake root holding the mirror's /usr/libexec/locationd
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d)
python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img get /usr/libexec $F/usr/libexec locationd >/dev/null 2>&1; mkdir -p $F/private/var/db
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$A/payload-locfix1/|g; s|^import os, json, hashlib\(.*\)$|import os, json, hashlib\1\nos.chown=lambda *a: None|" | python3 -; }
st() { echo "locationd $(sha256sum $F/usr/libexec/locationd | cut -c1-12) db $(ls $F/private/var/db | tr '\n' ' ')"; }
S0=$(st); echo "start:   $S0"; echo "$S0" | grep -q 'locationd 75066240cfcc'
run guest-locfix1.sh; S1=$(st); echo "install: $S1"; echo "$S1" | grep -q 'locationd bea79d177a24 db nord2-locfix'
run guest-locfix1.sh; S2=$(st); [ "$S1" = "$S2" ]
run guest-locfix1-undo.sh; S3=$(st); echo "undo:    $S3"; [ "$S0" = "$S3" ]
run guest-locfix1-undo.sh; S4=$(st); [ "$S3" = "$S4" ]
echo "--- an unknown locationd: must refuse and change nothing"; printf 'x' >> $F/usr/libexec/locationd; B5=$(st)
run guest-locfix1.sh 2>&1 | tail -1 || true; S5=$(st); [ "$S5" = "$B5" ]
rm -rf "$F"; echo REHEARSAL_OK
