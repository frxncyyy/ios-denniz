#!/usr/bin/env python3
"""Hands-free control of the Nord 2 while iOS runs, over the "Nord2 XNU console" USB device (kernel patch 0296).

  phone.py status                          kernel side state (JSON); with 0640 kernels also panel / brightness / HID delivery
  phone.py panel                           cheap panel state: on/dark, DCS display off sent, brightness (DBV), overlay, counters
  phone.py wait [SECONDS]                  wait until the phone answers (default 240)
  phone.py tap X Y [--ms 60]               one finger down/up
  phone.py press X Y [--ms 800]            long press
  phone.py swipe X0 Y0 X1 Y1 [--ms 300] [--before 30] [--after 0]
  phone.py drag  X0 Y0 X1 Y1 [--ms 600]    swipe that stops before lifting (no flick)
  phone.py pinch CX CY D0 D1 [--ms 600] [--angle 90]   two fingers D0 px apart -> D1 px apart, around CX,CY
  phone.py gesture N X0 Y0 X1 Y1 [X0b Y0b X1b Y1b] [--ms] [--before] [--after] [--keep] [--cont]   raw
  phone.py key power|volup|voldown|menu [--ms 80]
  phone.py abort                           lift everything
  phone.py reset fastboot                  sync, reset iOS; the phone comes back in LK fastboot (for the next flash)
  phone.py screenshot OUT.png [--scale 2] [--crop X,Y,W,H]   panel picture (scale 1 = 1080x2400, 2 = 540x1200 ...)
  phone.py logs [OUT] [--seconds 0] [--all]    new kernel-log bytes since the last call (state in ~/.cache/nord2-phone)
  phone.py home | wake | sleep | unlock    edge swipe up / power key if the panel is off / ... / wake + swipe up
  phone.py launch camera                   wake, unlock/home, Control Center, Camera button, dismiss the location question
  phone.py launch app X Y                  wake, unlock, home, tap the home screen icon at X,Y (read it from a screenshot)
  phone.py shutter [--key]                 tap Camera's shutter / record button (or volume-up)
  phone.py lens 0.5|1|flip                 Camera's zoom buttons / the front-back button
  phone.py mode video|photo                tap the neighbouring mode in the strip (from PHOTO / from VIDEO)
  phone.py focus X Y                       tap in the viewfinder
  phone.py scenario NAME|FILE OUTDIR [--wait-s N]   run a scripted sequence; screenshots, a log slice per step and steps.jsonl
                                           go to OUTDIR. Built in: photo, lenses, zoom, video (phone.py scenario list)

Coordinates are PANEL pixels, 1080x2400, origin top-left: exactly what a full-scale screenshot shows. With --pt they are
iOS points of the 375x812 iPhone X layout (pixel = point*3 - (22,18)); with --shot S they are pixels of a scale-S screenshot.
Only vendor IN control requests are sent. Nothing here touches fastboot/adb.

0640 (snap-20261114) kernels: screenshots work in the overlay modes (the plan on the panel composed in software on request;
regions the kernel cannot read are MAGENTA, flag "marked"); status asked with 128 bytes carries the real panel state (DCS display
off sent = dark), the brightness (DBV) and counters of the HID reports actually dispatched (injected touches / keys). Older
kernels: as before (panel state only from a screenshot).
"""
import argparse, ctypes as C, ctypes.util, json, math, os, struct, sys, time, zlib
from pathlib import Path

MAGIC = 0x5441324e
EXT_MAGIC = 0x534e0001                  # 0640: status words 20..31 present
W, H = 1080, 2400
OFFX, OFFY = 22, 18                     # logical 1125x2436 -> panel (SweetFramebuffer.cpp fOffX/fOffY)
KEYS = {'power': 0, 'volup': 1, 'voldown': 2, 'menu': 3}
STATE = Path(os.environ.get('NORD2_PHONE_STATE', Path.home() / '.cache/nord2-phone'))

lib = C.CDLL(ctypes.util.find_library('usb-1.0') or 'libusb-1.0.so.0')
ptr = C.c_void_p
for name, restype, argtypes in [
    ('init', C.c_int, [C.POINTER(ptr)]), ('exit', None, [ptr]),
    ('get_device_list', C.c_ssize_t, [ptr, C.POINTER(C.POINTER(ptr))]),
    ('free_device_list', None, [C.POINTER(ptr), C.c_int]),
    ('get_bus_number', C.c_uint8, [ptr]), ('get_device_address', C.c_uint8, [ptr]),
    ('open', C.c_int, [ptr, C.POINTER(ptr)]), ('close', None, [ptr]),
    ('control_transfer', C.c_int, [ptr, C.c_uint8, C.c_uint8, C.c_uint16, C.c_uint16, ptr, C.c_uint16, C.c_uint]),
]:
    f = getattr(lib, 'libusb_' + name); f.restype, f.argtypes = restype, argtypes


