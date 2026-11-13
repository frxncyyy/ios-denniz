#!/usr/bin/env python3
"""Synthetic inputs for the host self-tests: Photos-like 1/2-scale screenshots (with injected corruptions) and console logs.
Deterministic (seeded). Used by test_stress.py; can also be run by hand:  synth.py OUTDIR  (writes a sample set)."""
import json, random, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

W, H = 540, 1200
GRID_Y0, CELL, GAP, COLS = 120, 178, 3, 3


def thumb(rng, size):
    """a smooth 'photo': gradient + a few soft shapes + mild noise"""
    c0 = [rng.randint(20, 235) for _ in range(3)]; c1 = [rng.randint(20, 235) for _ in range(3)]
    im = Image.new('RGB', (size, size)); px = im.load()
    for y in range(size):
        t = y / size
        row = tuple(int(c0[k] * (1 - t) + c1[k] * t) for k in range(3))
        for x in range(size): px[x, y] = row
    d = ImageDraw.Draw(im)
    for _ in range(3):
        x, y, r = rng.randint(0, size), rng.randint(0, size), rng.randint(15, 60)
        d.ellipse((x - r, y - r, x + r, y + r), fill=tuple(rng.randint(30, 230) for _ in range(3)))
    im = im.filter(ImageFilter.GaussianBlur(3))
    n = Image.effect_noise((size, size), 6).convert('RGB')
    return Image.blend(im, n, 0.04)


def photos_screen(seed=1, dark=True, corrupt=None):
    rng = random.Random(seed)
    bg = (0, 0, 0) if dark else (255, 255, 255)
    im = Image.new('RGB', (W, H), bg); d = ImageDraw.Draw(im)
    d.rectangle((0, 0, W, 60), fill=bg); d.text((20, 70), 'Library', fill=(255, 255, 255) if dark else (0, 0, 0))
    thumbs = {}
    for i in range(6):
        for j in range(COLS):
            x = j * (CELL + GAP); y = GRID_Y0 + i * (CELL + GAP)
            if y > 1000: break
            t = thumb(rng, CELL); thumbs[(i, j)] = (x, y); im.paste(t, (x, y))
    # segmented control + tab bar (translucent: blur of what is behind)
    bar = im.crop((0, 1010, W, H)).filter(ImageFilter.GaussianBlur(12))
    bar = Image.blend(bar, Image.new('RGB', bar.size, (22, 22, 24) if dark else (248, 248, 248)), 0.85); im.paste(bar, (0, 1010))
    d = ImageDraw.Draw(im)
    for k, x in enumerate((67, 202, 340, 472)):
        col = (0, 122, 255) if k == 0 else (140, 140, 145)
        d.rounded_rectangle((x - 22, 1100, x + 22, 1140), 6, fill=col); d.text((x - 20, 1150), ['Library', 'For You', 'Albums', 'Search'][k], fill=col)
    if corrupt == 'black':
        for key in ((1, 1), (2, 0)):
            x, y = thumbs[key]; d.rectangle((x, y, x + CELL - 1, y + CELL - 1), fill=(0, 0, 0))
        if not dark: pass
    elif corrupt == 'stripes':
        x, y = thumbs[(2, 1)]; other = thumb(random.Random(999), CELL)
        for k in range(0, CELL, 24):                       # bands from another picture every 24 rows (two scroll positions)
            im.paste(other.crop((0, k, CELL, min(CELL, k + 10))), (x, y + k))
    elif corrupt == 'swap':
        r, g, b = im.split(); im = Image.merge('RGB', (b, g, r))
    elif corrupt == 'tabbar':
        piece = im.crop((0, 400, W, 470))                  # grid rows pasted into the bar at the wrong offset
        im.paste(piece, (0, 1090))
    elif corrupt == 'scrolled':                            # different content (another scroll position): not comparable
        im = photos_screen(seed + 50, dark)
    return im


