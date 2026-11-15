"""Emulate CPQRszFW::rsz_alg_det / rsz_config / rsz_tbl_select (phone libpq_prot.so) and compare with the C-like port."""
import struct, sys, math, random
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import *
d = open(sys.argv[1], 'rb').read()
phoff = struct.unpack_from('<Q', d, 0x20)[0]; phnum = struct.unpack_from('<H', d, 0x38)[0]
mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM); mu.mem_map(0, 0x200000)
for i in range(phnum):
    t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, phoff + i * 56)
    if t == 1 and va + msz < 0x200000: mu.mem_write(va, d[off:off + fsz])
mu.mem_map(0x1000000, 0x100000); mu.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)
THIS, IN, OUT, RET = 0x1000000, 0x1010000, 0x1020000, 0x10ff000
mu.mem_write(RET, b'\x00\x00\x00\x14')
ALG_DET, CONFIG, TBL = 0xba8c0, 0xbaec4, 0xbb128
def setup(iw, ih, ow, oh):
    mu.mem_write(THIS, b'\0' * 0x2000); mu.mem_write(IN, b'\0' * 0x400); mu.mem_write(OUT, b'\0' * 0x400)
    th = {0x0: 0, 0x4: 9, 0x8: 17, 0xc: 1, 0x20: 1, 0x24: 1, 0x28: 24, 0x18d8: 4, 0x18dc: 4}
    for k, v in th.items(): mu.mem_write(THIS + k, struct.pack('<i', v))
    for k, v in {0x8: ow, 0xc: oh, 0x20: iw, 0x24: ih}.items(): mu.mem_write(IN + k, struct.pack('<I', v))
def run(fn, x0, x1, x2, w3=0, w4=0):
    for r, v in ((UC_ARM64_REG_X0, x0), (UC_ARM64_REG_X1, x1), (UC_ARM64_REG_X2, x2), (UC_ARM64_REG_X3, w3), (UC_ARM64_REG_X4, w4)):
        mu.reg_write(r, v)
    mu.reg_write(UC_ARM64_REG_SP, 0x1080000); mu.reg_write(UC_ARM64_REG_X30, RET)
    mu.emu_start(fn, RET, count=100000)
    return mu.reg_read(UC_ARM64_REG_X0) & 0xffffffff
def rd(a): return struct.unpack('<I', mu.mem_read(a, 4))[0]
def vendor(iw, ih, ow, oh):
    setup(iw, ih, ow, oh)
    run(ALG_DET, THIS, IN, OUT)
    ah, av = rd(OUT + 0x10), rd(OUT + 0x14)
    run(CONFIG, THIS, IN, OUT, ah, 1); run(CONFIG, THIS, IN, OUT, av, 0)
    r = dict(ah=ah, av=av, sh=rd(OUT + 0x28), sv=rd(OUT + 0x2c), th=rd(OUT + 0x68), tv=rd(OUT + 0x6c), ph=rd(OUT + 0x20), pv=rd(OUT + 0x24))
    r['tbh'] = run(TBL, THIS, IN, ah, r['sh']); r['tbv'] = run(TBL, THIS, IN, av, r['sv'])
    return r
def port(iw, ih, ow, oh):
    def alg(i, o, vert):
        if i <= o: return 0
        if o < 3: return 1
        if i > 24 * o: return 1
        if vert and (i - 1) // (o - 1) > 0x1000: return 1
        return 2
    def step(a, i, o):
        if a == 0: return ((i - 1) << 15) // (o - 1), 0, 32768
        if i == o: return 0x100000, 0, 1 << 20
        s = -((-(o - 1) << 20) // (i - 1))      # ceil((o-1)/(i-1) * 2^20)
        t = 0
        if a == 2:
            t = 6
            for k in range(0, 6):
                if i <= o << (6 + k): t = k; break
        return s, t, 1 << 20
    def tbl(a, s): return (7 if s == 0x8000 else 9) if a == 0 else 17
    ah, av = alg(iw, ow, False), alg(ih, oh, True)
    sh, th, ph = step(ah, iw, ow); sv, tv, pv = step(av, ih, oh)
    return dict(ah=ah, av=av, sh=sh, sv=sv, th=th, tv=tv, ph=ph, pv=pv, tbh=tbl(ah, sh), tbv=tbl(av, sv))
random.seed(1); bad = 0; n = 0
cases = [(256, 256, 256, 256), (512, 512, 256, 256), (128, 128, 256, 256), (1024, 64, 768, 48), (512, 256, 256, 128), (1920, 1080, 640, 360)]
cases += [(random.randint(4, 4096), random.randint(4, 4096), random.randint(4, 4096), random.randint(4, 4096)) for _ in range(3000)]
cases = [c for c in cases if c[2] * 8 >= c[0] and c[0] * 8 >= c[2] and c[3] * 8 >= c[1] and c[1] * 8 >= c[3]] + cases[:6]
for c in cases:
    v, p = vendor(*c), port(*c); n += 1
    if v != p:
        bad += 1
        if bad < 6: print('MISMATCH', c, v, p)
for c in cases[:6]: print(c, vendor(*c))
print('rsz cases %d mismatches %d' % (n, bad))
