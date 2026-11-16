#!/bin/bash
# rehearsal of guest-storage-purge2.sh on a fake root rebuilt from the rootfs mirror (real names, sparse files of the real sizes)
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d -p "$A" .rehearse-storage-purge2.XXXXXX); trap 'rm -rf "$F"' EXIT
P=$A/payload-storage-purge2
python3 - "$B/autocycle" "$B/mirror/rootfs-mirror.img" "$F" "$P/purge2-list.json" <<'PY'
import sys,struct,os,json
sys.path.insert(0,sys.argv[1]); import hfsx
img=hfsx.Img(sys.argv[2]); img.replay(); fs=hfsx.HFS(img); F=sys.argv[3]
L=json.load(open(sys.argv[4]))
dirs=set(os.path.dirname(x['path']) for x in L)
dirs|={'/private/var/mobile/Media/DCIM','/private/var/tmp/com.tegu.metal.pipelines','/private/var/db/uuidtext/dsc','/private/var/db/nord2-metal'}
n=0
for d in sorted(dirs):
    try: c,_=fs.lookup(d)
    except Exception: continue
    os.makedirs(F+d,exist_ok=True)
    for nm,data in fs.listdir(c):
        if struct.unpack('>h',data[:2])[0]!=2: continue
        with open(F+d+'/'+nm.replace('/',':'),'wb') as o: o.truncate(struct.unpack('>Q',data[88:96])[0])
        n+=1
print('fake root files',n)
PY
snap(){ (cd $F && find . -type f | sort); }
run(){ sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/guest-storage-purge2.sh" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$1/|g" | python3 -; }
snap > $F.before
run $P > $F.out1; grep -c '^PURGE2_REMOVED' $F.out1; grep '^PURGE2_\(CATEGORY\|TOTAL\)' $F.out1
snap > $F.after
python3 - $F.before $F.after $P/purge2-list.json <<'PY'
import sys,json
b=set(l.strip()[1:] for l in open(sys.argv[1])); a=set(l.strip()[1:] for l in open(sys.argv[2]))
L=set(x['path'].replace(':','/') for x in json.load(open(sys.argv[3])))
gone=b-a
assert gone==(L&b), ('unexpected', sorted(gone^(L&b))[:5])
print('listed %d, present on the fake root %d (the rest already gone on the phone and skipped)'%(len(L),len(L&b)))
for k in ['/private/var/db/uuidtext/dsc/8F970452C9E33E88B114FEB1864BC9E0','/private/var/db/nord2-metal/libredwoodair.dylib.pre-libmerge6','/private/var/db/nord2-metal/Nord2Metal.pre-thumbcomp5','/private/var/db/nord2-metal/receipt.json','/private/var/db/nord2-metal/launchd.original']:
    assert k in a, k
kept=lambda pre: len([x for x in a if x.startswith(pre)])
print('removed exactly the list (%d); kept: dcim %d, tegu pipelines %d, crashreports %d, Persist %d, nord2-metal %d'%(len(gone),kept('/private/var/mobile/Media/DCIM/'),kept('/private/var/tmp/com.tegu.metal.pipelines/'),kept('/private/var/mobile/Library/Logs/CrashReporter/'),kept('/private/var/db/diagnostics/Persist/'),kept('/private/var/db/nord2-metal/')))
PY
run $P > $F.out2; grep -q 'PURGE2_TOTAL files=0 bytes=0' $F.out2; echo "second run: nothing to remove"
T=$(mktemp -d -p "$A" .rehearse-storage-purge2-bad.XXXXXX); python3 -c "import json,sys;L=json.load(open(sys.argv[1]));L.append({'path':'/private/var/mobile/Media/DCIM/100APPLE/IMG_0001.HEIC','size':1,'cat':'x'});json.dump(L,open(sys.argv[2],'w'))" $P/purge2-list.json $T/purge2-list.json
run $T | tee $F.out3 | tail -1; grep -q PURGE2_REFUSED $F.out3; rm -rf $T
rm -f $F.before $F.after $F.out1 $F.out2 $F.out3; echo REHEARSAL_OK
