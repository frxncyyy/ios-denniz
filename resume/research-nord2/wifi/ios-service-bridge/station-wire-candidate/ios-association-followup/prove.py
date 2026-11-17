#!/usr/bin/env python3
"""Bounded original-cache proof; static data flow, not an executed Apple builder."""
from pathlib import Path
import hashlib,json,re,runpy,struct
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
B=R/'research/nord2/wifi/ios-service-bridge'
Cache=runpy.run_path(str(B/'cache_view.py'))['Cache']
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
text=C.read(0x1d3eb9000,344064)
assert hashlib.sha256(text).hexdigest()=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
raw=(B/'IO80211-disassembly.txt').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='9945310dce64768e03c7a28a05bcd13a8e90742576f251681166e6af99041a37'
ranges={
 'pmk64-producer':[(0x1d3ebeefc,0x1d3ebf1a4),(0x1d3ebff54,0x1d3ebffcc),
  (0x1d3ec0a64,0x1d3ec0b40),(0x1d3ec0bb0,0x1d3ec0c38)],
 'event9-complete-consumer':[(0x1d3ee5814,0x1d3ee58a8),(0x1d3ee5a1c,0x1d3ee5be0),
  (0x1d3ed7fa0,0x1d3ed7ff4),(0x1d3ed8ab4,0x1d3ed8af8),
  (0x1d3edb710,0x1d3edbbfc)],
 'tail-policy':[(0x1d3ebf1ec,0x1d3ebf21c),(0x1d3ebf2e0,0x1d3ebf308),(0x1d3ebf598,0x1d3ebf5c8),
  (0x1d3ebfd80,0x1d3ebfdbc),(0x1d3ec11ac,0x1d3ec11dc),(0x1d3ec12e8,0x1d3ec12fc)]}
verified={};instructions={}
for name,rs in ranges.items():
 rows=[]
 for row in raw.decode().splitlines():
  m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+(.*)$',row)
  if m and any(lo<=int(m[1],16)<hi for lo,hi in rs):
   a=int(m[1],16);assert C.read(a,4)==bytes.fromhex(m[2]);rows.append(row);instructions[a]=m[3]
 (P/(name+'.txt')).write_text('\n'.join(rows)+'\n');verified[name]=len(rows)
# Prove the actual branch island to bzero using original library symbols.
stub=0x1d58f20bc
w,l,br=struct.unpack('<III',C.read(stub,12))
assert w&0x9f000000==0x90000000 and l&0xffc00000==0x91000000 and br==0xd61f0200
imm=((w>>29)&3)|(((w>>5)&0x7ffff)<<2)
if imm&(1<<20):imm-=1<<21
target=(stub&~4095)+(imm<<12)+((l>>10)&4095)
assert target==0x1d1b021b0
lib=0x1d1b01000;h=C.read(lib,32);n,size=struct.unpack_from('<II',h,16)
commands=C.read(lib+32,size);off=0
for _ in range(n):
 cmd,sz=struct.unpack_from('<II',commands,off)
 if cmd==0x19:
  sn,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',commands,off+8)
  if sn.rstrip(b'\0')==b'__LINKEDIT':link=va-fo
 if cmd==2:sy,ns,st,ss=struct.unpack_from('<IIII',commands,off+8)
 off+=sz
assert ns<10000
symbols=[]
for i in range(ns):
 ix,typ,sect,desc,value=struct.unpack('<IBBHQ',C.read(link+sy+i*16,16))
 if value==target:symbols.append(C.read(link+st+ix,128).split(b'\0')[0].decode())
assert set(symbols)=={'___bzero','__platform_bzero'}
for address,pattern in {
 0x1d3ee5ab8:'[sp, #16]',0x1d3ee5abc:'#4116',0x1d3ee5ac0:'0x1d58f20bc',
 0x1d3ec0acc:'#64',0x1d3ec0ad0:'0x1d3ec0c38',0x1d3ec0bb4:'0x1d3ec0bc0',
 0x1d3ec0bd0:'cbz',0x1d3ec0bdc:'0x1d3ec0f60',0x1d3ec0be0:'0x1d3ec0c30',
 0x1d3ec0c30:'str\twzr, [sp, #1040]',0x1d3ec0c34:'strh\tw27, [sp, #544]'}.items():
 assert pattern in instructions[address],(hex(address),pattern,instructions[address])
evidence={'method':'ordinary original-cache bytes + original library nlist + static branch data flow; no Apple execution/REA provider/device',
 'framework_text_sha256':hashlib.sha256(text).hexdigest(),'verified_instructions':verified,
 'bzero':{'stub':hex(stub),'target':hex(target),'symbols':symbols,
  'library':'/usr/lib/system/libsystem_platform.dylib','target_prefix_sha256':hashlib.sha256(C.read(target,64)).hexdigest()},
 'event9_extent':{'allocated_sp_bytes':4272,'event_message_sp_offset':128,
  'bzero_sp_offset':152,'bzero_bytes':4116,'interface_sp_offset':152,
  'primary_and_metadata_sp_offset':168,'metadata_read_extent':152,
  'largest_metadata_sp_end_exclusive':320,'bzero_sp_end_exclusive':4268,
  'zero_primary_record_bytes':48,'scratch_event_total_size':40,
  'secondary_bytes':'skipped by callback; not the source of initialized metadata'},
 'pmk64_branch':{'input':'validated64 hexadecimal characters; public synthetic fixture only',
  'preconditions':{'upper_auth':8,'saved_original_auth_w27':8,'saved_psk_path_sp100':1,'card75':False,'FT':False},
  'trace':['0acc compare input length64','0ad4 upper8','0adc ->0bb0','0bb4 ->0bc0',
   '0bd0 original auth8 nonzero','0bdc savedPSK true','0be0 disabled log ->0c30',
   '0c30 flags504=0','0c34 upper=saved original8','0c38 continue'],
  'output':{'key_length':32,'key_cipher':6,'upper_auth':8,'native_flags504':0},
  'common_passphrase':'not length64; bypasses this clearing block and can retain4; still unsupported'},
 'unresolved':['meaning/enforcement of nonzero504 including common-passphrase4 and CARD75 bit30',
  'nonzero580 conditional native behavior','nonzero584 transition-disable bit domains/enforcement',
  'nonzero588 band preference domains/enforcement']}
(P/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
# Consumer-derived field projection, not execution of complete CFDictionary builder.
fixture=bytearray(592)
def put(off,fmt,*v):struct.pack_into(fmt,fixture,off,*v)
put(0,'<I',1);put(4,'<HHH',2,1,8);put(12,'<I',7);fixture[16:23]=b'fixture'
put(56,'<III',1,32,6)
public_hex=''.join(f'{i:02x}' for i in range(1,33));fixture[72:104]=bytes.fromhex(public_hex)
rsn=bytes.fromhex(json.loads((P.parent/'ios-association/rsn-oracle.json').read_text())['rsn22_hex'])
put(204,'<H',len(rsn));fixture[206:228]=rsn
assert fixture[504:508]==bytes(4)
build=P/'_build';build.mkdir(exist_ok=True)
(build/'producer-fixture.h').write_text('static const unsigned char pmk64_fixture[592]={'+','.join(map(str,fixture))+'};\n')
print('PASS original-cache byte checks, bzero symbol, PMK64 clearing branch and event9 extent')
