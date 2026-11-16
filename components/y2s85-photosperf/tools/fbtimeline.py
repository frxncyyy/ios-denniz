import re,json,sys
d=open(sys.argv[1],'rb').read().decode('latin-1'); st=[json.loads(l) for l in open(sys.argv[2])]
ev=[]
for m in re.finditer(r'FB PRESENT: n=(\d+) avg_us=(\d+) max_us=(\d+) copy_us=(\d+)/(\d+)',d): ev.append((m.start(),'PRES n=%s avg=%sus copy=%s'%(m[1],m[2],m[4])))
for m in re.finditer(r'FB SEEN: presents=(\d+) different=(\d+)',d): ev.append((m.start(),'SEEN p=%s diff=%s'%(m[1],m[2])))
for m in re.finditer(r'FB SYNC: pushes=(\d+) safe=\d+ .*?src_race=(\d+) busy_after=(\d+)',d): ev.append((m.start(),'SYNC push=%s race=%s'%(m[1],m[2])))
for m in re.finditer(r'FB HW: mode=(\d) .*?hw_frames=(\d+) cpu_frames=(\d+).*?why\[[^\]]*\]=([\d/]+)',d): ev.append((m.start(),'HW m=%s hw=%s cpu=%s why=%s'%(m[1],m[2],m[3],m[4])))
for m in re.finditer(r'SWAPWAIT: level=\d+ calls=(\d+) not_ready=(\d+) blocked=\d+ timeouts=\d+ swaps_dropped=(\d+)',d): ev.append((m.start(),'SWW calls=%s nr=%s drop=%s'%(m[1],m[2],m[3])))
for s in st:
  if s.get('scenario') in('photos',): ev.append((s['log_offset'],'>>> STEP r%d %s t=%.1f'%(s['round'],s['line'],s['start']-1794517532)))
ev.sort()
lo,hi=int(sys.argv[3]),int(sys.argv[4])
for o,e in ev:
  if lo<=o<=hi: print(o,e)
