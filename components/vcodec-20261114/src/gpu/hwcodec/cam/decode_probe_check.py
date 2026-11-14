#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""decode_probe_check.py -- verdict for the VideoToolbox decode cases of gpu/video's boot probe (dp1, 12 Nov 2026).

    decode_probe_check.py LIVE.LOG [LIVE2.LOG ...] [--since TEXT] [--quiet]

Input: console captures (autocycle/runs/<tag>/ui-usb/xnu-live.log, or `autotest/phone.py logs` output).  Reads files only.

What it reads
  - gpu/video's probe (/usr/libexec/n2vt_probe, job com.nord2.videoprobe, gate /private/var/db/nord2-video-probe):
      'N2VIDEO probe: vt.decode.avc1 pass ...'          one result line per case (written AFTER the case finished)
      'N2VIDEO probe: + vt.decode.avc1 | ... | dec_hw=1 | frames=8/8 | md5=... | golden=... | match=1 ...'
    Each case runs in its own child process (`n2vt_probe --child NAME`), one after the other, so every N2VDEC / N2VCD line
    between the previous case's result line and this case's result line belongs to this case.
  - Slim.videodecoder (gpu/hwcodec/vt/dec), through the N2VIDEO channel: 'N2VIDEO n2vt_probe[pid]: N2VDEC ...'
      'registered 2 hardware decoder(s) (avc1+hvc1)'      plug-in loaded in the child, switch nord2-video-hwdec on
      'session start 31637661 176x144 stride .. MM21 ..'  HARDWARE session open (31637661 = avc1, 31637668 = hvc1)
      'first picture pts=0 176x144'                       first hardware picture delivered
      'hardware unusable (WHY, ERR): session runs on the software decoder'   in-object fallback (prod4)
      'avc1|hvc1 decoder not offered: daemon unavailable' factory refused, VT took Apple's H264SW / VCPHEVC
      'not registered: switch files ... say off'          hwdec switch missing -> Apple's decoders only
  - vcodecd: 'N2VCD <t> dec: session N open client=C codec=0x31637661 fmt=MM21', 'dec: session N header 176x144 ...',
    'dec: START i done ...', 'dec: session N close ...', failures ('input failed', 'header failed', ...).

Hardware vs software
  dec_hw=1   only our hardware object answers UsingHardwareAcceleratedVideoDecoder = true (n2vtd_obj.c); Apple's
             H264SW/VCPHEVC do not answer it (dec_hw=-1 in every software run so far, e.g. hm1-heic), and after the in-object
             fallback the property is forwarded to the software decoder.
  A case is HARDWARE when dec_hw=1, or (if the probe line is missing) when its segment has 'N2VDEC session start' and no
  fallback line.  md5 match=1 then means the hardware output is bit-exact with the golden luma of frame 0.
Exit status: 0 = every present decode case passed on the hardware with match=1, 1 = otherwise, 2 = probe not found.
"""
import argparse
import re
import sys

CASES = ('vt.decode.avc1', 'vt.decode.hvc1')
DAEMON_CODEC = {'vt.decode.avc1': '31637661', 'vt.decode.hvc1': '31637668'}

RX_RESULT = re.compile(r'N2VIDEO probe: (vt\.[\w.-]+|img\.[\w.-]+|audio\.[\w.-]+|av\.[\w.-]+) (pass|fail|skip|crash)\b')
RX_FIELDS = re.compile(r'N2VIDEO probe: \+ ([\w.-]+) (\|.*)')
RX_DONE = re.compile(r'N2VIDEO probe: DONE pass=(\d+) fail=(\d+) skip=(\d+) crash=(\d+) file=(\S+)')
RX_VDEC = re.compile(r'N2VIDEO ([\w.-]+)\[(\d+)\]: N2VDEC (.*)')
RX_VCD = re.compile(r'N2VCD [\d.]+ dec: (.*)')


def parse_fields(tail):
    out = {}
    for part in tail.split('|'):
        part = part.strip()
        if '=' in part:
            k, v = part.split('=', 1)
            out.setdefault(k.strip(), v.strip())
    return out


def analyse(lines):
    """Walk the log once; cut it into per-case segments at the probe's result lines."""
    seg = []                       # lines since the previous result line
    cases = {}                     # name -> dict
    fields = {}                    # name -> merged '+' fields
    done = None
    probe_seen = False
    registration = []
    for line in lines:
        m = RX_FIELDS.search(line)
        if m:
            probe_seen = True
            fields.setdefault(m.group(1), {}).update(
                {k: v for k, v in parse_fields(m.group(2)).items() if k not in fields.get(m.group(1), {})})
            continue
        m = RX_RESULT.search(line)
        if m:
            probe_seen = True
            name = m.group(1)
            if name in CASES:
                cases[name] = {'result': m.group(2), 'segment': seg}
            seg = []
            continue
        m = RX_DONE.search(line)
        if m:
            probe_seen = True
            done = m.groups()
            continue
        mv = RX_VDEC.search(line)
        if mv and re.search(r'registered|not registered', mv.group(3)):
            registration.append('%s: %s' % (mv.group(1), mv.group(3).strip()))
        # dp1 (12 Nov): on the phone VideoToolbox runs the decoder OUT of process, in mediaserverd, so the plug-in's
        # N2VDEC lines carry 'mediaserverd[29]', not the probe child.  N2VDEC is printed only by Slim.videodecoder, so
        # every process counts; the cases run one after the other, so the segment still belongs to one case.
        if mv or RX_VCD.search(line) or 'N2VIDEO n2vt_probe' in line:
            seg.append(line.strip())
    for name, c in cases.items():
        c['fields'] = fields.get(name, {})
    return probe_seen, cases, done, registration, fields


