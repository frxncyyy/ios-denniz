from pathlib import Path
import re,importlib.util,json
p=Path(__file__).resolve().parent
sp=importlib.util.spec_from_file_location('parser',p/'parse-backend.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
main=''.join(l+'\n' for l in (p.parent/'log-parser/v3/synthetic-combined-success.log').read_text().splitlines() if l.startswith('NORD2 WIFI MAIN'))
data=(p.parent/'rx-readback/full-native.log').read_text();blocks=re.findall(r'NORD2 WIFI CAP ARM:.*?NORD2 WIFI CAP WORKER:[^\n]*\n',data,re.S)
assert blocks
positive=negative=0
for block in blocks:
 if block.count('NORD2 WIFI NORMAL COMMAND:')!=22:continue
 if block.count('NORD2 WIFI CAP ARM:')!=1 or 'MAIN DRAIN:' in block or 'NORD2 WIFI BACKEND: status=-' in block:continue
 b=(main+block).encode();r=m.parse(b)
 assert r['terminal_receipt_valid'],r
 if positive==0:(p/'synthetic-success.log').write_bytes(b)
 positive+=1
b=(p/'synthetic-success.log').read_bytes()
def reject(x):
 global negative
 r=m.parse(x);assert not r['terminal_receipt_valid'] and not r['complete'],r;negative+=1
lines=b.decode().splitlines(True)
for i,l in enumerate(lines):
 if any('NORD2 WIFI '+k+':' in l for k in m.SCHEMA):
  reject(''.join(lines[:i]+lines[i+1:]).encode());reject(''.join(lines[:i]+[l,l]+lines[i+1:]).encode())
  reject(''.join(lines[:i]+[l.rstrip('\n')]+lines[i+1:]).encode())
  for field,value in re.findall(r'([a-z_]+)=([^ \r\n]+)',l):
   for bad in ['-1','0x10000000000000000','x','1/2/3/4/5']:
    altered=re.sub(r'(?<!\w)'+field+r'=[^ \r\n]+',field+'='+bad,l,count=1);reject(''.join(lines[:i]+[altered]+lines[i+1:]).encode())
# Unknown scan/RX tags must not be silently stripped.
reject(b.replace(b'NORD2 WIFI RXDMA:',b'NORD2 WIFI RXDMAOTHER:',1))
# Exact valid-mask refusal even when raw words are all legitimate uint32.
reject(b.replace(b'valid=0x3f',b'valid=0x1f',1))
# Raw control/descriptor values are observations, not forced echo contracts.
x=b.replace(b'cfg=0x50209075/0x58209075',b'cfg=0xffffffff/0xffffffff').replace(b'control=0x9002c000/0x400/0x3ff/0x1',b'control=0xffffffff/0xffffffff/0xffffffff/0xffffffff')
assert m.parse(x)['terminal_receipt_valid'];positive+=1
# Fully bounded raw channel array: semantic times remain uninterpreted.
rf=b'NORD2 WIFI SCANRF: version=0 pno=0 beacons=0 split=0/0 sparse=0/0/0 channels=0\n'
rfbase=re.sub(rb'NORD2 WIFI SCANRFCHANNEL:[^\n]*\n',b'',b)
rfbase=re.sub(rb'NORD2 WIFI SCANRF:[^\n]*\n',rf,rfbase)
extra=b''.join(('NORD2 WIFI SCANRFCHANNEL: n=%u channel=%u beacon_probe=255 mdrdy=255 idle_raw=65535 scan_time_raw=65535\n'%(i,i+1)).encode() for i in range(64))
full=rfbase.replace(rf,rf.replace(b'channels=0',b'channels=64')+extra)
assert m.parse(full)['complete'];positive+=1
for tag in [b'channel=1 ',b'beacon_probe=255 ',b'mdrdy=255 ',b'idle_raw=65535 ',b'scan_time_raw=65535\n']:
 value=b'256 ' if tag.startswith((b'channel=',b'beacon_probe=',b'mdrdy=')) else b'65536'+(b'\n' if tag.endswith(b'\n') else b' ')
 reject(full.replace(tag,tag.split(b'=')[0]+b'='+value,1))
reject(full.replace(b'n=63 channel=64',b'n=62 channel=64'))
reject(full.replace(b'channels=64',b'channels=63'))
reject(full+extra.splitlines(keepends=True)[0])
# Source eventbudget exit has no final capture: zero placeholders are not reads.
def change(data,key,field,value,which=0):
 ls=data.decode().splitlines(True);inds=[i for i,l in enumerate(ls) if l.startswith('NORD2 WIFI '+key+':')];i=inds[which]
 ls[i]=re.sub(r'(?<!\w)'+field+r'=[^ \r\n]+',field+'='+str(value),ls[i],count=1);return ''.join(ls).encode()
part=next((main+x).encode() for x in blocks if x.count('NORD2 WIFI NORMAL COMMAND:')==22 and 'NORD2 WIFI BACKEND: status=0 ' in x and 'done=0 timed_out=1' in x)
for key,field,value,which in [('NORMAL','events',8192,0),('NORMAL','pumps',533,0),('NORMAL','callbacks',100000,0),('NORMAL','event_limit',1,0),('NORMAL','service_timed_out',0,0),('NORMAL BOUNDS','last',700000,0),('NORMAL BOUNDS','command_deadline',3000000,0),('NORMAL BOUNDS','service_deadline',15700000,0),('NORMAL COMMAND','polls',600,21),('RX','packets',8192,0),('RX','rearmed',8192,0),('RX','events',8188,0),('RX','debug',8188,0),('SCAN','timed_out',0,0),('RXQUEUE','packets',8188,1),('RXQUEUE','rearmed',8188,1),('RXQUEUE','next',0,1),('RXQUEUE','didx',0,1)]:part=change(part,key,field,value,which)
part=re.sub(rb'NORD2 WIFI RXEVENT:[^\n]*\n',b'',part)
history=''.join('NORD2 WIFI RXEVENT: n=%d bytes=48 outer=0x3800 inner=0x1234 eid=0x27 seq=0 kind=2 status=0\n'%i for i in range(64)).encode()
part=part.replace(b'NORD2 WIFI BASIC WORKER:',history+b'NORD2 WIFI BASIC WORKER:')
part=re.sub(rb'NORD2 WIFI RXDMA: n=1[^\n]*\n',b'NORD2 WIFI RXDMA: n=1 attempted=0 complete=0 error=0 reason=0 valid=0 cfg=0/0 started=0 last=0\n',part)
for j in range(4):
 row=('NORD2 WIFI RXDMAQUEUE: n=1 slot=%u q=0 next=0 valid=0 base=0 expected=0 control=0/0/0/0 ext_addr=0 ext=0 descriptor=0 raw=0/0/0/0\n'%j).encode()
 part=re.sub(rb'NORD2 WIFI RXDMAQUEUE: n=1 slot='+str(j).encode()+rb'[^\n]*\n',row,part)
r=m.parse(part);assert r['terminal_receipt_valid'] and not r['complete'] and not r['rf_counters_observed'],r;positive+=1
reject(part.replace(b'n=1 attempted=0 complete=0',b'n=1 attempted=0 complete=1'))
report={'positive':positive,'negative':negative,'scope':'Synthetic full-native fixtures plus unchanged v3 MAIN seed; no hardware result','status':'PASS'};(p/'test-report.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
