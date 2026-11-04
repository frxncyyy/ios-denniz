#!/bin/bash
# Rehearsal of guest-gpufinal1.sh / -undo.sh (y2s90-final Nord2Metal) on fake roots made from the mirror's bundle + receipt (its library:
# libmerge6 fe59bddb), over thumbfix5 (the mirror's state), thumbfix4 and thumbcomp5; unshare -r; nothing touches the mirror or a phone.
# Fake roots live under gpu/build/.rehearse-gpufinal1 (not /tmp) and are removed at the end.
# Per start state: install, install again (ALREADY), -off payload (no-final set), plain payload (no-final cleared), -off again, a
# no-gamepipe made by someone else (kept by install and undo), undo (driver restored + only our flag removed), undo again (nothing to do)
# -> byte-identical to the start (once the foreign flag is taken away again); a foreign Nord2Metal and the sibling usecount1/photosperf1/
# gamepipe1 builds are refused.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEW=1e11b1666376f1a701fc90f4db7b815922630cee1a1793915073468ebe512fe7
TOP=$B/.rehearse-gpufinal1; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
for OLD in payload-thumbfix5 payload-thumbfix4 payload-thumbcomp5; do
  F=$TOP/$OLD; mkdir -p "$F/root"; R=$F/root
  mkdir -p "$R/$BU" "$R/private/var/db/nord2-metal"
  $H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" receipt.json >/dev/null 2>&1
  NAMES=$(python3 -c "import json;inv=json.load(open('$R/private/var/db/nord2-metal/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  cp "$A/$OLD/Nord2Metal" "$R/$BU/Nord2Metal"; chmod 755 "$R/$BU/Nord2Metal"
  python3 - "$R" <<'PY'
import hashlib,json,sys
R=sys.argv[1]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
for n in ('Nord2Metal','libredwoodair.dylib'): rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest()
open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
  S0=$(st); DBD=$R/private/var/db/nord2-metal; FL=$DBD/no-final
  echo "== over $OLD ($(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)), library $(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-12)"
  run guest-gpufinal1.sh "$A/payload-gpufinal1" | grep -E '^GPUFINAL1_INSTALLED|^CC_JOB' | head -2
  run guest-gpufinal1.sh "$A/payload-gpufinal1" | grep -E '^GPUFINAL1_ALREADY'
  [ "$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-64)" = $NEW ] || die "not installed"
  [ "$(python3 -c "import json;print(json.load(open('$DBD/receipt.json'))['inventory']['$BU']['Nord2Metal'])")" = $NEW ] || die "receipt"
  run guest-gpufinal1.sh "$A/payload-gpufinal1-off" | grep -E '^GPUFINAL1_FLAG'
  [ "$(cat "$FL")" = made-by-gpufinal1 ] || die "no-final not set"
  run guest-gpufinal1.sh "$A/payload-gpufinal1" | grep -E '^GPUFINAL1_FLAG'
  [ ! -e "$FL" ] || die "no-final not cleared"
  run guest-gpufinal1.sh "$A/payload-gpufinal1-off" | grep -E '^GPUFINAL1_FLAG'
  echo user > "$DBD/no-gamepipe"                                   # a switch made by someone else: never ours to remove
  run guest-gpufinal1.sh "$A/payload-gpufinal1-off" | grep -E '^GPUFINAL1_FLAG no-gamepipe'
  run guest-gpufinal1-undo.sh "$A/payload-gpufinal1-undo" | grep -E '^GPUFINAL1_UNDONE|^GPUFINAL1_FLAG'
  run guest-gpufinal1-undo.sh "$A/payload-gpufinal1-undo" | grep -E '^GPUFINAL1_UNDONE'
  [ "$(cat "$DBD/no-gamepipe")" = user ] || die "foreign flag touched"; rm "$DBD/no-gamepipe"
  [ ! -e "$FL" ] && [ ! -e "$DBD/Nord2Metal.pre-gpufinal1" ] || die "leftovers"
  S1=$(st); [ "$S0" = "$S1" ] && echo "  byte-identical after install+flags+undo: ok" || die "$OLD not byte-identical"
  for SIB in payload-usecount1 payload-photosperf1 payload-gamepipe1; do
    cp "$A/$SIB/Nord2Metal" "$R/$BU/Nord2Metal"
    python3 - "$R" <<'PY'
import hashlib,json,sys
R=sys.argv[1]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU]['Nord2Metal']=hashlib.sha256(open(R+'/'+BU+'/Nord2Metal','rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
    if run guest-gpufinal1.sh "$A/payload-gpufinal1" > "$F/s.log" 2>&1; then die "sibling $SIB accepted"; fi
    grep -q 'undo it first' "$F/s.log" || die "sibling $SIB refused for another reason: $(tail -1 "$F/s.log")"
  done
  echo "  sibling usecount1/photosperf1/gamepipe1 refused (undo them first): ok"
  echo foreign > "$R/$BU/Nord2Metal"; if run guest-gpufinal1.sh "$A/payload-gpufinal1" > "$F/f.log" 2>&1; then die "foreign accepted"; fi
  echo "  foreign Nord2Metal refused: ok"
done
echo "REHEARSAL_OK gpufinal1 over thumbfix5 (mirror state), thumbfix4, thumbcomp5; library libmerge6; no-final set/cleared/undone; foreign flag kept; siblings refused"
