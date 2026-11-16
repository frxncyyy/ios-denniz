#!/usr/bin/env python3
"""Compare complete, pixel-exact private GPU probes from the same fixture build.

Times include synchronous submission costs; they are neither GPU cycles nor FPS.
Requires one USB segment per input. Case numbers are fixture-build-specific.
Missing/corrupted records are explicitly excluded from the comparison.
"""
import argparse
import json
from pathlib import Path
import re


def probes(text):
    endings = re.findall(r'TEGU_TILE_PROBE end cases=(\d+) failed=(\d+) error=(-?\d+) result=(\w+)', text)
    if len(endings) != 1 or endings[0][1:] != ('0', '0', 'PASS'):
        raise ValueError('need one complete passing probe run')
    rows, excluded, seen = {}, [], set()
    required = {'case','size','draws','duplicate','clear','half','rc','differing','bad','refbad',
                'maxdiff','guards','changed','ref_us','tile_us'}
    for match in re.finditer(r'TEGU_(TILE|WIDE|MEDIUM|PLAIN)_PROBE case=([^\r\n]+)', text):
        fields = dict(re.findall(r'(\w+)=([^\s]+)', 'case='+match[2]))
        case = int(fields['case'])
        if case in seen:
            raise ValueError('duplicate probe: use one USB segment')
        seen.add(case)
        if not required <= fields.keys():
            excluded.append(dict(case=case, reason='missing fields', fields=sorted(required-fields.keys())))
            continue
        key = (match[1], *(fields[k] for k in ('case','size','draws','duplicate','clear','half')))
        if key in rows:
            raise ValueError('duplicate probe: use one USB segment')
        if any(int(fields[k]) for k in ('rc','differing','bad','refbad','maxdiff','guards')) or int(fields['changed']) <= 0:
            raise ValueError('probe is not pixel-exact')
        times = {k: int(fields[k]) for k in ('ref_us','tile_us')}
        if min(times.values()) <= 0:
            raise ValueError('invalid timing')
        rows[key] = times
    expected = set(range(int(endings[0][0])))
    if not seen or not seen <= expected:
        raise ValueError('missing or out-of-range probe records')
    excluded.extend(dict(case=case, reason='missing record') for case in sorted(expected-seen))
    return rows, excluded


def compare(control, candidate):
    (a, ax), (b, bx) = probes(control), probes(candidate)
    ac, bc = {int(k[1]): k for k in a}, {int(k[1]): k for k in b}
    for case in ac.keys() & bc.keys():
        if ac[case] != bc[case]:
            raise ValueError('fixture cases differ; compare the same probe build')
    if set(ac) | {r['case'] for r in ax} != set(bc) | {r['case'] for r in bx}:
        raise ValueError('fixture case counts differ')
    common = a.keys() & b.keys()
    if not common:
        raise ValueError('no complete matching cases')
    rows = [dict(kind=k[0], case=int(k[1]), size=k[2], draws=int(k[3]), duplicate=int(k[4]),
                 clear=int(k[5]), half=int(k[6]), control_us=a[k]['tile_us'],
                 candidate_us=b[k]['tile_us'], ratio=b[k]['tile_us']/a[k]['tile_us']) for k in a if k in common]
    return dict(compared_cases=len(rows), control_excluded=ax, candidate_excluded=bx,
                control_unmatched=sorted(int(k[1]) for k in a.keys()-common),
                candidate_unmatched=sorted(int(k[1]) for k in b.keys()-common), cases=rows)



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('control', type=Path)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    try:
        rows = compare(args.control.read_text(errors='replace'), args.candidate.read_text(errors='replace'))
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(rows, indent=2))
