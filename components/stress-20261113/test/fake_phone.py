"""Fake autotest/phone.py for host tests (loaded through STRESS_PHONE_PY). Same names as phone.py: Phone, Gone, png.
The kernel log is a deterministic stream of 100-byte lines 'L%08d ...\\n', one line every FAKE_LINE_MS ms (discrete, so the
compare-and-swap rewind can succeed), into a buffer of FAKE_CAP bytes that a rewind restarts at 0 (lines printed while the buffer
is full are lost, as on the phone). State shared across processes in FAKE_STATE (JSON): t0, buffer start line, rewinds, boot.
FAKE_GONE_AFTER_S: the device disappears that many seconds after t0. FAKE_REBOOT_AT_S: boot id changes and the stream restarts.
FAKE_HANG_GESTURE: the Nth gesture of a process blocks forever (watchdog test)."""
import json, os, time
from pathlib import Path

LINE = 100
CAP = int(os.environ.get('FAKE_CAP', 65536))
LINE_MS = float(os.environ.get('FAKE_LINE_MS', 20))
STATE = Path(os.environ['FAKE_STATE'])
_gest = [0]


class Gone(Exception):
    pass


def line(i):
    s = 'L%08d NORD2 FAKE: filler' % i
    return (s + ' ' * (LINE - 1 - len(s)) + '\n').encode()


def png(path, w, h, rgb):
    from PIL import Image
    Image.frombytes('RGB', (w, h), rgb).save(path)


class Phone:
    def __init__(self, wait=0.0):
        end = time.monotonic() + wait
        while self._gone():
            if time.monotonic() >= end: raise Gone('"Nord2 XNU console" is not on USB (fake)')
            time.sleep(0.2)

    # -- shared state
    def _st(self):
        if not STATE.exists():
            STATE.write_text(json.dumps(dict(t0=time.time(), start=0, rewinds=0, boot=7)))
        st = json.loads(STATE.read_text())
        rb = os.environ.get('FAKE_REBOOT_AT_S')
        if rb and st['boot'] == 7 and time.time() - st['t0'] >= float(rb):
            st = dict(t0=time.time(), start=0, rewinds=0, boot=8, rebooted=True); STATE.write_text(json.dumps(st))
        return st

    def _gone(self):
        g = os.environ.get('FAKE_GONE_AFTER_S')
        return bool(g) and time.time() - self._st()['t0'] >= float(g)

    def _produced(self, st):
        return int((time.time() - st['t0']) * 1000 / LINE_MS)          # lines printed since t0

    def _published(self, st):
        return min(CAP // LINE * LINE, (self._produced(st) - st['start']) * LINE)

    def _check(self):
        if self._gone(): raise Gone('the phone left USB (fake)')

    # -- phone.py API used by the stress tools
    def status(self):
        self._check(); st = self._st()
        return dict(version=2, busy=0, boot=st['boot'], log_rewinds=st['rewinds'], panel_off=0)

    def log_info(self):
        self._check(); st = self._st()
        return dict(requests=0, resets=0, capacity=CAP // LINE * LINE, published=self._published(st))

    def log_read(self, offset, limit):
        self._check(); st = self._st(); pub = self._published(st)
        end = min(pub, offset + limit, offset + 1024 * 8)
        if end <= offset: return b''
        first = offset // LINE
        data = b''.join(line(st['start'] + i) for i in range(first, (end + LINE - 1) // LINE))
        return data[offset - first * LINE:end - first * LINE]

    def log_rewind(self, size):
        self._check(); st = self._st()
        if self._published(st) != size: return False
        full = self._published(st) >= CAP // LINE * LINE
        st['start'] = self._produced(st) if full else st['start'] + size // LINE
        st['rewinds'] += 1; STATE.write_text(json.dumps(st))
        return True

    def gesture(self, paths, ms=0, before=30, after=0, keep=False, cont=False):
        self._check(); _gest[0] += 1
        if int(os.environ.get('FAKE_HANG_GESTURE', 0)) == _gest[0]:
            while True: time.sleep(1)
        time.sleep((ms + before + after) / 1000 * float(os.environ.get('FAKE_SPEED', 0.01)))

    def key(self, name, ms=80): self._check()

    def snapshot(self, scale=2, tries=3, crop=None, packed=True):
        self._check(); w, h = 1080 // scale, 2400 // scale
        return w, h, bytes([40]) * (w * h * 3), dict(presents_during_snap=0, panel_off=0, transferred=w * h * 3)

    def close(self): pass
