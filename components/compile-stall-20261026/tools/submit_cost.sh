#!/bin/bash
# submit_cost.sh TAG : steady exec_us per frame for each scenario/mode, and a least-squares fit exec = a*jobs + b*draws
G=$NORD2_WORK/gpu; W=$G/build/compile-stall-20261026
A=$(sed -n 1p $W/tools/fbargs.txt);L=$(sed -n 2p $W/tools/fbargs.txt); cd $G/build/frame-bench-20261026
env LD_LIBRARY_PATH=$L ASAN_OPTIONS=detect_leaks=0 FB_ASYNC=1 TEGU_PLAN_MEMO_VERIFY=0 nice -n 10 ./frame_bench_bin $A 60 > $W/submit-$1.out 2>/dev/null
python3 - $W/submit-$1.out <<'PY'
import sys,collections
agg=collections.defaultdict(list)
for l in open(sys.argv[1]):
    if not l.startswith('FB_FRAME ') or 'phase=steady' not in l: continue
    d=dict(t.split('=',1) for t in l.split()[1:] if '=' in t)
    agg[(d['scn'],d['mode'])].append((float(d['exec_us']),int(d['kjobs']),int(d['draws'])))
rows=[]
for k,v in sorted(agg.items()):
    v=sorted(v)[:len(v)//2+1] # lower half: less noise
    e=sum(x[0] for x in v)/len(v);j=v[0][1];dr=v[0][2];rows.append((e,j,dr))
    print('%-11s %-8s exec_us=%8.1f jobs=%4d draws=%4d'%(k[0],k[1],e,j,dr))
import itertools
sjj=sum(j*j for e,j,d in rows);sjd=sum(j*d for e,j,d in rows);sdd=sum(d*d for e,j,d in rows)
sej=sum(e*j for e,j,d in rows);sed=sum(e*d for e,j,d in rows);det=sjj*sdd-sjd*sjd
a=(sej*sdd-sed*sjd)/det;b=(sed*sjj-sej*sjd)/det
print('FIT exec_us = %.2f us/job + %.2f us/draw'%(a,b))
PY
