#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# mnl_emu.py -- host harness for MediaTek's libmnl.so (AArch64 bionic ELF) on an x86_64 host, M3 of the Nord2 GNSS port.
# Runs the real library code under the Unicorn CPU emulator (pip unicorn, AArch64), with:
#   * our own ELF loader: LOAD segments at BASE, ANDROID_RELR relocations, the 300 JUMP_SLOT GOT entries pointed at
#     trap stubs (one per import); every import is implemented here in Python (the "bionic shim" spec, see SHIM below);
#   * the 20-slot callback table of mtk_gps_sys_function_register, recovered from mnld (table at mnld 0x60b80, 17 slots
#     set; libmnl copies 0x00..0x98 into its globals): slot 0x00 = NMEA output (mtk_gps_sys_nmea_output_to_app_print
#     calls it), the others are traced;
#   * a fake /dev/gpsdl0 byte pipe that records what libmnl writes.
# Cooperative threads: pthread_create queues the start routine; a blocking import (read with no data, cond_wait, sleep,
# epoll_pwait) parks the thread; a round-robin scheduler resumes threads by saved CPU context.
# usage: mnl_emu.py LIBMNL.so [--steps N] [--log FILE]      (PYTHONPATH must contain the unicorn package)
# mnl_emu2.py = mnl_emu.py + (1) a VIRTUAL clock: clock_gettime/gettimeofday/time read VT, usleep/sleep/cond_timedwait/
# epoll_pwait park the thread until a VT deadline and the scheduler jumps VT to the next deadline when nothing can run;
# (2) cond_signal/broadcast wake cond waiters; (3) an RX queue for the fake /dev/gpsdl0 (read returns queued bytes) fed
# by a DSP-reply script (--dsp-script FILE.py defining on_tx(seq, data, vt_ns, rx) / on_tick(vt_ns, rx), rx = bytearray);
# (4) every gpsdl write/read logged with VT and a frame-pointer backtrace symbolized from .gnu_debugdata;
# (5) --call-dsp-init: call the (unreferenced) export mtk_gps_dsp_init after mtk_gps_mnl_run to drive _BootDSP_UART_V2.
import struct, sys, os, time, argparse, lzma, json
sys.path.insert(0, os.environ.get('PYDEPS', os.path.expanduser('~/.cache/n2mnld-pydeps')))   # works under python3 -I
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.arm64_const import *

BASE = 0x40000000
TRAP = 0x10000000          # one 4-byte trap per import / callback
HEAP = 0x60000000; HEAP_SZ = 0x4000000
STK = 0x70000000; STK_SZ = 0x100000; NSTK = 16
TLS = 0x7f000000
MAGIC_RET = TRAP + 0xfff0  # return address for top-level calls

ap = argparse.ArgumentParser(); ap.add_argument('lib'); ap.add_argument('--steps', type=int, default=200_000_000); ap.add_argument('--log')
ap.add_argument('--trace-imports', action='store_true'); ap.add_argument('--dsp-script'); ap.add_argument('--vt-limit', type=float, default=20.0)
ap.add_argument('--call-dsp-init', action='store_true'); ap.add_argument('--cfg1-word0', type=int, default=1); ap.add_argument('--chip-x', type=lambda v: int(v, 0), default=0x250); ap.add_argument('--chip-y', type=lambda v: int(v, 0), default=0xffff6893); ap.add_argument('--tx-bin'); ap.add_argument('--tx-json'); ap.add_argument('--max-rounds', type=int, default=200000)
a = ap.parse_args()
LOG = open(a.log, 'w') if a.log else sys.stdout
def log(*x): print(*x, file=LOG, flush=True)

elf = open(a.lib, 'rb').read()
e_phoff, = struct.unpack_from('<Q', elf, 0x20); e_phnum, = struct.unpack_from('<H', elf, 0x38)
loads, dyn = [], None
for i in range(e_phnum):
    p_type, p_flags, p_off, p_vaddr, _, p_filesz, p_memsz, _ = struct.unpack_from('<IIQQQQQQ', elf, e_phoff + 56 * i)
    if p_type == 1: loads.append((p_vaddr, p_off, p_filesz, p_memsz))
    if p_type == 2: dyn = (p_off, p_filesz)
mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
SYMS = []
def _load_syms():
    shoff, = struct.unpack_from('<Q', elf, 0x28); shn, = struct.unpack_from('<H', elf, 0x3c); shstr, = struct.unpack_from('<H', elf, 0x3e)
    sh = [struct.unpack_from('<IIQQQQIIQQ', elf, shoff + 64 * i) for i in range(shn)]
    names = elf[sh[shstr][4]:sh[shstr][4] + sh[shstr][5]]
    def nm(o): return names[o:names.index(b'\0', o)].decode()
    out = []
    def symtab(img, secs, typ):
        for x in secs:
            if x[1] != typ: continue
            st = secs[x[6]]
            for k in range(x[5] // 24):
                n, info, oth, sx, val, sz = struct.unpack_from('<IBBHQQ', img, x[4] + 24 * k)
                if (info & 0xf) == 2 and val:
                    out.append((val, sz, img[st[4] + n:img.index(b'\0', st[4] + n)].decode('latin1')))
    for x in sh:
        if nm(x[0]) == '.gnu_debugdata':
            dd = lzma.decompress(elf[x[4]:x[4] + x[5]])
            o2, = struct.unpack_from('<Q', dd, 0x28); n2, = struct.unpack_from('<H', dd, 0x3c)
            symtab(dd, [struct.unpack_from('<IIQQQQIIQQ', dd, o2 + 64 * i) for i in range(n2)], 2)
    symtab(elf, sh, 11)
    out.sort(); return out
SYMS = _load_syms()
import bisect
_SYMV = [x[0] for x in SYMS]
def symname(off):
    i = bisect.bisect_right(_SYMV, off) - 1
    if i >= 0 and off < SYMS[i][0] + max(SYMS[i][1], 1): return '%s+%#x' % (SYMS[i][2], off - SYMS[i][0])
    return '%#x' % off
top = max(v + m for v, _, _, m in loads)
mu.mem_map(BASE, (top + 0xfff) & ~0xfff)
for v, o, f, m in loads: mu.mem_write(BASE + v, elf[o:o + f])
D = {}
for i in range(0, dyn[1], 16):
    t, v = struct.unpack_from('<qQ', elf, dyn[0] + i)
    if t == 0: break
    D.setdefault(t, v)
DT_SYMTAB, DT_STRTAB, DT_JMPREL, DT_PLTRELSZ, RELR, RELRSZ = 6, 5, 23, 2, 0x6fffe000, 0x6fffe001
def cstr(off):
    end = elf.index(b'\0', off); return elf[off:end].decode('latin1')
def sym(i):
    n, info, oth, shn, val, sz = struct.unpack_from('<IBBHQQ', elf, D[DT_SYMTAB] + 24 * i)
    return cstr(D[DT_STRTAB] + n), val, shn
# RELR
relr = D[RELR]; where = 0
for i in range(D[RELRSZ] // 8):
    ent, = struct.unpack_from('<Q', elf, relr + 8 * i)
    if not ent & 1:
        where = ent; v = struct.unpack('<Q', mu.mem_read(BASE + where, 8))[0]; mu.mem_write(BASE + where, struct.pack('<Q', v + BASE)); where += 8
    else:
        bits = ent >> 1
        for b in range(63):
            if bits & (1 << b):
                w = where + 8 * b; v = struct.unpack('<Q', mu.mem_read(BASE + w, 8))[0]; mu.mem_write(BASE + w, struct.pack('<Q', v + BASE))
        where += 63 * 8
# exports
EXP = {}
e_shoff, = struct.unpack_from('<Q', elf, 0x28); e_shnum, = struct.unpack_from('<H', elf, 0x3c)
nsyms = 0
for i in range(e_shnum):
    sh_type, = struct.unpack_from('<I', elf, e_shoff + 64 * i + 4)
    if sh_type == 11: nsyms = struct.unpack_from('<Q', elf, e_shoff + 64 * i + 32)[0] // 24   # SHT_DYNSYM size
for i in range(1, nsyms):
    n, v, shn = sym(i)
    if shn and v: EXP[n] = BASE + v
# imports -> traps
mu.mem_map(TRAP, 0x10000)
IMP = {}          # trap addr -> name
for i in range(D[DT_PLTRELSZ] // 24):
    off, info, add = struct.unpack_from('<QQq', elf, D[DT_JMPREL] + 24 * i)
    name, sval, sshn = sym(info >> 32)
    if sshn and sval:                       # preemptible export of libmnl itself (PLT call to its own function)
        mu.mem_write(BASE + off, struct.pack('<Q', BASE + sval)); continue
    t = TRAP + 4 * i; IMP[t] = name
    mu.mem_write(BASE + off, struct.pack('<Q', t))
mu.mem_write(TRAP, b'\xc0\x03\x5f\xd6' * 0x4000)   # ret everywhere; hooks run before
CB = {}           # callback table traps
for k in range(20): CB[TRAP + 0x8000 + 4 * k] = k * 8
mu.mem_map(HEAP, HEAP_SZ); mu.mem_map(STK, STK_SZ * NSTK); mu.mem_map(TLS, 0x10000)
mu.reg_write(UC_ARM64_REG_TPIDR_EL0, TLS + 0x100)
heap_top = [HEAP + 0x1000]
def malloc(n):
    p = (heap_top[0] + 15) & ~15; heap_top[0] = p + max(n, 16)
    if heap_top[0] > HEAP + HEAP_SZ: raise MemoryError
    return p
def rstr(p, maxn=4096):
    b = bytes(mu.mem_read(p, maxn)); return b[:b.index(b'\0')] if b'\0' in b else b
R = lambda n: mu.reg_read(getattr(__import__('unicorn.arm64_const', fromlist=['x']), 'UC_ARM64_REG_X%d' % n))
def W(n, v): mu.reg_write(getattr(__import__('unicorn.arm64_const', fromlist=['x']), 'UC_ARM64_REG_X%d' % n), v & 0xffffffffffffffff)

# ---- Linux AAPCS64 varargs (what the shim must do on Apple, where variadics go on the stack): read args in Linux order
class VA:
    def __init__(self, first_gp, first_fp=0, va_list=None):
        self.gp = first_gp; self.fp = first_fp; self.vl = va_list; self.sp = mu.reg_read(UC_ARM64_REG_SP)
    def int(self):
        if self.vl is not None:
            stk, grtop, vrtop, groff, vroff = struct.unpack('<QQQii', mu.mem_read(self.vl, 32))
            if groff < 0:
                v, = struct.unpack('<Q', mu.mem_read(grtop + groff, 8)); mu.mem_write(self.vl + 24, struct.pack('<i', groff + 8)); return v
            v, = struct.unpack('<Q', mu.mem_read(stk, 8)); mu.mem_write(self.vl, struct.pack('<Q', stk + 8)); return v
        if self.gp < 8: v = R(self.gp); self.gp += 1; return v
        v, = struct.unpack('<Q', mu.mem_read(self.sp, 8)); self.sp += 8; return v
    def dbl(self):
        if self.vl is not None:
            stk, grtop, vrtop, groff, vroff = struct.unpack('<QQQii', mu.mem_read(self.vl, 32))
            if vroff < 0:
                v, = struct.unpack('<d', mu.mem_read(vrtop + vroff, 8)); mu.mem_write(self.vl + 28, struct.pack('<i', vroff + 16)); return v
            v, = struct.unpack('<d', mu.mem_read(stk, 8)); mu.mem_write(self.vl, struct.pack('<Q', stk + 8)); return v
        if self.fp < 8:
            v = struct.unpack('<d', mu.reg_read(UC_ARM64_REG_Q0 + self.fp).to_bytes(16, 'little')[:8])[0] if False else \
                struct.unpack('<d', struct.pack('<Q', mu.reg_read(getattr(__import__('unicorn.arm64_const', fromlist=['x']), 'UC_ARM64_REG_D%d' % self.fp))))[0]
            self.fp += 1; return v
        v, = struct.unpack('<d', mu.mem_read(self.sp, 8)); self.sp += 8; return v
def cfmt(fmt, va):
    out = []; i = 0; f = fmt
    while i < len(f):
        c = f[i]
        if c != '%': out.append(c); i += 1; continue
        j = i + 1
        while j < len(f) and f[j] in '-+ #0123456789.*hljztL': j += 1
        if j >= len(f): break
        spec = f[i:j + 1]; conv = f[j]; i = j + 1
        if conv == '%': out.append('%'); continue
        flags = spec[1:-1]
        if '*' in flags: flags = flags.replace('*', str(va.int() & 0xffffffff))
        flags = flags.replace('l', '').replace('h', '').replace('z', '').replace('j', '').replace('t', '').replace('L', '')
        if conv in 'di':
            v = va.int(); v = v - (1 << 64) if v >> 63 else v
            if 'l' not in spec[1:-1] and 'z' not in spec[1:-1] and 'j' not in spec[1:-1]: v = ((v & 0xffffffff) ^ 0x80000000) - 0x80000000
            out.append(('%' + flags + 'd') % v)
        elif conv in 'uxXo':
            v = va.int()
            if 'l' not in spec[1:-1] and 'z' not in spec[1:-1]: v &= 0xffffffff
            out.append(('%' + flags + conv) % v)
        elif conv == 'c': out.append(chr(va.int() & 0xff))
        elif conv == 's': p = va.int(); out.append(rstr(p).decode('latin1') if p else '(null)')
        elif conv == 'p': out.append('0x%x' % va.int())
        elif conv in 'fFeEgGaA': out.append(('%' + flags + conv) % va.dbl())
        else: out.append(spec)
    return ''.join(out)

# ---- fake world
FILES = {}; next_fd = [10]
PIPE_TX = bytearray()        # what libmnl wrote to /dev/gpsdl0
EPOCH = 1_700_000_000; VT = [0]      # virtual ns since start
def vnow():
    VT[0] += 20_000; return EPOCH * 10**9 + VT[0]
RX = {}                              # fd -> bytearray of DSP->host bytes waiting
TXLOG = []; RXLOG = []; TXSEQ = [0]
WOKEN = set()
DSP = {}
if a.dsp_script:
    exec(compile(open(a.dsp_script).read(), a.dsp_script, 'exec'), DSP)
def bt(n=12):
    out = []; fp = mu.reg_read(UC_ARM64_REG_X29); lr = mu.reg_read(UC_ARM64_REG_LR)
    out.append(symname(lr - BASE) if BASE <= lr < BASE + 0x600000 else hex(lr))
    for _ in range(n):
        if not (STK <= fp < STK + STK_SZ * NSTK): break
        nfp, nlr = struct.unpack('<QQ', mu.mem_read(fp, 16))
        if not (BASE <= nlr < BASE + 0x600000): break
        out.append(symname(nlr - BASE)); fp = nfp
    return ' < '.join(out)
EVENTS = []
NMEA = []
props = {}
def openf(path, flags):
    fd = next_fd[0]; next_fd[0] += 1
    FILES[fd] = {'path': path, 'pos': 0, 'data': b''}
    if path.startswith('/vendor/etc/') or path.startswith('/data/vendor/gps/') or path.startswith('/vendor/etc/gnss'):
        hp = os.path.join(os.path.dirname(a.lib), '..', '..', 'etc', os.path.basename(path))
        if os.path.exists(hp): FILES[fd]['data'] = open(hp, 'rb').read()
        elif 'MNL_Config' not in path: del FILES[fd]; return -1
    return fd

def ret(v): W(0, v)
threads = []; cur = [0]
class Block(Exception): pass

def imp_call(name):
    if a.trace_imports: log('IMP', name, hex(R(0)), hex(R(1)), hex(R(2)), 'lr=libmnl+%#x' % (mu.reg_read(UC_ARM64_REG_LR) - BASE))
    x = [R(i) for i in range(8)]
    if name in ('malloc',): ret(malloc(x[0]))
    elif name == 'free': ret(0)
    elif name in ('memcpy', '__memcpy_chk'): mu.mem_write(x[0], bytes(mu.mem_read(x[1], x[2]))) if x[2] else None; ret(x[0])
    elif name in ('memset', '__memset_chk'): mu.mem_write(x[0], bytes([x[1] & 0xff]) * x[2]) if x[2] else None; ret(x[0])
    elif name in ('strlen', '__strlen_chk'): ret(len(rstr(x[0], 65536)))
    elif name in ('strcpy', '__strcpy_chk'): s = rstr(x[1]) + b'\0'; mu.mem_write(x[0], s); ret(x[0])
    elif name in ('strncpy', '__strncpy_chk', '__strncpy_chk2'):
        s = rstr(x[1])[:x[2]]; mu.mem_write(x[0], s + b'\0' * (x[2] - len(s))); ret(x[0])
    elif name in ('strcat', '__strcat_chk'): d0 = rstr(x[0]); mu.mem_write(x[0] + len(d0), rstr(x[1]) + b'\0'); ret(x[0])
    elif name in ('strncat', '__strncat_chk'): d0 = rstr(x[0]); mu.mem_write(x[0] + len(d0), rstr(x[1])[:x[2]] + b'\0'); ret(x[0])
    elif name == 'strcmp': s1, s2 = rstr(x[0]), rstr(x[1]); ret((s1 > s2) - (s1 < s2))
    elif name == 'strncmp': s1, s2 = rstr(x[0])[:x[2]], rstr(x[1])[:x[2]]; ret((s1 > s2) - (s1 < s2))
    elif name == 'strchr': s = rstr(x[0]); k = s.find(bytes([x[1] & 0xff])); ret(x[0] + k if k >= 0 else 0)
    elif name == 'strrchr': s = rstr(x[0]); k = s.rfind(bytes([x[1] & 0xff])); ret(x[0] + k if k >= 0 else 0)
    elif name == 'strstr': s = rstr(x[0]); k = s.find(rstr(x[1])); ret(x[0] + k if k >= 0 else 0)
    elif name in ('vsnprintf', '__vsnprintf_chk', '__vsprintf_chk'):
        if name == '__vsnprintf_chk': fmt, vl, n = rstr(x[4]), x[5], x[1]
        elif name == '__vsprintf_chk': fmt, vl, n = rstr(x[3]), x[4], x[2]
        else: fmt, vl, n = rstr(x[2]), x[3], x[1]
        s = cfmt(fmt.decode('latin1'), VA(8, 0, vl)).encode('latin1')
        if n: mu.mem_write(x[0], s[:max(n - 1, 0)] + b'\0')
        ret(len(s))
    elif name in ('printf', 'fprintf', 'puts', 'fputs'):
        if name == 'printf': s = cfmt(rstr(x[0]).decode('latin1'), VA(1))
        elif name == 'fprintf': s = cfmt(rstr(x[1]).decode('latin1'), VA(2))
        else: s = rstr(x[0]).decode('latin1')
        log('LIBMNL-OUT', s.rstrip()); ret(len(s))
    elif name == 'sscanf': ret(0)
    elif name in ('__open_2', 'open'):
        p = rstr(x[0]).decode('latin1'); fd = openf(p, x[1]); log('OPEN', p, '->', fd); ret(fd if fd >= 0 else -1)
    elif name == 'fopen':
        p = rstr(x[0]).decode('latin1'); fd = openf(p, 0); log('FOPEN', p, '->', fd); ret(0x7e000000 + fd if fd >= 0 else 0)
    elif name in ('fclose', 'close'): ret(0)
    elif name in ('read', '__read_chk', 'fread', 'fgets'):
        fd = x[0] if name in ('read', '__read_chk') else ((x[3] if name == 'fread' else x[2]) - 0x7e000000)
        f = FILES.get(fd)
        if f and f['path'].startswith('/dev/gpsdl'):
            q = RX.setdefault(fd, bytearray())
            if q:
                k = min(len(q), x[2]); b = bytes(q[:k]); del q[:k]; mu.mem_write(x[1], b)
                RXLOG.append((VT[0], fd, b.hex())); log('GPSDL-READ vt=%.6f fd=%d want=%d got=%d: %s' % (VT[0] / 1e9, fd, x[2], k, b[:64].hex())); ret(k); return
            log('GPSDL-READ-WAIT vt=%.6f fd=%d want=%d bt=%s' % (VT[0] / 1e9, fd, x[2], bt()))
            raise Block(('read', fd, x[1], x[2]))
        if not f: ret(-1 if name in ('read', '__read_chk') else 0); return
        if name == 'fgets':
            rest = f['data'][f['pos']:]
            if not rest: ret(0); return
            k = rest.find(b'\n'); k = len(rest) if k < 0 else k + 1; k = min(k, x[1] - 1)
            mu.mem_write(x[0], rest[:k] + b'\0'); f['pos'] += k; ret(x[0]); return
        n = x[2] if name != 'fread' else x[1] * x[2]
        b = f['data'][f['pos']:f['pos'] + n]; mu.mem_write(x[1] if name != 'fread' else x[0], b); f['pos'] += len(b)
        ret(len(b) if name != 'fread' else len(b) // max(x[1], 1))
    elif name in ('write', '__write_chk', 'fwrite', 'fputc'):
        if name == 'fputc': ret(x[0] & 0xff); return
        fd, buf, n = (x[0], x[1], x[2]) if name != 'fwrite' else (x[3] - 0x7e000000, x[0], x[1] * x[2])
        b = bytes(mu.mem_read(buf, n)) if n else b''
        f = FILES.get(fd)
        if f and f['path'].startswith('/dev/gpsdl'):
            PIPE_TX.extend(b); TXSEQ[0] += 1
            TXLOG.append({'seq': TXSEQ[0], 'vt_ns': VT[0], 'fd': fd, 'len': n, 'hex': b.hex() if n <= 4096 else None, 'bt': bt()})
            log('GPSDL-WRITE #%d vt=%.6f fd=%d %d bytes: %s%s bt=%s' % (TXSEQ[0], VT[0] / 1e9, fd, n, b[:64].hex(), '..' if n > 64 else '', bt()))
            if 'on_tx' in DSP: DSP['on_tx'](TXSEQ[0], b, VT[0], RX.setdefault(fd, bytearray()), log)
        ret(n if name != 'fwrite' else x[2])
    elif name == 'ioctl':
        # MT6893 gps_each_device.c: only TRIGGER_ASSERT(12), QUERY_STATUS(13), CO_CLOCK_FLAG(11), wakelocks(14,15),
        # HW_SUSPEND/RESUME(18,19), LISTEN_RST_EVT(20) exist; 6..9 are compiled out -> -EINVAL
        cmd = x[1]; r = {11: 0, 13: 0, 14: 0, 15: 0, 18: 0, 19: 0}.get(cmd, -22)
        log('IOCTL fd=%d cmd=%d -> %d' % (x[0], cmd, r))
        if r < 0: mu.mem_write(TLS + 0x40, struct.pack('<i', -r)); ret(-1)
        else: ret(r)
    elif name in ('access', 'stat', 'mkdir', 'chmod', 'remove', 'rename'): ret(-1 if name in ('access', 'stat') else 0)
    elif name == 'property_get':
        k = rstr(x[0]).decode(); v = props.get(k); dflt = rstr(x[2]) if x[2] else b''
        s = (v.encode() if v else dflt); mu.mem_write(x[1], s + b'\0'); ret(len(s))
    elif name == '__errno': ret(TLS + 0x40)
    elif name in ('pthread_mutex_init', 'pthread_mutex_lock', 'pthread_mutex_unlock', 'pthread_mutex_destroy',
                  'pthread_cond_init', 'pthread_cond_destroy', 'sigaction', 'sigemptyset',
                  'sigaddset', 'sigprocmask', 'pthread_setschedparam', 'pthread_getschedparam', 'setbuf',
                  'fflush', 'srand', 'cfsetispeed', 'cfsetospeed', 'tcsetattr', 'tcgetattr', 'tcflush', 'setsockopt',
                  'bind', 'listen', 'epoll_ctl', 'timer_delete', 'timer_settime', 'pthread_kill', 'closedir', 'fcntl', 'pthread_join',
                  'sched_get_priority_max', 'sched_get_priority_min', '__cxa_finalize'): ret(0)
    elif name == 'pthread_self': ret(0x1000 + cur[0])
    elif name == 'pthread_equal': ret(int(x[0] == x[1]))
    elif name == 'pthread_create':
        tid = len(threads); threads.append({'fn': x[2], 'arg': x[3], 'ctx': None, 'state': 'new', 'name': hex(x[2] - BASE)})
        mu.mem_write(x[0], struct.pack('<Q', 0x1000 + tid)); log('PTHREAD_CREATE fn=libmnl+%#x arg=%#x' % (x[2] - BASE, x[3])); ret(0)
    elif name == 'usleep': raise Block(('usleep', VT[0] + max(x[0] & 0xffffffff, 1) * 1000))
    elif name == 'sleep': raise Block(('sleep', VT[0] + max((x[0] & 0xffffffff) * 10**9, 1000)))
    elif name == 'pthread_cond_timedwait':
        sec, nsec = struct.unpack('<qq', mu.mem_read(x[2], 16)); raise Block(('pthread_cond_timedwait', sec * 10**9 + nsec - EPOCH * 10**9, x[0]))
    elif name == 'pthread_cond_wait': raise Block(('pthread_cond_wait', VT[0] + 100 * 10**6, x[0]))
    elif name == 'epoll_pwait': raise Block(('epoll_pwait', VT[0] + (10**12 if (x[3] & 0xffffffff) == 0xffffffff else (x[3] & 0xffffffff) * 10**6)))
    elif name == 'accept': raise Block(('accept', None))
    elif name in ('pthread_cond_signal', 'pthread_cond_broadcast'): WOKEN.add(x[0]); ret(0)
    elif name == 'pthread_exit': raise Block(('exit',))
    elif name in ('socket', 'pipe', 'epoll_create', 'timer_create', 'opendir'):
        if name == 'pipe': mu.mem_write(x[0], struct.pack('<ii', 90, 91))
        ret(0 if name in ('pipe', 'timer_create') else 50)
    elif name in ('time',): t = vnow() // 10**9; (mu.mem_write(x[0], struct.pack('<q', t)) if x[0] else None); ret(t)
    elif name in ('clock_gettime',): t = vnow(); mu.mem_write(x[1], struct.pack('<qq', t // 10**9, t % 10**9)); ret(0)
    elif name == 'gettimeofday': t = vnow(); mu.mem_write(x[0], struct.pack('<qq', t // 10**9, (t % 10**9) // 1000)); ret(0)
    elif name == 'rand': ret(4)
    elif name in ('abort', 'exit', '__stack_chk_fail'): log('FATAL', name); raise SystemExit(2)
    elif name in ('atoi',): ret(int((rstr(x[0]) or b'0').split()[0] or 0) & 0xffffffff)
    elif name in ('strtoul',): ret(int(rstr(x[0]) or b'0', x[2] or 0))
    else:
        log('UNIMPL', name); ret(0)

def cb_call(slot):
    x = [R(i) for i in range(8)]
    if slot == 0x00:
        s = bytes(mu.mem_read(x[0], x[1])) if x[1] and x[1] < 4096 else rstr(x[0]); NMEA.append(s); log('NMEA', s)
    elif slot == 0x08 and 0 < x[1] < 4096:
        log('MNL-LOG', bytes(mu.mem_read(x[0], x[1])).rstrip(b'\0\r\n').decode('latin1'))
    else:
        log('CB +0x%02x(%#x, %#x, %#x)' % (slot, x[0], x[1], x[2]))
    ret(0)

steps = [0]
def hook(uc, addr, size, ud):
    if addr in IMP:
        try: imp_call(IMP[addr])
        except Block as b:
            threads[cur[0]]['blocked'] = b.args[0]; uc.emu_stop(); threads[cur[0]]['state'] = 'blocked'
            uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_LR)); ret(0)
    elif addr in CB: cb_call(CB[addr])
    elif addr == MAGIC_RET:
        threads[cur[0]]['state'] = 'done'; uc.emu_stop()
mu.hook_add(UC_HOOK_CODE, hook, begin=TRAP, end=TRAP + 0x10000)
def bad(uc, access, addr, size, value, ud):
    log('FAULT access=%d addr=%#x pc=%#x (libmnl+%#x)' % (access, addr, uc.reg_read(UC_ARM64_REG_PC), uc.reg_read(UC_ARM64_REG_PC) - BASE)); return False
mu.hook_add(UC_HOOK_MEM_UNMAPPED, bad)
MARK = {0x3e1100: 'BootDSP_UART_V2(L1) code_size log', 0x3e1124: 'BootDSP_UART_V2(L1) stage 1', 0x3e1644: 'BootDSP(L1) start',
        0x3e1a14: 'BootDSP(L1) waiting', 0x3e1fcc: 'BootDSP(L1) finished', 0x435cdc: 'Send_DspCode fail L1', 0x3efe34: 'MVCD'}
REACHED = []
def mark(uc, addr, size, ud):
    n = MARK.get(addr - BASE)
    if n: REACHED.append(n); log('REACHED', n, 'thread', cur[0])
for off in MARK: mu.hook_add(UC_HOOK_CODE, mark, begin=BASE + off, end=BASE + off)
DBGP = 0x3f9acc     # libmnl internal level-gated debug printf (w0 module, w1 level, x2 tag, x3 fmt, varargs from x4/d0)
def dbgp(uc, addr, size, ud):
    try:
        fmt = rstr(R(3)).decode('latin1'); tag = rstr(R(2)).decode('latin1') if R(2) else ''
        log('DBG vt=%.6f [%d/%d %s] %s' % (VT[0] / 1e9, R(0) & 0xff, R(1) & 0xff, tag, cfmt(fmt, VA(4, 0)).rstrip()))
    except Exception as e: log('DBG decode error', e)
mu.hook_add(UC_HOOK_CODE, dbgp, begin=BASE + DBGP, end=BASE + DBGP)

def call(fn, *args, tid=None):
    """run fn on a fresh thread context until it returns or blocks"""
    if tid is None:
        tid = len(threads); threads.append({'fn': fn, 'arg': 0, 'ctx': None, 'state': 'new', 'name': 'main'})
    t = threads[tid]; cur[0] = tid
    sp = STK + STK_SZ * (tid % NSTK) + STK_SZ - 0x100
    mu.reg_write(UC_ARM64_REG_SP, sp); mu.reg_write(UC_ARM64_REG_LR, MAGIC_RET)
    for i, v in enumerate(args): W(i, v)
    t['state'] = 'run'
    try: mu.emu_start(fn, MAGIC_RET + 4, count=a.steps)
    except UcError as e: log('EMU ERROR', e, 'pc=libmnl+%#x' % (mu.reg_read(UC_ARM64_REG_PC) - BASE)); t['state'] = 'error'
    t['ctx'] = mu.context_save()
    return t['state']

def resume(tid):
    t = threads[tid]; cur[0] = tid; mu.context_restore(t['ctx']); t['state'] = 'run'
    try: mu.emu_start(mu.reg_read(UC_ARM64_REG_PC), MAGIC_RET + 4, count=a.steps)
    except UcError as e: log('EMU ERROR', e, 'pc=libmnl+%#x' % (mu.reg_read(UC_ARM64_REG_PC) - BASE)); t['state'] = 'error'
    t['ctx'] = mu.context_save()

# ---- the mnld replacement logic (subset): register callbacks, run
table = malloc(0xa0)
for k in range(20): mu.mem_write(table + 8 * k, struct.pack('<Q', TRAP + 0x8000 + 4 * k if k <= 16 else 0))   # mnld sets slots 0..16
log('libmnl exports', len(EXP), 'imports', len(IMP))
call(EXP['mtk_gps_sys_function_register'], table)
log('mtk_gps_sys_function_register ->', R(0))
cfg1 = malloc(0x200); cfg2 = malloc(0x500)
# cfg2 layout recovered from libmnl mtk_gps_mnl_run (memcpy 0x434 into its global, then): +0x280 int = DSP L1 device fd
# (-> libmnl global 0x57b264, used for read/ioctl), +0x284 int = second fd (L5), +0xfd char[] port name: "UseCallback"
# = no open (data through callbacks), +0x04 flags (bit3: second port at +0x11b). cfg1 (0x74 bytes, copied verbatim):
# from mnld 0x430e0: +0x00 double, +0x14 u16 = 100, +0x20 u32 = 115200.
fd_l1 = openf('/dev/gpsdl0', 2); fd_l5 = openf('/dev/gpsdl1', 2)
mu.mem_write(cfg2 + 0x280, struct.pack('<ii', fd_l1, fd_l5))
mu.mem_write(cfg2 + 0xfd, b'UseCallback\0')
mu.mem_write(cfg1 + 0x14, struct.pack('<H', 100)); mu.mem_write(cfg1 + 0x20, struct.pack('<I', 115200))
# mnld 0x4316c: cfg1+0x00 = 8 bytes from mnld .rodata 0x1ac08 = 01 00 00 00 00 00 00 00 (int 1, int 0), not a double.
# libmnl mtk_gps_init copies cfg1 to 0x58f138; mtk_init_gps_wrapper calls mtk_gps_dsp_init (-> hal_sys_dsp_boot ->
# vBootDSP_UART_V2 -> _BootDSP_UART_V2) only if that word is non-zero.  --cfg1-word0 0 reproduces the old harness.
mu.mem_write(cfg1, struct.pack('<ii', a.cfg1_word0, 0))
# mnld 0x45d2c..0x45c20 (MT6893 branch of its chip detection): cfg1+0x2c = cfg1+0x30 = 0x250 (u32 'x'),
# cfg1+0x34 = cfg1+0x3c = 0x00000000ffff6893 (u64 'y'). libmnl _BootDSP_UART_V2 picks the DSP blob from these.
if a.chip_x is not None: mu.mem_write(cfg1 + 0x2c, struct.pack('<II', a.chip_x, a.chip_x))
if a.chip_y is not None: mu.mem_write(cfg1 + 0x34, struct.pack('<QQ', a.chip_y, a.chip_y))
st = call(EXP['mtk_gps_mnl_run'], cfg1, cfg2)
log('mtk_gps_mnl_run state', st, threads[cur[0]].get('blocked'))
if a.call_dsp_init:                                    # optional: drive the unreferenced boot entry
    dcfg = malloc(0x100)
    tid = len(threads); threads.append({'fn': EXP['mtk_gps_dsp_init'], 'arg': dcfg, 'ctx': None, 'state': 'new', 'name': 'dsp_init'})
def wake_val(t):
    """return (ready, x0) for a blocked thread"""
    b = t.get('blocked') or ('',)
    k = b[0]
    if k == 'read':
        q = RX.get(b[1])
        return (bool(q), 0)
    if k in ('usleep', 'sleep', 'epoll_pwait'): return (VT[0] >= b[1], 0)
    if k == 'pthread_cond_timedwait':
        if b[2] in WOKEN: return (True, 0)
        return (VT[0] >= b[1], 110)
    if k == 'pthread_cond_wait': return (b[2] in WOKEN or VT[0] >= b[1], 0)
    return (False, 0)
def resume_with(tid, x0):
    t = threads[tid]; b = t.get('blocked') or ('',)
    cur[0] = tid; mu.context_restore(t['ctx'])
    if b[0] == 'read':                                 # complete the parked read now
        q = RX[b[1]]; k = min(len(q), b[3]); data = bytes(q[:k]); del q[:k]; mu.mem_write(b[2], data)
        RXLOG.append((VT[0], b[1], data.hex())); log('GPSDL-READ vt=%.6f fd=%d want=%d got=%d: %s' % (VT[0] / 1e9, b[1], b[3], k, data[:64].hex())); x0 = k
    W(0, x0); t['state'] = 'run'; t['blocked'] = None
    try: mu.emu_start(mu.reg_read(UC_ARM64_REG_PC), MAGIC_RET + 4, count=a.steps)
    except UcError as e: log('EMU ERROR', e, 'pc=libmnl+%#x' % (mu.reg_read(UC_ARM64_REG_PC) - BASE)); t['state'] = 'error'
    if t['state'] == 'run': log('THREAD %d ran out of steps at %s' % (tid, symname(mu.reg_read(UC_ARM64_REG_PC) - BASE))); t['state'] = 'blocked'; t['blocked'] = ('yield', 0)
    t['ctx'] = mu.context_save()
last_vt_log = [0]
for rnd in range(a.max_rounds):
    progressed = False
    for tid, t in enumerate(list(threads)):
        if t['state'] == 'new' and tid != 0:
            log('THREAD %d start libmnl+%s vt=%.6f' % (tid, t['name'], VT[0] / 1e9)); call(t['fn'], t['arg'], tid=tid); progressed = True
            if t['state'] == 'run': t['state'] = 'blocked'; t['blocked'] = ('yield', 0)
        elif t['state'] == 'blocked':
            if t['blocked'] and t['blocked'][0] == 'yield': resume_with(tid, 0); progressed = True; continue
            ok, v = wake_val(t)
            if ok:
                c = t['blocked'][2] if t['blocked'][0].startswith('pthread_cond') else None
                resume_with(tid, v); progressed = True
    WOKEN.clear()
    if 'on_tick' in DSP: DSP['on_tick'](VT[0], RX.setdefault(10, bytearray()), log)
    if not progressed:
        dl = [t['blocked'][1] for t in threads if t['state'] == 'blocked' and t.get('blocked') and t['blocked'][0] in ('usleep', 'sleep', 'epoll_pwait', 'pthread_cond_timedwait', 'pthread_cond_wait') and t['blocked'][1] is not None]
        if any(RX.get(fd) for fd in RX) and any(t.get('blocked') and t['blocked'][0] == 'read' for t in threads): continue
        if not dl: break
        VT[0] = max(VT[0], min(dl))
    if VT[0] > a.vt_limit * 1e9: log('VT limit reached'); break
    if VT[0] - last_vt_log[0] > 10**9: last_vt_log[0] = VT[0]; log('VT %.3f s, round %d, tx %d bytes' % (VT[0] / 1e9, rnd, len(PIPE_TX)))
if a.tx_bin: open(a.tx_bin, 'wb').write(bytes(PIPE_TX))
if a.tx_json: json.dump({'tx': TXLOG, 'rx': RXLOG}, open(a.tx_json, 'w'), indent=0)
log('SUMMARY vt=%.6f' % (VT[0] / 1e9), 'threads', [(i, t['name'], t['state'], t.get('blocked')) for i, t in enumerate(threads)])
log('SUMMARY gpsdl bytes written', len(PIPE_TX), 'nmea', len(NMEA), 'reached', sorted(set(REACHED)))
