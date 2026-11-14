import struct,sys,re
sys.path.insert(0,'.')
import d,dsc2,allsyms
TEXT=(0x1a1ee8ef0,0x1a1ee8ef0+0x2f760c)
code=dsc2.rd(TEXT[0],TEXT[1]-TEXT[0])
words=struct.unpack('<%dI'%(len(code)//4),code)
def find_str(sub):
    a=0x1a23552c8; b=dsc2.rd(a,0x33ccf); out=[]
    for m in re.finditer(rb'[\x20-\x7e]+\x00',b):
        s=m.group()[:-1].decode()
        if sub in s: out.append((a+m.start(),s))
    return out
def find_cf(sub,exact=False):
    out=[]
    base=0x1d7ba8438
    for k in range(0x2a2e0//32):
        a=base+k*32
        p=d.P(d.q(a+16)); t=dsc2.cs(p)
        if t is not None and ((t==sub) if exact else (sub in t)): out.append((a,t))
    return out
def xrefs(targets):
    T=set(targets); res={}
    pend={}
    for i,w in enumerate(words):
        pc=TEXT[0]+i*4
        if (w&0x9f000000)==0x90000000:
            rd=w&31; immlo=(w>>29)&3; immhi=(w>>5)&0x7ffff; imm=immhi<<2|immlo
            if imm&0x100000: imm-=0x200000
            pend[rd]=((pc&~0xfff)+(imm<<12),i)
        elif (w&0xffc00000)==0x91000000:
            rd=w&31; rn=(w>>5)&31
            if rn in pend and i-pend[rn][1]<8:
                t=pend[rn][0]+((w>>10)&0xfff)
                if t in T: res.setdefault(t,[]).append(pc)
    return res
if __name__=='__main__':
    for sub in sys.argv[1:]:
        ss=find_cf(sub,True)
        x=xrefs([a for a,s in ss])
        for a,s in ss:
            for pc in x.get(a,[]): print(s, hex(pc), d.sym(pc))
