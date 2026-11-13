#!/usr/bin/env python3
"""Host self-tests of the overnight stress harness (no phone). Run: python3 test/test_stress.py [-k name] [--keep]
Covers: shotcheck detectors (+ persistence, baseline medoid), record extraction on interleaved/truncated console text, the verdict
on synthetic PASS and FAIL runs (each failure class), stress_run dry determinism / window limit / phone loss / in-process watchdog
(fake phone), stress_capture continuity across rewinds / reboot / loss (fake phone), run_night.sh + run_night_resume.sh dry runs.
Scratch goes to test/_tmp (deleted unless --keep)."""
import json, os, random, shutil, subprocess, sys, time, traceback
from pathlib import Path

T = Path(__file__).resolve().parent; H = T.parent
sys.path.insert(0, str(H)); sys.path.insert(0, str(T))
import shotcheck, stress_verdict as SV, synth  # noqa: E402

TMP = T / '_tmp'
RESULTS = []


def test(fn):
    RESULTS.append(fn); return fn


def run(cmd, env=None, timeout=300):
    e = dict(os.environ); e.update(env or {})
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, env=e, timeout=timeout)
    return r.returncode, r.stdout + r.stderr


def verdict_of(sd, *extra):
    rc, out = run([sys.executable, H / 'stress_verdict.py', sd, '--json', sd / 'v.json'] + list(extra))
    return rc, out, json.loads((sd / 'v.json').read_text()) if (sd / 'v.json').exists() else None


def make_run(name, phases, bad=False, windows=1, hwmode=0, mutate=None, shots=None):
    """synthetic STRESS_DIR from synth.window_log; mutate(text, steps, w) may edit the window"""
    sd = TMP / name
    if sd.exists(): shutil.rmtree(sd)
    t0 = 1_000_000.0
    for w in range(1, windows + 1):
        d = sd / ('w%02d' % w); d.mkdir(parents=True)
        text, steps = synth.window_log(phases, bad=bad, t0=t0, win=w, hwmode=hwmode)
        for s in steps:
            if s.get('phase') == 'subway' and s.get('kind') == 'phase' and s['event'] == 'begin':
                steps.insert(steps.index(s) + 1, dict(kind='event', event='subway play', t=s['t'] + 1, window=w, phase='subway', log_offset=s['log_offset']))
                break
        summ = dict(window=w, rc=0, why='window done', log=str(d / 'console.log'))
        if mutate: text, steps, summ = mutate(text, steps, summ, w, d)
        if shots: steps = shots(steps, d)
        (d / 'console.log').write_text(text); (d / 'steps.jsonl').write_text(''.join(json.dumps(s) + '\n' for s in steps))
        (d / 'summary.json').write_text(json.dumps(summ))
        t0 = steps[-1]['t'] + 10
    return sd


GOOD = [('photos', 60), ('spam', 40), ('switcher', 30), ('subway', 60), ('idle', 20)]


# ---------------------------------------------------------------- shotcheck
@test
def shot_detectors():
    base = synth.photos_screen(3)
    exp = dict(black='black', stripes='stripes', swap='swap', tabbar='tabbar')
    for c, kind in exp.items():
        r = shotcheck.analyse(synth.photos_screen(3, corrupt=c), base)
        kinds = {d[0] for d in r['defects']}
        assert kind in kinds, (c, r['defects'])
    r = shotcheck.analyse(synth.photos_screen(3), base); assert not r['defects'] and r['comparable'], r
    r = shotcheck.analyse(synth.photos_screen(3, corrupt='scrolled'), base); assert r['comparable'] is False and not r['defects'], r
    for seed in (5, 9, 11):                                  # other clean grids, light mode too: no false defect
        for dark in (True, False):
            g = synth.photos_screen(seed, dark=dark)
            r = shotcheck.analyse(g); assert shotcheck.self_ok(r), (seed, dark, r['defects'], r.get('geom'))
            r = shotcheck.analyse(g, g); assert not r['defects'], (seed, dark, r['defects'])


@test
def shot_persistence():
    base = synth.photos_screen(3)
    ra = shotcheck.analyse(synth.photos_screen(3, corrupt='black'), base); rb = shotcheck.analyse(synth.photos_screen(3), base)
    v, pers, trans, score = shotcheck.pair_verdict(ra, rb); assert v == 'WARN' and not pers and trans, (v, pers, trans)
    v, pers, trans, score = shotcheck.pair_verdict(ra, ra); assert v == 'FAIL' and pers, (v, pers)