class Gone(Exception):
    pass


class Phone:
    def __init__(self, wait=0.0):
        self.ctx, self.handle = ptr(), ptr()
        if lib.libusb_init(C.byref(self.ctx)) != 0:
            raise SystemExit('libusb initialization failed')
        deadline = time.monotonic() + wait
        while True:
            if self._open():
                return
            if time.monotonic() >= deadline:
                raise Gone('"Nord2 XNU console" is not on USB (phone off, in fastboot/rescue, or iOS not up yet)')
            time.sleep(0.5)

    def _find(self):
        for node in Path('/sys/bus/usb/devices').iterdir():
            try:
                if (node / 'product').read_text().strip() == 'Nord2 XNU console' and \
                   (node / 'idVendor').read_text().strip() == '18d1' and (node / 'idProduct').read_text().strip() == 'd001':
                    return int((node / 'busnum').read_text()), int((node / 'devnum').read_text())
            except (OSError, ValueError):
                continue
        return None

    def _open(self):
        sel = self._find()
        if not sel:
            return False
        devs = C.POINTER(ptr)()
        n = lib.libusb_get_device_list(self.ctx, C.byref(devs))
        try:
            for i in range(max(n, 0)):
                if (lib.libusb_get_bus_number(devs[i]), lib.libusb_get_device_address(devs[i])) == sel:
                    for _ in range(50):
                        r = lib.libusb_open(devs[i], C.byref(self.handle))
                        if r != -3:
                            break
                        time.sleep(0.1)
                    return r == 0
        finally:
            lib.libusb_free_device_list(devs, 1)
        return False

    def close(self):
        if self.handle:
            lib.libusb_close(self.handle); self.handle = ptr()
        lib.libusb_exit(self.ctx)

    def req(self, code, value=0, index=0, size=64, timeout=1500, retries=4):
        buf = C.create_string_buffer(size)
        for attempt in range(retries + 1):
            n = lib.libusb_control_transfer(self.handle, 0xc0, code, value & 0xffff, index & 0xffff, buf, size, timeout)
            if n >= 0:
                return buf.raw[:n]
            if n == -4:
                raise Gone('the phone left USB')
            if n == -9 and code >= 0x70:                     # stall: not this kernel / not ready / bad argument
                raise OSError(f'request {code:#x} refused (stall)')
            if attempt == retries:
                raise OSError(f'USB request {code:#x} failed: libusb {n}')
            time.sleep(0.05)

    # ---- 0296
    def status(self):
        raw = self.req(0x70, size=128)
        if len(raw) not in (64, 80, 128):
            raise OSError('short status')
        v = struct.unpack('<32I', raw.ljust(128, b'\0'))       # version 1 kernels answer 16 words, 0296 v2 20, 0640 32
        if v[0] != MAGIC:
            raise OSError('not the autotest status')
        d = dict(version=v[1], busy=v[2], token=v[3], done=v[4], result=v[5], snap_seq=v[6], snap_ready=v[7],
                    snap_w=v[8], snap_h=v[9], snap_bytes=v[10], snap_scale=v[11] & 0xf, snap_packed=(v[11] >> 4) & 1, front_before=(v[11] >> 8) & 0xff,
                    front_after=(v[11] >> 16) & 0xff, panel_off=(v[11] >> 24) & 0xff, presents_during_snap=v[12],
                    gestures=v[13] & 0xffff, keys=v[13] >> 16, snaps=v[14] & 0xffff, refused=v[14] >> 16, down=v[15],
                    boot=v[16], log_rewinds=v[17], log_rewind_support={0: 'not tried', 1: 'yes', 2: 'no'}.get(v[18], v[18]),
                    snap_slots=(v[11] >> 8) & 0xff, snap_flags=(v[11] >> 16) & 0xff, snap_panel=(v[11] >> 24) & 0xff)
        d['ext'] = v[31] == EXT_MAGIC
        if d['ext']:
            f = v[20]
            nits = v[22] if v[22] < 0x80000000 else v[22] - (1 << 32)
            d['panel'] = dict(dark=bool(f & 2), display_off_requested=bool(f & 1), on=not (f & 3), overlay=bool(f & 4),
                              band_rotation=bool(f & 8), framebuffer=bool(f & 16), presents=v[23],
                              last_snapshot_flags=snap_flag_names((f >> 8) & 0xff), last_snapshot_ms=f >> 16)
            d['brightness'] = dict(dbv_sent=v[21] & 0xffff, dbv_target=v[21] >> 16, request_nits=None if nits < 0 else round(nits / 65536, 1),
                                   dbv_max=2047)
            d['hid'] = dict(injected_reports=v[24], injected_downs=v[25] & 0xffff, key_reports=v[25] >> 16, touch_reports=v[26],
                            touch_clients=v[27] & 0xffff, button_clients=v[27] >> 16, last_injected_xy=[v[28] & 0xffff, v[28] >> 16],
                            injected_without_client=v[29])
            d['panel_off'] = int(not d['panel']['on'])     # the real state (DCS / RequestPowerChange), not a picture's
        return d

    def reg(self, n, value):
        got = self.req(0x71, int(value), n, 4)
        if struct.unpack('<I', got)[0] != int(value) & 0xffff:
            raise OSError('register echo mismatch')

    def command(self, cmd, arg=0, timeout=30.0):
        """Queue one command and wait for the kernel thread to finish it. Result 1 = done."""
        t_end = time.monotonic() + timeout
        while True:
            ok, token = struct.unpack('<2I', self.req(0x72, cmd, arg, 8))
            if ok:
                break
            if time.monotonic() > t_end:
                raise OSError('kernel side stayed busy')
            time.sleep(0.01)
        while True:
            s = self.status()
            if s['done'] == token and not s['busy']:
                if s['result'] != 1:
                    raise OSError({2: 'bad argument', 3: 'aborted', 4: 'no picture (no framebuffer / no memory; kernels before 0640 have no overlay-mode screenshot)', 5: 'log grew', 6: 'log rewind not possible on this kernel',
                                   20: 'screenshot switched off (kern.n2snap=0)', 21: 'no overlay frame on the panel yet (boot) - try again', 22: 'no memory for the picture'}.get(s['result'], f"result {s['result']}"))
                return s
            if time.monotonic() > t_end:
                raise OSError('command did not finish')
            time.sleep(0.005)

    def gesture(self, paths, ms=0, before=30, after=0, keep=False, cont=False):
        assert 1 <= len(paths) <= 2
        for i, (x0, y0, x1, y1) in enumerate(paths):
            for j, v in enumerate((x0, y0, x1, y1)):
                lim = W if j % 2 == 0 else H
                self.reg(i * 4 + j, min(max(int(round(v)), 0), lim - 1))
        self.reg(8, min(int(ms), 65535)); self.reg(9, min(int(before), 65535)); self.reg(10, min(int(after), 65535))
        return self.command(1, len(paths) | (0x100 if keep else 0) | (0x200 if cont else 0), timeout=(ms + before + after) / 1000 + 10)

    def key(self, name, ms=80):
        self.reg(8, ms)
        return self.command(2, KEYS[name], timeout=ms / 1000 + 10)

    def abort(self):
        self.req(0x72, 4, 0, 8)

    def snapshot(self, scale=2, tries=3, crop=None, packed=True):
        """-> (width, height, rgb bytes, status). crop = (x, y, w, h) in panel pixels. Retries when two or more presents ran
        during the copy (possible tear). Version 2 kernels send runs of equal pixels packed (UI screens: several times less)."""
        v2 = self.status()['version'] >= 2
        if crop and not v2:
            raise OSError('this kernel (autotest version 1) cannot send a part of the panel')
        for i, val in enumerate(crop or (0, 0, 0, 0)):
            if v2: self.reg(11 + i, val)
        arg = scale | (16 if packed and v2 else 0)
        for attempt in range(tries):
            s = self.command(3, arg)
            if s['presents_during_snap'] < 2 or attempt == tries - 1:
                break
        total = s['snap_bytes']; data = bytearray()
        while len(data) < total:
            off = len(data)
            chunk = self.req(0x73, off & 0xffff, off >> 16, min(1024, total - off))
            if not chunk:
                raise OSError('picture read ended early')
            data += chunk
        s2 = self.status()
        if s2['snap_seq'] != s['snap_seq']:
            raise OSError('another picture was taken while this one was read')
        if s['snap_packed']:
            out = bytearray(); i = 0
            while i < total:
                c = data[i]; i += 1
                if c < 128:
                    out += data[i:i + 3 * (c + 1)]; i += 3 * (c + 1)
                else:
                    out += bytes(data[i:i + 3]) * (c - 126); i += 3
            data = out
        if len(data) != s['snap_w'] * s['snap_h'] * 3:
            raise OSError('picture size mismatch')
        s['transferred'] = total
        return s['snap_w'], s['snap_h'], bytes(data), s

    def log_rewind(self, size):
        """Start the kernel's 4 MiB live log again at 0 if it is still exactly `size` bytes. -> True if it did."""
        self.reg(15, size >> 16)
        try:
            self.command(6, size & 0xffff)
        except OSError as e:
            if 'log grew' in str(e): return False
            raise
        return self.log_info()['published'] < size          # a writer in mid-character puts the old size back: not rewound

    # ---- kernel log (protocol 2 of the console, as tools/nord2-usb-console.py)
    def log_info(self):
        st = struct.unpack('<7I', self.req(0x52, size=28))
        cap, published = struct.unpack('<2I', self.req(0x53, size=8))
        return dict(requests=st[2], resets=st[3], capacity=cap, published=published)

    def log_read(self, offset, limit):
        out = bytearray()
        while len(out) < limit:
            o = offset + len(out)
            chunk = self.req(0x54, o & 0xffff, o >> 16, min(1024, limit - len(out)))
            if not chunk:
                break
            out += chunk
        return bytes(out)


