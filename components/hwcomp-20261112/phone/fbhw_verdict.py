#!/usr/bin/env python3
"""Verdict table of a display HW composition run, per kern.n2fbhw mode, from a kernel console log
(phone.py logs output, or the xnu-live.log of nord2-usb-console.py / autocycle/txn_g.sh).

usage: fbhw_verdict.py LOG [--steps OUT/steps.jsonl] [--json OUT.json] [--tear-factor 1.5]
exit: 0 = no FAIL, 1 = a FAIL, 2 = no NORD2 FB data at all.

Records (console lines can be interleaved mid-line, so records are cut at every known prefix):
  N2FBHWCTL phase I/N mode=M ... | phase_end ... [FALLBACK] | boot mode=M | done final=M | error ...   (fbhwctl job)
  NORD2 FB SYNC: k=v ...       cumulative since boot (0393)            -> per segment: last value - last value before the segment
  NORD2 FB HW: mode=M k=v ...  cumulative since boot (0396, interface in DISPLAY_HW_COMPOSE.md) -> same delta rule; any key accepted
  NORD2 FB HW MODE: old=A new=B why=sysctl|fallback|boot   at every switch (segments by it when no fbhwctl job runs; why=fallback counts)
  NORD2 FB HW FALLBACK: ...    one-off line when 0396 drops to mode 0 (any record whose tag contains FALLBACK counts)
  NORD2 FB PRESENT: n=.. copy_us=avg/max ...   per stats interval (reset after printing) -> summed
  NORD2 FB OVL: frames=.. dsi_underrun=.. ovl0[bN=c ..] ovl1[..]  per interval -> b2 FME_UND, b5-8 EOF abnormal, b9-12 SMI underflow
  NORD2 FB TEAR: / FB RACE: / FB LONG:  counted
Segments: by N2FBHWCTL phases when present; else by the HW line's mode= value; else one segment (mode from --mode, default '?').
The first stats interval after a switch straddles both modes (stats are printed every few seconds); the 120 s phases make it small.
"""
import json, re, sys
from collections import defaultdict

REC = re.compile(r'(N2FBHWCTL |NORD2 FB [A-Z][A-Z ]*?: )')
KV = re.compile(r'([a-z][a-z0-9_]*)=(-?\d+)(?:/(\d+))?')
CUM = ('SYNC', 'HW')
FAIL_HW_KEYS = ('ovl_und', 'smi_und', 'eof_abn', 'dsi_und', 'fd_timeout', 'readback_bad')   # 0396 HW keys that are always FAIL

def records(text):
    parts = REC.split(text)
    for i in range(1, len(parts) - 1, 2):
        head, body = parts[i], parts[i + 1].split('\n', 1)[0]
        tag = 'CTL' if head.startswith('N2FBHWCTL') else head[len('NORD2 FB '):-2]
        yield tag, body

def kv(body):
    d = {}
    for k, a, b in KV.findall(body):
        d[k] = int(a)
        if b: d[k + '_max'] = int(b)
    return d

class Seg:
    def __init__(self, idx, mode, label):
        self.idx, self.mode, self.label = idx, mode, label
        self.first = {}; self.last = {}; self.sum = defaultdict(int); self.cnt = defaultdict(int)
        self.fallback = []; self.notes = []; self.offset0 = None; self.offset1 = None; self.interactions = 0

