#!/usr/bin/env python3
"""Summarise pipeline-cache misses from device logs and misses.txt, and say how
much of them a pack (make_pipeline_pack.py) would serve.  Host-only.

Inputs
  LOG...           xnu-live.log / probe.log with TEGU_PIPELINE_CACHE and
                   TEGU_PSO_MISS lines (key prefix only: 16 hex of the 64-hex
                   key, so the log alone cannot rebuild a pack).
  --misses FILE    <cache dir>/misses.txt pulled from the device: one
                   "TPC_MISS v1 air|mali key=<64 hex> ..." record per miss with
                   everything the key covers (libraries, constants, formats,
                   blends, vertex layout, profile hash).  This is the replay
                   record: gpu/metal/tegu/pipeline_corpus.py turns it (or the
                   TEGU_PSO_REC console copy of it, counted below) into the
                   corpus gpu/tools/build_pipeline_pack.py replays.
  --pack FILE      pack to check coverage against (key prefix match per kind);
                   needs --build/--bundle only to verify the pack's build tag.

Keys contain the build id, so two builds never share a key: coverage of a pack
is only meaningful for logs of the very build the pack was made for.
"""
import argparse
import collections
import json
from pathlib import Path
import re
import struct
import sys

MISS = re.compile(r'TEGU_PSO_MISS (air|mali) key=([0-9a-f]+) (.*)')
CACHE = re.compile(r'TEGU_PIPELINE_CACHE enabled=(\d) disk=(\S+) build=(\S+) pack=(\S+) entries=(\d+)')
RECORD = re.compile(r'TPC_MISS v1 (air|mali) key=([0-9a-f]{64}) (.*)')
PROCESS = re.compile(r'\|([A-Za-z0-9_.-]+)\|')


def kv(text):
    out = {}
    for word in text.split():
        if '=' in word:
            k, v = word.split('=', 1)
            out[k] = v
    return out


def parse_log(path):
    caches, misses = [], []
    for line in Path(path).read_text(errors='replace').splitlines():
        proc = PROCESS.match(line)
        proc = proc.group(1) if proc else '-'
        for m in CACHE.finditer(line):
            caches.append({'process': proc, 'enabled': int(m[1]), 'disk': m[2], 'build': m[3], 'pack': m[4], 'entries': int(m[5])})
        for m in MISS.finditer(line):
            f = kv(m[3])
            if 'ms' not in f:
                continue  # line cut by the USB log ring
            misses.append({'process': proc, 'kind': m[1], 'key16': m[2][:16], 'vertex': f.get('v'), 'fragment': f.get('f'),
                           'variant': f.get('variant') or f.get('entry'), 'fmt': f.get('fmt'), 'targets': f.get('targets'),
                           'ms': int(re.match(r'\d+', f['ms'])[0]), 'status': f.get('status') or (m[3].split()[-1])})
    return caches, misses


def parse_records(path):
    out = []
    for line in Path(path).read_text(errors='replace').splitlines():
        m = RECORD.match(line)
        if m:
            f = kv(m[3])
            out.append({'kind': m[1], 'key': m[2], 'ms': int(f.get('ms', 0)), 'fields': f})
    return out


def pack_keys(path):
    data = Path(path).read_bytes()
    if data[:4] != b'TGPK':
        sys.exit('not a pipeline pack')
    count = struct.unpack_from('<I', data, 8)[0]
    keys = {1: set(), 2: set()}
    for i in range(count):
        kind = struct.unpack_from('<I', data, 32 + 88 * i)[0]
        keys.setdefault(kind, set()).add(data[40 + 88 * i:48 + 88 * i].hex())
    return keys, data[16:24].hex()


