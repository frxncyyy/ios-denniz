#!/usr/bin/env python3
"""Host "boot" of a Nord 2 boot image: what LK does (gunzip the kernel to 0x40080000, ramdisk and FDT at the
header addresses, x0 = FDT, EL1, MMU off) and then the REAL loader bytes of the image under an AArch64
emulator (Unicorn), up to the jump into XNU or to the SoC reset. No phone, no device access.

  emu_boot.py IMAGE --kernel KERNEL --adt GPU_ADT --receipt image-receipt.json [--legacy LEGACY_IMAGE] [--stub STUB]

Checks (each prints ok/FAIL; exit 1 on any FAIL):
  boot    the loader reaches the payload's entry point with x0 = boot_args; every Mach-O segment in the RAM
          window equals the ORIGINAL kernel (KERNEL stripped and fixed up again here with the project's tools,
          independently of the image), BSS is zero, the ADT is the payload's with only the /chosen values the
          loader fills in, boot_args hold the command line of the receipt; outside the window only the
          loader's own BSS/stack and the pmsg ring changed; no MMIO, no unmapped access.
  legacy  (--legacy) the same run on the image with the Android kernel: identical RAM window, entry and x0.
  fail-*  failure paths of a nostock image: loader fatal() and XNU's chain-back enter the stub, which sets
          the RTC fast-boot bit through PMIF (model of the register protocol of sweet_arm_fastboot) and resets
          through TOPRGU; with PMIF stuck nothing is written to the PMIC and the reset still happens; every
          exception vector of the stub ends in the reset or in the park loop; XNU is never entered.
"""
import argparse, hashlib, importlib.util, json, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_INTR
from unicorn.arm64_const import *

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[3]                      # .../work
MAIN = WORK.parents[4]                      # .../ios-sweet
sys.path.insert(0, str(WORK / 'tools'))
import adt as apple_adt                      # noqa: E402

KERNEL_PA, RAMDISK_PA, STOCK_PA, LOADER_RESERVE = 0x40080000, 0x51100000, 0x40280000, 0x200000
LOW_BASE, LOW_SIZE = 0x40000000, 0x18000000
PSTORE_BASE, PSTORE_SIZE, PMSG_SIZE = 0x48090000, 0xe0000, 0x10000
PMSG_BASE = PSTORE_BASE + PSTORE_SIZE - PMSG_SIZE
PMIF, TOPRGU = 0x10026000, 0x10007000
CONN_BASE, CONN_BYTES, POOL_BASE, POOL_BYTES = 0x8fb00000, 0x450000, 0x90000000, 0xf20000
FAILS = []


def check(name, ok, detail=''):
    print(('ok   ' if ok else 'FAIL ') + name + (f' -- {detail}' if detail else ''))
    if not ok:
        FAILS.append(name)
    return ok


