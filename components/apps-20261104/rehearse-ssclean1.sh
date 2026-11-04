#!/bin/bash
# Rehearsal of guest-ssclean1.sh / -undo.sh / -dropbackup.sh (+ undo from IPA) on a fake root made from the mirror:
# /Applications/SubwaySurf.app with the catalog's own modes (preflight: every path, mode and uid/gid 0 read from the HFS catalog),
# the LaunchServices csstores (+ .nord2-prev) of InternalDaemon E7BA914B. unshare -r; nothing touches the mirror or a phone.
# Every guest write is checked against owned-ssclean1.json (path set AND content/mode of surviving paths).
# Scratch under $T (default: a new directory on disk below apps-20261104, ~1.2 GB peak, removed at the end; /tmp is not used).
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); M=$B/mirror/rootfs-mirror.img
T=${T:-$(mktemp -d -p "$A" .rehearse-ssclean1.XXXXXX)}; trap 'rm -rf "$T"' EXIT
R=$T/root; F=$T/f; mkdir -p "$R" "$F"
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/|'$2/|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
mustfail() { if run "$1" "$2" > "$F/neg.log" 2>&1; then echo "REHEARSAL FAILED: $3 accepted"; cat "$F/neg.log"; exit 1; fi; echo "  $3 refused: ok ($(grep -o 'AssertionError: .*' "$F/neg.log" | cut -c1-90))"; }
state() { python3 - "$R" <<'PY'
import hashlib,os,sys
R=sys.argv[1]; out=[]
for b,ds,fs in os.walk(R):
    for n in ds+fs:
        p=os.path.join(b,n); st=os.lstat(p); rel='/'+os.path.relpath(p,R)
        out.append((rel, oct(st.st_mode), hashlib.sha256(open(p,'rb').read()).hexdigest() if os.path.isfile(p) else 'dir'))
out.sort(); print(hashlib.sha256(repr(out).encode()).hexdigest()[:16])
PY
}
snap() { python3 - "$R" "$1" <<'PY'
import hashlib,json,os,sys
R,o=sys.argv[1:]; d={}
for b,ds,fs in os.walk(R):
    for n in ds+fs:
        p=os.path.join(b,n); st=os.lstat(p)
        d['/'+os.path.relpath(p,R)]=[st.st_mode, hashlib.sha256(open(p,'rb').read()).hexdigest() if os.path.isfile(p) else 'dir']
json.dump(d,open(o,'w'))
PY
}
owned() { python3 - "$F/pre.json" "$F/post.json" "$A/$1" <<'PY'
import json,sys
a,b,own=(json.load(open(x)) for x in sys.argv[1:4]); own=set(own)
changed=[p for p in set(a)|set(b) if a.get(p)!=b.get(p)]
bad=[p for p in changed if p not in own]
assert not bad, 'written outside the owned list: %r'%sorted(bad)[:10]
print('  owned ok: %d changed paths, all in %s'%(len(changed), sys.argv[3].rsplit('/',1)[1]))
PY
}
echo "== fake root from the mirror (catalog modes)"
python3 - "$B/autocycle/hfsx.py" "$M" "$R" "$A/payload-ssclean1/old-manifest.json" <<'PY'
import importlib.util, json, os, struct, sys
hx, img, R, man = sys.argv[1:]
s = importlib.util.spec_from_file_location('hfsx', hx); h = importlib.util.module_from_spec(s); s.loader.exec_module(h)
im = h.Img(img); im.replay(); fs = h.HFS(im); old = json.load(open(man))
def perms(data):
    uid, gid = struct.unpack('>II', data[32:40]); mode = struct.unpack('>H', data[42:44])[0]; return uid, gid, mode
cnid, _ = fs.lookup('/Applications/SubwaySurf.app'); dst = R + '/Applications/SubwaySurf.app'
seen_f, seen_d, bad = {}, set(), []
def walk(c, d, rel):
    os.makedirs(d, exist_ok=True)
    for nm, data in fs.listdir(c):
        rt = struct.unpack('>h', data[:2])[0]; uid, gid, mode = perms(data); r = (rel + '/' + nm).lstrip('/'); p = os.path.join(d, nm)
        if uid or gid: bad.append(('owner', r, uid, gid))
        if rt == 1:
            seen_d.add(r); walk(struct.unpack('>I', data[8:12])[0], p, r); os.chmod(p, mode & 0o7777)
            if mode & 0o7777 != 0o755: bad.append(('dirmode', r, oct(mode)))
        elif rt == 2:
            assert mode & 0o170000 == 0o100000, 'non-regular in bundle: ' + r
            buf = fs.read_fork(fs.fork(data[88:168]), struct.unpack('>I', data[8:12])[0])
            open(p, 'wb').write(buf); os.chmod(p, mode & 0o7777); seen_f[r] = mode & 0o7777