# ---------------------------------------------------------------- extraction
@test
def extract_interleaved():
    pace = synth.pace(89.7, 400)
    text = ('junk ' + pace[:180] + 'NORD2 GPU JOBS: n=5 fault=2 full=0\n' + 'NORD2 FB HW: mode=3 pm=2 hw_frames=10 cpu_frames=5 faults=0 last_fault=none race=1'
            + 'SWEET FB: x\n' + pace + '\n' + 'NORD2 GPU USER: NORD2_RW W PERF <Photos> slow: 123.4 ms commit (0.0 ms GPU wait), 1 draws: x\n'
            + 'NORD2 GPU USER: NORD2_RW F CRASH signal 6 at 0x1: pc x\nCRASH MobileSlideShow[12] sig 6 code 0x0 sub 0x0 slide 0x0 mh 0x0\n'
            + '12.345 memorystatus: killing_top_process pid 99 [SubwaySurf] (vm-pageshortage 1) 1000KB - memorystatus_available_pages: 10 compressor_size:1\n'
            + 'NORD2 HANG: SpringBoard[26] main thread stack #1 (9 frames, cache base 0x1):\nNORD2 HANG: SpringBoard main-thread stack probe on\n')
    E = SV.extract(text); kinds = [k for _, k, _ in E]
    assert kinds.count('pace') == 2, kinds                     # the truncated one parses without raising
    assert ('jobs', 2) in [(k, d.get('fault')) for _, k, d in E]
    assert any(k == 'hw' and d['hw'] == 10 and d['race'] == 1 for _, k, d in E)
    assert any(k == 'slow' and d['what'] == 'commit' and d['ms'] == 123.4 for _, k, d in E)
    assert kinds.count('crash') == 1 and kinds.count('ucrash') == 1 and kinds.count('jetsam') == 1 and kinds.count('hang') == 1, kinds


# ---------------------------------------------------------------- verdict on synthetic runs
@test
def verdict_pass():
    sd = make_run('pass', GOOD, windows=2)
    rc, out, v = verdict_of(sd)
    assert rc == 0 and v['verdict'] == 'PASS', out


@test
def verdict_fail_all():
    sd = make_run('failall', GOOD, bad=True)
    rc, out, v = verdict_of(sd)
    assert rc == 1, out
    msgs = ' | '.join(i['msg'] for i in v['items'] if i['level'] == 'FAIL')
    for want in ('GPU JOBS fault', 'AUTO-DISABLE', 'MobileSlideShow', 'SubwaySurf', 'FIXED90 FAIL'):
        assert want in msgs, (want, msgs)
    assert v['requirements']['R2'] == 'FAIL' and v['requirements']['R3'] == 'FAIL', v['requirements']


def _inject(extra, phase='spam'):
    def m(text, steps, summ, w, d):
        off = [s for s in steps if s.get('phase') == phase and s.get('kind') == 'step'][2]['log_offset']
        return text[:off] + extra + '\n' + text[off:], [dict(s, log_offset=s['log_offset'] + (len(extra) + 1 if s['log_offset'] > off else 0)) for s in steps], summ
    return m


@test
def verdict_fail_classes():
    cases = {
        'NORD2 PRFCNT: AS0 fault status 000000c1 address 0000000012345678 extra 0': ('R4', 'GPU MMU fault'),
        'NORD2 FB PANEL: 90 Hz reverted: reason=underrun xfer_lo_us=1 xfer_hi_us=2 underrun=1 faults=2 present 5 of the guard window, retries 1/2': ('R1', 'panel'),
        'NORD2 SURF USE: report t=60s mode 2: incs 5 decs 4 (user 1/1, display 1/1, gpu 1/1), live 1 (display 0, gpu 0) max-live 2 max-count 1; imbalance: unmatched-dec 3 dup-inc 0 kernel-unmatched 0/0 not-found 0; dropped-at-release 0 (0 mappings), free-nonzero 0, saturated 0, refused-off 0': ('R4', 'imbalance'),
        'NORD2 SURF USE: LEAK at free: surface 5 10x10 fmt 0x1 freed with counts 1/0, display 0, gpu 0 (free-nonzero 1)': ('R4', 'LEAK'),
        'NORD2 HANG: SpringBoard[26] main thread stack #1 (9 frames, cache base 0x1):': ('R4', 'hang'),
        '1.000 memorystatus: killing_top_process pid 77 [MobileSlideShow] (vm 1) 10KB - memorystatus_available_pages: 1 compressor_size:1': ('R4', 'jetsam'),
        'NORD2 FB HW: BACKOFF exhausted (4 faults): HW composition stays off (kern.n2fbhw=2 turns it on again)': ('R4', 'BACKOFF'),
        'CRASH backboardd[40] sig 11 code 0x1 sub 0x0 slide 0x0 mh 0x0': ('R4', 'backboardd'),
        'panic(cpu 0 caller 0xfffffe0007): "test"': ('R4', 'panic'),
    }
    for i, (line, (req, word)) in enumerate(cases.items()):
        sd = make_run('cls%d' % i, GOOD, mutate=_inject(line))
        rc, out, v = verdict_of(sd)
        fails = [x for x in v['items'] if x['level'] == 'FAIL']
        assert rc == 1 and any(x['req'] == req and word in x['msg'] for x in fails), (line, out[-800:])


