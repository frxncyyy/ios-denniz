#!/usr/bin/env python3
"""Summarize 0219 firmware clock queries; these are not FPS or utilization.

gpu_busy describes only the end of each query. CPU identifies the querying
core, not the CPU cluster whose rate is queried. Use one USB capture segment.
"""
import argparse
import json
from pathlib import Path
import re

DOMAINS = {2: 'cpucl0', 3: 'cpucl1', 4: 'cpucl2', 5: 'dsu', 6: 'bci'}
PATTERN = re.compile(
    r'TEGU SYSCLK: job (\d+) token (\d+) cpu (0x[0-9a-fA-F]+) '
    r'domain (\d+) khz (\d+) rc (-?\d+) us (\d+) gpu_busy ([01])\s*$')


def summarize(text):
    seen = set()
    rows = []
    malformed = 0
    for line in text.splitlines():
        if 'TEGU SYSCLK:' not in line:
            continue
        match = PATTERN.search(line)
        if not match:
            malformed += 1
            continue
        job, token, cpu, domain, rate, rc, elapsed, busy = match.groups()
        job, token, domain, rate, rc, elapsed, busy = map(
            int, (job, token, domain, rate, rc, elapsed, busy))
        if domain not in DOMAINS:
            malformed += 1
            continue
        key = (job, domain)
        if key in seen:
            raise ValueError('duplicate sample job/domain: select one USB segment')
        seen.add(key)
        rows.append(dict(job=job, token=token, cpu=int(cpu, 16), domain=domain,
                         khz=rate, rc=rc, query_us=elapsed, gpu_busy=busy))
    report = dict(samples=len(rows), jobs=len({r['job'] for r in rows}),
                  malformed=malformed, ipc_query_us=sum(r['query_us'] for r in rows),
                  querying_mpidrs=sorted({hex(r['cpu']) for r in rows}), domains={})
    for domain, name in DOMAINS.items():
        values = [r for r in rows if r['domain'] == domain]
        valid = [r for r in values if r['rc'] == 0 and r['khz'] > 0]
        report['domains'][name] = dict(
            samples=len(values), valid_samples=len(valid),
            rates_khz=sorted({r['khz'] for r in valid}),
            gpu_unfinished_at_query_end=sum(r['gpu_busy'] for r in valid),
            invalid=len(values) - len(valid))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        result = summarize(args.log.read_text(errors='replace'))
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
