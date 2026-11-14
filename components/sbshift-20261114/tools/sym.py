import sys,re
from img import which
import os
SL=int(os.environ.get("SL","0xfb2c000"),16)
def sym(a,mh):
    if mh and mh<=a<mh+0x2000000: return 'locationd+0x%x (0x%x)'%(a-mh, a-mh+0x100000000)
    x=a-SL
    if 0x180000000<=x<0x300000000:
        n,o,b=which(x); return '%s+0x%x [%x]'%(n,o,x)
    return hex(a)
mh=int(sys.argv[1],16)
for line in sys.stdin:
    if ' bt ' in line or ' pc ' in line:
        addrs=re.findall(r'0x[0-9a-f]+',line.split(' bt ')[1] if ' bt ' in line else ' '.join(line.split()[4:8:2]))
        print(line.split(' bt ')[0].split(' pc ')[0].strip(), ' <- '.join(sym(int(a,16),mh) for a in addrs))
