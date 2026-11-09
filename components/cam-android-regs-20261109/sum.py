import sys
for l in open(sys.argv[1]):
    l = l[l.find('N2D '):] if 'N2D ' in l else l
    p=l.split()
    if len(p)>2 and p[1] in ('MARK','GUARD','end'): print('##',' '.join(p[1:])); continue
    if len(p)==6 and p[1].endswith(':') and any(x!='00000000' for x in p[2:]): print(p[1][3:],' '.join(p[2:]))
