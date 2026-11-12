import sys,struct
sys.path.insert(0,'.')
import d,xref
words=xref.words; T0=xref.TEXT[0]
STUBN={}
def stubs_for(sel):
    out=[]
    a=d.OBJC_STUBS[0]
    while a<d.OBJC_STUBS[1]:
        if d.objc_stub_sel(a)==sel: out.append(a)
        a+=32
    return out
def callers(targets):
    T=set(targets); res=[]
    for i,w in enumerate(words):
        if (w&0xfc000000)==0x94000000:
            imm=w&0x3ffffff
            if imm&0x2000000: imm-=0x4000000
            t=T0+i*4+imm*4
            if t in T: res.append(T0+i*4)
    return res
if __name__=='__main__':
    for sel in sys.argv[1:]:
        st=stubs_for(sel); print(sel,[hex(s) for s in st])
        for c in callers(st): print('  ',hex(c),d.sym(c))