SNAP_FLAGS = {0x01: 'overlay-plan', 0x02: 'marked-magenta', 0x04: 'band-rotation', 0x08: 'yuv', 0x10: 'display-off-requested', 0x20: 'panel-dark'}


def snap_flag_names(f):
    return [n for b, n in SNAP_FLAGS.items() if f & b]


def png(path, w, h, rgb):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    stride = w * 3
    raw = b''.join(b'\0' + rgb[y * stride:(y + 1) * stride] for y in range(h))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def mean_luma(rgb):
    step = max(3, (len(rgb) // 3 // 4000) * 3)
    px = rgb[::step] if step % 3 else rgb[0::step]
    return sum(px) / max(len(px), 1)


THUMB = (36, 2008, 156, 156)             # Camera's last-picture thumbnail, panel pixels
# width of the yellow (selected) mode label in panel pixels, measured on the phone (run q10at4): PHOTO 133, VIDEO 120, PORTRAIT 197,
# PANO about 107 (white label seen at 722..828); SLO-MO / TIME-LAPSE not measured (red shutter, wider than VIDEO)
LABEL_W = {'photo': (125, 142), 'video': (112, 128)}


def cam_state(p):
    """What Camera shows: the selected mode from the yellow label of the mode strip and the colour of the shutter button."""
    w, h, rgb, _ = p.snapshot(1, crop=(0, 1880, 1080, 64))
    cols = [x for x in range(w) if sum(1 for y in range(0, h, 2) if rgb[(y * w + x) * 3] > 170 and rgb[(y * w + x) * 3 + 1] > 130 and rgb[(y * w + x) * 3 + 2] < 100) >= 2]
    w2, h2, b, _ = p.snapshot(4, crop=(480, 2020, 120, 120))
    n = w2 * h2; r_, g_, b_ = sum(b[0::3]) / n, sum(b[1::3]) / n, sum(b[2::3]) / n
    red = r_ > 120 and g_ < 110 and b_ < 110
    st = dict(label_x0=min(cols) if cols else None, label_x1=max(cols) if cols else None, label_width=(max(cols) - min(cols) + 1) if cols else 0,
              shutter_rgb=[round(r_), round(g_), round(b_)], shutter='red' if red else 'white' if min(r_, g_, b_) > 120 else 'other')
    lw = st['label_width']
    if not cols or st['shutter'] == 'other': st['mode'] = 'unknown'
    elif red: st['mode'] = 'video' if LABEL_W['video'][0] <= lw <= LABEL_W['video'][1] else 'left-of-video'
    else: st['mode'] = 'photo' if LABEL_W['photo'][0] <= lw <= LABEL_W['photo'][1] else 'right-of-photo'
    return st


SCENARIOS = {
    # one step per line: any phone.py verb, or  sleep S | shot NAME [--scale N] [--crop X,Y,W,H] | logs   ({wait} = --wait-s)
    'photo': """launch camera
sleep 3
logs
shot before --scale 4
shutter --watch {wait} --out {out}/{n}
shot after --scale 4
shot thumbnail --scale 1 --crop 36,2008,156,156
logs""",
    'lenses': """launch camera
sleep 3
logs
lens 1
sleep 3
shot wide --scale 4
shutter
sleep {wait}
logs
lens 0.5
sleep 4
shot ultrawide --scale 4
shutter
sleep {wait}
logs
lens 1
sleep 3
lens flip
sleep 5
shot front --scale 4
shutter
sleep {wait}
logs
lens flip
sleep 4
shot back-again --scale 4
logs""",
    'zoom': """launch camera
sleep 3
logs
shot z0 --scale 4
pinch 540 1000 200 800 --ms 700
sleep 2
shot zoomed-in --scale 4
logs
pinch 540 1000 800 200 --ms 700
sleep 2
shot zoomed-out --scale 4
focus 540 900
sleep 2
shot focus-tap --scale 4
logs""",
    'video': """launch camera
sleep 3
logs
mode video
sleep 4
shot video-mode --scale 4
shutter
sleep {wait}
shot recording --scale 4
shutter
sleep 3
shot stopped --scale 4
logs
mode photo
sleep 3
logs""",
}


def scenario(name, outdir, wait_s):
    """Each step is run as its own phone.py call (so a failing step does not end the run); everything is recorded."""
    import shlex, subprocess
    if name == 'list':
        for k, v in SCENARIOS.items(): print(f'--- {k}\n{v}')
        return 0
    text = SCENARIOS[name] if name in SCENARIOS else Path(name).read_text()
    out = Path(outdir); out.mkdir(parents=True, exist_ok=True)
    me = [sys.executable, str(Path(__file__).resolve())]
    failed = 0
    with (out / 'steps.jsonl').open('a') as rec:
        for n, line in enumerate([l.strip() for l in text.splitlines() if l.strip() and not l.strip().startswith('#')], 1):
            line = line.format(wait=wait_s, out=str(out), n=f'{n:02d}')
            w = shlex.split(line); t0 = time.time(); rc, said, made = 0, '', None
            if w[0] == 'sleep':
                time.sleep(float(w[1]))
            else:
                if w[0] == 'shot':
                    made = out / f'{n:02d}-{w[1]}.png'; cmd = me + ['screenshot', str(made)] + w[2:]
                elif w[0] == 'logs':
                    made = out / f'{n:02d}-log.txt'; cmd = me + ['logs', str(made)] + w[1:]
                else:
                    cmd = me + w
                r = subprocess.run(cmd, capture_output=True, text=True)
                rc, said = r.returncode, (r.stdout + r.stderr).strip()
                if w[0] == 'logs' and made.exists():
                    with (out / 'kernel.log').open('ab') as k: k.write(made.read_bytes())
                failed += rc != 0
            entry = dict(step=n, line=line, start=round(t0, 3), seconds=round(time.time() - t0, 2), rc=rc, output=said[-2000:],
                         file=str(made) if made and made.exists() else None)
            rec.write(json.dumps(entry) + '\n'); rec.flush()
            print(f"[{n:02d}] {line:<44} {'ok' if rc == 0 else 'FAILED'} {entry['seconds']:6.1f}s  {said.splitlines()[-1][:110] if said else ''}", flush=True)
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('verb')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--ms', type=int); ap.add_argument('--before', type=int, default=30); ap.add_argument('--after', type=int, default=0)
    ap.add_argument('--keep', action='store_true'); ap.add_argument('--cont', action='store_true')
    ap.add_argument('--angle', type=float, default=90.0, help='pinch axis in degrees (90 = vertical)')
    ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--crop', help='X,Y,W,H in panel pixels: only this part of the panel')
    ap.add_argument('--pt', action='store_true', help='coordinates are iOS points (375x812)')
    ap.add_argument('--shot', type=int, default=0, help='coordinates are pixels of a screenshot of this scale')
    ap.add_argument('--key', action='store_true'); ap.add_argument('--all', action='store_true')
    ap.add_argument('--rewind-at', type=int, default=1 << 20, help='logs: rewind the kernel buffer once this many bytes are read')
    ap.add_argument('--seconds', type=float, default=0); ap.add_argument('--wait', type=float, default=0)
    ap.add_argument('--wait-s', type=float, default=5, help='scenario: seconds to wait after a shutter press')
    ap.add_argument('--watch', type=float, default=0, help='shutter: watch the thumbnail for up to this many seconds')
    ap.add_argument('--out', help='shutter --watch: prefix for the two thumbnail pictures')
    a = ap.parse_args()

    def xy(x, y):
        x, y = float(x), float(y)
        if a.pt: return x * 3 - OFFX, y * 3 - OFFY
        if a.shot: return x * a.shot, y * a.shot
        return x, y

    def pt(x, y):            # built-in positions are given in points
        return x * 3 - OFFX, y * 3 - OFFY

    v, r = a.verb, a.args
    if v == 'scenario':
        if not r or (r[0] != 'list' and len(r) < 2): raise SystemExit('phone.py: scenario NAME|FILE OUTDIR   (scenario list)')
        raise SystemExit(scenario(r[0], r[1] if len(r) > 1 else '.', a.wait_s))
    try:
        p = Phone(wait=float(r[0]) if v == 'wait' and r else 240 if v == 'wait' else a.wait)
    except Gone as e:
        raise SystemExit(f'phone.py: {e}')
    try:
        def shot(path=None, scale=None, crop=None):
            w, h, rgb, s = p.snapshot(scale or a.scale, crop=crop)
            if path: png(path, w, h, rgb)
            return w, h, rgb, s

        def panel_dark():
            """True if the panel is off. 0640 kernels: the real state from the status (no picture needed); else a picture."""
            s = p.status()
            if s.get('ext'):
                return not s['panel']['on']
            w, h, rgb, s = shot(scale=8)
            return bool(s['panel_off']) or mean_luma(rgb) < 2

        def wait_panel(want_on, seconds=4.0):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if panel_dark() != want_on: return True
                time.sleep(0.25)
            return False

        def wake():
            if panel_dark():
                p.key('power')
                if p.status().get('ext'): wait_panel(True)
                time.sleep(1.0)
                return True
            return False

        def hid_now():
            s = p.status()
            return s.get('hid') if s.get('ext') else None

        def hid_delta(before):
            after = hid_now()
            if before is None or after is None: return {}
            return dict(injected_reports_delivered=after['injected_reports'] - before['injected_reports'],
                        injected_without_client=after['injected_without_client'] - before['injected_without_client'],
                        key_reports=after['key_reports'] - before['key_reports'], touch_clients=after['touch_clients'])

        if v in ('status', 'wait'):
            if v == 'wait':
                t = time.monotonic() + (float(r[0]) if r else 240)
                while True:
                    try: s = p.status(); break
                    except OSError:
                        if time.monotonic() > t: raise
                        time.sleep(0.5)
            else:
                s = p.status()
            s['log'] = p.log_info()
            print(json.dumps(s, indent=1))
        elif v == 'panel':
            s = p.status()
            if not s.get('ext'): raise SystemExit('phone.py: this kernel has no panel state in its status (0640 needed)')
            print(json.dumps(dict(panel=s['panel'], brightness=s['brightness'], hid=s['hid'], panel_off=s['panel_off']), indent=1))
        elif v == 'tap':
            h0 = hid_now(); x, y = xy(r[0], r[1]); p.gesture([(x, y, x, y)], 0, a.ms or 60, 0)
            if h0 is not None: print(json.dumps(dict(tap=[round(x), round(y)], **hid_delta(h0))))
        elif v == 'press':
            x, y = xy(r[0], r[1]); p.gesture([(x, y, x, y)], 0, a.ms or 800, 0)
        elif v in ('swipe', 'drag'):
            x0, y0 = xy(r[0], r[1]); x1, y1 = xy(r[2], r[3])
            p.gesture([(x0, y0, x1, y1)], a.ms or (300 if v == 'swipe' else 600), a.before, a.after or (250 if v == 'drag' else 0), a.keep, a.cont)
        elif v == 'pinch':
            cx, cy = xy(r[0], r[1]); d0, d1 = float(r[2]) / 2, float(r[3]) / 2
            if a.pt: d0, d1 = d0 * 3, d1 * 3
            if a.shot: d0, d1 = d0 * a.shot, d1 * a.shot
            ux, uy = math.cos(math.radians(a.angle)), math.sin(math.radians(a.angle))
            p.gesture([(cx - ux * d0, cy - uy * d0, cx - ux * d1, cy - uy * d1), (cx + ux * d0, cy + uy * d0, cx + ux * d1, cy + uy * d1)],
                      a.ms or 600, max(a.before, 60), a.after or 150, a.keep, a.cont)
        elif v == 'gesture':
            n = int(r[0]); c = [float(x) for x in r[1:]]
            assert len(c) == 4 * n
            paths = []
            for i in range(n):
                x0, y0 = xy(c[i * 4], c[i * 4 + 1]); x1, y1 = xy(c[i * 4 + 2], c[i * 4 + 3]); paths.append((x0, y0, x1, y1))
            p.gesture(paths, a.ms or 0, a.before, a.after, a.keep, a.cont)
        elif v == 'key':
            h0 = hid_now(); p.key(r[0], a.ms or 80)
            if h0 is not None:
                time.sleep(1.2 if r[0] == 'power' else 0.1)
                s = p.status()
                print(json.dumps(dict(key=r[0], **hid_delta(h0), panel_on=s['panel']['on'], panel_dark=s['panel']['dark'])))
        elif v == 'abort':
            p.abort()
        elif v == 'reset':
            # file systems synced, then a watchdog reset; the kernel arms the RTC fast-boot bit at every boot -> LK fastboot (~20 s)
            if r != ['fastboot']: raise SystemExit('phone.py: say "reset fastboot" (iOS is reset; the phone then waits in LK fastboot)')
            ok, token = struct.unpack('<2I', p.req(0x72, 5, 0x4642, 8))
            print('reset queued' if ok else 'refused (busy)')
        elif v == 'screenshot':
            w, h, rgb, s = shot(r[0], crop=tuple(int(x) for x in a.crop.split(',')) if a.crop else None)
            out = dict(file=r[0], width=w, height=h, scale=s['snap_scale'], bytes_over_usb=s['transferred'], panel_off=s['panel_off'],
                       presents_during_copy=s['presents_during_snap'], mean=round(mean_luma(rgb), 1))
            if s.get('ext'):
                out.update(slots=s['snap_slots'], flags=snap_flag_names(s['snap_flags']), panel_dark=s['panel']['dark'],
                           compose_ms=s['panel']['last_snapshot_ms'])
            print(json.dumps(out))
        elif v == 'logs':
            # The kernel keeps the first 4 MiB it prints after boot. Version 2 kernels can start that buffer again: once more than
            # --rewind-at bytes are read, it is rewound (only if nothing was printed since the last read, so nothing is lost).
            STATE.mkdir(parents=True, exist_ok=True)
            sf = STATE / 'log-offset.json'
            st0 = p.status()
            ident = dict(boot=st0['boot'], rewinds=st0['log_rewinds'], head=p.log_read(0, 64).hex() if st0['version'] < 2 else '')
            st = json.loads(sf.read_text()) if sf.exists() and not a.all else {}
            same = all(st.get(k) == ident[k] for k in ident)
            off = st.get('offset', 0) if same and st.get('offset', 0) <= p.log_info()['published'] else 0
            if st and not same and st.get('boot') == ident['boot'] and not a.all:
                print('phone.py: the log was rewound by someone else since the last call; reading from its start', file=sys.stderr)
            out = open(r[0], 'ab') if r else None
            end = time.monotonic() + a.seconds
            def save():
                sf.write_text(json.dumps(dict(offset=off, **ident)))
            while True:
                info = p.log_info()
                while off < info['published']:
                    data = p.log_read(off, info['published'] - off)
                    if not data: break
                    off += len(data)
                    (out or sys.stdout.buffer).write(data); (out or sys.stdout.buffer).flush()
                    save()
                if st0['version'] >= 2 and off >= a.rewind_at:
                    # the kernel only rewinds if nothing was printed since `off` (compare and swap), so try a few times
                    try:
                        for _ in range(40):
                            if p.log_rewind(off):
                                off = 0; ident['rewinds'] = p.status()['log_rewinds']; save(); break
                            data = p.log_read(off, p.log_info()['published'] - off)
                            off += len(data); (out or sys.stdout.buffer).write(data); (out or sys.stdout.buffer).flush(); save()
                    except OSError as e:
                        print(f'phone.py: {e}', file=sys.stderr); a.rewind_at = 1 << 30
                if time.monotonic() >= end: break
                time.sleep(0.25)
            save()
            if p.log_info()['published'] >= info['capacity']:
                print('phone.py: the kernel log buffer is full (4 MiB); lines printed since then are lost until a rewind/reboot', file=sys.stderr)
        elif v == 'wake':
            print('woke' if wake() else 'already on')
        elif v == 'sleep':
            if not panel_dark():
                p.key('power')
                print('sent power' + ('' if not p.status().get('ext') else ('; panel dark' if wait_panel(False) else '; panel still on after 4 s')))
            else:
                print('already dark')
        elif v == 'home':
            wake(); p.gesture([(540, 2392, 540, 1300)], 220, 20, 0)
        elif v == 'unlock':
            wake(); p.gesture([(540, 2392, 540, 1000)], 250, 20, 0)
        elif v == 'launch' and r and r[0] == 'app':
            # generic: wake, swipe up (unlock or leave the app), home again (first page), tap the icon at X,Y (panel pixels, or --pt/--shot)
            if len(r) < 3: raise SystemExit('phone.py: launch app X Y   (icon position from a screenshot)')
            h0 = hid_now(); wake()
            p.gesture([(540, 2392, 540, 1000)], 250, 20, 0); time.sleep(1.2)
            p.gesture([(540, 2392, 540, 1300)], 220, 20, 0); time.sleep(1.2)
            x, y = xy(r[1], r[2]); p.gesture([(x, y, x, y)], 0, 70, 0)
            print(json.dumps(dict(launch='sent', tap=[round(x), round(y)], **hid_delta(h0))))
        elif v == 'launch' and r and r[0] == 'camera':
            # Verified on hardware (q10at1): SpringBoard shows no Camera icon on this phone (MobileGestalt still-camera = 0) and the
            # lock screen's swipe-left does nothing, so: wake, swipe up (unlocks, or leaves the app in front), Control Center
            # (swipe down from the top-right corner), its Camera button. Then Camera's location question is answered "Cancel".
            wake()
            p.gesture([(540, 2392, 540, 900)], 250, 20, 0); time.sleep(1.5)
            p.gesture([(985, 24, 760, 1000)], 300, 30, 0); time.sleep(1.5)
            p.gesture([(164, 1468, 164, 1468)], 0, 60, 0); time.sleep(5.0)
            w, h, rgb, s = shot(scale=8)
            def row(y, x0, x1):
                return [sum(rgb[((y // 8) * w + x // 8) * 3:((y // 8) * w + x // 8) * 3 + 3]) / 3 for x in range(x0, x1, 8)]
            box, above = row(1030, 180, 900), row(940, 180, 900)
            dialog = max(box) - min(box) < 14 and 25 < sum(box) / len(box) < 90 and abs(sum(box) / len(box) - sum(above) / len(above)) > 8
            if dialog:
                p.gesture([(744, 1372, 744, 1372)], 0, 60, 0); time.sleep(1.0)
            print(json.dumps(dict(camera_launch='sent', location_dialog_dismissed=bool(dialog))))
        elif v == 'shutter':
            def press():
                if a.key: p.key('volup', a.ms or 120)
                else: p.gesture([(540, 2080, 540, 2080)], 0, a.ms or 70, 0)
            if a.watch <= 0:
                press()
            else:
                # the last-picture thumbnail (bottom left) before the press, then until it changes or --watch seconds are over
                def diff(x, y):
                    return sum(abs(i - j) for i, j in zip(x[::7], y[::7])) / max(len(x[::7]), 1)
                w0, h0, t0, _ = p.snapshot(1, crop=THUMB)
                noise = diff(t0, p.snapshot(1, crop=THUMB)[2])
                press(); start = time.monotonic(); changed = False; t1 = t0; peak = 0.0
                while time.monotonic() - start < a.watch:
                    time.sleep(0.4)
                    t1 = p.snapshot(1, crop=THUMB)[2]
                    d = diff(t0, t1); peak = max(peak, d)
                    if d > max(4.0, noise * 4):
                        changed = True; time.sleep(1.0); t1 = p.snapshot(1, crop=THUMB)[2]; break
                took = round(time.monotonic() - start, 1)
                if a.out:
                    png(a.out + '-thumb-before.png', w0, h0, t0); png(a.out + '-thumb-after.png', w0, h0, t1)
                print(json.dumps(dict(thumbnail_changed=changed, seconds=took, difference=round(diff(t0, t1), 1), peak=round(peak, 1),
                                      noise=round(noise, 1), thumb_mean=round(mean_luma(t1), 1))))
        elif v == 'lens':
            pos = {'0.5': (480, 1748), '.5': (480, 1748), '1': (600, 1748), 'front': (968, 2084), 'back': (968, 2084), 'flip': (968, 2084)}[r[0]]
            p.gesture([(pos[0], pos[1], pos[0], pos[1])], 0, 70, 0)
        elif v == 'mode':
            # The strip slides (the selected label is centred), so nothing is tapped by position: the mode in use is read from
            # the screen (cam_state) and changed one step at a time by a swipe over the viewfinder, until it is the one wanted.
            if r[0] == 'state':
                print(json.dumps(cam_state(p)))
            else:
                want = r[0]; assert want in ('video', 'photo')
                st = cam_state(p); moves = 0
                while st['mode'] != want and moves < 6 and st['mode'] != 'unknown':
                    go_right = st['mode'] in ('left-of-video',) or (want == 'photo' and st['mode'] == 'video')
                    p.gesture([(800, 1000, 280, 1000)] if go_right else [(280, 1000, 800, 1000)], 220, 30, 0)
                    moves += 1; time.sleep(3.0); st = cam_state(p)
                st['moves'] = moves; print(json.dumps(st))
                if st['mode'] != want: raise SystemExit(1)
        elif v == 'focus':
            x, y = xy(r[0], r[1]); p.gesture([(x, y, x, y)], 0, 70, 0)
        else:
            raise SystemExit(f'phone.py: unknown verb {v!r} (see --help)')
    except Gone as e:
        raise SystemExit(f'phone.py: {e}')
    except OSError as e:
        raise SystemExit(f'phone.py: {e}')
    finally:
        p.close()


if __name__ == '__main__':
    main()
