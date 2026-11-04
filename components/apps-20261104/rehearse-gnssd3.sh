#!/bin/bash
# (P) first, if the mirror already has gnssd installed (the phone after gnssd2): gnssd3 over it, idempotent, gnssd2 undo -> clean.
# rehearsal of guest-gnssd3.sh / -undo on a fake root from the mirror (launchd.plist, Metal receipt, /usr/local/libexec),
# with the gnssstep gate present. Scenarios: (a) fresh install, idempotent, gnssd3 undo == start;
# (b) the phone's real path: gnssd1 installed + a gnssd1b n2mnld.log in the dir -> gnssd3 installed over it (binary + args
# replaced, log kept) -> guest-gnssd1-undo.sh restores the start state; (c) gnssd3 over gnssd1 -> gnssd3 undo == start;
# (e) gnssd1 -> gnssd2 -> gnssd3 -> guest-gnssd2-undo.sh == start; (d) refusal when launchd.plist does not match the Metal receipt.
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d -p "$A" .rehearse-gnssd3.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
$H get /System/Library/xpc $F/System/Library/xpc launchd.plist >/dev/null 2>&1; rm -f $F/System/Library/xpc/launchd.plist.sig
$H get /private/var/db/nord2-metal $F/private/var/db/nord2-metal receipt.json >/dev/null 2>&1; mkdir -p $F/usr/local/libexec
$H get /usr/local/libexec $F/usr/local/libexec n2mnld >/dev/null 2>&1 || true
$H get /private/var/db $F/private/var/db nord2-gnssd nord2-gnssd.json nord2-gnssstep nord2-gnssstep.off-gnssd1 >/dev/null 2>&1 || true
$H get /private/var/db/n2gnss $F/private/var/db/n2gnss MNL_Config.xml MNL_Config.vendor.xml n2mnld.log n2mnld.log.1 >/dev/null 2>&1 || true
[ -d $F/private/var/db/n2gnss ] && chmod 700 $F/private/var/db/n2gnss; rmdir $F/private/var/db/n2gnss 2>/dev/null || true
run() { local p=$2; sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$A/$p/|g; s|^import os, json, hashlib, plistlib\(.*\)$|import os, json, hashlib, plistlib\1\nos.chown=lambda *a: None|" | python3 -; }
st() { python3 - "$F" <<'PY'
import plistlib,sys,os,hashlib,json
F=sys.argv[1]; d=plistlib.load(open(F+'/System/Library/xpc/launchd.plist','rb'))['LaunchDaemons']
j=d.get('/System/Library/LaunchDaemons/com.nord2.gnssd.plist')
r=json.load(open(F+'/private/var/db/nord2-metal/receipt.json'))['installed_launchd_sha256']
h=hashlib.sha256(open(F+'/System/Library/xpc/launchd.plist','rb').read()).hexdigest()
n=F+'/private/var/db/n2gnss'; nd=(sorted(os.listdir(n)), oct(os.stat(n).st_mode&0o777)) if os.path.isdir(n) else None
b=F+'/usr/local/libexec/n2mnld'; bs=hashlib.sha256(open(b,'rb').read()).hexdigest()[:12] if os.path.exists(b) else None
print('jobs',len(d),'gnssd',bool(j),'args',' '.join(j['ProgramArguments'][3:]) if j else None,'receipt==plist',r==h,'libexec',sorted(os.listdir(F+'/usr/local/libexec')),'bin',bs,'n2gnss',nd,'db',sorted(x for x in os.listdir(F+'/private/var/db') if 'gnss' in x))
PY
}
D2=$(sha256sum $A/payload-gnssd2/n2mnld | cut -c1-12); D3=$(sha256sum $A/payload-gnssd3/n2mnld | cut -c1-12)
SM=$(st); echo "mirror:       $SM"
if echo "$SM" | grep -q "gnssd True"; then
  echo "--- (P) the phone now (mirror): gnssd2 installed -> gnssd3 over it -> idempotent -> guest-gnssd2-undo.sh -> clean"
  echo "$SM" | grep -q "bin $D2" || { echo "mirror has an n2mnld that is not gnssd2: review"; exit 1; }
  run guest-gnssd3.sh payload-gnssd3; SP1=$(st); echo "gnssd3 over:  $SP1"
  echo "$SP1" | grep -q "gnssd True args --delay 60 --kboot 31 --trace-seconds 10 receipt==plist True libexec \[[^]]*'n2mnld'[^]]*\] bin $D3 n2gnss"
  grep -q '"payload": "gnssd3"' $F/private/var/db/nord2-gnssd.json
  run guest-gnssd3.sh payload-gnssd3; SP2=$(st); [ "$SP1" = "$SP2" ]; echo "idempotent over gnssd2 ok"
  run guest-gnssd2-undo.sh payload-gnssd2-undo; SC=$(st); echo "clean:        $SC"
  echo "$SC" | grep -q "gnssd False args None receipt==plist True libexec \[[^]]*\] bin None n2gnss None db \[\]"
fi
printf 'run\n' > $F/private/var/db/nord2-gnssstep
S0=$(st); echo "start:        $S0"; echo "$S0" | grep -q "gnssd False.*receipt==plist True.*db \['nord2-gnssstep'\]"
D1=$(sha256sum $A/payload-gnssd1/n2mnld | cut -c1-12)
echo "--- (a) fresh gnssd3"
run guest-gnssd3.sh payload-gnssd3; S1=$(st); echo "install:      $S1"
echo "$S1" | grep -q "gnssd True args --delay 60 --kboot 31 --trace-seconds 10 receipt==plist True libexec \[[^]]*'n2mnld'[^]]*\] bin $D3 n2gnss (\['MNL_Config.vendor.xml', 'MNL_Config.xml'\], '0o700') db \['n2gnss', 'nord2-gnssd', 'nord2-gnssd.json', 'nord2-gnssstep.off-gnssd1'\]"
grep -q '"payload": "gnssd3"' $F/private/var/db/nord2-gnssd.json
python3 - "$F" <<'PY'
import plistlib,sys
j=plistlib.load(open(sys.argv[1]+"/System/Library/xpc/launchd.plist","rb"))["LaunchDaemons"]["/System/Library/LaunchDaemons/com.nord2.gnssd.plist"]
assert j["RunAtLoad"] and "KeepAlive" not in j and j["UserName"]=="root" and j["ProgramArguments"][:3]==["/usr/local/libexec/n2mnld","--gate","/private/var/db/nord2-gnssd"]
PY
cmp $A/payload-gnssd3/n2mnld $F/usr/local/libexec/n2mnld
run guest-gnssd3.sh payload-gnssd3; S2=$(st); [ "$S1" = "$S2" ]; echo "idempotent install ok"
run guest-gnssd3-undo.sh payload-gnssd3-undo; S3=$(st); echo "gnssd3 undo:  $S3"; [ "$S0" = "$S3" ]
run guest-gnssd3-undo.sh payload-gnssd3-undo; S4=$(st); [ "$S3" = "$S4" ]; echo "idempotent undo ok"
echo "--- (b) gnssd1 + gnssd1b log -> gnssd3 over it -> gnssd1 undo"
run guest-gnssd1.sh payload-gnssd1 >/dev/null; printf '  0.012 N2MNLD start (gnssd1b)\n' > $F/private/var/db/n2gnss/n2mnld.log
S5=$(st); echo "gnssd1:       $S5"; echo "$S5" | grep -q "args --delay 60 --kboot 31 receipt==plist True.*bin $D1"
run guest-gnssd3.sh payload-gnssd3; S6=$(st); echo "gnssd3 over:  $S6"
echo "$S6" | grep -q "gnssd True args --delay 60 --kboot 31 --trace-seconds 10 receipt==plist True libexec \[[^]]*'n2mnld'[^]]*\] bin $D3 n2gnss (\['MNL_Config.vendor.xml', 'MNL_Config.xml', 'n2mnld.log'\], '0o700')"
grep -q gnssd1b $F/private/var/db/n2gnss/n2mnld.log; grep -q '"dir_made": true' $F/private/var/db/nord2-gnssd.json
run guest-gnssd1-undo.sh payload-gnssd1-undo; S7=$(st); echo "gnssd1 undo:  $S7"; [ "$S0" = "$S7" ]; echo "gnssd1 undo of gnssd3 ok"
echo "--- (c) gnssd1 -> gnssd3 -> gnssd3 undo"
run guest-gnssd1.sh payload-gnssd1 >/dev/null; run guest-gnssd3.sh payload-gnssd3 >/dev/null
run guest-gnssd3-undo.sh payload-gnssd3-undo >/dev/null; S8=$(st); [ "$S0" = "$S8" ]; echo "gnssd3 undo after upgrade ok"
echo "--- (e) the phone's path: gnssd1 -> gnssd2 (+ gnssd2 log) -> gnssd3 over it -> guest-gnssd2-undo.sh"
run guest-gnssd1.sh payload-gnssd1 >/dev/null; run guest-gnssd2.sh payload-gnssd2 >/dev/null; printf '  0.012 N2MNLD start gnssd2\n' >> $F/private/var/db/n2gnss/n2mnld.log
S10=$(st); echo "gnssd2:       $S10"; echo "$S10" | grep -q "bin $D2"
run guest-gnssd3.sh payload-gnssd3; S11=$(st); echo "gnssd3 over:  $S11"
echo "$S11" | grep -q "gnssd True args --delay 60 --kboot 31 --trace-seconds 10 receipt==plist True libexec \[[^]]*'n2mnld'[^]]*\] bin $D3 n2gnss (\['MNL_Config.vendor.xml', 'MNL_Config.xml', 'n2mnld.log'\], '0o700')"
grep -q '"payload": "gnssd3"' $F/private/var/db/nord2-gnssd.json; grep -q 'start gnssd2' $F/private/var/db/n2gnss/n2mnld.log
run guest-gnssd2-undo.sh payload-gnssd2-undo; S12=$(st); echo "gnssd2 undo:  $S12"; [ "$S0" = "$S12" ]; echo "gnssd2 undo of gnssd3 ok"
echo "--- (d) launchd.plist not matching the Metal receipt: must refuse and change nothing"
python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['installed_launchd_sha256']='0'*64;json.dump(d,open(p,'w'))" $F/private/var/db/nord2-metal/receipt.json
run guest-gnssd3.sh payload-gnssd3 2>&1 | tail -1 || true; S9=$(st); echo "after:        $S9"; echo "$S9" | grep -q "gnssd False.*bin None n2gnss None db \['nord2-gnssstep'\]"
rm -rf "$F"; echo REHEARSAL_OK
