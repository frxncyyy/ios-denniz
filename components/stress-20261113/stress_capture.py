#!/usr/bin/env python3
"""Console capture for ONE stress window (windows 2..N of run_night.sh; window 1 is txn_g.sh's own capture).
Reads the kernel's 4 MiB live log (console protocol 2, the same requests as phone.py logs / tools/nord2-usb-console.py) from where
the previous window stopped, writes it to OUT_FILE, and rewinds the kernel buffer once more than --rewind-at bytes were read (same
compare-and-swap rule as phone.py: only if nothing was printed since the read, so no byte is lost). Must be the ONLY log reader
while it runs (no nord2-usb-console.py / phone.py logs at the same time); gestures and screenshots may run in parallel.

  stress_capture.py OUT_FILE --state STATE.json [--seconds 560] [--stop-file F] [--rewind-at 1048576] [--init-from PREV_LOG]

--state: run-wide JSON {boot, rewinds, offset, head}; created/updated here. --init-from PREV_LOG (first use, e.g. window 1's
  xnu-live.log, which txn_g.sh read from offset 0 without rewinds): start at len(PREV_LOG) if the phone's buffer still begins with
  the same bytes, else at 0.
Stops after --seconds (<= 600), when --stop-file exists (then reads what is left), or when the phone leaves USB for > --lost-wait s.
Events (OUT_FILE + '.events.jsonl'): start, rewind, REBOOT (boot id changed: read from 0), BUFFER FULL (lines lost until rewind),
  REWOUND BY OTHER, LOST, end.  Exit 0 ok, 3 phone lost, 4 phone rebooted during/before this window (log still captured).
"""
import importlib.util, json, os, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PHONE_PY = Path(os.environ.get('STRESS_PHONE_PY') or HERE.parent / 'autotest' / 'phone.py')   # override: tests (fake phone)


def main(argv):
    if not argv or argv[0] in ('-h', '--help'): print(__doc__); return 0
    opt = lambda k, d=None: argv[argv.index(k) + 1] if k in argv else d
    out = Path(argv[0]); state_f = Path(opt('--state')); secs = float(opt('--seconds', '560'))
    assert 0 < secs <= 600, '--seconds must be <= 600'
    stop = Path(opt('--stop-file')) if opt('--stop-file') else None
    rewind_at = int(opt('--rewind-at', str(1 << 20))); lost_wait = float(opt('--lost-wait', '60'))
    out.parent.mkdir(parents=True, exist_ok=True)
    ev_f = open(str(out) + '.events.jsonl', 'a')
    def ev(**kw):
        kw['t'] = round(time.time(), 3); kw['file_bytes'] = out.stat().st_size if out.exists() else 0
        ev_f.write(json.dumps(kw) + '\n'); ev_f.flush(); print('capture:', json.dumps(kw), flush=True)
    spec = importlib.util.spec_from_file_location('nord2_phone', PHONE_PY); P = importlib.util.module_from_spec(spec); spec.loader.exec_module(P)

    def connect(wait):
        try: return P.Phone(wait=wait)
        except P.Gone: return None
    p = connect(lost_wait)
    if not p: ev(event='LOST', why='no console at window start'); return 3
    rc = 0
    try:
        st = p.status(); info = p.log_info()
        ident = dict(boot=st['boot'], rewinds=st['log_rewinds'])
        # 'head' = the first <= 64 bytes of the kernel buffer as WE read them since its last rewind (identifies the buffer
        # generation; compared as a prefix, so a buffer that was empty at the rewind still matches later)
        state = json.loads(state_f.read_text()) if state_f.exists() else None
        saved = bytes.fromhex(state.get('head', '')) if state else b''
        off = 0; headb = b''
        if state is None and opt('--init-from') and Path(opt('--init-from')).exists():
            prev = Path(opt('--init-from')).read_bytes()
            if prev[:64] == p.log_read(0, min(64, len(prev))) and len(prev) <= info['published']:
                off = len(prev); headb = prev[:64]; ev(event='start', how='continue after window 1 capture', offset=off, published=info['published'])
            else:
                ev(event='start', how='window 1 log does not match the buffer head: reading from 0', published=info['published'])
        elif state is None:
            ev(event='start', how='fresh', published=info['published'])
        elif state.get('boot') != ident['boot']:
            ev(event='REBOOT', old_boot=state.get('boot'), new_boot=ident['boot'], published=info['published']); rc = 4
        elif state.get('rewinds') != ident['rewinds'] or (saved and p.log_read(0, len(saved)) != saved) or state.get('offset', 0) > info['published']:
            ev(event='REWOUND BY OTHER', state=state, rewinds=ident['rewinds'], published=info['published'])
        else:
            off = state['offset']; headb = saved; ev(event='start', how='continue', offset=off, published=info['published'])

        def save():
            nonlocal headb
            if len(headb) < 64 and off > len(headb): headb = p.log_read(0, min(64, off))
            state_f.write_text(json.dumps(dict(offset=off, head=headb.hex(), **ident)))
        fh = open(out, 'ab'); end = time.monotonic() + secs; full_noted = False; stopping = None
        while True:
            try:
                info = p.log_info()
                while off < info['published']:
                    data = p.log_read(off, info['published'] - off)
                    if not data: break
                    off += len(data); fh.write(data); fh.flush(); save()
                if info['published'] >= info['capacity'] and not full_noted:
                    ev(event='BUFFER FULL', capacity=info['capacity']); full_noted = True
                if off >= rewind_at:
                    for _ in range(40):
                        if p.log_rewind(off):
                            ident['rewinds'] = p.status()['log_rewinds']; ev(event='rewind', at=off); off = 0
                            headb = b''; full_noted = False; save(); break
                        data = p.log_read(off, p.log_info()['published'] - off)
                        off += len(data); fh.write(data); fh.flush(); save()
                    else:
                        ev(event='rewind failed 40x (phone printing continuously); will retry', at=off)
            except (P.Gone, OSError) as e:
                ev(event='usb error', err=str(e)[:200])
                try: p.close()
                except Exception: pass
                p = connect(lost_wait)
                if not p: ev(event='LOST', why=str(e)[:200]); return 3
                st = p.status()
                if st['boot'] != ident['boot']:
                    ev(event='REBOOT', old_boot=ident['boot'], new_boot=st['boot']); rc = 4
                    ident = dict(boot=st['boot'], rewinds=st['log_rewinds']); off = 0; headb = b''; save()
                continue
            now = time.monotonic()
            if stopping is None and (now >= end or (stop and stop.exists())): stopping = now + 1.0   # one more read for the tail
            if stopping is not None and now >= stopping: break
            time.sleep(0.25)
        ev(event='end', offset=off)
        return rc
    finally:
        try: p and p.close()
        except Exception: pass


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
