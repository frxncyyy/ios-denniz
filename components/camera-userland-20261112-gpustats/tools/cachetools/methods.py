"""ObjC method table of one image of the cache (-[Class sel] / +[Class sel] -> IMP), as methods(base) -> sorted [(imp, name)]."""
import struct, os, pickle, sys
import dsc2 as dsc
P=lambda v: (v & 0xFFFFFFFFFF)
def q(a):
    b=dsc.rd(a,8); return struct.unpack('<Q',b)[0] if len(b)==8 else 0
_meth={}
SELBASE=0x18229979f
def methods(base):
    if base in _meth: return _meth[base]
    out=[]
    S=dsc.segs(base)
    secs={sn:(sa,ss) for _,_,_,ss_ in S for sn,sa,ss in ss_ if sn.startswith('__objc')}
    def ml(mlp,cname,meta):
        if not mlp: return
        h=dsc.rd(mlp,8)
        if len(h)<8: return
        ent,cnt=struct.unpack('<II',h); es=ent&0xfffc
        if cnt>5000: return
        for i in range(cnt):
            a=mlp+8+i*es
            if ent&0x80000000:
                n,t,imp=struct.unpack('<iii',dsc.rd(a,12))
                sel=dsc.cs(SELBASE+n) if ent&0x40000000 else dsc.cs(P(q(a+n)))
                sel=sel or '?'
                out.append((a+8+imp,('+' if meta else '-')+'[%s %s]'%(cname,sel)))
            else:
                n,t,imp=struct.unpack('<QQQ',dsc.rd(a,24)); out.append((P(imp),('+' if meta else '-')+'[%s %s]'%(cname,dsc.cs(P(n)))))
    for sec,iscat in (('__objc_classlist',0),('__objc_catlist',1)):
        if sec not in secs: continue
        sa,ss=secs[sec]
        for k in range(ss//8):
            c=P(q(sa+k*8))
            if not iscat:
                for meta in (0,1):
                    cc=c if not meta else P(q(c))
                    ro=P(q(cc+32))&~7
                    name=dsc.cs(P(q(ro+24))) or '?'
                    ml(P(q(ro+32)),name,meta)
            else:
                name=dsc.cs(P(q(c))) or '?'
                cls=P(q(c+8)); cn=None
                if cls:
                    ro=P(q(cls+32))&~7; cn=dsc.cs(P(q(ro+24)))
                ml(P(q(c+16)),'%s(%s)'%(cn or '?',name),0); ml(P(q(c+24)),'%s(%s)'%(cn or '?',name),1)
    out.sort(); _meth[base]=out; return out

if __name__=='__main__':
    # usage: methods.py <out cmcap.pkl>   (CMCapture's table; its base is looked up by path)
    base=[a for a,p in dsc.images if p.endswith('/CMCapture.framework/CMCapture')][0]
    m=methods(base)
    pickle.dump(m,open(sys.argv[1],'wb'))
    print(len(m),'methods of CMCapture at',hex(base))
