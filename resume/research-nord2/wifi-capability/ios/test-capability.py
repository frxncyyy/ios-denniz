#!/usr/bin/env python3
"""Synthetic CAP rows appended to actual frozen main receipt; NO actual CAP reply."""
from pathlib import Path
import importlib.util
import json
import re

P = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('capability', P / 'parse-capability.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
main = (P.parents[1] / 'wifi-main-swint/ios/fw-ready-first-snapshot.log').read_bytes()

def fixture(debug=0, sequence=4, unknown=0):
    count = debug + 1
    size = 56 + 8*unknown
    lines = [
        'NORD2 WIFI CAP ARM: cid=0x8a eid=0xec seq=4 deadline_us=3000000 event_budget=16',
        f'NORD2 WIFI CAP: status=0 phase=5 complete=1 halt=0 error=0 submit=1 txdone=1 events={count} rearmed={count} match=1 polls={count+1}',
        f'NORD2 WIFI CAP RX: bytes={size} irq=0/0 valid=3 debug={debug} echo={int(sequence==4)} mismatch={int(sequence!=4)} elapsed_us=0',
        f'NORD2 WIFI CAP DESCRIPTOR: raw={hex(0x90006000+((2+count)%16)*4096)}/0xf000000/0/0',
        f'NORD2 WIFI CAP TLV: elements={1+unknown} known=1 unknown={unknown} mac_present=1 mac_valid=1 resources=0 version=0 product=0 fw=0/0',
        'NORD2 WIFI CAP OWNER: native_error=0 native_halt=0 init_halt=0 driver=1 dmairq=0/0 valid=3 parked=0',
    ]
    lines += [f'NORD2 WIFI CAP EVENT: n={n} kind=2 type=0x3800 eid=0x27 seq=4 bytes=48 declared=0/0 debug_type=3 debug_bytes=0' for n in range(debug)]
    lines += [f'NORD2 WIFI CAP EVENT: n={debug} kind=1 type=0x3800 eid=0xec seq={sequence} bytes={size} declared=0/0 debug_type=0 debug_bytes=0',
              'NORD2 WIFI CAP WORKER: parking=1 retained=1 callbacks_complete=1']
    return main + ('\n'.join(lines)+'\n').encode()

valid = fixture()
positives = [valid, fixture(1), fixture(15), fixture(sequence=3), fixture(sequence=255), fixture(unknown=1),
             valid.replace(b'declared=0/0', b'declared=65535/65535'),
             valid.replace(b'kind=1 type=0x3800', b'kind=1 type=0x7800'),
             valid.replace(b'polls=2\n', b'polls=3000\n').replace(b'elapsed_us=0', b'elapsed_us=2999999')]
# Every native-supported resource schema and an empty TLV reply remain valid.
for size, metadata in [(40, b'elements=0 known=0 unknown=0 mac_present=0 mac_valid=0 resources=0 version=0'), (64, b'elements=1 known=1 unknown=0 mac_present=0 mac_valid=0 resources=1 version=0'), (76, b'elements=1 known=1 unknown=0 mac_present=0 mac_valid=0 resources=1 version=0x80000001'), (52, b'elements=1 known=1 unknown=0 mac_present=0 mac_valid=0 resources=0 version=0x80000002')]:
    positives.append(valid.replace(b'bytes=56', f'bytes={size}'.encode()).replace(b'elements=1 known=1 unknown=0 mac_present=1 mac_valid=1 resources=0 version=0', metadata))
for i, data in enumerate(positives):
    result = m.parse(data)
    assert result['complete'], (i, result['issues'])
    assert result['scan_ready'] is False and result['network_ready'] is False
    assert 'mac[' not in json.dumps(result).lower()
negative = [main, (P.parents[1]/'wifi-main-swint/ios/fw-ready-full-snapshot.log').read_bytes(), b'', valid+b'\xff', b'\x00'*(m.MAX_BYTES+1),
            valid+b'NORD2 WIFI CAP UNKNOWN: secret-payload\n',
            valid+b'NORD2 WIFI CAP: worker_start_error=5 worker_attempted=1 submit=0\n',
            valid+b'NORD2 WIFI CAP: uncertain retained DMA state; manual reset required\n',
            valid+b'NORD2 WIFI MAIN UNKNOWN: status=0\n']
cap = [line for line in valid.splitlines(keepends=True) if b'NORD2 WIFI CAP' in line]
for line in cap:
    negative += [valid.replace(line,b''), valid.replace(line,line+line), valid.replace(line,line.rstrip(b'\n')),
                 valid.replace(line,line.rstrip(b'\n')+b' NORD2 WIFI CAP\n'),
                 valid.replace(line,line.rstrip(b'\n')+b' extra=0\n')]
    # Each emitted scalar and each slash component independently overflows.
    body = line.decode()
    for match in re.finditer(r'(?<=[=/])(?:0x[0-9a-f]+|[0-9]+)', body):
        mutation = body[:match.start()]+'18446744073709551616'+body[match.end():]
        negative.append(valid.replace(line,mutation.encode()))
for a,b in [
    (b'status=0 phase=5',b'status=1 phase=5'),(b'phase=5 complete=1',b'phase=4 complete=1'),
    (b'halt=0 error=0',b'halt=1 error=0'),(b'halt=0 error=0',b'halt=0 error=23'),
    (b'submit=1',b'submit=0'),(b'txdone=1',b'txdone=0'),(b'events=1',b'events=0'),
    (b'rearmed=1',b'rearmed=0'),(b'match=1',b'match=0'),(b'polls=2',b'polls=1'),
    (b'deadline_us=3000000',b'deadline_us=3000001'),(b'event_budget=16',b'event_budget=17'),
    (b'cid=0x8a',b'cid=0x8b'),(b'eid=0xec seq=4 deadline',b'eid=0xed seq=4 deadline'),
    (b'bytes=56 irq',b'bytes=55 irq'),(b'irq=0/0',b'irq=0/0x100000'),(b'valid=3 debug',b'valid=1 debug'),
    (b'debug=0 echo',b'debug=1 echo'),(b'echo=1',b'echo=0'),(b'mismatch=0',b'mismatch=1'),
    (b'elapsed_us=0',b'elapsed_us=3000000'),(b'raw=0x90009000',b'raw=0x90008000'),
    (b'/0xf000000/0/0',b'/0xcf000000/0/0'),(b'native_error=0',b'native_error=1'),
    (b'native_halt=0',b'native_halt=1'),(b'init_halt=0 driver',b'init_halt=1 driver'),
    (b'driver=1 dmairq',b'driver=0 dmairq'),(b'dmairq=0/0',b'dmairq=0x200000/0'),
    (b'parked=0',b'parked=1'),(b'parking=1',b'parking=0'),(b'retained=1',b'retained=0'),
    (b'callbacks_complete=1',b'callbacks_complete=0'),(b'n=0 kind',b'n=1 kind'),
    (b'kind=1',b'kind=4'),(b'type=0x3800',b'type=0x3801'),(b'eid=0xec seq=4 bytes',b'eid=0x27 seq=4 bytes'),
    (b'declared=0/0',b'declared=65536/0'),(b'debug_type=0',b'debug_type=3'),
    (b'elements=1 known=1',b'elements=2 known=1'),(b'known=1 unknown=0',b'known=7 unknown=0'),
    (b'mac_present=1 mac_valid=1',b'mac_present=0 mac_valid=1'),(b'product=0',b'product=65536'),
    (b'version=0',b'version=1'),(b'bytes=56',b'bytes=40'),(b'known=1 unknown=0 mac_present=1',b'known=0 unknown=1 mac_present=1'),(b'resources=0 version=0',b'resources=0 version=0x80000001'),
]:
    assert a in valid, a
    negative.append(main + valid[len(main):].replace(a,b,1))
debug = fixture(1)
for a,b in [(b'kind=2',b'kind=4'),(b'eid=0x27',b'eid=0xf0'),(b'debug_type=3',b'debug_type=2'),(b'debug_bytes=0',b'debug_bytes=1')]:
    negative.append(debug.replace(a,b,1))
negative += [main+b''.join([cap[1],cap[0]]+cap[2:]), b''.join(cap)+main,
             fixture(15).replace(b'events=16',b'events=17'),
             valid.replace(b'NORD2 WIFI MAIN GUARD: reason=0',b'NORD2 WIFI MAIN GUARD: reason=1')]
for i,data in enumerate(negative):
    result = m.parse(data)
    assert not result['complete'] and not result['capability_reply'], (i,result)
    assert 'secret-payload' not in json.dumps(result)
# Dependency refusal is a stable failure, never unverified fallback parsing.
old = m.MAIN_MANIFEST
m.MAIN_MANIFEST = '0'*64
assert 'main dependency unavailable/unpinned' in m.parse(valid)['issues']
m.MAIN_MANIFEST = old
print(f'PASS {len(positives)} synthetic CAP positives and {len(negative)+1} refusal cases; actual main receipt only, NO actual capability reply captured')
