#!/usr/bin/env python3
"""integ-20261114 host-test glue, run at the end of the wtree copies of the sets' mk-candidate.py:  cand_post.py CANDDIR [--pack-sae] [--8021x-off]
--pack-sae   the SAE set's host packaging (its mk-candidate.py): the merged wifi_station_transaction.c needs wifi_station_wire2/wpa3/sae/sae_crypto
             and two BearSSL AES units, and the repo fixtures compile a fixed file list, so those units are appended to it.
--sae0       kern.n2wifi_sae=0 default (the 0430/0436 suites predate SAE: "0 = the 0430 decoder and selection")
--8021x-off  kern.n2wifi_8021x=0 default (the 0430/0434/0436 suites assert that an 802.1X request is refused, as before 0450)."""
import sys
from pathlib import Path
out = Path(sys.argv[1])
if '--pack-sae' in sys.argv:
    t = (out/'wifi_station_transaction.c').read_text()
    if 'host packaging: wifi_wpa3.c' not in t:
        t += '\n#undef X\n#undef W\n'
        for n in ('wifi_station_wire2.c', 'wifi_wpa3.c', 'wifi_sae.c', 'wifi_sae_crypto.c'):
            t += '\n/* host packaging: ' + n + ' */\n' + (out/n).read_text(); (out/n).unlink()
        t += '\n/* host packaging: BearSSL AES encryption units of the kernel archive */\n#include "symcipher/aes_ct_enc.c"\n#include "symcipher/aes_ct_cbcenc.c"\n'
        (out/'wifi_station_transaction.c').write_text(t)
if '--8021x-off' in sys.argv:
    p = out/'wifi_ios_join.c'; t = p.read_text(); o = 'unsigned niwj_8021x=0x11,niwj_ap_mfpr;'
    assert t.count(o) == 1; p.write_text(t.replace(o, 'unsigned niwj_8021x=0,niwj_ap_mfpr;'))
if '--sae0' in sys.argv:
    p = out/'wifi_ios_join.c'; t = p.read_text(); o = 'unsigned niwj_sae_mode=1,'
    assert t.count(o) == 1; p.write_text(t.replace(o, 'unsigned niwj_sae_mode=0,'))
print('cand_post:', out, ' '.join(sys.argv[2:]))