def classes(misses):
    rows = collections.OrderedDict()
    for m in misses:
        k = (m['kind'], m['fragment'] or m['variant'], m['variant'], m['fmt'], m['targets'])
        r = rows.setdefault(k, {'misses': 0, 'ms': 0, 'keys': set()})
        r['misses'] += 1
        r['ms'] += m['ms']
        r['keys'].add(m['key16'])
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('logs', nargs='*')
    p.add_argument('--misses', type=Path)
    p.add_argument('--pack', type=Path)
    p.add_argument('--json', type=Path, help='write the full ledger here')
    a = p.parse_args()
    ledger = {'logs': {}, 'cross_log_key_overlap': {}}
    keysets = {}
    for log in a.logs:
        caches, misses = parse_log(log)
        rows = classes(misses)
        total = sum(m['ms'] for m in misses)
        distinct = {(m['process'], m['key16']) for m in misses}
        entry = {'caches': caches, 'miss_lines': len(misses), 'distinct_process_keys': len(distinct), 'miss_ms': total,
                 'by_class': [{'kind': k[0], 'fragment': k[1], 'variant': k[2], 'fmt': k[3], 'targets': k[4],
                               'misses': r['misses'], 'ms': r['ms'], 'distinct_keys': len(r['keys'])} for k, r in
                              sorted(rows.items(), key=lambda kr: -kr[1]['ms'])],
                 'misses': misses}
        if a.pack:
            keys, tag = pack_keys(a.pack)
            kind_id = {'air': 1, 'mali': 2}
            served = [m for m in misses if m['key16'] in keys.get(kind_id[m['kind']], set())]
            entry['pack'] = {'tag': tag, 'entries': sum(map(len, keys.values())), 'would_serve': len(served),
                             'would_serve_ms': sum(m['ms'] for m in served)}
        # Console copy of the replay records (TEGU_PSO_REC chunks): complete ones can be replayed.
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'metal/tegu'))
        import pipeline_corpus
        plog = pipeline_corpus.Log(log)
        entry['console_records'] = {'complete': len(plog.records), 'incomplete': plog.incomplete_records,
                                    'by_source': dict(collections.Counter(r['src'] for r in plog.records))}
        ledger['logs'][log] = entry
        keysets[log] = {m['key16'] for m in misses}
        builds = sorted({c['build'] for c in caches})
        print(f'{log}\n  builds={builds} packs={sorted({c["pack"] for c in caches})} '
              f'disks={sorted({c["disk"] for c in caches})}')
        print(f'  {len(misses)} miss lines, {len(distinct)} distinct (process,key), {total} ms')
        cr = entry['console_records']
        print(f'  console replay records: {cr["complete"]} complete, {cr["incomplete"]} incomplete {cr["by_source"]}'
              + ('' if cr['complete'] else ' (log predates TEGU_PSO_REC: constants unknown, see pipeline_corpus.py scan)'))
        for k, r in list(sorted(rows.items(), key=lambda kr: -kr[1]['ms']))[:6]:
            print(f'    {k[0]:4} {str(k[1]):28} variant={k[2]} fmt={k[3]} targets={k[4]}: {r["misses"]} misses, '
                  f'{len(r["keys"])} keys, {r["ms"]} ms')
        if a.pack:
            print(f'  pack serves {entry["pack"]["would_serve"]}/{len(misses)} lines, {entry["pack"]["would_serve_ms"]} ms')
    names = list(keysets)
    for i, x in enumerate(names):
        for y in names[i + 1:]:
            both = keysets[x] & keysets[y]
            ledger['cross_log_key_overlap'][f'{x} | {y}'] = len(both)
            print(f'key overlap {Path(x).parent.parent.name} vs {Path(y).parent.parent.name}: {len(both)} '
                  f'(different builds share no key: the build id is in every key)')
    if a.misses:
        records = parse_records(a.misses)
        ledger['misses_txt'] = {'records': len(records), 'ms': sum(r['ms'] for r in records),
                                'air': sum(r['kind'] == 'air' for r in records)}
        print(f'{a.misses}: {len(records)} replay records, {ledger["misses_txt"]["ms"]} ms')
    if a.json:
        a.json.write_text(json.dumps(ledger, indent=1) + '\n')


if __name__ == '__main__':
    main()
