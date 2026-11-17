from pathlib import Path
import importlib.util
p=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('successor',p/'parse-evidence.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
data=(p.parent/'ios/fw-ready-first-snapshot.log').read_bytes()
assert m.parse(data)['complete']
cases=[]
for k in m.SCHEMA:
 line=next(x for x in data.splitlines(keepends=True) if ('NORD2 WIFI MAIN '+k+':').encode() in x)
 cases.extend([data.replace(line,line+line),data.replace(line,b''),data.replace(line,line.rstrip(b'\n'))])
cases += [data+b'NORD2 WIFI MAIN UNKNOWN: status=0\n',data+b'\xff\n']
for a,b in [(b'GUARD: reason=0',b'GUARD: reason=5'),(b'native_before=0/0',b'native_before=20/1'),(b'RXHEADER: expected=0x1/3 type=0x7',b'RXHEADER: expected=0x1/3 type=0x8'),(b'type=0x7 event=0x1/3 status=0',b'type=0x7 event=0x1/3 status=1'),(b'type=0x7 event=0x1/3',b'type=0x7 event=0x1/2'),(b'ready=1 raw=0x3 polls=1',b'ready=1 raw=0x1 polls=1'),(b'wait_ddone=0',b'wait_ddone=4294967296'),(b'consumed_zero=1 software_used=72',b'consumed_zero=0 software_used=72')]:
 assert a in data;cases.append(data.replace(a,b,1))
for i,x in enumerate(cases):
 r=m.parse(x);assert not r['complete'],i;assert r['dma_enabled'] is None
assert m.parse(data.replace(b'type=0x7 event=',b'type=0xe000 event='))['complete']
# CID remains metadata: selected INIT waiter does not require echo.
assert m.parse(data.replace(b'status=0 cid=0 valid=',b'status=0 cid=65 valid='))['complete']
print('PASS actual frozen success plus',len(cases),'negative duplicate/missing/truncation/error/ACK/readiness/width cases')
