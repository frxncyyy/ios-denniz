import sys
for l in open(sys.argv[1]):
    p=l.split()
    if len(p)==5 and p[1]=='EV':
        t=int(p[2])/13e3; off=int(p[3],16); v=p[4]
        print(f"{t:9.3f} ms  {'POWER' if off==0xffff else hex(off):>6}  {v}")
