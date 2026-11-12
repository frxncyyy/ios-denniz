#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""opus-jpeg. PC-side check of an n2jpeg_phone run (never touches the phone).

  phone_check.py DIR [--inputs DIR2] [--log FILE] [--psnr-min 30] [--slack 5] [--dec-y-min 45] [--dec-rgb-min 35]

DIR is a fetch directory (vcodec_check.py fetch NAME -> gpu/build/apps-20261104/vcodec-fetch/NAME) with jpeg-selftest.log
and out/, or any directory holding jpeg-selftest.log or jpeg.log + out/ (the host test).  For every out/jpeg-WxH-qQ.jpg:
  * structure (own marker walk): SOI, APP0 "JFIF" first, NO APP1/Exif, DQT, SOF0 WxH 3 components 4:2:0 (0x22/0x11/0x11),
    DHT, SOS, EOI as the last two bytes, nothing after EOI;
  * djpeg decodes it with exit 0 and an empty stderr (libjpeg-turbo warnings fail);
  * PSNR Y/U/V (ffmpeg psnr filter, no colour conversion: yuvj420p vs yuvj420p) against the kernel test picture regenerated
    on the PC (n2jpeg_phone pattern, host build = the same integer formula as the phone);
  * a libjpeg reference: the same YCbCr picture through libjpeg-turbo (PIL, no colour conversion, 4:2:0) at the hardware
    code's nominal quality, PSNR too.  PASS = psnr_y >= --psnr-min and psnr_y >= libjpeg_psnr_y - --slack.
For every out/jdec-NAME-WxH.FMT.yuv (hardware decode, cropped planes): the source JPEG (out/NAME.jpg or --inputs/NAME.jpg)
is decoded by libjpeg-turbo (PIL draft YCbCr = libjpeg raw colour space, and djpeg -dct int -nosmooth -ppm for RGB):
  Y PSNR + max |diff| and RGB PSNR; PASS = Y >= --dec-y-min and RGB >= --dec-rgb-min.
Exit 0 when nothing failed.
"""
import argparse, glob, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(os.path.dirname(HERE))                # gpu/hwcodec
NOMINAL = [(97, 97), (95, 95), (90, 90), (85, 85), (78, 78), (72, 72), (60, 60), (52, 52), (44, 44), (38, 38), (0, 30)]
fails = []


def nominal(q):
    for lo, n in NOMINAL:
        if q >= lo:
            return n
    return 30


def host_tool(tmp):
    exe = os.path.join(tmp, 'n2jpeg_phone_host')
    if not os.path.exists(exe):
        cmd = ['cc', '-std=c11', '-O2', '-I', os.path.join(HW, 'include'), '-I', os.path.join(HW, 'jpeg'),
               os.path.join(HERE, 'n2jpeg_phone.c'), os.path.join(HW, 'jpeg', 'rwjpeg.c'),
               os.path.join(HW, 'jdec', 'n2jdec_parse.c'), os.path.join(HW, 'jdec', 'n2jdec_words.c'), '-o', exe]
        subprocess.run(cmd, check=True)
    return exe


def ref_yuv(tmp, w, h):
    """tight yuvj420p of the test picture"""
    out = os.path.join(tmp, 'ref-%dx%d.yuv' % (w, h))
    if os.path.exists(out):
        return out
    nv = os.path.join(tmp, 'ref-%dx%d.nv12' % (w, h))
    subprocess.run([host_tool(tmp), 'pattern', '%dx%d' % (w, h), nv], check=True, capture_output=True)
    d = open(nv, 'rb').read()
    st, rows = (w + 15) & ~15, (h + 15) & ~15
    y = b''.join(d[r * st:r * st + w] for r in range(h))
    c = d[st * rows:]
    cb = bytearray(); cr = bytearray()
    for r in range(h // 2):
        line = c[r * st:r * st + w]
        cb += line[0::2]; cr += line[1::2]
    open(out, 'wb').write(y + bytes(cb) + bytes(cr))
    os.remove(nv)
    return out


def psnr(ref, w, h, jpg):
    r = subprocess.run(['ffmpeg', '-hide_banner', '-v', 'info', '-f', 'rawvideo', '-pix_fmt', 'yuvj420p', '-s', '%dx%d' % (w, h),
                        '-i', ref, '-i', jpg, '-lavfi', '[1:v]format=yuvj420p[d];[0:v][d]psnr', '-f', 'null', '-'],
                       capture_output=True, text=True)
    m = re.search(r'PSNR y:([\d.inf]+) u:([\d.inf]+) v:([\d.inf]+)', r.stderr)
    if not m:
        return None
    return tuple(float(x) if x != 'inf' else 99.0 for x in m.groups())


def structure(b, w, h):
    if b[:2] != b'\xff\xd8':
        return 'no SOI'
    if b[-2:] != b'\xff\xd9':
        return 'last two bytes are not EOI'
    pos, seen, first = 2, [], None
    while pos + 4 <= len(b):
        if b[pos] != 0xff:
            return 'not a marker at %d' % pos
        m = b[pos + 1]
        if m == 0xff:
            pos += 1; continue
        ln = (b[pos + 2] << 8) | b[pos + 3]
        seg = b[pos + 4:pos + 2 + ln]
        if first is None:
            first = (m, seg[:5])
        seen.append(m)
        if m == 0xe1 and seg[:6] == b'Exif\x00\x00':
            return 'has an EXIF APP1'
        if m == 0xc0:
            hh, ww, nc = (seg[1] << 8) | seg[2], (seg[3] << 8) | seg[4], seg[5]
            if (ww, hh, nc) != (w, h, 3):
                return 'SOF0 %dx%d %d comps' % (ww, hh, nc)
            samp = [seg[6 + 3 * i + 1] for i in range(3)]
            if samp != [0x22, 0x11, 0x11]:
                return 'sampling %s' % samp
        if m in (0xc1, 0xc2, 0xc3):
            return 'SOF%d (not baseline)' % (m - 0xc0)
        if m == 0xda:
            break
        pos += 2 + ln
    if first is None or first[0] != 0xe0 or first[1] != b'JFIF\x00':
        return 'first segment is not APP0 JFIF (%s)' % (first,)
    for need, name in ((0xdb, 'DQT'), (0xc0, 'SOF0'), (0xc4, 'DHT'), (0xda, 'SOS')):
        if need not in seen:
            return 'no ' + name
    return None


def libjpeg_ref(ref, w, h, q, out):
    """libjpeg(-turbo) encode of the picture with NO colour conversion: PIL hands a YCbCr image to libjpeg as JCS_YCbCr;
    the chroma is nearest-upsampled and libjpeg's 2x2 box downsampling (4:2:0, subsampling=2) gives the original back."""
    from PIL import Image
    d = open(ref, 'rb').read(); cw, ch = w // 2, h // 2
    Y = Image.frombytes('L', (w, h), d[:w * h])
    U = Image.frombytes('L', (cw, ch), d[w * h:w * h + cw * ch]).resize((w, h), Image.NEAREST)
    V = Image.frombytes('L', (cw, ch), d[w * h + cw * ch:]).resize((w, h), Image.NEAREST)
    Image.merge('YCbCr', (Y, U, V)).save(out, 'JPEG', quality=q, subsampling=2)


