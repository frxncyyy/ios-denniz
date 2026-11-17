from pathlib import Path
import importlib.util,re,json
P=Path(__file__).resolve().parent
sp=importlib.util.spec_from_file_location('parser',P/'parse-backend.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
seed=(P.parent/'rx-evidence-parser-v2/synthetic-success.log').read_bytes()
seed=seed.replace(b'NORD2 WIFI BASIC TX:',b'NORD2 WIFI BASIC ASYNC: events=0 sleepy=0 state_valid=0 state=0\nNORD2 WIFI BASIC TX:',1)
def change(b,key,field,value,which=0):
 ls=b.decode().splitlines(True);ix=[i for i,l in enumerate(ls) if l.startswith('NORD2 WIFI '+key+':')][which];ls[ix]=re.sub(r'(?<!\w)'+field+r'=[^ \r\n]+',field+'='+str(value),ls[ix],count=1);return ''.join(ls).encode()
def row(b,key):return m.base.decode(next(l for l in b.decode().splitlines() if l.startswith('NORD2 WIFI '+key+': ')),key,m.base.SCHEMA[key])
def bump(b):
 qs=[m.base.decode(l,'RXQUEUE',m.base.SCHEMA['RXQUEUE']) for l in b.decode().splitlines() if l.startswith('NORD2 WIFI RXQUEUE:')];q=qs[1]
 for field in ['next','didx']:b=change(b,'RXQUEUE',field,(q[field]+1)%16,1)
 return b
pos=neg=0
def accept(b):
 global pos
 r=m.parse(b);assert r['terminal_receipt_valid'] and r['complete'],r;assert not any(r[k] for k in ['firmware_ack','interface_available','association_available','network_ready']);pos+=1
def reject(b):
 global neg
 r=m.parse(b);assert not r['terminal_receipt_valid'] and not r['complete'],r;neg+=1
accept(seed)
for state in range(256):
 # Captured arbitrary inner/sequence/declared values are not sleepy echoes.
 cap=seed
 cap=change(cap,'CAP','events',2);cap=change(cap,'CAP','rearmed',2);cap=change(cap,'CAP','polls',3)
 cap=change(cap,'CAP DESCRIPTOR','raw','0x9000a000/0xf000000/0/0')
 cap=change(cap,'CAP EVENT','n',1);cap=change(cap,'CAP EVENTHEADER','n',1)
 extra=('NORD2 WIFI CAP EVENT: n=0 kind=3 type=0x3800 eid=0x7 seq=255 bytes=40 declared=40/16 debug_type=%u debug_bytes=0\nNORD2 WIFI CAP SLEEPY: n=0 state=%u\nNORD2 WIFI CAP EVENTHEADER: n=0 valid=1 bytes=36 inner=0x1234 raw=0x38000028/0/0/0/0/0/0x12340010/0xff07/0\n'%(state,state)).encode()
 cap=cap.replace(b'NORD2 WIFI CAP EVENT: n=1',extra+b'NORD2 WIFI CAP EVENT: n=1');cap=bump(cap);accept(cap)
 reject(cap.replace(('state=%u\n'%state).encode(),b'state=256\n',1))
 reject(cap.replace(b'eid=0x7 seq=255 bytes=40',b'eid=0x7 seq=255 bytes=39',1))
 # BASIC records only raw state, all16-frame budget uses debug+sleepy.
 basic=seed
 for field,value in [('events',1),('sleepy',1),('state_valid',1),('state',state)]:basic=change(basic,'BASIC ASYNC',field,value)
 basic=change(basic,'BASIC','rearmed',1)
 replacement=('NORD2 WIFI BASIC DEBUG: valid=1 index=4 kind=3 type=0x3800 eid=0x7 seq=255 bytes=40 declared=40/16 inner=0x1234 id=0 format=%u length=0\n'%state).encode()
 basic=re.sub(rb'NORD2 WIFI BASIC DEBUG:[^\n]*\n',replacement,basic)
 basic=re.sub(rb'NORD2 WIFI BASIC DEBUGHEADER:[^\n]*\n',b'NORD2 WIFI BASIC DEBUGHEADER: valid=1 raw=0x38000028/0/0/0/0/0/0x12340010/0xff07/0\n',basic);basic=bump(basic);accept(basic)
 reject(change(basic,'BASIC ASYNC','state_valid',0));reject(change(basic,'BASIC ASYNC','events',17));reject(change(basic,'BASIC DEBUG','format',256));reject(change(basic,'BASIC DEBUG','eid',8));reject(change(basic,'BASIC DEBUG','bytes',39))
# Normal event path already accepts every raw uint8 state, no new relaxation.
for state in range(256):
 normal=seed
 for key,field,value,which in [('NORMAL','events',6,0),('RX','packets',6,0),('RX','rearmed',6,0),('RX','events',2,0),('RX','sleepy',1,0),('RXQUEUE','packets',2,1),('RXQUEUE','rearmed',2,1),('RXQUEUE','next',6,1),('RXQUEUE','didx',6,1)]:normal=change(normal,key,field,value,which)
 event=('NORD2 WIFI RXEVENT: n=1 bytes=40 outer=0x3800 inner=0xabcd eid=0x7 seq=255 kind=3 status=%u\n'%state).encode()
 normal=normal.replace(b'NORD2 WIFI SCANRF:',event+b'NORD2 WIFI SCANRF:',1);accept(normal)
 reject(normal.replace(('status=%u\n'%state).encode(),b'status=256\n',1))
# New records are mandatory/pair-counted and strict.
reject(seed.replace(b'NORD2 WIFI BASIC ASYNC: events=0 sleepy=0 state_valid=0 state=0\n',b''))
reject(seed.replace(b'BASIC ASYNC:',b'BASIC ASYNCOTHER:'))
(P/'synthetic-success.log').write_bytes(seed)
report={'status':'PASS_SYNTHETIC_ONLY','positive':pos,'negative':neg,'scope':'All256 rawsleepy states CAP+BASIC+normal, opaqueinner/seq and exactpairedheader/counters; malformed/budget/unknown refusal. No hardware replay.'};(P/'test-report.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
