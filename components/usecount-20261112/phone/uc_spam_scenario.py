#!/usr/bin/env python3
"""0398 / usecount1 phone scenario: spam app launches (the user's report: icons vanish when launches are spammed, odd icons/textures
in multitasking), then check icons, use counts and the display pool.  Host side, drives autotest/phone.py; for the orchestrator.

  uc_spam_scenario.py OUT [--rounds 6] [--icons "150,850;410,850;670,850;930,850"] [--fast 0.35] [--capture phone|FILE] [--dry-run]
  uc_spam_scenario.py --verdict OUT          (only re-evaluate OUT/kernel.log + OUT/shots)

Per round: 10 quick launch/home cycles over the icons (tap, --fast s, home, --fast s), app switcher open + two swipes + home,
Photos grid scroll, then a home-screen screenshot (1/2 scale) compared with the baseline taken before round 1.
  icons:  each icon cell (180x180 panel px around the tap point) must stay close to the baseline (mean abs difference <= 40 of 255);
          a vanished icon shows the wallpaper -> 'ICON CHANGED round r cell x,y'.  NOTE: phone.py screenshots copy the panel buffer:
          run with kern.n2fbhw 0 (default) or the picture is not what the OVL shows.
  kernel: 'NORD2 SURF USE:' report lines (boot+30 s, then every 60 s while traps come): unmatched-dec / kernel-unmatched / free-nonzero
          must stay 0, no 'LEAK at free', no 'LEAK SUSPECT'; 'gpu holds used=' must reach 0 at least once (GPU idle);
          'NORD2 SURF USE: metal: kernel use count REAL' must appear (driver usecount1 on a 0398 kernel).
  pool:   'NORD2 FB POOL: leak candidates' lines: used MiB of the last report (after the final 'home' + 70 s idle) must be <= 64 MiB
          (backboardd's own framebuffers + the home screen), and 'never shown' surfaces must not hold more than 32 MiB.
Exit 0 PASS, 1 FAIL, 2 no data, 3 the phone did not answer.  Coordinates as fbhw_scenario.py (Photos icon 150,850 [V]; the other
three icons of that row are assumed at x = 410/670/930).
"""
import json, os, re, shlex, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
PHONE = HERE.parents[1] / 'autotest' / 'phone.py'
CELL = 90            # half size of an icon cell, panel px

