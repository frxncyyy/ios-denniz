#!/bin/bash
# Rehearsal of guest-usecount1.sh / -undo.sh (y2s84-usecount Nord2Metal) on fake roots made from the mirror's bundle + receipt, over
# thumbfix5 (the mirror's state) and thumbfix4; unshare -r; nothing touches the mirror or a phone.  Fake roots live under
# gpu/build/.rehearse-usecount1 (not /tmp) and are removed at the end.
# Per start state: install, install again (ALREADY), keepfreeze payload (flag set), plain payload again (flag cleared), keepfreeze
# again, undo (driver restored + flag removed), undo again (nothing to do) -> byte-identical to the start; a foreign Nord2Metal is refused.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
TOP=$B/.rehearse-usecount1; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
for OLD in payload-thumbfix5 payload-thumbfix4; do
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
  S0=$(st); FL=$R/private/var/db/nord2-metal/usecount-keepfreeze
  echo "== over $OLD ($(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)), library $(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-12)"
  run guest-usecount1.sh "$A/payload-usecount1" | grep -E '^USECOUNT1_INSTALLED|^CC_JOB' | head -2
  run guest-usecount1.sh "$A/payload-usecount1" | grep -E '^USECOUNT1_ALREADY'
  [ "$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-64)" = a6997fb7630c0b20beef7f7044e43575bf95ecdb87cdfcb16050fb5028518f2d ] || { echo "REHEARSAL FAILED: not installed"; exit 1; }
  run guest-usecount1.sh "$A/payload-usecount1-keepfreeze" | grep -E '^USECOUNT1_FLAG'
  [ -f "$FL" ] || { echo "REHEARSAL FAILED: keepfreeze flag not set"; exit 1; }
  run guest-usecount1.sh "$A/payload-usecount1" | grep -E '^USECOUNT1_FLAG'
  [ ! -e "$FL" ] || { echo "REHEARSAL FAILED: keepfreeze flag not cleared"; exit 1; }
  run guest-usecount1.sh "$A/payload-usecount1-keepfreeze" | grep -E '^USECOUNT1_FLAG'
  run guest-usecount1-undo.sh "$A/payload-usecount1-undo" | grep -E '^USECOUNT1_UNDONE|^USECOUNT1_FLAG'
  run guest-usecount1-undo.sh "$A/payload-usecount1-undo" | grep -E '^USECOUNT1_UNDONE'
  S1=$(st); [ "$S0" = "$S1" ] && echo "  byte-identical after install+flags+undo: ok" || { echo "REHEARSAL FAILED ($OLD)"; exit 1; }
  echo foreign > "$R/$BU/Nord2Metal"; if run guest-usecount1.sh "$A/payload-usecount1" > "$F/f.log" 2>&1; then echo "REHEARSAL FAILED: foreign accepted"; exit 1; fi
  echo "  foreign Nord2Metal refused: ok"
done
echo "REHEARSAL_OK usecount1 over thumbfix5 (mirror state) and thumbfix4; keepfreeze flag set/cleared/undone"
