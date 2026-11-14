import sys,struct,macho
maps=macho.load(sys.argv[1])
from dsc import findstr
sel=sys.argv[2]
for a in findstr(sel):
    pat=struct.pack('<Q',a)[:5]; refs=[]
    for b,sz,mm,o,n in maps:
        i=mm.find(pat,o,o+sz)
        while i!=-1:
            if (i-o)%8==0: refs.append(b+i-o)
            i=mm.find(pat,i+1,o+sz)
    print('sel',hex(a),'refs',[hex(r) for r in refs])
    # relative method entries: int32 nameoff (to selref) at addr e, so e+nameoff in refs
    for b,sz,mm,o,n in maps:
        for off in range(0,sz-12,4):
            no=struct.unpack_from('<i',mm,o+off)[0]
            if b+off+no in refs:
                to,io=struct.unpack_from('<ii',mm,o+off+4)
                imp=b+off+8+io
                if 0x100000000<=imp<0x102000000: print(' rel entry',hex(b+off),'imp',hex(imp))
            # absolute: name ptr == a
        i=mm.find(pat,o,o+sz)
        while i!=-1:
            if (i-o)%8==0:
                impv=struct.unpack_from('<Q',mm,i+16)[0]&0xffffffffff
                print(' abs entry?',hex(b+i-o),'imp',hex(impv))
            i=mm.find(pat,i+1,o+sz)