@test
def verdict_benign():
    """sig 9 kill = INFO, unrelated daemon crash = WARN, allow-list, boot-time crash ignored"""
    sd = make_run('benign', GOOD, mutate=_inject('CRASH SubwaySurf[281] sig 9 code 0x0 sub 0x0 slide 0x0 mh 0x0\nCRASH parsecd[230] sig 5 code 0x0 sub 0x0 slide 0x0 mh 0x0'))
    rc, out, v = verdict_of(sd)
    assert rc == 3 and v['verdict'] == 'WARN', out
    rc, out, v = verdict_of(sd, '--allow-crash', 'parsecd')
    assert rc == 0, out


@test
def verdict_loss_reboot_leak():
    def lost(text, steps, summ, w, d):
        summ.update(rc=3, why='PHONE LOST at step 9: x'); (d / 'PHONE_LOST.txt').write_text('PHONE LOST at step 9'); return text, steps, summ
    rc, out, v = verdict_of(make_run('lost', GOOD, mutate=lost)); assert rc == 1 and 'PHONE LOST' in out, out
    def reboot(text, steps, summ, w, d):
        if w == 2: (d / 'console.log.events.jsonl').write_text(json.dumps(dict(event='REBOOT', new_boot=8)) + '\n')
        return text, steps, summ
    rc, out, v = verdict_of(make_run('reboot', GOOD, windows=2, mutate=reboot)); assert rc == 1 and 'REBOOT' in out, out
    # leak: 14 windows ~ 2.3 h with private +200 MiB/h
    def leak(text, steps, summ, w, d):
        import re
        return re.sub(r'private=(\d+) MiB', lambda m: 'private=%d MiB' % (int(m.group(1)) + 200 * (w - 1) * 0.17), text), steps, summ
    sd = make_run('leak', [('photos', 60), ('spam', 60), ('idle', 480)], windows=14, mutate=leak)
    rc, out, v = verdict_of(sd); assert rc == 1 and 'leak' in out, out[-1500:]


@test
def verdict_subway_slow():
    def slow(text, steps, summ, w, d):
        return text.replace('active_fps=62.0', 'active_fps=45.0'), steps, summ
    rc, out, v = verdict_of(make_run('subslow', GOOD, mutate=slow))
    assert rc == 1 and v['requirements']['R2'] == 'FAIL' and 'active_fps 45.0 < 60' in out, out


@test
def verdict_shots_hwmode():
    """shots taken while kern.n2fbhw=3 composes are INVALID (stale panel buffer): no grid verdict, WARN 'none valid'"""
    def shots(steps, d):
        (d / 'shots').mkdir(exist_ok=True); out = []
        for s in steps:
            out.append(s)
            if s.get('phase') == 'photos' and s.get('kind') == 'step' and s['step'] in (3, 4):
                for ab in 'ab':
                    f = d / 'shots' / ('cp%d%s.png' % (s['step'], ab)); synth.photos_screen(3, corrupt='black').save(f)
                    out.append(dict(kind='shot', name='photos-c%d-%s' % (s['step'], ab), checkpoint=(1 if s['step'] == 3 else 3) + (ab == 'b'),
                                    file=str(f), t=s['t'], window=1, phase='photos', log_offset=s['log_offset']))
        return out
    rc, out, v = verdict_of(make_run('hw3', GOOD, hwmode=3, shots=shots))
    assert v['requirements']['R3'] == 'WARN' and all(not s['valid'] for s in v['screenshots']), out
    rc, out, v = verdict_of(make_run('hw0', GOOD, hwmode=0, shots=shots))   # same corrupted pictures in CPU mode, explicit clean baseline
    base = TMP / 'clean.png'; synth.photos_screen(3).save(base)
    rc, out, v = verdict_of(TMP / 'hw0', '--baseline', base)
    assert rc == 1 and v['requirements']['R3'] == 'FAIL' and 'corrupted' in out, out


