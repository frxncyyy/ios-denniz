#!/usr/bin/env python3
"""Bounded original selected cache checks; no Apple execution or device IO."""
from pathlib import Path
import runpy,re,json,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').exists());B=P.parent
C=runpy.run_path(str(B/'cache_view.py'))['Cache'](R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
assert hashlib.sha256(C.read(0x1d3eb9000,344064)).hexdigest()=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
D=B/'IO80211-disassembly.txt';assert hashlib.sha256(D.read_bytes()).hexdigest()=='9945310dce64768e03c7a28a05bcd13a8e90742576f251681166e6af99041a37'
ranges=[(0x1d3ebff54,0x1d3ebffcc),(0x1d3ec0a64,0x1d3ec0ad4),(0x1d3ec0bb0,0x1d3ec0c38),(0x1d3ec1744,0x1d3ec177c),(0x1d3ed7e90,0x1d3ed8008),(0x1d3eda808,0x1d3edaae8)]
rows=[];inst={}
for line in D.read_text().splitlines():
 m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+(.*)$',line)
 if m and any(a<=int(m[1],16)<z for a,z in ranges):
  va=int(m[1],16);assert C.read(va,4)==bytes.fromhex(m[2]);rows.append(line);inst[va]=m[3]
for a,s in {0x1d3eda824:'cmp\tw2, #32',0x1d3eda890:'x24, #4',0x1d3eda8c4:'x24, #16',0x1d3eda8f0:'x24, #20',0x1d3eda91c:'x24, #24',0x1d3ed7f9c:'0x1d3eda808',0x1d3ec0c30:'str\twzr, [sp, #1040]'}.items():assert s in inst[a],(hex(a),s,inst[a])
t=C.read(0x1d3ef5a4e,9);assert 0x1d3ed7cd4+4*t[2]==0x1d3ed7f64
(P/'consumer-excerpt.txt').write_text('\n'.join(rows)+'\n')
refs=[B/'station-wire-candidate/ios-association/manifest.json',B/'station-wire-candidate/ios-association-followup/manifest.json',R/'research/reference/oosexgang/hackios-xnu-kernel/xnu/files/bsd/dev/redwood_apple80211.c']
record={'method':'Original same-IPSW cache bytes and pinned source; static field projections, no executed Apple CF builder or hardware join','verified_instructions':len(rows),'event4':{'dispatch':'0x1d3ed7f64->0x1d3eda808','min_primary':32,'down':0,'rssi':4,'snr':8,'noise':10,'involuntary':16,'reason':20,'subreason':24},'PMK_policy':{'flags0':'producer ready64hex clears504 at0x1d3ec0c30','flags4':'producer common passphrase retains4; port explicitly implements equivalent WPA2-PSK/CCMP host negotiation; proprietary native offload meaning is not claimed','bit30':'unsupported; CARD75 not advertised'},'event9':'zero-primary48B terminal only; immutable followup proves selected scratch initialization+GET21 fallback','references':{str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in refs}}
(P/'source-evidence.json').write_text(json.dumps(record,indent=2)+'\n');print('PASS original selected consumer/producer',len(rows),'instructions')
