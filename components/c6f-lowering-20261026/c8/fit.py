import re,sys
def solve(A,b):
    n=len(A);M=[A[i][:]+[b[i]] for i in range(n)]
    for c in range(n):
        p=max(range(c,n),key=lambda r:abs(M[r][c]));M[c],M[p]=M[p],M[c]
        for r in range(n):
            if r!=c:
                f=M[r][c]/M[c][c];M[r]=[x-f*y for x,y in zip(M[r],M[c])]
    return [M[i][n]/M[i][i] for i in range(n)]
def lstsq(X,y):
    k=len(X[0]);A=[[sum(x[i]*x[j] for x in X) for j in range(k)] for i in range(k)];b=[sum(x[i]*t for x,t in zip(X,y)) for i in range(k)]
    return solve(A,b)
lines=open(sys.argv[1],errors='replace').read().split('\n')
rows=[];cur={}
for i,l in enumerate(lines,1):
    m=re.search(r'TEGU_PERF_FRAME ab=(\S+) window_ms=(\d+) commands=(\d+) mean_exec_us=(\d+) mean_gpu_us=(\d+) kernel_jobs=(\d+)',l)
    if m: cur=dict(c=int(m.group(3)),ex=int(m.group(4))/1000,kj=int(m.group(6)),line=i)
    m=re.search(r'TEGU_PERF_BATCH cmds=(\d+) enc=(\d+) .*draws=(\d+) jobs=(\d+) jobs_fs=(\d+)/(\d+) jobs_off=(\d+)/(\d+)',l)
    if m and cur: cur.update(enc=int(m.group(2)),draws=int(m.group(3)),fs=int(m.group(5)),off=int(m.group(7))+int(m.group(8)))
    m=re.search(r'TEGU_PERF_RT .*tiles=(\d+)/(\d+)/(\d+)',l)
    if m and cur and 'enc' in cur:
        cur['tiles']=int(m.group(2))
        if cur['c']>=10: rows.append(cur)
        cur={}
X=[];y=[]
for r in rows:
    c=r['c'];X.append([r['kj']/c,r['tiles']/c/1e5,1.0]);y.append(r['ex'])
co=lstsq(X,y);pred=[sum(a*b for a,b in zip(x,co)) for x in X]
import math
print("per kj %.3f ms, per 1e5 tiles %.3f ms, const %.2f ms; rms %.2f n=%d"%(co[0],co[1],co[2],math.sqrt(sum((p-t)**2 for p,t in zip(pred,y))/len(y)),len(y)))
for r,p,x in zip(rows,pred,X):
    if 11100<r['line']<12000: print(r['line'],'kj/c %.1f tiles/c %.0f exec %.1f pred %.1f'%(x[0],x[1]*1e5,r['ex'],p))
print("--- split: fs jobs, offscreen jobs, other kj, tiles")
X2=[];y2=[]
for r in rows:
    c=r['c'];other=max(0,r['kj']-r['fs']-r['off'])
    X2.append([r['fs']/c,r['off']/c,other/c,r['tiles']/c/1e5,1.0]);y2.append(r['ex'])
co=lstsq(X2,y2);pred=[sum(a*b for a,b in zip(x,co)) for x in X2]
print("fs %.3f off %.3f other %.3f per1e5tiles %.3f const %.2f rms %.2f"%(*co,math.sqrt(sum((p-t)**2 for p,t in zip(pred,y2))/len(y2))))
for r,p,x in zip(rows,pred,X2):
    if 11100<r['line']<12000: print(r['line'],'fs %.1f off %.1f other %.1f tiles %.0f exec %.1f pred %.1f'%(x[0],x[1],x[2],x[3]*1e5,r['ex'],p))
print("--- real kernel jobs (BATCH jobs): fs, off, tiles")
rows2=[]
cur={}
for i,l in enumerate(lines,1):
    m=re.search(r'TEGU_PERF_FRAME ab=(\S+) window_ms=(\d+) commands=(\d+) mean_exec_us=(\d+)',l)
    if m: cur=dict(c=int(m.group(3)),ex=int(m.group(4))/1000,line=i)
    m=re.search(r'TEGU_PERF_BATCH cmds=(\d+) enc=(\d+) enc_fs=(\d+) enc_off=(\d+).*draws=(\d+) jobs=(\d+) jobs_fs=(\d+)/(\d+) jobs_off=(\d+)/(\d+)',l)
    if m and cur: cur.update(enc_fs=int(m.group(3)),enc_off=int(m.group(4)),jobs=int(m.group(6)),fs=int(m.group(7))+int(m.group(8)),off=int(m.group(9))+int(m.group(10)))
    m=re.search(r'TEGU_PERF_RT .*tiles=(\d+)/(\d+)/(\d+)',l)
    if m and cur and 'jobs' in cur:
        cur['tiles']=int(m.group(2))
        if cur['c']>=10: rows2.append(cur)
        cur={}
X3=[];y3=[]
for r in rows2:
    c=r['c'];X3.append([r['fs']/c,r['off']/c,r['tiles']/c/1e5,1.0]);y3.append(r['ex'])
co=lstsq(X3,y3);pred=[sum(a*b for a,b in zip(x,co)) for x in X3]
print("per fs job %.3f per off job %.3f per1e5 tile-units %.3f const %.2f rms %.2f n=%d"%(*co,math.sqrt(sum((p-t)**2 for p,t in zip(pred,y3))/len(y3)),len(y3)))
X4=[[x[1],x[2],1.0] for x in X3]
co4=lstsq(X4,y3);pred4=[sum(a*b for a,b in zip(x,co4)) for x in X4]
print("no-fs model: per off job %.3f per1e5 %.3f const %.2f rms %.2f"%(*co4,math.sqrt(sum((p-t)**2 for p,t in zip(pred4,y3))/len(y3))))
for r,x in zip(rows2,X3):
    if 11100<r['line']<12000 or 17400<r['line']<17800: print(r['line'],'fs %.1f off %.1f tiles %.0f encoff %.1f exec %.1f'%(x[0],x[1],x[2]*1e5,r['enc_off']/r['c'],r['ex']))
