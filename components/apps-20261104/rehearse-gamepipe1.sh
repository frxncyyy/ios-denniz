#!/bin/bash
# Rehearsal of guest-gamepipe1.sh / -off.sh (off and on) / -undo.sh on fake roots made from the mirror's bundle + receipt, once over each
# accepted earlier Nord2Metal (thumbfix5, thumbfix4); unshare -r; nothing touches the mirror or a phone. Every guest write is checked against
# owned-gamepipe1.json. Scratch under $T (default: a new directory below /tmp/claude-1000, removed at the end).
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
T=${T:-$(mktemp -d -p /tmp/claude-1000 rehearse-gamepipe1.XXXXXX)}; trap 'rm -rf "$T"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/|'$2/|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
owned() { python3 - "$R" "$A/owned-gamepipe1.json" "$F/before.lst" <<'PY'
import json,os,sys
R,own,before=sys.argv[1:]; own=set(json.load(open(own))); old=set(open(before).read().split('\n'))
now=set()
for b,ds,fs in os.walk(R):
    for n in ds+fs: now.add('/'+os.path.relpath(os.path.join(b,n),R))
bad=[p for p in (now^old) if p and p not in own]
assert not bad, 'written outside owned-gamepipe1.json: %r'%bad
PY
}
for OLD in payload-thumbfix5 payload-thumbfix4; do
  F=$T/$OLD; mkdir -p "$F/root"; R=$F/root
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
  (cd "$R" && find . -mindepth 1 | sed 's|^\.||') > "$F/before.lst"
  S0=$(st); echo "== over $OLD ($(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)), library $(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-12)"
  if run guest-gamepipe1-off.sh "$A/payload-gamepipe1-off" > "$F/o.log" 2>&1; then echo "REHEARSAL FAILED: off accepted without gamepipe1"; exit 1; fi
  echo "  off refused before install: ok"
  run guest-gamepipe1.sh "$A/payload-gamepipe1" | grep -E '^GAMEPIPE1_INSTALLED|^CC_'
  run guest-gamepipe1.sh "$A/payload-gamepipe1" | grep -E '^GAMEPIPE1_ALREADY'
  [ "$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-64)" = "4e0673355e60e7ba65dba8ca84ae1023da55fdf9cee87d070eed07a0c3dcf3d7" ] || { echo "REHEARSAL FAILED: wrong file"; exit 1; }
  owned
  run guest-gamepipe1-off.sh "$A/payload-gamepipe1-off" | grep -E '^GAMEPIPE1_OFF'
  [ "$(cat "$R/private/var/db/nord2-metal/no-gamepipe")" = "made-by-gamepipe1" ] || { echo "REHEARSAL FAILED: flag"; exit 1; }
  run guest-gamepipe1.sh "$A/payload-gamepipe1" | grep -E '^GAMEPIPE1_SWITCH no-gamepipe present'
  run guest-gamepipe1-off.sh "$A/payload-gamepipe1-on" | grep -E '^GAMEPIPE1_ON no-gamepipe removed'
  [ ! -e "$R/private/var/db/nord2-metal/no-gamepipe" ] || { echo "REHEARSAL FAILED: flag not removed"; exit 1; }
  run guest-gamepipe1-off.sh "$A/payload-gamepipe1-off" | grep -E '^GAMEPIPE1_OFF'
  owned
  run guest-gamepipe1-undo.sh "$A/payload-gamepipe1-undo" | grep -E '^GAMEPIPE1_UNDONE'
  run guest-gamepipe1-undo.sh "$A/payload-gamepipe1-undo" | grep -E '^GAMEPIPE1_UNDONE'
  S1=$(st); [ "$S0" = "$S1" ] && echo "  byte-identical after install+off+on+off+undo: ok" || { echo "REHEARSAL FAILED ($OLD)"; exit 1; }
  echo foreign > "$R/$BU/Nord2Metal"; if run guest-gamepipe1.sh "$A/payload-gamepipe1" > "$F/f.log" 2>&1; then echo "REHEARSAL FAILED: foreign accepted"; exit 1; fi
  echo "  foreign Nord2Metal refused: ok"
done
for p in payload-gamepipe1 payload-gamepipe1-undo payload-gamepipe1-off payload-gamepipe1-on; do
  python3 -c "import json;json.load(open('$A/$p/plan.json'))"
done
echo "REHEARSAL_OK gamepipe1 over thumbfix5, thumbfix4 (install, idempotent, off/on switch, undo byte-identical, foreign refused, owned)"
