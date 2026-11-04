#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""opus-ios. Reads the hardware-encoder test results on the build machine; never touches the phone.

  vcodec_check.py log KERNEL_LOG [--out DIR]
      the N2VCDT (selftest), N2VCD (vcodecd) and N2VCDP (probe) lines of a `phone.py logs` file: verdict per stage, the
      milestones, and the first access unit rebuilt from the probe's `hex` lines (-> DIR/first-au-from-log.h264, then probed
      with ffprobe).  This works from the live USB log alone, before any transaction.
  vcodec_check.py fetch NAME
      from the root file system MIRROR as the last transaction's snapshot left it (autocycle/hfsx.py, read-only):
      /private/var/db/nord2-vcodec/{*.log,selftest-*.txt,out/*} -> gpu/build/apps-20261104/vcodec-fetch/NAME/, then every
      .h264/.h265 is decoded with ffmpeg (frames, size, errors) and its first bytes compared with the log's hex if given;
      then `stats` runs on each stream (size/frames taken from the fetched probe.log's "start" line).
  vcodec_check.py stats STREAM [--size WxH] [--frames N] [--log probe.log] [--psnr-min DB]
      (opus-enc4k) per-frame sizes, picture types, key-frame interval, bit rate at the log's fps, and PSNR Y/U/V per frame
      against the probe's test pattern regenerated on the PC (gpu/hwcodec/ios/n2vcd_pattern_gen.c = the probe's own
      n2vcd_pattern.h, built with cc into /tmp).  Verdict line "STATS ... psnr_y min/avg=..": FAIL when a frame is missing,
      the decode errs, or the PSNR is below --psnr-min (default 30 dB).  The decoded stream's colour tags are cleared before
      the comparison: the vendor H.264 encoder writes BT.709 VUI for widths >= 1280 (BT.601 below and for HEVC), and ffmpeg's
      format negotiation then converted BT.709 -> BT.601 against the untagged reference, which read as ~27 dB (vc16..vc21).
"""
import argparse, glob, json, os, re, subprocess, sys, tempfile

A = os.path.dirname(os.path.abspath(__file__)); B = os.path.dirname(A)
HFSX = [sys.executable, os.path.join(B, 'autocycle', 'hfsx.py'), os.path.join(B, 'mirror', 'rootfs-mirror.img')]
DB = '/private/var/db/nord2-vcodec'


def probe_stream(path):
    info = subprocess.run(['ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0', '-show_entries',
                           'stream=codec_name,profile,width,height,nb_read_frames', '-of', 'csv=p=0', path],
                          capture_output=True, text=True)
    err = subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', path, '-f', 'null', '-'], capture_output=True, text=True)
    return info.stdout.strip() or info.stderr.strip()[:200], (err.stderr.strip().splitlines() or ['OK'])[0][:200]


HWC = os.path.join(B, '..', 'hwcodec', 'ios')
PGEN = '/tmp/n2hw-vcodec-check/n2vcd_pattern_gen'


def pattern_gen():
    src = os.path.join(HWC, 'n2vcd_pattern_gen.c')
    if not os.path.exists(PGEN) or os.path.getmtime(PGEN) < max(os.path.getmtime(src),
                                                                 os.path.getmtime(os.path.join(HWC, 'n2vcd_pattern.h'))):
        os.makedirs(os.path.dirname(PGEN), exist_ok=True)
        subprocess.run(['cc', '-O2', '-I', HWC, '-o', PGEN, src], check=True)
        subprocess.run([PGEN, '--selftest'], check=True, capture_output=True)
    return PGEN


def probe_start(log, index=None):
    """(w, h, frames, fps) from the last 'N2VCDP ... start ...' line of a probe log (the one of session `index` when the
    probe chained sessions with --then), or None"""
    best = None
    for l in open(log, errors='replace'):
        m = re.search(r' start (?:index=(\d+) )?(?:venc_fast=-?\d+ )?pid=\d+ codec=\w+ (\d+)x(\d+) frames=(\d+) fps=(\d+)', l)
        if m and (index is None or int(m.group(1) or 0) == index): best = tuple(int(x) for x in m.groups()[1:])
    return best


def probe_session_lines(log, index):
    """the probe's 'venc_fast', 'stats' and 'DONE' lines of session `index` (last occurrence: a retried session repeats)"""
    cur, out = None, {}
    for l in open(log, errors='replace'):
        m = re.search(r' start index=(\d+) ', l)
        if m: cur = int(m.group(1)); continue
        if cur != index: continue
        for key in ('venc_fast: ', ' stats ', ' DONE '):
            if key in l: out[key.strip(' :')] = l[l.index('N2VCDP'):].strip() if 'N2VCDP' in l else l.strip()
    return out


def cmd_stats(a):
    path = a.stream
    fm = re.search(r'probe-\d+-(\d+)-\w+-(\d+)x(\d+)\.h26[45]$', path)   # chained-probe name: index, size
    start = probe_start(a.log, int(fm.group(1)) if fm else None) if a.log and os.path.exists(a.log) else None
    if fm and not a.size: a.size = '%sx%s' % fm.group(2, 3)
    w, h = (int(x) for x in a.size.split('x')) if a.size else (start[:2] if start else (0, 0))
    fps = start[3] if start else 30
    fr = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                         'frame=key_frame,pict_type,pkt_size', '-of', 'json', path], capture_output=True, text=True)
    try: frs = json.loads(fr.stdout or '{}').get('frames', [])
    except ValueError: frs = []
    rows = [(int(d.get('key_frame', 0)), d.get('pict_type', '?'), int(d.get('pkt_size', 0))) for d in frs]
    keys = [i for i, r in enumerate(rows) if r[0]]
    gaps = [b - x for x, b in zip(keys, keys[1:])]
    sizes = [r[2] for r in rows]
    info = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                           'stream=codec_name,profile,level,width,height', '-of', 'csv=p=0', path], capture_output=True, text=True)
    print('  stream %s: %s' % (os.path.basename(path), info.stdout.strip()))
    if fm and a.log and os.path.exists(a.log):
        for k, v in sorted(probe_session_lines(a.log, int(fm.group(1))).items()):
            print('  probe %s: %s' % (k, re.sub(r'^N2VCDP [0-9.]+ ', '', v)[:240]))
    print('  frames=%d types=%s keys at %s (interval %s)' % (len(rows), ''.join(r[1] for r in rows)[:120], keys[:12],
                                                              sorted(set(gaps)) or '-'))
    if sizes:
        print('  bytes min/avg/max=%d/%d/%d first=%d; %.0f kbit/s at %d fps' % (min(sizes), sum(sizes) // len(sizes), max(sizes),
              sizes[0], sum(sizes) * 8.0 * fps / len(sizes) / 1000, fps))
        print('  per frame: ' + ' '.join(str(x) for x in sizes[:60]) + (' ...' if len(sizes) > 60 else ''))
    err = subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-xerror', '-i', path, '-f', 'null', '-'], capture_output=True, text=True)
    dec_ok = err.returncode == 0 and not err.stderr.strip()
    n = a.frames or (start[2] if start else len(rows))
    psnr = []
    if w and h and rows:
        n = min(n, len(rows))
        with tempfile.TemporaryDirectory() as td:
            st = os.path.join(td, 'psnr.txt')
            gen = subprocess.Popen([pattern_gen(), '%dx%d' % (w, h), str(n), '-'], stdout=subprocess.PIPE)
            r = subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', path, '-f', 'rawvideo', '-pix_fmt', 'nv12', '-s',
                                '%dx%d' % (w, h), '-i', 'pipe:0', '-lavfi',
                                '[0:v]setparams=colorspace=unknown:color_primaries=unknown:color_trc=unknown:range=tv,format=yuv420p[d];'
                                '[1:v]setparams=range=tv,format=yuv420p[r];[d][r]psnr=stats_file=' + st, '-frames:v', str(n),
                                '-f', 'null', '-'], stdin=gen.stdout, capture_output=True, text=True)
            gen.stdout.close(); gen.wait()
            if os.path.exists(st):
                for l in open(st):
                    m = dict(kv.split(':', 1) for kv in l.split() if ':' in kv)
                    psnr.append(tuple(float(m.get(k, 'nan').replace('inf', '99')) for k in ('psnr_y', 'psnr_u', 'psnr_v')))
            if r.returncode: print('  psnr: ffmpeg failed: %s' % r.stderr.strip()[:200])
    if psnr:
        y = [p[0] for p in psnr]
        print('  psnr_y per frame: ' + ' '.join('%.1f' % v for v in y[:60]))
        print('  psnr min/avg Y %.2f/%.2f U %.2f V %.2f dB over %d frames (reference: n2vcd_pattern.h %dx%d)' % (
              min(y), sum(y) / len(y), min(p[1] for p in psnr), min(p[2] for p in psnr), len(y), w, h))
    ok = dec_ok and rows and (not start or len(rows) == start[2]) and (not psnr or min(p[0] for p in psnr) >= a.psnr_min) and \
        (not (w and h) or bool(psnr))
    print('STATS %s %s frames=%d%s keys=%d decode=%s psnr_y min/avg=%s' % (
          'PASS' if ok else 'FAIL', os.path.basename(path), len(rows), '/%d' % start[2] if start else '', len(keys),
          'OK' if dec_ok else (err.stderr.strip().splitlines() or ['rc %d' % err.returncode])[0][:120],
          '%.2f/%.2f' % (min(p[0] for p in psnr), sum(p[0] for p in psnr) / len(psnr)) if psnr else 'n/a'))
    return 0 if ok else 1


def cmd_log(a):
    lines = open(a.log, errors='replace').read().splitlines()
    pick = lambda tag: [l[l.index(tag):] for l in lines if tag + ' ' in l]
    st, vd, pr = pick('N2VCDT'), pick('N2VCD'), pick('N2VCDP')
    vd = [l for l in vd if not l.startswith(('N2VCDT', 'N2VCDP'))]
    print('== selftest (N2VCDT): %d lines' % len(st))
    for l in st:
        if re.search(r' (T\d \w+ (PASS|FAIL|SKIP)|DONE|vm layout|T5 milestone|gate )', l): print('  ' + l[:260])
    print('== vcodecd (N2VCD): %d lines' % len(vd))
    for l in vd[:40]: print('  ' + l[:260])
    ms = sorted(set(re.findall(r'M-[a-f]', ' '.join(l for l in vd if 'milestone' in l))))
    print('  milestones named by vcodecd:', ms or 'none')
    print('== probe (N2VCDP): %d lines' % len(pr))
    for l in pr:
        if ' hex ' not in l: print('  ' + l[:260])
    hexes = []
    for l in pr:
        m = re.search(r' hex ([0-9a-f]{4}) ([0-9a-f]+)$', l.strip())
        if m: hexes.append((int(m.group(1), 16), bytes.fromhex(m.group(2))))
    if hexes:
        out = a.out or os.path.dirname(os.path.abspath(a.log))
        os.makedirs(out, exist_ok=True)
        data = b''.join(d for _, d in sorted(hexes))
        p = os.path.join(out, 'first-au-from-log.bin'); open(p, 'wb').write(data)
        nal = [data[i + 3] & 31 for i in range(len(data) - 3) if data[i:i + 3] == b'\x00\x00\x01']
        print('  first AU from the log: %d bytes -> %s; H.264 NAL types in it: %s; starts %s' %
              (len(data), p, nal, data[:8].hex()))
        print('  (an Annex B key frame starts 00000001 67 (SPS) ... 68 (PPS) ... 65 (IDR); HEVC 40 42 44 26)')
    done = [l for l in pr if ' DONE ' in l]
    print('VERDICT selftest=%s probe=%s' % ('PASS' if any('DONE pass=' in l and 'fail=0' in l for l in st) else ('FAIL' if st else 'absent'),
                                          ('PASS' if done and 'ok=1' in done[-1] else 'FAIL') if done else 'absent'))


def cmd_fetch(a):
    o = os.path.join(A, 'vcodec-fetch', a.name); os.makedirs(os.path.join(o, 'out'), exist_ok=True)
    ls = subprocess.run(HFSX + ['ls', DB], capture_output=True, text=True).stdout
    names = [l.split()[-1] for l in ls.splitlines() if l.strip() and not l.split()[-1].endswith('/') and l.split()[0].isdigit()]
    if names: subprocess.run(HFSX + ['get', DB, o] + names, capture_output=True)
    ls2 = subprocess.run(HFSX + ['ls', DB + '/out'], capture_output=True, text=True).stdout
    outs = [l.split()[-1] for l in ls2.splitlines() if l.strip() and l.split()[0].isdigit() and int(l.split()[0]) < 150_000_000]
    if outs: subprocess.run(HFSX + ['get', DB + '/out', os.path.join(o, 'out')] + outs, capture_output=True)
    for f in sorted(glob.glob(os.path.join(o, '*'))):
        if os.path.isfile(f): print('FILE %8d %s' % (os.path.getsize(f), os.path.relpath(f, o)))
    for f in sorted(glob.glob(os.path.join(o, 'out', '*.h26[45]'))):
        info, err = probe_stream(f)
        print('STREAM %d %s | %s | decode: %s | first bytes %s' % (os.path.getsize(f), os.path.relpath(f, o), info, err,
                                                                 open(f, 'rb').read(16).hex()))
        lg = os.path.join(o, 'probe.log')
        cmd_stats(argparse.Namespace(stream=f, size=None, frames=None, log=lg if os.path.exists(lg) else None, psnr_min=30.0))
    print('kept in', o)


ap = argparse.ArgumentParser(); sp = ap.add_subparsers(dest='cmd', required=True)
p = sp.add_parser('log'); p.add_argument('log'); p.add_argument('--out')
p = sp.add_parser('fetch'); p.add_argument('name')
p = sp.add_parser('stats'); p.add_argument('stream'); p.add_argument('--size'); p.add_argument('--frames', type=int)
p.add_argument('--log'); p.add_argument('--psnr-min', type=float, default=30.0)
a = ap.parse_args(); sys.exit({'log': cmd_log, 'fetch': cmd_fetch, 'stats': cmd_stats}[a.cmd](a) or 0)
