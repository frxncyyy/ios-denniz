import re,sys
targets={int(a,16):n for a,n in (x.split('=') for x in sys.argv[2:])}
pg={}
rx=re.compile(r'^([0-9a-f]+):\s+(\w+)\s+(.*)$')
for line in open(sys.argv[1]):
    m=rx.match(line)
    if not m: continue
    a,op,args=m.groups()
    if op=='adrp':
        mm=re.match(r'(x\d+), .*; 0x([0-9a-f]+)',args)
        if mm: pg[mm.group(1)]=int(mm.group(2),16)
    elif op=='add':
        mm=re.match(r'(x\d+), (x\d+), #0x([0-9a-f]+)',args)
        if mm and mm.group(2) in pg:
            t=pg[mm.group(2)]+int(mm.group(3),16)
            if t in targets: print(a, targets[t])
