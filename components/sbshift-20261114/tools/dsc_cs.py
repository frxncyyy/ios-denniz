#!/usr/bin/env python3
"""dump the code signature superblob of a dyld subcache; optionally verify page hashes around an offset. usage: dsc_cs.py FILE [fileoff]"""
import struct, sys, hashlib
fn=sys.argv[1]; data=open(fn,'rb')
h=data.read(0x200)
cso,css=struct.unpack_from('<QQ',h,0x28)
print('magic',h[:16],'codeSigOff',hex(cso),'size',hex(css))
data.seek(cso); sb=data.read(css)
magic,length,count=struct.unpack_from('>III',sb,0)
print('superblob %#x len %#x count %d'%(magic,length,count))
for i in range(count):
    typ,off=struct.unpack_from('>II',sb,12+8*i)
    m,l=struct.unpack_from('>II',sb,off)
    print(' slot type %#x off %#x magic %#x len %#x'%(typ,off,m,l))
    if m==0xfade0c02:
        cd=sb[off:off+l]
        (version,flags,hashOffset,identOffset,nSpecial,nCode,codeLimit,hashSize,hashType,platform,pageSize)=struct.unpack_from('>IIIIIIIBBBB',cd,8)
        ident=cd[identOffset:cd.index(b'\0',identOffset)]
        print('  CD ver %#x flags %#x hashOff %#x ident %s nSpecial %d nCode %d codeLimit %#x hashSize %d hashType %d pageSize 2^%d'%(version,flags,hashOffset,ident,nSpecial,nCode,codeLimit,hashSize,hashType,pageSize))
        if version>=0x20400:
            print('  codeLimit64/execSeg fields', cd[0x34:0x58].hex())
        if len(sys.argv)>2:
            fo=int(sys.argv[2],0); ps=1<<pageSize; pg=fo//ps
            data.seek(pg*ps); page=data.read(min(ps,codeLimit-pg*ps))
            want=cd[hashOffset+pg*hashSize:hashOffset+(pg+1)*hashSize]
            got=(hashlib.sha256 if hashType==2 else hashlib.sha1)(page).digest()[:hashSize]
            print('  page',pg,'stored',want.hex(),'computed',got.hex(),'MATCH' if want==got else 'DIFF', 'cd file offset', hex(cso+off+hashOffset+pg*hashSize))
