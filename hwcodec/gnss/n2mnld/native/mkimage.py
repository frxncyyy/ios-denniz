#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# mkimage.py -- flatten MediaTek libmnl.so (AArch64 bionic ELF) for the native iOS runtime n2mnld.
# usage: mkimage.py LIBMNL.so OUTDIR
# writes OUTDIR/mnl_text.bin  = ELF vaddr [0, TEXT_END)   (R + RX LOAD segments, gaps zero)  -> __TEXT,__mnl
#        OUTDIR/mnl_data.bin  = ELF vaddr [DATA_START, DATA_END) (RELRO + RW + bss as zeros)  -> __DATA,__mnl
#        OUTDIR/mnl_tab.h     = layout constants, expanded RELR offsets, JUMP_SLOT table, export offsets, import names
# Relocations are resolved at run time by n2mnld (n2_mnl_load): RELR += base, JUMP_SLOT -> shim or base + value.
import hashlib, struct, sys, os

PT_LOAD, PT_DYNAMIC = 1, 2
DT_RELR_A, DT_RELRSZ_A, DT_RELR, DT_RELRSZ = 0x6fffe000, 0x6fffe001, 36, 35
DT_JMPREL, DT_PLTRELSZ, DT_SYMTAB, DT_STRTAB = 23, 2, 6, 5
DT_RELA, DT_RELASZ, DT_REL, DT_TEXTREL, DT_INIT, DT_INIT_ARRAY = 7, 8, 17, 22, 12, 25
R_JUMP_SLOT = 1026
EXPORTS = ['mtk_gps_sys_function_register', 'mtk_gps_mnl_run', 'mtk_gps_mnl_stop', 'mtk_gps_dsp_init',
           'mtk_gps_inject_ntp_time', 'mtk_gps_inject_nlp_location']


