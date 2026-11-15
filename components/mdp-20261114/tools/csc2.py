import sys, struct
src=open(sys.argv[0].replace('csc2.py','run_cal.py')).read().split('CALR, CALW')[0]
exec(src)
THIS=0x1090000; mu.mem_write(THIS, b'\0'*0x3000)
P=0x10a0000
def callh(fn, args, stackptrs):
    mu.mem_write(P, b'\0'*0x100)
    mu.mem_write(THIS, b'\0'*0x3000)
    regs=[UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_X4,UC_ARM64_REG_X5,UC_ARM64_REG_X6,UC_ARM64_REG_X7]
    for r,v in zip(regs,args): mu.reg_write(r, v)
    sp=0x1070000
    for i,p in enumerate(stackptrs): mu.mem_write(sp+8*i, struct.pack('<Q',p))
    mu.reg_write(UC_ARM64_REG_SP, sp); mu.reg_write(UC_ARM64_REG_X30, RET)
    mu.mem_write(RET, b'\x00\x00\x00\x14')
    mu.emu_start(fn, RET, count=200000)
    return [struct.unpack('<i', mu.mem_read(P+4*i,4))[0] for i in range(8)]
NV12=0x4c084c
print('WROT helper outs: [x4]=id [x5]=swap [x6]=en [x7]=mtx')
for fmt in (0x20202022, 0x30202022, 0x30202002, 0x20202021, 0x10202002, 0x10202022, 0x202002):
    for w1,w2 in ((0,0),(1,0),(2,0),(3,0),(0,2),(1,2),(2,2)):
        try:
            o=callh(0xbe8a0,[THIS,w1,w2,fmt,P,P+4,P+8,P+12],[])
            print(hex(fmt),'w1',w1,'w2',w2,o[:4])
        except Exception as e:
            print(hex(fmt),'w1',w1,'w2',w2,'EXC',e); break
