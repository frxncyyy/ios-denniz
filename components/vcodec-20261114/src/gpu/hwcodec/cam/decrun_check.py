#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# decrun (12 Nov 2026): verdict of n2vt_decrun (payload-vcodec-prod6-decrun) from a console capture
# (autocycle/runs/<tag>/ui-usb/xnu-live.log) or the phone's /private/var/db/nord2-vcodec/decrun.log.
# Per clip: HARDWARE / SOFTWARE / UNKNOWN (dec_hw=1/0/-1), frames got/sent vs ffmpeg, fps, and bit-exactness against the
# host reference gpu/hwcodec/vtdec_run/expected.json (expected_md5.py): md5first, md5all (ordered), md5set (any order).
# Also counts the plug-in's N2VDEC lines and vcodecd's 'dec:' lines (the hardware path evidence).  Exit 0 = every expected
# clip decoded completely, bit-exact, on hardware.  usage: decrun_check.py LOG [EXPECTED.json]
import json, os, re, sys

RX = re.compile(r'N2VTDEC clip=(\S+) codec=(\S+) (\d+)x(\d+) frames=(\d+)/(\d+) fps=([\d.]+) dec_hw=(-?\d+) md5first=(\S+)(.*)')
RX_KV = re.compile(r'(\w+)=(\S+)')
RX_F = re.compile(r'N2VTDEC_F clip=(\S+) i=(\d+) md5=([0-9a-f]{32})')

def main():
    if len(sys.argv) < 2: sys.exit('usage: decrun_check.py LOG [EXPECTED.json]')
    exp_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'vtdec_run', 'expected.json')
    exp = json.load(open(exp_path))
    text = open(sys.argv[1], errors='replace').read()
    res, frames = {}, {}
    for m in RX.finditer(text):
        kv = dict(RX_KV.findall(m.group(10)))
        res[m.group(1)] = dict(codec=m.group(2), w=int(m.group(3)), h=int(m.group(4)), got=int(m.group(5)), sent=int(m.group(6)),
                               fps=float(m.group(7)), hw=int(m.group(8)), md5first=m.group(9), **kv)
    cmps = {m.group(1): m.groups()[1:] for m in re.finditer(r'N2VTDEC_CMP clip=(\S+) equal=(\d+)/(\d+) expected=\d+ bad=(\d+) first_bad=(\S+)', text)}
    cmps = {k: (v[0], v[1], v[2], v[3]) for k, v in cmps.items()}
    for m in RX_F.finditer(text): frames.setdefault(m.group(1), {})[int(m.group(2))] = m.group(3)
    start = 'N2VTDEC START' in text; done = re.search(r'N2VTDEC DONE clips=(\d+) ok=(\d+)', text)
    nvdec = len(re.findall(r'N2VDEC ', text)); nvcd = len(re.findall(r'N2VCD [\d.]+ dec: ', text))
    print('decrun: START %s, DONE %s; plug-in N2VDEC lines %d, vcodecd dec: lines %d' % (start, done.group(0) if done else 'absent', nvdec, nvcd))
    chg = re.findall(r'N2VTDEC_CHANGED clip=(\S+) i=(\d+)', text)   # dr3d: picture changed after delivery (surface reuse)
    if chg or '--rehash-ms' in text or 'rehash_ms=' in text:
        print('decrun: pictures that CHANGED %s after delivery: %d %s' % ('(rehash)', len(chg), sorted(set(c for c, _ in chg))))
    # dr4: VT runs the decoder in mediaserverd, so the tool's property read gives -1; the hardware evidence is the plug-in's
    # N2VDEC session lines without a software handover
    sw = re.search(r'N2VDEC .*(hardware unusable|software|handover)', text)
    hw_ev = nvdec > 0 and not sw
    allok = True
    runs = [(c, c) for c in exp]   # (name in the log, clip of expected.json); dr4: second pass "<clip>@rN"
    runs += [(n, n.split('@')[0]) for n in sorted(res) if '@' in n and n.split('@')[0] in exp]
    for clip, base in runs:
        e = exp[base]
        r = res.get(clip)
        if not r:
            print('%-34s MISSING (no N2VTDEC line)' % clip); allok = False; continue
        verdict = {1: 'HARDWARE', 0: 'SOFTWARE'}.get(r['hw'], 'HARDWARE(plug-in log)' if hw_ev else 'UNKNOWN(dec_hw=%d)' % r['hw'])
        size_ok = (r['w'], r['h']) == (e['w'], e['h'])
        full = r['got'] == e['frames'] and r['sent'] == e['frames']
        m_first = r['md5first'] == e['md5first']; m_all = r.get('md5all') == e['md5all']; m_set = r.get('md5set') == e['md5set']
        exact = 'BIT-EXACT (display order)' if m_all else 'BIT-EXACT (other output order)' if m_set else \
                'first frame only' if m_first else 'MISMATCH'
        if clip in frames:   # per-frame list available (decrun.log or --console-frames): first differing index
            got = [frames[clip][i] for i in sorted(frames[clip])]
            bad = [i for i, (a, b) in enumerate(zip(got, e['md5'])) if a != b]
            exact += '; per-frame %d/%d equal%s' % (len(got) - len(bad), len(e['md5']), ', first diff at %d' % bad[0] if bad else '')
        print('%-34s %s %s %dx%d%s frames %d/%d (ffmpeg %d) fps %.1f md5 %s status %s cb_err %s' % (
            clip, verdict, r['codec'], r['w'], r['h'], '' if size_ok else ' (expected %dx%d)' % (e['w'], e['h']), r['got'], r['sent'],
            e['frames'], r['fps'], exact, r.get('status', '?'), r.get('cb_err', '?')))
        cmp = cmps.get(clip)   # dr5: the tool's own comparison against the compiled-in ffmpeg list
        if cmp:
            exact += '; phone CMP equal %s/%s bad %s first_bad %s' % cmp
            if int(cmp[0]) == e['frames'] and cmp[2] == '0': m_set = True
        if not ((r['hw'] == 1 or (r['hw'] == -1 and hw_ev)) and full and size_ok and (m_all or m_set)): allok = False
    print('VERDICT: %s' % ('HARDWARE DECODE OK at real sizes (all clips complete, bit-exact, dec_hw=1)' if allok else 'NOT OK (see lines above)'))
    return 0 if allok else 1

if __name__ == '__main__':
    sys.exit(main())
