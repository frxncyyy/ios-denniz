#!/bin/bash
# gpupipe1 (copy of y2s99c-perf/ship/rehearse-bigpool1.sh + scenario 7 on top of bigpool1): rehearsal of guest-gpupipe1.sh / -undo.sh on fake roots made from the mirror's Nord2Metal bundle + receipt (the mirror = THE
# PHONE: shader99b, Nord2Metal fe4ecc9f + library 7c0c5aba). unshare -r; nothing touches the mirror or a phone (adapted from
# y2s99a-api-gaps/ship/rehearse-api99a.sh). Every write is checked against owned-gpupipe1.json; every refusal leaves the root identical.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/../../.." && pwd); BP=$B/y2s99c-perf/ship; AP=$B/apps-20261104; BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWL=46e556ef49a1d873c768c73e424ae239e2b7be86a0d4b9a5df212713f0d860b8; BPL=d1c90b063e93298ce7876519dd11d6750f25da5c0db92d0ff02d79b790228a14; OLDL=7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56
N99=fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d
TOP=$A/.rehearse-gpupipe1; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . \( -type f -o -type d \) -printf '%p %m\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-gpupipe1.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3])
diff=[k for k in set(a)|set(b) if a.get(k)!=b.get(k)]
bad=sorted(k for k in diff if k not in own)
assert not bad, 'written outside owned-gpupipe1.json: %r'%bad
print('  owned check: %d path(s) differ, all owned: %s'%(len(diff), sorted(k.rsplit('/',1)[1] for k in diff)))
PY
}
setfile() { cp "$2" "$R/$BU/$1"; python3 - "$R" "$1" <<'PY'
import hashlib,json,sys
R,n=sys.argv[1:]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
}
fresh() {
  F=$TOP/$1; mkdir -p "$F/root"; R=$F/root; DBD=$R/private/var/db/nord2-metal
  mkdir -p "$R/$BU" "$DBD"
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-shader99b libredwoodair.dylib.pre-shader99b >/dev/null 2>&1
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"; chmod 644 "$R/$BU/libredwoodair.dylib"
  [ "$(h Nord2Metal)" = $N99 ] && [ "$(h libredwoodair.dylib)" = $OLDL ] || die "mirror is not shader99b (phone)"
}
refused() {
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
P=$A/payload-gpupipe1; PO=$A/payload-gpupipe1-off; PU=$A/payload-gpupipe1-undo
[ "$(sha256sum < $P/libredwoodair.dylib | cut -c1-64)" = $NEWL ] && cmp -s $P/libredwoodair.dylib $PO/libredwoodair.dylib && [ "$(sha256sum < $PU/libredwoodair.dylib | cut -c1-64)" = $BPL ] || die "payload files"
cmp -s $P/libredwoodair.dylib "$A/../build1/payload/Nord2Metal.bundle/libredwoodair.dylib" || die "payload is not the gpupipe1 build"

echo "== 1 phone state (shader99b): install, again, undo, again"
fresh phone; s0=$(st); snap "$F/0.snap"
run guest-gpupipe1.sh $P | grep -E '^GPUPIPE1_(INSTALLED|NOTE|FLAG)|^CC_'
[ "$(h libredwoodair.dylib)" = $NEWL ] && [ "$(h Nord2Metal)" = $N99 ] || die "not installed / Nord2Metal touched"
python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['libredwoodair.dylib']=='$NEWL' and i['Nord2Metal']=='$N99' and rc['gpupipe1']['previous']=={'libredwoodair.dylib':'$OLDL'} and 'shader99b' in rc" || die "receipt"
[ "$(sha256sum < "$DBD/libredwoodair.dylib.pre-gpupipe1" | cut -c1-64)" = $OLDL ] || die "backup"
[ "$(stat -c %a "$R/$BU/libredwoodair.dylib")" = 644 ] || die "mode"
snap "$F/1.snap"; owned "$F/0.snap" "$F/1.snap"; s1=$(st)
run guest-gpupipe1.sh $P | grep -qE '^GPUPIPE1_ALREADY' || die "not idempotent"; [ "$(st)" = "$s1" ] || die "second install changed the root"
run guest-gpupipe1-undo.sh $PU | grep -E '^GPUPIPE1_UNDONE'
[ "$(st)" = "$s0" ] || die "undo is not byte-identical to the start"
run guest-gpupipe1-undo.sh $PU | grep -q "nothing to do" || die "second undo"; [ "$(st)" = "$s0" ] || die "second undo changed the root"

echo "== 2 the -off payload sets the switch; the plain payload clears it; undo removes ours"
run guest-gpupipe1.sh $PO | grep -E '^GPUPIPE1_(INSTALLED|FLAG)'
[ "$(cat "$DBD/no-gpupipe")" = made-by-gpupipe1 ] || die "switch not set"
snap "$F/2.snap"; owned "$F/0.snap" "$F/2.snap"
run guest-gpupipe1.sh $P | grep -E '^GPUPIPE1_(ALREADY|FLAG)'
[ ! -e "$DBD/no-gpupipe" ] || die "switch not cleared"
run guest-gpupipe1.sh $PO > /dev/null; run guest-gpupipe1-undo.sh $PU | grep -E '^GPUPIPE1_(UNDONE|FLAG)'
[ "$(st)" = "$s0" ] || die "undo after -off is not byte-identical to the start"

echo "== 3 a switch that is not ours is kept by install and by undo"
printf 'by hand\n' > "$DBD/no-gpupipe"; sf=$(st)
run guest-gpupipe1.sh $P | grep -E '^GPUPIPE1_NOTE' || die "foreign switch not reported"
run guest-gpupipe1-undo.sh $PU | grep -E '^GPUPIPE1_NOTE' || die "foreign switch not reported by undo"
[ "$(cat "$DBD/no-gpupipe")" = "by hand" ] && [ "$(st)" = "$sf" ] || die "foreign switch touched"
rm "$DBD/no-gpupipe"

echo "== 4 undo without its backup: the payload fallback (bigpool1 library) is used only when the previous library was bigpool1"
run guest-gpupipe1.sh $P > /dev/null; rm "$DBD/libredwoodair.dylib.pre-gpupipe1"
refused guest-gpupipe1-undo.sh $PU "no usable copy" "undo without backup when the previous library was shader99b's"
cp "$BP/payload-bigpool1-undo/libredwoodair.dylib" "$DBD/libredwoodair.dylib.pre-gpupipe1"; chmod 644 "$DBD/libredwoodair.dylib.pre-gpupipe1"
run guest-gpupipe1-undo.sh $PU > /dev/null; [ "$(st)" = "$s0" ] || die "undo after restoring the backup"
run guest-gpupipe1.sh $P > /dev/null; printf 'x' >> "$DBD/libredwoodair.dylib.pre-gpupipe1"
refused guest-gpupipe1-undo.sh $PU "no usable copy" "undo with a damaged backup"
cp "$BP/payload-bigpool1-undo/libredwoodair.dylib" "$DBD/libredwoodair.dylib.pre-gpupipe1"; chmod 644 "$DBD/libredwoodair.dylib.pre-gpupipe1"
run guest-gpupipe1-undo.sh $PU > /dev/null; [ "$(st)" = "$s0" ] || die "undo after repair"

echo "== 5 refusals of the install (root unchanged)"
fresh other; mkdir "$F/badpay"; cp $P/libredwoodair.dylib "$F/badpay/"; printf 'x' >> "$F/badpay/libredwoodair.dylib"
refused guest-gpupipe1.sh "$F/badpay" "is not the gpupipe1 one" "a payload library that is not the build"
setfile Nord2Metal "$AP/payload-api99a/Nord2Metal" 2>/dev/null || setfile Nord2Metal "$B/y2s99a-api-gaps/ship/payload-api99a/Nord2Metal"
refused guest-gpupipe1.sh $P "is not shader99b fe4ecc9f" "current Nord2Metal = api99a"
fresh lib; setfile libredwoodair.dylib "$AP/payload-gpufinal7/libredwoodair.dylib"
refused guest-gpupipe1.sh $P "neither shader99b 7c0c5aba nor bigpool1 d1c90b06" "current library = gpufinal7's"
fresh drift; printf 'x' >> "$R/$BU/libredwoodair.dylib"
refused guest-gpupipe1.sh $P "differs from its receipt" "a bundle that differs from its receipt"
fresh stale; python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['gpupipe1']={'installed':{'x':'y'}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused guest-gpupipe1.sh $P "stale receipt record" "a stale record gpupipe1"
fresh bak; printf 'other\n' > "$DBD/libredwoodair.dylib.pre-gpupipe1"
refused guest-gpupipe1.sh $P "a different backup" "a different libredwoodair.dylib.pre-gpupipe1 already there"
fresh link; ln -s /etc/passwd "$DBD/no-gpupipe"
refused guest-gpupipe1.sh $P "not a regular file" "a switch that is a symlink"
fresh undo-foreign; setfile libredwoodair.dylib "$AP/payload-gpufinal7/libredwoodair.dylib"
refused guest-gpupipe1-undo.sh $PU "neither gpupipe1 nor shader99b/bigpool1" "undo over gpufinal7's library"

echo "== 6 after gpupipe1 undo, shader99b's own undo still works; over gpupipe1 it refuses"
fresh chain; run guest-gpupipe1.sh $P > /dev/null; run guest-gpupipe1-undo.sh $PU > /dev/null
run_ap() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$AP/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
run_ap guest-shader99b-undo.sh "$AP/payload-shader99b-undo" | grep -E '^SHADER99B_UNDONE' | cut -c1-160
fresh chain2; run guest-gpupipe1.sh $P > /dev/null
if run_ap guest-shader99b-undo.sh "$AP/payload-shader99b-undo" > "$F/t1.log" 2>&1; then die "shader99b-undo accepted over gpupipe1"; fi
echo "  shader99b-undo over gpupipe1 refuses (undo gpupipe1 first): $(grep -m1 -o 'AssertionError.*' "$F/t1.log" | cut -c1-110)"
echo "== 7 on top of bigpool1 (installed by its own guest): install, again, undo (backup and fallback), then bigpool1's own undo"
run_bp() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$BP/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
fresh onbp; s00=$(st); run_bp guest-bigpool1.sh "$BP/payload-bigpool1" | grep -E '^BIGPOOL1_INSTALLED' | cut -c1-80
[ "$(h libredwoodair.dylib)" = $BPL ] || die "bigpool1 not installed"; sb=$(st); snap "$F/b.snap"
run guest-gpupipe1.sh $P | grep -E '^GPUPIPE1_INSTALLED'
[ "$(h libredwoodair.dylib)" = $NEWL ] || die "gpupipe1 not installed over bigpool1"
python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));assert rc['gpupipe1']['previous']=={'libredwoodair.dylib':'$BPL'} and 'bigpool1' in rc" || die "receipt over bigpool1"
snap "$F/c.snap"; owned "$F/b.snap" "$F/c.snap"
run guest-gpupipe1.sh $P | grep -qE '^GPUPIPE1_ALREADY' || die "not idempotent over bigpool1"
run guest-gpupipe1-undo.sh $PU | grep -E '^GPUPIPE1_UNDONE'; [ "$(st)" = "$sb" ] || die "undo over bigpool1 is not byte-identical"
run guest-gpupipe1.sh $P > /dev/null; rm "$DBD/libredwoodair.dylib.pre-gpupipe1"
run guest-gpupipe1-undo.sh $PU | grep -E '^GPUPIPE1_UNDONE' | grep -q fallback || die "fallback (bigpool1 copy) not used"; [ "$(st)" = "$sb" ] || die "fallback undo over bigpool1"
run_bp guest-bigpool1-undo.sh "$BP/payload-bigpool1-undo" | grep -E '^BIGPOOL1_UNDONE' | cut -c1-80; [ "$(st)" = "$s00" ] || die "bigpool1's own undo after ours"
fresh onbp2; run_bp guest-bigpool1.sh "$BP/payload-bigpool1" > /dev/null; run guest-gpupipe1.sh $P > /dev/null
if run_bp guest-bigpool1-undo.sh "$BP/payload-bigpool1-undo" > "$F/t2.log" 2>&1; then die "bigpool1-undo accepted over gpupipe1"; fi
echo "  bigpool1-undo over gpupipe1 refuses (undo gpupipe1 first): $(grep -m1 -o 'AssertionError.*' "$F/t2.log" | cut -c1-110)"
echo REHEARSAL_OK
