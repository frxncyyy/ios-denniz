import sys
sys.path.insert(0,'$NORD2_WORK/gpu/build/sbshift-20261114/tools')
import sys,subprocess,struct,re,tempfile,os
from dsc import *
from xref import adrp_target
start=int(sys.argv[1],16); n=int(sys.argv[2],0)
buf=read(start,n)
tf=tempfile.mktemp(suffix='.bin',dir=os.path.dirname(os.path.abspath(__file__))); open(tf,'wb').write(buf)
ef=tf+'.o'
subprocess.run(['llvm-objcopy','-I','binary','-O','elf64-littleaarch64','--rename-section','.data=.text,code',tf,ef],check=True)
out=subprocess.run(['llvm-objdump','-d','--no-show-raw-insn','--triple=aarch64',ef],capture_output=True,text=True).stdout
regs={}
def ann(t):
    s=cstr(t)
    if s and len(s)>1 and all(32<=ord(c)<127 for c in s[:40]): return repr(s[:80])
    q=read(t,8)
    if q:
        p=struct.unpack('<Q',q)[0]
        s2=cstr(p & 0xfffffffff)
        if s2 and len(s2)>1 and all(32<=ord(c)<127 for c in s2[:40]): return '*-> '+repr(s2[:80])
        # CFString: isa, flags, ptr, len
        q2=read(t+16,8)
        if q2:
            s3=cstr(struct.unpack('<Q',q2)[0]&0xfffffffff)
            if s3 and len(s3)>1 and all(32<=ord(c)<127 for c in s3[:40]): return 'CFSTR '+repr(s3[:80])
    return ''
for line in out.splitlines():
    m=re.match(r'\s*([0-9a-f]+):\s+(.*)',line)
    if not m: continue
    off=int(m.group(1),16); pc=start+off; txt=m.group(2)
    ins=struct.unpack_from('<I',buf,off)[0]
    note=''
    r=adrp_target(ins,pc)
    if r: regs[r[1]]=r[0]
    else:
        mm=re.match(r'(add|ldr)\s+x(\d+), x(\d+), #(0x[0-9a-f]+|\d+)',txt) or re.match(r'(ldr)\s+x(\d+), \[x(\d+), #(0x[0-9a-f]+|\d+)\]',txt)
        if mm and int(mm.group(3)) in regs:
            t=regs[int(mm.group(3))]+int(mm.group(4),0)
            note=hex(t)+' '+ann(t)
        md=re.match(r'ldr\s+([dsq])(\d+), \[x(\d+), #(0x[0-9a-f]+|\d+)\]',txt)
        if md and int(md.group(3)) in regs:
            t=regs[int(md.group(3))]+int(md.group(4),0)
            if md.group(1)=='d': note='=%r'%struct.unpack('<d',read(t,8))[0]
            elif md.group(1)=='s': note='=%r'%struct.unpack('<f',read(t,4))[0]
            else: note='=%r'%(struct.unpack('<dd',read(t,16)),)
        bm=re.match(r'(bl|b|cbz|cbnz|tbz|tbnz|b\.\w+)\s+(?:[wx]\d+, )?(?:#0x[0-9a-f]+, )?(0x[0-9a-f]+)',txt)
        if bm:
            tgt=start+int(bm.group(2),0); note='-> '+hex(tgt)
            try:
                i0,i1=struct.unpack('<II',read(tgt,8))
                r0=adrp_target(i0,tgt)
                if r0 and (i1&0xffc00000)==0xf9400000:
                    slot=r0[0]+((i1>>10)&0xfff)*8
                    q=struct.unpack('<Q',read(slot,8))[0]&0xfffffffff
                    nm=cstr(q)
                    if nm and nm.isprintable(): note+=' ['+nm+']'
                    else: note+=' [stub->%x]'%q
            except Exception: pass
            pass
    print('%x: %-40s %s'%(pc,txt.split('<')[0].strip(),note))
os.unlink(tf); os.unlink(ef)
