#!/usr/bin/env python3
"""Display HW composition phone scenario (for the orchestrator; host side, drives autotest/phone.py).
Runs N rounds of interactions (home page swipes, Photos grid scroll, Control Center, app switch) while the kernel console is captured,
then prints the per-mode verdict table of fbhw_verdict.py (tear/race/underflow/fallback per kern.n2fbhw mode).

  fbhw_scenario.py OUT [--rounds N | --duration S | --cycle] [--scenarios home,photos,cc,switch] [--capture phone|FILE] [--shots] [--dry-run]

--capture phone (default): pulls the kernel log with `phone.py logs OUT/kernel.log` after every scenario block (rewinds the 4 MiB buffer
    at 1 MiB, so NO other console reader may run: do not combine with autocycle/txn_g.sh's live nord2-usb-console.py capture).
--capture FILE: the console is already being captured into FILE (txn_g.sh: autocycle/runs/TAG/ui-usb/xnu-live.log); only its size is
    recorded per step, phone.py logs is never called (no rewinds). Give txn_g.sh READS >= the scenario length + boot (e.g. READS=900).
--cycle: for payload-fbhwcycle (modes 0,1,2,3 x 120 s after 90 s): runs rounds until "N2FBHWCTL done" is in the log (+15 s), max 900 s.
--duration S: rounds until S seconds passed.  --rounds N (default 3).  --shots: one 1/4-scale screenshot per scenario block
    (NOTE: phone.py screenshots copy the panel buffer; in modes >= 1 that is not what the OVL shows, see DISPLAY_HW_COMPOSE.md).
Out: OUT/steps.jsonl (each step: kind, scenario, round, line, rc, seconds, log_offset), OUT/kernel.log (phone mode), OUT/verdict.txt,
OUT/verdict.json. Exit = fbhw_verdict.py's (0 no FAIL, 1 FAIL, 2 no data) or 3 if the phone did not answer.
Coordinates (1080x2400 panel): Photos icon 150,850 and Albums tab 680,2220 (NOTES 2026-11-12, ph1); Library tab 135,2220 assumed
(iOS 16 four-tab bar); Control Center = swipe from 985,24 (phone.py launch camera); home = phone.py home.
"""
import json, os, shlex, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
PHONE = HERE.parent.parent / 'autotest' / 'phone.py'

SCEN = {
    'home': ['home', 'sleep 1', 'swipe 900 1300 150 1300 --ms 250', 'sleep 1', 'swipe 150 1300 900 1300 --ms 250', 'sleep 1',
             'swipe 540 700 540 1500 --ms 300', 'sleep 1.5', 'home', 'sleep 1'],                  # pages, then Spotlight pull-down
    'photos': ['home', 'sleep 1', 'tap 150 850', 'sleep 3', 'tap 135 2220', 'sleep 2']
              + ['swipe 540 1900 540 700 --ms 300', 'sleep 0.7'] * 4 + ['swipe 540 700 540 1900 --ms 300', 'sleep 0.7'] * 4
              + ['tap 680 2220', 'sleep 1.5', 'swipe 540 1800 540 900 --ms 300', 'sleep 1', 'home', 'sleep 1'],
    'cc': ['swipe 985 24 760 1000 --ms 300', 'sleep 1.5', 'swipe 540 1600 540 400 --ms 250', 'sleep 1', 'home', 'sleep 1',
           'swipe 985 24 760 1000 --ms 300', 'sleep 1', 'home', 'sleep 1'],
    'switch': ['home', 'sleep 1', 'tap 150 850', 'sleep 3', 'home', 'sleep 1', 'drag 540 2392 540 1500 --ms 500 --after 600', 'sleep 1.5',
               'swipe 300 1200 900 1200 --ms 250', 'sleep 1', 'tap 540 1200', 'sleep 2', 'swipe 100 2380 980 2380 --ms 250', 'sleep 2',
               'swipe 980 2380 100 2380 --ms 250', 'sleep 2', 'home', 'sleep 1'],
}

