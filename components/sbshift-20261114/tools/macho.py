# make dsc-style 'maps' from a thin arm64 Mach-O so xref.py/dis.py helpers work on it
import struct, mmap, os, sys
import dsc
def load(path):
    f=open(path,'rb'); mm=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
    magic,ct,cst,ft,ncmds,sz,fl,_=struct.unpack_from('<IiiIIIII',mm,0)
    assert magic==0xfeedfacf
    off=32; maps=[]
    for i in range(ncmds):
        cmd,cs=struct.unpack_from('<II',mm,off)
        if cmd==0x19:
            name=mm[off+8:off+24].rstrip(b'\0').decode(); va,vs,fo,fs=struct.unpack_from('<QQQQ',mm,off+24)
            if fs: maps.append((va,fs,mm,fo,os.path.basename(path)))
        off+=cs
    dsc.maps[:]=maps
    return maps