def check_encode(f, tmp, a):
    m = re.match(r'jpeg-(\d+)x(\d+)-q(\d+)\.jpg$', os.path.basename(f))
    w, h, q = map(int, m.groups())
    b = open(f, 'rb').read()
    bad = structure(b, w, h)
    dj = subprocess.run(['djpeg', '-outfile', os.devnull, f], capture_output=True, text=True)
    if dj.returncode != 0 or dj.stderr.strip():
        bad = (bad + '; ' if bad else '') + 'djpeg rc=%d %s' % (dj.returncode, dj.stderr.strip()[:120])
    ref = ref_yuv(tmp, w, h)
    p = psnr(ref, w, h, f)
    cj = os.path.join(tmp, 'libjpeg-%dx%d-q%d.jpg' % (w, h, nominal(q)))
    if not os.path.exists(cj):
        libjpeg_ref(ref, w, h, nominal(q), cj)
    pc = psnr(ref, w, h, cj)
    ok = bad is None and p is not None and pc is not None and p[0] >= a.psnr_min and p[0] >= pc[0] - a.slack
    line = 'ENC %-4s %4dx%-4d q=%-3d bytes=%-8d bpp=%.3f psnr y/u/v=%s libjpeg(q%d) y/u/v=%s bytes=%d%s' % (
        'PASS' if ok else 'FAIL', w, h, q, len(b), len(b) * 8 / (w * h), '/'.join('%.2f' % x for x in p) if p else '?',
        nominal(q), '/'.join('%.2f' % x for x in pc) if pc else '?', os.path.getsize(cj), ' | ' + bad if bad else '')
    print(line)
    if not ok:
        fails.append(line)


def plane_psnr(a_bytes, b_bytes):
    from PIL import Image, ImageChops, ImageStat
    n = len(a_bytes)
    ia = Image.frombytes('L', (n, 1), a_bytes); ib = Image.frombytes('L', (n, 1), b_bytes)
    d = ImageChops.difference(ia, ib)
    mse = ImageStat.Stat(d).sum2[0] / n
    mx = d.getextrema()[1]
    return (99.0 if mse == 0 else 10 * __import__('math').log10(255 * 255 / mse)), mx


