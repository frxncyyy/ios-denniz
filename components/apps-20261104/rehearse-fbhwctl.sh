#!/bin/bash
# rehearsal of guest-fbhwctl.sh / guest-fbhwctl-undo.sh on a fake root from the mirror (launchd.plist, Metal receipt, /usr/local/libexec).
# (a) every payload-fbhw* config accepted by n2fbhwctl's own parser (hwcomp-20261112/fbhwctl/cfgcheck.c), payload files = plan;
# (b) install fbhwmode1, idempotent; (c) fbhwcycle over it: only the config changes (job, binary, launchd.plist unchanged);
# (d) undo == start, idempotent; (e) refusal when launchd.plist does not match the Metal receipt; (f) the guest's writes are in
# owned-fbhwctl.json.
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; C=$B/hwcomp-20261112/fbhwctl
F=$(mktemp -d -p "$A" .rehearse-fbhwctl.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
cc -Wall -Wextra -Werror -o $F/cfgcheck $C/cfgcheck.c
for p in fbhwmode0 fbhwmode1 fbhwmode2 fbhwmode3 fbhwcycle; do
  python3 - $A/payload-$p <<'PY'
import json,sys,hashlib
d=sys.argv[1]; p=json.load(open(d+'/plan.json'))
assert hashlib.sha256(open(d+'/n2fbhwctl','rb').read()).hexdigest()==p['files']['n2fbhwctl'], d
PY
  $F/cfgcheck "$(python3 -c "import json;print(json.load(open('$A/payload-$p/plan.json'))['config'])")" | sed "s/^/$p: /"; done
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
run guest-fbhwctl.sh payload-fbhwmode1 | grep FBHWCTL_; S1=$(st); echo "mode1:   $S1"; echo "$S1" | grep -q "fbhwctl True .*receipt==plist True .*'n2fbhwctl'.*cfg modes=1 period=0 delay=20 pool=1"
run guest-fbhwctl.sh payload-fbhwmode1 >/dev/null; S2=$(st); [ "$S1" = "$S2" ]
run guest-fbhwctl.sh payload-fbhwcycle | grep FBHWCTL_; S3=$(st); echo "cycle:   $S3"; echo "$S3" | grep -q "cfg modes=0,1,2,3 period=120 delay=90 pool=1 final=0"
[ "$(echo "$S1" | sed 's/cfg .* db/db/')" = "$(echo "$S3" | sed 's/cfg .* db/db/')" ] || { echo "cycle over mode1 changed more than the config"; exit 1; }
cmp $F/usr/local/libexec/n2fbhwctl $C/build/n2fbhwctl
python3 - "$F" $A/owned-fbhwctl.json <<'PY'
import json,sys,os
F,o=sys.argv[1],set(json.load(open(sys.argv[2])))
w=['/usr/local/libexec/n2fbhwctl','/private/var/db/nord2-fbhw-mode','/private/var/db/nord2-fbhwctl.json','/System/Library/xpc/launchd.plist','/private/var/db/nord2-metal/receipt.json','/usr/local/libexec','/private/var/db']
miss=[p for p in w if p not in o]; assert not miss, ('not owned', miss)
left=[p for p in os.popen('cd %s && find . -name "*.nord2tmp"'%F).read().split()]; assert not left, left
print('owned covers the writes; no temp files left')
PY
run guest-fbhwctl-undo.sh payload-fbhwctl-undo | grep FBHWCTL_; S4=$(st); echo "undo:    $S4"; [ "$S0" = "$S4" ]
run guest-fbhwctl-undo.sh payload-fbhwctl-undo >/dev/null; S5=$(st); [ "$S4" = "$S5" ]
echo "--- launchd.plist not matching the Metal receipt: must refuse and change nothing"
python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['installed_launchd_sha256']='0'*64;json.dump(d,open(p,'w'))" $F/private/var/db/nord2-metal/receipt.json
run guest-fbhwctl.sh payload-fbhwmode2 2>&1 | tail -1 || true; S6=$(st); echo "after:   $S6"; echo "$S6" | grep -q "fbhwctl False .*cfg None"
rm -rf "$F"; echo REHEARSAL_OK
