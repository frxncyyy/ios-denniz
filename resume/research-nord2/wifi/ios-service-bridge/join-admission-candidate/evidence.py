#!/usr/bin/env python3
"""Check bounded selected Apple instruction bytes/tables in the original cache."""
from pathlib import Path
import runpy,re,struct,json,hashlib
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if(x/'AGENTS.md').exists());B=P.parent;C=runpy.run_path(str(B/'cache_view.py'))['Cache'](R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(C.read(0x1d3eb9000,344064))=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
ranges=[('RSN_caps_and_trailer',0x1d3ec1380,0x1d3ec13f8),('RSN_trailer_length',0x1d3ec1548,0x1d3ec15c4),('RawSet_error_and_syncwait',0x1d3ec14b4,0x1d3ec1520),('GET21_poll',0x1d3ed66f0,0x1d3ed6874),('AKM6_gate',0x1d3ec0658,0x1d3ec08c8),('AKM_final_selector',0x1d3ec08dc,0x1d3ec0910),('AKM6_auth_upper',0x1d3ec09cc,0x1d3ec09d4)]
rows=[];proof=[];ins={}
for name,lo,hi in ranges:
 part=[]
 for l in(B/'IO80211-disassembly.txt').read_text().splitlines():
  m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+(.*)$',l)
  if m and lo<=int(m[1],16)<hi:
   a=int(m[1],16);assert C.read(a,4)==bytes.fromhex(m[2]);ins[a]=m[3];part.append(l)
 assert len(part)==(hi-lo)//4;(rows.extend(['# '+name]+part));proof.append(dict(name=name,start=hex(lo),end=hex(hi),sha256=sha(C.read(lo,hi-lo))))
for a,x in {0x1d3ed66f0:'mov\tw20, #400',0x1d3ed66f4:'mov\tw0, #20000',0x1d3ec13c4:'strh\twzr',0x1d3ec1594:'stur\tw8',0x1d3ec15b4:'strb\tw8',0x1d3ec14c8:'cbnz\tw21',0x1d3ec1508:'mov\tw1, #22',0x1d3ec0748:'mov\tw9, #1024',0x1d3ec0818:'cbz\tw9',0x1d3ec09cc:'mov\tw8, #1024'}.items():assert x in ins[a],(hex(a),ins[a])
strings=[]
for a,value in [(0x1dabae998,'IE_KEY_RSN_BCIPHER'),(0x1dabae6f8,'80211W_ENABLED'),(0x1dabadfd8,'IE_KEY_RSN_AUTHSELS')]:
 ptr=struct.unpack('<Q',C.read(a+16,8))[0]&0xfffffffff;assert C.read(ptr,100).split(b'\0')[0].decode()==value;strings.append(dict(address=hex(a),value=value))
tables=[]
for base,n,anchor,label in [(0x1d3ef5601,18,0x1d3ec0798,'AKM_priority_dispatch'),(0x1d3ef5614,38,0x1d3ebff84,'AKM_final_dispatch'),(0x1d3ef564c,8,0x1d3ec13ec,'BIP_dispatch')]:
 raw=C.read(base,n);tables.append(dict(name=label,address=hex(base),hex=raw.hex(),sha256=sha(raw)))
assert 0x1d3ec0798+4*C.read(0x1d3ef5601+5,1)[0]==0x1d3ec0814
assert 0x1d3ebff84+4*struct.unpack('<H',C.read(0x1d3ef5614+12,2))[0]==0x1d3ec09cc
priority=C.read(0x1d3ef8890,19);assert priority[6]==3 and priority[2]==2
(P/'original-producer.txt').write_text('\n'.join(rows)+'\n')
record=dict(status='PASS_STATIC_BYTES_ONLY',instructions=len(ins),ranges=proof,strings=strings,tables=tables,priority_hex=priority.hex(),framework_TEXT_sha256=sha(C.read(0x1d3eb9000,344064)),conclusions={'station_rsn':'Producer caps0 may append zeroPMKID count and BIP6/11/12/13; exact22/24/28 accepted and normalized to22, noPMF negotiation','framework_budget':'Sync GET21 helper400 iterations with20000us sleep, thenSET22; joinqueue8s uses exactsame kerneluptime domain as nativeclock','AKM6':'Original priority dispatch gates6 on80211W_ENABLED true; priority6=3 outranks2=2. FinalAKM6 mapsauth_upper1024, retainedtruthful unsupported. No captured actualSET20 dictionary, so cannotclaimSettings flagvalue or eliminate runtime risk.','scan_overflow':'nsb_add onlydroppedcounter source is full16record cache; successful matchedstate7 completes beforedeadline, excluding otherpartial causes.'},limits=['Original selected cache byte evidence, no Apple code execution or REA provider available','No target execution, credentials, SSID or device data inspected','RSN optionalPMF trailer normalization is port policy with stationcaps0; noSHA256/SAE/PMF claims'])
(P/'source-evidence.json').write_text(json.dumps(record,indent=2)+'\n');print('PASS original selected producer',len(ins),'instructions')
