from pathlib import Path
import subprocess,re,json,struct
from cache_view import Cache
P=Path(__file__).resolve().parent;R=P.parents[3];c=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');names={v:k for k,v in json.loads((P/'dependency-exports.json').read_text()).items()};names.update({v:k for k,v in json.loads((P/'framework-map.json').read_text())['exports'].items()})
s=subprocess.check_output([str(R/'../toolchains/proton-clang/bin/llvm-objdump'),'-d',str(P/'IO80211-TEXT-analysis.macho')],text=True)
out=[]
for line in s.splitlines():
 m=re.search(r'\bbl\s+0x([0-9a-f]+)',line)
 if m:
  a=int(m[1],16);name=names.get(a)
  if not name:
   try:
    w,l,br=struct.unpack('<III',c.read(a,12))
    if w&0x9f000000==0x90000000 and l&0xffc00000==0x91000000 and br==0xd61f0200:
     imm=((w>>29)&3)|(((w>>5)&0x7ffff)<<2);imm=imm-(1<<21) if imm&(1<<20) else imm;target=(a&~4095)+(imm<<12)+((l>>10)&4095);name=names.get(target)
   except ValueError:pass
  if name:line+=' ; '+name
 out.append(line)
(P/'IO80211-disassembly.txt').write_text('\n'.join(out)+'\n')
