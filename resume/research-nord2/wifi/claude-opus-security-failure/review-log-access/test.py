"""Synthetic bounded catalog and privacy policy checks; no device calls."""
from pathlib import Path
import importlib.util,struct,tempfile,json
p=Path(__file__).with_name('collect.py');s=importlib.util.spec_from_file_location('logs',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
W16=lambda b,o,v:struct.pack_into('>H',b,o,v)
W32=lambda b,o,v:struct.pack_into('>I',b,o,v)
W64=lambda b,o,v:struct.pack_into('>Q',b,o,v)
B=4096;mem=bytearray(64*B);volume=memoryview(mem)[1024:1536];volume[:4]=b'HX\x00\x05';W32(volume,4,0x100);W32(volume,40,B);W32(volume,44,64)
def fork(b,o,start,blocks,logical):W64(b,o,logical);W32(b,o+12,blocks);W32(b,o+16,start);W32(b,o+20,blocks)
fork(volume,272,1,4,4*B);hdr=memoryview(mem)[B:2*B];hdr[8]=1;W16(hdr,14,2);W32(hdr,16,1);W16(hdr,32,B);W32(hdr,36,4);hdr[51]=0xbc

def key(parent,name):
 n=name.encode('utf-16-be');b=bytearray(8+len(n));W16(b,0,6+len(n));W32(b,2,parent);W16(b,6,len(name));b[8:]=n;return bytes(b)
def folder(cnid):
 b=bytearray(88);W16(b,0,1);W32(b,8,cnid);W16(b,42,0o40755);return bytes(b)
def file(cnid,block,raw,mode=0o100600):
 b=bytearray(248);W16(b,0,2);W32(b,8,cnid);W32(b,16,100+cnid);W16(b,42,mode);fork(b,88,block,1,len(raw));mem[block*B:block*B+len(raw)]=raw;return bytes(b)
def node(num,rows,height=1,next=0,prev=0):
 b=bytearray(B);W32(b,0,next);W32(b,4,prev);b[8]=255 if height==1 else 0;b[9]=height;W16(b,10,len(rows));pos=14
 for i,row in enumerate(rows):W16(b,B-2*(i+1),pos);b[pos:pos+len(row)]=row;pos+=len(row)
 W16(b,B-2*(len(rows)+1),pos);mem[(1+num)*B:(2+num)*B]=b
rows=[(2,'private',folder(10)),(10,'var',folder(11)),(11,'log',folder(20)),(11,'mobile',folder(12)),(12,'Library',folder(13)),(13,'Logs',folder(14)),(14,'CrashReporter',folder(15)),(15,'JetsamEvent-test.ips',file(90,20,b'UNRELATED-JETSAM-CONTENT')), (15,'Preferences-test.ips',file(91,21,b'PREFS-TEST')), (15,'unrelated-secret.ips',file(92,22,b'MUST-NOT-READ')), (15,'wifid-test.ips',file(93,23,b'WIFI-TEST')), (15,'wifid-link.ips',file(94,24,b'not-a-link-target',0o120777)),(15,'wifip2pd-test.ips',file(95,25,b'P2P-TEST'))]
rows.sort(key=lambda x:(x[0],x[1]));split=next(i for i,x in enumerate(rows) if x[1]=='wifid-link.ips');left=rows[:split];right=rows[split:]
node(1,[key(left[0][0],left[0][1])+struct.pack('>I',2),key(right[0][0],right[0][1])+struct.pack('>I',3)],height=2)
node(2,[key(a,b)+v for a,b,v in left],next=3);node(3,[key(a,b)+v for a,b,v in right],prev=2)
reads=[]
def fetch(o,n):reads.append(o);assert o%B==0 and n==B;return bytes(mem[o:o+n])
r=m.Reader(fetch,len(mem));assert [n for n,_ in r.children(m.CRASH)]==sorted(x[1] for x in rows if x[0]==15)
with tempfile.TemporaryDirectory() as td:
 result=m.collect(r,Path(td),fetch,related=True);assert sum('saved_as' in x for x in result['files'])==3;assert any(x.get('error')=='symlink/compressed/hardlink/special file refused' for x in result['files']);assert len(result['jetsam_candidates'])==1
 assert 20*B not in reads and 22*B not in reads and 24*B not in reads
 assert all('MUST-NOT-READ' not in f.read_text() for f in Path(td).iterdir());assert result['header_stable'] and not result['volume_dirty']
assert not m.FILE_RE.fullmatch('wifid-other.ips/escape');assert not m.FILE_RE.fullmatch('other-wifid.ips');assert not m.FILE_RE.fullmatch('com.apple.wifi.plist');assert m.FILE_RE.fullmatch('WiFiAgent-test.crash')
# Cycle/backlink/record count and dirty-header refusal remain bounded.
original=bytes(mem)
for mutate in ['cycle','backlink','count']:
 mem[:]=original
 if mutate=='cycle':W32(mem,4*B,2)
 elif mutate=='backlink':W32(mem,4*B+4,99)
 else:W16(mem,3*B+10,65535)
 try:m.Reader(fetch,len(mem)).children(m.CRASH);assert False,mutate
 except m.h.Refusal:pass
# A finite child cap counts nonASCII storage entries before name filtering.
mem[:]=original;saved_cap=m.MAX_ENTRIES;m.MAX_ENTRIES=1
try:
 try:m.Reader(fetch,len(mem)).children(m.CRASH);assert False
 except m.h.Refusal as e:assert str(e)=='directory entry cap'
 r=m.Reader(fetch,len(mem));r.resolve=lambda path:folder(15);r.depth=1;r.node=lambda n,height:[((15,(128,)),b''),((15,(129,)),b'')]
 try:r.children(m.CRASH);assert False
 except m.h.Refusal as e:assert str(e)=='directory entry cap'
finally:m.MAX_ENTRIES=saved_cap
mem[:]=original;W32(mem,1028,0)
try:m.Reader(fetch,len(mem));assert False
except m.h.Refusal:pass
assert m.Reader(fetch,len(mem),True).dirty
print('PASS synthetic real HFSX index/leaf chain, allowlist-only3logs, Jetsam/unrelated/symlink payload unread, cycle/backlink/record/dirty gates; no device IO')
# Selection is lexical, not the unreliable modification time; fixed eight files.
class Selection:
 dirty=False;read_bytes=0;cache={0:bytes(4096)}
 def __init__(self):self.read=[]
 def children(self,path):
  if path!=m.CRASH:return None
  result=[]
  for i in range(12):
   rec=bytearray(248);W16(rec,0,2);W32(rec,8,100+i);W32(rec,16,1000-i);W16(rec,42,0o100600);result.append((f'wifid-1969-12-31-1600{i:02}.ips',bytes(rec)))
  return result
 def resolve(self,path):return None
 def read_log(self,path):self.read.append(path);return {'path':path,'found':True},b'synthetic diagnostic'
a=Selection()
with tempfile.TemporaryDirectory() as td:
 result=m.collect(a,Path(td),lambda o,n:bytes(n));assert len(a.read)==8 and a.read==sorted(a.read,reverse=True);assert a.read[0].endswith('160011.ips') and result['truncated_selection']
mem[:]=original;r=m.Reader(fetch,len(mem));old=r.fork;r.fork=lambda *a:(m.MAX_FILE_BYTES+1,[])
try:r.read_log(m.CRASH+'/wifid-test.ips');assert False
except m.h.Refusal as e:assert '4MiB' in str(e)
r.fork=old;r.read_bytes=m.MAX_READ_BYTES;before=len(reads)
try:r.get(30*B,1);assert False
except m.h.Refusal:pass
assert len(reads)==before
try:m.Reader(lambda o,n:b'',len(mem));assert False
except m.h.Refusal:pass
print('PASS lexical1969-filename order/max8 ignoringmtime, perfile4MiB and physical36MiB budgets/shortread fail before payload fetch')
# A failing fake ADB executable must never leak even its synthetic serial.
import subprocess
with tempfile.TemporaryDirectory() as td:
 td=Path(td);adb=td/'fake-adb';adb.write_text('#!/bin/sh\nexit 17\n');adb.chmod(0o700);serial='SYNTHETIC-IDENTITY-DO-NOT-PRINT'
 q=subprocess.run(['python3',str(p),'--adb',str(adb),'--serial',serial,'--output',str(td/'out')],text=True,capture_output=True)
 assert q.returncode and serial not in q.stdout+q.stderr and 'Traceback' not in q.stderr
 assert all(serial not in f.read_text() for f in (td/'out').iterdir())
print('PASS fake-transport preflight failure refuses before block reads and redacts serial/command traceback')
