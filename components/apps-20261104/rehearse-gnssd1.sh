#!/bin/bash
# rehearsal of guest-gnssd1.sh / -undo on a fake root from the mirror (launchd.plist, Metal receipt, /usr/local/libexec),
# with the gnssstep gate present (must be moved by install and restored by undo).
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d -p "$A" .rehearse-gnssd1.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
$H get /System/Library/xpc $F/System/Library/xpc launchd.plist >/dev/null 2>&1; rm -f $F/System/Library/xpc/launchd.plist.sig
$H get /private/var/db/nord2-metal $F/private/var/db/nord2-metal receipt.json >/dev/null 2>&1; mkdir -p $F/usr/local/libexec
printf 'run\n' > $F/private/var/db/nord2-gnssstep
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$A/payload-gnssd1/|g; s|^import os, json, hashlib, plistlib\(.*\)$|import os, json, hashlib, plistlib\1\nos.chown=lambda *a: None|" | python3 -; }
st() { python3 - "$F" <<'PY'
import plistlib,sys,os,hashlib,json
F=sys.argv[1]; d=plistlib.load(open(F+'/System/Library/xpc/launchd.plist','rb'))['LaunchDaemons']
j=d.get('/System/Library/LaunchDaemons/com.nord2.gnssd.plist')
r=json.load(open(F+'/private/var/db/nord2-metal/receipt.json'))['installed_launchd_sha256']
h=hashlib.sha256(open(F+'/System/Library/xpc/launchd.plist','rb').read()).hexdigest()
n=F+'/private/var/db/n2gnss'; nd=(sorted(os.listdir(n)), oct(os.stat(n).st_mode&0o777)) if os.path.isdir(n) else None
print('jobs',len(d),'gnssd',bool(j),'receipt==plist',r==h,'libexec',sorted(os.listdir(F+'/usr/local/libexec')),'n2gnss',nd,'db',sorted(x for x in os.listdir(F+'/private/var/db') if 'gnss' in x))
PY
}
S0=$(st); echo "start:   $S0"; echo "$S0" | grep -q "receipt==plist True.*db \['nord2-gnssstep'\]"
PA=$(python3 -c 'import json,sys;print(" ".join(json.load(open(sys.argv[1]))["args"]))' "$A/payload-gnssd1/plan.json")
run guest-gnssd1.sh; S1=$(st); echo "install: $S1"
echo "$S1" | grep -q "gnssd True receipt==plist True libexec \['n2mnld'\] n2gnss (\['MNL_Config.vendor.xml', 'MNL_Config.xml'\], '0o700') db \['n2gnss', 'nord2-gnssd', 'nord2-gnssd.json', 'nord2-gnssstep.off-gnssd1'\]"
python3 - "$F" "$PA" <<'PY'
import plistlib,sys
j=plistlib.load(open(sys.argv[1]+"/System/Library/xpc/launchd.plist","rb"))["LaunchDaemons"]["/System/Library/LaunchDaemons/com.nord2.gnssd.plist"]
a=" ".join(j["ProgramArguments"]); print("args:", a); assert a=="/usr/local/libexec/n2mnld --gate /private/var/db/nord2-gnssd "+sys.argv[2], a
assert j["RunAtLoad"] and "KeepAlive" not in j and j["UserName"]=="root"
PY
cmp $A/payload-gnssd1/n2mnld $F/usr/local/libexec/n2mnld
run guest-gnssd1.sh; S2=$(st); [ "$S1" = "$S2" ]; echo "idempotent install ok"
run guest-gnssd1-undo.sh; S3=$(st); echo "undo:    $S3"; [ "$S0" = "$S3" ]
run guest-gnssd1-undo.sh; S4=$(st); [ "$S3" = "$S4" ]; echo "idempotent undo ok (gnssstep gate restored)"
echo "--- launchd.plist not matching the Metal receipt: must refuse and change nothing"
python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['installed_launchd_sha256']='0'*64;json.dump(d,open(p,'w'))" $F/private/var/db/nord2-metal/receipt.json
run guest-gnssd1.sh 2>&1 | tail -1 || true; S5=$(st); echo "after:   $S5"; echo "$S5" | grep -q "gnssd False.*libexec \[\] n2gnss None db \['nord2-gnssstep'\]"
rm -rf "$F"; echo REHEARSAL_OK
