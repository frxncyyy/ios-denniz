#!/bin/bash
# Rehearsal of guest-libmerge3-undo-tc.sh then guest-libmerge3-tc.sh (library A/B under Nord2Metal thumbcomp) on a fake root: the mirror's
# bundle, receipt and /private/var/db/nord2-metal records, with Nord2Metal set to y2s77-thumbcomp; unshare -r; nothing touches the mirror.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
F=/tmp/n2hw-opus-ios/rehearse-libmerge3-tc; rm -rf "$F"; mkdir -p "$F/root"; R=$F/root
mkdir -p "$R/$BU" "$R/private/var/db/nord2-metal"
DBN=$($H ls /private/var/db/nord2-metal 2>/dev/null | awk '{print $NF}' | grep -v '^\.' | tr '\n' ' ' || true)
$H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" $DBN >/dev/null 2>&1 || $H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" receipt.json >/dev/null 2>&1
NAMES=$(python3 -c "import json;inv=json.load(open('$R/private/var/db/nord2-metal/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
$H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
cp "$A/payload-thumbcomp/Nord2Metal" "$R/$BU/Nord2Metal"; chmod 755 "$R/$BU/Nord2Metal"
python3 - "$R" <<'PY'
import hashlib,json,sys
R=sys.argv[1]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU]['Nord2Metal']=hashlib.sha256(open(R+'/'+BU+'/Nord2Metal','rb').read()).hexdigest()
open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
lib() { sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-12; }
echo "start: library $(lib), Nord2Metal $(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)"
ls "$R/private/var/db/nord2-metal" | grep -i 'libmerge\|pre-' | head
echo "== undo-tc (back to the base library)"; run guest-libmerge3-undo-tc.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3|^CC_' | head -4; echo "  library now $(lib)"
echo "== install-tc (libmerge3 again)"; run guest-libmerge3-tc.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3|^CC_' | head -4; echo "  library now $(lib)"
[ "$(lib)" = 3c71650a7884 ] && echo "REHEARSAL_OK libmerge3 undo-tc + install-tc under thumbcomp" || echo "REHEARSAL FAILED"
rm -rf "$F"
