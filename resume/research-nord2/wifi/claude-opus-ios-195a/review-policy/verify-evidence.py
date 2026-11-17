#!/usr/bin/env python3
"""Focused original-cache checks only; no fresh disassembly or target execution."""
from pathlib import Path
import hashlib,json,re,runpy,struct
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
B=R/'research/nord2/wifi/ios-service-bridge'
Cache=runpy.run_path(str(B/'cache_view.py'))['Cache']
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
text=C.read(0x1d3eb9000,344064)
assert hashlib.sha256(text).hexdigest()=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
dis=(B/'IO80211-disassembly.txt').read_bytes()
assert hashlib.sha256(dis).hexdigest()=='9945310dce64768e03c7a28a05bcd13a8e90742576f251681166e6af99041a37'
ranges={'scheduling-builder':[(0x1d3ebb93c,0x1d3ebbaf0),(0x1d3ed6250,0x1d3ed62fc)],
 'rawset-return':[(0x1d3ec3958,0x1d3ec39b0),(0x1d3ec3ea0,0x1d3ec3edc),
  (0x1d3ebc49c,0x1d3ebc4d8),(0x1d3ebc880,0x1d3ebc8bc),
  (0x1d3ebe8bc,0x1d3ebe8c8),(0x1d3ebe9e8,0x1d3ebe9f4)]}
counts={}
for name,rs in ranges.items():
 rows=[]
 for row in dis.decode().splitlines():
  m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+',row)
  if m and any(lo<=int(m[1],16)<hi for lo,hi in rs):
   assert C.read(int(m[1],16),4)==bytes.fromhex(m[2]);rows.append(row)
 (P/(name+'.txt')).write_text('\n'.join(rows)+'\n');counts[name]=len(rows)
exports=json.loads((B/'dependency-exports.json').read_text())
mask=0xfffffffff
got=struct.unpack('<Q',C.read(0x1daca9b68,8))[0]&mask
assert got==exports['_kCFBooleanTrue']==0x1df247f18
obj=struct.unpack('<Q',C.read(got,8))[0]&mask
assert obj==exports['___kCFBooleanTrue']==0x1df28ff90
# Independently verify the original CoreFoundation nlist for the imported slot.
base,name=next((a,n) for a,n in C.images() if n.endswith('/CoreFoundation.framework/CoreFoundation'))
h=C.read(base,32);n,size=struct.unpack_from('<II',h,16);commands=C.read(base+32,size);off=0
for _ in range(n):
 cmd,sz=struct.unpack_from('<II',commands,off)
 if cmd==0x19:
  sn,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',commands,off+8)
  if sn.rstrip(b'\0')==b'__LINKEDIT':link=va-fo
 if cmd==2:sy,ns,st,ss=struct.unpack_from('<IIII',commands,off+8)
 off+=sz
assert ns<200000
found=[]
for i in range(ns):
 ix,typ,sect,desc,value=struct.unpack('<IBBHQ',C.read(link+sy+i*16,16))
 if value in (got,obj):found.append([C.read(link+st+ix,128).split(b'\0')[0].decode(),hex(value)])
assert ['_kCFBooleanTrue',hex(got)] in found
keys={}
for a,expected in [(0x1dabadcf8,'SCAN_LOW_PRIORITY'),(0x1dabadd18,'SCAN_LOW_LATENCY')]:
 ptr=struct.unpack_from('<Q',C.read(a,32),16)[0]&mask
 value=C.read(ptr,128).split(b'\0')[0].decode();assert value==expected;keys[hex(a)]={'value':value,'ascii_va':hex(ptr)}
W=(R/'research/nord2/wifi/ios-abi/wifid').read_bytes()
assert hashlib.sha256(W).hexdigest()=='0ec6ad6ee120fb107510393c485a19ceaf54ef8dc7da479899a8cf7b93cdf871'
segments=[];off=32
for _ in range(struct.unpack_from('<I',W,16)[0]):
 cmd,sz=struct.unpack_from('<II',W,off)
 if cmd==25:
  va,vs,fo,fs=struct.unpack_from('<QQQQ',W,off+24);segments.append((va,fo,fs))
 off+=sz
wrapper=B/'service-compat-resume/scan-requests/wifid-scan-wrapper.txt'
rows=[]
for row in wrapper.read_text().splitlines():
 m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+',row)
 if not m:continue
 a=int(m[1],16);hits=[(va,fo)for va,fo,fs in segments if va<=a and a-va+4<=fs];assert len(hits)==1
 va,fo=hits[0];assert W[fo+a-va:fo+a-va+4]==bytes.fromhex(m[2]);rows.append(row)
(P/'wifid-retry.txt').write_text('\n'.join(rows)+'\n');counts['wifid-retry']=len(rows)
strings=['Setting low latency to true for interactive client','Normal priority, low-latency scan request',
 'Low priority, non-low-latency scan request','Low-latency scan request: shouldBlockScans=%d, no throttling']
string_offsets={s:W.find(s.encode()) for s in strings};assert all(x>=0 for x in string_offsets.values())
out={'method':'ordinary original cache/wifid byte checks and original CoreFoundation nlist; no REA provider/runtime',
 'framework_text_sha256':hashlib.sha256(text).hexdigest(),'verified_instructions':counts,'keys':keys,
 'boolean_true':{'GOT':'0x1daca9b68','variable':hex(got),'object':hex(obj),'original_nlist':found},
 'wifid_sha256':hashlib.sha256(W).hexdigest(),'static_log_string_file_offsets':string_offsets,
 'rawset':'IOUC returned32bit value is copied unchanged to errno and returned; ScanAsync retrieves errno unchanged',
 'retry':'wifid selected wrapper retries16/61/-3905, not kIOReturnBusy0xe00002d5',
 'scheduling_limit':'best-effort dedicated unthrottled scan FIFO only; no priority reordering/preemption/deadline guarantee'}
(P/'evidence.json').write_text(json.dumps(out,indent=2)+'\n')
print('PASS focused original-source evidence:',counts,'kCFBooleanTrue confirmed')