# ---------------------------------------------------------------- the live FDT LK hands over (synthetic, minimal)
def make_fdt(compatible=b'oplus,mt6893\0mediatek,MT6893\0'):
    strings = bytearray(); offs = {}
    def s(name):
        if name not in offs:
            offs[name] = len(strings); strings.extend(name.encode() + b'\0')
        return offs[name]
    out = bytearray()
    def begin(name):
        out.extend(struct.pack('>I', 1) + name.encode() + b'\0'); out.extend(b'\0' * (-len(out) & 3))
    def end():
        out.extend(struct.pack('>I', 2))
    def prop(name, val=b''):
        out.extend(struct.pack('>III', 3, len(val), s(name)) + val); out.extend(b'\0' * (-len(out) & 3))
    c2 = lambda *v: b''.join(struct.pack('>Q', x) for x in v)
    begin('')
    prop('#address-cells', struct.pack('>I', 2)); prop('#size-cells', struct.pack('>I', 2))
    prop('compatible', compatible)
    begin('chosen'); prop('bootargs', b'console=tty0 root=/dev/ram ramoops.mem_address=0x48090000 ramoops.pmsg_size=0x10000 bootopt=64S3,32N2,64N2\0'); end()
    begin('memory@40000000'); prop('device_type', b'memory\0'); prop('reg', c2(0x40000000, 0x2c0000000)); end()
    begin('reserved-memory')
    prop('#address-cells', struct.pack('>I', 2)); prop('#size-cells', struct.pack('>I', 2)); prop('ranges')
    begin('mblock-16-pstore'); prop('reg', c2(PSTORE_BASE, PSTORE_SIZE)); end()
    begin('consys-reserve-memory'); prop('no-map'); prop('size', c2(CONN_BYTES)); prop('alignment', c2(0x100000))
    prop('alloc-ranges', c2(0x50000000, 0x40000000)); end()
    begin('wifi-reserve-memory'); prop('compatible', b'shared-dma-pool\0'); prop('no-map'); prop('size', c2(POOL_BYTES))
    prop('alignment', c2(0x1000000)); prop('alloc-ranges', c2(0x40000000, 0x80000000)); prop('phandle', struct.pack('>I', 0x55)); end()
    end()
    begin('consys@18000000'); prop('compatible', b'mediatek,mt6893-consys\0'); end()
    begin('wifi@18000000'); prop('compatible', b'mediatek,wifi\0'); prop('memory-region', struct.pack('>I', 0x55)); end()
    end()
    out.extend(struct.pack('>I', 9))
    rsv = b'\0' * 16
    off_rsv = 40; off_struct = off_rsv + len(rsv); off_strings = off_struct + len(out)
    strings.extend(b'\0' * (-len(strings) & 3))
    total = off_strings + len(strings)
    hdr = struct.pack('>10I', 0xd00dfeed, total, off_struct, off_strings, off_rsv, 17, 16, 0, len(strings), len(out))
    return hdr + rsv + bytes(out) + bytes(strings)


# ---------------------------------------------------------------- boot image -> what LK leaves in RAM
class Image:
    def __init__(self, path):
        d = Path(path).read_bytes(); self.raw = d
        u = lambda o: struct.unpack_from('<I', d, o)[0]
        assert d[:8] == b'ANDROID!' and u(40) == 2 and u(36) == 2048
        page = 2048; al = lambda n: (n + page - 1) // page * page
        ks, rs, ds = u(8), u(16), u(1648)
        self.kernel_addr, self.ramdisk_addr = u(12), u(20)
        self.dtb_addr = struct.unpack_from('<Q', d, 1652)[0]
        kgz = d[page:page + ks]; r0 = page + al(ks)
        self.ramdisk = d[r0:r0 + rs]; self.dtb = d[r0 + al(rs):r0 + al(rs) + ds]
        dec = zlib.decompressobj(31); self.blob = dec.decompress(kgz) + dec.flush()
        assert dec.eof, 'kernel gzip stream incomplete'            # LK: "decompress kernel image fail!!!"
        self.appended = dec.unused_data
        self.image_size = struct.unpack_from('<Q', self.blob, LOADER_RESERVE + 16)[0]
        self.payload_off = (LOADER_RESERVE + self.image_size + 0x3fff) & ~0x3fff
        self.payload = self.blob[self.payload_off:]
        self.is_stub = self.blob[LOADER_RESERVE + 0x40:LOADER_RESERVE + 0x4a] == b'NORD2STUB1'


class Payload:
    def __init__(self, p):
        magic, ver, self.nsegs = struct.unpack_from('<8sII', p, 0)
        assert magic == b'SWEETXNU' and ver == 1
        (self.virt_base, self.entry_va, self.adt_off, self.adt_size, self.kernel_end,
         self.phys_base, self.mem_size) = struct.unpack_from('<7Q', p, 0x10)
        self.cmdline = p[0x48:0x48 + 608]
        self.segs = [struct.unpack_from('<4Q', p, 0x2a8 + 32 * i) for i in range(self.nsegs)]
        self.delta = self.phys_base - self.virt_base
        self.entry_pa = self.entry_va + self.delta
        self.kernel_end_pa = self.kernel_end + self.delta
        self.ba_pa = (self.kernel_end_pa + self.adt_size + 0x3fff) & ~0x3fff
        self.adt = p[self.adt_off:self.adt_off + self.adt_size]


