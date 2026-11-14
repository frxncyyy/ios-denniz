import re,sys,collections
p=sys.argv[1]; step=int(sys.argv[2]) if len(sys.argv)>2 else 15
L=open(p,errors='replace').read().split('\n')
win=[];cur=collections.Counter();aks=collections.Counter();ex=collections.Counter();t=0
out=[]
bat='';cls='';fresh=''
for l in L:
    m=re.search(r'SWEET AKS: (\S+) sel (\d+)',l)
    if m: aks[m.group(1)]+=1
    m=re.match(r'exec (\S+)',l)
    if m and m.group(1)!='xpcproxy': ex[m.group(1)]+=1
    m=re.search(r'DVFS DECISION cpuL=(\d+)MHz cpuB=(\d+)MHz.*class=(\w+).*bat=([\d.]+)',l)
    if m: bat=m.group(4); cls=m.group(3); clk=m.group(1)+'/'+m.group(2)
    m=re.search(r'NORD2 SYS: win_ms=(\d+) cpu_busy%=([\d/]+)',l)
    if m:
        win.append((int(m.group(1)),[int(x) for x in m.group(2).split('/')]))
        if len(win)==step:
            tot=sum(w[0] for w in win); t+=tot
            per=[sum(w[0]*w[1][c] for w in win)/tot for c in range(8)]
            print(f't+{t/1000:5.0f}s cores '+' '.join(f'{x:3.0f}' for x in per)+f' sum {sum(per)/100:.2f} bat {bat} {cls} {clk} | aks '+' '.join(f'{k}:{v}' for k,v in aks.most_common(4))+' | exec '+' '.join(f'{k}:{v}' for k,v in ex.most_common(5)))
            win=[];aks.clear();ex.clear()
