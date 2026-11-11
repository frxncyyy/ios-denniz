#!/usr/bin/env python3
"""Verify the generated Nord 2 ADT exposes the Mali node correctly."""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import adt  # noqa: E402

with tempfile.TemporaryDirectory() as td:
    out = Path(td) / 'nord2.adt'
    subprocess.run([sys.executable, str(ROOT / 'tools/mkadt-nord2.py'),
                    str(out)], check=True, capture_output=True)
    root = adt.parse(out.read_bytes())[0]

def find(node, name):
    if node.name == name:
        return node
    for child in node.children:
        got = find(child, name)
        if got:
            return got
    return None

def prop(node, name):
    for k, _, v in node.props:
        if k == name:
            return v
    raise SystemExit(f'missing {name}')

mali = find(root, 'mali')
if not mali:
    raise SystemExit('mali node missing')
reg = struct.unpack('<QQ', prop(mali, 'reg'))
assert reg == (0x12000000, 0x4000), reg
irq = struct.unpack('<5I', prop(mali, 'interrupts'))
assert irq == (362, 363, 364, 365, 366), irq
gic = find(root, 'gic')
assert prop(mali, 'interrupt-parent') == prop(gic, 'AAPL,phandle')
assert prop(gic, 'interrupt-controller') == b''
assert struct.unpack('<I', prop(gic, '#interrupt-cells')) == (1,)
assert struct.unpack('<I', prop(gic, '#address-cells')) == (0,)
comp = prop(mali, 'compatible')
assert b'mediatek,mali' in comp and b'arm,mali-valhall' in comp
assert struct.unpack('<QQ', prop(mali, 'mediatek,mfgcfg')) == (0x13fbf000, 0x1000)
# Vendor mt6893.dts: apmixedsys_clk is syscon@1000C000; 10001000 is
# infracfg, not the PLL bank. The Mali node does not declare DMA coherence.
assert struct.unpack('<QQ', prop(mali, 'mediatek,apmixedsys')) == (0x1000c000, 0x1000)
assert not any(k == 'dma-coherent' for k, _, _ in mali.props)
print('Nord 2 ADT Mali node PASS')
