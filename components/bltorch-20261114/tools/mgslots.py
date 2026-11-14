#!/usr/bin/env python3
# mgslots.py LIBMG CACHEPLIST KEY... : cache slot, valid flag and value of MobileGestalt keys (iOS 16.7 20H392 layout:
# table entry 0xa0 bytes, +0x98 u16 flags, +0x9a u16 slot, +0x9c u32 type; CacheData = 594 x 8-byte values, then 594 valid bytes at 0x1290)
import sys,struct,hashlib,base64,plistlib
f=open(sys.argv[1],'rb').read()
p=plistlib.load(open(sys.argv[2],'rb')); d=p['CacheData']
DC_VM,DC_FO,DC_SZ=0x1d72ffea8,0x74000,0x64000
TX_VM=0x19c555000
def ob(k): return base64.b64encode(hashlib.md5(('MGCopyAnswer'+k).encode()).digest()).decode()[:22]
for k in sys.argv[3:]:
    o=f.find(ob(k).encode()+b'\0')
    if o<0: print(k,'not a key'); continue
    v=TX_VM+o
    hit=None
    for q in range(DC_FO,DC_FO+DC_SZ-8,8):
        w=struct.unpack_from('<Q',f,q)[0]
        if w&0xfffffffff==v and (w>>36)==0x20: hit=q; break
    if hit is None: print(k,'no table entry'); continue
    flags,slot,typ=struct.unpack_from('<HHI',f,hit+0x98)
    fn=struct.unpack_from('<Q',f,hit+8)[0]&0xfffffffff
    if slot==0xffff: print('%-28s slot none flags %#x type %d fn %#x'%(k,flags,typ,fn)); continue
    val=d[slot*8:slot*8+8]; valid=d[0x1290+slot]
    print('%-28s slot %#5x valid %d value %s flags %#x type %d fn %#x'%(k,slot,valid,val.hex(),flags,typ,fn))
