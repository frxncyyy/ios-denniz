# read-only: copy every file under a *.pipelinelib directory (and the AegirPoster appex binary) out of the rootfs mirror
import os, struct, sys
B, out = sys.argv[1:3]
pats = sys.argv[3:]
sys.path.insert(0, B + '/autocycle')
import hfsx
img = hfsx.Img(B + '/mirror/rootfs-mirror.img'); img.replay(); fs = hfsx.HFS(img)
ent = {}
i = fs.cat.first
while i:
    flink, kind, _, recs = fs.cat.node(i)
    for r in recs:
        p, nm, d = fs.cat.catkey(r)
        rt = struct.unpack('>h', d[:2])[0]
        if rt in (1, 2): ent[struct.unpack('>I', d[8:12])[0]] = (p, nm, rt)
    fs.cat.cache.clear(); i = flink
def path(cn):
    o = []
    while cn in ent and cn != 2:
        p, nm, _ = ent[cn]; o.append(nm); cn = p
    return '/' + '/'.join(reversed(o))
n = 0
for cn, (p, nm, rt) in sorted(ent.items()):
    if rt != 2: continue
    pa = path(cn)
    if not any(x in pa for x in pats): continue
    try:
        c, rec = fs.lookup(pa)
        data = fs.read_fork(fs.fork(rec[88:168]), c)
    except Exception as e:
        print('ERR', pa, e); continue
    dst = out + pa
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'wb').write(data); n += 1
    print(len(data), pa)
print('files', n)
