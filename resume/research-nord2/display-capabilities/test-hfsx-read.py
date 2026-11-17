from pathlib import Path
import importlib.util,struct,hashlib,json
p=Path(__file__).with_name('hfsx-read.py');s=importlib.util.spec_from_file_location('h',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
image=p.resolve().parents[3]/'research/nord2/rootfs-vm/ios16-root-20H392-nord2-v1.img'
f=image.open('rb');calls=[]
def fetch(o,n):
 assert o%4096==0 and n==4096 and 0<=o<image.stat().st_size;calls.append(o);f.seek(o);return f.read(n)
r=m.Reader(fetch,image.stat().st_size);results=[r.file(p) for p in m.TARGETS[:3]]
assert not results[0]['found'] and not results[1]['found']
a=results[2];assert a['sha256']=='e80395c626cc633e4998428d4c85bba17f55a36b00fd2d74e3b479450b037856'
assert a['uid']==a['gid']==501 and a['mode']==0o100600 and a['bytes']==171
assert hashlib.sha256(bytes.fromhex(a['hex'])).hexdigest()==a['sha256']
assert r.read_bytes<150000 and len(calls)==len(set(calls))
for target in ['../etc/passwd','private//var','private/é']:
 try:r.file(target);assert False
 except m.Refusal:pass
def refused(callback):
 try:m.Reader(callback,image.stat().st_size);assert False
 except (m.Refusal,struct.error):pass
refused(lambda o,n:b'')
for offset,value in [(1024,b'H+'),(1064,struct.pack('>I',512)),(1028,struct.pack('>I',0)),(1028,struct.pack('>I',0x900))]:
 def changed(o,n):
  b=bytearray(fetch(o,n))
  if o<=offset<o+n:b[offset-o:offset-o+len(value)]=value
  return bytes(b)
 refused(changed)
def dirty(o,n):
 b=bytearray(fetch(o,n))
 if o==0:struct.pack_into('>I',b,1028,0)
 return bytes(b)
d=m.Reader(dirty,image.stat().st_size,True);assert d.dirty
# Exact node validator rejects corrupt record offsets, key length and child bounds.
for mutation in ['offset','key','child']:
 t=m.Reader(fetch,image.stat().st_size);original=t.fread
 def changed(fork,o,n):
  b=bytearray(original(fork,o,n))
  if o==t.root*t.node_size:
   start=m.U16(b,n-2)
   if mutation=='offset':struct.pack_into('>H',b,n-2,0)
   elif mutation=='key':struct.pack_into('>H',b,start,0xffff)
   else:
    for z in range(m.U16(b,10)):
     at=m.U16(b,n-2*(z+1));struct.pack_into('>I',b,at+2+m.U16(b,at),0xffffffff)
  return bytes(b)
 t.fread=changed
 try:
  if mutation=='child':t.file(m.TARGETS[2]);assert False
  else:t.node(t.root,t.depth);assert False
 except (m.Refusal,struct.error):pass
 try:t.node(t.nodes,1);assert False
 except m.Refusal:pass
print('PASS exact baseline/QEMU SHA+stat, aligned bounded/cached reads, missing paths, malformed header/node/key/child, dirty distinction and traversal gates')

for offset,value in [(12,0xffffffff),(16,r.blocks),(20,r.blocks)]:
 fork=bytearray(80);struct.pack_into('>Q',fork,0,4096);struct.pack_into('>I',fork,12,1);struct.pack_into('>II',fork,16,1,1);struct.pack_into('>I',fork,offset,value)
 try:r.fork(fork,0);assert False
 except m.Refusal:pass
