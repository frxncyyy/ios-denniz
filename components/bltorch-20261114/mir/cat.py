import sys,struct,pickle,os
sys.path.insert(0,'$NORD2_WORK/gpu/build/autocycle')
import hfsx
IMG='$NORD2_WORK/gpu/build/mirror/rootfs-mirror.img'
def load():
    img=hfsx.Img(IMG); img.replay(); return hfsx.HFS(img)
def index(fs):
    if os.path.exists('cat.pkl'): return pickle.load(open('cat.pkl','rb'))
    ent={}  # cnid -> (parent,name,type,size)
    i=fs.cat.first
    while i:
        flink,kind,_,recs=fs.cat.node(i)
        for r in recs:
            p,nm,d=fs.cat.catkey(r)
            rt=struct.unpack('>h',d[:2])[0]
            if rt in (1,2):
                cn=struct.unpack('>I',d[8:12])[0]
                ent[cn]=(p,nm,rt,struct.unpack('>Q',d[88:96])[0] if rt==2 else 0)
        fs.cat.cache.clear(); i=flink
    pickle.dump(ent,open('cat.pkl','wb')); return ent
def path(ent,cn):
    out=[]
    while cn in ent and cn!=2:
        p,nm,_,_=ent[cn]; out.append(nm); cn=p
    return '/'+'/'.join(reversed(out))
def get(fs,p):
    cnid,rec=fs.lookup(p); return fs.read_fork(fs.fork(rec[88:168]),cnid)
if __name__=='__main__':
    fs=load(); ent=index(fs); print(len(ent),'entries')
    for pat in sys.argv[1:]:
        for cn,(p,nm,rt,sz) in ent.items():
            if pat in nm: print(rt,sz,path(ent,cn))
