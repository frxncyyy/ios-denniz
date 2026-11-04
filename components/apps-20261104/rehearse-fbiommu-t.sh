#!/bin/bash
# Rehearsal of the display-IOMMU step payloads payload-fbiommu-t1..t6 (guest-fbiommu.sh) + payload-fbiommu-undo (guest-fbhwctl-undo.sh)
# on a fake root from the mirror.  Same checks as rehearse-fbiommu.sh:
# (a) each config accepted by n2fbhwctl's own parser (iommu-20261112/fbiommuctl/cfgcheck.c), binary sha == plan == fbiommuctl/build;
# (b) t1 install, idempotent; t2..t6 over it change ONLY the config line (job, binary, launchd.plist unchanged), each config exact;
# (c) undo == clean start, idempotent (a persisted config re-runs at every boot: the undo is the only way out);
# (d) refusal when launchd.plist does not match the Metal receipt; (e) the guest's writes are in owned-fbiommu-t.json.
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; C=$B/iommu-20261112/fbiommuctl
F=$(mktemp -d -p "$A" .rehearse-fbiommu-t.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
cc -Wall -Wextra -Werror -o $F/cfgcheck $C/cfgcheck.c
cfg() { python3 -c "import json;print(json.load(open('$A/payload-$1/plan.json'))['config'])"; }
for p in t1 t2 t3 t4 t5 t6; do
  python3 - $A/payload-fbiommu-$p $C/build/n2fbhwctl <<'PY'
import json,sys,hashlib
d=sys.argv[1]; p=json.load(open(d+'/plan.json')); h=lambda f: hashlib.sha256(open(f,'rb').read()).hexdigest()
assert h(d+'/n2fbhwctl')==p['files']['n2fbhwctl']==h(sys.argv[2]), d
assert p['guest']=='guest-fbiommu.sh' and 'pool=' not in p['config'], d      # pool never touched by the IOMMU steps
PY
  $F/cfgcheck "$(cfg fbiommu-$p)" | sed "s/^/fbiommu-$p: /"; done
python3 -c "import json;p=json.load(open('$A/payload-fbiommu-undo/plan.json'));assert p['guest']=='guest-fbhwctl-undo.sh' and p['files']=={}"
$H get /System/Library/xpc $F/System/Library/xpc launchd.plist >/dev/null 2>&1; rm -f $F/System/Library/xpc/launchd.plist.sig
$H get /private/var/db/nord2-metal $F/private/var/db/nord2-metal receipt.json >/dev/null 2>&1; mkdir -p $F/usr/local/libexec
run() { local p=$2; sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$A/$p/|g; s|^import os, json, hashlib, plistlib\(.*\)$|import os, json, hashlib, plistlib\1\nos.chown=lambda *a: None|" | python3 -; }
st() { python3 - "$F" <<'PY'
import plistlib,sys,os,hashlib,json
F=sys.argv[1]; raw=open(F+'/System/Library/xpc/launchd.plist','rb').read(); d=plistlib.loads(raw)['LaunchDaemons']
j=d.get('/System/Library/LaunchDaemons/com.nord2.fbhwctl.plist')
r=json.load(open(F+'/private/var/db/nord2-metal/receipt.json'))['installed_launchd_sha256']
c=F+'/private/var/db/nord2-fbhw-mode'
print('jobs',len(d),'fbhwctl',bool(j),'args',j and j['ProgramArguments'],'receipt==plist',r==hashlib.sha256(raw).hexdigest(),
      'libexec',sorted(os.listdir(F+'/usr/local/libexec')),'cfg',open(c).read().strip() if os.path.exists(c) else None,
      'db',sorted(x for x in os.listdir(F+'/private/var/db') if 'fbhw' in x),'plist',hashlib.sha256(raw).hexdigest()[:12])
PY
}
S0=$(st); echo "start:   $S0"; echo "$S0" | grep -q 'receipt==plist True'
if echo "$S0" | grep -q 'fbhwctl True'; then      # mirror taken from a phone with the fbhwctl job (fbhwmode3): baseline = after undo
  echo "mirror already has com.nord2.fbhwctl (phone state): undo first, later states are compared with that clean state"
  run guest-fbhwctl-undo.sh payload-fbiommu-undo | grep FBHWCTL_; S0=$(st); echo "clean:   $S0"; echo "$S0" | grep -q 'fbhwctl False .*receipt==plist True'
fi
run guest-fbiommu.sh payload-fbiommu-t1 | grep FBHWCTL_; S1=$(st); echo "t1:      $S1"
echo "$S1" | grep -qF "fbhwctl True" && echo "$S1" | grep -qF "receipt==plist True" && echo "$S1" | grep -qF "cfg $(cfg fbiommu-t1) db"
run guest-fbiommu.sh payload-fbiommu-t1 >/dev/null; [ "$S1" = "$(st)" ]
cmp $F/usr/local/libexec/n2fbhwctl $C/build/n2fbhwctl
for p in t2 t3 t4 t5 t6; do
  run guest-fbiommu.sh payload-fbiommu-$p | grep FBHWCTL_; S=$(st); echo "$p:      $S"; echo "$S" | grep -qF "cfg $(cfg fbiommu-$p) db"
  [ "$(echo "$S1" | sed 's/cfg .* db/db/')" = "$(echo "$S" | sed 's/cfg .* db/db/')" ] || { echo "$p over t1 changed more than the config"; exit 1; }
done
python3 - "$F" $A/owned-fbiommu-t.json <<'PY'
import json,sys,os
F,o=sys.argv[1],set(json.load(open(sys.argv[2])))
w=['/usr/local/libexec/n2fbhwctl','/private/var/db/nord2-fbhw-mode','/private/var/db/nord2-fbhwctl.json','/System/Library/xpc/launchd.plist','/private/var/db/nord2-metal/receipt.json','/usr/local/libexec','/private/var/db']
miss=[p for p in w if p not in o]; assert not miss, ('not owned', miss)
left=[p for p in os.popen('cd %s && find . -name "*.nord2tmp"'%F).read().split()]; assert not left, left
print('owned covers the writes; no temp files left')
PY
run guest-fbhwctl-undo.sh payload-fbiommu-undo | grep FBHWCTL_; S4=$(st); echo "undo:    $S4"; [ "$S0" = "$S4" ]
run guest-fbhwctl-undo.sh payload-fbiommu-undo >/dev/null; [ "$S4" = "$(st)" ]
echo "--- launchd.plist not matching the Metal receipt: must refuse and change nothing"
python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['installed_launchd_sha256']='0'*64;json.dump(d,open(p,'w'))" $F/private/var/db/nord2-metal/receipt.json
run guest-fbiommu.sh payload-fbiommu-t5 2>&1 | tail -1 || true; S6=$(st); echo "after:   $S6"; echo "$S6" | grep -q "fbhwctl False .*cfg None"
rm -rf "$F"; echo REHEARSAL_OK
