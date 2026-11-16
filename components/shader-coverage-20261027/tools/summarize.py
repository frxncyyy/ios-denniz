#!/usr/bin/env python3
"""Summarize a replay run: outcome counts per kind/class and root-cause groups ranked
by blocked functions (CoreAnimation and UI first)."""
import collections, json, re, sys
from pathlib import Path

CLASSES = ['coreanimation', 'ui_frameworks', 'camera_video', 'mps_ml', '3d_maps_ar', 'other']


def cause(r):
    o = r['outcome']
    if o == 'ok':
        return 'ok'
    if o == 'timeout':
        return 'timeout'
    if o == 'crash':
        c = r.get('crash') or ''
        c = re.sub(r'0x[0-9a-f]+', 'X', c)
        return 'crash: ' + c[:140]
    e = (r.get('error') or '').strip()
    if not e and r.get('compile_rc') == 1 and r.get('translation') == 'translated':
        e = 'options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet)'
    stage = 'translate' if r.get('translation') != 'translated' else 'mali'
    e = re.sub(r'FunctionConstant \{ index: \d+ \}', 'FunctionConstant', e)
    e = re.sub(r'ColorAttachment \{ index: \d+ \}', 'ColorAttachment', e)
    e = re.sub(r'VertexAttribute \{ attribute: \d+ \}', 'VertexAttribute', e)
    e = re.sub(r'`[^`]*`', '`..`', e)
    e = re.sub(r'\b\d{2,}\b', 'N', e)
    return f'{stage}: ' + e[:160]


def main(run, prev=None):
    rows = [json.loads(l) for l in open(Path(run) / 'results.jsonl')]
    rows = list({r['id']: r for r in rows}.values())
    out = {'total': len(rows), 'by_kind_class': {}, 'causes': []}
    t = collections.defaultdict(collections.Counter)
    for r in rows:
        t[(r['kind'], r['class'])][r['outcome']] += 1
    for k in sorted(t, key=lambda k: (k[0], CLASSES.index(k[1]))):
        out['by_kind_class']['%s/%s' % k] = dict(t[k])
    g = collections.defaultdict(list)
    for r in rows:
        c = cause(r)
        if c != 'ok':
            g[c].append(r)
    ranked = sorted(g.items(), key=lambda kv: (-sum(1 for r in kv[1] if r['class'] == 'coreanimation'),
                                               -sum(1 for r in kv[1] if r['class'] == 'ui_frameworks'), -len(kv[1])))
    for c, rs in ranked:
        cc = collections.Counter(r['class'] for r in rs)
        out['causes'].append({'cause': c, 'count': len(rs), 'by_class': dict(cc), 'kinds': dict(collections.Counter(r['kind'] for r in rs)),
                              'examples': [r['id'] + ' ' + r['name'][:60] for r in rs[:4]]})
    json.dump(out, open(Path(run) / 'summary.json', 'w'), indent=1)
    print('total', len(rows))
    for k, v in out['by_kind_class'].items():
        print(f'  {k:28s} {v}')
    print('outcomes', dict(collections.Counter(r['outcome'] for r in rows)))
    for c in out['causes'][:int(sys.argv[2]) if len(sys.argv) > 2 else 40]:
        print(f"{c['count']:5d} {c['by_class']} {c['cause']}")


if __name__ == '__main__':
    main(sys.argv[1])
