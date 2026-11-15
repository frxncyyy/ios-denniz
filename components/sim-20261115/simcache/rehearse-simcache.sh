#!/bin/bash
# fake-root rehearsal of the simcache1 guests (rehearse-gfxall.sh pattern): fake root = the CURRENT mirror's launchd.plist + Metal receipt;
# unshare -r; nothing touches the mirror or a phone. Checks: install / again (ALREADY) / switches / undo back to byte-identical,
# every write inside owned-simcache1.json, refusals leave the root unchanged.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/../.." && pwd); HX="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
TOP=$(mktemp -d -p "$A" .rh.XXXX); trap 'rm -rf "$TOP"' EXIT
F=$TOP/py; mkdir -p $F; R=$TOP/root; mkdir -p $R/System/Library/xpc $R/private/var/db/nord2-metal $R/usr/libexec
$HX get /System/Library/xpc $R/System/Library/xpc launchd.plist >/dev/null
$HX get /private/var/db/nord2-metal $R/private/var/db/nord2-metal receipt.json >/dev/null
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/|'$2/|g" > "$F/g.py"; unshare -r python3 "$F/g.py" 2>&1; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-simcache1.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3]); ch=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k))
bad=[k for k in ch if k not in own]; assert not bad, 'written outside owned: %r'%bad
print('  owned check: %d changed, all owned'%len(ch))
PY
}
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
snap $TOP/s0
echo "== install"; o=$(run $A/guest-simcache1.sh $A/payload-simcache1); echo "$o" | grep -E 'SIMCACHE1_' ; echo "$o" | grep -q SIMCACHE1_INSTALLED || die install
snap $TOP/s1; owned $TOP/s0 $TOP/s1
python3 -c "import plistlib;d=plistlib.load(open('$R/System/Library/xpc/launchd.plist','rb'))['LaunchDaemons']['/System/Library/LaunchDaemons/com.nord2.simcache.plist'];print('  job',d['Label'],d['ProgramArguments'],'LaunchOnlyOnce',d['LaunchOnlyOnce'])"
python3 -c "import json,hashlib;rc=json.load(open('$R/private/var/db/nord2-metal/receipt.json'));print('  metal receipt launchd sha matches:',rc['installed_launchd_sha256']==hashlib.sha256(open('$R/System/Library/xpc/launchd.plist','rb').read()).hexdigest())"
ls -la $R/usr/libexec/n2simcache | awk '{print "  "$1, $NF}'
echo "== again"; run $A/guest-simcache1.sh $A/payload-simcache1 | grep -q SIMCACHE1_ALREADY || die again; snap $TOP/s2; cmp -s $TOP/s1 $TOP/s2 || die "again changed something"; echo "  ALREADY, unchanged"
echo "== off"; run $A/guest-simcache1.sh $A/payload-simcache1-off | grep -E 'SWITCHES|INSTALLED'; [ -f $R/private/var/db/nord2-simcache/disabled ] || die off
echo "== on"; run $A/guest-simcache1.sh $A/payload-simcache1-on | grep -E 'SWITCHES'; [ ! -e $R/private/var/db/nord2-simcache/disabled ] || die on
echo "== helper's own 'norestart' (auto) is kept by -sw-keepkey, cleared by -on"; printf 'auto\n' > $R/private/var/db/nord2-simcache/norestart
run $A/guest-simcache1.sh $A/payload-simcache1-sw-keepkey | grep SWITCHES; [ -f $R/private/var/db/nord2-simcache/norestart ] || die "auto norestart lost"
run $A/guest-simcache1.sh $A/payload-simcache1-on | grep SWITCHES; [ ! -e $R/private/var/db/nord2-simcache/norestart ] || die "on did not clear"
echo "== foreign switch file refused"; printf 'x' > $R/private/var/db/nord2-simcache/dryrun; snap $TOP/f0
o=$(run $A/guest-simcache1.sh $A/payload-simcache1 || true); echo "$o" | grep -q 'SIMCACHE1_REFUSED' || die "no refusal"; snap $TOP/f1; cmp -s $TOP/f0 $TOP/f1 || die "refusal wrote"; rm $R/private/var/db/nord2-simcache/dryrun; echo "  refused, unchanged"
echo "== runtime log files then undo"; echo x > $R/private/var/db/nord2-simcache/log; echo y > $R/private/var/db/nord2-simcache/stderr.log
run $A/guest-simcache1-undo.sh $A/payload-simcache1-undo | grep SIMCACHE1_UNDONE || die undo
snap $TOP/s3; cmp -s $TOP/s0 $TOP/s3 && echo "  undo: root byte-identical to the mirror's" || { diff $TOP/s0 $TOP/s3 | head; die "undo not identical"; }
echo "== undo again (no-op)"; run $A/guest-simcache1-undo.sh $A/payload-simcache1-undo | grep -q 'nothing to undo' || die "undo2"
echo "== switches before install refused"; o=$(run $A/guest-simcache1.sh $A/payload-simcache1-off || true); echo "$o" | grep -q REFUSED || die "switch w/o install"
echo REHEARSAL_OK
