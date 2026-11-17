#!/usr/bin/env python3
"""Bounded NXP PM3 register-container decoder; emits evidence, never programs HW."""
import argparse, struct, zlib, json, hashlib
from pathlib import Path
class Invalid(ValueError): pass
class Container:
 def __init__(self,b):
  self.b=b
  if not 46 <= len(b) <= 1048576: raise Invalid('size bound')
  if b[:6]!=b'PM3_01': raise Invalid('unsupported header')
  size,crc=self.get('<II',6)
  if size!=len(b) or zlib.crc32(b[14:])!=crc: raise Invalid('size/CRC')
  self.ndev,self.nprof,self.nlive=self.get('<HHH',40)
  if not 1<=self.ndev<=4 or self.nprof>128 or self.nlive>32: raise Invalid('count bound')
  self.take(46,4*(self.ndev+self.nlive))
 def take(self,o,n):
  if o<0 or n<0 or o+n>len(self.b): raise Invalid('out of bounds')
  return self.b[o:o+n]
 def get(self,f,o): return struct.unpack(f,self.take(o,struct.calcsize(f)))
 def ptr(self,o):
  v,=self.get('<I',o); p,t=v&0xffffff,v>>24
  if t>28: raise Invalid('descriptor type')
  self.take(p,1); return p,t
 def string(self,o):
  p,t=self.ptr(o)
  if t!=3: raise Invalid('string type')
  e=self.b.find(b'\0',p,min(len(self.b),p+256))
  if e<0: raise Invalid('unterminated string')
  return self.b[p:e].decode('ascii')
 def op(self,o):
  p,t=self.ptr(o)
  if t==2:
   reg,val,mask=self.get('<BHH',p)
  elif t==16:
   val,f=self.get('<HH',p); reg=f>>8; pos=(f>>4)&15; width=(f&15)+1
   if pos+width>16 or val>=(1<<width): raise Invalid('invalid bitfield')
   mask=((1<<width)-1)<<pos; val<<=pos
  elif t in (4,5):
   name=self.string(p); size,=self.get('<I',p+4); payload=self.take(p+8,size)
   if size<36: raise Invalid('short file')
   declared,crc=struct.unpack_from('<HI',payload,6)
   if declared!=size or zlib.crc32(payload[12:])!=crc: raise Invalid('file size/CRC')
   if t==5 and (size!=42 or payload[36:]!=bytes.fromhex('74ffff000000')): raise Invalid('unsupported patch')
   return {'descriptor':t,'offset':p,'name':name,'size':size,'payload_hex':payload[36:].hex()}
  else: return {'descriptor':t,'offset':p}
  return {'descriptor':t,'offset':p,'reg':reg,'mask':mask,'value':val,'delay_us':0}
 def decode(self):
  devs=[]
  for i in range(self.ndev):
   p,t=self.ptr(46+4*i)
   if t!=0: raise Invalid('device type')
   n,bus,addr,func,devid=self.get('<BBBBI',p); self.take(p+12,n*4)
   d={'bus':bus,'address':addr,'function':func,'devid':devid,'name':self.string(p+8),'device_ops':[],'profiles':[]}
   for j in range(n):
    q=p+12+4*j; a,k=self.ptr(q)
    if k==1:
     ln,group,pid=self.get('<BBH',a)
     if not ln or pid!=0x1234: raise Invalid('profile header')
     self.take(a+8,(ln-1)*4)
     d['profiles'].append({'name':self.string(a+4),'group':group,'ops':[self.op(a+8+4*x) for x in range(ln-1)]})
    else: d['device_ops'].append(self.op(q))
   devs.append(d)
  return {'sha256':hashlib.sha256(self.b).hexdigest(),'application_type':self.b[32:40].split(b'\0')[0].decode(),'devices':devs}
def main():
 a=argparse.ArgumentParser();a.add_argument('file');p=a.parse_args(); print(json.dumps(Container(Path(p.file).read_bytes()).decode(),indent=2))
if __name__=='__main__':main()
