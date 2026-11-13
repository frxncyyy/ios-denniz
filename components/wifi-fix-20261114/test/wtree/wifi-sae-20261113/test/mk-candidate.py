#!/usr/bin/env python3
"""Candidate overlay for the host test runners (tools/test-nord2-wifi-*.py --candidate, test/run-host-tests.sh):
the files of wifi-20261113/patched-sources-0430 (q10jgw2) and of this directory's patched-sources-0432..0434 without
the kernel-only wrapper lines of the .c files.
Host packaging only (the kernel links separate objects): the repo runners compile a fixed file list, so the four new
units (wifi_station_wire2.c, wifi_wpa3.c, wifi_sae.c, wifi_sae_crypto.c) and the two BearSSL AES encryption units of
the kernel archive (aes_ct_enc.c, aes_ct_cbcenc.c) are appended to wifi_station_transaction.c.
usage: mk-candidate.py OUTDIR [--legacy] [--sae0]
  --legacy: defaults niwj_privmac_mode=0 / niwi_scan_relax=0 (the pre-0430 behaviour) and niwj_sae_mode=0
  --sae0:   default niwj_sae_mode=0 (kern.n2wifi_sae=0, the pre-0432 behaviour)"""
import sys
from pathlib import Path
H=Path(__file__).resolve().parent.parent;P=H.parent/'wifi-20261113';out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def strip(f):
    t=f.read_text()
    if f.suffix=='.c':
        l=t.split('\n')
        assert l[0]=='#include <arm64/sweet_early.h>' and l[1]=='#if defined(NORD2)',f
        while l and l[-1]=='':l.pop()
        assert l[-1]=='#endif /* NORD2 */',f
        l=l[2:-1]
        while l and l[-1]=='':l.pop()
        t='\n'.join(l)+'\n'
    return t
files={}
for d in [P/'patched-sources-0430']+sorted(H.glob('patched-sources-04*')):
    for f in sorted((d/'bsd/dev').iterdir()):files[f.name]=strip(f)
t=files['wifi_station_transaction.c']+'\n#undef X\n#undef W\n'
for n in ('wifi_station_wire2.c','wifi_wpa3.c','wifi_sae.c','wifi_sae_crypto.c'):t+='\n/* host packaging: '+n+' */\n'+files.pop(n)
files['wifi_station_transaction.c']=t+'\n/* host packaging: BearSSL AES encryption units of the kernel archive */\n#include "symcipher/aes_ct_enc.c"\n#include "symcipher/aes_ct_cbcenc.c"\n'
for n,t in files.items():
    if '--legacy' in sys.argv:
        t=t.replace('unsigned niwj_privmac_mode=1;','unsigned niwj_privmac_mode=0;').replace('unsigned niwi_scan_relax=1;','unsigned niwi_scan_relax=0;')
    if '--sae0' in sys.argv or '--legacy' in sys.argv:
        t=t.replace('unsigned niwj_sae_mode=1,','unsigned niwj_sae_mode=0,')
    (out/n).write_text(t)
print('candidate:',out,len(files),'files')
