#!/usr/bin/env python3
"""Extract the exact local Android workaround, without adding firmware to git.

The only accepted artifact is the file shipped with Nord 2 kernel 04d00c2.
This is build-time preparation, not a runtime general firmware loader.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED = '1a52a7f3c7c8b15e13e226d0956091621a83baafd80a3f93debb8271110e5951'

def extract(data):
    if len(data) != 440 or hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError('unqualified WA firmware; do not substitute another revision')
    if struct.unpack_from('<HHI', data) != (0x4157, 2, 416):
        raise ValueError('header')
    if struct.unpack_from('<QIIQ', data, 416) != (0x100000000, 0, 392, 6):
        raise ValueError('job descriptor')
    if struct.unpack_from('<QIIII', data, 392) != (0x100000000, 384, 6, 8, 0):
        raise ValueError('mapping descriptor')
    return data[8:392]

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('firmware', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    data = args.firmware.read_bytes()
    payload = extract(data)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    text = '/* Generated from local Android firmware; never commit this file. */\n'
    text += 'static const unsigned char valhall_wa_payload[384] = {\n'
    text += '\n'.join('    ' + ','.join(f'0x{x:02x}' for x in payload[i:i+16]) + ','
                      for i in range(0, len(payload), 16)) + '\n};\n'
    args.output.write_text(text)
    receipt = {'source': str(args.firmware.resolve()), 'sha256': EXPECTED,
               'payload_sha256': hashlib.sha256(payload).hexdigest(),
               'js': 0, 'va': '0x100000000', 'flags': 6, 'map_flags': 6,
               'generated_header_sha256': hashlib.sha256(text.encode()).hexdigest()}
    args.output.with_suffix('.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))
