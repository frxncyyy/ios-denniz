#!/bin/bash
# rehearsal of guest-locsim1.sh / -undo on a fake root from the mirror, which already holds locationd1 (committed by loc1).
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d); SRC=$(mktemp -d)
P=private/var/mobile/Library/Preferences; C=private/var/root/Library/Caches/locationd
python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img get /$P $F/$P com.apple.locationd.plist 2>/dev/null >/dev/null
python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img get /$C $F/$C clients.plist 2>/dev/null >/dev/null
rm -f $F/$P/com.apple.locationd.StatusBarIconManager.plist; mkdir -p $F/private/var/db; cp $A/payload-locsim1/sim.json $SRC/
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$SRC/|g; s|^import os, plistlib\(.*\)$|import os, plistlib\1\nos.chown=lambda *a: None|" | python3 -; }
st() { python3 -c "
import plistlib,sys,os;d=plistlib.load(open(sys.argv[1]+'/$P/com.apple.locationd.plist','rb'))
print(sorted(d.items()), sorted(os.listdir(sys.argv[1]+'/private/var/db')))" $F; }
S0=$(st); echo "mirror (phone, locationd1 already committed): $S0"; echo "$S0" | grep -q "LocationServicesEnabledIn8.0', True" || { echo "mirror has no locationd1"; exit 1; }
run guest-locsim1.sh; S1=$(st); echo "install: $S1"
run guest-locsim1.sh; S2=$(st); [ "$S1" = "$S2" ]
echo "$S1" | grep -q "('SimulatedLocationAccuracy', 10.0)"
run guest-locsim1-undo.sh; S3=$(st); echo "undo:    $S3"
run guest-locsim1-undo.sh; S4=$(st); [ "$S3" = "$S4" ]; [ "$S0" = "$S3" ]
echo "--- bad sim.json (accuracy 0): must refuse and change nothing"
python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['SimulatedLocationAccuracy']=0;json.dump(d,open(p,'w'))" $SRC/sim.json
run guest-locsim1.sh 2>&1 | tail -1 || true; S5=$(st); [ "$S5" = "$S0" ]
rm -rf "$F" "$SRC"; echo REHEARSAL_OK
