import pickle,sys,re,statistics as st
ev=pickle.load(open(sys.argv[1],'rb'))
L=open(sys.argv[2],errors='replace').read().split('\n')
cc=[i for i,l in enumerate(L,1) if 'UICollectionViewFlowLayout is not defined' in l]
# windows delimited by CL events
cls=[(i,d) for i,k,d in ev if k=='CL']
prev=0
print('line  win  bbd_fresh/s app(shown/fresh) | PACE afps k1 rpp drops(ready/nosw) | GPUbusy avg[max] | bat therm | cpuLmax Bmax now | class jank% | topSw botSw taps CCanim | jobs/s q_us run_us | pgin | t')
for (i,d) in cls:
    w=[(j,k,x) for j,k,x in ev if prev<j<=i+8]
    pa=[x for j,k,x in w if k=='PA']
    gd=[x for j,k,x in w if k=='GD' and j<=i]
    dv=[x for j,k,x in w if k=='DV' and j<=i]
    gj=[x for j,k,x in w if k=='GJ' and j<=i]
    sy=[x for j,k,x in w if k=='SY' and j<=i]
    T=[x for j,k,x in w if k=='T' and j<=i]
    top=bot=tap=0; down=None
    for j,k,x in w:
        if k!='H' or j>i: continue
        if x[0]=='down': down=x
        elif down:
            dy=x[2]-down[2]
            if down[2]<80 and dy>300: top+=1
            elif down[2]>2300 and dy<-300: bot+=1
            elif abs(dy)<30 and abs(x[1]-down[1])<30: tap+=1
            down=None
    nc=len([c for c in cc if prev<c<=i])
    win=d[0]; bb=d[1].get('backboardd',(0,0)); others=' '.join('%s:%d/%d'%(n[:6],a,b) for n,(a,b) in d[1].items() if n!='backboardd')
    p=pa[-1] if pa else None
    ps='%5.1f %4d %s %3d(%d/%d)'%(p[3],p[4],'/'.join(map(str,p[5:10])),p[10],p[11],p[12]) if p else '-'
    gb='%3d[%3d]'%(st.mean(int(x[5]) for x in gd),max(int(x[6]) for x in gd)) if gd else '-'
    bat=dv[-1][16] if dv else '-'; th=dv[-1][15] if dv else '-'
    ul=max(int(x[3]) for x in dv) if dv else -1; ub=max(int(x[4]) for x in dv) if dv else -1
    now='%s/%s/%s'%(dv[-1][6],dv[-1][7],dv[-1][8]) if dv else '-'
    jank=st.mean(int(x[25]) for x in dv) if dv else -1
    jb='%5d %5d %4d'%(sum(x[0] for x in gj)*1000/max(1,sum(x[3] for x in gj)), st.mean(x[4] for x in gj), st.mean(x[6] for x in gj)) if gj else '-'
    pg=sum(int(x[4]) for x in sy)
    print('%5d %5d %5.1f %-22s | %s | %s | %s %s | %4d %4d %s | %3d | %d %d %d %d | %s | %d | %s'%(i,win,bb[1]*1000/win,others,ps,gb,bat,th,ul,ub,now,jank,top,bot,tap,nc,jb,pg,T[-1] if T else ''))
    prev=i