# ---------------------------------------------------------------- stress_run
@test
def run_dry_determinism():
    acts = []
    for name, win in (('a', 1), ('b', 1), ('c', 2)):
        d = TMP / ('det' + name)
        rc, out = run([sys.executable, H / 'stress_run.py', d, '--dry-run', '--minutes', '4', '--seed', '5', '--window', win]); assert rc == 0, out
        acts.append([json.loads(l)['action'] for l in open(d / 'steps.jsonl') if '"step"' in l and json.loads(l)['kind'] == 'step'])
    assert acts[0] == acts[1] and acts[0] != acts[2] and len(acts[0]) > 200, (len(acts[0]), len(acts[2]))
    rc, out = run([sys.executable, H / 'stress_run.py', TMP / 'detx', '--dry-run', '--minutes', '9.5']); assert rc == 2, out
    rc, out = run([sys.executable, H / 'stress_run.py', TMP / 'dety', '--dry-run', '--minutes', '4', '--plan', 'photos:60,bogus:5']); assert rc != 0 and 'unknown phase' in out


@test
def run_dry_loss():
    d = TMP / 'drylose'
    rc, out = run([sys.executable, H / 'stress_run.py', d, '--dry-run', '--minutes', '5', '--dry-lose-after', '100']); assert rc == 3, out
    assert 'PHONE LOST at step' in (d / 'PHONE_LOST.txt').read_text()


@test
def run_fake_inproc():
    """real InprocDev against the fake phone: completes; a hung gesture trips the watchdog; a vanished phone ends the window rc 3"""
    st = TMP / 'fake1.json'; st.unlink(missing_ok=True); log = TMP / 'fake1.log'; log.write_bytes(b'x' * 10)
    env = dict(STRESS_PHONE_PY=str(T / 'fake_phone.py'), FAKE_STATE=str(st))
    rc, out = run([sys.executable, H / 'stress_run.py', TMP / 'fk1', '--log', log, '--minutes', '0.25', '--plan', 'spam:7,photos:8'], env); assert rc == 0, out
    steps = [json.loads(l) for l in open(TMP / 'fk1' / 'steps.jsonl')]
    assert sum(1 for s in steps if s['kind'] == 'step') > 20 and all(s['log_offset'] == 10 for s in steps if s['kind'] == 'step'), out
    assert any(s['kind'] == 'shot' and s.get('file') for s in steps), 'no screenshot written'
    st.unlink(); t0 = time.time()
    rc, out = run([sys.executable, H / 'stress_run.py', TMP / 'fk2', '--log', log, '--minutes', '0.5', '--plan', 'spam:25'], dict(env, FAKE_HANG_GESTURE='5'), timeout=120)
    steps = [json.loads(l) for l in open(TMP / 'fk2' / 'steps.jsonl')]
    assert rc == 0 and any('TimeoutError' in (s.get('err') or '') for s in steps) and time.time() - t0 < 60, out
    st.unlink()
    rc, out = run([sys.executable, H / 'stress_run.py', TMP / 'fk3', '--log', log, '--minutes', '1', '--plan', 'spam:50', '--lost-wait', '3'],
                  dict(env, FAKE_GONE_AFTER_S='4'), timeout=120)
    assert rc == 3 and (TMP / 'fk3' / 'PHONE_LOST.txt').exists(), out


# ---------------------------------------------------------------- stress_capture
def _lines_ok(data, first=None):
    nums = [int(l[1:9]) for l in data.decode().splitlines()]
    gaps = [(a, b) for a, b in zip(nums, nums[1:]) if b != a + 1]
    return nums, gaps


@test
def capture_rewinds_continuity():
    st = TMP / 'fcap.json'; st.unlink(missing_ok=True); state = TMP / 'cap-state.json'; state.unlink(missing_ok=True)
    env = dict(STRESS_PHONE_PY=str(T / 'fake_phone.py'), FAKE_STATE=str(st), FAKE_CAP='65536', FAKE_LINE_MS='10')
    a, b = TMP / 'capA.log', TMP / 'capB.log'
    for f in (a, b): f.unlink(missing_ok=True); Path(str(f) + '.events.jsonl').unlink(missing_ok=True)
    rc, out = run([sys.executable, H / 'stress_capture.py', a, '--state', state, '--seconds', '4', '--rewind-at', '16384'], env); assert rc == 0, out
    time.sleep(1.0)                                            # gap between windows: nobody reads, nothing may be lost
    rc, out = run([sys.executable, H / 'stress_capture.py', b, '--state', state, '--seconds', '4', '--rewind-at', '16384'], env); assert rc == 0, out
    nums, gaps = _lines_ok(a.read_bytes() + b.read_bytes())
    ev = [json.loads(l) for l in open(str(a) + '.events.jsonl')] + [json.loads(l) for l in open(str(b) + '.events.jsonl')]
    assert nums[0] == 0 and not gaps and len(nums) > 600, (nums[:3], gaps[:5], len(nums))
    assert sum(1 for e in ev if e['event'] == 'rewind') >= 3, ev


