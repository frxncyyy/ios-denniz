"""Run calRDMASetting / calWROTSetting from the phone's libdpframework.so (lib64/mt6893) under unicorn (leaf functions)."""
import struct, sys
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import *
LIB = sys.argv[1]
d = open(LIB, 'rb').read()
# ELF64 program headers
phoff = struct.unpack_from('<Q', d, 0x20)[0]; phnum = struct.unpack_from('<H', d, 0x38)[0]
mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
mu.mem_map(0, 0x400000)
for i in range(phnum):
    t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, phoff + i * 56)
    if t == 1 and va + msz < 0x400000:
        mu.mem_write(va, d[off:off + fsz])
mu.mem_map(0x1000000, 0x100000)          # stack + data
# enable FP (CPACR_EL1.FPEN)
mu.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)
RET = 0x10ff000
def call(fn, inp, nout=16):
    IN, OUT = 0x1080000, 0x1081000
    mu.mem_write(IN, struct.pack('<%dI' % len(inp), *[x & 0xffffffff for x in inp]))
    mu.mem_write(OUT, b'\0' * 4 * nout)
    mu.mem_write(RET, b'\x00\x00\x00\x14')   # b . (we stop at RET)
    mu.reg_write(UC_ARM64_REG_X0, IN); mu.reg_write(UC_ARM64_REG_X1, OUT)
    mu.reg_write(UC_ARM64_REG_SP, 0x1070000); mu.reg_write(UC_ARM64_REG_X30, RET)
    mu.emu_start(fn, RET, count=200000)
    return struct.unpack('<%dI' % nout, mu.mem_read(OUT, 4 * nout))
CALR, CALW = 0x215d34, 0x2164f4
RGBA = 0x202042
for fmt, name in ((RGBA, 'RGBA8888?'), (0x202062, 'BGRA8888?'), (0x201841, 'RGB888?')):
    for flag in (0, 1):
        for w in (256, 1024, 512, 64):
            o = call(CALR, [flag, 0, 0, fmt, w - 1, 0])
            print('RDMA %s flag %d w %4d -> %s' % (name, flag, w, ' '.join('%#x' % v for v in o[:10])))
for in10, in14 in ((0, 0), (256, 256), (0x100, 0x40), (4096, 64), (8192, 128), (1920, 1080)):
    o = call(CALW, [0, RGBA, 0, 256, in10, in14], 4)
    print('WROT in10 %d in14 %d tw 256 -> out %s -> MAIN_BUF %#x' % (in10, in14, list(o[:2]), ((o[0] << 16) + (o[1] << 8)) & 0xffff7f00))
