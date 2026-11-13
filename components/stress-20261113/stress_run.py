#!/usr/bin/env python3
"""Overnight UI stress: one capture WINDOW (<= 540 s) of heavy, fast, seeded-random interactions on the Nord 2 (iOS 16.7 port).
Host side; drives the phone over the "Nord2 XNU console" USB device through autotest/phone.py. For the orchestrator.

  stress_run.py OUT --log CONSOLE_FILE [--minutes 8.5] [--seed 1] [--window 1] [--plan photos:110,spam:80,...]
                [--coords coords.json] [--backend inproc|cli] [--no-shots] [--lost-wait 60]
                [--dry-run [--dry-realtime] [--dry-lose-after N]]     (dry run: no phone, simulated clock; N = simulated loss)
  stress_run.py OUT --calibrate [--coords coords.json]   home-screen screenshot + icon-cell table (check the icon positions first)
  stress_run.py OUT --latency                             per-gesture overhead: 20 status reads + 10 home swipes, then exits

--log: the console file the CALLER is capturing into (txn_g.sh's runs/TAG/ui-usb/xnu-live.log for window 1, stress_capture.py's
       wNN/console.log for later windows). Only its size is read (log_offset of every step). phone.py logs is NEVER called here
       (it rewinds the kernel buffer under the caller's reader).
Phases (--plan NAME:SECONDS,...; default fills --minutes, see DEFAULT_PLAN): spam, photos, switcher, ccnc, settings, apps, rotate,
       subway, idle. Every phase is a seeded-random sequence (random.Random(seed*1000+window)), so a window can be replayed.
Out:  OUT/steps.jsonl   {kind: step|phase|shot|event, step, t, phase, action, rc, dt, log_offset, err, ...}
      OUT/shots/*.png   checkpoints (1/2 scale): Photos Library at a fixed position (two shots 1 s apart) + home screen
      OUT/summary.json  steps, failures, end reason;  OUT/PHONE_LOST.txt when the console went away (+ the last log lines)
Exit: 0 window done, 3 phone lost (console gone > --lost-wait s), 4 phone rebooted (status 'boot' changed), 2 usage.
Never blocks: every phone operation runs in a worker thread with a hard deadline (gesture time + 6 s, screenshots 25 s); a missed
deadline or a USB error is followed by a status probe on a fresh handle; the window ends after --lost-wait s without an answer.
SAFETY: taps only go to whitelisted targets (home icons, Photos tab bar, the game's centre, a switcher card); no taps inside the
Photos grid/viewer or Settings rows. Long presses are always followed by a home swipe (never a tap) so no context-menu item
(Delete/Remove App) can be hit. Residual risk: see README.md (Photos viewer trash button).
"""
import json, os, random, shlex, subprocess, sys, threading, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PHONE_PY = Path(os.environ.get('STRESS_PHONE_PY') or HERE.parent / 'autotest' / 'phone.py')   # override: tests (fake phone)
MAX_WINDOW_S = 540

DEFAULT_COORDS = {
    # panel pixels 1080x2400.  verified: seen on a 1080x2400 home screenshot (user / NOTES); others are assumptions -> --calibrate
    'icons': {'Photos': [150, 855], 'App Store': [410, 855], 'Settings': [675, 855], 'Subway Surf': [975, 855],
              'Safari': [410, 2235], 'Messages': [675, 2235]},
    'verified': {'Photos': True, 'Settings': True, 'Subway Surf': True, 'App Store': False, 'Safari': False, 'Messages': False},
    'photos_tabs': {'Library': [135, 2220], 'For You': [405, 2220], 'Albums': [680, 2220], 'Search': [945, 2220]},
    'home': [540, 2392, 540, 1300],               # phone.py home
    'switcher': [540, 2392, 540, 1500],           # drag, hold 600 ms (fbhw_scenario.py)
    'cc': [985, 24, 760, 1000],                   # Control Center (phone.py launch camera)
    'nc': [400, 10, 400, 1500],                   # Notification Center (top edge, left of the notch)
    'appswitch_edge_y': 2380,                     # bottom-edge horizontal swipe = previous/next app
    'grid_center': [540, 1250],
    'game_center': [540, 1300],
}

