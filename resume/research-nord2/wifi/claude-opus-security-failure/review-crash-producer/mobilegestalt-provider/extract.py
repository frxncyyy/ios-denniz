import sys,struct,json
from pathlib import Path
sys.path.insert(0,'research/nord2/wifi/ios-service-bridge');from cache_view import Cache
c=Cache('research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');p=Path(__file__).parent
a,n=next((a,n) for a,n in c.images() if n=='/usr/lib/libMobileGestalt.dylib')
e=c.exports(a);h=c.read(a,32);nc,size=struct.unpack_from('<II',h,16);b=c.read(a+32,size);o=0;segs=[]
for _ in range(nc):
 cmd,sz=struct.unpack_from('<II',b,o)
 if cmd==0x19:
  sn,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',b,o+8);segs.append({'name':sn.rstrip(b'\0').decode(),'va':va,'size':vs,'fileoff':fo,'filesize':fs})
 o+=sz
(p/'map.json').write_text(json.dumps({'image':n,'base':a,'exports':e,'segments':segs},indent=2))
s=next(x for x in segs if x['name']=='__TEXT');data=bytearray(c.read(a,s['size']));o=32
for _ in range(nc):
 cmd,sz=struct.unpack_from('<II',data,o)
 if cmd==0x19 and data[o+8:o+24].rstrip(b'\0')==b'__TEXT':
  lc=bytearray(data[o:o+sz]);struct.pack_into('<Q',lc,40,0)
  for j in range(struct.unpack_from('<I',lc,64)[0]):
   so=72+80*j;old=struct.unpack_from('<I',lc,so+48)[0];struct.pack_into('<I',lc,so+48,old-s['fileoff'] if old else 0)
  break
 o+=sz
struct.pack_into('<I',data,12,1);struct.pack_into('<II',data,16,1,len(lc));data[32:32+len(lc)]=lc;(p/'TEXT-analysis.macho').write_bytes(data)
