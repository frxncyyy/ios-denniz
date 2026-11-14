#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""cam_check.py -- host-side verdicts for the Camera / Photos hardware-codec test (opus-cam, 12 Nov 2026).

    cam_check.py log LIVE.LOG [--since TEXT]   summarise the plug-in (N2VIDEO), decoder (N2VDEC) and daemon (N2VCD) lines of a
                                               phone.py / logtail capture: which encoder each VideoToolbox session got, the
                                               first-picture parameters Camera asked for, per-session fps, restarts, errors
    cam_check.py movie FILE.MOV [...]          ffprobe + bit-stream facts of a recorded clip: codec/profile/level, size,
                                               frames, duration, fps, slices per picture and SPS VUI -> which encoder made it
                                               (gpu/video's software n2avc writes 4 slices per picture, Constrained Baseline;
                                               the MediaTek VENC writes 1 slice per picture), compared with the 21 fps baseline

Reads files only; never talks to the phone.  Needs ffprobe/ffmpeg for `movie`.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

BASELINE_FPS = 21.0     # user6 (11 Nov): software n2avc, 1080p, 814 pictures in 38.7 s


def cmd_log(a):
    text = open(a.log, 'rb').read().decode('utf-8', 'replace')
    if a.since and a.since in text:
        text = text[text.rindex(a.since):]
    lines = text.splitlines()
    pick = lambda rx: [l for l in lines if re.search(rx, l)]
    out = []
    reg = pick(r'N2VIDEO .*(registered|hardware encoders)')
    out.append('registration (Slim.videoencoder / Slim.videodecoder):')
    out += ['  ' + l.strip() for l in reg[-6:]] or ['  (none: plug-in not loaded in this capture, or lines suppressed)']
    hw_open = pick(r'n2hwenc: .*hardware session open')
    hw_fail = pick(r'n2hwenc: .*falls back to the next encoder')
    hw_first = pick(r'n2hwenc: first picture')
    hw_end = pick(r'n2hwenc: session \d+x\d+ ended')
    hw_err = pick(r'n2hwenc: .*(fatal|failed|refused|no answer)')
    sw_start = pick(r'n2avc: session \d+x\d+ started')
    sw_end = pick(r'n2avc: session \d+x\d+ ended')
    out.append('encoder sessions: hardware opened %d, hardware skipped (fallback) %d, software n2avc started %d'
               % (len(hw_open), len(hw_fail), len(sw_start)))
    for l in hw_open + hw_fail + hw_first + hw_end + hw_err + sw_start + sw_end:
        out.append('  ' + l.strip()[:400])
    fps = []
    for l in hw_end:
        m = re.search(r'ended: (\d+) pictures .* \(([\d.]+) fps submitted\)', l)
        if m and int(m.group(1)) > 30:
            fps.append(float(m.group(2)))
    dec = pick(r'N2VDEC ')
    out.append('decoder plug-in lines: %d' % len(dec))
    out += ['  ' + l.strip()[:300] for l in dec[-12:]]
    d = pick(r'N2VCD ')
    keys = [l for l in d if re.search(r'start dev=|serving|jetsam|session \d+ (open|close)|dec: session|exiting 75|'
                                      r'restart|timeout|failed|guest|stopping|exit', l)]
    out.append('vcodecd lines: %d (shown: lifecycle, sessions, errors)' % len(d))
    out += ['  ' + l.strip()[:300] for l in keys[-40:]]
    restarts = len([l for l in d if 'exiting 75' in l])
    timeouts = len([l for l in d if 'hardware timeout' in l or 'slow encode' in l])
    print('\n'.join(out))
    print('VERDICT hw_encoder_used=%s software_fallbacks=%d hw_errors=%d decoder_used=%s daemon_restarts=%d '
          'daemon_timeouts=%d hw_fps=%s (software baseline %.0f fps)'
          % ('yes' if hw_open and hw_first else 'no', len(hw_fail) + len(sw_start), len(hw_err),
             'yes' if any('session start' in l for l in dec) else 'no', restarts, timeouts,
             ','.join('%.1f' % f for f in fps) or '-', BASELINE_FPS))
    return 0


