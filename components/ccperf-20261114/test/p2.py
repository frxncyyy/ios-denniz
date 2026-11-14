import re,sys
txt=open(sys.argv[1],errors='replace').read()
L=txt.split('\n')
ev=[]
def add(i,k,d): ev.append((i,k,d))
for i,l in enumerate(L,1):
    if 'NORD2' not in l and 'SWEET HID' not in l and '2026-11-13' not in l: continue
    m=re.search(r'2026-11-13 (\d\d:\d\d:\d\d)',l)
    if m: add(i,'T',m.group(1))
    for m in re.finditer(r'SWEET HID: touch 0 (down|up) at (\d+),(\d+)( edge)?',l): add(i,'H',(m.group(1),int(m.group(2)),int(m.group(3)),bool(m.group(4))))
    for m in re.finditer(r'NORD2 FB CLIENT: win_ms=(\d+) (.*?)\| other',l):
        d={}
        for p in re.finditer(r'(\S*)\[(\d+)\] shown=(\d+) fresh=(\d+)',m.group(2)): d[p.group(1) or '?']=(int(p.group(3)),int(p.group(4)))
        add(i,'CL',(int(m.group(1)),d))
    for m in re.finditer(r'NORD2 FB PACE: .*?pushes=(\d+) active=(\d+) idle_gaps=(\d+) active_fps=([\d.]+) k1_permille=(\d+) refresh_per_present_1/2/3/4/5\+=(\d+)/(\d+)/(\d+)/(\d+)/(\d+) dropped=(\d+) \(ready (\d+) nosw (\d+)\) best_run=(\d+)',l):
        add(i,'PA',tuple(float(x) if '.' in x else int(x) for x in m.groups()))
    for m in re.finditer(r'NORD2 FB SEEN: presents=(\d+) .*?gaps_ms<=20/34/67/100/more=(\d+)/(\d+)/(\d+)/(\d+)/(\d+) max_gap_ms=(\d+)',l): add(i,'SE',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 FB ARRIVAL: swap_gaps_ms<=8/20/34/67/more=(\d+)/(\d+)/(\d+)/(\d+)/(\d+) arrival_to_copy_ms<=8/16/25/33/more=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)',l): add(i,'AR',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 DVFS DECISION cpuL=(\d+)MHz cpuB=(\d+)MHz gpu=(\d+)MHz util L=(-?\d+) B=(-?\d+) G=(\d+) \| now L=(\d+) B=(\d+) G=(\d+) \| mode cpu=(\d) gpu=(\d+) class=(\w+) why L=(\w+) B=(\w+) G=(\w+) \| therm=(\d) bat=([\d.]+)C cap L=(\d+) B=(\d+) G=(\d+) \| 2s: chg=(\d+) cls=(\d+)/(\d+)/(\d+)/(\d+)/(\d+) late=(\d+)',l):
        add(i,'DV',m.groups())
    for m in re.finditer(r'NORD2 GDVFS: mode=(\d) cur=(\d+) want=(\d+) cap=(\d+) floor=(\d+) kHz pll=\d+ mux=\d busy_pm avg=(\d+) max=(\d+) now=(\d+) why=(\w+) therm=(\d) bat_cC=(\d+)',l): add(i,'GD',m.groups())
    for m in re.finditer(r'NORD2 GPU JOBS: n=(\d+) fault=(\d+) full=(\d+) win_ms=(\d+) q_us=(\d+)/(\d+) run_us=(\d+)/(\d+) tot_us=(\d+)/(\d+)',l): add(i,'GJ',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 GPU LAT: n=(\d+) win_ms=(\d+) irq_us=(\d+)/(\d+) hand_us=(\d+)/(\d+) phase_us=\S+ depth=(\d+)/(\d+) busy_pm=(\d+) kinds=c(\d+)/r(\d+)/f(\d+)',l): add(i,'GL',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 SYS: win_ms=(\d+) cpu_busy%=([\d/]+) \(avg (\d+), big (\d+)\) \| vm pageins=(\d+) pageouts=(\d+) faults=(\d+) zerofill=(\d+) cow=(\d+) decomp=(\d+)',l): add(i,'SY',m.groups())
    for m in re.finditer(r'NORD2 MEM SURF: wired=(\d+) MiB bufs=(\d+) .*?iommu=(\d+) MiB .*?gpu_private=(\d+) MiB combined=(\d+)/(\d+) MiB avail=(\d+)',l): add(i,'MS',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 MEM: free=(\d+) wired=(\d+) active=(\d+) inactive=(\d+) spec=(\d+) throttled=(\d+) pageable_int=(\d+) pageable_ext=(\d+) avail=(\d+) compressed=(\d+)',l): add(i,'ME',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 PRFCNT A win_us=(\d+) took_us=\d+ busy=\d+ cores=\d+ gpu=(\d+) js0=(\d+) js1=(\d+) til=(\d+)',l): add(i,'PA_',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 PRFCNT B l2look=(\d+) rlook=(\d+) wlook=(\d+) rbeat=(\d+) wbeat=(\d+) arstall=(\d+) wstall=(\d+)',l): add(i,'PB_',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 PRFCNT D frag=(\d+) exec=(\d+) fpk=(\d+) quads=(\d+) warps=(\d+) ptiles=(\d+) fma=(\d+)',l): add(i,'PD_',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 PRFCNT E texflit=(\d+) texdfch=(\d+) texstall=(\d+) texstarve=(\d+) rdtex=(\d+) wrtib=(\d+) rdftc=(\d+)',l): add(i,'PE_',tuple(map(int,m.groups())))
    for m in re.finditer(r'NORD2 PRFCNT C lat0_127=(\d+) lat128_191=(\d+) lat192_255=(\d+) lat256_319=(\d+) lat320_383=(\d+)',l): add(i,'PC_',tuple(map(int,m.groups())))
import pickle; pickle.dump(ev,open(sys.argv[2],'wb'))
from collections import Counter
print(Counter(k for _,k,_ in ev))
