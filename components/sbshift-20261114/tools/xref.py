import re,struct,sys
from dsc import *
def adrp_target(ins,pc):
    if ins & 0x9f000000 != 0x90000000: return None
    immlo=(ins>>29)&3; immhi=(ins>>5)&0x7ffff; imm=(immhi<<2)|immlo
    if imm & (1<<20): imm-= (1<<21)
    return (pc & ~0xfff) + (imm<<12), ins & 31
def xrefs(target, onlyfile=None, kinds=('add','ldr')):
    lo=target&0xfff; out=[]
    pats=[]
    # add Xd,Xn,#lo
    b1=bytes([((lo&0x3f)<<2)|r for r in range(4)])
    pats.append(('add',re.compile(b'(?s).['+re.escape(b1)+b']'+re.escape(bytes([lo>>6]))+b'\x91')))
    if lo%8==0:  # ldr Xt,[Xn,#lo]  0xF9400000 | (lo/8)<<10
        i=lo//8; b1=bytes([((i&0x3f)<<2)|r for r in range(4)])
        pats.append(('ldr',re.compile(b'(?s).['+re.escape(b1)+b']'+re.escape(bytes([0x40|(i>>6)]))+b'\xf9')))
    for b,sz,mm,o,n in maps:
        if onlyfile and n!=onlyfile: continue
        data=mm[o:o+sz]
        for k,p in pats:
            for m in p.finditer(data):
                off=m.start()
                if off%4: continue
                ins=struct.unpack_from('<I',data,off)[0]; rn=(ins>>5)&31
                for back in range(1,12):
                    if off-4*back<0: break
                    pi=struct.unpack_from('<I',data,off-4*back)[0]
                    r=adrp_target(pi,b+off-4*back)
                    if r and r[1]==rn:
                        if r[0]==target&~0xfff: out.append((b+off,k,n))
                        break
    return out
if __name__=='__main__':
    s=sys.argv[1]
    for a in findstr(s):
        print('str',hex(a),v2f(a)[2])
        for x in xrefs(a, sys.argv[2] if len(sys.argv)>2 else None): print('  xref',hex(x[0]),x[1],x[2])
