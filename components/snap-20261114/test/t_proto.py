#!/usr/bin/env python3
"""0640 protocol test: phone.py (the delivered copy) against the real nord2_autotest.inc + compositor (t_proto_lib.so).
Checks the status framing (80 -> 128 bytes, words 20..31), the snapshot command / 0x73 picture reads / packed decoding for scales and
crops (every picture must equal the full scale-1 composition sampled), refusal codes, and the high-level verbs that now read the
panel state from the status (wake, sleep, panel, tap with the HID delivery delta, screenshot JSON)."""
import ctypes as C, importlib.util, io, json, sys, contextlib
from pathlib import Path

here = Path(__file__).resolve().parent
lib = C.CDLL(str(here / 't_proto_lib.so'))
lib.nat_usb.argtypes = [C.c_char_p, C.c_char_p, C.c_size_t, C.POINTER(C.c_uint32)]
spec = importlib.util.spec_from_file_location('phone', sys.argv[1] if len(sys.argv) > 1 else str(here.parent / 'phone.py'))
phone = importlib.util.module_from_spec(spec); spec.loader.exec_module(phone)


class FakePhone(phone.Phone):
    def __init__(self, wait=0.0):
        self.handle = None; self.ctx = None

    def close(self):
        pass

    def req(self, code, value=0, index=0, size=64, timeout=1500, retries=4):
        setup = bytes([0xc0, code, value & 255, (value >> 8) & 255, index & 255, (index >> 8) & 255, size & 255, size >> 8])
        out = C.create_string_buffer(1024); n = C.c_uint32(0)
        r = lib.nat_usb(setup, out, 1024, C.byref(n))
        if r != 1:
            raise OSError(f'request {code:#x} refused (stall)')
        return out.raw[:n.value]


def check(c, what):
    if not c:
        print('FAIL:', what); sys.exit(1)


lib.t_setup(); lib.t_start()
W, H = 1080, 2400
full = C.create_string_buffer(W * H * 3); lib.t_full(full); full = full.raw
p = FakePhone()
s = p.status()
check(s['ext'] and s['panel']['on'] and s['brightness']['dbv_sent'] == 640 and s['brightness']['dbv_target'] == 700 and s['brightness']['request_nits'] == 120.0, 'status words 20..31')
old = p.req(0x70, size=80); check(len(old) == 80 and old[:4] == b'N2AT', 'old 80-byte status unchanged')
for scale, crop, packed in [(4, None, True), (2, None, False), (1, (0, 0, 1080, 130), True), (3, (500, 990, 300, 400), True), (8, None, True), (1, (1070, 2390, 50, 50), False)]:
    w, h, rgb, st = p.snapshot(scale, crop=crop, packed=packed)
    x0, y0, cw, ch = crop or (0, 0, W, H)
    cw, ch = min(cw, W - x0), min(ch, H - y0)
    check((w, h) == (cw // scale, ch // scale), f'size {scale} {crop}')
    exp = b''.join(full[((y0 + j * scale) * W + x0 + i * scale) * 3:((y0 + j * scale) * W + x0 + i * scale) * 3 + 3] for j in range(h) for i in range(w))
    check(rgb == exp, f'picture {scale} {crop} packed={packed}')
    check('band-rotation' in phone.snap_flag_names(st['snap_flags']) and st['snap_slots'] >= 4, 'flags/slots in status')
    print(f'ok picture scale {scale} crop {crop} packed {packed}: {w}x{h}, {st["transferred"]} bytes over the channel')
lib.t_refuse(21)
try:
    p.snapshot(4); check(False, 'refusal')
except OSError as e:
    check('no overlay frame' in str(e), 'refusal 21 message: ' + str(e))
lib.t_refuse(0)


def run(*argv):
    phone.Phone = FakePhone
    sys.argv = ['phone.py'] + list(argv)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        try:
            phone.main()
        except SystemExit as e:
            if e.code not in (None, 0): raise
    return buf.getvalue().strip()


o = json.loads(run('panel')); check(o['panel']['on'] and o['panel_off'] == 0, 'panel verb')
check(run('wake') == 'already on', 'wake when on')
o = run('sleep'); check('panel dark' in o and lib.t_dark() == 1, 'sleep -> power -> dark: ' + o)
check(json.loads(run('panel'))['panel']['dark'], 'panel dark after sleep')
check(run('wake') == 'woke' and lib.t_dark() == 0, 'wake from dark')
o = json.loads(run('tap', '300', '500')); check(o['injected_reports_delivered'] >= 2 and o['tap'] == [300, 500], 'tap delta ' + str(o))
o = json.loads(run('key', 'volup')); check(o['key_reports'] == 2, 'key delta ' + str(o))
o = json.loads(run('screenshot', str(here / 'shot.png'), '--scale', '8')); check(o['width'] == 135 and 'overlay-plan' in o['flags'] and o['panel_off'] == 0, 'screenshot json ' + str(o))
o = json.loads(run('launch', 'app', '200', '700')); check(o['launch'] == 'sent' and o['injected_reports_delivered'] >= 6, 'launch app ' + str(o))
check(run('home') == '', 'home')
(here / 'shot.png').unlink()
print('t_proto OK')
