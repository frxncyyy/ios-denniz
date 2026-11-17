from pathlib import Path
import sys,struct,json
P=Path(__file__).resolve().parent;sys.path.insert(0,str(P.parent));from cache_view import Cache
R=P.parents[4];c=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');b=c.read(0x1dabacb00,2736);out=[]
for i in range(0,len(b),16):
 n,ptr=struct.unpack_from('<I4xQ',b,i);a=ptr&0xffffffffff;s=c.read(a,200).split(b'\0')[0].decode();out.append({'id':n,'name':s,'table_va':hex(0x1dabacb00+i),'string_va':hex(a)})
(P/'event-names.json').write_text(json.dumps(out,indent=2)+'\n')
for e in out:
 if any(x in e['name'].lower() for x in ['scan','assoc','power','link']):print(e['id'],e['name'])
