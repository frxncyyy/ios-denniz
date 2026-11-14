import re,sys,collections
for p in sys.argv[1:]:
    L=open(p,errors='replace').read().split('\n')
    tot=collections.Counter();n=0;msgs=collections.Counter();seen=collections.Counter()
    for l in L:
        if 'np cpu ms/2s (msgs received):' in l:
            n+=1
            for name,ms,mg in re.findall(r' ([^\s=]+)=(\d+)\((\d+)\)',l.split('received):')[1]):
                tot[name]+=int(ms); msgs[name]+=int(mg); seen[name]+=1
    s=sum(tot.values())
    print(f'== {p.split("/")[-2:]}: {n} windows (~{n*2}s sampled), listed CPU total {s/1000:.1f}s = {s/1000/(n*2) if n else 0:.2f} cores avg (top-5 per window only)')
    for k,v in tot.most_common(15):
        print(f'   {k:28s} {v/1000:7.1f}s {v*100/s:5.1f}%  in {seen[k]:3d} windows, avg {v/seen[k]:5.0f} ms/2s when listed, msgs {msgs[k]}')