def judge(name, c):
    f = c['fields']
    seg = c['segment']
    vdec = [RX_VDEC.search(l).group(3) for l in seg if RX_VDEC.search(l)]
    vcd = [RX_VCD.search(l).group(1) for l in seg if RX_VCD.search(l)]
    hw_start = [s for s in vdec if re.match(r'session start [0-9a-f]{8} ', s)]   # not 'session start failed'
    first_pic = [s for s in vdec if s.startswith('first picture')]
    fallback = [s for s in vdec if 'runs on the software decoder' in s]
    not_offered = [s for s in vdec if 'decoder not offered' in s]
    not_reg = [s for s in vdec if s.startswith('not registered')]
    hw_err = [s for s in vdec if re.search(r'failed|rejected|dropped|no software decoder|failed too', s)]
    code = DAEMON_CODEC[name]
    d_open = [s for s in vcd if re.search(r'session \d+ open .*codec=0x' + code, s)]
    d_hdr = [s for s in vcd if re.search(r'session \d+ header \d+x\d+', s)]
    d_start = [s for s in vcd if s.startswith('START')]
    d_err = [s for s in vcd if re.search(r'failed|rejected|unsupported|fatal|idle for|unregistered|twice', s)]
    dec_hw = f.get('dec_hw', '?')

    # dp1: dec_hw is -1 even for a hardware session (the property is not answered across VideoToolbox's process
    # boundary), so the N2VDEC lines decide when they are there.
    if dec_hw == '1':
        decoder = 'HARDWARE'
    elif hw_start and not fallback:
        decoder = 'HARDWARE (from N2VDEC: %s%s)' % (hw_start[0][:60], '' if first_pic else '; NO first picture line')
    elif fallback:
        decoder = 'SOFTWARE-FALLBACK (in-object: %s)' % re.sub(r'^\S+: ', '', fallback[0])[:120]
    elif not_offered:
        decoder = 'SOFTWARE (factory: daemon unavailable)'
    elif not_reg:
        decoder = 'SOFTWARE (Slim.videodecoder not registered: nord2-video-hwdec off)'
    elif dec_hw in ('-1', '0'):
        decoder = 'SOFTWARE (dec_hw=%s; no N2VDEC line for this case: plug-in not loaded, or its 40 lines used up)' % dec_hw
    else:
        decoder = 'UNKNOWN'
    match = f.get('match')
    md5 = 'match' if match == '1' else 'MISMATCH' if match == '0' else ('none' if f.get('md5') == 'none' else 'n/a')
    return {
        'decoder': decoder, 'hw': decoder.startswith('HARDWARE'), 'md5': md5, 'frames': f.get('frames', '?'),
        'statuses': f.get('statuses', '?'), 'dec_hw': dec_hw, 'wh': f.get('wh', '?'), 'result': c['result'],
        'evidence': {'N2VDEC session start': hw_start[:1], 'N2VDEC first picture': first_pic[:1],
                     'N2VDEC fallback/not offered': (fallback + not_offered + not_reg)[:2],
                     'N2VDEC errors': hw_err[:3], 'N2VCD open': d_open[:1], 'N2VCD header': d_hdr[:1],
                     'N2VCD START lines': ['%d' % len(d_start)] if d_start else [], 'N2VCD errors': d_err[:3]},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--since', help='only the text after the last occurrence of this marker (in the concatenated logs)')
    ap.add_argument('--quiet', action='store_true', help='print only the VERDICT lines')
    a = ap.parse_args()
    text = ''.join(open(p, 'rb').read().decode('utf-8', 'replace') + '\n' for p in a.logs)
    if a.since and a.since in text:
        text = text[text.rindex(a.since):]
    probe_seen, cases, done, registration, _ = analyse(text.splitlines())
    if not probe_seen:
        print('VERDICT probe=absent: no "N2VIDEO probe:" line (switch /private/var/db/nord2-video-probe missing, job '
              'com.nord2.videoprobe not run yet (45 s after boot), or capture too short)')
        return 2
    if not a.quiet:
        print('probe: %s' % ('DONE pass=%s fail=%s skip=%s crash=%s file=%s' % done if done else
                             'no DONE line yet (still running or capture ended)'))
        print('decoder registration lines: ' + ('; '.join(registration[-4:]) if registration else '(none)'))
    all_ok = True
    for name in CASES:
        if name not in cases:
            print('VERDICT %s: no result line (case not reached yet)' % name)
            all_ok = False
            continue
        j = judge(name, cases[name])
        ok = j['hw'] and j['md5'] == 'match' and j['result'] == 'pass'
        all_ok &= ok
        if not a.quiet:
            print('%s: result=%s dec_hw=%s frames=%s statuses=%s wh=%s' % (name, j['result'], j['dec_hw'], j['frames'],
                                                                          j['statuses'], j['wh']))
            for k, v in j['evidence'].items():
                for s in v:
                    print('    %-28s %s' % (k, s[:220]))
        print('VERDICT %s: decoder=%s md5=%s frames=%s' % (name, j['decoder'], j['md5'], j['frames']))
    print('VERDICT overall: %s' % ('HARDWARE DECODE OK (both codecs on Slim.videodecoder hardware, bit-exact)' if all_ok
                                   else 'NOT (all) hardware+bit-exact: see the lines above'))
    return 0 if all_ok else 1


if __name__ == '__main__':
    sys.exit(main())
