#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""hevc_cam_check.py -- host verdicts for Camera HEVC (hvc1) video on the hardware encoder (hevccam, 12 Nov 2026).

    hevc_cam_check.py log LIVE.LOG [--since TEXT]   plug-in (N2VIDEO n2hwenc), daemon (N2VCD) and camera-diag lines: did the
                                                    Camera's 1080p session go to the HARDWARE hvc1 object, the first picture's
                                                    parameters (ProfileLevel HEVC_..., bitrate, fps, gop, realtime), the end
                                                    line (pictures, failures, fps), restarts (exiting 75), fallbacks
                                                    (falls back / VCPHEVC / n2avc), fatal errors, the source-attribute line
    hevc_cam_check.py movie FILE.MOV [...]          ffprobe + bitstream: codec hevc, codec tag hvc1 (not hev1), profile Main,
                                                    level, size, frames, duration, fps, B-frames (none expected: DTS = PTS),
                                                    key frames, parameter sets in the sample entry and NOT repeated in the
                                                    samples (hvc1), slices per picture (VENC = 1), full host decode
    hevc_cam_check.py both LIVE.LOG FILE.MOV        both, one final VERDICT line

Reads files only; never talks to the phone. Needs ffprobe/ffmpeg for `movie`.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile


def cmd_log(a, quiet=False):
    text = open(a.log, 'rb').read().decode('utf-8', 'replace')
    if a.since and a.since in text:
        text = text[text.rindex(a.since):]
    lines = text.splitlines()
    pick = lambda rx: [l.strip() for l in lines if re.search(rx, l)]
    hevc_open = pick(r'n2hwenc: hevc \d+x\d+: hardware session open')
    avc_open = pick(r'n2hwenc: avc \d+x\d+: hardware session open')
    hevc_fail = pick(r'n2hwenc: hevc \d+x\d+: .*falls back')
    first = pick(r'n2hwenc: first picture')
    hevc_first = [l for l in first if re.search(r'\): hevc,', l)]
    ended = pick(r'n2hwenc: session \d+x\d+ ended')
    fatal = [l for l in pick(r'n2hwenc: .*(fatal|failed|refused|no answer|malfunction)')
             if not re.search(r'n2hwenc: session \d+x\d+ ended:', l) or not re.search(r' 0 failures', l)]   # (end line: 'ENC_SET refused 0')
    sw = pick(r'n2avc: session \d+x\d+ started|VCPHEVC.*(session|started|startSession|encodeFrame)')   # (not the registration line)
    restarts = pick(r'N2VCD .*exiting 75')
    vcd_hevc = pick(r'N2VCD .*session \d+ open .*(hevc|h265|hvc1)')   # (daemon lines: vcodecd.log, rarely on the console)
    vcd_err = pick(r'N2VCD .*(hardware timeout|slow encode|guest stop|E_GUEST|failed)')
    diag = pick(r'diag: still: source 0: .*taken out')
    reg = pick(r'N2VIDEO .*hardware encoders registered')
    real = []   # sessions with pictures
    for l in ended:
        m = re.search(r'session (\d+)x(\d+) ended: (\d+) pictures \((\d+) key\), (\d+) bytes, (\d+) failures.*\(([\d.]+) fps submitted\)', l)
        if m and int(m.group(3)) > 0:
            real.append({'size': '%sx%s' % (m.group(1), m.group(2)), 'pictures': int(m.group(3)), 'keys': int(m.group(4)),
                         'bytes': int(m.group(5)), 'failures': int(m.group(6)), 'fps': float(m.group(7)), 'line': l})
    prof = []
    for l in hevc_first:
        m = re.search(r'ProfileLevel (\S+) -> profile (\d+) level (\d+)', l)
        prof.append(m.groups() if m else ('?', '?', '?'))
    out = ['registration: ' + (reg[-1] if reg else '(no "hardware encoders registered" line: claim-hw off or budget spent)'),
           'camera source attributes: ' + (diag[-1][:200] if diag else '(no diag line)'),
           'encoder sessions: hevc hardware opened %d, avc hardware opened %d, hevc fallbacks %d, software/VCPHEVC lines %d'
           % (len(hevc_open), len(avc_open), len(hevc_fail), len(sw))]
    out += ['  ' + l[:400] for l in hevc_open + avc_open + hevc_fail + first + ended + fatal + sw[:6]]
    out.append('vcodecd: hevc session opens %d, restarts (exiting 75) %d, errors %d' % (len(vcd_hevc), len(restarts), len(vcd_err)))
    out += ['  ' + l[:300] for l in (vcd_hevc + restarts + vcd_err)[-20:]]
    hevc_rec = [r for r in real if any(r['size'] in l for l in hevc_first)]
    ok = bool(hevc_open and hevc_first and hevc_rec and all(r['failures'] == 0 for r in hevc_rec) and not hevc_fail and not fatal)
    if not hevc_open and avc_open:
        why = 'Camera asked for H.264 (avc): Formats still "Most Compatible" (CAMUserPreferenceCaptureEncodingBehavior != 0) or HEVC not offered'
    elif hevc_fail:
        why = 'hevc StartSession failed -> VideoToolbox fell back (VCPHEVC software or none)'
    elif hevc_open and not hevc_first:
        why = 'hevc session opened but no first picture (first-picture reopen refused? see fatal / exiting 75 lines)'
    elif fatal:
        why = 'plug-in fatal/refused lines present'
    elif not hevc_open:
        why = 'no hevc session in this capture'
    else:
        why = 'ok'
    res = {'hevc_hw_used': ok, 'why': why, 'profile_level': prof, 'recordings': [{k: v for k, v in r.items() if k != 'line'} for r in hevc_rec],
           'daemon_restarts': len(restarts), 'daemon_errors': len(vcd_err), 'fallbacks': len(hevc_fail) + len(sw)}
    if not quiet:
        print('\n'.join(out))
        print('VERDICT log hevc_hw_used=%s why="%s" ProfileLevel=%s recordings=%s daemon_restarts=%d daemon_errors=%d fallbacks=%d'
              % ('yes' if ok else 'no', why, ','.join('%s->%s/%s' % p for p in prof) or '-',
                 ','.join('%s:%dpic/%dkey/%dfail/%.1ffps' % (r['size'], r['pictures'], r['keys'], r['failures'], r['fps']) for r in hevc_rec) or '-',
                 len(restarts), len(vcd_err), res['fallbacks']))
    return res


