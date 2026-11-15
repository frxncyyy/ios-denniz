#!/usr/bin/env python3
"""Candidate overlay for tools/test-nord2-wifi-ios-client.py from a built stage (q10jmdp15): stage wifi_* sources without the
kernel-only wrapper lines (#include <arm64/sweet_early.h> / #if defined(NORD2) ... #endif /* NORD2 */).
usage: mk-cand.py STAGE OUTDIR [EXTRA_SOURCE...]"""
import sys
from pathlib import Path
st=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
files=sorted((st/'bsd/dev').glob('wifi_*'))+[st/'iokit/Kernel/arm/wifi_ios_client.cpp']+[Path(x) for x in sys.argv[3:]]
n=0
for f in files:
    if f.suffix not in ('.c','.h','.inc','.cpp'):continue
    t=f.read_text();l=t.split('\n')
    if len(l)>2 and l[0]=='#include <arm64/sweet_early.h>' and l[1]=='#if defined(NORD2)':
        while l and l[-1]=='':l.pop()
        assert l[-1].startswith('#endif'),f
        l=l[2:-1];t='\n'.join(l)+'\n'
    (out/f.name).write_text(t);n+=1
print('candidate:',out,n,'files')