def parse(text, default_mode='?'):
    segs = []; base = {}          # base[tag] = last cumulative dict before the current segment
    cur = None; have_ctl = 'N2FBHWCTL phase' in text
    def start(mode, label):
        nonlocal cur
        cur = Seg(len(segs), mode, label); segs.append(cur)
        for tag in CUM:
            if tag in base: cur.first[tag] = dict(base[tag])
    start(default_mode, 'pre' if have_ctl else 'all')
    pos = 0
    for tag, body in records(text):
        d = kv(body)
        if tag == 'CTL':
            if body.startswith('phase ') and 'set' in body:
                start(d.get('mode', '?'), body.split()[1])
            elif body.startswith('phase_end'):
                if 'FALLBACK' in body: cur.fallback.append('ctl readback=%s' % d.get('readback'))
                start(cur.mode, 'after ' + cur.label)        # time between phase_end and the next phase (normally none)
            elif body.startswith('boot '):
                if cur.label == 'pre': cur.mode = d.get('mode', cur.mode)
            elif body.startswith('done'):
                start(d.get('final', '?'), 'final')
            elif body.startswith('error'):
                cur.notes.append('ctl error: ' + body[:80])
            continue
        if 'FALLBACK' in tag:
            cur.fallback.append(body[:100]); continue
        if not have_ctl and (tag == 'HW MODE' and 'new' in d or tag == 'HW' and 'mode' in d):
            m = d['new'] if tag == 'HW MODE' else d['mode']
            if m != cur.mode:
                if cur.mode == '?' or not (cur.last or cur.sum or cur.cnt): cur.mode = m
                else: start(m, 'hw-mode')
            if tag == 'HW MODE':
                if 'fallback' in body: cur.fallback.append(body[:100])
                continue
        if tag in CUM:
            # a truncated record (interleaving) may miss keys: merge into the last value
            m = dict(cur.last.get(tag, base.get(tag, {}))); m.update(d); cur.last[tag] = m; base[tag] = m
        elif tag == 'PRESENT':
            n = d.get('n', 0); cur.sum['presents'] += n; cur.sum['copy_us_x_n'] += d.get('copy_us', 0) * n
            cur.sum['busy_forced'] += d.get('busy_forced', 0); cur.sum['timeouts'] += d.get('timeouts', 0)
            cur.sum['copy_max'] = max(cur.sum['copy_max'], d.get('copy_us_max', 0))
        elif tag == 'OVL':
            cur.sum['dsi_und'] += d.get('dsi_underrun', 0); cur.sum['ovl_frames'] += d.get('frames', 0)
            for b, c in re.findall(r'\bb(\d+)=(\d+)', body):
                b, c = int(b), int(c)
                if b == 2: cur.sum['fme_und'] += c
                elif 5 <= b <= 8: cur.sum['eof_abn'] += c
                elif 9 <= b <= 12: cur.sum['smi_und'] += c
        elif tag in ('TEAR', 'RACE', 'LONG'):
            cur.cnt[tag] += 1
    def data(s): return s.sum or s.cnt or any(s.last.get(t) != s.first.get(t) for t in s.last)
    return [s for s in segs if data(s) or s.fallback or s.notes or s.label[0].isdigit()]   # empty pre/after/final segments hidden

def delta(s, tag, k):
    a = s.last.get(tag, {}).get(k); b = s.first.get(tag, {}).get(k, 0)
    return None if a is None else a - b

def row(s):
    r = dict(seg=s.idx, label=s.label, mode=s.mode, interactions=s.interactions)
    for k in ('pushes', 'tear_none_te', 'tear_ambig', 'src_race', 'busy_after', 'copy_mismatch', 'dropped'):
        r[k] = delta(s, 'SYNC', k)
    hw = {k: delta(s, 'HW', k) for k in s.last.get('HW', {}) if k != 'mode'}
    r['hw'] = hw
    r['presents'] = s.sum['presents']
    r['copy_avg_us'] = s.sum['copy_us_x_n'] // s.sum['presents'] if s.sum['presents'] else None
    r['copy_max_us'] = s.sum['copy_max']
    for k in ('fme_und', 'smi_und', 'eof_abn', 'dsi_und', 'busy_forced', 'timeouts'): r[k] = s.sum[k]
    r['tear_lines'], r['race_lines'], r['long_lines'] = s.cnt['TEAR'], s.cnt['RACE'], s.cnt['LONG']
    r['fallback'] = s.fallback; r['notes'] = s.notes
    p = r['pushes'] or 0
    r['tear_per_1k'] = round(1000.0 * ((r['tear_none_te'] or 0) + (r['tear_ambig'] or 0)) / p, 1) if p else None
    return r

