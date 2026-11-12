#!/usr/bin/env python3
"""0397: summarize 'NORD2 FB PACE:' lines (kernel console) or one kern.n2fbpace string into a 'fixed 90 fps' verdict.

  pace_report.py LOG            per-window table + totals per kern.n2fbhw segment + verdict
  pace_report.py --stat 'STR'   verdict for one `sysctl -n kern.n2fbpace` read (bracket a scenario with kern.n2fbpacereset=1)

FIXED90 PASS needs: panel 88.5..90.5 Hz (TE 11.05..11.30 ms), >= 50 active intervals, k1 >= 98 % of active intervals
(refresh_per_present_1), active_fps >= 87.  Otherwise WARN (k1 >= 90 %) or FAIL, with the reason.
"""
import re, sys

KV = re.compile(r'([a-z0-9_<=./+]+)=([0-9./]+)')

def parse(s):
    d = {}
    for k, v in KV.findall(s):
        d[k] = [float(x) for x in v.split('/')] if '/' in v else float(v)
    return d

def add(t, d):
    for k in ('pushes', 'active', 'idle_gaps', 'dropped'):
        t[k] = t.get(k, 0) + d.get(k, 0)
    for k in ('refresh_per_present_1/2/3/4/5+', 'ivl_ms<=12.5/18/24/35/50/100', 'rate_s<30/45/62/75/85/88/88+'):
        if k in d:
            t[k] = [a + b for a, b in zip(t.get(k, [0] * len(d[k])), d[k])]
    if d.get('active'):
        t['act_us'] = t.get('act_us', 0) + d['active'] / max(d.get('active_fps', 0), 1e-9) * 1e6
    t['te'] = t.get('te', []) + [d.get('te_us', 0)]

def verdict(active, k1, fps, te_us):
    hz = 1e6 / te_us if te_us else 0
    why = []
    if not 88.5 <= hz <= 90.5: why.append('panel at %.1f Hz (TE %d us), not 90' % (hz, te_us))
    if active < 50: why.append('only %d active intervals' % active)
    k1p = 100.0 * k1 / active if active else 0
    if k1p < 98: why.append('k1 %.1f %% (< 98 %%: refreshes dropped)' % k1p)
    if fps < 87: why.append('active_fps %.1f' % fps)
    v = 'PASS' if not why else ('WARN' if k1p >= 90 and 88.5 <= hz <= 90.5 else 'FAIL')
    return 'FIXED90 %s  panel=%.1fHz active=%d k1=%.1f%% active_fps=%.1f%s' % (v, hz, active, k1p, fps, ('  (' + '; '.join(why) + ')') if why else '')

def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--stat':
        d = parse(sys.argv[2]); k = d.get('refresh_per_present_1/2/3/4/5+', [0])
        print(verdict(int(d.get('active', 0)), int(k[0]), d.get('active_fps', 0), d.get('te_us', 0)))
        return
    text = open(sys.argv[1], errors='replace').read()
    ev = [(m.start(), 'M', m.group(1)) for m in re.finditer(r'NORD2 FB HW: mode \d -> (\d)', text)]
    ev += [(m.start(), 'P', m.group(1)) for m in re.finditer(r'NORD2 FB PACE: (panel_hz=[^\n]*?)(?=NORD2|\n|$)', text)]
    ev += [(m.start(), 'E', m.group(0)) for m in re.finditer(r'NORD2 FB PANEL[^\n]*?(?=NORD2|\n|$)', text)]
    ev.sort()
    seg, tot = 'boot', {}
    def flush():
        if tot.get('active'):
            k = tot['refresh_per_present_1/2/3/4/5+']; te = sorted(x for x in tot['te'] if x)[len(tot['te']) // 2] if tot['te'] else 0
            print('  segment kern.n2fbhw=%s: %s  k=%s dropped=%d' % (seg, verdict(int(tot['active']), int(k[0]), tot['active'] / tot['act_us'] * 1e6, te),
                                                              '/'.join('%d' % x for x in k), tot['dropped']))
    for _, kind, s in ev:
        if kind == 'M':
            flush(); seg, tot = s, {}; print('---- kern.n2fbhw -> %s' % s); continue
        if kind == 'E':
            print('   ', s.strip()[:160]); continue
        d = parse(s); add(tot, d)
        k = d.get('refresh_per_present_1/2/3/4/5+', [0] * 5)
        print('  hz=%5.1f pushes=%4d active=%4d fps=%5.1f k=%s dropped=%d (ready %s) xfer=%dus' % (
            d.get('panel_hz', 0), d.get('pushes', 0), d.get('active', 0), d.get('active_fps', 0), '/'.join('%d' % x for x in k),
            d.get('dropped', 0), re.search(r'ready (\d+)', s).group(1) if 'ready' in s else '?', d.get('xfer_true_avg_us', 0)))
    flush()

if __name__ == '__main__':
    main()
