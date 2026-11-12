import struct,pickle,os,bisect
import dsc2,exports
import os
WORK=os.environ.get('RWCAM_WORK','.')
SB=WORK+'/lz/sym.bin'
PK=WORK+'/allsyms.pkl'
def build():
    f=open(SB,'rb'); f.seek(16384)
    d=f.read(590909901)
    no,nc,so,ss,eo,ec=struct.unpack('<6I',d[:24])
    syms={}
    # exports
    ex=exports.build_cache()
    for p,(a,L) in ex.items():
        for addr,n in L:
            nm=dsc2.rd(exports.NAMES+(n&0x0fffffff),120).split(b'\0')[0].decode('latin1')
            syms[addr]=nm
    # locals
    for i in range(ec):
        off,st,cnt=struct.unpack('<QII',d[eo+i*16:eo+i*16+16])
        for k in range(st,st+cnt):
            strx,typ,sect,desc,val=struct.unpack('<IBBHQ',d[no+k*16:no+k*16+16])
            if typ & 0xe0: continue
            if (typ&0xe)!=0xe: continue
            nm=d[so+strx:so+strx+120].split(b'\0')[0].decode('latin1')
            syms.setdefault(val,nm)
    keys=sorted(syms)
    return keys,[syms[k] for k in keys]
if os.path.exists(PK): K,N=pickle.load(open(PK,'rb'))
else:
    K,N=build(); pickle.dump((K,N),open(PK,'wb'))
def name(addr):
    i=bisect.bisect_right(K,addr)-1
    if i<0: return '?'
    return '%s+0x%x'%(N[i],addr-K[i]) if addr!=K[i] else N[i]
if __name__=='__main__':
    import sys
    print(len(K))
    for x in sys.argv[1:]: print(x,name(int(x,16)))
