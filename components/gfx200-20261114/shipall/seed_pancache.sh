#!/bin/bash
# gfxall4/5: build payload-<SEEDBASE>-seeded (SEEDBASE default gfxall7) = payload-<SEEDBASE> + pancache-seed/ (the read-only shader cache /usr/lib/nord2-metal-cache that
# sandboxed apps can read from their first launch), from the shader cache entries the PHONE wrote while gfxall3/gfxall4 ran:
# /private/var/db/nord2-metal/cache (backboardd, SpringBoard, daemons) + every app's Library/Caches/com.nord2.pancache, read from the
# MIRROR (refreshed by every transaction's snapshot step: run this after the next transaction that follows a usage session).
# Every entry is checked (magic, version, length, payload SHA-1) before it is shipped; the owned list gets each seed path.
#   seed_pancache.sh [MAX_FILES=4000]
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/../.." && pwd)
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
T=$(mktemp -d -p "$A"); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/in"
$H rget /private/var/db/nord2-metal/cache "$T/in/db" 2>/dev/null || true
for u in $($H ls /private/var/mobile/Containers/Data/Application 2>/dev/null | awk '$NF ~ /\/$/ {sub("/$","",$NF); print $NF}'); do
  $H rget "/private/var/mobile/Containers/Data/Application/$u/Library/Caches/com.nord2.pancache" "$T/in/app-$u" >/dev/null 2>&1 || true
done
BASE=${SEEDBASE:-gfxall7}; O=$A/payload-$BASE-seeded; rm -rf "$O"; cp -a "$A/payload-$BASE" "$O"; mkdir "$O/pancache-seed"
python3 -I - "$T/in" "$O/pancache-seed" "${1:-4000}" "$A/owned-gfxall.json" "$A/owned-$BASE-seeded.json" <<'PY'
import hashlib, json, os, struct, sys
src, dst, cap, own_in, own_out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
ok = bad = dup = 0; names = []
for base, _, files in os.walk(src):
    for n in sorted(files):
        if not (len(n) == 43 and n.endswith('.pc') and all(c in '0123456789abcdef' for c in n[:40])): continue
        d = open(os.path.join(base, n), 'rb').read()
        good = len(d) > 52 and len(d) <= (2 << 20) and d[:4] == b'N2PC' and struct.unpack('<I', d[4:8])[0] == 1
        if good:
            plen = struct.unpack('<I', d[28:32])[0]
            good = 52 + plen == len(d) and hashlib.sha1(d[52:]).digest() == d[32:52]
        if not good: bad += 1; continue
        if os.path.exists(os.path.join(dst, n)): dup += 1; continue
        if ok >= cap: continue
        open(os.path.join(dst, n), 'wb').write(d); ok += 1; names.append(n)
own = json.load(open(own_in)) + ['/usr/lib/nord2-metal-cache/' + n for n in names]
json.dump(sorted(set(own)), open(own_out, 'w'), indent=1)
print('SEED: %d entries shipped, %d rejected, %d duplicates (owned list: %s)' % (ok, bad, dup, own_out))
PY
du -sh "$O/pancache-seed"
echo "payload: $O  (guest-$BASE.sh; owned: owned-$BASE-seeded.json) -- reinstall: UNDO=1 VARIANT=$BASE first if $BASE is installed"
