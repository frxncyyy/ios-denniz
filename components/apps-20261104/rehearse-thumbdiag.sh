#!/bin/bash
# Rehearsal of guest-thumbdiag.sh / -undo.sh on fake roots made from the mirror's bundle + receipt, once over each accepted earlier
# Nord2Metal (vt23, thumbstage, thumbsync) and library state; unshare -r; nothing touches the mirror or a phone.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
for OLD in payload-thumbfix payload-vt23; do
  F=/tmp/n2hw-opus-ios/rehearse-thumbdiag-$OLD; rm -rf "$F"; mkdir -p "$F/root"; R=$F/root
  mkdir -p "$R/$BU" "$R/private/var/db/nord2-metal"
  $H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" receipt.json >/dev/null 2>&1
  NAMES=$(python3 -c "import json;inv=json.load(open('$R/private/var/db/nord2-metal/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  # put the accepted earlier Nord2Metal in place and make the receipt agree (the state the phone may be in)
  cp "$A/$OLD/Nord2Metal" "$R/$BU/Nord2Metal"; chmod 755 "$R/$BU/Nord2Metal"
  python3 - "$R" <<'PY'
import hashlib,json,sys
R=sys.argv[1]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
for n in ('Nord2Metal','libredwoodair.dylib'): rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest()
open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
  S0=$(st); echo "== over $OLD ($(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)), library $(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-12)"
  run guest-thumbdiag.sh "$A/payload-thumbdiag" | grep -E '^THUMBDIAG_|^CC_' | head -3
  run guest-thumbdiag.sh "$A/payload-thumbdiag" | grep -E '^THUMBDIAG_ALREADY'
  run guest-thumbdiag-undo.sh "$A/payload-thumbdiag-undo" | grep -E '^THUMBDIAG_UNDONE'
  run guest-thumbdiag-undo.sh "$A/payload-thumbdiag-undo" | grep -E '^THUMBDIAG_UNDONE'
  S1=$(st); [ "$S0" = "$S1" ] && echo "  byte-identical after install+undo: ok" || { echo "REHEARSAL FAILED ($OLD)"; exit 1; }
  echo foreign > "$R/$BU/Nord2Metal"; if run guest-thumbdiag.sh "$A/payload-thumbdiag" > "$F/f.log" 2>&1; then echo "REHEARSAL FAILED: foreign accepted"; exit 1; fi
  echo "  foreign Nord2Metal refused: ok"
done
echo "REHEARSAL_OK thumbdiag over thumbfix, vt23"
