#!/usr/bin/env python3
"""Strict read-only HFSX exact-path reader. No writes or journal replay."""
import struct,hashlib,json,argparse,subprocess,importlib.util
from pathlib import Path
U16=lambda b,o:struct.unpack_from('>H',b,o)[0]
U32=lambda b,o:struct.unpack_from('>I',b,o)[0]
U64=lambda b,o:struct.unpack_from('>Q',b,o)[0]
class Refusal(Exception):pass
class Reader:
 def __init__(self,read,size,allow_dirty=False):
  self.callback=read;self.size=size;self.cache={};self.read_bytes=0
  v=self.get(1024,512)
  if v[:4]!=b'HX\x00\x05':raise Refusal('unsupported volume signature/version')
  self.block=U32(v,40);self.blocks=U32(v,44)
  if self.block!=4096 or self.blocks*self.block>size:raise Refusal('volume geometry')
  self.attrs=U32(v,4);self.dirty=not bool(self.attrs&0x100) or bool(self.attrs&0x800)
  if self.dirty and not allow_dirty:raise Refusal('journal ambiguity: volume not cleanly unmounted; --allow-dirty-observation permits untrusted observation only')
  self.catalog=self.fork(v,272)
  h=self.fread(self.catalog,0,512)
  self.node_size=U16(h,32);self.depth=U16(h,14);self.root=U32(h,16);self.nodes=U32(h,36)
  if h[8]!=1 or self.node_size not in (4096,8192,16384,32768) or not 1<=self.depth<=16 or h[51]!=0xbc or not self.root or self.root>=self.nodes or self.nodes*self.node_size>self.catalog[0]:raise Refusal('unsupported/malformed catalog header')
 def get(self,off,n):
  if off<0 or n<0 or off+n>self.size:raise Refusal('physical bounds')
  out=bytearray()
  while n:
   page=off//4096*4096;at=off-page;count=min(n,4096-at)
   if page not in self.cache:
    if self.read_bytes+4096>4*1024*1024:raise Refusal('read budget exceeded')
    b=self.callback(page,4096)
    if len(b)!=4096:raise Refusal('short physical read')
    self.cache[page]=b;self.read_bytes+=4096
   out.extend(self.cache[page][at:at+count]);off+=count;n-=count
  return bytes(out)
 def fork(self,b,o):
  logical=U64(b,o);total=U32(b,o+12);ext=[];covered=0;ended=False
  for i in range(8):
   start,count=U32(b,o+16+i*8),U32(b,o+20+i*8)
   if not count:
    if start:raise Refusal('empty extent start')
    ended=True;continue
   if ended:raise Refusal('extent after terminator')
   if start+count>self.blocks:raise Refusal('extent volume escape')
   if any(start<s+c and s<start+count for s,c in ext):raise Refusal('overlapping extents')
   ext.append((start,count));covered+=count
  if covered!=total or logical>covered*self.block:raise Refusal('extent overflow unsupported or malformed fork')
  return logical,ext
 def fread(self,f,off,n):
  if off<0 or n<0 or off+n>f[0]:raise Refusal('fork bounds')
  out=bytearray();base=0
  for start,count in f[1]:
   end=base+count*self.block
   if off<end and n:
    at=off-base;k=min(n,end-off);out+=self.get(start*self.block+at,k);off+=k;n-=k
   base=end
  if n:raise Refusal('unmapped fork')
  return bytes(out)
 def node(self,num,height):
  if not 0<num<self.nodes:raise Refusal('node ID bounds')
  b=self.fread(self.catalog,num*self.node_size,self.node_size);kind=struct.unpack_from('>b',b,8)[0];n=U16(b,10)
  if b[9]!=height or kind!=(-1 if height==1 else 0) or n>self.node_size//8:raise Refusal('node type/height/count')
  offsets=[U16(b,self.node_size-2*(i+1)) for i in range(n+1)]
  if not offsets or offsets[0]<14 or offsets[-1]>self.node_size-2*(n+1) or any(x>=y for x,y in zip(offsets,offsets[1:])):raise Refusal('node record offsets')
  rows=[]
  for a,z in zip(offsets,offsets[1:]):
   rec=b[a:z];k=U16(rec,0)
   if k<6 or k>516 or k+2>len(rec) or k!=6+2*U16(rec,6):raise Refusal('catalog key length')
   key=(U32(rec,2),tuple(U16(rec,8+i*2) for i in range(U16(rec,6))))
   if rows and key<=rows[-1][0]:raise Refusal('unsorted/duplicate catalog keys')
   rows.append((key,rec[2+k:]))
  return rows
 def lookup(self,parent,name):
  key=(parent,tuple(name.encode('utf-16-be')[i]*256+name.encode('utf-16-be')[i+1] for i in range(0,len(name.encode('utf-16-be')),2)))
  num=self.root
  for height in range(self.depth,0,-1):
   rows=self.node(num,height)
   if height==1:return next((v for k,v in rows if k==key),None)
   candidates=[v for k,v in rows if k<=key]
   if not candidates:return None
   if len(candidates[-1])!=4:raise Refusal('index child size')
   num=U32(candidates[-1],0)
  raise Refusal('traversal')
 def file(self,path):
  parts=path.strip('/').split('/');parent=2
  if any(not x or x in ('.','..') or not x.isascii() for x in parts):raise Refusal('only exact ASCII paths supported')
  for i,name in enumerate(parts):
   rec=self.lookup(parent,name)
   if rec is None:return {'path':path,'found':False}
   typ=U16(rec,0)
   if i<len(parts)-1:
    if typ!=1 or len(rec)<88:raise Refusal('not ordinary directory')
    parent=U32(rec,8)
   else:
    if typ!=2 or len(rec)!=248:raise Refusal('not ordinary file')
    mode=U16(rec,42)
    if mode&0xf000!=0x8000 or rec[41]&0x20 or rec[48:52] in (b'hlnk',b'fdrp'):raise Refusal('symlink/compressed/hardlink/special file refused')
    f=self.fork(rec,88)
    if f[0]>262144:raise Refusal('file cap')
    raw=self.fread(f,0,f[0]);return {'path':path,'found':True,'cnid':U32(rec,8),'uid':U32(rec,32),'gid':U32(rec,36),'mode':mode,'create_time_hfs':U32(rec,12),'content_modify_time_hfs':U32(rec,16),'attribute_modify_time_hfs':U32(rec,20),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'hex':raw.hex(),'dirty_volume_observation':self.dirty}
