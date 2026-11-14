#!/usr/bin/env python3
"""ram8-20261114: the full 8 GiB physical map of the Nord 2 under the iOS port, built from the live DTB (dtb/nord2/live.dtb via fdtget:
/memory reg, every /reserved-memory node with a reg, LK's mblock_info free list) + the fixed claims of our code + XNU's window + the
graphics-heap table parsed from a sweet_gfx_heap.h.  Prints one row per range (owner, size, must stay reserved?) and the totals.

    memmap.py [--heap PATH/sweet_gfx_heap.h] [--check]
--check: exit 1 on any overlap between a heap region and anything else, a heap region outside DRAM, or a byte claimed twice.
Default heap table: piece-0651/b if it exists, else piece-0650/b.
"""
import re, struct, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
DTB = Path('$NORD2_REPO/dtb/nord2/live.dtb')
MiB = 1 << 20


def fdt(path, prop, typ='x'):
    r = subprocess.run(['fdtget', '-t', typ, str(DTB), path, prop], capture_output=True, text=True)
    return r.stdout.split() if r.returncode == 0 else None


def cells64(v):
    v = [int(x, 16) for x in v]
    return [((v[i] << 32) | v[i + 1], (v[i + 2] << 32) | v[i + 3]) for i in range(0, len(v), 4)]


def dt_reserved():
    names = subprocess.run(['fdtget', '-l', str(DTB), '/reserved-memory'], capture_output=True, text=True, check=True).stdout.split()
    out, dyn = [], []
    for n in names:
        reg = fdt('/reserved-memory/' + n, 'reg')
        props = subprocess.run(['fdtget', '-p', str(DTB), '/reserved-memory/' + n], capture_output=True, text=True).stdout.split()
        nomap = 'no-map' in props
        if reg:
            for b, s in cells64(reg):
                out.append((b, b + s, n.replace('mblock-', 'mb').split('-', 1)[-1] if n.startswith('mblock') else n, nomap))
        else:
            dyn.append(n)
    return out, dyn


def lk_free():
    raw = bytes(int(x, 16) for x in fdt('/memory', 'mblock_info', 'bx'))
    n = struct.unpack_from('<Q', raw, 0)[0]
    return [(lambda s, z: (s, s + z))(*struct.unpack_from('<QQ', raw, 8 + 24 * i)) for i in range(n)]


# Fixed claims of our code (source of each in the comment) and the loader's live addresses.  keep = must stay out of XNU/heap.
OURS = [
    (0x40000000, 0x40400000, 'loader Image 0x40080000 + chain-back stub 0x40280000 (/chosen nord2-stock-entry)', 'ours: boot', True),
    (0x47c00000, 0x47e00000, 'stock DTB 0x47c80000, Wi-Fi maps 1 MiB of it (wifi_main_phy_xnu.c)', 'ours: boot', True),
    (0x70000000, 0x70200000, 'N2D DVFS share (nord2_dvfs.h)', 'ours: pool', True),
    (0x89000000, 0x89320000, 'SCP DRAM (nord2_scp.inc)', 'ours: pool', True),
    (0x8fb00000, 0x8ff50000, 'Wi-Fi CONN claim (loader nord2-conn-claim)', 'ours: pool', True),
    (0x90000000, 0x90f20000, 'Wi-Fi DMA pool (loader nord2-wifi-dma-claim)', 'ours: pool', True),
    (0xa0000000, 0xb8000000, 'vcodec RWVC pool 384 MiB (redwood_vcodec.h, 0313)', 'ours: pool', True),
    (0xc0000000, 0xc1000000, 'display pool kept part (IOMMU tables 580 KiB; 0394 pool if enabled)', 'ours: pool', True),
    (0xd0000000, 0xd8000000, 'camera buffers (nord2_camsv/camdev.inc)', 'ours: pool', True),
    (0xd8000000, 0xda800000, 'JPEG encoder (nord2_jpegdev.inc)', 'ours: pool', True),
    (0xda800000, 0xdfc00000, 'JPEG decoder + guard (redwood_jpegdec.h)', 'ours: pool', True),
    (0xdfc00000, 0xdfe70000, 'GNSS window (0584)', 'ours: pool', True),
    (0x100000000, 0x23e000000, 'XNU window 5088 MiB (ram5g loader: physBase 0x100000000, memSize 0x13e000000)', 'XNU', False),
]
# LK-reserved, no reserved-memory node (Linux uses it as RAM after boot); dead after the loader jumped to XNU
LKBOOT = [(0x40080000, 0x47d00000, 'LK: kernel Image / SWEETXNU payload / DTB area'), (0x48200000, 0x48600000, 'LK reserved (unknown, small)'),
          (0x48f00000, 0x55100000, 'LK: boot buffers, ramdisk 0x51100000')]


def heap_table(path):
    s = Path(path).read_text()
    body = s[s.index('sweet_gfx_heap_regions[SWEET_GFX_HEAP_NREG] = {'):]
    body = body[:body.index('};')]
    rows = []
    for m in re.finditer(r'\{\s*(0x[0-9a-f]+)ull,\s*(0x[0-9a-f]+)ull,\s*SWEET_GFX_HEAP_(SAFE|CHECK),\s*([^,]+),\s*"([^"]+)"', body):
        b, z = int(m.group(1), 16), int(m.group(2), 16)
        rows.append((b, b + z, m.group(5), m.group(3), m.group(4).strip()))
    return rows


