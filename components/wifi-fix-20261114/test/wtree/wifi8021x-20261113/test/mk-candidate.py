#!/usr/bin/env python3
"""Candidate overlay for the host runners: bsd/dev files of wifi-20261113/patched-sources-0430 (q10jgw2) and of
patched-sources-0450 and -0452 (this directory's parent), .c files without the kernel-only wrapper lines.
usage: mk-candidate.py OUTDIR [--off]      (--off: default kern.n2wifi_8021x=0, the behaviour before 0450)"""
import sys
from pathlib import Path
H=Path(__file__).resolve().parent.parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
dirs=sorted((H.parent/'wifi-20261113').glob('patched-sources-0430'))+sorted(H.glob('patched-sources-045[02]'));assert len(dirs)==3,dirs
for d in dirs:
    for f in sorted((d/'bsd/dev').iterdir()):
        t=f.read_text()
        if f.suffix=='.c':
            l=t.split('\n')
            assert l[0]=='#include <arm64/sweet_early.h>' and l[1]=='#if defined(NORD2)',f
            while l and l[-1]=='':l.pop()
            assert l[-1]=='#endif /* NORD2 */',f
            l=l[2:-1]
            while l and l[-1]=='':l.pop()
            t='\n'.join(l)+'\n'
        if '--off' in sys.argv and f.name=='wifi_ios_join.c' and d.name=='patched-sources-0452':
            assert t.count('unsigned niwj_8021x=0x11,niwj_ap_mfpr;')==1;t=t.replace('unsigned niwj_8021x=0x11,niwj_ap_mfpr;','unsigned niwj_8021x=0,niwj_ap_mfpr;')
        (out/f.name).write_text(t)
print('candidate:',out,len(list(out.iterdir())),'files')

import subprocess as _sp; _sp.run(["python3", "$NORD2_WORK/gpu/build/integ-20261114/test/cand_post.py", str(out)] + "--pack-sae".split(), check=True)