TARGETS=['private/var/mobile/Library/Preferences/'+n+'.plist' for n in ['com.apple.iokit.IOMobileGraphicsFamily','com.apple.backboardd','com.apple.springboard']]+['Library/Preferences/com.apple.iokit.IOMobileGraphicsFamily.plist']
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--image',type=Path);p.add_argument('--adb');p.add_argument('--serial');p.add_argument('--output',type=Path,required=True);p.add_argument('--allow-dirty-observation',action='store_true');a=p.parse_args()
 if bool(a.image)==bool(a.adb):p.error('choose --image or --adb')
 a.output.mkdir(parents=True,exist_ok=False)
 if a.image:
  f=a.image.open('rb');size=a.image.stat().st_size
  def fetch(off,n):f.seek(off);return f.read(n)
 else:
  if not a.serial:p.error('--serial required')
  s=importlib.util.spec_from_file_location('install',Path(__file__).resolve().parents[3]/'tools/nord2-install-root.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
  pre=m.preflight(a.adb,a.serial);size=pre['partition_bytes']  # whole userdata (16 GiB volume or grown, storage-20261114)
  (a.output/'preflight.json').write_text(json.dumps(pre,indent=2))
  def fetch(off,n):
   cmd=f'dd if=/dev/block/sdc57 bs=4096 skip={off//4096} count={n//4096} 2>/dev/null'
   r=subprocess.run([a.adb,'-s',a.serial,'shell','-T',cmd],capture_output=True,timeout=30,check=True)
   if r.stderr:raise Refusal('unexpected ADB stderr')
   return r.stdout
 r=None
 try:
  r=Reader(fetch,size,a.allow_dirty_observation);files=[]
  for path in TARGETS:
   try:files.append(r.file(path))
   except (Refusal,struct.error) as e:files.append({'path':path,'error':str(e)})
  if fetch(0,4096)!=r.cache[0]:raise Refusal('volume header changed during observation')
  result={'files':files,'header_stable':True,'read_bytes':r.read_bytes+4096,'volume_dirty':r.dirty,'trusted_clean_observation':not r.dirty,'writes':0}
 except Exception as e:result={'error':str(e),'writes':0}
 if r:
  for off,raw in r.cache.items():(a.output/f'page-{off:012x}.bin').write_bytes(raw)
 (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
 if 'error' in result:raise SystemExit(1)
if __name__=='__main__':main()