def shot_cells(png, icons, scale=2):
    from PIL import Image
    im = Image.open(png).convert('L'); out = []
    for x, y in icons:
        box = ((x - CELL) // scale, (y - CELL) // scale, (x + CELL) // scale, (y + CELL) // scale)
        out.append(list(im.crop(box).tobytes()))
    return out

def cell_diff(a, b):
    return sum(abs(p - q) for p, q in zip(a, b)) / max(1, len(a))

def verdict(out, icons):
    out = Path(out); fails, notes = [], []
    log = out / 'kernel.log'
    text = log.read_text(errors='replace') if log.exists() else ''
    if not text: return 2, ['no kernel log']
    use = [l for l in text.splitlines() if 'NORD2 SURF USE:' in l]
    pool = [l for l in text.splitlines() if 'NORD2 FB POOL: leak candidates' in l]
    if not use: fails.append('no NORD2 SURF USE lines (kernel without 0398?)')
    for l in use:
        m = re.search(r'unmatched-dec (\d+) dup-inc (\d+) kernel-unmatched (\d+)/(\d+)', l)
        if m and (int(m.group(1)) or int(m.group(3)) or int(m.group(4))): fails.append('imbalance: ' + l.split('NORD2 SURF USE:')[1][:160])
        m = re.search(r'free-nonzero (\d+)', l)
        if m and int(m.group(1)): fails.append('free-nonzero: ' + m.group(0))
        if 'LEAK at free' in l or 'LEAK SUSPECT' in l: fails.append(l.split('NORD2 SURF USE:')[1][:200])
    gpu = [int(m.group(1)) for m in (re.search(r'gpu holds used=(\d+)', l) for l in use) if m]
    if gpu and min(gpu) != 0: fails.append('GPU holds never back to 0 (min %d)' % min(gpu))
    if not any('kernel use count REAL' in l for l in use): notes.append('driver probe line missing (usecount1 not installed, or no texture over a surface yet)')
    last_live = [l for l in use if ' live ' in l and 'incs' in l][-1:] or ['-']
    notes.append('last counters: ' + last_live[0].split('NORD2 SURF USE:')[-1][:220])
    if pool:
        m = re.search(r'used (\d+) of \d+ MiB.*never shown (\d+) \((\d+) MiB\)', pool[-1])
        if m:
            used, never_mib = int(m.group(1)), int(m.group(3))
            notes.append('pool: last used %d MiB, never-shown %d MiB (%d reports)' % (used, never_mib, len(pool)))
            if used > 64: fails.append('pool used %d MiB > 64 after idle' % used)
            if never_mib > 32: fails.append('never-shown pool surfaces hold %d MiB' % never_mib)
    else:
        notes.append('no pool report (kern.n2fbhw_pool 0?)')
    shots = sorted((out / 'shots').glob('round*.png')) if (out / 'shots').exists() else []
    base = out / 'shots' / 'baseline.png'
    if base.exists() and shots:
        b = shot_cells(base, icons)
        for s in shots:
            for (x, y), c0, c1 in zip(icons, b, shot_cells(s, icons)):
                d = cell_diff(c0, c1)
                if d > 40: fails.append('ICON CHANGED %s cell %d,%d (mean diff %.0f)' % (s.stem, x, y, d))
        notes.append('icons: %d screenshots x %d cells compared' % (len(shots), len(icons)))
    else:
        notes.append('icons: no screenshots')
    return (1 if fails else 0), fails + notes

def main(argv):
    if not argv or argv[0] in ('-h', '--help'): print(__doc__); return 0
    opt = lambda k, d=None: argv[argv.index(k) + 1] if k in argv else d
    icons = [tuple(int(v) for v in p.split(',')) for p in opt('--icons', '150,850;410,850;670,850;930,850').split(';')]
    if argv[0] == '--verdict':
        rc, lines = verdict(argv[1], icons); print('\n'.join(lines)); print('VERDICT', ['PASS', 'FAIL', 'NO DATA'][rc]); return rc
    out = Path(argv[0]); out.mkdir(parents=True, exist_ok=True); (out / 'shots').mkdir(exist_ok=True)
    rounds = int(opt('--rounds', '6')); fast = float(opt('--fast', '0.35')); dry = '--dry-run' in argv
    cap = opt('--capture', 'phone'); phone_cap = cap == 'phone'
    if not phone_cap and Path(cap).resolve() != (out / 'kernel.log').resolve():
        try: os.symlink(Path(cap).resolve(), out / 'kernel.log')
        except FileExistsError: pass
    rec = open(out / 'steps.jsonl', 'a')
    def phone(line, kind='step', rnd=0):
        w = shlex.split(line)
        if w[0] == 'sleep':
            if not dry: time.sleep(float(w[1]))
            return 0
        t0 = time.time(); rc, said = 0, 'dry-run'
        if not dry:
            try:
                r = subprocess.run([sys.executable, str(PHONE)] + w, capture_output=True, text=True, timeout=120); rc, said = r.returncode, (r.stdout + r.stderr).strip()
            except subprocess.TimeoutExpired: rc, said = 124, 'timeout'
        rec.write(json.dumps(dict(kind=kind, round=rnd, line=line, rc=rc, seconds=round(time.time() - t0, 2), output=said[-200:])) + '\n'); rec.flush()
        return rc
    def pull():
        if phone_cap: phone('logs %s --rewind-at 1048576' % shlex.quote(str(out / 'kernel.log')), kind='logs')
    print('usecount spam scenario: rounds=%d fast=%.2fs icons=%s capture=%s%s' % (rounds, fast, icons, cap, ' DRY' if dry else ''))
    if not dry and phone('wait 60', kind='setup') != 0: print('phone does not answer'); return 3
    phone('unlock', kind='setup'); phone('home', kind='setup'); phone('sleep 2')
    phone('screenshot %s --scale 2' % shlex.quote(str(out / 'shots' / 'baseline.png')), kind='shot'); pull()
    for rnd in range(1, rounds + 1):
        for k in range(10):                                     # spam: launch, leave at once, next icon
            x, y = icons[k % len(icons)]
            phone('tap %d %d' % (x, y), 'spam', rnd); phone('sleep %.2f' % fast); phone('home', 'spam', rnd); phone('sleep %.2f' % fast)
        for s in ['sleep 1', 'drag 540 2392 540 1500 --ms 500 --after 600', 'sleep 1.2', 'swipe 300 1200 900 1200 --ms 250', 'sleep 0.8',
                  'swipe 900 1200 300 1200 --ms 250', 'sleep 0.8', 'home', 'sleep 1',                       # app switcher
                  'tap 150 850', 'sleep 2.5', 'swipe 540 1900 540 700 --ms 300', 'sleep 0.6', 'swipe 540 700 540 1900 --ms 300',
                  'sleep 0.6', 'home', 'sleep 2']:                                                           # Photos grid
            phone(s, 'multi', rnd)
        phone('screenshot %s --scale 2' % shlex.quote(str(out / 'shots' / ('round%02d.png' % rnd))), kind='shot', rnd=rnd)
        pull()
    phone('home', kind='idle'); phone('sleep 70'); pull()       # idle: the next 60 s report shows the steady state
    if dry: print('dry run done'); return 0
    rc, lines = verdict(out, icons); (out / 'verdict.txt').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines)); print('VERDICT', ['PASS', 'FAIL', 'NO DATA'][rc]); return rc

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
