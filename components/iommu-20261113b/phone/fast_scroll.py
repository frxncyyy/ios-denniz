#!/usr/bin/env python3
"""SCEN=fast for run_fbiommu_step.sh: continuous Photos-grid scroll, no idle, no home gesture.
  fast_scroll.py OUT --capture XNU_LIVE_LOG --seconds S [--period 0.5]
wait 60 (phone answers) + unlock once, tap Photos (150,850), tap Library (135,2220), then alternate swipe up / swipe down
(540,1800 <-> 540,900, 250 ms) every --period seconds until S seconds pass or the phone stops answering (3 failed steps in a row).
OUT/steps.jsonl: one record per phone.py call (kind, line, rc, start = host epoch s, seconds, log_offset = capture file size after
the call), the same format as fbhw_scenario.py, so fbiommu_step_verdict.py can time log lines against the host clock.
Reads only the capture file's size (no phone.py logs: one reader)."""
import sys, time, json, shlex, subprocess
from pathlib import Path
PHONE = Path(__file__).resolve().parents[2] / 'autotest' / 'phone.py'

def main(a):
    if not a or a[0] in ('-h', '--help'): print(__doc__); return 0
    out = Path(a[0]); opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
    log = Path(opt('--capture')); secs = float(opt('--seconds', '150')); period = float(opt('--period', '0.5'))
    out.mkdir(parents=True, exist_ok=True); rec = open(out / 'steps.jsonl', 'a'); t0 = time.time()
    def size():
        try: return log.stat().st_size
        except OSError: return 0
    def phone(line, kind='interaction'):
        s = time.time()
        try:
            r = subprocess.run([sys.executable, str(PHONE)] + shlex.split(line), capture_output=True, text=True, timeout=30)
            rc, said = r.returncode, (r.stdout + r.stderr).strip()
        except subprocess.TimeoutExpired: rc, said = 124, 'timeout'
        rec.write(json.dumps(dict(kind=kind, scenario='fast', round=0, line=line, rc=rc, start=round(s, 3), seconds=round(time.time() - s, 2),
                                  log_offset=size(), output=said[-200:])) + '\n'); rec.flush()
        return rc
    if phone('wait 60', 'setup') != 0: print('phone does not answer'); return 3
    phone('unlock', 'setup'); time.sleep(1.5)
    phone('tap 150 850', 'setup'); time.sleep(3); phone('tap 135 2220', 'setup'); time.sleep(2)
    n = bad = 0; up = True
    while time.time() - t0 < secs:
        s = time.time()
        rc = phone('swipe 540 1800 540 900 --ms 250' if up else 'swipe 540 900 540 1800 --ms 250'); up = not up; n += 1
        bad = bad + 1 if rc != 0 else 0
        if bad >= 3: print('fast scroll: phone stopped answering after %d swipes, t=%.0f s' % (n, time.time() - t0)); return 1
        time.sleep(max(0.0, period - (time.time() - s)))
    print('fast scroll: %d swipes in %.0f s, phone answered throughout' % (n, time.time() - t0)); return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
