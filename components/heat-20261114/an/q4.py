import re,sys,collections
for p in sys.argv[1:]:
    L=open(p,errors='replace').read().split('\n')
    print('==',p.split('/')[-1])
    # display state by DVFS ticks
    on=None;ticks=collections.Counter();gb=[];gk=collections.Counter()
    fbc=[];pace=[]
    scan=0;at=0;aterr=0;pcm=[];scp=[]
    for l in L:
        if 'SWEET FB: display on' in l: on=True
        elif 'SWEET FB: display off' in l: on=False
        if 'DVFS DECISION' in l: ticks[on]+=1
        m=re.search(r'GDVFS: mode=\d+ cur=(\d+).*busy_pm avg=(\d+) max=(\d+)',l)
        if m: gk[(on,m.group(1))]+=1; gb.append((on,int(m.group(2)),int(m.group(3))))
        m=re.search(r'FB CLIENT: win_ms=(\d+) (.*)',l)
        if m: fbc.append((int(m.group(1)),m.group(2)[:110]))
        m=re.search(r'FB PACE: panel_hz=([\d.]+).*?pushes=(\d+) active=(\d+) idle_gaps=(\d+) active_fps=([\d.]+)',l)
        if m: pace.append(m.groups())
        if 'SCANADMIT' in l: scan+=1
        if 'NORD2 AT!' in l: at+=1
        m=re.search(r'AUDIO PCM: opens=(\d+) writes=(\d+) queued=(\d+) peak=\d+ running=(\d+)',l)
        if m: pcm.append(m.groups())
        m=re.search(r'NORD2 SCP: t=(\d+)s records (\d+) msgs (\d+)',l)
        if m: scp.append(tuple(map(int,m.groups())))
    print(' DVFS ticks by display state (None=unknown before first event):',dict(ticks))
    print(' GPU clock by display:',dict(gk))
    for st in (None,True,False):
        x=[b for b in gb if b[0]==st]
        if x: print(f'  display={st}: GPU busy_pm avg mean {sum(b[1] for b in x)/len(x):.0f} max-of-max {max(b[2] for b in x)} windows {len(x)} zero-windows {sum(1 for b in x if b[1]==0)}')
    tot=sum(w for w,_ in fbc)
    print(' FB CLIENT windows',len(fbc),'total s',tot//1000)
    for w,t in fbc[:4]+fbc[-3:]: print('   ',w,t)
    if pace:
        fps=[float(x[4]) for x in pace]; pushes=sum(int(x[1]) for x in pace)
        print(' FB PACE windows',len(pace),'panel_hz',pace[0][0],'pushes total',pushes,'active_fps median',sorted(fps)[len(fps)//2])
    print(' wifi SCANADMIT',scan,'AT! errors',at,'PCM first/last',pcm[:1],pcm[-1:],'SCP first/last',scp[:1],scp[-1:])
    if len(scp)>1:
        dt=scp[-1][0]-scp[0][0]; print(f'  SCP rate: {(scp[-1][1]-scp[0][1])/dt:.0f} records/s, {(scp[-1][2]-scp[0][2])/dt:.0f} msgs/s')
