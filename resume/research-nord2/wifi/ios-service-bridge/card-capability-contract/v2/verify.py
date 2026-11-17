#!/usr/bin/env python3
"""Read-only same-IPSW byte evidence and private synthetic CARD codec tests."""
from pathlib import Path
import sys,struct,json,hashlib,re,subprocess,tempfile
P=Path(__file__).resolve().parent;B=P.parents[1];R=B.parents[3];sys.path.insert(0,str(B));from cache_view import Cache
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
identity=json.loads((B/'evidence.json').read_text());assert hashlib.sha256(C.read(0x1d3eb9000,344064)).hexdigest()==identity['framework_original_text_sha256']
core=json.loads((B/'channel-mode-audit/identity.json').read_text());assert hashlib.sha256(C.read(0x1abd5e000,839680)).hexdigest()==core['original_text_sha256']
lines=(B/'IO80211-disassembly.txt').read_text().splitlines();ranges=[('association_context',0x1d3ebf1a4,0x1d3ebf1dc),('saved_context',0x1d3ebf3f0,0x1d3ebf3f4),('reload_context',0x1d3ec0d84,0x1d3ec0dd8),('WPA_RSN_and_WEP_gates',0x1d3ec0dd8,0x1d3ec0ec4),('RSN_cipher_gates',0x1d3ec0fbc,0x1d3ec1030),('WPA_cipher_gates',0x1d3ec1170,0x1d3ec11f4),('bind_raw13',0x1d3ed5050,0x1d3ed5074),('copyvalue_raw13',0x1d3ec5138,0x1d3ec51e4),('Apple80211Get_dispatch',0x1d3ec172c,0x1d3ec1740)];out=[];proof=[]
for name,lo,hi in ranges:
 rows=[]
 for line in lines:
  m=re.match(r'([0-9a-f]+): ((?:[0-9a-f]{2} ){4})',line)
  if m and lo<=int(m[1],16)<hi:assert C.read(int(m[1],16),4)==bytes.fromhex(m[2]);rows.append(line)
 assert len(rows)==(hi-lo)//4;out.extend(['\n# '+name]+rows);proof.append({'name':name,'start':hex(lo),'end_exclusive':hex(hi),'bytes':hi-lo,'sha256':hashlib.sha256(C.read(lo,hi-lo)).hexdigest()})
core_out=subprocess.check_output([str(R/'../toolchains/proton-clang/bin/llvm-objdump'),'-d','--start-address=0x1abd7c7a0','--stop-address=0x1abd7c87c',str(B/'channel-mode-audit/CoreWiFi-TEXT-analysis.macho')],text=True)
count=0
for line in core_out.splitlines():
 m=re.match(r'([0-9a-f]+): ((?:[0-9a-f]{2} ){4})',line)
 if m:assert C.read(int(m[1],16),4)==bytes.fromhex(m[2]);count+=1
assert count==0xdc//4
out.extend(['\n# CWFApple80211 capabilities:',core_out]);proof.append({'name':'CWFApple80211 capabilities:','start':'0x1abd7c7a0','end_exclusive':'0x1abd7c87c','bytes':220,'sha256':hashlib.sha256(C.read(0x1abd7c7a0,220)).hexdigest()})
# Resolve the actual CoreWiFi branch island to exported Apple80211Get, not its name alone.
a=0x1afb67eb8;w,l,br=struct.unpack('<III',C.read(a,12));assert w&0x9f000000==0x90000000 and l&0xffc00000==0x91000000 and br==0xd61f0200
imm=((w>>29)&3)|(((w>>5)&0x7ffff)<<2);imm=imm-(1<<21) if imm&(1<<20) else imm;target=(a&~4095)+(imm<<12)+((l>>10)&4095);assert target==json.loads((B/'framework-map.json').read_text())['exports']['_Apple80211Get']
strings=[]
for a,expected in [(0x1dabadf98,'RSN_IE'),(0x1dabadf78,'WPA_IE'),(0x1dabae298,'IE_KEY_RSN_MCIPHER'),(0x1dabae278,'IE_KEY_WPA_MCIPHER')]:
 raw=struct.unpack('<Q',C.read(a+16,8))[0];p=raw&0xfffffffff;s=C.read(p,100).split(b'\0')[0].decode();assert s==expected;strings.append({'CFString':hex(a),'ascii_va':hex(p),'value':s})
suites=[]
for off,expected in [(2221,'000fac02'),(2225,'000fac01'),(2233,'000fac04'),(2237,'000fac05'),(2245,'000fac08'),(2249,'000fac09'),(2265,'0050f205'),(2269,'0050f204'),(2273,'0050f201'),(2277,'0050f202')]:
 a=0x1d3ef8000+off;raw=C.read(a,4);assert raw.hex()==expected;suites.append({'va':hex(a),'bytes':raw.hex()})
(P/'source-excerpt.txt').write_text('\n'.join(out)+'\n');(P/'source-evidence.json').write_text(json.dumps({'method':'ordinary shell static same-IPSW original-cache instruction/data validation; no REA/provider/runtime','framework_original_text_sha256':identity['framework_original_text_sha256'],'CoreWiFi_original_text_sha256':core['original_text_sha256'],'ranges':proof,'instruction_bytes_verified':sum(x['bytes'] for x in proof),'actual_dictionary_keys':strings,'actual_cipher_suites':suites,'CoreWiFi_Apple80211Get_island':{'va':'0x1afb67eb8','resolved_export':hex(target)},'feature_gates':[{'behavior':'WEP40/104 family','raw_bit':0,'gate_pcs':['0x1d3ec0eb4','0x1d3ec0f50','0x1d3ec0fe8','0x1d3ec119c']},{'behavior':'TKIP','raw_bit':1,'gate_pcs':['0x1d3ec0fc0','0x1d3ec1174']},{'behavior':'CCMP128','raw_bit':3,'gate_pcs':['0x1d3ec0fd4','0x1d3ec1188']},{'behavior':'WPA_IE path','raw_bit':13,'gate_pcs':['0x1d3ec0e10']},{'behavior':'RSN_IE path','raw_bit':14,'gate_pcs':['0x1d3ec0df4']},{'behavior':'GCMP128/256 family','raw_bit':76,'gate_pcs':['0x1d3ec0ffc','0x1d3ec1010']}],'limitations':['These are observed behavior gates, not a complete canonical Apple feature enum.','No MTK native association/key/encrypted data paths installed; no current CARD success policy supplied.','CoreWiFi method copies numeric set without minimum nonzero check; not a complete wifid startup/feature audit.']},indent=2)+'\n')
with tempfile.TemporaryDirectory(prefix='niwc-card-') as td:
 exe=Path(td)/'test';cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(B),'-I'+str(B/'initial-query-audit'),str(P/'card.c'),str(P/'test.c'),str(B/'control_contract.c'),str(B/'initial-query-audit/query_contract.c'),'-o',str(exe)];subprocess.run(cmd,check=True);s=subprocess.check_output([str(exe)],text=True);(P/'test.log').write_text(s);print(s.strip());print('PASS original cache evidence',sum(x['bytes'] for x in proof),'instruction bytes')
