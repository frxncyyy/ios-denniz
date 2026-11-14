import pickle,sys
ev=pickle.load(open(sys.argv[1],'rb'))
gl=[(i,d) for i,k,d in ev if k=='GL']
pa=[(i,d) for i,k,d in ev if k=='PA_']
def nxt(kind,i):
    for j,k,d in ev:
        if k==kind and i<j<=i+6: return d
rows=[]
for i,a in pa:
    # nearest GL line
    j,g=min(gl,key=lambda x:abs(x[0]-i))
    if abs(j-i)>60: continue
    d=nxt('PD_',i)
    win=a[0]/1e6
    cj=g[9]*1000.0/g[1]; fj=g[11]*1000.0/g[1]   # jobs/s
    if cj<300 or fj<300: continue
    js0=a[2]/654e6/win; js1=a[3]/654e6/win; til=a[4]/654e6/win; gpu=a[1]/654e6/win
    rows.append((i,cj,fj,1e6*js1/cj,1e6*til/cj,1e6*(js1-til)/cj,1e6*js0/fj, g[8]/10.0, 100*gpu, d[3]*4/win/fj/1e3))
print('line  cjobs/s fjobs/s | js1 us/job  tiler us/job  (js1-til) | js0 us/fjob | drv busy%  hw gpu% | kpx/fjob')
for r in rows: print('%5d %6.0f %6.0f | %6.1f %6.1f %6.1f | %6.1f | %5.1f %5.1f | %6.0f'%r)
import statistics as st
print('min (js1-til)/job', min(r[5] for r in rows), 'median', st.median(r[5] for r in rows), 'min js1/job', min(r[3] for r in rows), 'min js0/fjob', min(r[6] for r in rows))
