#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""opus-jpeg. Reads the HEIC-measurement run (payload-heic-measure) on the PC; never touches the phone.

  heic_report.py NAME [--no-fetch] [--live LOG]

1. fetch (default): from the root-fs mirror as the NEXT transaction's snapshot left it (autocycle/hfsx.py, read-only):
   /private/var/db/nord2-video/trace/*.log and /private/var/db/nord2-video/probe-*.jsonl -> apps-20261104/heic-fetch/NAME/
2. live: N2VIDEO "trace" lines of autocycle/runs/NAME/ui-usb/xnu-live.log (first 30 lines per process; the fallback when a
   sandboxed process could not write the trace file) + the camera diag line (HEIC offered or not).
3. summary per process and encoder instance: frame or tile session, sizes, frame/tile counts, pixel formats, every property
   set (key = value -> status), frame properties, copyProperty keys; the answer lines "TILES|FRAMES", "SIZES", "PROPERTIES".
"""
import argparse, collections, glob, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'build')
HFSX = [sys.executable, os.path.join(B, 'autocycle', 'hfsx.py'), os.path.join(B, 'mirror', 'rootfs-mirror.img')]


def fetch(name):
    o = os.path.join(B, 'apps-20261104', 'heic-fetch', name); os.makedirs(o, exist_ok=True)
    for d, pat in (('/private/var/db/nord2-video/trace', r'\.log$'), ('/private/var/db/nord2-video', r'^probe.*\.(jsonl|stderr)$')):
        ls = subprocess.run(HFSX + ['ls', d], capture_output=True, text=True).stdout
        names = [l.split()[-1] for l in ls.splitlines() if l.strip() and l.split()[0].isdigit() and re.search(pat, l.split()[-1])]
        if names:
            subprocess.run(HFSX + ['get', d, o] + names, capture_output=True)
    return o


def analyse(tag, lines, out):
    inst = collections.OrderedDict()
    head = [l for l in lines if l.startswith('vtable:')]
    for l in lines:
        m = re.match(r'#(\d+) (\w+)(.*)', l)
        if not m:
            continue
        i = inst.setdefault(m.group(1), dict(create='', start=[], frames=0, fsizes=collections.Counter(), tiles=0, tilecalls=[],
                                             props=collections.OrderedDict(), copies=collections.Counter(), fprops=collections.Counter(),
                                             other=collections.Counter(), end=''))
        verb, rest = m.group(2), m.group(3)
        if verb == 'create': i['create'] = rest.strip()
        elif verb == 'startSession': i['start'].append(rest.strip())
        elif verb == 'encodeFrame':
            i['frames'] += 1
            mm = re.search(r': (\d+x\d+) fmt=(\S+)', rest); i['fsizes'][(mm.group(1), mm.group(2)) if mm else '?'] += 1
            mp = re.search(r'props=(.*) -> ', rest); i['fprops'][mp.group(1)[:200] if mp else '?'] += 1
        elif verb in ('startTileSession', 'prepareToEncodeTiles', 'encodeTile', 'completeTiles'):
            i['tiles'] += verb == 'encodeTile'
            if len(i['tilecalls']) < 6 or verb != 'encodeTile': i['tilecalls'].append(verb + rest[:160])
        elif verb == 'setProperty':
            mm = re.match(r' (\S+) = (.*) -> (-?\d+)$', rest)
            if mm: i['props'][mm.group(1)] = '%s -> %s' % (mm.group(2)[:160], mm.group(3))
        elif verb == 'copyProperty':
            i['copies'][rest.split()[0] if rest.split() else '?'] += 1
        elif verb in ('finalize', 'invalidate'): i['end'] = verb + rest
        else: i['other'][verb] += 1
    out.append('### %s' % tag)
    for h in head[:1]:
        out.append('- encoder %s' % h)
    for k, i in inst.items():
        kind = 'TILES' if i['tiles'] or any(c.startswith('startTileSession') for c in i['tilecalls']) else 'FRAMES' if i['frames'] else 'none'
        out.append('- instance #%s `%s`: **%s**; startSession %s; frames %d %s; tiles %d' % (
            k, i['create'], kind, ', '.join(i['start']) or '-', i['frames'],
            ', '.join('%s %s x%d' % (s[0], s[1], n) if isinstance(s, tuple) else '%s x%d' % (s, n) for s, n in i['fsizes'].items()), i['tiles']))
        for c in i['tilecalls']:
            out.append('  - tile call `%s`' % c)
        for p, v in i['props'].items():
            out.append('  - set `%s` = `%s`' % (p, v))
        if i['copies']:
            out.append('  - copyProperty: ' + ', '.join('%s x%d' % kv for kv in i['copies'].most_common()))
        for fp, n in i['fprops'].most_common(3):
            out.append('  - frame properties (x%d): `%s`' % (n, fp))
        if i['other']:
            out.append('  - other calls: ' + ', '.join('%s x%d' % kv for kv in i['other'].items()))
        if i['end']:
            out.append('  - `%s`' % i['end'])
    if not inst:
        out.append('- no encoder instance in this log')


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('name'); ap.add_argument('--no-fetch', action='store_true'); ap.add_argument('--live')
    a = ap.parse_args()
    out = ['## HEIC measurement %s' % a.name, '']
    d = os.path.join(B, 'apps-20261104', 'heic-fetch', a.name) if a.no_fetch else fetch(a.name)
    logs = sorted(glob.glob(os.path.join(d, '*.log')))
    for f in logs:
        analyse('[V-phone trace file] %s' % os.path.basename(f), [l.rstrip('\n') for l in open(f, errors='replace')], out)
    if not logs:
        out.append('- no trace file fetched from %s (sandbox? switch? not fetched yet?)' % d)
    for f in sorted(glob.glob(os.path.join(d, 'probe-*.jsonl')), key=os.path.getmtime)[-1:]:
        heic = [l.strip() for l in open(f, errors='replace') if 'heic' in l.lower()]
        out.append('- n2vt_probe %s: %d HEIC lines; %s' % (os.path.basename(f), len(heic), ' | '.join(h[:200] for h in heic[:3])))
    live = a.live or os.path.join(B, 'autocycle', 'runs', a.name, 'ui-usb', 'xnu-live.log')
    if os.path.exists(live):
        L = open(live, errors='replace').read().splitlines()
        byproc = collections.defaultdict(list)
        for l in L:
            m = re.search(r'N2VIDEO (\S+)\[(\d+)\]: trace (.*)', l)
            if m: byproc['%s[%s]' % (m.group(1), m.group(2))].append(m.group(3))
        for p, ls in byproc.items():
            analyse('[V-phone live console, first lines only] %s' % p, ls, out)
        for l in L:
            if 'diag: still' in l or 'img.big.heic' in l or 'img.write/read.heic' in l:
                out.append('- live: `%s`' % l.strip()[:220])
    else:
        out.append('- live log %s absent' % live)
    print('\n'.join(out))


if __name__ == '__main__':
    sys.exit(main())
