#!/usr/bin/env python3
"""Make a disassembly-only TEXT carrier; not loadable/reconstituted framework."""
import struct,json
from pathlib import Path
from cache_view import Cache
P=Path(__file__).resolve().parent;R=P.parents[3];m=json.loads((P/'framework-map.json').read_text());c=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');a=m['base'];s=next(x for x in m['segments'] if x['name']=='__TEXT');b=bytearray(c.read(a,s['size']));n,size=struct.unpack_from('<II',b,16);off=32
for _ in range(n):
 cmd,sz=struct.unpack_from('<II',b,off)
 if cmd==0x19 and b[off+8:off+24].rstrip(b'\0')==b'__TEXT':
  lc=bytearray(b[off:off+sz]);struct.pack_into('<Q',lc,40,0)
  ns=struct.unpack_from('<I',lc,64)[0]
  for i in range(ns):
   so=72+80*i;old=struct.unpack_from('<I',lc,so+48)[0]
   struct.pack_into('<I',lc,so+48,old-s['fileoff'] if old else 0)
  break
 off+=sz
struct.pack_into('<I',b,12,1) # disassembly-only MH_OBJECT carrier
struct.pack_into('<II',b,16,1,len(lc));b[32:32+len(lc)]=lc
(P/'IO80211-TEXT-analysis.macho').write_bytes(b)