def main():
    lib, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    e = open(lib, 'rb').read()
    assert e[:4] == b'\x7fELF' and e[4] == 2 and struct.unpack_from('<H', e, 18)[0] == 183, 'not ELF64 AArch64'
    phoff, = struct.unpack_from('<Q', e, 32)
    phentsz, phnum = struct.unpack_from('<HH', e, 54)
    loads, dyn = [], None
    for i in range(phnum):
        p_type, p_flags, off, va, _, fsz, msz, _ = struct.unpack_from('<IIQQQQQQ', e, phoff + i * phentsz)
        if p_type == PT_LOAD:
            loads.append((va, off, fsz, msz, p_flags))
        elif p_type == PT_DYNAMIC:
            dyn = (off, fsz)
    loads.sort()
    rw = [l for l in loads if l[4] & 2]
    ro = [l for l in loads if not l[4] & 2]
    assert ro and rw and max(l[0] + l[3] for l in ro) <= rw[0][0], 'unexpected segment order'
    text_end = max(l[0] + l[3] for l in ro)
    data_start = rw[0][0]
    data_end = max(l[0] + l[3] for l in rw)
    assert data_start % 0x4000 == 0x2000, 'data start must be 0x2000 mod 16K (text placed at __TEXT+0x2000)'

    def flat(lo, hi, segs):
        img = bytearray(hi - lo)
        for va, off, fsz, msz, _ in segs:
            img[va - lo:va - lo + fsz] = e[off:off + fsz]
        return img
    text = flat(0, data_start, ro)            # pad up to data_start so __DATA lands right after (distance check)
    data = flat(data_start, data_end, rw)
    # gnssd1b crash (2026-11-12): bionic's stack protector reads the canary as `mrs Xt, TPIDR_EL0 ; ldr [Xt, #0x28]`
    # (TLS slot 5). On iOS arm64 TPIDR_EL0 holds the CPU NUMBER (x21 = 5 -> fault at 0x2d in mtk_gps_mnl_run's body
    # 0x4126e0). Rewrite every such read to `mrs Xt, TPIDRRO_EL0` (op2 2 -> 3, bit 5): the Darwin per-thread TSD base,
    # whose slot 5 at +0x28 is a stable per-thread word, so the prologue/epilogue canary compare holds. All 858 reads in
    # libmnl are canary reads (no msr, no other offsets; checked on the disassembly).
    ntp = 0
    for va, off, fsz, msz, fl in ro:
        if fl & 1:                                 # PF_X
            for i in range(va & ~3, va + fsz - 3, 4):
                w, = struct.unpack_from('<I', text, i)
                if w & ~0x1f == 0xd53bd040:        # mrs Xt, TPIDR_EL0
                    struct.pack_into('<I', text, i, w | 0x20); ntp += 1
    assert ntp == 858, 'TPIDR_EL0 patch count %d (want 858)' % ntp
    print('patched %d mrs TPIDR_EL0 -> TPIDRRO_EL0' % ntp)

    def rd(va, n):                             # read n bytes at ELF vaddr va (from the file image)
        for v, off, fsz, msz, _ in loads:
            if v <= va < v + fsz:
                return e[off + va - v:off + va - v + n]
        raise ValueError(hex(va))
    d = {}
    for k in range(dyn[1] // 16):
        tag, val = struct.unpack_from('<qQ', e, dyn[0] + 16 * k)
        if tag == 0:
            break
        d.setdefault(tag, val)
    for bad in (DT_RELA, DT_REL, DT_TEXTREL, DT_INIT, DT_INIT_ARRAY):
        assert bad not in d or (bad == DT_RELA and d.get(DT_RELASZ, 0) == 0), 'unsupported dynamic tag %d' % bad
    # RELR (Android or standard tag)
    relr_va, relr_sz = d.get(DT_RELR_A, d.get(DT_RELR)), d.get(DT_RELRSZ_A, d.get(DT_RELRSZ, 0))
    relr = []
    raw = rd(relr_va, relr_sz) if relr_sz else b''
    where = 0
    for (w,) in struct.iter_unpack('<Q', raw):
        if w & 1 == 0:
            relr.append(w); where = w + 8
        else:
            for b in range(63):
                if (w >> (b + 1)) & 1:
                    relr.append(where + 8 * b)
            where += 63 * 8
    for o in relr:
        assert data_start <= o < data_end, 'RELR outside the data image: %#x' % o
    symtab, strtab = d[DT_SYMTAB], d[DT_STRTAB]

    def cstr(va):
        s = bytearray()
        while True:
            c = rd(va + len(s), 1)[0]
            if c == 0:
                return s.decode()
            s.append(c)
    imports, jumps, selfb = [], [], []
    jr = rd(d[DT_JMPREL], d[DT_PLTRELSZ])
    for k in range(len(jr) // 24):
        r_off, r_info, r_add = struct.unpack_from('<QQq', jr, 24 * k)
        assert r_info & 0xffffffff == R_JUMP_SLOT and r_add == 0, 'unexpected PLT reloc'
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from('<IBBHQQ', rd(symtab + 24 * (r_info >> 32), 24))
        name = cstr(strtab + st_name)
        assert data_start <= r_off < data_end
        if st_shndx != 0:
            selfb.append((r_off, st_value, name))         # preemptible export bound to libmnl itself
        else:
            if name not in imports:
                imports.append(name)
            jumps.append((r_off, imports.index(name)))
    exports = {}
    nsym = (min(v for v in (strtab, d.get(0x6ffffef5, 1 << 62), d.get(0x6ffffff0, 1 << 62)) if v > symtab) - symtab) // 24
    for k in range(nsym):
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from('<IBBHQQ', rd(symtab + 24 * k, 24))
        if st_shndx and st_name:
            n = cstr(strtab + st_name)
            if n in EXPORTS:
                exports[n] = st_value
    open(os.path.join(out, 'mnl_text.bin'), 'wb').write(text)
    open(os.path.join(out, 'mnl_data.bin'), 'wb').write(data)
    sha = hashlib.sha256(e).hexdigest()
    h = ['/* generated by mkimage.py from libmnl.so sha256 %s -- do not edit */' % sha,
         '#define N2MNL_SHA256 "%s"' % sha,
         '#define N2MNL_TPIDR_PATCHED %d' % ntp,
         '#define N2MNL_DATA_START 0x%xu' % data_start, '#define N2MNL_DATA_END 0x%xu' % data_end,
         '#define N2MNL_TEXT_END 0x%xu' % text_end]
    for n in EXPORTS:
        h.append('#define N2MNL_EXP_%s 0x%xu' % (n, exports[n]))
    h.append('static const uint32_t n2mnl_relr[%d] = {' % len(relr))
    h += ['\t' + ', '.join('0x%x' % o for o in relr[i:i + 8]) + ',' for i in range(0, len(relr), 8)]
    h.append('};')
    h.append('static const char *const n2mnl_imports[%d] = {' % len(imports))
    h += ['\t"%s",' % n for n in imports]
    h.append('};')
    h.append('static const struct { uint32_t got; uint16_t imp; } n2mnl_jumps[%d] = {' % len(jumps))
    h += ['\t{0x%x, %d},' % j for j in jumps]
    h.append('};')
    h.append('static const struct { uint32_t got; uint32_t value; } n2mnl_selfb[%d] = {' % max(1, len(selfb)))
    h += ['\t{0x%x, 0x%x}, /* %s */' % s for s in selfb] or ['\t{0, 0},']
    h.append('};')
    h.append('#define N2MNL_NSELF %d' % len(selfb))
    open(os.path.join(out, 'mnl_tab.h'), 'w').write('\n'.join(h) + '\n')
    print('MKIMAGE OK text %#x data %#x..%#x relr %d imports %d jumps %d self %d sha %s' %
          (len(text), data_start, data_end, len(relr), len(imports), len(jumps), len(selfb), sha[:16]))


main()
