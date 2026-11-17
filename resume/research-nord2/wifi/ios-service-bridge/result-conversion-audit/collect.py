#!/usr/bin/env python3
"""Read-only, bounded same-IPSW static age/rate evidence, no target execution."""
from pathlib import Path
import sys,struct,subprocess,hashlib,json
P=Path(__file__).resolve().parent;B=P.parent;R=next(p for p in P.parents if p.name=='ios-sweet');sys.path.insert(0,str(B));from cache_view import Cache
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert hashlib.sha256(C.read(0x1abd5e000,839680)).hexdigest()=='5a71851bbffeaf836285ae280cc4db27ab57ed426abb904a0d71d17cd066eb3d'
assert hashlib.sha256(C.read(0x1d3eb9000,344064)).hexdigest()=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
assert sha(B.parent/'ios-abi/wifid')=='0ec6ad6ee120fb107510393c485a19ceaf54ef8dc7da479899a8cf7b93cdf871'
strings={}
for a in [0x1d8465288,0x1d84646e8]:
 b=C.read(a,32);ptr=struct.unpack_from('<Q',b,16)[0]&0xffffffffff;n=struct.unpack_from('<Q',b,24)[0];assert n<80;strings[hex(a)]={'string_address':hex(ptr),'text':C.read(ptr,n).decode()}
for a in [0x1d8475a58,0x1d8474888,0x1d8477230]:
 ptr=struct.unpack('<Q',C.read(a,8))[0]&0xffffffffff;strings[hex(a)]={'selector_address':hex(ptr),'selector':C.read(ptr,100).split(b'\0')[0].decode()}
(P/'resolved-strings.json').write_text(json.dumps(strings,indent=2)+'\n')
objdump=R.parent/'toolchains/proton-clang/bin/llvm-objdump';artifact=B/'channel-mode-audit/CoreWiFi-TEXT-analysis.macho';out=[]
for start,end in [(0x1abde48a8,0x1abde48e4),(0x1abdd4fcc,0x1abdd4ffc),(0x1abdd6b4c,0x1abdd6b8c),(0x1abdd8e90,0x1abdd8eac),(0x1abe21b40,0x1abe21b54),(0x1abe25780,0x1abe25794),(0x1abe2ab40,0x1abe2ab54)]:
 out.append(subprocess.check_output([str(objdump),'-d','--start-address='+hex(start),'--stop-address='+hex(end),str(artifact)],text=True))
(P/'age-bounded-disassembly.txt').write_text('\n'.join(out))
lines=(B/'IO80211-disassembly.txt').read_text().splitlines();out=[]
for l in lines:
 try:a=int(l.split(':',1)[0],16)
 except ValueError:continue
 if 0x1d3ebd24c<=a<0x1d3ebd304 or 0x1d3ebd5bc<=a<0x1d3ebd660:out.append(l)
(P/'raw-rate-age-disassembly.txt').write_text('\n'.join(out)+'\n')
print('PASS pinned same-IPSW text, bounded age/rate consumer evidence; static only')
