#!/usr/bin/env python3
"""Summarize 0215 reported clock rates, not bandwidth, utilization or FPS.

Only busy=1 with rc=0 proves the sampled job was unfinished at both query
boundaries. IPC query overhead is included in that job's GPU-wait timing.
USB reconnects replay records: use one capture segment.
"""
import argparse
import json
from pathlib import Path
import re

PATTERN = re.compile(
    r'TEGU MEMCLK: job (\d+) token (\d+) mif_khz (\d+) rc (-?\d+) us (\d+) busy ([01]) '
    r'int_khz (\d+) rc (-?\d+) us (\d+) busy ([01]) stopped ([01])')


def summarize(text):
    seen = set()
    rows = []
    malformed = 0
    for line in text.splitlines():
        if 'TEGU MEMCLK:' not in line:
            continue
        match = PATTERN.search(line)
        if not match:
            malformed += 1
            continue
        job, token, mif, mr, mu, mb, inter, ir, iu, ib, stopped = map(int, match.groups())
        if job in seen:
            raise ValueError('duplicate sample job: select one USB segment')
        seen.add(job)
        rows.append(dict(job=job, token=token, stopped=stopped,
                         mif=dict(khz=mif, rc=mr, query_us=mu, busy=mb),
                         interconnect=dict(khz=inter, rc=ir, query_us=iu, busy=ib)))
    report = dict(samples=len(rows), malformed=malformed,
                  stopped=sum(r['stopped'] for r in rows),
                  ipc_query_us=sum(r[d]['query_us'] for r in rows for d in ('mif', 'interconnect')))
    for domain in ('mif', 'interconnect'):
        values = [r[domain] for r in rows]
        active = [v['khz'] for v in values if not v['rc'] and v['busy']]
        report[domain] = dict(busy_samples=len(active), busy_rates_khz=sorted(set(active)),
                              completed_during_or_before_query=sum(not v['rc'] and not v['busy'] for v in values),
                              invalid=sum(v['rc'] != 0 for v in values))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        report = summarize(args.log.read_text(errors='replace'))
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
