#!/usr/bin/env python3
"""gpu/hwcodec host tool for kernel patch 0298 (hardware JPEG encoder, autotest command 7).

  hwc.py run OUTDIR [--size WxH] [--q CODE] [--upto N] [--no-off]
        phone test: steps 0 (status), 1 (VEN power + clocks), 2 (larb7 + reset), 3 (encode), 4 (off), the kernel log saved
        after every step (OUTDIR/NN-stepS.log, OUTDIR/kernel.log), the JPEG fetched to OUTDIR/hw.jpg and checked.
        --upto 0 = read-only status only; --upto 1 = power only; ...  Stops at the first step that does not return 1.
  hwc.py step N OUTDIR [--size WxH] [--q CODE]     one step (after `run --upto`)
  hwc.py check FILE.jpg WxH                        no phone: structure, decode (ffmpeg), PSNR against the kernel's test picture,
                                                   and what ffmpeg's own JPEG encoder gives for the same picture
  hwc.py pattern WxH OUT.nv12                      no phone: the kernel's test picture (n2j_pattern), padded to 16 like the kernel

Quality codes (the hardware's): 4 Q30, 0 Q38, 0xe Q44, 0xa Q52, 1 Q60, 0xf Q72, 6 Q78, 2 Q85, 7 Q90 (default), 3 Q95, 0x10 Q97.
Results of a step: 1 done, 16 bad step, 17 mapping, 18 display domain off, 19 power status timeout, 20 SRAM ack timeout,
21 domain/clocks not on (run the earlier steps), 22 arguments, 23 encode timeout, 24 encode status (stall), 25 size,
26 bus protection not acknowledged, 27 clock gates, 28 larb port still translating, 29 venc_sel did not switch.
"""
import argparse, json, math, struct, subprocess, sys, tempfile, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
AUTOTEST = HERE.parents[1] / 'build/autotest'
RESULTS = {1: 'done', 16: 'bad step', 17: 'mapping failed', 18: 'display domain off', 19: 'VEN power status timeout', 20: 'SRAM ack timeout',
           21: 'domain/clocks not on', 22: 'bad arguments', 23: 'encode timeout', 24: 'encode status (stall)', 25: 'bad size',
           26: 'bus protection not acknowledged', 27: 'clock gates did not open', 28: 'larb port still translating', 29: 'venc_sel did not switch',
           2: 'this kernel has no command 7 (not a 0298 kernel)'}


