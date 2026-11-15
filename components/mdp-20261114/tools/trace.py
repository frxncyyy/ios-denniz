import sys
src=open(sys.argv[0].replace('trace.py','run_cal.py')).read().split('CALR, CALW')[0]
exec(src)
from unicorn import UC_HOOK_CODE
seen=[]
h=mu.hook_add(UC_HOOK_CODE, lambda uc,a,s,u: seen.append(a), begin=0x215d34, end=0x2164f4)
call(0x215d34,[1,0,0,0x4c084c,255,0])
# print contiguous ranges
rng=[]; st=seen[0]; pv=seen[0]
for a in seen[1:]:
    if a!=pv+4: rng.append((st,pv)); st=a
    pv=a
rng.append((st,pv))
print(' '.join('%x-%x'%r for r in rng))
