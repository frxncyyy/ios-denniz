#!/usr/bin/env python3
"""Independent offset/header-ID and exact payload verification; no device access."""
import gzip,hashlib,json,struct,zlib
from pathlib import Path
HERE=Path(__file__).resolve().parent
source=HERE.parent/'nord2-adb-rescue.img'
def parse(path):
 b=path.read_bytes();assert b[:8]==b'ANDROID!'
 u=lambda off:struct.unpack_from('<I',b,off)[0]
 assert u(36)==2048 and u(40)==2 and u(24)==u(1632)==0
 pos=2048;parts=[]
 for size in [u(8),u(16),u(24),u(1632),u(1648)]:
  parts.append(b[pos:pos+size]);assert len(parts[-1])==size
  pos+=(size+2047)//2048*2048
 assert pos==len(b)
 h=hashlib.sha1()
 for p in parts:h.update(p);h.update(struct.pack('<I',len(p)))
 assert b[576:608]==h.digest()+bytes(12)
 return b,parts
before,old=parse(source);after,new=parse(HERE/'base.img')
assert old[0]==new[0] and old[4]==new[4]
assert gzip.decompress(old[1])==gzip.decompress(new[1])==(HERE/'ramdisk.cpio').read_bytes()
for i in range(2048):
 if i not in list(range(16,20))+list(range(576,608)):assert before[i]==after[i]
z=zlib.decompressobj(31);linux=z.decompress(new[0])+z.flush();assert z.eof and z.unused_data
m=json.loads((HERE/'manifest.json').read_text())
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(after)==m['base_sha256'] and sha(before)==m['source_sha256']
assert sha(linux)==m['linux_decompressed_sha256'] and sha(z.unused_data)==m['appended_dt_sha256']
assert sha(new[4])==m['header_dt_sha256']
print('PASS independent v2offsets/ID/headerallowlist/exactCPIO/Linux/bothDT/sourceidentity')