# ---------------------------------------------------------------- machine
class PmifModel:
    """PMIF SWINF2 as sweet_pw_read/sweet_pw_write use it: ACC 0xc80, WDATA 0xc84, RDATA 0xc94, VLD_CLR 0xca4, STA 0xca8 (fsm = bits 3:1)."""
    def __init__(self, stuck=False):
        self.stuck = stuck; self.fsm = 0; self.wdata = 0; self.rdata = 0
        self.rtc = {0x5b4: 0x0041, 0x588: 0x0000, 0x5c2: 0}; self.busy_reads = 3; self.writes = []; self.log = []
    def read(self, uc, off, size, _):
        if off == 0xca8: return (3 if self.stuck else self.fsm) << 1
        if off == 0xc94: return self.rdata
        self.log.append(('r', off)); return 0
    def write(self, uc, off, size, val, _):
        self.log.append(('w', off, val))
        if off == 0xc84: self.wdata = val
        elif off == 0xc80:
            addr = val & 0xffff
            if val & (1 << 29):
                self.rtc[addr] = self.wdata & 0xffff; self.writes.append((addr, self.wdata & 0xffff))
            else:
                v = self.rtc.get(addr, 0)
                if addr == 0x588 and self.busy_reads:
                    self.busy_reads -= 1; v |= 0x40
                self.rdata = v; self.fsm = 6
        elif off == 0xca4 and val == 1: self.fsm = 0


class Machine:
    def __init__(self, img, fdt, pmif=None, window=None):
        self.img = img; self.fdt = fdt; self.stop = None; self.events = []
        self.pmif = pmif or PmifModel(); self.wdt = {0: 0x0000007d}; self.wdt_writes = []
        uc = self.uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        uc.mem_map(LOW_BASE, LOW_SIZE)
        uc.mem_write(img.kernel_addr, img.blob)                 # LK: gunzip to kernel_addr
        uc.mem_write(img.ramdisk_addr, img.ramdisk)
        uc.mem_write(img.dtb_addr, fdt)                          # the live FDT lands at the header's dtb address
        self.window = window
        if window:
            uc.mem_map(window[0], window[1])
        uc.mmio_map(PMIF, 0x1000, self.pmif.read, None, self.pmif.write, None)
        uc.mmio_map(TOPRGU, 0x1000, self._wdt_r, None, self._wdt_w, None)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_CODE, self._stock, begin=STOCK_PA, end=STOCK_PA)
        self.before = bytes(uc.mem_read(LOW_BASE, LOW_SIZE))

    def _wdt_r(self, uc, off, size, _): return self.wdt.get(off, 0)
    def _wdt_w(self, uc, off, size, val, _):
        self.wdt_writes.append((off, val)); self.wdt[off] = val
        if off == 0x14 and val == 0x1209:
            self.stop = 'RESET'; uc.emu_stop()
    def _unmapped(self, uc, access, addr, size, value, _):
        self.stop = 'UNMAPPED %#x pc=%#x' % (addr, uc.reg_read(UC_ARM64_REG_PC)); return False
    def _intr(self, uc, intno, _):
        self.stop = 'EXCEPTION %d pc=%#x' % (intno, uc.reg_read(UC_ARM64_REG_PC)); uc.emu_stop()
    def _stock(self, uc, addr, size, _):
        self.events.append(('STOCK_ENTRY', uc.reg_read(UC_ARM64_REG_X0)))
        if not self.img.is_stub:
            self.stop = 'STOCK_ENTRY'; uc.emu_stop()
    def stop_at(self, addr, name):
        def h(uc, a, size, _):
            self.stop = name; uc.emu_stop()
        self.uc.hook_add(UC_HOOK_CODE, h, begin=addr, end=addr)

    def run(self, pc, x0, limit=400_000_000):
        uc = self.uc
        uc.reg_write(UC_ARM64_REG_X0, x0)
        for r in (UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3): uc.reg_write(r, 0)
        try:
            uc.emu_start(pc, 0, count=limit)
        except UcError as e:
            self.stop = self.stop or ('UCERROR %s pc=%#x' % (e, uc.reg_read(UC_ARM64_REG_PC)))
        if self.stop is None:
            self.stop = 'LIMIT pc=%#x' % uc.reg_read(UC_ARM64_REG_PC)
        return self.stop

    def pmsg(self):
        sig, start, size = struct.unpack('<3I', bytes(self.uc.mem_read(PMSG_BASE, 12)))
        return bytes(self.uc.mem_read(PMSG_BASE + 12, min(size, PMSG_SIZE - 12))).decode('latin1') if sig == 0x43474244 else ''

    def low_changes(self):
        after = bytes(self.uc.mem_read(LOW_BASE, LOW_SIZE)); out = []; step = 0x1000
        for o in range(0, LOW_SIZE, step):
            if after[o:o + step] != self.before[o:o + step]:
                if out and out[-1][1] == LOW_BASE + o: out[-1][1] += step
                else: out.append([LOW_BASE + o, LOW_BASE + o + step])
        return out


