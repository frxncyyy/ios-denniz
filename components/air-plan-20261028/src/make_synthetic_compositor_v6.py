#!/usr/bin/env python3
"""Build a host-only TGR6 package with qualified metadata and opaque zero code."""
from pathlib import Path
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: make_synthetic_compositor_v6.py OUTPUT")
out = Path(sys.argv[1])
size = 1664 + 1152 + 7424
p = bytearray(size)
def w(off, value): struct.pack_into('<I', p, off, value)
def words(off, values):
    for i, value in enumerate(values): w(off + 4 * i, value)

# Header and stage metadata; the code ranges remain synthetic opaque zeros.
words(0, [0x31524754, 6, 0xb8a21030, 80, 1152, 7424, 8, 76])
words(32, [32, 34, 3, 0, 0, 0x38000000, 8, 0])
words(64, [64, 51, 8, 0, 0, 0x30000000, 76, 0])
words(96, [3, 2, 512, 1024, 32, 0, 0x38000000, 8])
# Blend, sampled texture, reflection classes and attachment metadata.
words(128, [1, 0, 1, 1, 0, 1, 1, 0, 15])
words(192, [1, 16, 3, 0, 0, 4, 1])
words(256, [1, 1, 0, 4, 1, 1, 4, 4, 1, 1, 2, 3, 2, 1, 6, 3,
            0, 64, 16, 72, 2, 0, 4, 64, 68, 72])
words(384, [80, 15, 80, 0])
words(768, [0, 0, 109, 0, 32, 0, 1,
            1, 0, 103, 16, 32, 0, 1])
out.write_bytes(p)
print(f"synthetic TGR6 metadata package: {size} bytes, zero opaque shader data")
