#!/usr/bin/env python3
"""Read-only bounded split-cache VA view. No slide fixups or runtime claims."""
from pathlib import Path
import struct
class Cache:
 def __init__(self,root):
  self.root=Path(root);self.maps=[];self.files={}
  for p in sorted(self.root.glob('dyld_shared_cache_arm64*')):
   if p.name.endswith('.symbols'):continue
   with p.open('rb') as f:
    h=f.read(512)
    if not h.startswith(b'dyld_v1'):continue
    off,n=struct.unpack_from('<II',h,16)
    if n>64:raise ValueError('mapping bound')
    f.seek(off);b=f.read(n*32)
    for i in range(n):
     a,size,fo,_,_=struct.unpack_from('<QQQII',b,i*32);self.maps.append((a,a+size,fo,p))
 def read(self,a,n):
  if n>16*1024*1024:raise ValueError('read bound')
  for lo,hi,off,p in self.maps:
   if lo<=a and a+n<=hi:
    f=self.files.get(p)
    if f is None:f=self.files[p]=p.open('rb')
    f.seek(off+a-lo);b=f.read(n)
    if len(b)!=n:raise ValueError('short')
    return b
  raise ValueError('unmapped VA '+hex(a))
 def images(self):
  p=self.root/'dyld_shared_cache_arm64'
  with p.open('rb') as f:
   h=f.read(512);off,n=struct.unpack_from('<II',h,0x1c0)
   if n>10000:raise ValueError('images bound')
   f.seek(off);b=f.read(n*32)
   for i in range(n):
    a,_,_,po,_=struct.unpack_from('<QQQII',b,i*32);f.seek(po);name=f.read(1024).split(b'\0')[0].decode();yield a,name
 def exports(self,a):
  h=self.read(a,32);n,size=struct.unpack_from('<II',h,16);b=self.read(a+32,size);o=0;tr=None
  for _ in range(n):
   cmd,sz=struct.unpack_from('<II',b,o)
   if cmd==0x19:
    sn,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',b,o+8)
    if sn.rstrip(b'\0')==b'__LINKEDIT':base=va-fo
   if cmd==0x80000033:tr=struct.unpack_from('<II',b,o+8)
   if cmd==0x80000022:tr=struct.unpack_from('<II',b,o+40)
   o+=sz
  if not tr:return {}
  b=self.read(base+tr[0],tr[1]);out={}
  def u(p):
   v=s=0
   while True:
    x=b[p];p+=1;v|=(x&127)<<s
    if not x&128:return v,p
    s+=7
    if s>63:raise ValueError('uleb')
  def walk(p,name,d=0):
   if d>128:raise ValueError('depth')
   z,q=u(p);end=q+z
   if z:
    flags,r=u(q)
    if not flags&8:value,r=u(r);out[name]=a+value
   count=b[end];q=end+1
   for _ in range(count):
    z=b.index(0,q);edge=b[q:z].decode();child,q=u(z+1);walk(child,name+edge,d+1)
  walk(0,'');return out
