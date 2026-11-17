#!/usr/bin/env python3
"""Synthetic header contract only; actual failed log has no EVENTHEADER row."""
from pathlib import Path
import importlib.util
P=Path(__file__).resolve().parent;ROOT=P.parents[5]
spec=importlib.util.spec_from_file_location('proposed',P/'parse-proposed-receipt.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
main=(ROOT/'research/nord2/wifi-main-swint/ios/fw-ready-first-snapshot.log').read_bytes()
def fixture(inner=0xe000):
 rows=[
 'NORD2 WIFI CAP ARM: cid=0x8a eid=0xec seq=4 deadline_us=3000000 event_budget=16',
 'NORD2 WIFI CAP: status=0 phase=5 complete=1 halt=0 error=0 submit=1 txdone=1 events=1 rearmed=1 match=1 polls=2',
 'NORD2 WIFI CAP RX: bytes=56 irq=0/0 valid=3 debug=0 echo=1 mismatch=0 elapsed_us=0',
 'NORD2 WIFI CAP DESCRIPTOR: raw=0x90009000/0xf000000/0/0',
 'NORD2 WIFI CAP TLV: elements=1 known=1 unknown=0 mac_present=1 mac_valid=1 resources=0 version=0 product=0 fw=0/0',
 'NORD2 WIFI CAP OWNER: native_error=0 native_halt=0 init_halt=0 driver=1 dmairq=0/0 valid=3 parked=0',
 'NORD2 WIFI CAP EVENT: n=0 kind=1 type=0x3800 eid=0xec seq=4 bytes=56 declared=0/0 debug_type=0 debug_bytes=0',
 f'NORD2 WIFI CAP EVENTHEADER: n=0 valid=1 bytes=36 inner={hex(inner)} raw=0x38000000/0/0/0/0/0/{hex(inner<<16)}/0x4ec/0',
 'NORD2 WIFI CAP WORKER: parking=1 retained=1 callbacks_complete=1']
 return main+('\n'.join(rows)+'\n').encode()
valid=fixture();assert m.parse(valid)['complete']
for inner in (0,7,0xe001,65535):assert m.parse(fixture(inner))['complete']==m.SOURCE_PARITY
header=next(l for l in valid.splitlines(keepends=True) if b'EVENTHEADER' in l)
negative=[valid.replace(header,b''),valid.replace(header,header+header),valid.replace(header,header.rstrip(b'\n')),
 valid.replace(header,header.rstrip(b'\n')+b' payload=secretMAC\n'),valid+b'NORD2 WIFI CAP EVENTHEADER GARBAGE\n',valid+b'\xff',b'0'*(m.LIMIT+1)]
for a,b in [(b'valid=1 bytes=36',b'valid=0 bytes=36'),(b'bytes=36 inner=',b'bytes=40 inner='),(b'EVENTHEADER: n=0',b'EVENTHEADER: n=1'),(b'raw=0x38000000',b'raw=0x38000001'),(b'0xe0000000/0x4ec',b'0xe0010000/0x4ec'),(b'/0x4ec/0',b'/0x5ec/0'),(b'/0x4ec/0',b'/0x4ed/0'),(b'inner=0xe000',b'inner=65536'),(b'inner=0xe000',b'inner=18446744073709551616'),(b'raw=0x38000000',b'raw=4294967296'),(b'txdone=1',b'txdone=0'),(b'type=0x3800 eid=0xec',b'type=0x3801 eid=0xec')]:
 assert a in valid;negative.append(valid.replace(a,b,1))
for data in negative:
 result=m.parse(data);assert not result['complete'] and not result['capability_reply'],result
 assert 'secretMAC' not in str(result)
actual=(ROOT/'research/nord2/wifi-capability/ios/capability-failure-first-snapshot.log').read_bytes();assert not m.parse(actual)['header_metadata_complete']
# Do not claim the fixture's seven as actual: this is a synthetic hypothetical
# observation appended to the frozen failed metadata for error-path coverage.
event=next(l for l in actual.splitlines(keepends=True) if b'NORD2 WIFI CAP EVENT:' in l)
fake=b'NORD2 WIFI CAP EVENTHEADER: n=0 valid=1 bytes=36 inner=7 raw=0x380001f8/0/0/0/0/0/0x701e0/0x4ec/0\n'
r=m.parse(actual.replace(event,event+fake));assert r['header_metadata_complete'] and not r['complete'] and not r['capability_reply']
print(f'PASS proposed header parser: policy={"source-parity" if m.SOURCE_PARITY else "strict E000"}, bounded/raw-consistent paired rows; {len(negative)} refusal mutations; old actual failure lacks header; synthetic failure never claims capability success')