def main(argv):
    if not argv or argv[0] in ('-h', '--help'): print(__doc__); return 0
    out = Path(argv[0]); opt = lambda k, d=None: argv[argv.index(k) + 1] if k in argv else d
    dry = '--dry-run' in argv; cycle = '--cycle' in argv; shots = '--shots' in argv
    names = opt('--scenarios', 'home,photos,cc,switch').split(',')
    bad = [n for n in names if n not in SCEN]
    if bad: print('unknown scenario', bad, 'known', list(SCEN)); return 2
    rounds = int(opt('--rounds', '0')) or (0 if (cycle or opt('--duration')) else 3)
    duration = 900.0 if cycle else float(opt('--duration', '0'))
    cap = opt('--capture', 'phone'); phone_cap = cap == 'phone'
    log = out / 'kernel.log' if phone_cap else Path(cap)
    out.mkdir(parents=True, exist_ok=True); rec = open(out / 'steps.jsonl', 'a')
    t_start = time.time(); done_at = None

    def logsize():
        try: return log.stat().st_size
        except OSError: return 0
    def phone(line, kind='interaction', scen='', rnd=0):
        w = shlex.split(line); t0 = time.time(); rc, said = 0, ''
        if w[0] == 'sleep':
            if not dry: time.sleep(float(w[1]))
            return 0
        cmd = [sys.executable, str(PHONE)] + w
        if dry: said = 'dry-run'
        else:
            try:
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=120); rc, said = r.returncode, (r.stdout + r.stderr).strip()
            except subprocess.TimeoutExpired: rc, said = 124, 'timeout'
        rec.write(json.dumps(dict(kind=kind, scenario=scen, round=rnd, line=line, rc=rc, start=round(t0, 3), seconds=round(time.time() - t0, 2),
                                  log_offset=logsize(), output=said[-300:])) + '\n'); rec.flush()
        return rc
    def pull():
        if phone_cap: phone('logs %s --rewind-at 1048576' % shlex.quote(str(log)), kind='logs')

    print('fbhw scenario: %s capture=%s rounds=%s duration=%s cycle=%s%s' % (','.join(names), cap, rounds or '-', duration or '-', cycle, ' DRY' if dry else ''))
    if not dry:
        if phone('wait 60', kind='setup') != 0: print('phone does not answer'); return 3
    phone('unlock', kind='setup'); pull()
    rnd = 0; failed = 0
    while True:
        rnd += 1
        for n in names:
            steps = SCEN[n]; f = sum(phone(s, 'interaction', n, rnd) != 0 for s in steps); failed += f
            if shots: phone('screenshot %s --scale 4' % shlex.quote(str(out / ('r%02d-%s.png' % (rnd, n)))), kind='shot', scen=n, rnd=rnd)
            pull()
            print('round %d %-7s %s  t=%4.0fs log=%d B' % (rnd, n, 'ok' if not f else '%d steps failed' % f, time.time() - t_start, logsize()), flush=True)
        el = time.time() - t_start
        if dry and rnd >= max(rounds, 1): break
        if rounds and rnd >= rounds: break
        if cycle and done_at is None and log.exists() and b'N2FBHWCTL done' in log.read_bytes(): done_at = time.time()
        if done_at and time.time() - done_at > 15: break
        if duration and el >= duration: break
    phone('home', kind='setup'); pull()
    print('interactions failed: %d' % failed)
    if dry: print('dry run: %d steps recorded' % sum(1 for _ in open(out / 'steps.jsonl'))); return 0
    sys.path.insert(0, str(HERE)); import fbhw_verdict as V
    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = V.main([str(log), '--steps', str(out / 'steps.jsonl'), '--json', str(out / 'verdict.json')])
    (out / 'verdict.txt').write_text(buf.getvalue()); print(buf.getvalue())
    return rc

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
