#!/usr/bin/env python3
"""Bounded byte-checked original selected producer gates, no target execution."""
from pathlib import Path
import runpy,re,hashlib,json,struct
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if(x/'AGENTS.md').exists());B=R/'research/nord2/wifi/ios-service-bridge';C=runpy.run_path(str(B/'cache_view.py'))['Cache'](R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(C.read(0x1d3eb9000,344064))=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
ranges=[('SET20_envelope',0x1d3ebf830,0x1d3ebf850),('cached_CARD12',0x1d3ed5024,0x1d3ed5074),('saved_ctx',0x1d3ebf3f0,0x1d3ebf3f4),('RSN_or_WPA_gate',0x1d3ec0d98,0x1d3ec0e1c),('CCMP_group_gate',0x1d3ec0fd0,0x1d3ec0fe4),('CCMP_unicast_gate',0x1d3ec10d0,0x1d3ec1100),('cipher_refusal',0x1d3ec1198,0x1d3ec11ac),('RawSet_and_cleanup',0x1d3ec13f8,0x1d3ec141c),('error_exit',0x1d3ec151c,0x1d3ec152c)]
rows=[];ins={};proof=[]
for name,lo,hi in ranges:
 selected=[]
 for line in(B/'IO80211-disassembly.txt').read_text().splitlines():
  m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+(.*)$',line)
  if m and lo<=int(m[1],16)<hi:
   va=int(m[1],16);assert C.read(va,4)==bytes.fromhex(m[2]);selected.append(line);ins[va]=m[3]
 assert len(selected)==(hi-lo)//4;rows.extend(['# '+name]+selected);proof.append({'name':name,'start':hex(lo),'end':hex(hi),'sha256':sha(C.read(lo,hi-lo))})
for a,s in {0x1d3ebf830:'mov\tw8, #20',0x1d3ebf834:'mov\tw9, #592',0x1d3ec0df4:'tbnz\tw8, #6',0x1d3ec0e10:'tbnz\tw8, #5',0x1d3ec0e14:'mov\tw28, #-3900',0x1d3ec0e18:'0x1d3ec151c',0x1d3ec0fd4:'tbz\tw8, #3',0x1d3ec10ec:'tbnz\tw9, #3',0x1d3ec11a0:'mov\tw28, #-3903',0x1d3ec11a8:'0x1d3ec151c',0x1d3ec1414:'_Apple80211RawSet'}.items():assert s in ins[a],(hex(a),s,ins[a])
strings=[]
for a,expected in [(0x1dabadf98,'RSN_IE'),(0x1dabadf78,'WPA_IE')]:
 raw=struct.unpack('<Q',C.read(a+16,8))[0];ptr=raw&0xfffffffff;value=C.read(ptr,80).split(b'\0')[0].decode();assert value==expected;strings.append({'cfstring':hex(a),'bytes':hex(ptr),'value':value})
assert C.read(0x1d3ef8000+2233,4)==bytes.fromhex('000fac04')
group4=0x1d3ec0eb0+4*struct.unpack('<H',C.read(0x1d3ef563a+2*(4-1),2))[0];assert group4==0x1d3ec0fd0
log=R/'research/nord2/wifi-ios-connect/ios/connection-attempt1.log';data=log.read_bytes();assert len(data)==426714 and sha(data)=='c59ec35b6084f56707ed99ebfcd2dc3a5963f41aa8302d6a67229f1c18ed52b2';assert b'NORD2 WIFI IOS JOIN'not in data and b'NORD2 WIFI IOS STATION'not in data
client=(P/'baseline/shim/nord2-xnu/wifi_ios_client.cpp').read_text();assert 'if(q->request==12){niwc_manifest empty={};'in client
(P/'original-producer-gates.txt').write_text('\n'.join(rows)+'\n');report={'status':'PASS_ORIGINAL_SELECTED_CACHE_BYTES_STATIC_ONLY','ranges':proof,'strings':strings,'group_CCMP_suite':'000fac04','framework_TEXT_sha256':sha(C.read(0x1d3eb9000,344064)),'failed_attempt_log_sha256':sha(data),'failed_attempt_log_bytes':len(data),'executed_db14928f_client_sha256':sha(client.encode()),'executed_db14928f_GET12':'driver-owned explicit empty inventory => exactly13zero bytes; implemented WPA2 service inventory not wired','observation':'No JOINREQ/JOINCONTROL/STATION in immutable attempted connection trace','source_result':'Empty ctx byte81 blocks both RSN_IE bit6 and WPA_IE bit5, returns-3900 via0x1d3ec151c before RawSet0x1d3ec1414. CCMP group bit3 absent returns-3903; unicast CCMP bit3 absent cannot select pairwise suite.','inference':'Ordinary selected RSN/WPA2 association cannot reach SET20 with these cached zero capability bits; this is a concrete sufficient static blocker and strongly explains observed missing SET20. Actual live Apple callstack/error code remains unobserved.','limits':['Only existing selected original cache and disassembly carriers used; no REA provider available, no engine install/target execution','No UI dictionary/credential/passphrase/SSID/MAC inspected or logged','Static evidence does not prove sole runtime cause or actual current UI error; no capability bits changed']}
(P/'evidence.json').write_text(json.dumps(report,indent=2)+'\n');print('Verified',len(ins),'original instructions; empty CARD blocks RSN builder before RawSet')
