import re,sys,collections
def load(p): return open(p,errors='replace').read().split('\n')
def sysan(lines,name):
    rows=[]
    for i,l in enumerate(lines):
        m=re.search(r'NORD2 SYS: win_ms=(\d+) cpu_busy%=([\d/]+) \(avg (\d+), big (\d+)\)',l)
        if m: rows.append((i,int(m.group(1)),[int(x) for x in m.group(2).split('/')]))
    tot=sum(r[1] for r in rows)
    print(f'== {name}: SYS windows {len(rows)} total {tot/1000:.0f}s')
    per=[sum(r[1]*r[2][c] for r in rows)/tot for c in range(8)]
    print('  per-core busy% (time-weighted):',' '.join(f'{x:.1f}' for x in per),' avg',f'{sum(per)/8:.1f}','LL(0-3)',f'{sum(per[:4])/4:.1f}','big(4-7)',f'{sum(per[4:])/4:.1f}')
    print('  cpu-seconds per second (cores busy):',f'{sum(per)/100:.2f}')
    return rows
def dvfs(lines,name):
    rows=[]
    for i,l in enumerate(lines):
        m=re.search(r'DVFS DECISION cpuL=(\d+)MHz cpuB=(\d+)MHz gpu=(\d+)MHz util L=(\d+) B=(\d+) G=(\d+) \| now L=(\d+) B=(\d+) G=(\d+) \| mode cpu=(\d+) gpu=(\d+) class=(\w+) why L=(\w+) B=(\w+) G=(\w+) \| therm=(\d+) bat=([\d.]+)C cap L=(\d+) B=(\d+) G=(\d+)',l)
        if m: rows.append((i,)+m.groups())
    print(f'== {name}: DVFS decisions {len(rows)} (~{len(rows)*2}s)')
    for idx,lab in ((1,'cpuL'),(2,'cpuB'),(3,'gpu'),(12,'class'),(13,'whyL'),(14,'whyB'),(15,'whyG'),(16,'therm')):
        c=collections.Counter(r[idx] for r in rows)
        print('  ',lab,' '.join(f'{k}:{v*100//len(rows)}%' for k,v in sorted(c.items(),key=lambda kv:-kv[1])[:12]))
    # idle-class clocks
    c=collections.Counter((r[1],r[2]) for r in rows if r[12]=='idle')
    print('   idle-class (L,B) clocks:',c.most_common(8))
    u=collections.Counter(int(r[4])//100*100 for r in rows); print('   utilL hist',sorted(u.items()))
    u=collections.Counter(int(r[5])//100*100 for r in rows); print('   utilB hist',sorted(u.items()))
    bats=[float(r[17]) for r in rows]
    print('   bat first/min/max/last',bats[0],min(bats),max(bats),bats[-1])
    # temperature timeline every ~60s
    print('   bat timeline (every 30 decisions):',' '.join(f'{bats[i]:.1f}' for i in range(0,len(bats),30)))
    return rows
for p,n in ((sys.argv[1],'heat1'),(sys.argv[2],'hf1_cap')):
    L=load(p); sysan(L,n); dvfs(L,n)
