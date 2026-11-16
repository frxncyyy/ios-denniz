#!/usr/bin/env python3
"""Streaming extractor for diag_fileprovider.sh: newest N boots of a unified-log jsonl (file-provider processes), deduplicated
per (boot, process, format string), time-ordered.  usage: fp_extract.py FP.jsonl [N=2]"""
import collections, json, re, sys

f = sys.argv[1]; n = int(sys.argv[2]) if len(sys.argv) > 2 else 2
rb = re.compile(r'"boot_uuid":"([0-9A-F]{8})'); rt = re.compile(r'"timestamp":"([^"]+)"')
last = {}
for l in open(f, errors='replace'):
    b = rb.search(l); t = rt.search(l)
    if b and t and t.group(1) > last.get(b.group(1), ''):
        last[b.group(1)] = t.group(1)
keep = sorted(last, key=last.get)[-n:]
SKIP = ('xpc_activity', 'Voucher', 'APS', 'Topics', 'Delivering', 'setMessageSize', 'container_query', 'Query;', 'Requesting container')
seen = collections.Counter(); rows = collections.defaultdict(list)
for l in open(f, errors='replace'):
    b = rb.search(l)
    if not b or b.group(1) not in keep:
        continue
    try:
        d = json.loads(l)
    except Exception:
        continue
    if d.get('log_type') not in ('Info', 'Default', 'Error', 'Fault'):
        continue
    p = (d.get('process') or '').split('/')[-1]; m = (d.get('message') or '').replace('\n', ' '); fs = d.get('format_string') or ''
    if p == 'runningboardd' and not any(k in m for k in ('LocalStorage', 'ileProvider', 'fileprovider', 'pkd', 'plugin', 'appex')):
        continue
    if any(x in fs for x in SKIP):
        continue
    k = (b.group(1), p, fs)
    if seen[k] >= 3:
        continue
    seen[k] += 1
    rows[b.group(1)].append('%s %-14s %s | %s' % (d.get('timestamp', '')[11:23], p[:14], (d.get('log_type') or '')[:3], m[:500]))
for b in keep:
    print('===== boot', b, last[b], len(rows[b]), 'lines')
    print('\n'.join(sorted(rows[b])))
