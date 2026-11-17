from pathlib import Path
import sys,re,struct
P=Path(__file__).resolve().parent;sys.path.insert(0,str(P.parent));from cache_view import Cache
R=P.parents[4];c=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');lines=(P.parent/'IO80211-disassembly.txt').read_text().splitlines();regs={};out=[]
for line in lines:
 m=re.match(r'([0-9a-f]+):',line)
 if not m:continue
 a=int(m[1],16)
 if not(0x1d3ebcb38<=a<0x1d3ebde00 or 0x1d3ebb534<=a<0x1d3ebbfac):continue
 m=re.search(r'adrp\s+(x\d+), 0x([0-9a-f]+)',line)
 if m:regs[m[1]]=int(m[2],16)
 m=re.search(r'add\s+(x\d+), (x\d+), #(\d+)',line)
 if m and m[2] in regs:
  va=regs[m[2]]+int(m[3]);regs[m[1]]=va
  try:
   b=c.read(va,32);ptr=struct.unpack_from('<Q',b,16)[0]&0xffffffffff;length=struct.unpack_from('<Q',b,24)[0]
   if length<200 and length:
    s=c.read(ptr,length).decode('ascii')
    if all(32<=ord(x)<127 for x in s):line+=' ; CFSTR '+repr(s)
  except (ValueError,UnicodeDecodeError):pass
 out.append(line)
(P/'conversion-constants.txt').write_text('\n'.join(out)+'\n')
