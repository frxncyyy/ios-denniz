#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mix_report.py -- tables from an n2perf_mix.c dump (opus-perf).

    python3 -I mix_report.py MIX.txt [--obj NAME:BASEHEX:PATH ...] [--top N] [--phase P]

Per phase: total guest instructions, share executed by a predecoded fast path, the top mnemonics (llvm-mc /
llvm-objdump as the disassembler oracle), the top guest functions (exclusive counts, from the dynamic symbols
of the objects named with --obj) and the host calls.  Reads only; writes nothing but stdout (temporary
assembler files go to a private temporary directory).
"""
import argparse, collections, os, re, subprocess, sys, tempfile, bisect


def disasm(raws):
    """raws: set of (thumb, raw) -> {key: mnemonic text}"""
    out = {}
    with tempfile.TemporaryDirectory(prefix='n2perf-') as td:
        for thumb in (0, 1):
            items = sorted(r for t, r in raws if t == thumb)
            if not items:
                continue
            lines = ['.syntax unified', '.thumb' if thumb else '.arm', '.text']
            for r in items:
                if not thumb:
                    lines.append('.inst 0x%08x' % r)
                elif r >= 0xE8000000:
                    lines.append('.inst.w 0x%08x' % r)
                else:
                    lines.append('.inst.n 0x%04x' % r)
            s = os.path.join(td, 'x.s')
            o = os.path.join(td, 'x.o')
            with open(s, 'w') as f:
                f.write('\n'.join(lines) + '\n')
            subprocess.run(['llvm-mc', '--triple=%s' % ('thumbv7a-linux-gnueabi' if thumb else 'armv7a-linux-gnueabi'),
                            '-mattr=+neon,+vfp4', '-filetype=obj', s, '-o', o], check=True)
            d = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '--mattr=+neon,+vfp4',
                                '--triple=%s' % ('thumbv7a-linux-gnueabi' if thumb else 'armv7a-linux-gnueabi'), o],
                               capture_output=True, text=True).stdout
            texts = []
            for line in d.splitlines():
                m = re.match(r'^\s*([0-9a-f]+):\s+(.*)$', line)
                if m:
                    texts.append(m.group(2).strip())
            for r, t in zip(items, texts):
                out[(thumb, r)] = t
    return out


def mnemonic(text):
    if not text:
        return '?'
    m = text.split()[0]
    m = re.sub(r'\.w$', '', m)
    return m


def load_syms(objs):
    syms = []
    for spec in objs:
        name, base, path = spec.split(':', 2)
        base = int(base, 16)
        out = subprocess.run(['llvm-nm', '-D', '--defined-only', '-n', '-S', path], capture_output=True, text=True).stdout
        for line in out.splitlines():
            p = line.split()
            if len(p) >= 4 and p[2] in 'TtWw':
                syms.append((base + (int(p[0], 16) & ~1), int(p[1], 16), '%s:%s' % (name, p[3])))
        syms.append((base, 0, '%s:?' % name))
    syms.sort()
    return syms


def where(syms, keys, pc):
    i = bisect.bisect_right(keys, pc) - 1
    if i < 0:
        return '?'
    a, sz, n = syms[i]
    if sz and pc >= a + sz:
        return n.split(':')[0] + ':(no symbol)'
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mix')
    ap.add_argument('--obj', action='append', default=[])
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--phase', type=int, default=None)
    ap.add_argument('--exclude-obj', action='append', default=[],
                    help='also report each phase without the pcs of this object (e.g. vpud: its log poll loop)')
    a = ap.parse_args()

    names = {}
    ins = collections.defaultdict(collections.Counter)
    fast = collections.defaultdict(int)
    pcs = collections.defaultdict(collections.Counter)
    hcs = collections.defaultdict(collections.Counter)
    mmio = {}
    with open(a.mix) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == 'M':
                names[int(p[1])] = ' '.join(p[2:])
            elif p[0] == 'I':
                ph, th, raw, n, op = int(p[1]), int(p[2]), int(p[3], 16), int(p[4]), int(p[5])
                ins[ph][(th, raw)] += n
                if op:
                    fast[ph] += n
            elif p[0] == 'P':
                pcs[int(p[1])][int(p[2], 16)] += int(p[3])
            elif p[0] == 'H':
                hcs[int(p[1])][p[2]] += int(p[3])
            elif p[0] == 'X':
                mmio[int(p[1])] = (int(p[2]), int(p[3]))
    allraw = set()
    for c in ins.values():
        allraw |= set(c)
    dis = disasm(allraw)
    syms = load_syms(a.obj) if a.obj else []
    keys = [s[0] for s in syms]

    phases = sorted(set(ins) | set(names))
    if a.phase is not None:
        phases = [a.phase]
    grand = sum(sum(c.values()) for c in ins.values())
    print('total guest instructions (all phases): %d' % grand)
    for ph in phases:
        c = ins.get(ph, collections.Counter())
        tot = sum(c.values())
        print('\n=== phase %d %s: %d guest instructions, %.1f%% via fast path, %d distinct encodings, '
              'mmio r/w %s' % (ph, names.get(ph, '?'), tot, 100.0 * fast[ph] / tot if tot else 0, len(c),
                               mmio.get(ph, (0, 0))))
        if not tot:
            continue
        isa = collections.Counter()
        for (th, raw), n in c.items():
            isa['A32' if not th else ('T32' if raw >= 0xE8000000 else 'T16')] += n
        print('  ISA: ' + ', '.join('%s %.1f%%' % (k, 100.0 * v / tot) for k, v in isa.most_common()))
        mn = collections.Counter()
        for k, n in c.items():
            mn[mnemonic(dis.get(k))] += n
        print('  top mnemonics:')
        for m, n in mn.most_common(a.top):
            print('    %-12s %12d %5.1f%%' % (m, n, 100.0 * n / tot))
        cls = collections.Counter()
        for k, n in c.items():
            t = dis.get(k, '')
            m = mnemonic(t)
            if re.match(r'^v(ld|st)[1-4]', m) or re.search(r'\s[qd]\d', t) and not re.match(r'^v(ldr|str|ldm|stm|push|pop|mov|mrs|msr)', m):
                cls['NEON'] += n
            elif m.startswith('v'):
                cls['VFP'] += n
            elif re.match(r'^(ldr|str|ldm|stm|push|pop|ldrex|strex|tbb|tbh|pld)', m):
                cls['load/store'] += n
            elif re.match(r'^(b|bl|blx|bx|cbz|cbnz)(eq|ne|hs|lo|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?$', m):
                cls['branch'] += n
            else:
                cls['integer/other'] += n
        print('  classes: ' + ', '.join('%s %.1f%%' % (k, 100.0 * v / tot) for k, v in cls.most_common()))
        slow = collections.Counter()
        fastset = set()
        with open(a.mix) as f:
            for line in f:
                p = line.split()
                if p and p[0] == 'I' and int(p[1]) == ph and int(p[5]):
                    fastset.add((int(p[2]), int(p[3], 16)))
        for k, n in c.items():
            if k not in fastset:
                slow[dis.get(k, '?')] += n
        print('  top encodings still on the class path:')
        for t, n in slow.most_common(12):
            print('    %-40s %10d %5.1f%%' % (t[:40], n, 100.0 * n / tot))
        if syms and a.exclude_obj:
            ex = sum(n for pc, n in pcs[ph].items() if where(syms, keys, pc).split(':')[0] in a.exclude_obj)
            print('  without %s: %d guest instructions' % ('+'.join(a.exclude_obj), tot - ex))
        if syms:
            fn = collections.Counter()
            for pc, n in pcs[ph].items():
                fn[where(syms, keys, pc)] += n
            print('  top guest functions (exclusive):')
            for m, n in fn.most_common(a.top):
                print('    %-60s %12d %5.1f%%' % (m[:60], n, 100.0 * n / tot))
        if hcs[ph]:
            print('  host calls: ' + ', '.join('%s %d' % kv for kv in hcs[ph].most_common(15)))


if __name__ == '__main__':
    main()