def el1_check(uc):
    return (uc.reg_read(UC_ARM64_REG_PSTATE) >> 2) & 3


# ---------------------------------------------------------------- expected payload from the original kernel
def expected_payload(kernel, adt_path, cmdline, tmp):
    strip = MAIN.parent / 'toolchains/cctools/bin/aarch64-apple-darwin-strip'
    ks = Path(tmp) / 'kernel.strip'
    subprocess.run([str(strip), str(kernel), '-o', str(ks)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sp = importlib.util.spec_from_file_location('mkpayload', WORK / 'tools/mkpayload.py'); mk = importlib.util.module_from_spec(sp); sp.loader.exec_module(mk)
    blob, info = mk.build(str(ks), str(adt_path), cmdline)
    return blob


def chosen_props(adt):
    le32 = lambda o: struct.unpack_from('<I', adt, o)[0]
    q = 8
    for _ in range(le32(0)): q += 36 + ((le32(q + 32) & 0x7fffffff) + 3 & ~3)
    cn = le32(q); q += 8; out = {}
    for _ in range(cn):
        name = adt[q:q + 32].split(b'\0')[0].decode(); ln = le32(q + 32) & 0x7fffffff
        out[name] = (q + 36, ln); q += 36 + ((ln + 3) & ~3)
    return out


def boot(img, fdt, tag, want_payload=None, receipt=None):
    """Normal boot: returns (window bytes, entry, x0) or None."""
    p = Payload(img.payload)
    win = (p.phys_base, 0x4000000)
    m = Machine(img, fdt, window=win)
    check(f'{tag}: emulated CPU starts at EL1 (the loader itself refuses MMU/D-cache on)', el1_check(m.uc) == 1)
    m.stop_at(p.entry_pa, 'XNU_ENTRY')
    stop = m.run(img.kernel_addr, img.dtb_addr)
    log = m.pmsg()
    if not check(f'{tag}: loader jumps to the XNU entry {p.entry_pa:#x}', stop == 'XNU_ENTRY', stop):
        print(log); return None
    uc = m.uc; x0 = uc.reg_read(UC_ARM64_REG_X0)
    check(f'{tag}: x0 = boot_args {p.ba_pa:#x}', x0 == p.ba_pa, hex(x0))
    check(f'{tag}: the stock/stub entry was never executed', not m.events)
    check(f'{tag}: no PMIF/TOPRGU access on the boot path', not m.pmif.log and not m.wdt_writes)
    window = bytes(uc.mem_read(*win))
    src = want_payload if want_payload is not None else img.payload
    if want_payload is not None:
        check(f'{tag}: payload in the image == mkpayload(strip(original kernel), gpu.adt, receipt command line)',
              img.payload == want_payload, f'{len(img.payload)} bytes sha256 {hashlib.sha256(img.payload).hexdigest()[:16]}')
    ps = Payload(src); ok_seg = True; total = 0; bss = 0
    for va, vs, fo, fs in ps.segs:
        o = va + ps.delta - win[0]
        ok_seg &= window[o:o + fs] == src[fo:fo + fs] and window.count(0, o + fs, o + vs) == vs - fs
        total += fs; bss += vs - fs
    check(f'{tag}: all {ps.nsegs} kernel segments in RAM equal the original kernel, BSS zero', ok_seg, f'{total} bytes copied, {bss} zeroed')
    # ADT: only the /chosen values the loader fills in differ
    ao = ps.kernel_end_pa - win[0]; adt_ram = window[ao:ao + ps.adt_size]; props = chosen_props(ps.adt)
    allowed = bytearray(len(adt_ram))
    for name in ('nord2-stock-entry', 'nord2-stock-dtb', 'nord2-xnu-region', 'nord2-conn-claim', 'nord2-wifi-dma-claim'):
        if name in props:
            o, ln = props[name]; allowed[o:o + ln] = b'\1' * ln
    diff = [i for i in range(len(adt_ram)) if adt_ram[i] != ps.adt[i]]
    check(f'{tag}: ADT in RAM = payload ADT except the loader-filled /chosen values', all(allowed[i] for i in diff), f'{len(diff)} bytes differ')
    val = lambda n: adt_ram[props[n][0]:props[n][0] + props[n][1]]
    head_off = ps.segs[0][0] - ps.virt_base
    region_end = ps.phys_base + (((ps.mem_size - head_off) >> 25) << 25)
    check(f'{tag}: /chosen nord2-stock-entry = 0x40280000, nord2-stock-dtb = FDT, nord2-xnu-region = window',
          val('nord2-stock-entry') == struct.pack('<Q', STOCK_PA) and val('nord2-stock-dtb') == struct.pack('<Q', img.dtb_addr) and
          val('nord2-xnu-region') == struct.pack('<QQ', ps.phys_base, region_end))
    check(f'{tag}: /chosen CONN and DMA claims recorded (fixed iOS claims)',
          val('nord2-conn-claim') == struct.pack('<QQII', CONN_BASE, CONN_BYTES, 3, 1) and
          val('nord2-wifi-dma-claim') == struct.pack('<QQII', POOL_BASE, POOL_BYTES, 3, 1))
    bo = ps.ba_pa - win[0]; ba = window[bo:bo + 736]
    rev, ver, virt, phys, msz, top = struct.unpack_from('<HH4x4Q', ba, 0)
    dtp, dtl = struct.unpack_from('<QI', ba, 96); cl = ba[108:108 + 608]; flags, actual = struct.unpack_from('<QQ', ba, 720)
    check(f'{tag}: boot_args (rev 2/2, virtBase, physBase, memSize, top, deviceTree VA/length, memSizeActual)',
          (rev, ver, virt, phys, msz, top, dtp, dtl, actual) ==
          (2, 2, ps.virt_base, ps.phys_base, ps.mem_size, ps.ba_pa + 0x4000, ps.kernel_end, ps.adt_size, ps.mem_size) and ba[40:88] == bytes(48),
          f'physBase {phys:#x} memSize {msz:#x} top {top:#x}')
    want_cl = receipt['commandline'].encode() if receipt else ps.cmdline.split(b'\0')[0]
    check(f'{tag}: boot_args command line = receipt ({len(want_cl)} chars)', cl.split(b'\0')[0] == want_cl and cl == ps.cmdline)
    used_end = ps.ba_pa + 0x4000 - win[0]
    first = ps.segs[0][0] + ps.delta - win[0]
    check(f'{tag}: nothing written in the window outside kernel/ADT/boot_args', window.count(0, 0, first) == first and window.count(0, used_end) == len(window) - used_end)
    ch = m.low_changes(); lo = img.kernel_addr
    okc = all((lo <= a and b <= lo + 0x10000) or (PMSG_BASE <= a and b <= PMSG_BASE + PMSG_SIZE) for a, b in ch)
    check(f'{tag}: below the window only the loader BSS/stack and the pmsg ring changed', okc, ' '.join(f'{a:#x}-{b:#x}' for a, b in ch))
    lines = [l for l in log.split('\n') if l]
    print('     early console (pmsg ring):'); [print('       ' + l) for l in lines]
    check(f'{tag}: loader log ends with JUMP TO XNU', lines[-1:] == ['NORD2-XNU LOADER: JUMP TO XNU'])
    return window[first:used_end], p.entry_pa, x0, lines


def run_failures(img, fdt):
    p = Payload(img.payload)
    def machine(**kw):
        m = Machine(img, kw.pop('fdt', fdt), window=(p.phys_base, 0x4000000), **kw); m.stop_at(p.entry_pa, 'XNU_ENTRY'); return m
    def reset_ok(tag, m, stop, armed=True):
        check(f'{tag}: ends in the TOPRGU reset, XNU never entered', stop == 'RESET', stop)
        check(f'{tag}: stub entered once with x0 = FDT', m.events == [('STOCK_ENTRY', img.dtb_addr)], str(m.events))
        mode = [v for o, v in m.wdt_writes if o == 0]
        check(f'{tag}: WDT_MODE = (old & ~IRQ) | key 0x22, then SWRST 0x1209', mode == [(0x7d & ~8) | 0x22000000] and m.wdt_writes[-1] == (0x14, 0x1209), str([(hex(o), hex(v)) for o, v in m.wdt_writes]))
        if armed:
            check(f'{tag}: RTC_PDN1 |= FAST_BOOT (0x2000) and RTC_WRTGR = 1 written through PMIF, in this order, nothing else',
                  m.pmif.writes == [(0x5b4, 0x2041), (0x5c2, 1)], str([(hex(a), hex(v)) for a, v in m.pmif.writes]))
        else:
            check(f'{tag}: PMIF stuck: nothing written to the PMIC', m.pmif.writes == [] and not any(e[0] == 'w' for e in m.pmif.log), str(m.pmif.log[:4]))
        win = bytes(m.uc.mem_read(p.phys_base, 0x4000000))
        log = m.pmsg(); tail = [l for l in log.split('\n') if l][-3:]
        check(f'{tag}: stub lines in the pmsg ring', 'NORD2 STUB: no Android kernel in this boot image' in log and
              ('fast-boot bit written' if armed else 'fast-boot bit NOT written') in log, ' | '.join(tail))
        return win
    # 1. loader fatal: payload magic damaged in RAM
    m = machine(); m.uc.mem_write(img.kernel_addr + img.payload_off, b'BROKEN!!'); stop = m.run(img.kernel_addr, img.dtb_addr)
    win = reset_ok('fail-payload', m, stop)
    check('fail-payload: loader said FATAL and the RAM window is untouched', 'LOADER FATAL: no SWEETXNU payload' in m.pmsg() and win.count(0) == len(win))
    # 2. loader fatal: FDT is not the Nord 2's
    m = machine(fdt=make_fdt(b'qcom,sm7150\0')); stop = m.run(img.kernel_addr, img.dtb_addr)
    reset_ok('fail-fdt', m, stop); check('fail-fdt: loader said FATAL: not MT6893', 'LOADER FATAL: not MT6893' in m.pmsg())
    # 3. loader fatal with PMIF not answering
    m = machine(pmif=PmifModel(stuck=True)); m.uc.mem_write(img.kernel_addr + img.payload_off, b'BROKEN!!'); stop = m.run(img.kernel_addr, img.dtb_addr)
    reset_ok('fail-pmif-stuck', m, stop, armed=False)
    # 4. XNU chain-back (nord2_leave_xnu trampoline): entry 0x40280000, x0 = dtb, SCTLR as the trampoline sets it, foreign SP/VBAR
    m = machine(); uc = m.uc
    uc.mem_write(PMSG_BASE, struct.pack('<3I', 0x43474244, 0, 19) + b'NORD2-XNU PANIC: x\n')
    uc.reg_write(UC_ARM64_REG_SP, 0xfffffe0012345670); stop = m.run(STOCK_PA, img.dtb_addr)
    reset_ok('fail-xnu-chainback', m, stop)
    check('fail-xnu-chainback: stack inside the stub image', STOCK_PA < uc.reg_read(UC_ARM64_REG_SP) <= STOCK_PA + 0x10000, hex(uc.reg_read(UC_ARM64_REG_SP)))
    # 5. full pmsg ring / no ring: the stub writes nothing there and still resets
    m = machine(); m.uc.mem_write(PMSG_BASE, struct.pack('<3I', 0x43474244, 0, PMSG_SIZE - 12)); stop = m.run(STOCK_PA, img.dtb_addr)
    check('fail-ring-full: reset, ring size unchanged', stop == 'RESET' and struct.unpack('<3I', bytes(m.uc.mem_read(PMSG_BASE, 12)))[2] == PMSG_SIZE - 12, stop)
    m = machine(); stop = m.run(STOCK_PA, img.dtb_addr)
    check('fail-no-ring: reset, ring header not created', stop == 'RESET' and bytes(m.uc.mem_read(PMSG_BASE, 12)) == bytes(12), stop)
    # 6. exception vectors of the stub: table 1 -> reset (no PMIC access), table 2 -> park (wfe loop)
    stub = img.blob[LOADER_RESERVE:LOADER_RESERVE + 0x4000]
    b0 = struct.unpack_from('<I', stub, 0)[0]; entry = (b0 & 0x3ffffff) * 4
    tables = [o for o in range(0x800, 0x4000 - 0x800 + 1, 0x800)
              if all(struct.unpack_from('<I', stub, o + 0x80 * i)[0] >> 26 == 0x05 for i in range(16))]
    check('fail-vectors: the stub has exactly two 2 KiB-aligned vector tables', len(tables) == 2, str([hex(t) for t in tables]))
    if len(tables) == 2:
        res = []
        for i in range(16):
            m = machine(); res.append((m.run(STOCK_PA + tables[0] + 0x80 * i, 0, limit=2_000_000), m.pmif.log, m.events))
        check('fail-vectors: all 16 vectors of table 1 go to the TOPRGU reset without touching PMIF', all(r == ('RESET', [], []) for r in res), str(res[0][0]))
        res = []
        for i in range(16):
            m = machine(); st = m.run(STOCK_PA + tables[1] + 0x80 * i, 0, limit=20_000); pc = m.uc.reg_read(UC_ARM64_REG_PC)
            ins = struct.unpack('<I', bytes(m.uc.mem_read(pc & ~3, 4)))[0]
            res.append(st.startswith('LIMIT') and not m.wdt_writes and not m.pmif.log and STOCK_PA + entry <= pc < STOCK_PA + tables[0] and ins in (0xd503205f, 0x17ffffff))
        check('fail-vectors: all 16 vectors of table 2 park (wfe loop), no MMIO', all(res))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image', type=Path); ap.add_argument('--kernel', type=Path); ap.add_argument('--adt', type=Path)
    ap.add_argument('--receipt', type=Path); ap.add_argument('--legacy', type=Path); ap.add_argument('--stub', type=Path)
    ap.add_argument('--save-payload', type=Path, help='write mkpayload(strip(KERNEL)) here (input of the static checker)')
    ap.add_argument('--mutate', choices=['kernel-byte', 'stub-byte', 'adt-byte'], help='self-test: damage the decompressed blob in memory; the run must FAIL')
    a = ap.parse_args()
    fdt = make_fdt(); img = Image(a.image)
    if a.mutate:
        blob = bytearray(img.blob); pm = Payload(img.payload)
        off = {'kernel-byte': img.payload_off + pm.segs[2][2] + pm.segs[2][3] // 2, 'stub-byte': LOADER_RESERVE + 0x810,
               'adt-byte': img.payload_off + pm.adt_off + pm.adt_size - 5}[a.mutate]
        blob[off] ^= 0x40; img.blob = bytes(blob); img.payload = img.blob[img.payload_off:]
        print(f'SELF-TEST: one bit flipped at blob offset {off:#x} ({a.mutate})')
    receipt = json.loads(a.receipt.read_text()) if a.receipt else None
    print(f'image {a.image.name}: {len(img.raw)} bytes, boot_spare {33554432 - len(img.raw)}, kernel blob {len(img.blob)} -> at {img.kernel_addr:#x}, '
          f'image_size {img.image_size:#x}, payload at {img.kernel_addr + img.payload_off:#x} ({len(img.payload)} bytes), layout {"nostock (stub)" if img.is_stub else "stock Android kernel"}')
    if receipt:
        check('receipt: sha256 and size of the image', receipt['sha256'] == hashlib.sha256(img.raw).hexdigest() and receipt['bytes'] == len(img.raw) and receipt['boot_spare'] == 33554432 - len(img.raw))
    check('header: kernel 0x40080000, ramdisk 0x51100000, blob below the ramdisk and the FDT address',
          img.kernel_addr == KERNEL_PA and img.ramdisk_addr == RAMDISK_PA and img.kernel_addr + len(img.blob) <= min(RAMDISK_PA, img.dtb_addr), hex(img.kernel_addr + len(img.blob)))
    if a.stub:
        st = bytearray(a.stub.read_bytes()); struct.pack_into('<Q', st, 16, img.image_size)
        check('stub in the image = stub-Image (image_size apart), rest of its image zero', img.is_stub and img.blob[LOADER_RESERVE:LOADER_RESERVE + len(st)] == bytes(st) and
              img.blob.count(0, LOADER_RESERVE + len(st), LOADER_RESERVE + img.image_size) == img.image_size - len(st))
    want = None
    if a.kernel:
        with tempfile.TemporaryDirectory(dir=str(HERE / 'out')) as tmp:
            want = expected_payload(a.kernel, a.adt, receipt['commandline'], tmp)
        check('receipt: kernel sha256', receipt['kernel'] == hashlib.sha256(a.kernel.read_bytes()).hexdigest())
        if a.save_payload:
            a.save_payload.write_bytes(want)
    r = boot(img, fdt, 'boot', want, receipt)
    if a.legacy and r and not a.mutate:
        leg = Image(a.legacy)
        check('legacy: reference image carries the Android kernel, same loader bytes, same ramdisk/DT table/appended DTBs',
              not leg.is_stub and leg.blob[:LOADER_RESERVE] == img.blob[:LOADER_RESERVE] and leg.ramdisk == img.ramdisk and leg.dtb == img.dtb and leg.appended == img.appended)
        same_payload = leg.payload == img.payload
        print(f'     legacy payload {"identical to" if same_payload else "DIFFERENT from"} this image\'s payload; payload offset {leg.payload_off:#x} vs {img.payload_off:#x}')
        check('legacy: payload at the same RAM address in both layouts', leg.payload_off == img.payload_off)
        rl = boot(leg, fdt, 'legacy')
        if rl:
            if same_payload:
                check('legacy vs nostock: RAM window byte-identical, same entry and x0', rl[0] == r[0] and rl[1:3] == r[1:3],
                      f'sha256 {hashlib.sha256(r[0]).hexdigest()[:16]} / {hashlib.sha256(rl[0]).hexdigest()[:16]}')
            d = [(x, y) for x, y in zip(rl[3], r[3]) if x != y]
            check('legacy vs nostock: the loader log differs only in the stock_image_size line', len(rl[3]) == len(r[3]) and all(x.startswith('stock_image_size=') for x, _ in d) and (len(d) == 1 or not same_payload and len(d) >= 1), str(d))
    if img.is_stub and not a.mutate:
        run_failures(img, fdt)
    print('HOST BOOT TEST: ' + ('FAILURES: ' + '; '.join(FAILS) if FAILS else 'all checks passed'))
    sys.exit(1 if FAILS else 0)


if __name__ == '__main__':
    main()
