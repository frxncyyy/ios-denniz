#!/bin/bash
# rehearsal of guest-locationd1.sh / guest-locationd1-undo.sh on a fake root built from the rootfs mirror (read-only):
# the scripts' own Python, /work/root -> fake root, os.chown -> no-op (host is not root). Ends with REHEARSAL_OK.
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d)
P=private/var/mobile/Library/Preferences; C=private/var/root/Library/Caches/locationd
python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img get /$P $F/$P com.apple.locationd.plist 2>/dev/null >/dev/null
python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img get /$C $F/$C clients.plist 2>/dev/null >/dev/null
rm -f $F/$P/com.apple.locationd.StatusBarIconManager.plist; mkdir -p $F/private/var/db
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|^import os, plistlib\(.*\)$|import os, plistlib\1\nos.chown=lambda *a: None|" | python3 -; }
st() { python3 - "$F" <<'PY'
import plistlib,sys,hashlib,os
F=sys.argv[1]; p=F+'/private/var/mobile/Library/Preferences/com.apple.locationd.plist'; c=F+'/private/var/root/Library/Caches/locationd/clients.plist'
a=plistlib.load(open(p,'rb')); b=plistlib.load(open(c,'rb'))
h=lambda x: hashlib.sha256(open(x,'rb').read()).hexdigest()[:12]
print('prefs',h(p),'LSE=%r'%a.get('LocationServicesEnabledIn8.0','<absent>'),'| clients',h(c),'camera=%r'%b.get('com.apple.camera'),'| db:',sorted(os.listdir(F+'/private/var/db')))
PY
}
S0=$(st); echo "start:   $S0"
run guest-locationd1.sh;      S1=$(st); echo "install: $S1"
run guest-locationd1.sh;      S2=$(st); echo "again:   $S2"; [ "$S1" = "$S2" ]
echo "$S1" | grep -q "LSE=True" && echo "$S1" | grep -q "'Authorization': 2"
run guest-locationd1-undo.sh; S3=$(st); echo "undo:    $S3"
run guest-locationd1-undo.sh; S4=$(st); echo "again:   $S4"; [ "$S3" = "$S4" ]
c() { echo "$1" | sed 's/prefs [0-9a-f]* //; s/clients [0-9a-f]* //; s/| db:.*//'; }   # content, not bytes (plistlib re-encodes the bplist)
[ "$(c "$S0")" = "$(c "$S3")" ] || { echo "undo did not restore the start content"; exit 1; }
echo "--- Camera already decided (Authorization 1 = Never): must refuse and change nothing"
python3 -c "
import plistlib,sys;p=sys.argv[1];d=plistlib.load(open(p,'rb'));d['com.apple.camera']['Authorization']=1;plistlib.dump(d,open(p,'wb'),fmt=plistlib.FMT_BINARY)" $F/$C/clients.plist
B1=$(st); run guest-locationd1.sh 2>&1 | tail -1 || true; B2=$(st); echo "after:   $B2"; [ "$B1" = "$B2" ]
rm -rf "$F"; echo REHEARSAL_OK
