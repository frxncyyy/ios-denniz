#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# List VENC_WriteHW/ReadHW/PollHW call sites of libvcodecdrv.so with the constant offset/value loaded into r1/r2
# just before the call.  Input: llvm-objdump -d --no-show-raw-insn --triple=thumbv8a libvcodecdrv.so > drv.s (in /tmp).
# Usage: wsites.py drv.s [all]   (default: H.264 functions only).  Owner: opus-venc.
import re,sys
cur=None; win=[]
for line in open(sys.argv[1]):
    m=re.match(r'^([0-9a-f]+) <(.*)>:$',line)
    if m: cur=m.group(2); win=[]; continue
    m=re.match(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$',line)
    if not m: continue
    addr,op,args=m.groups()
    if op=='blx' and re.search(r'<(VENC_(Write|Read|Poll)HW|VENC_PollHWAddr)@plt>',args):
        fn=re.search(r'<(\w+)@plt>',args).group(1)
        r1=r2=None
        for a,o,g in reversed(win[-10:]):
            if r1 is None and re.match(r'(movs?|movw|mov\.w|addw)\b',o) and g.startswith('r1,'): r1=g.split(',',1)[1].strip()
            if r2 is None and re.match(r'(movs?|movw|mov\.w)\b',o) and g.startswith('r2,'): r2=g.split(',',1)[1].strip()
        if cur.startswith(('H264','h264')) or len(sys.argv)>2:
            print(f"{cur:40s} {addr:>7s} {fn:14s} off={r1} val={r2}")
    win.append((addr,op,args))
