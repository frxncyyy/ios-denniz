#!/usr/bin/env python3
"""Validate captured BASIC_CONFIG telemetry, without granting hardware authority.

Completion proves the bounded transport receipt, never a firmware ACK or a
usable network. A clean prerequisite skip is reported separately. No payload,
MAC address, calibration data or nearby network identifiers are returned.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re

P = Path(__file__).resolve().parent
ROOT = P.parents[3]
LIMIT = 8 * 1024 * 1024
SCHEMA = {
    'ARM': 'cid seq bytes deadline_us rx_budget',
    'RESULT': 'status phase complete skipped reason halt error submit txdone polls debug rearmed callbacks publications',
    'TX': 'cid seq bytes from to completed didx control consumed ack_expected firmware_ack',
    'RESOURCE': 'valid version pse/2 ple/2 pages/2 extra debit/2 reserved remaining',
    'CAPS': 'valid sku efuse country phy band5 bw nss path known unknown',
    'DEBUG': 'valid index kind type eid seq bytes declared/2 inner id format length',
    'DEBUGHEADER': 'valid raw/9',
    'OWNER': 'worker native_error native_halt init_halt transport_halt firmware_halt completed lease',
    'WORKER': 'parking retained callbacks_complete',
}
NUM = r'(0x[0-9a-fA-F]+|[0-9]+)'
PREFIX = {key: 'NORD2 WIFI BASIC' + ('' if key == 'RESULT' else ' ' + key) + ':' for key in SCHEMA}

def pattern(key):
    fields = []
    for token in SCHEMA[key].split():
        name, _, count = token.partition('/')
        num = r'(-?[0-9]+)' if name == 'status' else NUM
        fields.append(name + '=' + '/'.join([num] * int(count or 1)))
    return re.compile(re.escape(PREFIX[key]) + ' ' + ' '.join(fields) + '$')

RX = {key: pattern(key) for key in SCHEMA}

def dependency():
    dep = json.loads((P / 'parser-dependency.json').read_text())
    expected = {
        'path': 'research/nord2/wifi-capability-format/ios/parse-capability.py',
        'sha256': 'bdda5dd9bba42c0f667e57eda9747f324301130ee18b9418bd55638768a330ba',
        'dependency_path': 'research/nord2/wifi-capability-format/ios/parser-dependency.json',
        'dependency_sha256': '1831bc8948965a0c1302584943f5760223cf0fe5848192c112b23cc76b586c2d',
    }
    for key, value in expected.items():
        if dep.get(key) != value:
            raise ValueError('capability dependency identity')
    for path_key, hash_key in [('path', 'sha256'), ('dependency_path', 'dependency_sha256')]:
        if hashlib.sha256((ROOT / dep[path_key]).read_bytes()).hexdigest() != dep[hash_key]:
            raise ValueError('capability dependency contents')
    spec = importlib.util.spec_from_file_location('basic_capability_receipt', ROOT / dep['path'])
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def parse(data):
    result = dict(complete=False, classification='incomplete_or_conflicting',
                  basic_config_transferred=False, clean_skip=False,
                  firmware_ack=False, scan_ready=False, network_ready=False,
                  issues=[], sha256=hashlib.sha256(data).hexdigest())
    issues = result['issues']
    if len(data) > LIMIT:
        issues.append('input>8MiB'); return result
    try:
        text = data.decode('utf8')
    except UnicodeDecodeError:
        issues.append('invalid UTF8'); return result
    rows, order, kept, cap_positions = {}, [], [], {}
    for number, line in enumerate(text.splitlines(keepends=True)):
        if 'NORD2 WIFI CAP EVENTHEADER:' in line:
            cap_positions['last_header'] = number
        if 'NORD2 WIFI CAP WORKER:' in line:
            cap_positions['worker'] = number
        if 'NORD2 WIFI CAP TLV:' in line:
            cap_positions['tlv'] = line
        if 'NORD2 WIFI BASIC' not in line:
            kept.append(line); continue
        body = line[line.index('NORD2 WIFI BASIC'):].rstrip('\r\n')
        if not line.endswith('\n') or len(body.encode()) >= 255 or line.count('NORD2 WIFI') != 1:
            issues.append('truncated/interleaved BASIC record'); continue
        found = [(key, regex.fullmatch(body)) for key, regex in RX.items()]
        found = [(key, match) for key, match in found if match]
        if len(found) != 1:
            issues.append('unknown/malformed BASIC record'); continue
        key, match = found[0]
        values = tuple(int(v, 16 if v.startswith('0x') else 10) if len(v) < 23 else 1 << 65 for v in match.groups())
        if any(not (-(1 << 31) <= v < 1 << 31) if key == 'RESULT' and i == 0 else not (0 <= v < 1 << 32) for i, v in enumerate(values)):
            issues.append('numeric width BASIC ' + key)
        if key in rows:
            issues.append('duplicate BASIC ' + key)
        rows[key] = values
        order.append((number, key))
    try:
        cap = dependency().parse(''.join(kept).encode())
    except (OSError, ValueError, KeyError, TypeError, ImportError):
        issues.append('pinned capability dependency unavailable'); return result
    if not cap.get('complete'):
        issues.append('capability receipt incomplete/conflicting')
    required = set(SCHEMA) - {'ARM', 'TX'}
    if not required <= set(rows):
        issues.append('missing BASIC records'); return result

    def check(ok, why):
        if not ok:
            issues.append(why)

    status = rows['RESULT']
    rc, phase, complete, skipped, reason, halt, error, submit, txdone, polls, debug, rearmed, callbacks, publications = status
    skip = skipped == 1
    expected_order = ([] if skip else ['ARM']) + ['RESULT'] + ([] if skip else ['TX']) + ['RESOURCE', 'CAPS', 'DEBUG', 'DEBUGHEADER', 'OWNER', 'WORKER']
    check([key for _, key in order] == expected_order, 'BASIC source order/record set')
    check(cap_positions.get('last_header', 1 << 32) < order[0][0] and order[-1][0] < cap_positions.get('worker', -1), 'BASIC outside capability worker lifetime')
    check(rows['WORKER'] == (1, 1, 1), 'worker parking incomplete')
    check(rows['OWNER'] == (1, 0, 0, 0, 0, 0, 4 if skip else 5, 0), 'owner/closed lease contradiction')
    valid, version, pse_data, pse_cmd, ple_data, ple_cmd, pse_page, ple_page, extra, debit_pse, debit_ple, reserved, remaining = rows['RESOURCE']
    check(valid in (0, 1) and pse_page <= 65535 and ple_page <= 65535 and extra <= 255, 'resource metadata width')
    if skip:
        # nni_basic's fixed76-byte/zero-option call cannot fail in this version;
        # retain the native defensive branch, but do not certify an impossible
        # reason4 receipt as a clean skip.
        check(status[:4] == (1, 6, 0, 1) and 1 <= reason <= 3 and not any(status[5:]), 'skip performed work or halted')
        check(not (debit_pse or debit_ple or reserved), 'skip spent resource')
        check(remaining == 0, 'skip budget contradiction')
        if reason < 3:
            check(not any(rows['RESOURCE']) and not any(rows['CAPS']), 'skip before decode contains metadata')
        elif reason == 3:
            check(not valid or version != 0x80000001 or pse_cmd == 0, 'resource skip without refused prerequisite')
    else:
        check(status[:9] == (0, 5, 1, 0, 0, 0, 0, 1, 1), 'BASIC completion contradiction')
        check(1 <= polls <= 3000 and 0 <= debug <= 16 and rearmed == debug and callbacks > 0 and publications == 1, 'bounded transport receipt contradiction')
        check(rows.get('ARM') == (2, 5, 76, 3000000, 16), 'BASIC ARM contradiction')
        tx = rows.get('TX', ())
        check(len(tx) == 11 and tx[:7] == (2, 5, 76, 4, 5, 5, 5) and tx[9:] == (0, 0), 'CID2 sequence/index/ACK contradiction')
        if len(tx) == 11:
            check((tx[7], tx[8]) in ((0x80000000, 1), (0xc04c0000, 0)), 'TX DDONE/writeback contradiction')
        check(valid == 1 and version == 0x80000001 and pse_cmd > 0 and (debit_pse, debit_ple, reserved) == (1, 0, 1) and remaining == pse_cmd - 1, 'TC4 one-command accounting contradiction')

    meta = rows['CAPS']
    mvalid, sku, efuse, country, phy, band5, bw, nss, path, known, unknown = meta
    check(mvalid in (0, 1) and sku in (0, 1) and phy in (0, 1) and all(v <= 255 for v in (efuse, band5, bw, nss, path)), 'capability metadata width')
    if not mvalid:
        check(not any(meta[1:9]), 'invalid metadata contains decoded values')
    else:
        check(sku or (efuse == country == 0), 'absent SKU has values')
        check(phy or not any((band5, bw, nss, path)), 'absent PHY has values')
    # The first two skip reasons precede re-decoding of the retained frame.
    if not skip or reason >= 3:
        tlv = cap_positions.get('tlv', '')
        fields = dict(re.findall(r'(\w+)=(0x[0-9a-fA-F]+|[0-9]+)', tlv))
        try:
            value = lambda key: int(fields[key], 16 if fields[key].startswith('0x') else 10)
            check((known, unknown, valid, version) == (value('known'), value('unknown'), value('resources'), value('version')), 'BASIC versus owned capability metadata contradiction')
        except (KeyError, ValueError):
            issues.append('missing capability TLV correlation')

    d = rows['DEBUG']
    h = rows['DEBUGHEADER']
    if debug == 0:
        check(not any(d) and not any(h), 'debug metadata without consumed event')
    else:
        v, index, kind, outer, eid, seq, size, rxd, declared, inner, ident, fmt, length = d
        check(v == h[0] == 1 and kind == 2 and outer <= 65535 and (outer & 0x380f) == 0x3800 and eid == 0x27 and inner == 0xe000 and fmt == 3, 'debug classification contradiction')
        check(48 <= size <= 3840 and length <= min(1200, size - 48) and seq <= 255 and rxd <= 65535 and declared <= 65535 and ident <= 65535, 'debug extent/width contradiction')
        check(index == (2 + len(cap.get('headers', [])) + debug) % 16, 'last debug ring index contradiction')
        words = h[1:]
        check((words[0] & 65535, words[0] >> 16, words[6] & 65535, words[6] >> 16, words[7] & 255, (words[7] >> 8) & 255) == (rxd, outer, declared, inner, eid, seq), 'debug raw header contradiction')
    if not issues:
        result.update(complete=True, clean_skip=skip, basic_config_transferred=not skip,
                      classification='prerequisite_skip_worker_parked' if skip else 'basic_config_tx_completed_worker_parked',
                      capability_reply=True, fw_ready=True, records=rows)
    return result

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    args = ap.parse_args()
    with args.log.open('rb') as f:
        receipt = parse(f.read(LIMIT + 1))
    print(json.dumps(receipt, indent=2))
    raise SystemExit(0 if receipt['complete'] else 2)
