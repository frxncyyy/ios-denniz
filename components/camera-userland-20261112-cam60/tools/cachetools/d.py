import sys,pickle,bisect,re,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0,'$NORD2_WORK/gpu/build/ui-corpus-20261025/compositor-gate-20261025')
import dsc_dis as dd, dsc_qc
import dsc2 as dsc
dd._c=dsc_qc.Cache()
SC=os.environ.get('RWCAM_WORK','.')
M=pickle.load(open(SC+'/cmcap.pkl','rb'))
addrs=[a for a,n in M]
def find(pat):
    return [(a,n) for a,n in M if re.search(pat,n)]
def bounds(name):
    for i,(a,n) in enumerate(M):
        if n==name: return a,M[i+1][0]
    raise KeyError(name)
import struct
import dsc2 as dsc
P=lambda v: v & 0xFFFFFFFFFF
def q(a):
    b=dsc.rd(a,8); return struct.unpack('<Q',b)[0] if len(b)==8 else 0
OBJC_STUBS=(0x1a24259a0,0x1a24259a0+0x30660)
STUBS=(0x1a21e04fc,0x1a21e04fc+0x3114)
_ocache={}
def objc_stub_sel(a):
    if a in _ocache: return _ocache[a]
    base=a
    w=struct.unpack('<5I',dsc.rd(base,20))
    # adrp x1; ldr x1,[x1,#off]
    def adrp(i,pc):
        immlo=(i>>29)&3; immhi=(i>>5)&0x7ffff; imm=(immhi<<2|immlo); 
        if imm&0x100000: imm-=0x200000
        return (pc&~0xfff)+(imm<<12)
    pg=adrp(w[0],base); off=((w[1]>>10)&0xfff)<<3
    ref=P(q(pg+off)); n=dsc.cs(ref)
    _ocache[a]=n; return n
def stub_target(a):
    w=struct.unpack('<3I',dsc.rd(a,12))
    def adrp(i,pc):
        immlo=(i>>29)&3; immhi=(i>>5)&0x7ffff; imm=(immhi<<2|immlo)
        if imm&0x100000: imm-=0x200000
        return (pc&~0xfff)+(imm<<12)
    pg=adrp(w[0],a); off=((w[1]>>10)&0xfff)<<3
    return P(q(pg+off))

try:
    import sym as _sym
except Exception: _sym=None
import allsyms
def _adrp(i,pc):
    immlo=(i>>29)&3; immhi=(i>>5)&0x7ffff; imm=(immhi<<2|immlo)
    if imm&0x100000: imm-=0x200000
    return (pc&~0xfff)+(imm<<12)
def follow(t):
    for _ in range(3):
        b=dsc.rd(t,12)
        if len(b)<12: return t
        w=struct.unpack('<3I',b)
        if (w[0]&0x9f00001f)==0x90000010 and (w[2]==0xd61f0200):
            pg=_adrp(w[0],t)
            if (w[1]&0xffc003ff)==0x91000210:   # add x16,x16,#imm
                t=pg+((w[1]>>10)&0xfff); continue
            if (w[1]&0xffc003ff)==0xf9400210:   # ldr x16,[x16,#imm]
                t=P(q(pg+(((w[1]>>10)&0xfff)<<3))); continue
        break
    return t
def name_target(t):
    if OBJC_STUBS[0]<=t<OBJC_STUBS[1]:
        return 'msg$'+str(objc_stub_sel(t))
    t2=follow(t)
    return allsyms.name(t2)
CFS=(0x1d7ba8438,0x1d7ba8438+0x2a2e0)
def cfstr(a):
    if CFS[0]<=a<CFS[1]:
        p=P(q(a+16)); ln=q(a+24)
        return dsc.cs(p)
def clean(t):
    out=[]
    pend={}
    for l in t.splitlines():
        ma=re.match(r'\s*([0-9a-f]+):\s+adrp\s+(x\d+), 0x([0-9a-f]+)',l)
        if ma: pend[ma[2]]=int(ma[3],16)
        ml=re.match(r'\s*([0-9a-f]+):\s+ldr\s+(x\d+), \[(x\d+), #0x([0-9a-f]+)\]',l)
        if ml and ml[3] in pend:
            slot=pend[ml[3]]+int(ml[4],16)
            try:
                v=P(q(slot)); nm=allsyms.name(v)
                if '+' not in nm and nm!='?': l+='    ; =%s'%nm
                else:
                    c=cfstr(v)
                    if c: l+='    ; =@"%s"'%c[:60]
            except Exception: pass

        l=re.sub(r'<_binary[^>]*>','',l)
        m=re.match(r'\s*([0-9a-f]+):\s+(bl|b)\s+0x([0-9a-f]+)',l)
        if m:
            tg=int(m[3],16)
            try: l+='    ; '+name_target(tg)
            except Exception as e: l+='    ; ?'
        m2=re.search(r'; @0x([0-9a-f]+)$',l)
        if m2:
            try:
                c=cfstr(int(m2[1],16))
                if c is not None: l=l[:m2.start()]+'; @"%s"'%c[:80]
            except Exception: pass
        out.append(l)
    return '\n'.join(out)
def dis(name_or_addr,end=None,out=os.environ.get('RWCAM_WORK','.')+'/dd'):
    if isinstance(name_or_addr,str):
        a,e=bounds(name_or_addr)
    else: a,e=name_or_addr,end
    t=dd.dis(a,e,out=out)
    t=dd.annotate(t)
    return clean(t)
def sym(addr):
    i=bisect.bisect_right(addrs,addr)-1
    return '%s+0x%x'%(M[i][1],addr-M[i][0])
if __name__=='__main__':
    for x in sys.argv[1:]:
        print(dis(x))

def fn(x):
    if isinstance(x,str) and x.startswith('0x'): x=int(x,16)
    if isinstance(x,str):
        a,e=bounds(x)
    else:
        a=x
        i=bisect.bisect_right(allsyms.K,a)
        e=allsyms.K[i]
        j=bisect.bisect_right(addrs,a)
        if j<len(addrs): e=min(e,addrs[j])
    t=dis(a,e)
    return '\n'.join(l.split('\t',1)[0].strip()+'\t'+l.split('\t',1)[1] if '\t' in l else l for l in t.splitlines() if l.strip() and not l.startswith('/') and 'Disassembly' not in l)
