#!/bin/bash
# rehearsal of guest-gnssstep1.sh / -undo on a fake root from the mirror (launchd.plist, Metal receipt, /usr/local/libexec)
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d -p "$A" .rehearse-gnssl8a.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
$H get /System/Library/xpc $F/System/Library/xpc launchd.plist >/dev/null 2>&1; rm -f $F/System/Library/xpc/launchd.plist.sig
$H get /private/var/db/nord2-metal $F/private/var/db/nord2-metal receipt.json >/dev/null 2>&1; mkdir -p $F/usr/local/libexec
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$A/payload-gnssl8a/|g; s|^import os, json, hashlib, plistlib\(.*\)$|import os, json, hashlib, plistlib\1\nos.chown=lambda *a: None|" | python3 -; }
st() { python3 - "$F" <<'PY'
import plistlib,sys,os,hashlib,json
F=sys.argv[1]; d=plistlib.load(open(F+'/System/Library/xpc/launchd.plist','rb'))['LaunchDaemons']
j=d.get('/System/Library/LaunchDaemons/com.nord2.gnssstep.plist')
r=json.load(open(F+'/private/var/db/nord2-metal/receipt.json'))['installed_launchd_sha256']
h=hashlib.sha256(open(F+'/System/Library/xpc/launchd.plist','rb').read()).hexdigest()
print('jobs',len(d),'gnssstep',bool(j),'receipt==plist',r==h,'libexec',sorted(os.listdir(F+'/usr/local/libexec')),'db',sorted(x for x in os.listdir(F+'/private/var/db') if 'gnssprobe' in x))
PY
}
S0=$(st); echo "start:   $S0"; echo "$S0" | grep -q 'receipt==plist True'
PA=$(python3 -c 'import json,sys;print(" ".join(json.load(open(sys.argv[1]))["args"]))' "$A/payload-gnssl8a/plan.json")
run guest-gnssstep1.sh; S1=$(st); echo "install: $S1"; echo "$S1" | grep -q "gnssstep True receipt==plist True"
python3 - "$F" "$PA" <<'PY'
import plistlib,sys
j=plistlib.load(open(sys.argv[1]+"/System/Library/xpc/launchd.plist","rb"))["LaunchDaemons"]["/System/Library/LaunchDaemons/com.nord2.gnssstep.plist"]
a=" ".join(j["ProgramArguments"]); print("args:", a); assert a.endswith(sys.argv[2]), a
PY
run guest-gnssstep1.sh; S2=$(st); [ "$S1" = "$S2" ]
run guest-gnssstep1-undo.sh; S3=$(st); echo "undo:    $S3"; echo "$S3" | grep -q "gnssstep False receipt==plist True libexec \[\] db \[\]"  # the mirror may already carry the job from the last phone run
run guest-gnssstep1-undo.sh; S4=$(st); [ "$S3" = "$S4" ]
echo "--- launchd.plist not matching the Metal receipt: must refuse and change nothing"
python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['installed_launchd_sha256']='0'*64;json.dump(d,open(p,'w'))" $F/private/var/db/nord2-metal/receipt.json
run guest-gnssstep1.sh 2>&1 | tail -1 || true; S5=$(st); echo "after:   $S5"; echo "$S5" | grep -q "gnssstep False"
rm -rf "$F"; echo REHEARSAL_OK
