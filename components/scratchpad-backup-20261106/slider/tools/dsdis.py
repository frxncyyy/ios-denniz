import sys; sys.path.insert(0, sys.argv[0].rsplit('/',1)[0]); sys.path.insert(0, sys.argv[0].rsplit('/',2)[0]+'/pylib')
from dsc import *
import capstone, struct
md=capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM); md.detail=False
def printable(a):
    try:
        s=cstr(a)
        if 2<=len(s)<200 and all(32<=ord(c)<127 for c in s): return s
    except Exception: pass
    return None
def annotate(a):
    s=printable(a)
    if s: return '"%s"'%s
    try:
        q=struct.unpack('<Q',read(a,8))[0]
        t=q & 0xfffffffff
        if t < 0x180000000: t += 0x180000000
        s=printable(t)
        if s: return '[-> "%s"]'%s
        # CFString: isa,flags,ptr,len
        p=struct.unpack('<Q',read(a+16,8))[0] & 0xfffffffff
        if p and p < 0x180000000: p += 0x180000000
        s=printable(p)
        if s: return '@"%s"'%s
    except Exception: pass
    return ''
_stub={}
def stubname(t):
    if t in _stub: return _stub[t]
    r=''
    try:
        w=struct.unpack('<II',read(t,8))
        x,y=w
        if (x & 0x9f00001f)==0x90000001 and (y & 0xffc003ff)==0xf9400021:
            imm=((x>>29)&3)|(((x>>5)&0x7ffff)<<2)
            if imm&(1<<20): imm-=1<<21
            a=(t&~0xfff)+(imm<<12)+((y>>10)&0xfff)*8
            r=annotate(a)
        elif (x & 0x9f00001f)==0x90000010:
            r='(stub/GOT)'
    except Exception as e: r=''
    _stub[t]=r; return r
def dis(start, n):
    code=read(start, n)
    regs={}
    for ins in md.disasm(code, start):
        note=''
        if ins.mnemonic=='adrp':
            r=ins.op_str.split(',')[0]; regs[r]=int(ins.op_str.split('#')[1],16)
        elif ins.mnemonic in ('add','ldr') and '#' in ins.op_str:
            ops=[x.strip() for x in ins.op_str.replace('[','').replace(']','').split(',')]
            src=ops[1]
            if src in regs:
                try: imm=int(ops[-1].lstrip('#'),16)
                except: imm=None
                if imm is not None:
                    a=regs[src]+imm; note='; %x %s'%(a, annotate(a))
        if ins.mnemonic=='bl':
            note='; '+stubname(int(ins.op_str.lstrip('#'),16))
        print('%x: %-8s %s %s'%(ins.address, ins.mnemonic, ins.op_str, note))
if __name__=='__main__':
    dis(int(sys.argv[1],16), int(sys.argv[2],16))
