#!/usr/bin/env python3
"""Before/after compile outcome per (pair, combo) between two gate_replay result sets."""
import json, sys, collections
def load(p):
    d = {}
    for l in open(p):
        r = json.loads(l)
        for c in r['combos']:
            x = c['result']; d[(r['pair'], c['tag'])] = (x.get('compile_rc'), (x.get('compile_error') or '')[:80], x.get('fs_tls'), x.get('vs_tls'))
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
keys = sorted(set(a) & set(b)); t = collections.Counter(); changed = []
for k in keys:
    o, n = a[k][0] == 0, b[k][0] == 0
    t[('ok' if o else 'ref') + '->' + ('ok' if n else 'ref')] += 1
    if o != n: changed.append((k, a[k], b[k]))
print('common', len(keys), 'only_before', len(set(a) - set(b)), 'only_after', len(set(b) - set(a)))
for k, v in t.most_common(): print(v, k)
focus = collections.Counter()
for (p, tag), o, n in changed: focus[(p.split('|')[1], o[1][:50], 'ok' if n[0] == 0 else n[1][:60])] += 1
for k, v in sorted(focus.items()): print(' ', v, k)
reg = [(k, o, n) for k, o, n in changed if o[0] == 0]
print('regressions', len(reg)); [print('  REG', k, n) for k, o, n in reg[:20]]
