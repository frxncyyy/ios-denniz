#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-libmerge1.sh / guest-libmerge1-undo.sh on FAKE roots (the scripts' own Python with /work/root and /src replaced), inside
# `unshare -r` so chown(0,0) works. The fake root carries the REAL Nord2Metal.bundle entries and receipt.json read from the rootfs mirror
# (read-only, autocycle/hfsx.py), brought to y2s65-thumbsync as rehearse-thumbrc.sh does. Nothing touches the mirror or a phone.
# Scenarios: from the mirror's state; from the base 6df1af45; from thumbrc (its real guest); from camstats1 and gnr1 (SIMULATED: their guests
# refuse Nord2Metal thumbsync, so their file, backup, receipt entry (and gnr1's switch + marker) are written as their guests would);
# refusals: a foreign library, a Nord2Metal other than thumbsync, a stale libmerge1 record.        usage: rehearse-libmerge1.sh
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-/tmp/n2hw-opus-ios/rehearse-libmerge1}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"; BU=System/Library/Extensions/Nord2Metal.bundle; DB=private/var/db/nord2-metal
NEW=616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d; BASE=6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea
TRC=35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52; CST=0f7c4bad29f85b12c8a173dfde03e6014c117657ed8ce771764df6b5ad31a1b0
GNR=e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90
mkdir -p "$R/$BU" "$R/$DB"
$H get /$DB "$R/$DB" receipt.json >/dev/null 2>&1
NAMES=$(python3 -c "import json;inv=json.load(open('$R/$DB/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
$H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
chmod 755 "$R/$BU/Nord2Metal"
# the mirror's own copies of the backups and the gnr1 switch, where present (so a variant's state is rehearsed as it is)
for f in libredwoodair.dylib.pre-thumbrc libredwoodair.dylib.pre-camstats1 libredwoodair.dylib.pre-gnr1 libredwoodair.dylib.pre-libmerge1 gnr1-trace.made-by-gnr1; do
  $H get /$DB "$R/$DB" $f >/dev/null 2>&1 || true; done
$H get /private/var/db "$R/private/var/db" nord2-gpu-gnr1-trace >/dev/null 2>&1 || true
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|" > "$F/$1.py"
        unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f ! -path "./$DB/receipt.json" -exec sha256sum {} + | sort; sha256sum $DB/receipt.json) | sha256sum | cut -c1-16; }
lib() { sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-64; }
M=$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)
if [ "$M" = 59212d387b54 ]; then run guest-thumbstage.sh "$A/payload-thumbstage" | grep -E '^THUMBSTAGE_INSTALLED'; M=6482590ef28a; fi
if [ "$M" = 6482590ef28a ]; then echo "mirror is at thumbstage: installing thumbsync first"; run guest-thumbsync.sh "$A/payload-thumbsync" | grep -E '^THUMBSYNC_INSTALLED'; fi
echo "mirror: Nord2Metal $(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12) libredwoodair $(lib | cut -c1-12) bundle: $NAMES"
cp -a "$R" "$F/root.mirror"
reset() { rm -rf "$R"; cp -a "$F/$1" "$R"; }
cycle() {   # install, again, undo, undo again: the root must come back byte for byte
  local S0 S1; S0=$(st); echo "  start $S0 libredwoodair $(lib | cut -c1-12)"
  run guest-libmerge1.sh "$A/payload-libmerge1" | grep -E '^LIBMERGE1_(FROM|INSTALLED|GNR1)|^CC_' | sed 's/^/    /'
  [ "$(lib)" = $NEW ] && echo "    libredwoodair.dylib = y2s67-libmerge1: ok"
  python3 -c "import json;m=json.load(open('$R/$DB/receipt.json'))['libmerge1'];assert m['previous']=='$1',m;print('    receipt libmerge1.previous =',m['previous'][:12],m['previous_name'],': ok')"
  run guest-libmerge1.sh "$A/payload-libmerge1" | grep -E '^LIBMERGE1_ALREADY' | sed 's/^/    /'
  run guest-libmerge1-undo.sh "$A/payload-libmerge1-undo" | grep -E '^LIBMERGE1_UNDONE|^CC_RECEIPT' | sed 's/^/    /'
  [ "$(lib)" = "$1" ] && echo "    libredwoodair.dylib back to $(lib | cut -c1-12): ok"
  run guest-libmerge1-undo.sh "$A/payload-libmerge1-undo" | grep -E '^LIBMERGE1_UNDONE' | sed 's/^/    /'
  S1=$(st); [ "$S0" = "$S1" ] && echo "  root byte-identical after install+undo: ok" || { echo "REHEARSAL FAILED: $S0 != $S1"; exit 1; }
}
simulate() {  # simulate SHA BACKUP [gnr1]: a variant installed as its guest would (file, backup = base, receipt inventory)
  cp "$B/$4/payload/Nord2Metal.bundle/libredwoodair.dylib" "$R/$BU/libredwoodair.dylib"; [ "$(lib)" = "$1" ]
  cp "$F/base.lib" "$R/$DB/libredwoodair.dylib.$2"
  python3 -c "import json;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['libredwoodair.dylib']='$1';open(p,'w').write(json.dumps(r,indent=2)+'\n')"
  if [ "${3:-}" = gnr1 ] && [ ! -e "$R/private/var/db/nord2-gpu-gnr1-trace" ]; then : > "$R/private/var/db/nord2-gpu-gnr1-trace"; echo 'y2s63-gnr1 created /private/var/db/nord2-gpu-gnr1-trace' > "$R/$DB/gnr1-trace.made-by-gnr1"; fi
}
ML=$(lib)
echo "== 1. from the mirror's state ($(lib | cut -c1-12))"; cycle $ML
# a base root: the mirror's, or the mirror's with its variant undone
reset root.mirror
case $ML in
  $BASE) ;;
  $TRC) run guest-thumbrc-undo.sh "$A/payload-thumbrc-undo" | grep -E '^THUMBRC_UNDONE' ;;
  $CST|$GNR) python3 -c "
