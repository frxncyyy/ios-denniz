#!/usr/bin/env python3
"""Synthetic continuation receipts embedded in a pinned actual CAP capture."""
from pathlib import Path
import hashlib
import importlib.util
import json

P = Path(__file__).resolve().parent
ROOT = P.parents[3]
spec = importlib.util.spec_from_file_location('basic_receipt', P / 'parse-basic.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
actual = ROOT / 'research/nord2/wifi-capability-format/ios/capability-success-first-snapshot.log'
data = actual.read_bytes()
assert hashlib.sha256(data).hexdigest() == '286a3a8066286062b427aa163ff8b3a910fa787164182332ed4beec3531d4b88'
CAP_END = 'NORD2 WIFI CAP WORKER: parking=1 retained=1 callbacks_complete=1\n'
source = data.decode()
assert source.count(CAP_END) == 1

def line(key, values):
    parts = []
    index = 0
    for token in m.SCHEMA[key].split():
        name, _, count = token.partition('/')
        count = int(count or 1)
        parts.append(name + '=' + '/'.join(str(v) for v in values[index:index+count]))
        index += count
    assert index == len(values)
    return m.PREFIX[key] + ' ' + ' '.join(parts) + '\n'

def receipt(rows):
    block = ''.join(line(key, values) for key, values in rows)
    return source.replace(CAP_END, block + CAP_END).encode()

# Quotas, PHY and SKU values below are deliberately synthetic, not observed.
base = [
    ('ARM', (2, 5, 76, 3000000, 16)),
    ('RESULT', (0, 5, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 70, 1)),
    ('TX', (2, 5, 76, 4, 5, 5, 5, 0x80000000, 1, 0, 0)),
    ('TXDESC', (1, 0x90000040, 0x90005000, 76, 0x90005000, 0x80000000, 0, 0)),
    ('RESOURCE', (1, 0x80000001, 100, 2, 80, 1, 128, 256, 0, 1, 0, 1, 1)),
    ('CAPS', (1, 1, 0, 0, 1, 1, 2, 2, 3, 6, 13)),
    ('DEBUG', (0,) * 13),
    ('DEBUGHEADER', (0,) * 10),
    ('OWNER', (1, 0, 0, 0, 0, 0, 5, 0)),
    ('WORKER', (1, 1, 1)),
]

def mutate(rows, key, at, value):
    output = []
    for name, vals in rows:
        vals = list(vals)
        if name == key:
            vals[at] = value
        output.append((name, tuple(vals)))
    return output

positive = negative = 0
def accept(rows, skip=False):
    global positive
    result = m.parse(receipt(rows))
    assert result['complete'], result
    assert result['clean_skip'] == skip
    assert result['basic_config_transferred'] != skip
    assert not result['firmware_ack'] and not result['scan_ready'] and not result['network_ready']
    positive += 1

def reject(payload):
    global negative
    result = m.parse(payload)
    assert not result['complete'] and not result['basic_config_transferred'], result
    negative += 1

accept(base)
accept(mutate(mutate(mutate(base, 'TX', 7, 0xc04c0000), 'TX', 8, 0), 'TXDESC', 5, 0xc04c0000))
accept(mutate(mutate(base, 'RESOURCE', 3, 0xffffffff), 'RESOURCE', 12, 0xfffffffe))
accept(mutate(mutate(base, 'RESOURCE', 6, 0), 'RESOURCE', 7, 0))
# Optional metadata parse failure must not invent a MAC/SKU gate for CID2.
accept([(key, (0,) * 9 + (6, 13) if key == 'CAPS' else values) for key, values in base])

for count in (1, 16):
    rows = mutate(mutate(base, 'RESULT', 10, count), 'RESULT', 11, count)
    index = (3 + count) % 16
    rows = [(key, (1, index, 2, 0x3800, 0x27, 255, 48, 65535, 65535, 0xe000, 12, 3, 0) if key == 'DEBUG'
             else (1, 0x3800ffff, 0, 0, 0, 0, 0, 0xe000ffff, 0xff27, 0) if key == 'DEBUGHEADER'
             else values) for key, values in rows]
    accept(rows)
    reject(receipt(mutate(rows, 'DEBUG', 12, 1)))
    reject(receipt(mutate(rows, 'DEBUG', 9, 0x3800)))
    reject(receipt(mutate(rows, 'DEBUGHEADER', 7, 0xe000fffe)))

for reason in (1, 2, 3):
    skip = []
    for key, values in base:
        if key in ('ARM', 'TX', 'TXDESC'):
            continue
        if key == 'RESULT': values = (1, 6, 0, 1, reason) + (0,) * 9
        if key == 'OWNER': values = (1, 0, 0, 0, 0, 0, 4, 0)
        if key == 'RESOURCE': values = (0,) * 13 if reason < 3 else (1, 0x80000001, 100, 0, 80, 1, 128, 256, 0, 0, 0, 0, 0)
        if key == 'CAPS' and reason < 3: values = (0,) * 11
        skip.append((key, values))
    accept(skip, True)
    reject(receipt(mutate(skip, 'RESULT', 12, 1)))
    reject(receipt(mutate(skip, 'RESOURCE', 11, 1)))
    reject(receipt(mutate(skip, 'RESULT', 4, 4)))
    if reason < 3:
        reject(receipt(mutate(skip, 'CAPS', 9, 6)))
        reject(receipt(mutate(skip, 'RESOURCE', 2, 1)))
    else:
        reject(receipt(mutate(skip, 'RESOURCE', 3, 2)))

for key in m.SCHEMA:
    reject(receipt([(name, values) for name, values in base if name != key]))
    reject(receipt(base + [next(row for row in base if row[0] == key)]))
for key, position, value in [
    ('ARM', 0, 3), ('ARM', 1, 4), ('ARM', 2, 64), ('ARM', 3, 3000001),
    ('RESULT', 0, -1), ('RESULT', 1, 4), ('RESULT', 2, 0), ('RESULT', 5, 1),
    ('RESULT', 6, 1), ('RESULT', 7, 0), ('RESULT', 8, 0), ('RESULT', 9, 0),
    ('RESULT', 9, 3001), ('RESULT', 10, 17), ('RESULT', 11, 1),
    ('RESULT', 12, 0), ('RESULT', 13, 2),
    ('TX', 0, 3), ('TX', 1, 6), ('TX', 2, 77), ('TX', 3, 5),
    ('TX', 4, 6), ('TX', 5, 4), ('TX', 6, 4), ('TX', 7, 0),
    ('TX', 8, 0), ('TX', 9, 1), ('TX', 10, 1),
    ('TXDESC', 0, 0), ('TXDESC', 0, 2), ('TXDESC', 1, 0x90000050),
    ('TXDESC', 2, 0x90006000), ('TXDESC', 3, 75), ('TXDESC', 4, 1 << 32),
    ('TXDESC', 5, 0), ('TXDESC', 6, 1 << 32), ('TXDESC', 7, 1 << 32),
    ('RESOURCE', 0, 0), ('RESOURCE', 1, 0), ('RESOURCE', 3, 0),
    ('RESOURCE', 6, 65536), ('RESOURCE', 8, 256), ('RESOURCE', 9, 2),
    ('RESOURCE', 10, 1), ('RESOURCE', 11, 0), ('RESOURCE', 12, 2),
    ('CAPS', 0, 2), ('CAPS', 2, 256), ('CAPS', 9, 5), ('CAPS', 10, 14),
    ('DEBUG', 0, 1), ('DEBUGHEADER', 1, 1), ('OWNER', 0, 0),
    ('OWNER', 1, 1), ('OWNER', 2, 1), ('OWNER', 3, 1), ('OWNER', 4, 1),
    ('OWNER', 5, 1), ('OWNER', 6, 4), ('OWNER', 7, 1), ('WORKER', 0, 0),
]: reject(receipt(mutate(base, key, position, value)))

for index in range(len(base)-1):
    rows = list(base)
    rows[index], rows[index+1] = rows[index+1], rows[index]
    reject(receipt(rows))

payload = receipt(base)
reject(data)  # Old successful image is not a BASIC receipt.
reject(payload.replace(b'NORD2 WIFI BASIC ARM:', b'NORD2 WIFI BASIC UNKNOWN:', 1))
reject(payload.replace(b'publications=1\n', b'publications=1', 1))
reject(payload.replace(b'callbacks=70', b'callbacks=4294967296', 1))
reject(payload.replace(b'callbacks=70', b'callbacks=' + b'9'*100, 1))
reject(payload.replace(b'callbacks=70', b'callbacks=-1', 1))
reject(payload + b'\xff')
reject(b'x' * (m.LIMIT + 1))
reject(payload.replace(b'NORD2 WIFI CAP: status=0', b'NORD2 WIFI CAP: status=1', 1))
block = b''.join(line(key, values).encode() for key, values in base)
reject(block + data)
reject(data + block)

# Source-parity: every non-DONE control bit and all returned noncontrol
# words may change; none authorizes an address or changes source software extent.
for control in [0x80000000, 0xc04c0000, 0xc0000000, 0xffffffff] + [0x80000000 | (1 << bit) for bit in range(31)]:
    rows=mutate(mutate(mutate(base,'TX',7,control),'TX',8,int(control==0x80000000)),'TXDESC',5,control)
    rows=mutate(mutate(mutate(rows,'TXDESC',4,0xffffffff),'TXDESC',6,0x12345678),'TXDESC',7,0xffffffff)
    accept(rows)
    reject(receipt(mutate(rows,'TXDESC',5,control & 0x7fffffff)))
    reject(receipt(mutate(mutate(mutate(rows,'TX',7,control & 0x7fffffff),'TX',8,0),'TXDESC',5,control & 0x7fffffff)))
# Last copied observation count may be less than polls, but never greater.
accept(mutate(mutate(base,'RESULT',9,3000),'TXDESC',0,3000))
reject((ROOT/'research/nord2/wifi-basic-config/ios/basic-failure-full.log').read_bytes())

print(json.dumps({'result': 'PASS_SYNTHETIC_BASIC_RECEIPT_TESTS', 'positive': positive,
                  'negative': negative, 'actual_basic_hardware_receipt': False}))
