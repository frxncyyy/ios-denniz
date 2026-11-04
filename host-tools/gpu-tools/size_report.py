#!/usr/bin/env python3
"""Report Mach-O section sizes for the new kernel objects (linked estimate)."""
import argparse
import glob
import os
import struct
import sys

def sections(path):
    d = open(path, 'rb').read()
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from('<IiiIIIII', d, 0)
    if magic != 0xfeedfacf:
        return {}
    off = 32
    out = {}
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', d, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            nsects = struct.unpack_from('<I', d, off + 64)[0]
            so = off + 72
            for _ in range(nsects):
                seg = d[so + 16:so + 32].rstrip(b'\0').decode()
                sect = d[so:so + 16].rstrip(b'\0').decode()
                sz = struct.unpack_from('<Q', d, so + 40)[0]
                out[(seg, sect)] = sz
                so += 80
        off += size
    return out

root = os.path.join(os.path.dirname(__file__), '..')
p = argparse.ArgumentParser()
p.add_argument('--dir', default=os.path.join(root, 'build/objs'))
args = p.parse_args()
paths = sorted(glob.glob(os.path.join(args.dir, 'nord2_gpu_*.o')) +
               [os.path.join(args.dir, 'Nord2GPU.o')])
total_text = total_const = total_str = 0
for p in paths:
    if not os.path.exists(p):
        continue
    s = sections(p)
    text = s.get(('__TEXT', '__text'), 0)
    const = sum(v for (seg, sect), v in s.items()
                if sect in ('__const', '__cstring', '__literal16') and
                seg.startswith('__'))
    total_text += text
    total_const += const
    print(f'{os.path.basename(p):24s} text={text:6d} const/str={const:6d}')
print(f'{"TOTAL":24s} text={total_text:6d} const/str={total_const:6d} '
      f'sum={total_text + total_const:6d}')
print('Note: final linked size depends on dead-strip, alignment, -Os and the '
      'full kernel link; this is an object-level estimate.')