def judge(rows, factor):
    base = [r['tear_per_1k'] for r in rows if r['mode'] == 0 and r['tear_per_1k'] is not None]
    bt = sum(base) / len(base) if base else None
    for r in rows:
        why, warn = [], []
        und = r['fme_und'] + r['smi_und'] + r['eof_abn'] + r['dsi_und']
        if und: why.append('underflow ovl_b2=%d smi=%d eof=%d dsi=%d' % (r['fme_und'], r['smi_und'], r['eof_abn'], r['dsi_und']))
        if r['fallback']: why.append('fallback: ' + '; '.join(r['fallback'])[:120])
        for k in FAIL_HW_KEYS:
            if (r['hw'].get(k) or 0) > 0: why.append('%s=%d' % (k, r['hw'][k]))
        if isinstance(r['mode'], int) and r['mode'] >= 1:
            if not r['hw']: why.append('no NORD2 FB HW line in this mode')
            elif not (r['hw'].get('hw_frames') or 0): why.append('hw_frames=0 (nothing composed by the OVL)')
            if (r['hw'].get('hw_race') or 0) > 0: warn.append('hw_race=%d' % r['hw']['hw_race'])
        if r['notes']: warn += r['notes']
        if not r['pushes'] and not r['presents']: warn.append('no presents in this segment')
        for k in ('src_race', 'busy_after', 'copy_mismatch'):
            if (r[k] or 0) > 0: warn.append('%s=%d' % (k, r[k]))
        if r['busy_forced']: warn.append('busy_forced=%d' % r['busy_forced'])
        if r['timeouts']: warn.append('frame_done timeouts=%d (CPU path; the HW path reports fd_timeout)' % r['timeouts'])
        if bt is not None and r['mode'] != 0 and r['tear_per_1k'] is not None and r['tear_per_1k'] > bt * factor + 2:
            warn.append('tear %.1f/1k > mode0 %.1f x%.1f' % (r['tear_per_1k'], bt, factor))
        r['verdict'] = 'FAIL' if why else 'WARN' if warn else 'PASS'; r['why'] = why + warn
    return rows

def table(rows):
    h = ['seg', 'phase', 'mode', 'inter', 'pushes', 'presents', 'hw_fr', 'cpu_fr', 'hw%', 'tear/1k', 'src_race', 'busy_aft', 'hw_race',
         'und(b2/smi/eof/dsi)', 'fallbk', 'copy_avg', 'verdict']
    out = [h]
    for r in rows:
        hf, cf = r['hw'].get('hw_frames'), r['hw'].get('cpu_frames')
        pct = '%d' % (100 * hf // (hf + cf)) if hf is not None and cf is not None and hf + cf else '-'
        f = lambda v: '-' if v is None else str(v)
        out.append([f(r['seg']), r['label'], f(r['mode']), f(r['interactions']), f(r['pushes']), f(r['presents']), f(hf), f(cf), pct,
                    f(r['tear_per_1k']), f(r['src_race']), f(r['busy_after']), f(r['hw'].get('hw_race')),
                    '%d/%d/%d/%d' % (r['fme_und'], r['smi_und'], r['eof_abn'], r['dsi_und']), str(len(r['fallback'])), f(r['copy_avg_us']), r['verdict']])
    w = [max(len(x[i]) for x in out) for i in range(len(h))]
    lines = ['  '.join(c.rjust(w[i]) for i, c in enumerate(x)) for x in out]
    for r in rows:
        if r['why']: lines.append('  seg %d mode %s %s: %s' % (r['seg'], r['mode'], r['verdict'], '; '.join(r['why'])))
    return '\n'.join(lines)

def main(argv):
    if not argv or argv[0] in ('-h', '--help'): print(__doc__); return 0
    log = argv[0]; opt = lambda k, d=None: argv[argv.index(k) + 1] if k in argv else d
    text = open(log, 'rb').read().decode('utf-8', 'replace')
    mode = opt('--mode', '?'); mode = int(mode) if mode.isdigit() else mode
    if 'NORD2 FB ' not in text: print('no NORD2 FB lines in', log); return 2
    segs = parse(text, mode)
    st = opt('--steps')
    if st:
        # steps.jsonl from fbhw_scenario.py: each step has log_offset = bytes of the log when the step ran -> segment by offset
        # (the same events that open a segment in parse(); byte offsets == char offsets only for ASCII logs, which these are)
        cuts = [m.start() for m in re.finditer(r'N2FBHWCTL (?:phase \d+/\d+ mode=\d+ set|phase_end|done)', text)]
        byidx = {s.idx: s for s in segs}
        for line in open(st):
            e = json.loads(line)
            if e.get('kind') != 'interaction' or e.get('log_offset') is None: continue
            k = sum(1 for c in cuts if c < e['log_offset'])
            if k in byidx: byidx[k].interactions += 1
    rows = judge([row(s) for s in segs], float(opt('--tear-factor', '1.5')))
    print(table(rows))
    if opt('--json'): json.dump(rows, open(opt('--json'), 'w'), indent=1, default=str)
    return 1 if any(r['verdict'] == 'FAIL' for r in rows) else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
