#!/usr/bin/env python3
"""camflash_check.py (camflash-20261114): verdicts for the q10jcr camera / flash test from a kernel log (phone.py scenario OUTDIR/kernel.log,
or a txn capture runs/TAG/ui-usb/xnu-live.log).  Streams the file (no full load).

  camflash_check.py LOG [--expect-flash N] [--json]

Sections: kernel (0560 strobe announced, torch/strobe writes, I2C failures), camera (readers' frame rate, stalls, faults), photographs
(StillImageCaptureNow asked, pictures given, Delivering still image err:0), flash (sequence begun/done, PreFlashComplete/FlashFired posted,
flash frames with the kernel's LED times, partial / timeouts, levels), video (hardware encoder sessions), crashes (mediaserverd / Camera).
Exit 0 = every expected item present; 1 = something missing (listed under "missing").
"""
import json, re, sys

pats = {
    'strobe_made': r'NORD2 TORCH STROBE: 0560 camera flash: strobe on',
    'torch_made': r'NORD2 TORCH: /dev/redwood-torch made',
    'torch_on': r'NORD2 TORCH: on, level (\d+)/1000',
    'strobe_fired': r'NORD2 TORCH: strobe (\d+) ms, level (\d+)/1000 -> ISTRB (\d+) \((\d+) mA x 2\)',
    'torch_off': r'NORD2 TORCH: off \(([^)]*)\) FLED_STAT (\S+) (\S+)',
    'torch_fail': r'NORD2 TORCH: .*(FAILED|torch disabled|thermal level [12])',
    'reader_rate': r'redwood: camera: core: node \d camera (\d): 5\.0 s: (\d+) frames read \(([\d.]+) a second\)',
    'reader_gave_up': r'the reader gave up',
    'stall': r'sessions started anew for a stall\)|stall',
    'still_asked': r'StillImageCaptureNow (\d+): StillImage pictures now given',
    'still_flash': r'StillImageCaptureNow (\d+) WITH THE FLASH( \(forced[^)]*\)| \(ForceFlashOn\))?: (.*)',
    'flashmode': r'stream: ([A-Za-z -]+): FlashMode (\d) -> (\d)',
    'still_picture': r'StillImage: picture (\d+) of the photograph asked for',
    'delivered': r'Delivering still image with processing flags: \d+ .*dimensions: (\d+x\d+) err:(-?\d+)',
    'fl_begin': r'core: flash: camera (\d) photograph (\d+) begins at frame (\d+): ambient ([\d.]+) ms x ([\d.]+), level ([\d.]+)',
    'fl_pre': r'core: flash: pre-flash done at frame (\d+) after (\d+) lit frames: .* flash exposure ([\d.]+) ms x ([\d.]+), strobe (\d+) ms',
    'fl_frame': r'core: flash: frame (\d+) is the photograph\'s (FLASH|ambient) frame: ([\d.]+) ms x ([\d.]+), level ([\d.]+), .*\(strobe ([\d.]+) ms long, times (from the kernel|estimated)',
    'fl_done': r'core: flash: camera (\d) photograph done( PARTIAL[^:]*)?( TIMED OUT)?: (\d+) flash frame\(s\), level ambient ([\d.]+) pre-flash ([\d.]+) flash ([\d.]+)',
    'fl_refused': r'core: flash: (the kernel refused the strobe request|no /dev/redwood-torch|.*NOT TAKEN)',
    'posted': r'stream: [A-Za-z -]+: posted (PreFlashComplete|FlashFired) \(camera \d\): (-?\d+)',
    'notposted': r'(PreFlashComplete|FlashFired) (\(camera \d\) not posted: a forced flash|NOT posted)',
    'fl_over': r'the flash photograph is over: (\d+) flash picture\(s\) given in all, (\d+) asked',
    'video_session': r'session (\d+x\d+) ended: (\d+) pictures .* (\d+) failures',
    'hwenc_open': r'hardware session open',
    'crash': r'(CRASH|crashed|SIGSEGV|EXC_BAD_ACCESS).{0,80}(mediaserverd|Camera)|(mediaserverd|Camera).{0,80}(CRASH|crashed|SIGSEGV)',
    'jetsam': r'jetsam.{0,60}(mediaserverd|Camera)',
    'panic': r'panic\(',
}
rx = {k: re.compile(v) for k, v in pats.items()}


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); return 2
    path = a[0]; want_flash = int(a[a.index('--expect-flash') + 1]) if '--expect-flash' in a else 0
    hits = {k: [] for k in pats}
    with open(path, 'rb') as f:
        for raw in f:
            line = raw.decode('utf-8', 'replace')
            for k, r in rx.items():
                m = r.search(line)
                if m and len(hits[k]) < 400:
                    hits[k].append(m.groups() if m.groups() else (line.strip()[:160],))
    rates = [float(g[2]) for g in hits['reader_rate']]
    v = {
        'kernel': {'0560_strobe_announced': bool(hits['strobe_made']), 'torch_device': bool(hits['torch_made']),
                   'torch_on_writes': len(hits['torch_on']), 'strobes': [dict(ms=int(g[0]), istrb=int(g[2]), mA_x2=int(g[3])) for g in hits['strobe_fired']][:10],
                   'offs': [g[0] for g in hits['torch_off']][:12], 'failures_or_thermal': [g[0] for g in hits['torch_fail']][:5]},
        'camera': {'reader_5s_lines': len(rates), 'fps_min': min(rates) if rates else None, 'fps_max': max(rates) if rates else None,
                   'fps_median': sorted(rates)[len(rates) // 2] if rates else None, 'reader_gave_up': len(hits['reader_gave_up'])},
        'photographs': {'asked': len(hits['still_asked']), 'pictures_given': len(hits['still_picture']),
                        'delivered_err0': sum(1 for g in hits['delivered'] if g[1] == '0'), 'delivered_err': [g for g in hits['delivered'] if g[1] != '0'][:5],
                        'dims': sorted({g[0] for g in hits['delivered']})},
        'flash': {'flashmode_changes': [f'{g[0]}:{g[1]}->{g[2]}' for g in hits['flashmode']][:10], 'asked_with_flash': len(hits['still_flash']),
                  'forced': sum(1 for g in hits['still_flash'] if g[1] and 'forced' in g[1]),
                  'begun': len(hits['fl_begin']), 'pre_done': [dict(lit=int(g[1]), exp_ms=float(g[2]), gain=float(g[3]), strobe_ms=int(g[4])) for g in hits['fl_pre']][:6],
                  'flash_frames': [dict(kind=g[1], exp_ms=float(g[2]), gain=float(g[3]), level=float(g[4]), strobe_ms=float(g[5]), times=g[6]) for g in hits['fl_frame']][:10],
                  'done': len(hits['fl_done']), 'partial': sum(1 for g in hits['fl_done'] if g[1]), 'timed_out': sum(1 for g in hits['fl_done'] if g[2]),
                  'levels': [dict(ambient=float(g[4]), pre=float(g[5]), flash=float(g[6])) for g in hits['fl_done']][:6],
                  'posted': [f'{g[0]}={g[1]}' for g in hits['posted']][:12], 'not_posted': [g[0] for g in hits['notposted']][:6],
                  'refused': [g[0] for g in hits['fl_refused']][:4], 'over': [f'{g[0]}/{g[1]}' for g in hits['fl_over']][:6]},
        'video': {'sessions': [dict(size=g[0], pictures=int(g[1]), failures=int(g[2])) for g in hits['video_session']][:6], 'hw_open': len(hits['hwenc_open'])},
        'trouble': {'crash': len(hits['crash']), 'jetsam': len(hits['jetsam']), 'panic': len(hits['panic']), 'samples': (hits['crash'] + hits['jetsam'])[:4]},
    }
    miss = []
    if not v['kernel']['0560_strobe_announced']: miss.append('0560 strobe line (kernel is not q10jcr?)')
    if not rates: miss.append('camera reader 5 s lines (camera never streamed)')
    if v['photographs']['asked'] and v['photographs']['delivered_err0'] < v['photographs']['asked']: miss.append('a photograph asked without Delivering still image err:0')
    if want_flash:
        f = v['flash']
        if f['begun'] < want_flash: miss.append(f'flash sequences begun {f["begun"]} < {want_flash}')
        if len(v['kernel']['strobes']) < want_flash: miss.append('kernel strobe lines (NORD2 TORCH: strobe ...)')
        if f['done'] < f['begun']: miss.append('flash sequence not done')
        if f['partial'] or f['timed_out']: miss.append('flash partial / timed out')
        if not any(x['kind'] == 'FLASH' and x['times'] == 'from the kernel' for x in f['flash_frames']): miss.append('a FLASH frame with the kernel LED times')
        if not f['forced'] and not any(p.startswith('FlashFired=0') for p in f['posted']): miss.append('FlashFired posted (ForceFlashOn path)')
    if v['trouble']['crash'] or v['trouble']['panic']: miss.append('crash/panic lines')
    v['missing'] = miss
    v['verdict'] = 'OK' if not miss else 'CHECK'
    print(json.dumps(v, indent=1))
    return 0 if not miss else 1


if __name__ == '__main__':
    sys.exit(main())
