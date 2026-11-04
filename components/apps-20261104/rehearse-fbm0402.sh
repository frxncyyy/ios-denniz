#!/bin/bash
# 0402b rehearsal of guest-fbm0402.sh (+ guest-fbm0402-undo.sh) on a fake root from the mirror: payload-fbm0402cycle(2)
# (hwmulti-20261113-on0401b/fbm0402ctl, the n2fbhwctl fork with scal/h6/layers/bbd).
# (a) the config is accepted by the fork's own parser (cfgcheck.c), payload files = plan; (b) install, idempotent;
# (c) over an existing fbiommu install (0401 payload) and back: job + launchd.plist unchanged, only binary + config swapped;
# (d) the old guests refuse the new tokens; (e) the guest's writes are in owned-fbm0402.json; (f) undo == start, idempotent;
# (g) refusal when launchd.plist does not match the Metal receipt.
set -e
# usage: rehearse-fbm0402.sh [payload-fbm0402cycle (default) | payload-fbm0402cycle2]
P=${1:-payload-fbm0402cycle}; U=$P-undo
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; C=$B/hwmulti-20261113-on0401b/fbm0402ctl; CI=$B/iommu-20261112/fbiommuctl
F=$(mktemp -d -p "$A" .rehearse-fbm0402.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
cc -Wall -Wextra -Werror -o $F/cfgcheck $C/cfgcheck.c
python3 - $A/$P <<'PY'
import json,sys,hashlib
d=sys.argv[1]; p=json.load(open(d+'/plan.json'))
assert hashlib.sha256(open(d+'/n2fbhwctl','rb').read()).hexdigest()==p['files']['n2fbhwctl'], d
assert p['guest']=='guest-fbm0402.sh'
PY
CFG=$(python3 -c "import json;print(json.load(open('$A/$P/plan.json'))['config'])")
$F/cfgcheck "$CFG" | sed "s/^/$P: /"
cmp $A/$P/n2fbhwctl $C/build/n2fbhwctl
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
if echo "$S0" | grep -q 'fbhwctl True'; then
  echo "mirror already has com.nord2.fbhwctl (phone state): undo first, later states are compared with that clean state"
  run guest-fbm0402-undo.sh $U | grep FBHWCTL_; S0=$(st); echo "clean:   $S0"; echo "$S0" | grep -q 'fbhwctl False .*receipt==plist True'
fi
run guest-fbm0402.sh $P | grep FBHWCTL_; S1=$(st); echo "cycle:   $S1"
echo "$S1" | grep -q "fbhwctl True .*receipt==plist True .*'n2fbhwctl'.*cfg $CFG"
run guest-fbm0402.sh $P >/dev/null; S2=$(st); [ "$S1" = "$S2" ] && echo "idempotent: ok"
cmp $F/usr/local/libexec/n2fbhwctl $C/build/n2fbhwctl
echo "--- the old guests refuse the new tokens (a wrong guest/payload pair cannot install half a config)"
run guest-fbiommu.sh $P 2>&1 | grep -q 'bad config' && echo "fbiommu guest refuses scal/h6/layers/bbd: ok"
run guest-fbhwctl.sh $P 2>&1 | grep -q 'bad config' && echo "fbhwctl guest refuses them: ok"; S2b=$(st); [ "$S1" = "$S2b" ]
echo "--- over an existing fbiommu install (0401 payload) and back: job + launchd.plist unchanged, binary/config swapped"
run guest-fbiommu.sh payload-fbiommumode3 >/dev/null; S7=$(st); cmp $F/usr/local/libexec/n2fbhwctl $CI/build/n2fbhwctl
run guest-fbm0402.sh $P | grep FBHWCTL_; S8=$(st); echo "over:    $S8"
[ "$(echo "$S7" | sed 's/cfg .* db/db/')" = "$(echo "$S8" | sed 's/cfg .* db/db/')" ] || { echo "fbm0402 over fbiommu changed more than binary+config"; exit 1; }
cmp $F/usr/local/libexec/n2fbhwctl $C/build/n2fbhwctl
python3 - "$F" $A/owned-fbm0402.json <<'PY'
import json,sys,os
F,o=sys.argv[1],set(json.load(open(sys.argv[2])))
w=['/usr/local/libexec/n2fbhwctl','/private/var/db/nord2-fbhw-mode','/private/var/db/nord2-fbhwctl.json','/System/Library/xpc/launchd.plist','/private/var/db/nord2-metal/receipt.json','/usr/local/libexec','/private/var/db']
miss=[p for p in w if p not in o]; assert not miss, ('not owned', miss)
left=[p for p in os.popen('cd %s && find . -name "*.nord2tmp"'%F).read().split()]; assert not left, left
print('owned covers the writes; no temp files left')
PY
run guest-fbm0402-undo.sh $U | grep FBHWCTL_; S4=$(st); echo "undo:    $S4"; [ "$S0" = "$S4" ]
run guest-fbm0402-undo.sh $U >/dev/null; S5=$(st); [ "$S4" = "$S5" ]
echo "--- launchd.plist not matching the Metal receipt: must refuse and change nothing"
python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['installed_launchd_sha256']='0'*64;json.dump(d,open(p,'w'))" $F/private/var/db/nord2-metal/receipt.json
run guest-fbm0402.sh $P 2>&1 | tail -1 || true; S6=$(st); echo "after:   $S6"; echo "$S6" | grep -q "fbhwctl False .*cfg None"
rm -rf "$F"; echo REHEARSAL_OK