walk(cnid, dst, '')
os.chmod(dst, 0o755)
assert set(seen_f) == set(old['files']) and seen_d == set(old['dirs']), 'mirror bundle paths differ from the modded IPA list'
bad += [('filemode', r, oct(m)) for r, m in seen_f.items() if m != old['files'][r][1]]
assert not bad, bad[:5]
print('  mirror /Applications/SubwaySurf.app: %d files %d dirs, every mode and owner (0:0) as guest-install.sh made them' % (len(seen_f), len(seen_d)))
LS = '/private/var/mobile/Containers/Data/InternalDaemon/E7BA914B-B80E-478B-B724-FD94EE472472/Library/Caches'
c, _ = fs.lookup(LS); os.makedirs(R + LS)
for nm, data in fs.listdir(c):
    if 'LaunchServices' in nm and struct.unpack('>h', data[:2])[0] == 2:
        open(R + LS + '/' + nm, 'wb').write(fs.read_fork(fs.fork(data[88:168]), struct.unpack('>I', data[8:12])[0])); print('  ', nm)
os.makedirs(R + '/private/var/db', exist_ok=True)
PY
S0=$(state); snap "$F/pre.json"; echo "  state $S0"
echo "== negative cases before install"
mustfail guest-ssclean1-dropbackup.sh "$A/payload-ssclean1-dropbackup" "dropbackup without ssclean1"
run guest-ssclean1-undo.sh "$A/payload-ssclean1-undo" | grep -E '^SSCLEAN1_UNDO_ALREADY'
[ "$(state)" = "$S0" ] || { echo "REHEARSAL FAILED: undo-before-install changed something"; exit 1; }
echo "== install"
run guest-ssclean1.sh "$A/payload-ssclean1" | grep -E '^SSCLEAN1_|^INSTALLED|^LS_|^CC_'
run guest-ssclean1.sh "$A/payload-ssclean1" | grep -E '^SSCLEAN1_ALREADY'
snap "$F/post.json"; owned owned-ssclean1.json
! find "$R/Applications/SubwaySurf.app" | grep -q -E 'iGameGod|libSubwaySurfers' || { echo "REHEARSAL FAILED: mod files in the new bundle"; exit 1; }
[ "$(unshare -r stat -c '%u:%g %a' "$R/Applications/SubwaySurf.app/SubwaySurf")" = "0:0 755" ] || { echo "REHEARSAL FAILED: exe owner/mode"; exit 1; }
echo "  new bundle: no iGameGod/libSubwaySurfers, SubwaySurf 0:0 755: ok"
echo "== undo (kept backup)"
cp "$F/post.json" "$F/pre.json"
run guest-ssclean1-undo.sh "$A/payload-ssclean1-undo" | grep -E '^SSCLEAN1_UNDONE|^LS_|^CC_'
snap "$F/post.json"; owned owned-ssclean1-undo.json
run guest-ssclean1-undo.sh "$A/payload-ssclean1-undo" | grep -E '^SSCLEAN1_UNDO_ALREADY'
S1=$(state); [ "$S1" = "$S0" ] && echo "  byte-identical after install+undo: ok ($S1)" || { echo "REHEARSAL FAILED: undo not identical"; exit 1; }
echo "== install, dropbackup, undo from IPA"
run guest-ssclean1.sh "$A/payload-ssclean1" | grep -E '^INSTALLED'
snap "$F/pre.json"
run guest-ssclean1-dropbackup.sh "$A/payload-ssclean1-dropbackup" | grep -E '^SSCLEAN1_BACKUP_DROPPED'
snap "$F/post.json"; owned owned-ssclean1-dropbackup.json
run guest-ssclean1-dropbackup.sh "$A/payload-ssclean1-dropbackup" | grep -E '^SSCLEAN1_BACKUP_ALREADY_DROPPED'
run guest-ssclean1.sh "$A/payload-ssclean1" | grep -E '^SSCLEAN1_ALREADY'
mustfail guest-ssclean1-undo.sh "$A/payload-ssclean1-undo" "undo without backup and without IPA"
cp "$F/post.json" "$F/pre.json"
run guest-ssclean1-undo.sh "$A/payload-ssclean1-undoipa" | grep -E '^SSCLEAN1_UNDONE|^LS_'
snap "$F/post.json"; owned owned-ssclean1-undo.json
S2=$(state); [ "$S2" = "$S0" ] && echo "  identical (content+modes) after install+dropbackup+undoipa: ok ($S2)" || { echo "REHEARSAL FAILED: undoipa not identical"; exit 1; }
echo "== foreign bundle refused"
cp -p "$R/Applications/SubwaySurf.app/Info.plist" "$F/ip.bak"; echo x >> "$R/Applications/SubwaySurf.app/Info.plist"
mustfail guest-ssclean1.sh "$A/payload-ssclean1" "modified modded bundle"
cp -p "$F/ip.bak" "$R/Applications/SubwaySurf.app/Info.plist"; [ "$(state)" = "$S0" ] || { echo "REHEARSAL FAILED: refusal wrote"; exit 1; }
for p in payload-ssclean1 payload-ssclean1-undo payload-ssclean1-undoipa payload-ssclean1-dropbackup; do python3 -c "import json;json.load(open('$A/$p/plan.json'))"; done
echo "REHEARSAL_OK ssclean1 (mirror bundle = modded IPA list with catalog modes, install, idempotent, owned, undo byte-identical, dropbackup + undoipa identical, foreign refused)"
