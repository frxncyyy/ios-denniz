import struct,sys,re
from dsc import *
from xref import xrefs
s=sys.argv[1]; fil=sys.argv[2] if len(sys.argv)>2 else None
for a in findstr(s):
    # find CFString structs pointing at a: 8-byte value a at offset +16
    pat=struct.pack('<Q',a)[:5]
    for b,sz,mm,o,n in maps:
        i=mm.find(pat,o,o+sz)
        while i!=-1:
            cf=b+i-o-16
            fl=struct.unpack_from('<Q',mm,i-8)[0]
            if (i-o)%8: i=mm.find(pat,i+1,o+sz); continue
            if fl&0xffffffff in (0x7c8,0x7d0) or True:
                xs=[x for x in xrefs(cf,fil)]
                if xs: print('str',hex(a),'cfstr',hex(cf),'flags',hex(fl),[ (hex(x[0]),x[1]) for x in xs][:12])
            i=mm.find(pat,i+1,o+sz)