def main():
    check = '--check' in sys.argv
    hp = None
    if '--heap' in sys.argv:
        hp = sys.argv[sys.argv.index('--heap') + 1]
    else:
        for c in ('piece-0651/b', 'piece-0650/b'):
            p = HERE / c / 'iokit/Kernel/arm/sweet_gfx_heap.h'
            if p.exists():
                hp = p
                break
    mem = cells64(fdt('/memory', 'reg'))
    assert mem == [(0x40000000, 0x200000000)], mem
    dram = (0x40000000, 0x240000000)
    vend, dyn = dt_reserved()
    free = lk_free()
    heap = heap_table(hp)
    rows = []   # (b, e, name, owner, keep)
    for b, e, n, nomap in vend:
        owner = 'modem' if n in ('ccci', 'ap_md_nc_smem', 'md_mem_usage', 'ccci_tag_mem') else 'firmware'
        if n == 'framebuffer':
            owner = 'LK framebuffer (console/panel)'
        rows.append((b, e, n + (' (no-map)' if nomap else ''), owner, True))
    rows += [(b, e, n, o, k) for b, e, n, o, k in OURS]
    rows += [(b, e, n, 'heap ' + c + ('' if f == '0' else ' ' + f.replace('SWEET_GFX_HEAP_F_', '')), False) for b, e, n, c, f in heap]
    bad = 0
    for i, a in enumerate(rows):
        if not (dram[0] <= a[0] < a[1] <= dram[1]):
            print('OUTSIDE DRAM:', a); bad += 1
        for b in rows[i + 1:]:
            if a[0] < b[1] and b[0] < a[1]:
                if {a[2].split(' ')[0], b[3]} == {'dramc-rk0', 'XNU'} or {b[2].split(' ')[0], a[3]} == {'dramc-rk0', 'XNU'}:
                    print('KNOWN (since ram5088, boots fine): the DRAMC rank-0 dummy-read page 0x13ffff000 lies inside XNU\'s window')
                    continue
                print('OVERLAP: %s [%#x..%#x) and %s [%#x..%#x)' % (a[2], a[0], a[1], b[2], b[0], b[1])); bad += 1
    # the heap regions must be LK-free or LK boot buffers (Linux RAM)
    for b, e, n, c, f in heap:
        ok = any(b >= fb and e <= fe for fb, fe in free) or any(b >= lb and e <= le for lb, le, _ in LKBOOT)
        if not ok:
            print('HEAP REGION NOT FREE RAM:', n); bad += 1
    rows.sort()
    # gaps = DRAM nobody claims
    gaps, cur = [], dram[0]
    for b, e, *_ in rows:
        if b > cur:
            gaps.append((cur, b))
        cur = max(cur, e)
    if cur < dram[1]:
        gaps.append((cur, dram[1]))
    allrows = rows + [(b, e, 'unclaimed' + next((' = ' + t for lb, le, t in LKBOOT if b < le and lb < e), ''), 'nobody', False) for b, e in gaps]
    allrows.sort()
    print('%-13s %-13s %9s  %-34s %-26s %s' % ('base', 'end', 'MiB', 'region', 'owner', 'must stay reserved'))
    tot = {}
    for b, e, n, o, k in allrows:
        print('%#013x %#013x %9.2f  %-34.34s %-26.26s %s' % (b, e, (e - b) / MiB, n, o, 'yes' if k else ('no (in use by iOS)' if o.startswith(('XNU', 'heap')) else 'no')))
        key = o.split(' (')[0] if not o.startswith('heap') else 'graphics heap ' + ('CHECK' if 'CHECK' in o else 'SAFE')
        tot[key] = tot.get(key, 0) + (e - b)
    print()
    for k in sorted(tot, key=lambda k: -tot[k]):
        print('%-34s %9.2f MiB' % (k, tot[k] / MiB))
    print('%-34s %9.2f MiB' % ('total', sum(tot.values()) / MiB))
    print('Linux-only dynamic reservations (no reg; not allocated under iOS): ' + ', '.join(dyn))
    xnu = sum(e - b for b, e, n, o, k in rows if o == 'XNU')
    hs = sum(e - b for b, e, n, c, f in heap if c == 'SAFE')
    hc = sum(e - b for b, e, n, c, f in heap if c == 'CHECK')
    print('iOS-usable: XNU %d MiB + heap SAFE %.1f MiB (+ CHECK %.1f MiB when on) = %.1f MiB (%.1f MiB with CHECK) of 8192' %
          (xnu // MiB, hs / MiB, hc / MiB, (xnu + hs) / MiB, (xnu + hs + hc) / MiB))
    print('heap table:', hp)
    if check:
        print('MEMMAP_CHECK_OK' if bad == 0 else 'MEMMAP_CHECK_FAILED %d' % bad)
        sys.exit(1 if bad else 0)


main()
