#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib
from cache_view import Cache
P=Path(__file__).resolve().parent;R=P.parents[3]
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
a,name=next((a,n) for a,n in C.images() if n.endswith('/IO80211.framework/IO80211'))
h=C.read(a,32);n,size=struct.unpack_from('<II',h,16);b=C.read(a+32,size);off=0;segs=[];exports=None
for i in range(n):
 cmd,sz=struct.unpack_from('<II',b,off)
 if cmd==0x19:
  sn,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',b,off+8);segs.append(dict(name=sn.rstrip(b'\0').decode(),va=va,size=vs,fileoff=fo,filesize=fs))
 if cmd==0x80000033:exports=struct.unpack_from('<II',b,off+8)
 off+=sz
link=next(s for s in segs if s['name']=='__LINKEDIT');trie=C.read(link['va']+exports[0]-link['fileoff'],exports[1]);out={}
def uleb(p):
 v=0;s=0
 while True:
  x=trie[p];p+=1;v|=(x&127)<<s
  if not x&128:return v,p
  s+=7
  if s>63:raise ValueError('uleb')
def walk(p,prefix,depth=0):
 if depth>128:raise ValueError('depth')
 ts,q=uleb(p);end=q+ts
 if ts:
  flags,r=uleb(q)
  if not flags&8:
   value,r=uleb(r);out[prefix]=a+value
 count=trie[end];q=end+1
 for _ in range(count):
  z=trie.index(0,q);edge=trie[q:z].decode();child,q=uleb(z+1);walk(child,prefix+edge,depth+1)
walk(0,'')
(P/'framework-map.json').write_text(json.dumps({'evidence':'static shell split-cache VA reads; no REA provider/decomp/runtime','image':name,'base':a,'segments':segs,'exports':out,'export_trie_sha256':hashlib.sha256(trie).hexdigest()},indent=2)+'\n')
print('IO80211 exports',len(out))
for k,v in out.items():
 if any(x in k for x in ['Open','BindTo','ScanAsync','EventMonitoring','StartMonitoring','RawGet','GetPower','Close']):print(hex(v),k)