DEFAULT_PLAN = [('photos', 110), ('spam', 80), ('switcher', 45), ('ccnc', 40), ('settings', 35), ('apps', 40), ('rotate', 35),
                ('subway', 130), ('idle', 25)]                     # = 540 s; scaled to --minutes

UI_APPS = ['Photos', 'Settings', 'App Store', 'Safari', 'Messages', 'Subway Surf']


class Lost(Exception):
    pass


class Rebooted(Exception):
    pass


# ---------------------------------------------------------------- device backends
class DryDev:
    """No phone. Simulates gesture time (only slept with --dry-realtime)."""
    def __init__(self, realtime=False, lose_after=0): self.rt = realtime; self.boot = 1; self.n = 0; self.lose = lose_after
    def _t(self, s):
        if self.rt: time.sleep(s)
    def _gone(self):
        if self.lose and self.n >= self.lose: raise Lost('dry run: simulated phone loss after %d gestures' % self.n)
    def gesture(self, paths, ms, before=0, after=0): self._gone(); self.n += 1; self._t((ms + before + after) / 1000 + 0.02)
    def key(self, name, ms=80): self._gone(); self._t(ms / 1000)
    def status(self): self._gone(); return dict(boot=self.boot, version=2, busy=0)
    def screenshot(self, path, scale=2):
        self._gone()
        try:
            from PIL import Image
            Image.new('RGB', (1080 // scale, 2400 // scale), (30, 30, 30)).save(path)
        except Exception:
            Path(path).write_bytes(b'')
        return dict(presents_during_copy=0, panel_off=0)
    def close(self): pass


class InprocDev:
    """One USB handle kept open (no interpreter start per gesture). Operations run in a worker thread with a hard deadline."""
    def __init__(self, wait=10.0):
        import importlib.util
        spec = importlib.util.spec_from_file_location('nord2_phone', PHONE_PY)
        self.mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(self.mod)
        self.wait = wait; self.p = None; self._open(wait)

    def _open(self, wait):
        try:
            self.p = self.mod.Phone(wait=wait)
        except self.mod.Gone as e:
            raise Lost(str(e))

    def _run(self, fn, deadline):
        box = {}
        def body():
            try: box['r'] = fn()
            except BaseException as e: box['e'] = e
        th = threading.Thread(target=body, daemon=True); th.start(); th.join(deadline)
        if th.is_alive():
            self.p = None                                # abandon the handle the stuck thread owns; reopen at the next call
            raise TimeoutError('phone operation exceeded %.1f s' % deadline)
        if 'e' in box:
            e = box['e']
            if isinstance(e, self.mod.Gone): self.p = None; raise Lost(str(e))
            raise e
        return box.get('r')

    def _need(self):
        if self.p is None: self._open(self.wait)

    def gesture(self, paths, ms, before=0, after=0):
        self._need(); p = self.p
        return self._run(lambda: p.gesture(paths, ms, before, after), (ms + before + after) / 1000 + 6)
    def key(self, name, ms=80):
        self._need(); p = self.p
        return self._run(lambda: p.key(name, ms), ms / 1000 + 6)
    def status(self):
        self._need(); p = self.p
        return self._run(p.status, 6)
    def screenshot(self, path, scale=2):
        self._need(); p = self.p; mod = self.mod
        def f():
            w, h, rgb, s = p.snapshot(scale); mod.png(path, w, h, rgb)
            return dict(presents_during_copy=s['presents_during_snap'], panel_off=s['panel_off'], bytes_over_usb=s.get('transferred'))
        return self._run(f, 25)
    def close(self):
        try:
            if self.p: self.p.close()
        except Exception: pass


class CliDev:
    """phone.py per call (slower: ~0.10-0.13 s median overhead per call measured in run fbhw2), subprocess timeout each."""
    def _call(self, args, timeout):
        try:
            r = subprocess.run([sys.executable, str(PHONE_PY)] + [str(a) for a in args], capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            raise TimeoutError('phone.py %s timed out' % args[0])
        out = (r.stdout + r.stderr).strip()
        if r.returncode:
            if 'not on USB' in out or 'left USB' in out: raise Lost(out[-200:])
            raise OSError(out[-200:])
        return out
    def gesture(self, paths, ms, before=0, after=0):
        flat = [v for pth in paths for v in pth]
        self._call(['gesture', len(paths)] + flat + ['--ms', ms, '--before', before, '--after', after], (ms + before + after) / 1000 + 8)
    def key(self, name, ms=80): self._call(['key', name, '--ms', ms], 8)
    def status(self): return json.loads(self._call(['status'], 8))
    def screenshot(self, path, scale=2):
        o = json.loads(self._call(['screenshot', path, '--scale', scale], 30).splitlines()[-1])
        return dict(presents_during_copy=o.get('presents_during_copy'), panel_off=o.get('panel_off'))
    def close(self): pass


# ---------------------------------------------------------------- the runner
class Runner:
    def __init__(self, out, dev, log, rng, coords, window, deadline, lost_wait=60.0, shots=True, shots_root=None, dry=False):
        self.out, self.dev, self.log, self.rng, self.c = out, dev, log, rng, coords
        self.window, self.lost_wait, self.shots, self.dry = window, lost_wait, shots, dry
        self.shots_dir = out / 'shots'; self.shots_dir.mkdir(parents=True, exist_ok=True)
        self.rec = open(out / 'steps.jsonl', 'a'); self.step = 0; self.fail = 0; self.phase = 'setup'
        self.boot = None; self.last_status = 0.0; self.cp = 0
        self.vt = time.monotonic() if dry else None          # dry run: simulated clock (sleeps and gesture times advance it)
        self.wall0 = time.time(); self.vt0 = self.vt
        self.deadline = self.now() + deadline                  # deadline = window length in seconds

    def wall(self):
        """wall clock (simulated in a dry run, so the steps carry plausible times)"""
        return time.time() if self.vt is None else self.wall0 + (self.vt - self.vt0)

    def now(self):
        return self.vt if self.vt is not None else time.monotonic()

    # -- records
    def logsize(self):
        try: return self.log.stat().st_size if self.log else None
        except OSError: return None
    def write(self, **kw):
        kw.setdefault('t', round(self.wall(), 3)); kw.setdefault('phase', self.phase); kw.setdefault('window', self.window)
        kw.setdefault('log_offset', self.logsize())
        self.rec.write(json.dumps(kw) + '\n'); self.rec.flush()
    def left(self): return self.deadline - self.now()

    # -- health
    def probe(self, why):
        """A call failed: is the phone still there? Raises Lost / Rebooted."""
        t_end = self.now() + self.lost_wait
        while True:
            try:
                s = self.dev.status(); break
            except (Lost, TimeoutError, OSError) as e:
                if self.now() > t_end:
                    raise Lost('%s; no status answer for %.0f s (%s)' % (why, self.lost_wait, e))
                if self.vt is not None: self.vt += 1.0
                else: time.sleep(1.0)
        self.check_boot(s)
    def check_boot(self, s):
        b = s.get('boot')
        if self.boot is None: self.boot = b
        elif b != self.boot: raise Rebooted('status boot %s -> %s' % (self.boot, b))
        self.last_status = self.now()

    def call(self, action, fn, *a, **kw):
        self.step += 1; t0 = self.now(); rc, err = 0, None
        try:
            fn(*a, **kw)
        except (Lost, TimeoutError, OSError) as e:
            rc, err = 1, '%s: %s' % (type(e).__name__, str(e)[:200]); self.fail += 1
        if self.vt is not None:
            g = [x for x in a if isinstance(x, int)]
            self.vt += sum(g[:3]) / 1000 + 0.03 if g else 0.03
        self.write(kind='step', step=self.step, action=action, rc=rc, dt=round(self.now() - t0, 3), err=err)
        if rc: self.probe('step %d %s failed: %s' % (self.step, action, err))
        elif self.now() - self.last_status > 30:
            try: self.check_boot(self.dev.status())
            except (Lost, TimeoutError, OSError) as e: self.probe('status failed: %s' % e)
        return rc

    def sleep(self, s):
        s = max(0.0, min(s, self.left()))
        if self.vt is not None: self.vt += s
        if (not self.dry or getattr(self.dev, 'rt', False)) and s > 0: time.sleep(s)

    # -- gesture vocabulary (short, fast; before/after hold times kept at 0..30 ms)
    def g(self, name, path, ms, before=15, after=0): return self.call(name, self.dev.gesture, [tuple(path)], int(ms), int(before), int(after))
    def tap(self, name, xy, ms=50): return self.g(name, (xy[0], xy[1], xy[0], xy[1]), 0, ms, 0)
    def home(self, fast=False): return self.g('home', self.c['home'], self.rng.randint(110, 170) if fast else 220, 10, 0)
    def launch(self, app): return self.tap('launch ' + app, self.c['icons'][app])
    def fling(self, up=True, ms=None, x=None, y0=1950, y1=650):
        x = x if x is not None else self.rng.randint(380, 700); ms = ms or self.rng.randint(80, 250)
        return self.g('fling %s %dms' % ('up' if up else 'down', ms), (x, y0, x + self.rng.randint(-30, 30), y1) if up else (x, y1, x + self.rng.randint(-30, 30), y0), ms, self.rng.randint(0, 20), 0)
    def pinch(self, d0, d1, ms=None):
        cx, cy = self.c['grid_center']; ms = ms or self.rng.randint(250, 500)
        a, b = d0 / 2, d1 / 2
        return self.call('pinch %d->%d' % (d0, d1), self.dev.gesture, [(cx, cy - a, cx, cy - b), (cx, cy + a, cx, cy + b)], ms, 60, 80)
    def switcher(self): return self.g('switcher', self.c['switcher'], self.rng.randint(300, 500), 15, self.rng.randint(350, 600))
    def safe_back(self):
        """Close a photo viewer / sheet without tapping: swipe down from mid screen (in the grid it just scrolls up)."""
        return self.g('swipe-down dismiss', (540, 900, 540, 1900), self.rng.randint(150, 220), 10, 0)

    # -- checkpoints
    def screenshot(self, name):
        if not self.shots: return
        self.cp += 1; path = self.shots_dir / ('w%02d-cp%03d-%s.png' % (self.window, self.cp, name))
        self.step += 1; t0 = self.now(); rc, err, meta = 0, None, {}
        try: meta = self.dev.screenshot(str(path), 2) or {}
        except (Lost, TimeoutError, OSError) as e: rc, err = 1, '%s: %s' % (type(e).__name__, str(e)[:200]); self.fail += 1
        self.write(kind='shot', step=self.step, action='screenshot ' + name, file=str(path) if path.exists() else None, name=name,
                   checkpoint=self.cp, rc=rc, dt=round(self.now() - t0, 3), err=err, **meta)
        if rc: self.probe('screenshot failed: %s' % err)

    def photos_checkpoint(self, tag):
        """Photos Library at a fixed position: dismiss, Library tab twice (scrolls to the newest), settle 2 s, two shots 1 s apart."""
        self.safe_back(); self.sleep(0.4)
        self.tap('tab Library', self.c['photos_tabs']['Library']); self.sleep(0.6)
        self.tap('tab Library (to newest)', self.c['photos_tabs']['Library']); self.sleep(2.0)
        self.screenshot('photos-%s-a' % tag); self.sleep(1.0); self.screenshot('photos-%s-b' % tag)

    # ---------------------------------------------------------------- phases
    def ph_spam(self, until):
        apps = [a for a in UI_APPS if a in self.c['icons']]
        while self.now() < until:
            r = self.rng.random()
            self.launch(self.rng.choice(apps)); self.sleep(self.rng.uniform(0.12, 1.2))
            self.home(fast=True); self.sleep(self.rng.uniform(0.08, 0.6))
            if r < 0.15:                                          # kill the newest card in the switcher
                self.switcher(); self.sleep(self.rng.uniform(0.3, 0.8))
                self.g('switcher kill card', (540, 1300, 540 + self.rng.randint(-40, 40), 150), self.rng.randint(100, 180), 10, 0)
                self.sleep(0.3); self.home(fast=True); self.sleep(0.2)

    def ph_photos(self, until):
        self.home(); self.sleep(0.4); self.launch('Photos'); self.sleep(2.5)
        self.photos_checkpoint('start')
        mid_done = False; t_mid = self.now() + (until - self.now()) / 2
        tabs = list(self.c['photos_tabs'].items())
        while self.now() < until:
            r = self.rng.random()
            if r < 0.55:                                          # burst of flings, both directions, 80-250 ms
                up = self.rng.random() < 0.6
                for _ in range(self.rng.randint(3, 9)):
                    self.fling(up=up if self.rng.random() < 0.8 else not up); self.sleep(self.rng.uniform(0.0, 0.25))
            elif r < 0.75:                                        # tab hopping (after a no-tap dismiss)
                self.safe_back()
                for _ in range(self.rng.randint(2, 6)):
                    n, xy = self.rng.choice(tabs); self.tap('tab ' + n, xy); self.sleep(self.rng.uniform(0.1, 0.7))
                self.tap('tab Library', self.c['photos_tabs']['Library']); self.sleep(0.3)
            elif r < 0.87:                                        # pinch: grid out then back in
                self.pinch(600, 250); self.sleep(self.rng.uniform(0.2, 0.6)); self.pinch(250, 600); self.sleep(0.3)
            elif r < 0.93:                                        # long press, then a home swipe (never a tap) and back in
                cx, cy = self.c['grid_center']
                self.g('long press grid', (cx, cy, cx, cy), 0, self.rng.randint(600, 900), 0); self.sleep(0.5)
                self.home(); self.sleep(0.4); self.launch('Photos'); self.sleep(1.0); self.safe_back()
            else:                                                 # leave and come back fast
                self.home(fast=True); self.sleep(self.rng.uniform(0.1, 0.5)); self.launch('Photos'); self.sleep(self.rng.uniform(0.3, 1.0))
            if not mid_done and self.now() > t_mid and self.left() > 20:
                self.photos_checkpoint('mid'); mid_done = True
        if self.left() > 8: self.photos_checkpoint('end')
        self.home()

    def ph_switcher(self, until):
        while self.now() < until:
            self.switcher(); self.sleep(self.rng.uniform(0.2, 0.7))
            for _ in range(self.rng.randint(2, 7)):
                d = self.rng.choice([-1, 1]); y = self.rng.randint(1000, 1400)
                self.g('switcher pan', (540 - 330 * d, y, 540 + 330 * d, y), self.rng.randint(90, 220), 10, 0); self.sleep(self.rng.uniform(0.0, 0.3))
            r = self.rng.random()
            if r < 0.5: self.tap('switcher open card', (540, 1200)); self.sleep(self.rng.uniform(0.3, 1.2))
            elif r < 0.65: self.g('switcher kill card', (540, 1300, 540, 150), self.rng.randint(100, 180), 10, 0); self.sleep(0.3)
            self.home(fast=True); self.sleep(self.rng.uniform(0.1, 0.4))

    def ph_ccnc(self, until):
        while self.now() < until:
            if self.rng.random() < 0.6:
                self.g('control center', self.c['cc'], self.rng.randint(120, 300), 10, 0); self.sleep(self.rng.uniform(0.2, 0.9))
                self.g('cc dismiss', (540, 1900, 540, 300), self.rng.randint(100, 200), 10, 0) if self.rng.random() < 0.5 else self.home(fast=True)
            else:
                self.g('notification center', self.c['nc'], self.rng.randint(120, 300), 10, 0); self.sleep(self.rng.uniform(0.2, 0.9))
                self.home(fast=True)
            self.sleep(self.rng.uniform(0.05, 0.4))
            if self.rng.random() < 0.2:                           # over an app too
                self.launch(self.rng.choice(['Photos', 'Settings'])); self.sleep(0.6)

    def ph_settings(self, until):
        self.home(); self.sleep(0.3); self.launch('Settings'); self.sleep(1.5)
        while self.now() < until:
            r = self.rng.random()
            if r < 0.8:
                up = self.rng.random() < 0.55
                for _ in range(self.rng.randint(2, 6)): self.fling(up=up, y0=1900, y1=700); self.sleep(self.rng.uniform(0.0, 0.3))
            elif r < 0.9:
                self.g('edge back', (6, 1200, 760, 1200), self.rng.randint(150, 250), 10, 0); self.sleep(0.3)
            else:
                self.home(fast=True); self.sleep(0.2); self.launch('Settings'); self.sleep(0.8)
        self.home()

    def ph_apps(self, until):
        """Launch a random app and scroll it (Safari / Messages / App Store / Settings): no taps inside the apps."""
        apps = [a for a in ('Safari', 'Messages', 'App Store', 'Settings', 'Photos') if a in self.c['icons']]
        while self.now() < until:
            self.home(fast=True); self.sleep(0.15); self.launch(self.rng.choice(apps)); self.sleep(self.rng.uniform(0.6, 1.5))
            for _ in range(self.rng.randint(2, 6)):
                self.fling(up=self.rng.random() < 0.6, y0=1850, y1=750); self.sleep(self.rng.uniform(0.0, 0.3))

    def ph_rotate(self, until):
        """Rotate through the recent apps with bottom-edge horizontal swipes (no taps)."""
        y = self.c['appswitch_edge_y']
        self.launch('Photos'); self.sleep(0.8)
        while self.now() < until:
            d = 1 if self.rng.random() < 0.7 else -1
            self.g('edge app switch', (540 - 440 * d, y, 540 + 440 * d, y), self.rng.randint(120, 250), 10, 0)
            self.sleep(self.rng.uniform(0.1, 0.8))
            if self.rng.random() < 0.3: self.fling(up=self.rng.random() < 0.5, y0=1800, y1=800)
        self.home()

    def ph_subway(self, until):
        """Launch, wait for the game, tap to play, then play (left/right/up/down swipes) until the phase ends."""
        cx, cy = self.c['game_center']
        self.home(); self.sleep(0.3); self.launch('Subway Surf')
        self.write(kind='event', event='subway launched')
        self.sleep(min(float(self.c.get('game_load_s', 25)), max(0, until - self.now() - 10)))
        for _ in range(3): self.tap('game tap to play', (cx, cy)); self.sleep(0.7)
        self.write(kind='event', event='subway play')
        last_tap = self.now()
        while self.now() < until:
            dx, dy = self.rng.choice([(-1, 0), (1, 0), (0, -1), (0, 1), (-1, 0), (1, 0), (0, -1)])
            self.g('game swipe %s' % {(-1, 0): 'left', (1, 0): 'right', (0, -1): 'up', (0, 1): 'down'}[(dx, dy)],
                   (cx, cy, cx + dx * 380, cy + dy * 450), self.rng.randint(80, 170), 5, 0)
            self.sleep(self.rng.uniform(0.25, 0.7))
            if self.now() - last_tap > 15:                  # after a crash into a train: tap to restart
                self.tap('game tap', (cx, cy)); last_tap = self.now()
        self.home()

    def ph_idle(self, until):
        self.home(); self.sleep(0.5)
        self.screenshot('home')
        self.sleep(until - self.now())

    def run(self, plan):
        try:
            self.check_boot(self.dev.status())
            self.write(kind='event', event='start', boot=self.boot)
            self.phase = 'setup'
            self.g('unlock', (540, 2392, 540, 1000), 250, 20, 0); self.sleep(0.8); self.home(); self.sleep(0.5)
            total = sum(s for _, s in plan); t0 = self.now()
            for name, secs in plan:
                if self.left() < 3: break
                until = min(self.now() + secs, self.deadline - 1.5)
                self.phase = name; self.write(kind='phase', event='begin', planned_s=secs)
                getattr(self, 'ph_' + name)(until)
                self.write(kind='phase', event='end')
            self.phase = 'end'; self.home()
            return 0, 'window done'
        except Lost as e:
            return 3, 'PHONE LOST at step %d: %s' % (self.step, e)
        except Rebooted as e:
            return 4, 'PHONE REBOOTED at step %d: %s' % (self.step, e)


def scale_plan(plan, seconds):
    tot = sum(s for _, s in plan)
    return [(n, max(10, round(s * seconds / tot))) for n, s in plan] if tot else plan


def parse_plan(s):
    out = []
    for item in s.split(','):
        n, _, v = item.partition(':')
        if not hasattr(Runner, 'ph_' + n): raise SystemExit('unknown phase %r (known: %s)' % (n, ', '.join(k[3:] for k in dir(Runner) if k.startswith('ph_'))))
        out.append((n, float(v or 60)))
    return out


def tail_lines(path, n=40):
    try:
        with open(path, 'rb') as f:
            f.seek(0, 2); size = f.tell(); f.seek(max(0, size - 16384))
            return f.read().decode('utf-8', 'replace').splitlines()[-n:]
    except OSError:
        return ['(no log file)']


def load_coords(path):
    c = json.loads(json.dumps(DEFAULT_COORDS))
    if path:
        user = json.loads(Path(path).read_text())
        for k, v in user.items():
            if isinstance(v, dict) and isinstance(c.get(k), dict): c[k].update(v)
            else: c[k] = v
    return c


def calibrate(out, dev, coords):
    from PIL import Image, ImageStat, ImageDraw
    out.mkdir(parents=True, exist_ok=True)
    dev.gesture([(540, 2392, 540, 1000)], 250, 20, 0); time.sleep(1.0); dev.gesture([tuple(coords['home'])], 220, 20, 0); time.sleep(1.5)
    p = out / 'calib-home.png'; dev.screenshot(str(p), 2)
    im = Image.open(p).convert('RGB'); sheet = Image.new('RGB', (100 * len(coords['icons']), 120), (255, 255, 255)); d = ImageDraw.Draw(sheet)
    print('%-12s %-11s %-9s %-7s %s' % ('icon', 'panel x,y', 'verified', 'std', 'looks like an icon (std > 25)'))
    for i, (n, (x, y)) in enumerate(coords['icons'].items()):
        box = ((x - 80) // 2, (y - 80) // 2, (x + 80) // 2, (y + 80) // 2); cell = im.crop(box)
        std = sum(ImageStat.Stat(cell).stddev) / 3
        sheet.paste(cell.resize((80, 80)), (100 * i + 10, 5)); d.text((100 * i + 5, 95), n[:14], fill=(0, 0, 0))
        print('%-12s %-11s %-9s %-7.1f %s' % (n, '%d,%d' % (x, y), coords['verified'].get(n, False), std, 'yes' if std > 25 else 'NO: check'))
    sheet.save(out / 'calib-cells.png'); print('pictures:', p, out / 'calib-cells.png'); print('fix positions with --coords FILE (JSON, same keys as DEFAULT_COORDS)')


def latency(dev, coords):
    t = []
    for _ in range(20):
        a = time.monotonic(); dev.status(); t.append(time.monotonic() - a)
    g = []
    for _ in range(10):
        a = time.monotonic(); dev.gesture([tuple(coords['home'])], 120, 0, 0); g.append(time.monotonic() - a - 0.12); time.sleep(0.2)
    t.sort(); g.sort()
    print(json.dumps(dict(status_s=dict(min=round(t[0], 4), med=round(t[10], 4), max=round(t[-1], 4)),
                          gesture_overhead_s=dict(min=round(g[0], 4), med=round(g[5], 4), max=round(g[-1], 4)))))


def main(argv):
    if not argv or argv[0] in ('-h', '--help'): print(__doc__); return 0
    out = Path(argv[0]); opt = lambda k, d=None: argv[argv.index(k) + 1] if k in argv else d
    dry = '--dry-run' in argv; coords = load_coords(opt('--coords'))
    backend = 'dry' if dry else opt('--backend', 'inproc')
    out.mkdir(parents=True, exist_ok=True)
    try:
        dev = DryDev('--dry-realtime' in argv, int(opt('--dry-lose-after', '0'))) if dry else InprocDev(wait=float(opt('--connect-wait', '20'))) if backend == 'inproc' else CliDev()
    except Lost as e:
        (out / 'PHONE_LOST.txt').write_text('PHONE LOST at step 0 (no console at start): %s\n' % e); print('PHONE LOST at start:', e); return 3
    try:
        if '--calibrate' in argv: calibrate(out, dev, coords); return 0
        if '--latency' in argv: latency(dev, coords); return 0
        minutes = float(opt('--minutes', '8.5')); secs = minutes * 60
        if secs > MAX_WINDOW_S: print('--minutes %.2f > %d s: one capture window is at most 540 s (console tool limit 600 s)' % (minutes, MAX_WINDOW_S)); return 2
        seed, window = int(opt('--seed', '1')), int(opt('--window', '1'))
        plan = parse_plan(opt('--plan')) if opt('--plan') else scale_plan(DEFAULT_PLAN, secs)
        log = Path(opt('--log')) if opt('--log') else None
        if not log and not dry: print('--log CONSOLE_FILE is required (the caller captures the console)'); return 2
        rng = random.Random(seed * 1000 + window)
        r = Runner(out, dev, log, rng, coords, window, secs, float(opt('--lost-wait', '60')),
                   shots='--no-shots' not in argv, dry=dry)
        r.write(kind='event', event='config', seed=seed, plan=plan, minutes=minutes, backend=backend, coords=coords, dry=dry)
        print('stress window %d: seed %d, %.1f min, plan %s, backend %s%s' % (window, seed, minutes, ','.join('%s:%d' % p for p in plan), backend, ' DRY' if dry else ''), flush=True)
        t0 = time.time(); rc, why = r.run(plan)
        r.write(kind='event', event='end', rc=rc, why=why)
        if rc in (3, 4):
            (out / 'PHONE_LOST.txt').write_text('%s\nwindow %d phase %s t=%s\nlast console lines (%s):\n%s\n' % (
                why, window, r.phase, time.strftime('%F %T'), log, '\n'.join(tail_lines(log) if log else [])))
        summ = dict(window=window, seed=seed, rc=rc, why=why, steps=r.step, failed_calls=r.fail, seconds=round(time.time() - t0, 1),
                    checkpoints=r.cp, log=str(log) if log else None, log_bytes_end=r.logsize())
        (out / 'summary.json').write_text(json.dumps(summ, indent=1) + '\n')
        print('window %d: %s, %d steps (%d failed), %d checkpoints, %.0f s' % (window, why, r.step, r.fail, r.cp, summ['seconds']), flush=True)
        return rc
    finally:
        dev.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
