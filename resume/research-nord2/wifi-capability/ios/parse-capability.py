#!/usr/bin/env python3
"""Strict bounded captured-only capability receipt, from actual native emitters.

No actual capability reply has been captured. Synthetic receipt tests exercise
this contract; they do not prove normal E000 RX on hardware or networking.
No frame payload or MAC address is logged or returned by this parser.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re

ROOT = Path(__file__).resolve().parents[4]
MAIN = ROOT / 'research/nord2/wifi-main-swint/evidence-v2'
MAIN_MANIFEST = '2e9052aba2a1fc7904b55e0546411d01e5b7ecee4891c614a65c40977fb0726c'
MAIN_REVIEW = 'eb87a9b997e03ddfd5db560c87cae9a781feb2aa43f5803db671706635aae3f1'
MAX_BYTES = 8 * 1024 * 1024
# Field order and multiplicity follow wifi_capability_bind.inc exactly.
SCHEMA = {
    'ARM': 'cid eid seq deadline_us event_budget',
    'RESULT': 'status phase complete halt error submit txdone events rearmed match polls',
    'RX': 'bytes irq/2 valid debug echo mismatch elapsed_us',
    'DESCRIPTOR': 'raw/4',
    'TLV': 'elements known unknown mac_present mac_valid resources version product fw/2',
    'OWNER': 'native_error native_halt init_halt driver dmairq/2 valid parked',
    'EVENT': 'n kind type eid seq bytes declared/2 debug_type debug_bytes',
    'WORKER': 'parking retained callbacks_complete',
}
PREFIX = {key: 'NORD2 WIFI CAP' + ('' if key == 'RESULT' else ' ' + key) + ':' for key in SCHEMA}
NUM = r'(0x[0-9a-fA-F]+|[0-9]+)'
def pattern(key):
    fields = []
    for token in SCHEMA[key].split():
        name, _, count = token.partition('/')
        fields.append(name + '=' + '/'.join([NUM] * int(count or 1)))
    return re.compile(re.escape(PREFIX[key]) + ' ' + ' '.join(fields) + '$')
RX = {key: pattern(key) for key in SCHEMA}

def main_parser():
    manifest = (MAIN / 'manifest.json').read_bytes()
    if hashlib.sha256(manifest).hexdigest() != MAIN_MANIFEST:
        raise ValueError('main dependency manifest pin')
    metadata = json.loads(manifest)
    for path, wanted in list(metadata['files'].items()) + list(metadata['dependencies'].items()):
        target = MAIN / path if path in metadata['files'] else ROOT / path
        if hashlib.sha256(target.read_bytes()).hexdigest() != wanted:
            raise ValueError('main dependency file pin')
    review_bytes = (MAIN / 'independent-review.json').read_bytes()
    if hashlib.sha256(review_bytes).hexdigest() != MAIN_REVIEW:
        raise ValueError('main review pin')
    rom = ROOT / 'research/nord2/wifi-rom/ios/parse-evidence.py'
    if hashlib.sha256(rom.read_bytes()).hexdigest() != '662436f9227caf71132b39bdf2647941868bd33d61d917316d95a304fc522e6e':
        raise ValueError('ROM parser pin')
    review = json.loads(review_bytes)
    if review.get('result') != 'PASS_OFFLINE_PARSER_REVIEW' or review.get('candidate_manifest_sha256') != MAIN_MANIFEST or review.get('parser_sha256') != metadata['files']['parse-evidence.py']:
        raise ValueError('main independent review identity')
    spec = importlib.util.spec_from_file_location('cap_main_v2', MAIN / 'parse-evidence.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def parse(data):
    result = dict(complete=False, classification='incomplete_or_conflicting', issues=[],
                  capability_reply=False, fw_ready=False, scan_ready=False, network_ready=False,
                  sha256=hashlib.sha256(data).hexdigest())
    issues = result['issues']
    if len(data) > MAX_BYTES:
        issues.append('input>8MiB'); return result
    try:
        text = data.decode('utf8')
    except UnicodeDecodeError:
        issues.append('invalid UTF8'); return result
    rows, events, order, kept = {}, [], [], []
    main_last = 0
    for number, line in enumerate(text.splitlines(keepends=True), 1):
        if 'NORD2 WIFI MAIN' in line:
            main_last = number
        if 'NORD2 WIFI CAP' not in line:
            kept.append(line); continue
        body = line[line.index('NORD2 WIFI CAP'):].rstrip('\r\n')
        if not line.endswith('\n') or len(body.encode()) >= 255 or line.count('NORD2 WIFI') != 1:
            issues.append('truncated/interleaved capability record'); continue
        found = [(key, regex.fullmatch(body)) for key, regex in RX.items()]
        found = [(key, match) for key, match in found if match]
        if len(found) != 1:
            issues.append('unknown/malformed/fatal capability record'); continue
        key, match = found[0]
        values = tuple(int(v, 16 if v.startswith('0x') else 10) if len(v) < 23 else 1 << 65 for v in match.groups())
        if any(v >= (1 << (64 if key == 'RX' and i == 7 else 32)) for i, v in enumerate(values)):
            issues.append('numeric width capability ' + key)
        order.append((number, key))
        if key == 'EVENT':
            if len(events) < 16: events.append(values)
            else: issues.append('event budget exceeded')
        else:
            if key in rows: issues.append('duplicate capability ' + key)
            rows[key] = values
    try:
        main = main_parser().parse(''.join(kept).encode())
    except (OSError, ValueError, KeyError, TypeError, ImportError):
        issues.append('main dependency unavailable/unpinned'); return result
    if not main.get('complete'):
        issues.append('main FW_READY receipt incomplete/conflicting')
    required = set(SCHEMA) - {'EVENT'}
    if set(rows) != required:
        issues.append('missing capability record set')
    if set(rows) == required:
        def check(ok, why):
            if not ok: issues.append(why)
        arm, status, rx, desc, tlv, owner, worker = (rows[k] for k in ('ARM','RESULT','RX','DESCRIPTOR','TLV','OWNER','WORKER'))
        check(arm == (0x8a, 0xec, 4, 3000000, 16), 'capability ARM contradiction')
        count = status[7]
        check(status[:7] == (0,5,1,0,0,1,1) and 1 <= count <= 16 and status[8:10] == (count,1) and count + 1 <= status[10] <= 3000, 'capability completion contradiction')
        check(len(events) == count, 'event row count contradiction')
        check(rx[3] == 3 and not ((rx[1] | rx[2]) & 0x00300000) and 0 <= rx[7] < 3000000, 'capability IRQ/deadline contradiction')
        check(owner[:4] == (0,0,0,1) and owner[6:] == (3,0) and not ((owner[4] | owner[5]) & 0x00300000), 'capability owner contradiction')
        check(worker == (1,1,1), 'worker retained parking missing')
        # Final descriptor is the rearmed RX slot, not the consumed TX/RX tuple.
        check(desc == (0x90006000 + ((2 + count) % 16) * 4096, 3840 << 16, 0,0), 'rearmed descriptor contradiction')
        check([key for _, key in order] == ['ARM','RESULT','RX','DESCRIPTOR','TLV','OWNER'] + ['EVENT'] * len(events) + ['WORKER'], 'capability source order')
        check(main_last < order[0][0], 'capability before main receipt')
        debug = 0
        for index, event in enumerate(events):
            n, kind, rx_class, eid, seq, size, rxd, declared, debug_type, debug_bytes = event
            check(n == index and (rx_class & 0x380f) == 0x3800 and rx_class <= 0xffff and seq <= 255 and rxd <= 0xffff and declared <= 0xffff and 36 <= size <= 3840, 'event index/type/extent contradiction')
            if index == len(events)-1:
                check(kind == 1 and eid == 0xec and size >= 40 and debug_type == debug_bytes == 0, 'final capability event contradiction')
                check(rx[0] == size and rx[5] == int(seq == arm[2]) and rx[6] == int(seq != arm[2]), 'captured sequence/extent contradiction')
            else:
                check(kind == 2 and eid == 0x27 and size >= 48 and debug_type == 3 and debug_bytes <= 1200 and debug_bytes <= size-48, 'preceding bounded debug event contradiction')
                debug += 1
        check(rx[4] == debug, 'debug count contradiction')
        elements, known, unknown, present, valid, resources, version, product, fw, build = tlv
        check(known <= 6 and elements == known + unknown and elements <= max(0, (rx[0]-40)//8), 'TLV count/extent contradiction')
        check(present in (0,1) and valid in (0,1) and valid <= present and resources in (0,1) and present <= known and resources <= known and product <= 0xffff and fw <= 0xffff and build <= 0xffff, 'TLV metadata contradiction')
        inferred_tags = present + int(bool(resources or version)) + int(bool(product)) + int(bool(fw or build))
        check(inferred_tags <= known, 'TLV known tag contradiction')
        body_minimum = 4*known + 4*present + (24 if version == 0x80000001 else 12 if resources else 0) + 16*int(bool(product)) + 20*int(bool(fw or build))
        check(40 + 8*elements + body_minimum <= rx[0], 'TLV minimum body extent contradiction')
        check((version == 0 and resources in (0,1)) or (version == 0x80000001 and resources == 1) or (version & 0x80000000 and version != 0x80000001 and resources == 0), 'resource version contradiction')
    if not issues:
        result.update(complete=True, classification='capability_reply_worker_parked', capability_reply=True,
                      fw_ready=True, records=rows, events=events)
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    with args.log.open('rb') as source:
        receipt = parse(source.read(MAX_BYTES + 1))
    print(json.dumps(receipt, indent=2))
    raise SystemExit(0 if receipt['complete'] else 2)
