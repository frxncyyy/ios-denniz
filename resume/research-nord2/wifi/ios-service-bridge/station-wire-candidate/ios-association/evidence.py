#!/usr/bin/env python3
"""Offline original-cache byte verification; ordinary tools, not REA Evidence."""
from pathlib import Path
import hashlib,json,re,runpy,struct
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
B=R/'research/nord2/wifi/ios-service-bridge'
Cache=runpy.run_path(str(B/'cache_view.py'),run_name='cache_view_local')['Cache']
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
text=C.read(0x1d3eb9000,344064)
assert hashlib.sha256(text).hexdigest()=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
disasm_bytes=(B/'IO80211-disassembly.txt').read_bytes()
assert hashlib.sha256(disasm_bytes).hexdigest()=='9945310dce64768e03c7a28a05bcd13a8e90742576f251681166e6af99041a37'
disasm=disasm_bytes.decode();lines=[];keys={};previous=''
for line in disasm.splitlines():
 m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+(.*)$',line)
 if not m:continue
 a=int(m[1],16)
 if not 0x1d3ebf1a4<=a<0x1d3ec16d0:continue
 assert C.read(a,4)==bytes.fromhex(m[2]);lines.append(line)
 # Literal CFString only, exact ADRP x1 + ADD x1 pattern.
 q=re.search(r'adrp\s+x1, 0x([0-9a-f]+)',previous)
 t=re.search(r'add\s+x1, x1, #(\d+)',m[3])
 if q and t:
  va=int(q[1],16)+int(t[1]);raw=C.read(va,32)
  ptr=struct.unpack_from('<Q',raw,16)[0]&0xfffffffff
  try:
   value=C.read(ptr,192).split(b'\0')[0].decode('ascii')
   if value and len(value)<160:keys[hex(va)]={'value':value,'ascii_va':hex(ptr),'instruction':hex(a)}
  except (ValueError,UnicodeDecodeError):pass
 previous=m[3]
(P/'associate-builder.txt').write_text('\n'.join(lines)+'\n')
(P/'static-keys.json').write_text(json.dumps(keys,indent=2)+'\n')
values={hex(a):C.read(a,n).hex() for a,n in [(0x1d3ef9198,8),(0x1d3ef88ad,8),(0x1d3ef88b1,4),(0x1d3ef5614,38),(0x1dabaa0f0,64),(0x1dabaa130,144)]}
result_table=struct.unpack('<35i',C.read(0x1d3ef90d0,140))
assert {i:result_table[i-1] for i in [1,2,9,21]}=={1:0,2:-3906,9:-3905,21:-3924}
# Original group branch literal, pairwise table indexed by cipher-2, and AKM
# table indexed by selector-1; instruction anchors are retained in builder.
group=C.read(0x1d3ef88b9,4)
group_target=0x1d3ec0eb0+4*struct.unpack('<H',C.read(0x1d3ef563a+(4-1)*2,2))[0]
assert group_target==0x1d3ec0fd0
pair_ptr=struct.unpack('<Q',C.read(0x1dabaa0f0+(4-2)*8,8))[0]&0xfffffffff
akm_ptr=struct.unpack('<Q',C.read(0x1dabaa130+(2-1)*8,8))[0]&0xfffffffff
pair=C.read(pair_ptr,4);akm=C.read(akm_ptr,4)
assert group==pair==bytes.fromhex('000fac04') and akm==bytes.fromhex('000fac02')
rsn=bytes([48,20,1,0])+group+bytes([1,0])+pair+bytes([1,0])+akm+bytes([0,0])
suite_evidence={'group_literal_va':'0x1d3ef88b9','group_branch':'0x1d3ec0fd0..1024',
 'group_input':4,'group_dispatch_table_va':'0x1d3ef563a','group_target':hex(group_target),
 'pairwise_table_va':'0x1dabaa0f0','pairwise_input':4,'pairwise_literal_va':hex(pair_ptr),
 'pairwise_instructions':'0x1d3ec10f4..1120','akm_table_va':'0x1dabaa130','akm_input':2,
 'akm_literal_va':hex(akm_ptr),'akm_instructions':'0x1d3ec1124..115c','rsn22_hex':rsn.hex()}
(P/'rsn-oracle.json').write_text(json.dumps(suite_evidence,indent=2)+'\n')
supplement=[]
for lo,hi in [(0x1d3ec1744,0x1d3ec177c),(0x1d3ed66a0,0x1d3ed676c),(0x1d3ec4038,0x1d3ec4080)]:
 for row in disasm.splitlines():
  match=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+',row)
  if match and lo<=int(match[1],16)<hi:
   assert C.read(int(match[1],16),4)==bytes.fromhex(match[2]);supplement.append(row)
(P/'result-disconnect-excerpt.txt').write_text('\n'.join(supplement)+'\n')
event_rows=[]
event_ranges=[(0x1d3ed7e6c,0x1d3ed7e90),(0x1d3ed7fa0,0x1d3ed7ff4),
 (0x1d3ed8ab4,0x1d3ed8af8),(0x1d3edb710,0x1d3edb798),
 (0x1d3edb7cc,0x1d3edb7e4),(0x1d3edbb08,0x1d3edbb7c),(0x1d3edc198,0x1d3edc1ac)]
for row in disasm.splitlines():
 match=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+',row)
 if match and any(lo<=int(match[1],16)<hi for lo,hi in event_ranges):
  assert C.read(int(match[1],16),4)==bytes.fromhex(match[2]);event_rows.append(row)
event_table=C.read(0x1d3ef5a4e,9)
assert 0x1d3ed7cd4+4*event_table[9-2]==0x1d3ed7fa0
(P/'event9-open-contract.txt').write_text('\n'.join(event_rows)+'\n')
(P/'static-evidence.json').write_text(json.dumps({'method':'ordinary original split-cache VA byte checks, no REA provider/runtime','REA_unavailable':'No connected REA tools or rea CLI; reused selected pinned framework evidence without installation/configuration changes','original_text_sha256':hashlib.sha256(text).hexdigest(),'disassembly_sha256':hashlib.sha256(disasm_bytes).hexdigest(),'verified_builder_instructions':len(lines),'values':values,'association_result_map':dict(enumerate(result_table,1)),'verified_result_disconnect_instructions':len(supplement),'verified_event_open_contract_instructions':len(event_rows),'event_table_va':'0x1d3ef5a4e','event_table':event_table.hex(),'event9_target':'0x1d3ed7fa0','event9_contract':'UNRESOLVED: no-primary fallback queries21, but metadata pointer is nonnull and helper reads metadata beyond absent primary payload'},indent=2)+'\n')
print('PASS pinned cache instructions:',len(lines),'static keys:',len(keys))