def check_decode(f, a, outdir):
    from PIL import Image, ImageChops, ImageStat
    import math
    m = re.match(r'jdec-(.+)-(\d+)x(\d+)\.(420p|422p|grey)\.yuv$', os.path.basename(f))
    name, w, h, fmt = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
    src = None
    for nm in (name, re.sub(r'-[EFG]\w+$', '', name)):     # jdec-NAME-STEP-WxH (RWJD steps F0..F6/G0/E2/E3/E4x) or jdec-NAME-WxH
        for d in [outdir] + ([a.inputs] if a.inputs else []):
            if src is None and os.path.exists(os.path.join(d, nm + '.jpg')):
                src = os.path.join(d, nm + '.jpg')
    if src is None:
        line = 'DEC FAIL %s: source %s.jpg not found (give --inputs)' % (os.path.basename(f), name)
        print(line); fails.append(line); return
    raw = open(f, 'rb').read()
    cw, ch = ((w + 1) // 2, (h + 1) // 2) if fmt == '420p' else ((w + 1) // 2, h) if fmt == '422p' else (0, 0)
    if len(raw) != w * h + 2 * cw * ch:
        line = 'DEC FAIL %s: %d bytes, want %d' % (os.path.basename(f), len(raw), w * h + 2 * cw * ch)
        print(line); fails.append(line); return
    im = Image.open(src); im.draft('YCbCr', im.size)
    yref = im.split()[0].tobytes() if im.mode == 'YCbCr' else im.convert('L').tobytes()
    py, my = plane_psnr(raw[:w * h], yref)
    Y = Image.frombytes('L', (w, h), raw[:w * h])
    if cw:
        U = Image.frombytes('L', (cw, ch), raw[w * h:w * h + cw * ch]).resize((w, h), Image.NEAREST)
        V = Image.frombytes('L', (cw, ch), raw[w * h + cw * ch:]).resize((w, h), Image.NEAREST)
        rgb = Image.merge('YCbCr', (Y, U, V)).convert('RGB')
    else:
        rgb = Y.convert('RGB')
    with tempfile.NamedTemporaryFile(suffix='.ppm') as t:
        subprocess.run(['djpeg', '-dct', 'int', '-nosmooth', '-ppm', '-outfile', t.name, src], check=True)
        ref = Image.open(t.name).convert('RGB')
        st = ImageStat.Stat(ImageChops.difference(rgb, ref))
        mse = sum(st.sum2) / (3 * w * h)
    prgb = 99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse)
    ok = py >= a.dec_y_min and prgb >= a.dec_rgb_min
    line = 'DEC %-4s %s %dx%d %s: Y vs libjpeg psnr=%.2f max|d|=%d, RGB vs djpeg -nosmooth psnr=%.2f' % (
        'PASS' if ok else 'FAIL', name, w, h, fmt, py, my, prgb)
    print(line)
    if not ok:
        fails.append(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir'); ap.add_argument('--inputs'); ap.add_argument('--log')
    ap.add_argument('--psnr-min', type=float, default=30.0); ap.add_argument('--slack', type=float, default=5.0)
    ap.add_argument('--dec-y-min', type=float, default=45.0); ap.add_argument('--dec-rgb-min', type=float, default=35.0)
    a = ap.parse_args()
    outdir = os.path.join(a.dir, 'out') if os.path.isdir(os.path.join(a.dir, 'out')) else a.dir
    log = a.log or next((os.path.join(a.dir, n) for n in ('jpeg-selftest.log', 'jpeg.log') if os.path.exists(os.path.join(a.dir, n))),
                        os.path.join(a.dir, 'jpeg-selftest.log'))
    if os.path.exists(log):
        lines = [l.rstrip('\n') for l in open(log, errors='replace')]
        done = [l for l in lines if 'N2JPGP DONE' in l]
        print('LOG', log, '|', done[-1] if done else 'NO DONE LINE (crash/kill/still running?)')
        for l in lines:
            if re.search(r'N2JPGP (FAIL|SKIP|NODEV)|N2JPG .*FAIL|N2JPG bench|N2JPGP info|N2JPGP sysctl|N2JPGP jetsam', l):
                print('  ' + l)
        if not done:
            fails.append('no DONE line')
        elif not re.search(r'fail=0\b', done[-1]):
            fails.append(done[-1])
    else:
        print('LOG', log, 'absent'); fails.append('no log')
    with tempfile.TemporaryDirectory(prefix='n2jpgp-check-') as tmp:
        for f in sorted(glob.glob(os.path.join(outdir, 'jpeg-*x*-q*.jpg'))):
            check_encode(f, tmp, a)
        for f in sorted(glob.glob(os.path.join(outdir, 'jdec-*.yuv'))):
            check_decode(f, a, outdir)
    print('JPEG_CHECK %s (%d failure%s)' % ('PASS' if not fails else 'FAIL', len(fails), '' if len(fails) == 1 else 's'))
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