# ---------------------------------------------------------------- console logs
def pace(fps, active=200, k1=None, hz=90.0):
    te = int(1e6 / hz); k1 = active if k1 is None else k1
    return ('NORD2 FB PACE: panel_hz=%.1f te_us=%d hb_us=11111 mode=0 pushes=%d active=%d idle_gaps=1 active_fps=%.1f k1_permille=%d '
            'refresh_per_present_1/2/3/4/5+=%d/%d/0/0/0 dropped=%d (ready 0 nosw 0) best_run=50 ivl_ms<=12.5/18/24/35/50/100=%d/0/0/0/0/0' %
            (hz, te, active + 1, active, fps, 1000 * k1 // max(active, 1), k1, active - k1, active - k1, active))


def present(n, copy=2000):
    return 'NORD2 FB PRESENT: n=%d avg_us=11000 max_us=14000 copy_us=%d/%d xfer_us=9000/10000 legacy=0 timeouts=0 busy_skips=0 busy_forced=0 late_ticks=0 rate_hz=90' % (n, copy, copy * 2)


def mem(free, private, bos):
    return ('NORD2 MEM: free=%d wired=28286 active=71397 inactive=16395 spec=55068 throttled=0 pageable_int=35065 pageable_ext=107795 avail=212394 compressed=0 free_target=3867 (16K pages)\n'
            'NORD2 MEM GPU: bos_live=%d released_held=0 fullest_handle_table=314/8192 private=%d MiB' % (free, bos, private))


def hwline(mode, hw, cpu, **kw):
    s = 'NORD2 FB HW: mode=%d hw_frames=%d cpu_frames=%d hw_race=0 ovl_und=0 smi_und=0 eof_abn=0 dsi_und=0 fd_timeout=0 readback_bad=0 fallback=0' % (mode, hw, cpu)
    for k, v in kw.items(): s = s.replace('%s=0' % k, '%s=%d' % (k, v))
    return s


def window_log(phases, bad=False, t0=1000.0, base_private=600, win=1, hwmode=0):
    """-> (log text, steps list). phases: list of (name, seconds). Each 5 s: PACE/PRESENT/JOBS/MEM/HW; bad=True injects faults."""
    lines = ['NORD2 USB: native console continuous', 'boot noise CRASH driverkitd[71] sig 9 code 0x0 sub 0x0 slide 0x0 mh 0x0']
    steps = []; t = t0; hw = cpu = 0; size = lambda: sum(len(l) + 1 for l in lines)
    steps.append(dict(kind='event', event='start', t=t, window=win, phase='setup', log_offset=size()))
    for name, secs in phases:
        steps.append(dict(kind='phase', event='begin', t=t, window=win, phase=name, log_offset=size()))
        for k in range(int(secs // 5)):
            t += 5
            game = name == 'subway'
            fps = (62.0 if game else 89.6)
            if bad and name == 'photos': fps = 41.0
            act = int(fps * 4.6)
            lines.append(pace(fps, act, act if not (bad and name == 'photos') else int(act * 0.6)))
            lines.append(present(int(fps * 5)))
            lines.append('NORD2 GPU JOBS: n=900 fault=%d full=0 win_ms=5000 q_us=150/1087 run_us=964/3571' % (1 if bad and k == 1 and name == 'photos' else 0))
            priv = base_private + (40 * (t - t0) / 3600 if not bad else 400 * (t - t0) / 3600)
            lines.append(mem(60000 - int(t - t0), int(priv), 1400))
            hw += 400 if hwmode >= 2 else 0; cpu += 30 if hwmode >= 2 else 400
            lines.append(hwline(hwmode, hw, cpu))
            if name == 'photos':
                lines.append('NORD2 GPU USER: NORD2_RW I PERF PHOTOS A <MobileSlideShow> fps=%.1f frames=%d' % (fps, int(fps * 5)))
            if game:
                lines.append('NORD2 GPU USER: NORD2_RW I PERF <SubwaySurf> perf: 5 s, 300 command buffers')
            if k % 3 == 0:
                lines.append('NORD2 GPU USER: NORD2_RW W PERF <backboardd> slow draw: 1.%d ms (Gallium 0.4 ms) x' % k)
                lines.append('NORD2 GPU ALLOC SLOW: bytes=3145728 flags=4 md=95 prepare=648 zero=1373 segments=102 map=225 total=%d us (lock dropped)' % (2443 + k))
            steps.append(dict(kind='step', step=len(steps), t=t, window=win, phase=name, action='fling up 120ms', rc=0, log_offset=size()))
        if bad and name == 'photos':
            lines.append('NORD2 FB HW: AUTO-DISABLE ovl-underflow ovl0 int 0x4 kern.n2fbhw 2 -> 0')
            lines.append('CRASH MobileSlideShow[311] sig 11 code 0x1 sub 0x0 slide 0x1000 mh 0x100000000')
        if bad and name == 'subway':
            lines.append('NORD2 GPU USER: NORD2_RW W PERF <SubwaySurf> slow commit: 296.0 ms')
            lines.append('CRASH SubwaySurf[281] sig 6 code 0x0 sub 0x0 slide 0x1000 mh 0x100000000')
        steps.append(dict(kind='phase', event='end', t=t, window=win, phase=name, log_offset=size()))
    steps.append(dict(kind='event', event='end', t=t, window=win, phase='end', rc=0, log_offset=size()))
    return '\n'.join(lines) + '\n', steps


def fill(wdir, bad=False, hwmode=0, private0=600, t_night0=None):
    """Turn a stress_run.py --dry-run window into a realistic one: console.log with a stats block every 5 s of the steps'
    (simulated) time, every step's log_offset set, summary.json 'log' -> console.log, Photos checkpoint pictures replaced by
    synthetic grids (bad=True: faults + a corrupted checkpoint pair + a game crash)."""
    wdir = Path(wdir); steps = [json.loads(l) for l in open(wdir / 'steps.jsonl')]
    lines = []; size = [0]
    def put(l): lines.append(l); size[0] += len(l) + 1
    put('stress window console (synthetic)')
    t_next = None; hw = cpu = 0; phase = 'setup'; play = False; t0 = t_night0 or (steps[0]['t'] if steps else 0)
    corrupt_done = False
    for e in steps:
        t = e.get('t', t0)
        if t_next is None: t_next = t + 5
        while t >= t_next:
            ui = phase not in ('subway', 'idle', 'setup', 'end')
            fps = 62.5 if (phase == 'subway' and play) else 89.7 if ui else 30.0
            if bad and phase == 'photos': fps = 52.0
            act = int(fps * 4.7) if ui or play else 20
            k1 = int(act * 0.7) if bad and phase == 'photos' else act
            put(pace(fps, act, k1)); put(present(int(fps * 5)))
            put('NORD2 GPU JOBS: n=900 fault=%d full=0 win_ms=5000 q_us=150/1087 run_us=964/3571' % (1 if bad and phase == 'photos' else 0))
            priv = private0 + ((300 if bad else 10) * (t_next - t0) / 3600)
            put(mem(60000 - int((t_next - t0) / 10), int(priv), 1400))
            hw += 400 if hwmode >= 2 else 0; cpu += 30 if hwmode >= 2 else 400; put(hwline(hwmode, hw, cpu))
            if phase == 'photos': put('NORD2 GPU USER: NORD2_RW I PERF NORD2 PERF PHOTOS A MobileSlideS opt=1 fps=%.1f fr=300 cb=310 lat=1.0/2.0ms commit=0.7ms drvcpu=40.0ms/s' % fps)
            if phase == 'subway' and play: put('NORD2 GPU USER: NORD2_RW W PERF <SubwaySurf> slow draw: 2.1 ms (Gallium 0.4 ms) SYBO/Bend/Combined | 1 colour')
            t_next += 5
        if e.get('kind') == 'phase' and e.get('event') == 'begin': phase = e['phase']; play = False
        if e.get('event') == 'subway play': play = True
        if bad and e.get('kind') == 'phase' and e.get('event') == 'end' and e['phase'] == 'subway':
            put('CRASH SubwaySurf[281] sig 11 code 0x1 sub 0x0 slide 0x1000 mh 0x100000000')
        if bad and e.get('kind') == 'phase' and e.get('event') == 'end' and e['phase'] == 'photos':
            put('NORD2 FB HW: AUTO-DISABLE ovl-underflow (ovl0 b2): kern.n2fbhw 2 -> 0, hw_frames=1 ovl_int=00000004 dsi=00000000 race_run=0')
        e['log_offset'] = size[0]
        if e.get('kind') == 'shot' and e.get('file') and 'photos' in e.get('name', ''):
            c = None
            if bad and not corrupt_done and e['name'].endswith('-a'): c = 'black'
            if bad and corrupt_done == 'a': c = 'black'
            photos_screen(3, corrupt=c).save(e['file'])
            if c and e['name'].endswith('-a'): corrupt_done = 'a'
            elif c: corrupt_done = True
    (wdir / 'console.log').write_text('\n'.join(lines) + '\n')
    (wdir / 'steps.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in steps))
    sf = wdir / 'summary.json'; summ = json.loads(sf.read_text()) if sf.exists() else {}
    summ['log'] = str(wdir / 'console.log'); sf.write_text(json.dumps(summ, indent=1) + '\n')
    return wdir / 'console.log'


def main(argv):
    if argv and argv[0] == '--fill':
        print(fill(argv[1], bad='--bad' in argv, hwmode=int(argv[argv.index('--hwmode') + 1]) if '--hwmode' in argv else 0)); return
    out = Path(argv[0] if argv else 'synth-out'); out.mkdir(parents=True, exist_ok=True)
    for c in (None, 'black', 'stripes', 'swap', 'tabbar', 'scrolled'):
        photos_screen(3, corrupt=c).save(out / ('photos-%s.png' % (c or 'good')))
    text, steps = window_log([('photos', 60), ('subway', 60)])
    (out / 'console.log').write_text(text); (out / 'steps.jsonl').write_text(''.join(json.dumps(s) + '\n' for s in steps))
    print('wrote', out)


if __name__ == '__main__':
    main(sys.argv[1:])
