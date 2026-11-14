import pickle,sys,re
ev=pickle.load(open(sys.argv[1],'rb'))
L=open(sys.argv[2],errors='replace').read().split('\n')
cc=set(i for i,l in enumerate(L,1) if 'UICollectionViewFlowLayout is not defined' in l)
lo=int(sys.argv[3]); hi=int(sys.argv[4])
lastT=None; out=[]
down=None
for i,k,d in ev:
    if i<lo or i>hi: continue
    if k=='T':
        if d!=lastT: lastT=d
    elif k=='H':
        if d[0]=='down': down=(i,d)
        else:
            if down:
                (di,dd)=down
                out.append((di,'touch %d,%d%s -> %d,%d'%(dd[1],dd[2],' EDGE' if dd[3] else '',d[1],d[2])+'  t~%s'%lastT))
                down=None
    elif k=='CL':
        out.append((i,'== CLIENT win=%d %s'%(d[0],' '.join('%s %d/%d'%(n,a,b) for n,(a,b) in d[1].items()))))
    elif k=='PA':
        out.append((i,'   PACE active_fps=%.1f k1=%d rpp=%s dropped=%d (ready %d nosw %d) best_run=%d'%(d[3],d[4],'/'.join(map(str,d[5:10])),d[10],d[11],d[12],d[13])))
    elif k=='DV':
        out.append((i,'   DV now L=%s B=%s G=%s utilL=%s B=%s G=%s class=%s therm=%s bat=%s cls=%s late=%s'%(d[6],d[7],d[8],d[3],d[4],d[5],d[11],d[15],d[16],'/'.join(d[21:26]),d[26])))
    elif k=='GD':
        out.append((i,'   GD busy avg=%s max=%s want=%s cap=%s why=%s'%(d[5],d[6],d[2],d[3],d[8])))
    elif k=='SY':
        out.append((i,'   SYS cpu=%s avg %s big %s pgin=%s'%(d[1],d[2],d[3],d[4])))
    elif k=='GJ':
        out.append((i,'   GJ n=%d full=%d q=%d/%d run=%d/%d'%(d[0],d[2],d[4],d[5],d[6],d[7])))
ccl=sorted(c for c in cc if lo<=c<=hi)
for c in ccl: out.append((c,'   ~CCanim'))
out.sort()
prev=None
for i,s in out:
    if s=='   ~CCanim':
        if prev=='cc': continue
        prev='cc'
    else: prev=None
    print(i,s)
