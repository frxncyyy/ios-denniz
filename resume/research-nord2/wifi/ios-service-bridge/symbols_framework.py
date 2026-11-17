import struct,json
from pathlib import Path
from cache_view import Cache
P=Path(__file__).resolve().parent;R=P.parents[3];c=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');a=0x1d3eb9000;h=c.read(a,32);n,size=struct.unpack_from('<II',h,16);b=c.read(a+32,size);off=0
for i in range(n):
 cmd,sz=struct.unpack_from('<II',b,off)
 if cmd==0x19:
  sn,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',b,off+8)
  if sn.rstrip(b'\0')==b'__LINKEDIT':base=va-fo
 if cmd==2:sy,ns,st,ss=struct.unpack_from('<IIII',b,off+8)
 off+=sz
syms=c.read(base+sy,ns*16);pages={};out={}
def string(a):
 page=a&~4095
 if page not in pages:pages[page]=c.read(page,8192)
 return pages[page][a-page:a-page+1024].split(b'\0')[0].decode(errors='replace')
for i in range(ns):
 ix,typ,sect,desc,value=struct.unpack_from('<IBBHQ',syms,i*16);name=string(base+st+ix);out[name]={'value':value,'type':typ,'index':i}
(P/'framework-symbols.json').write_text(json.dumps(out,indent=2)+'\n');print('symbols',len(out),'string pages',len(pages))
