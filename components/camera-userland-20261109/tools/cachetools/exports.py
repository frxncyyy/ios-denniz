import dsc2,struct,pickle,os
M=dsc2.M
pa,ps=struct.unpack('<QQ',M[0x98:0xa8])
f=struct.unpack('<'+'Q'*16,dsc2.rd(pa,128))
IMG,ICNT,EXP,ECNT,NAMES=f[1],f[2],f[3],f[4],f[11]
imgs=dsc2.rd(IMG,ICNT*16)
exps=dsc2.rd(EXP,ECNT*8)
import os
CACHE=os.environ.get('RWCAM_WORK','.')+'/exports.pkl'
def build():
    out={}
    for i,(a,p) in enumerate(dsc2.images):
        cs,cc,es,ec=struct.unpack('<IIII',imgs[i*16:i*16+16])
        L=[]
        for k in range(es,es+ec):
            off,no=struct.unpack('<II',exps[k*8:k*8+8])
            L.append((a+off, no))
        out[p]=(a,L)
    return out
def names(imgpath):
    a,L=build_cache()[imgpath]
    r=[]
    for addr,no in L:
        s=dsc2.rd(NAMES+(no&0x0fffffff),120).split(b'\0')[0].decode('latin1')
        r.append((addr,s,no>>28))
    return r
_c=None
def build_cache():
    global _c
    if _c is None: _c=build()
    return _c
if __name__=='__main__':
    import sys
    r=names('/System/Library/PrivateFrameworks/CMCapture.framework/CMCapture')
    print(len(r))
    for x in r[:15]: print(hex(x[0]),x[1],x[2])