def nal_units(data):
    i, n = 0, len(data)
    starts = []
    while i + 3 <= n:
        if data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 1:
            starts.append(i + 3)
            i += 3
        else:
            i += 1
    for k, s in enumerate(starts):
        e = starts[k + 1] - 3 if k + 1 < len(starts) else n
        while e > s and data[e - 1] == 0:
            e -= 1
        yield data[s:e]


def cmd_movie(a):
    rc = 0
    for f in a.files:
        info = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-count_frames', '-show_entries',
                               'stream=codec_name,profile,level,width,height,nb_read_frames,avg_frame_rate,r_frame_rate,'
                               'duration,bit_rate,pix_fmt,color_range,color_space', '-of', 'default=nw=1', f],
                              capture_output=True, text=True).stdout
        kv = dict(l.split('=', 1) for l in info.splitlines() if '=' in l)
        frames = int(kv.get('nb_read_frames', '0') or 0)
        dur = float(kv.get('duration', '0') or 0)
        fps = frames / dur if dur > 0 else 0.0
        codec = kv.get('codec_name', '?')
        slices = []
        ext = {'h264': ('h264_mp4toannexb', 'h264'), 'hevc': ('hevc_mp4toannexb', 'hevc')}.get(codec)
        if ext:
            with tempfile.TemporaryDirectory() as td:
                es = os.path.join(td, 'v.es')
                subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', f, '-map', '0:v:0', '-c', 'copy', '-bsf:v',
                                ext[0], '-frames:v', '60', '-f', ext[1], es], capture_output=True)
                data = open(es, 'rb').read() if os.path.exists(es) else b''
            per, cur = [], 0
            for nal in nal_units(data):
                if not nal:
                    continue
                t = (nal[0] & 0x1f) if codec == 'h264' else ((nal[0] >> 1) & 0x3f)
                is_slice = t in (1, 5) if codec == 'h264' else t <= 31
                first = (nal[1] & 0x80) != 0 if codec == 'h264' and len(nal) > 1 else \
                        ((nal[2] & 0x80) != 0 if len(nal) > 2 else True)
                if is_slice and first and cur:
                    per.append(cur)
                    cur = 0
                if is_slice:
                    cur += 1
            if cur:
                per.append(cur)
            slices = per
        spp = (sum(slices) / len(slices)) if slices else 0
        made_by = ('software n2avc (4 slices/picture)' if round(spp) == 4 else
                   'hardware VENC (1 slice/picture)' if round(spp) == 1 else 'unknown (%.1f slices/picture)' % spp)
        decode_err = subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', f, '-map', '0:v:0', '-f', 'null', '-'],
                                    capture_output=True, text=True).stderr.strip().splitlines()
        decode_err = [l for l in decode_err if 'non monotonically increasing dts' not in l]   # null-muxer timestamp noise
        print('MOVIE %s' % f)
        print('  %s %s level %s %sx%s %s range=%s space=%s, %d frames in %.2f s = %.2f fps (avg_frame_rate %s), %s bit/s'
              % (codec, kv.get('profile'), kv.get('level'), kv.get('width'), kv.get('height'), kv.get('pix_fmt'),
                 kv.get('color_range'), kv.get('color_space'), frames, dur, fps, kv.get('avg_frame_rate'),
                 kv.get('bit_rate')))
        print('  encoder: %s; host decode: %s' % (made_by, 'OK' if not decode_err else '; '.join(decode_err[:3])))
        print('VERDICT movie=%s encoder=%s fps=%.2f vs_software_baseline=%+.1f decode=%s'
              % (os.path.basename(f), 'hw' if round(spp) == 1 else 'sw' if round(spp) == 4 else '?', fps,
                 fps - BASELINE_FPS, 'ok' if not decode_err else 'errors'))
        if decode_err or frames == 0:
            rc = 1
    return rc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    p = sp.add_parser('log')
    p.add_argument('log')
    p.add_argument('--since', help='only the text after the last occurrence of this marker')
    p = sp.add_parser('movie')
    p.add_argument('files', nargs='+')
    a = ap.parse_args()
    return {'log': cmd_log, 'movie': cmd_movie}[a.cmd](a)


if __name__ == '__main__':
    sys.exit(main())
