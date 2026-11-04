#!/bin/bash
# Rehearsal of guest-gpufinal3.sh / -undo.sh (y2s93-final Nord2Metal + y2s91-thumbgrid library, one transaction) on fake roots made
# from the mirror's bundle + receipt (= the phone: thumbfix5 f90a091b + libmerge6 fe59bddb); unshare -r; nothing touches the mirror or a
# phone. Fake roots under gpu/build/.rehearse-gpufinal3 (not /tmp), removed at the end. Every gpufinal3 write is checked against
# owned-gpufinal3.json; every refusal is checked to leave the root byte-identical (checks before the first write).
# Start states: thumbfix5+libmerge6 (mirror), thumbfix4+libmerge5, thumbcomp5+libmerge4, usecount1/photosperf1/gamepipe1 (+libmerge6),
# gpufinal1 (made by guest-gpufinal1.sh, with its -off no-final), gpufinal2 (made by guest-gpufinal2.sh, plain and -off), thumbgrid1 (made by guest-thumbgrid1.sh: library already new);
# undo fallbacks (missing backups); refusals (foreign Nord2Metal / library, libmerge3, new driver without record, stale record).
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=e31a9b073095a22d0bd3fe07e6dc72f6075fa669b2377ea76aded1eb97a0f3f0; NEWL=7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd
TOP=$B/.rehearse-gpufinal3; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
lst() { (cd "$R" && find . -mindepth 1 | sed 's|^\.||' | sort) > "$1"; }
owned() { python3 - "$R" "$A/owned-gpufinal3.json" "$1" <<'PY'
import json,os,sys
R,own,before=sys.argv[1:]; own=set(json.load(open(own))); old=set(open(before).read().split('\n'))
now=set()
for b,ds,fs in os.walk(R):
    for n in ds+fs: now.add('/'+os.path.relpath(os.path.join(b,n),R))
bad=[p for p in (now^old) if p and p not in own]
assert not bad, 'written outside owned-gpufinal3.json: %r'%bad
PY
}
setfile() { cp "$2" "$R/$BU/$1"; chmod 755 "$R/$BU/$1"; python3 - "$R" "$1" <<'PY'
import hashlib,json,sys
R,n=sys.argv[1:]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
}
fresh() {   # fresh <name>: fake root = the mirror's bundle + receipt
  F=$TOP/$1; mkdir -p "$F/root"; R=$F/root; DBD=$R/private/var/db/nord2-metal
  mkdir -p "$R/$BU" "$DBD"
  $H get /private/var/db/nord2-metal "$DBD" receipt.json >/dev/null 2>&1
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"
  [ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = fe59bddb ] || die "mirror is not thumbfix5 + libmerge6"
}
refused() {  # refused <guest> <payload> <grep> <what>: must fail with that reason and change nothing
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
cycle() {   # install, again, undo, again -> byte-identical + owned
  local s0 n0 l0; s0=$(st); n0=$(h Nord2Metal); l0=$(h libredwoodair.dylib); lst "$F/before.lst"
  echo "== $1: Nord2Metal ${n0:0:12}, library ${l0:0:12}"
  run guest-gpufinal3.sh "$A/payload-gpufinal3" | grep -E '^GPUFINAL3_(INSTALLED|LIBRARY|NOTE|SWITCH)'
  run guest-gpufinal3.sh "$A/payload-gpufinal3" | grep -qE '^GPUFINAL3_ALREADY' || die "not idempotent"
  [ "$(h Nord2Metal)" = $NEWN ] && [ "$(h libredwoodair.dylib)" = $NEWL ] || die "pair not installed"
  python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['Nord2Metal']=='$NEWN' and i['libredwoodair.dylib']=='$NEWL' and rc['gpufinal3']['previous']=={'Nord2Metal':'$n0','libredwoodair.dylib':'$l0'}" || die "receipt"
  owned "$F/before.lst"
  ${2:-true}
  run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" | grep -E '^GPUFINAL3_UNDONE'
  run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" | grep -qE '^GPUFINAL3_UNDONE nothing to do' || die "undo not idempotent"
  [ ! -e "$DBD/Nord2Metal.pre-gpufinal3" ] && [ ! -e "$DBD/libredwoodair.dylib.pre-gpufinal3" ] || die "backups left"
  [ "$(st)" = "$s0" ] && echo "  install+again+undo+again byte-identical, owned: ok" || die "$1 not byte-identical after undo"
}
flagtest() {  # inside an installed gpufinal3: -off sets no-final, plain clears it, foreign flag kept
  run guest-gpufinal3.sh "$A/payload-gpufinal3-off" | grep -E '^GPUFINAL3_FLAG'
  [ "$(cat "$DBD/no-final")" = made-by-gpufinal3 ] || die "no-final not set"
  run guest-gpufinal3.sh "$A/payload-gpufinal3" | grep -E '^GPUFINAL3_FLAG'
  [ ! -e "$DBD/no-final" ] || die "no-final not cleared"
  run guest-gpufinal3.sh "$A/payload-gpufinal3-off" >/dev/null
  echo user > "$DBD/no-gamepipe"; echo user > "$DBD/no-thumbgrid-log"
  run guest-gpufinal3.sh "$A/payload-gpufinal3" | grep -E '^GPUFINAL3_SWITCH'
  run guest-gpufinal3.sh "$A/payload-gpufinal3-off" >/dev/null
  run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" | grep -E '^GPUFINAL3_FLAG'
  [ "$(cat "$DBD/no-gamepipe")" = user ] && [ "$(cat "$DBD/no-thumbgrid-log")" = user ] || die "foreign flag touched"
  [ ! -e "$DBD/no-final" ] || die "our no-final left by undo"
  rm "$DBD/no-gamepipe" "$DBD/no-thumbgrid-log"
  run guest-gpufinal3.sh "$A/payload-gpufinal3" >/dev/null   # back to installed for cycle()'s own undo
  echo "  -off sets no-final, plain clears it, foreign switches kept by install and undo: ok"
}

# 1. the mirror state (= phone): thumbfix5 + libmerge6, full flag flow
fresh mirror; cycle "thumbfix5 + libmerge6 (mirror = phone)" flagtest
# 2..6 other Nord2Metal / library combinations
for C in "payload-thumbfix4 payload-libmerge5" "payload-thumbcomp5 payload-libmerge4" "payload-usecount1 payload-libmerge6" "payload-photosperf1 payload-libmerge5" "payload-gamepipe1 payload-libmerge4"; do
  set -- $C; fresh "$1-$2"; setfile Nord2Metal "$A/$1/Nord2Metal"; setfile libredwoodair.dylib "$A/$2/libredwoodair.dylib"; cycle "${1#payload-} + ${2#payload-}"
done
# 7. gpufinal1 installed by its own guest, with its -off (no-final made-by-gpufinal1): kept, reported; gpufinal1 undo works after ours
fresh gpufinal1; run guest-gpufinal1.sh "$A/payload-gpufinal1-off" >/dev/null; [ "$(h Nord2Metal | cut -c1-8)" = 1e11b166 ] || die "gpufinal1 setup"
cycle "gpufinal1 (+ its no-final) + libmerge6" "eval refused guest-gpufinal1-undo.sh $A/payload-gpufinal1-undo 'neither\\|unknown libredwoodair' 'gpufinal1-undo while gpufinal3 is installed'"
[ "$(cat "$DBD/no-final")" = made-by-gpufinal1 ] || die "gpufinal1 flag touched"
run guest-gpufinal1-undo.sh "$A/payload-gpufinal1-undo" | grep -E '^GPUFINAL1_UNDONE'; [ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] || die "gpufinal1 undo after"
echo "  gpufinal1's own undo still works after gpufinal3's undo: ok"
# 8. thumbgrid1 installed by its own guest: the library is already the new one (left, no backup); thumbgrid1 undo works after ours
fresh thumbgrid1; run guest-thumbgrid1.sh "$A/payload-thumbgrid1" >/dev/null; [ "$(h Nord2Metal | cut -c1-8)" = 5752a629 ] || die "thumbgrid1 setup"
cycle "thumbgrid1 (Nord2Metal + library)" "eval refused guest-thumbgrid1-undo.sh $A/payload-thumbgrid1-undo neither 'thumbgrid1-undo while gpufinal3 is installed'"
run guest-thumbgrid1-undo.sh "$A/payload-thumbgrid1-undo" | grep -E '^THUMBGRID1_UNDONE'
[ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = fe59bddb ] || die "thumbgrid1 undo after"
echo "  thumbgrid1's own undo still works after gpufinal3's undo: ok"
# 8b. gpufinal2 installed by its own guest (plain, and with its -off no-final made-by-gpufinal2): Nord2Metal be7a746d gets a .pre-gpufinal3
#     backup, the library is already the new one (left, no backup); gpufinal2's record/backups untouched; its own undo works after ours
for GP in payload-gpufinal2 payload-gpufinal2-off; do
  fresh "$GP"; run guest-gpufinal2.sh "$A/$GP" >/dev/null; [ "$(h Nord2Metal | cut -c1-8)" = be7a746d ] || die "gpufinal2 setup"
  cycle "gpufinal2 via ${GP#payload-}" "eval refused guest-gpufinal2-undo.sh $A/payload-gpufinal2-undo neither 'gpufinal2-undo while gpufinal3 is installed'"
  if [ "$GP" = payload-gpufinal2-off ]; then [ "$(cat "$DBD/no-final")" = made-by-gpufinal2 ] || die "gpufinal2 flag touched"; fi
  run guest-gpufinal2-undo.sh "$A/payload-gpufinal2-undo" | grep -E '^GPUFINAL2_UNDONE'
  [ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = fe59bddb ] || die "gpufinal2 undo after"
  [ ! -e "$DBD/no-final" ] || die "gpufinal2's no-final left after its undo"
  echo "  gpufinal2's own undo still works after gpufinal3's undo (${GP#payload-}): ok"
done
# 8c. final-camera: shipped by no payload, a user's file -> kept by install and undo, reported
fresh finalcamera; echo user > "$DBD/final-camera"
run guest-gpufinal3.sh "$A/payload-gpufinal3" | grep -E '^GPUFINAL3_SWITCH final-camera' || die "final-camera not reported"
run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" >/dev/null; [ "$(cat "$DBD/final-camera")" = user ] || die "final-camera touched"
echo "  a user's final-camera is reported and kept by install and undo: ok"
# 9. undo fallbacks: both backups lost over thumbfix5 + libmerge6 -> payload fallbacks; lost backup of thumbfix4 -> refused
fresh fallback; S0=$(st); run guest-gpufinal3.sh "$A/payload-gpufinal3" >/dev/null; rm "$DBD"/*.pre-gpufinal3
run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" | grep -E '^GPUFINAL3_UNDONE'; [ "$(st)" = "$S0" ] || die "fallback undo not byte-identical"
echo "  undo with both backups missing (fallbacks thumbfix5 + libmerge6): byte-identical: ok"
setfile Nord2Metal "$A/payload-thumbfix4/Nord2Metal"; run guest-gpufinal3.sh "$A/payload-gpufinal3" >/dev/null; rm "$DBD/Nord2Metal.pre-gpufinal3"
refused guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" "no usable copy of the previous Nord2Metal" "undo with the thumbfix4 backup lost (fallback is thumbfix5)"
# 10. refusals before any write
fresh refuse
echo foreign > "$F/foreign"; setfile Nord2Metal "$F/foreign"
refused guest-gpufinal3.sh "$A/payload-gpufinal3" "none of the known states" "foreign Nord2Metal"
setfile Nord2Metal "$A/payload-thumbfix5/Nord2Metal"; setfile libredwoodair.dylib "$F/foreign"
refused guest-gpufinal3.sh "$A/payload-gpufinal3" "none of the known states" "foreign library"
setfile libredwoodair.dylib "$A/payload-libmerge3/libredwoodair.dylib"
refused guest-gpufinal3.sh "$A/payload-gpufinal3" "none of the known states" "library libmerge3"
setfile libredwoodair.dylib "$A/payload-libmerge6/libredwoodair.dylib"; setfile Nord2Metal "$A/payload-gpufinal3/Nord2Metal"
refused guest-gpufinal3.sh "$A/payload-gpufinal3" "without its record" "y2s93 Nord2Metal without record, old library"
setfile Nord2Metal "$A/payload-thumbfix5/Nord2Metal"
python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['gpufinal3']={'previous':{},'installed':{}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused guest-gpufinal3.sh "$A/payload-gpufinal3" "stale receipt record" "stale gpufinal3 record"
python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc.pop('gpufinal3');open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
echo bad > "$R/$BU/libredwoodair.dylib.nord2tmp"
refused guest-gpufinal3.sh "$A/payload-gpufinal3" "differs from its receipt" "stray file in the bundle"
rm "$R/$BU/libredwoodair.dylib.nord2tmp"
for p in payload-gpufinal3 payload-gpufinal3-off payload-gpufinal3-undo; do python3 -c "import json;json.load(open('$A/$p/plan.json'))"; done
echo "REHEARSAL_OK gpufinal3 (Nord2Metal e31a9b07 + library 7fa81fcf, one transaction) over thumbfix5+libmerge6 (mirror), thumbfix4+libmerge5, thumbcomp5+libmerge4, usecount1/photosperf1/gamepipe1, gpufinal1(+no-final), thumbgrid1, gpufinal2(+no-final); idempotent; -off no-final set/cleared; foreign switches kept; undo byte-identical (backups or fallbacks); refusals change nothing; owned"