def pattern(w, h):
    """n2j_pattern(): -> (stride, rows, bytes) NV12, stride x rows luma then stride x rows/2 interleaved Cb,Cr."""
    stride, rows = (w + 15) & ~15, (h + 15) & ~15
    out = bytearray(stride * rows * 3 // 2)
    for y in range(rows):
        yy = min(y, h - 1); o = y * stride
        for x in range(stride):
            xx = min(x, w - 1)
            out[o + x] = (16 + xx * 180 // w + (40 if ((xx >> 4) + (yy >> 4)) & 1 else 0) + ((xx * 7 + yy * 13) & 7)) & 0xff
    c = stride * rows
    for y in range(rows // 2):
        yy = min(2 * y, h - 1); o = c + y * stride
        for x in range(stride // 2):
            xx = min(2 * x, w - 1)
            out[o + 2 * x] = 78 + yy * 100 // h
            out[o + 2 * x + 1] = 78 + xx * 100 // w
    return stride, rows, bytes(out)


def structure(data):
    """Walk the markers. -> dict(ok, why, width, height, sampling, tables, markers)."""
    r = dict(ok=False, why='', markers=[], tables={})
    if data[:2] != b'\xff\xd8':
        r['why'] = 'no SOI'; return r
    i = 2
    while i + 4 <= len(data):
        if data[i] != 0xff:
            r['why'] = f'marker expected at {i}'; return r
        m = data[i + 1]; n = struct.unpack('>H', data[i + 2:i + 4])[0]; seg = data[i + 4:i + 2 + n]
        r['markers'].append(f'{m:02X}')
        if m == 0xdb:
            j = 0
            while j < len(seg):
                r['tables'][seg[j] & 15] = list(seg[j + 1:j + 65]); j += 65
        elif m in (0xc0, 0xc1, 0xc2):
            r['height'], r['width'] = struct.unpack('>HH', seg[1:5])
            r['sampling'] = [(seg[6 + 3 * k] & 15, seg[7 + 3 * k] >> 4, seg[7 + 3 * k] & 15, seg[8 + 3 * k]) for k in range(seg[5])]
            r['sof'] = m
        elif m == 0xda:
            t = data.rstrip(b'\x00'); r['pad'] = len(data) - len(t); r['ok'] = t[-2:] == b'\xff\xd9'; r['why'] = '' if r['ok'] else 'no EOI at the end'
            r['scan_bytes'] = len(data) - (i + 2 + n) - 2
            return r
        i += 2 + n
    r['why'] = 'no SOS'; return r


def psnr(a, b):
    se = sum((x - y) * (x - y) for x, y in zip(a, b))
    return 99.0 if se == 0 else 10 * math.log10(255 * 255 * len(a) / se)


def decode(path, w, h):
    """ffmpeg -> full-range planar 4:2:0 (y, u, v) of w x h."""
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-pix_fmt', 'yuvj420p', '-f', 'rawvideo', '-'], capture_output=True, check=True).stdout
    cw, ch = (w + 1) // 2, (h + 1) // 2
    if len(raw) != w * h + 2 * cw * ch:
        raise SystemExit(f'decoded size {len(raw)} is not {w}x{h} 4:2:0')
    return raw[:w * h], raw[w * h:w * h + cw * ch], raw[w * h + cw * ch:]


def planes(w, h):
    stride, rows, nv = pattern(w, h)
    y = b''.join(nv[r * stride:r * stride + w] for r in range(h))
    c = stride * rows
    u = b''.join(nv[c + r * stride:c + r * stride + w:2] for r in range(h // 2))
    v = b''.join(nv[c + r * stride + 1:c + r * stride + w:2] for r in range(h // 2))
    return y, u, v


def check(path, w, h):
    data = Path(path).read_bytes()
    s = structure(data)
    rep = dict(file=str(path), bytes=len(data), structure_ok=s['ok'], why=s['why'], markers=' '.join(s['markers']), width=s.get('width'), height=s.get('height'),
               sampling=s.get('sampling'), luma_q_first8=s['tables'].get(0, [])[:8])
    ok = s['ok'] and s.get('width') == w and s.get('height') == h
    if ok:
        try:
            y, u, v = decode(path, w, h)
            ry, ru, rv = planes(w, h)
            rep['psnr_y'], rep['psnr_cb'], rep['psnr_cr'] = round(psnr(y, ry), 2), round(psnr(u, ru), 2), round(psnr(v, rv), 2)
            ok = rep['psnr_y'] >= 30 and rep['psnr_cb'] >= 30 and rep['psnr_cr'] >= 30
        except (subprocess.CalledProcessError, SystemExit) as e:
            rep['decode_error'] = str(e); ok = False
        with tempfile.TemporaryDirectory() as t:                       # what a software encoder makes of the same picture
            ry, ru, rv = planes(w, h)
            (Path(t) / 'p.yuv').write_bytes(ry + ru + rv)
            r = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'yuvj420p', '-s', f'{w}x{h}', '-i', f'{t}/p.yuv', '-q:v', '3',
                                '-frames:v', '1', f'{t}/ref.jpg'], capture_output=True)
            if r.returncode == 0:
                y, u, v = decode(f'{t}/ref.jpg', w, h)
                rep['reference_ffmpeg_q3'] = dict(bytes=(Path(t) / 'ref.jpg').stat().st_size, psnr_y=round(psnr(y, ry), 2))
    rep['verdict'] = 'PASS: a valid JPEG of the test picture' if ok else 'FAIL'
    return ok, rep


def size_arg(s):
    w, h = s.lower().split('x'); return int(w), int(h)


def phone():
    sys.path.insert(0, str(AUTOTEST))
    import phone as ph
    return ph, ph.Phone(wait=5.0)


def run_step(ph, p, step, out, n, w, h, q):
    for i, v in enumerate((w, h, q)):
        p.reg(i, v)
    ok, token = struct.unpack('<2I', p.req(0x72, 7, step, 8))
    if not ok:
        raise SystemExit('command 7 refused: the kernel side is busy, or this is not a 0298 kernel')
    t_end = time.monotonic() + 20
    while True:
        s = p.status()
        if s['done'] == token and not s['busy']:
            break
        if time.monotonic() > t_end:
            raise SystemExit(f'step {step} did not finish in 20 s (kernel thread stuck?)')
        time.sleep(0.01)
    time.sleep(0.3)
    log = out / f'{n:02d}-step{step}.log'
    subprocess.run([sys.executable, str(AUTOTEST / 'phone.py'), 'logs', str(log), '--seconds', '3'], capture_output=True)
    lines = [l for l in log.read_text(errors='replace').splitlines() if 'NORD2 JPG' in l] if log.exists() else []
    if log.exists():
        with (out / 'kernel.log').open('ab') as k: k.write(log.read_bytes())
    print(f'step {step}: result {s["result"]} ({RESULTS.get(s["result"], "?")})')
    for l in lines: print('   ', l[-230:])
    return s


def fetch(p, s, path):
    total = s['snap_bytes']; data = b''
    if not s['snap_ready'] or not total:
        raise SystemExit('no JPEG on the picture channel')
    while len(data) < total:
        off = len(data)
        chunk = p.req(0x73, off & 0xffff, off >> 16, min(1024, total - off))
        if not chunk: raise SystemExit('read ended early')
        data += chunk
    Path(path).write_bytes(data)
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('verb', choices=['run', 'step', 'check', 'pattern']); ap.add_argument('args', nargs='*')
    ap.add_argument('--size', type=size_arg, default=(640, 480)); ap.add_argument('--q', type=lambda x: int(x, 0), default=7)
    ap.add_argument('--upto', type=int, default=4); ap.add_argument('--no-off', action='store_true')
    a = ap.parse_args()
    w, h = a.size
    if a.verb == 'pattern':
        w, h = size_arg(a.args[0]); Path(a.args[1]).write_bytes(pattern(w, h)[2]); return 0
    if a.verb == 'check':
        ok, rep = check(a.args[0], *size_arg(a.args[1])); print(json.dumps(rep, indent=1)); return 0 if ok else 1
    steps = [int(a.args[0])] if a.verb == 'step' else [s for s in (0, 1, 2, 3, 4) if s <= a.upto and not (s == 4 and a.no_off)]
    out = Path(a.args[1] if a.verb == 'step' else a.args[0]); out.mkdir(parents=True, exist_ok=True)
    ph, p = phone()
    n0 = len(list(out.glob('*-step*.log'))); rc = 0; verdict = None
    for n, step in enumerate(steps, n0):
        s = run_step(ph, p, step, out, n, w, h, a.q)
        if step == 3 and s['result'] == 1:
            fetch(p, s, out / 'hw.jpg')
            ok, verdict = check(out / 'hw.jpg', w, h)
            (out / 'check.json').write_text(json.dumps(verdict, indent=1) + '\n'); print(json.dumps(verdict, indent=1))
            rc |= 0 if ok else 1
        if s['result'] != 1:
            rc = 1
            if step != 4 and 4 in steps:
                print('stopping; step 4 (off) is still run to leave the domain as it was')
                run_step(ph, p, 4, out, n + 1, w, h, a.q)
            break
    p.close()
    return rc


if __name__ == '__main__':
    sys.exit(main())
