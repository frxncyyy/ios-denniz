import sys,struct
import macho, dsc
macho.load(sys.argv[1])
from xref import xrefs
from dsc import findstr, maps
for s in sys.argv[2:]:
    for a in findstr(s):
        # direct refs + CFString refs (CFString data ptr low 5 bytes)
        pat=struct.pack('<Q',a)[:5]; cfs=[]
        for b,sz,mm,o,n in maps:
            i=mm.find(pat,o,o+sz)
            while i!=-1:
                if (i-o)%8==0: cfs.append(b+i-o-16)
                i=mm.find(pat,i+1,o+sz)
        print(s,hex(a),'direct',[hex(x[0]) for x in xrefs(a)][:6],'cfstr',[(hex(c),[hex(x[0]) for x in xrefs(c)][:6]) for c in cfs])