def nal_units(data):
    i, n, starts = 0, len(data), []
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


def length_prefixed(data, size=4):
    """NAL types of one MP4 sample (4-byte lengths)."""
    i, types = 0, []
    while i + size <= len(data):
        ln = int.from_bytes(data[i:i + size], 'big')
        if ln <= 0 or i + size + ln > len(data):
            return types, False
        types.append((data[i + size] >> 1) & 0x3f)
        i += size + ln
    return types, i == len(data)


def ffjson(args):
    r = subprocess.run(['ffprobe', '-v', 'error', '-of', 'json'] + args, capture_output=True, text=True)
    return json.loads(r.stdout or '{}')


def cmd_movie_one(f, quiet=False):
    st = ffjson(['-select_streams', 'v:0', '-count_frames', '-show_entries',
                 'stream=codec_name,codec_tag_string,profile,level,width,height,nb_read_frames,avg_frame_rate,r_frame_rate,'
                 'duration,bit_rate,pix_fmt,color_range,color_space,color_transfer,color_primaries,has_b_frames,extradata_size', f])
    s = (st.get('streams') or [{}])[0]
    frames = int(s.get('nb_read_frames') or 0)
    dur = float(s.get('duration') or 0)
    fps = frames / dur if dur > 0 else 0.0
    # packets: pts/dts order, key flags, raw samples for in-band parameter sets
    pk = ffjson(['-select_streams', 'v:0', '-show_entries', 'packet=pts,dts,flags,size', f]).get('packets', [])
    keys = sum(1 for p in pk if 'K' in (p.get('flags') or ''))
    reorder = sum(1 for p in pk if p.get('pts') is not None and p.get('dts') is not None and p['pts'] != p['dts'])
    fr = ffjson(['-select_streams', 'v:0', '-read_intervals', '%+#120', '-show_entries', 'frame=pict_type', f]).get('frames', [])
    ptypes = {}
    for x in fr:
        ptypes[x.get('pict_type', '?')] = ptypes.get(x.get('pict_type', '?'), 0) + 1
    inband, slices, vcl_types, bad_len = 0, [], {}, 0
    with tempfile.TemporaryDirectory() as td:
        raw = os.path.join(td, 'raw.hevc')       # samples as stored (length-prefixed), first 60
        subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', f, '-map', '0:v:0', '-c', 'copy', '-frames:v', '60',
                        '-f', 'data', raw], capture_output=True)
        es = os.path.join(td, 'v.es')
        subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', f, '-map', '0:v:0', '-c', 'copy', '-bsf:v', 'hevc_mp4toannexb',
                        '-frames:v', '60', '-f', 'hevc', es], capture_output=True)
        data = open(es, 'rb').read() if os.path.exists(es) else b''
        # per-sample NAL types: ffprobe gives sizes; the data muxer concatenates samples in order
        rawb = open(raw, 'rb').read() if os.path.exists(raw) else b''
        off = 0
        for p in pk[:60]:
            n = int(p.get('size') or 0)
            t, okl = length_prefixed(rawb[off:off + n])
            off += n
            if not okl:
                bad_len += 1
            if any(x in (32, 33, 34) for x in t):
                inband += 1
            for x in t:
                if x <= 31:
                    vcl_types[x] = vcl_types.get(x, 0) + 1
    cur = 0
    for nal in nal_units(data):
        if len(nal) < 3:
            continue
        t = (nal[0] >> 1) & 0x3f
        if t <= 31:
            if (nal[2] & 0x80) and cur:
                slices.append(cur)
                cur = 0
            cur += 1
    if cur:
        slices.append(cur)
    spp = sum(slices) / len(slices) if slices else 0.0
    dec = subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', f, '-map', '0:v:0', '-f', 'null', '-'],
                         capture_output=True, text=True).stderr.strip().splitlines()
    dec = [l for l in dec if 'non monotonically increasing dts' not in l]
    problems = []
    if s.get('codec_name') != 'hevc':
        problems.append('codec %s (not hevc)' % s.get('codec_name'))
    if s.get('codec_tag_string') != 'hvc1':
        problems.append('sample entry %s (not hvc1)' % s.get('codec_tag_string'))
    if s.get('profile') not in ('Main',):
        problems.append('profile %s' % s.get('profile'))
    if frames == 0:
        problems.append('no frames')
    if dec:
        problems.append('decode errors')
    if bad_len:
        problems.append('%d samples not 4-byte length-prefixed' % bad_len)
    if inband:
        problems.append('%d samples carry VPS/SPS/PPS in-band (hvc1 should not; harmless for most players)' % inband)
    if reorder or ptypes.get('B'):
        problems.append('B-frames/reordering (pts!=dts %d, B %d): unexpected for the VENC' % (reorder, ptypes.get('B', 0)))
    made_by = 'hardware VENC (1 slice/picture)' if slices and round(spp) == 1 else 'unknown (%.2f slices/picture)' % spp
    hard = [p for p in problems if 'harmless' not in p]
    res = {'file': f, 'ok': not hard, 'problems': problems, 'fps': fps, 'frames': frames, 'encoder': made_by}
    if not quiet:
        print('MOVIE %s' % f)
        print('  %s tag %s %s level %s %sx%s %s range=%s primaries=%s transfer=%s; %d frames in %.2f s = %.2f fps (avg %s), %s bit/s'
              % (s.get('codec_name'), s.get('codec_tag_string'), s.get('profile'), s.get('level'), s.get('width'), s.get('height'),
                 s.get('pix_fmt'), s.get('color_range'), s.get('color_primaries'), s.get('color_transfer'), frames, dur, fps,
                 s.get('avg_frame_rate'), s.get('bit_rate')))
        print('  packets %d, key %d, pts!=dts %d, has_b_frames %s, picture types (first 120) %s, hvcC extradata %s B'
              % (len(pk), keys, reorder, s.get('has_b_frames'), ptypes, s.get('extradata_size')))
        print('  first 60 samples: VCL NAL types %s, samples with in-band VPS/SPS/PPS %d, bad length framing %d'
              % (vcl_types, inband, bad_len))
        print('  encoder: %s; host decode: %s' % (made_by, 'OK' if not dec else '; '.join(dec[:3])))
        print('VERDICT movie=%s hevc_ok=%s encoder=%s fps=%.2f problems=%s'
              % (os.path.basename(f), 'yes' if res['ok'] else 'no', 'hw' if 'hardware' in made_by else '?', fps,
                 '; '.join(problems) or 'none'))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    p = sp.add_parser('log'); p.add_argument('log'); p.add_argument('--since')
    p = sp.add_parser('movie'); p.add_argument('files', nargs='+')
    p = sp.add_parser('both'); p.add_argument('log'); p.add_argument('files', nargs='+'); p.add_argument('--since')
    a = ap.parse_args()
    if a.cmd == 'log':
        return 0 if cmd_log(a)['hevc_hw_used'] else 1
    if a.cmd == 'movie':
        return 0 if all([cmd_movie_one(f)['ok'] for f in a.files]) else 1
    lr = cmd_log(a)
    mr = [cmd_movie_one(f) for f in a.files]
    good = lr['hevc_hw_used'] and all(m['ok'] and 'hardware' in m['encoder'] for m in mr)
    print('VERDICT HEVC CAMERA %s (log: %s; movies: %s)' % ('OK: hvc1 recorded on the hardware encoder' if good else 'NOT OK',
          lr['why'], ', '.join('%s %s' % (os.path.basename(m['file']), 'ok' if m['ok'] else '/'.join(m['problems'])) for m in mr)))
    return 0 if good else 1


if __name__ == '__main__':
    sys.exit(main())
