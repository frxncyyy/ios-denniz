import pickle,sys
ev=pickle.load(open(sys.argv[1],'rb'))
# assemble PRFCNT sets by consecutive lines
pa=[(i,d) for i,k,d in ev if k=='PA_']
def nxt(kind,i):
    for j,k,d in ev:
        if k==kind and i<j<=i+6: return d
    return None
cls=[(i,d) for i,k,d in ev if k=='CL']
pace=[(i,d) for i,k,d in ev if k=='PA']
gds=[(i,d) for i,k,d in ev if k=='GD']
def fps_at(i):
    # frames/s from the FB window containing line i (next CL after i)
    for (j,d) in cls:
        if j>=i:
            p=[x for (q,x) in pace if j<=q<=j+8]
            return (j, p[0][3] if p else 0, d[1].get('backboardd',(0,0))[1]*1000.0/d[0])
    return (0,0,0)
print('line  cl_line afps bbd/s | gpu%  js0% js1% til% | frag%core exec% | Mquads/s Mpx/frame ptiles/f | rdGB/s wrGB/s (16B/beat) texrd wrtib | wstall/gpucyc | lat avg(cyc) >256% | texstall/texflit')
for i,a in pa:
    b=nxt('PB_',i); c=nxt('PC_',i); d=nxt('PD_',i); e=nxt('PE_',i)
    if not(b and c and d and e): continue
    win=a[0]/1e6; cyc=654e6*win
    cl,af,bbd=fps_at(i)
    fr=max(af,1)
    lat_n=sum(c); lat_avg=(c[0]*64+c[1]*160+c[2]*224+c[3]*288+c[4]*352)/max(1,lat_n)
    print('%5d %5d %5.1f %5.1f | %4.0f %4.0f %4.0f %4.0f | %4.0f %4.0f | %6.0f %5.1f %6.0f | %5.2f %5.2f %5.2f %5.2f | %4.2f | %4.0f %3.0f | %4.2f'%(
        i,cl,af,bbd,100*a[1]/cyc,100*a[2]/cyc,100*a[3]/cyc,100*a[4]/cyc,
        100*d[0]/(cyc*9),100*d[1]/(cyc*9),
        d[3]/win/1e6, d[3]*4/win/fr/1e6, d[5]/win/fr,
        b[3]*16/win/1e9,b[4]*16/win/1e9,e[4]*16/win/1e9,e[5]*16/win/1e9,
        b[6]/max(1,a[1]), lat_avg,100*(c[3]+c[4])/max(1,lat_n), e[2]/max(1,e[0])))
