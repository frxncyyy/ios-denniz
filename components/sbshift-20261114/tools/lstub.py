import sys,struct,macho
maps=macho.load('../loc/bin/locationd')
from dsc import read,cstr
from xref import adrp_target
def stubsel(a):
    i0,i1=struct.unpack('<II',read(a,8)); r=adrp_target(i0,a)
    if not r: return None
    slot=r[0]+((i1>>10)&0xfff)*8
    v=struct.unpack('<Q',read(slot,8))[0]; return cstr((v&0xfffffffff)+0x100000000)
if __name__=='__main__':
    for s in sys.argv[1:]: print(s, stubsel(int(s,16)))
