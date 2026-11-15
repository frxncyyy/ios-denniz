import sys, struct
src=open(sys.argv[0].replace('csc.py','run_cal.py')).read().split('CALR, CALW')[0]
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
print('RDMA helper (this, w1, w2=srcfmt, x3..x7, [sp]=0x38,[sp+8]=0x34 en,[sp+16]=0x30 mtx): outs id,swap,b23,b24,b28,0x38,en,mtx')
for fmt in (NV12, 0x4c086c, 0x202042):
    for w1 in range(0,6):
        o=callh(0xc10d8,[THIS,w1,fmt,P,P+4,P+8,P+12,P+16],[P+20,P+24,P+28])
        print(hex(fmt),'w1',w1,o[:8])
