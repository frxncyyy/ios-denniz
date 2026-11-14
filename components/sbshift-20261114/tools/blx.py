import sys,struct,macho
maps=macho.load(sys.argv[1]); T=int(sys.argv[2],16)
for b,sz,mm,o,n in maps:
    if b!=0x100000000: continue
    data=mm[o:o+sz]
    for off in range(0,len(data)-3,4):
        ins=struct.unpack_from('<I',data,off)[0]
        if ins>>26 in (0x25,0x05):
            imm=ins&0x3ffffff
            if imm&(1<<25): imm-=1<<26
            if b+off+imm*4==T: print(hex(b+off), 'bl' if ins>>26==0x25 else 'b')
    # pointer refs
    pat=struct.pack('<Q',T)[:5]
for b,sz,mm,o,n in maps:
    i=mm.find(struct.pack('<Q',T)[:5],o,o+sz)
    while i!=-1:
        print('ptr at',hex(b+i-o)); i=mm.find(struct.pack('<Q',T)[:5],i+1,o+sz)
