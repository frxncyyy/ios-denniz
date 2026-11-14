#!/usr/bin/env python3
"""Candidate overlay for the host test runners (tools/test-nord2-wifi-*.py --candidate, test/run-host-tests.sh): the files of
patched-sources-0430 and -0436 without the kernel-only wrapper lines of the .c files, plus a SOURCE MODEL of the 0436 binary
patch: shim wifi_normal_transport.c with the same four "exactly one scan command" checks removed (the kernel keeps the CI object).
usage: mk-candidate.py OUTDIR [--legacy]     (--legacy: niwj_privmac_mode=0 / niwi_scan_relax=0 defaults = pre-0430 behaviour)"""
import sys
from pathlib import Path
H=Path(__file__).resolve().parent.parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
R=next(p for p in H.parents if (p/'shim/nord2-xnu/wifi_normal_transport.c').exists() and (p/'research/nord2/wifi/calibration').exists())
for d in sorted(H.glob('patched-sources-04*')):
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
        if '--legacy' in sys.argv:
            t=t.replace('unsigned niwj_privmac_mode=1;','unsigned niwj_privmac_mode=0;').replace('unsigned niwi_scan_relax=1;','unsigned niwi_scan_relax=0;')
        (out/f.name).write_text(t)
t=(R/'shim/nord2-xnu/wifi_normal_transport.c').read_text()
for old in ("r->count!=1||r->entry[0].cid!=3||r->entry[0].bytes!=1252||r->entry[0].set_query!=1||","s->count==1&&s->entry[0].cid==3&&s->entry[0].bytes==1252&&s->entry[0].set_query==1&&"):
    assert t.count(old)==2,old; t=t.replace(old,'')
(out/'wifi_normal_transport.c').write_text(t)
print('candidate:',out,len(list(out.iterdir())),'files')

import subprocess as _sp; _sp.run(["python3", "$NORD2_WORK/gpu/build/integ-20261114/test/cand_post.py", str(out)] + "--pack-sae --8021x-off --sae0".split(), check=True)