import json,os;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['libredwoodair.dylib']='$BASE';open(p,'w').write(json.dumps(r,indent=2)+'\n')"
     for b in pre-camstats1 pre-gnr1; do [ -f "$R/$DB/libredwoodair.dylib.$b" ] && mv "$R/$DB/libredwoodair.dylib.$b" "$R/$BU/libredwoodair.dylib" || true; done
     echo "(variant undone by hand for the base scenarios)";;
  *) echo "REHEARSAL FAILED: mirror library $ML unknown"; exit 1;;
esac
[ "$(lib)" = $BASE ]; cp "$R/$BU/libredwoodair.dylib" "$F/base.lib"; rm -rf "$F/root.base"; cp -a "$R" "$F/root.base"
if [ $ML != $BASE ]; then echo "== 2. from the base"; cycle $BASE; fi
echo "== 3. from thumbrc (its real guest), then thumbrc's own undo"
reset root.base; SB=$(st); run guest-thumbrc.sh "$A/payload-thumbrc" | grep -E '^THUMBRC_INSTALLED' | sed 's/^/  /'
cycle $TRC
[ "$(sha256sum < "$R/$DB/libredwoodair.dylib.pre-thumbrc" | cut -c1-64)" = $BASE ] && echo "  thumbrc's backup untouched: ok"
run guest-thumbrc-undo.sh "$A/payload-thumbrc-undo" | grep -E '^THUMBRC_UNDONE' | sed 's/^/  /'
[ "$(st)" = "$SB" ] && echo "  thumbrc undo after libmerge1 undo: back to the base root: ok" || { echo "REHEARSAL FAILED: thumbrc chain"; exit 1; }
echo "== 4. from camstats1 (simulated)"; reset root.base; simulate $CST pre-camstats1 "" y2s65-cam-stats; cycle $CST
echo "== 5. from gnr1 (simulated, with its trace switch)"; reset root.base; simulate $GNR pre-gnr1 gnr1 y2s63-gnr1; cycle $GNR
echo "== 6. refusals (each must stop and change nothing)"
refuse() { local S0 L; S0=$(st); if run $1 "$A/$2" > "$F/refuse.log" 2>&1; then echo "REHEARSAL FAILED: $3 accepted"; exit 1; fi
           L=$(grep -E 'AssertionError' "$F/refuse.log" | tail -1 | cut -c1-200); [ "$(st)" = "$S0" ] && echo "  $3: refused, nothing changed: ok ($L)" || { echo "REHEARSAL FAILED: $3 changed the root"; exit 1; }; }
reset root.base; echo foreign > "$R/$BU/libredwoodair.dylib"
python3 -c "import json,hashlib;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['libredwoodair.dylib']=hashlib.sha256(b'foreign\n').hexdigest();open(p,'w').write(json.dumps(r,indent=2)+'\n')"
refuse guest-libmerge1.sh payload-libmerge1 "a foreign libredwoodair (receipt agreeing)"
reset root.base; echo foreign > "$R/$BU/libredwoodair.dylib"; refuse guest-libmerge1.sh payload-libmerge1 "a library differing from its receipt"
reset root.base; cp "$B/y2s63-vt23/payload/Nord2Metal.bundle/Nord2Metal" "$R/$BU/Nord2Metal"
python3 -c "import json;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['Nord2Metal']='59212d387b5400b8ef524f36c6b437e67e4b2390e9c222c77acf62c621954a1d';open(p,'w').write(json.dumps(r,indent=2)+'\n')"
refuse guest-libmerge1.sh payload-libmerge1 "Nord2Metal y2s63-vt23 instead of thumbsync"
reset root.base; simulate $CST pre-camstats1 "" y2s65-cam-stats; rm "$R/$DB/libredwoodair.dylib.pre-camstats1"
refuse guest-libmerge1.sh payload-libmerge1 "camstats installed without its backup"
reset root.base; python3 -c "import json;p='$R/$DB/receipt.json';r=json.load(open(p));r['libmerge1']={'previous':'$BASE'};open(p,'w').write(json.dumps(r,indent=2)+'\n')"
refuse guest-libmerge1.sh payload-libmerge1 "a stale libmerge1 record"
reset root.base; run guest-libmerge1.sh "$A/payload-libmerge1" > /dev/null; echo junk > "$R/$DB/libredwoodair.dylib.pre-libmerge1"
refuse guest-libmerge1-undo.sh payload-libmerge1-undo "undo with a wrong backup"
reset root.base; run guest-libmerge1.sh "$A/payload-libmerge1" > /dev/null; rm "$R/$DB/libredwoodair.dylib.pre-libmerge1"
run guest-libmerge1-undo.sh "$A/payload-libmerge1-undo" | grep -E '^LIBMERGE1_UNDONE' | sed 's/^/  backup lost, base recorded: /'
[ "$(lib)" = $BASE ] && echo "  restored from the undo payload: ok"
echo "REHEARSAL_OK libmerge1: mirror state $(echo $ML | cut -c1-12), base, thumbrc, camstats1, gnr1 each install+undo byte-identical; 6 refusals"
