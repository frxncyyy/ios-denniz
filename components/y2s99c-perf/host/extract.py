import re,sys
s=open(sys.argv[1]).read()
a=s.index('/* y2s99c: the parked big buffers'); b=s.index('/* Every pooled buffer back to the kernel')
d0=s.index('#define NP_BIG_SLOTS'); d1=s.index('static int big_off = -1;')
open(sys.argv[2],'w').write(s[d0:d1]+'static int big_off = 0;\n'+s[a:b])
