from pathlib import Path
import sys,json,struct,re,hashlib,base64,uuid
sys.path.insert(0,'research/nord2/wifi/ios-service-bridge');from cache_view import Cache
P=Path(__file__).parent;c=Cache('research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');m=json.loads((P/'map.json').read_text());a=m['base'];head=c.read(a,32);n,size=struct.unpack_from('<II',head,16);lc=c.read(a+32,size);off=0;uid=None
for _ in range(n):
 cmd,sz=struct.unpack_from('<II',lc,off)
 if cmd==0x1b:uid=str(uuid.UUID(bytes=lc[off+8:off+24]))
 off+=sz
hashed=base64.b64encode(hashlib.md5(b'MGCopyAnswerWifiAddressData').digest()).decode().rstrip('=');assert c.read(0x19c5b3dfa,len(hashed)).decode()==hashed
record=struct.unpack('<8Q',c.read(0x1d7325f30,64));assert record[2]&0xfffffffff==0x19c5b3dfa and record[3]&0xfffffffff==0x19c5716e8
def cfstr(a):
 q=struct.unpack('<4Q',c.read(a,32));return c.read(q[2]&0xfffffffff,q[3]).decode()
assert cfstr(0x1d735c620)=='mac-address-wifi0';assert cfstr(0x1d735a420)=='local-mac-address'
ranges=[(0x19c5716e8,0x19c571778),(0x19c55b368,0x19c55b3b4),(0x19c557414,0x19c5574cc),(0x19c57c070,0x19c57c084),(0x19c55a024,0x19c55a07c)]
verified=[]
for line in (P/'annotated.txt').read_text().splitlines():
 q=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+){3}[0-9a-f]{2})',line)
 if q:
  va=int(q[1],16)
  if any(lo<=va<hi for lo,hi in ranges):assert c.read(va,4)==bytes.fromhex(q[2]);verified.append(line)
(P/'byte-verified.txt').write_text('\n'.join(verified)+'\n')
imports={0x1a2593194:('_CFGetTypeID',0x186e63ec4),0x1a25930d4:('_CFDataGetTypeID',0x186e70fec),0x1a2593524:('_IORegistryEntryFromPath',0x18e55b710),0x1a259350c:('_IORegistryEntryCreateCFProperty',0x18e55668c)}
for a,(name,target) in imports.items():
 w,l,br=struct.unpack('<III',c.read(a,12));assert w&0x9f000000==0x90000000 and l&0xffc00000==0x91000000 and br==0xd61f0200
 imm=((w>>29)&3)|(((w>>5)&0x7ffff)<<2);imm=imm-(1<<21) if imm&(1<<20) else imm;assert (a&~4095)+(imm<<12)+((l>>10)&4095)==target
segments=[dict(name=s['name'],va=hex(s['va']),size=s['size'],sha256=hashlib.sha256(c.read(s['va'],s['size'])).hexdigest()) for s in m['segments'] if s['name'] in ('__TEXT','__DATA_CONST')]
(P/'verification.json').write_text(json.dumps({'image':m['image'],'uuid':uid,'segments':segments,'instructions_verified':len(verified),'key':hashed,'getter':hex(0x19c5716e8),'primary_path':'IODeviceTree:/chosen','property':'mac-address-wifi0','type':'CFData','consumer_bytes':6,'imports':{hex(k):{'name':v[0],'target':hex(v[1])} for k,v in imports.items()},'evidence_kind':'ordinary shell static original-cache reads; no REA provider/runtime'},indent=2)+'\n')
print('PASS',len(verified),'original cache instructions and key/provider/type imports')