@test
def capture_init_from_and_reboot():
    st = TMP / 'fcap2.json'; st.unlink(missing_ok=True); state = TMP / 'cap-state2.json'; state.unlink(missing_ok=True)
    env = dict(STRESS_PHONE_PY=str(T / 'fake_phone.py'), FAKE_STATE=str(st), FAKE_CAP='4194304', FAKE_LINE_MS='10')
    import importlib.util
    spec = importlib.util.spec_from_file_location('fp', T / 'fake_phone.py'); os.environ['FAKE_STATE'] = str(st)
    fp = importlib.util.module_from_spec(spec); spec.loader.exec_module(fp)
    p = fp.Phone(); time.sleep(1.0); w1 = TMP / 'w1.log'; w1.write_bytes(p.log_read(0, p.log_info()['published']))   # 'window 1' capture
    c = TMP / 'capC.log'; c.unlink(missing_ok=True)
    rc, out = run([sys.executable, H / 'stress_capture.py', c, '--state', state, '--seconds', '2', '--init-from', w1], env); assert rc == 0, out
    nums, gaps = _lines_ok(w1.read_bytes() + c.read_bytes()); assert nums[0] == 0 and not gaps, gaps[:3]
    d = TMP / 'capD.log'; d.unlink(missing_ok=True)
    rc, out = run([sys.executable, H / 'stress_capture.py', d, '--state', state, '--seconds', '3'], dict(env, FAKE_REBOOT_AT_S='0'))
    assert rc == 4 and 'REBOOT' in out, out
    e = TMP / 'capE.log'; e.unlink(missing_ok=True)
    rc, out = run([sys.executable, H / 'stress_capture.py', e, '--state', state, '--seconds', '5', '--lost-wait', '1'], dict(env, FAKE_GONE_AFTER_S='0'))
    assert rc == 3 and 'LOST' in out, out


# ---------------------------------------------------------------- wrappers
@test
def night_dry():
    runs = TMP / 'runs'
    if runs.exists(): shutil.rmtree(runs)
    env = dict(RUNS=str(runs), WINDOWS='2')
    rc, out = run(['bash', H / 'run_night.sh', 'x.img', 'np', '--dry-run'], env); assert rc == 0 and 'STRESS VERDICT: PASS' in out, out[-1500:]
    rc, out = run(['bash', H / 'run_night.sh', 'x.img', 'np', '--dry-run'], env); assert rc == 2 and 'exists' in out, out
    rc, out = run(['bash', H / 'run_night.sh', 'x.img', 'nb', '--dry-run'], dict(env, DRY_BAD='1')); assert rc == 1 and 'STRESS VERDICT: FAIL' in out, out[-1500:]
    rc, out = run(['bash', H / 'run_night.sh', 'x.img', 'nl', '--dry-run'], dict(env, WINDOWS='3', DRY_LOSE_WINDOW='2')); assert rc == 3 and 'PHONE LOST' in out, out[-1500:]
    assert json.loads((runs / 'nl' / 'stress' / 'night.json').read_text())['next_window'] == 3
    rc, out = run(['bash', H / 'run_night_resume.sh', 'nl', '--dry-run'], dict(RUNS=str(runs))); assert rc == 1 and 'resume nl at window 3 of 3' in out, out[-1500:]
    assert (runs / 'nl' / 'stress' / 'w03' / 'steps.jsonl').exists()


def main(argv):
    keep = '--keep' in argv; only = argv[argv.index('-k') + 1] if '-k' in argv else None
    TMP.mkdir(exist_ok=True); bad = 0
    for fn in RESULTS:
        if only and only not in fn.__name__: continue
        t0 = time.time()
        try:
            fn(); print('ok    %-28s %5.1f s' % (fn.__name__, time.time() - t0), flush=True)
        except Exception:
            bad += 1; print('FAIL  %-28s %5.1f s' % (fn.__name__, time.time() - t0)); traceback.print_exc()
    if not keep: shutil.rmtree(TMP, ignore_errors=True)
    print('ALL OK' if not bad else '%d FAILED' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
