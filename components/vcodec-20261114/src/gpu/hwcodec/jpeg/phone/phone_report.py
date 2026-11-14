#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""opus-jpeg. Result chain of a jp* phone run (never touches the phone).

  phone_report.py NAME [--fetch] [--no-doc] [--live LOG]

1. live:  autocycle/runs/NAME/ui-usb/xnu-live.log (the USB console of the run): N2JPGP/N2JPG lines (the job also writes them to
          /dev/console) + the kernel's NORD2 JPEG/JPG/VCODEC lines.  Available right after txn.sh's capture.
2. files: with --fetch, `vcodec_check.py fetch NAME` (the mirror as the NEXT transaction's snapshot left it) then
          phone_check.py on apps-20261104/vcodec-fetch/NAME (structure, djpeg, PSNR vs picture and vs libjpeg, decode vs djpeg).
          Without --fetch an existing fetch directory is used if present.
3. doc:   the summary replaces the block between "<!-- JPEG-RESULT NAME BEGIN -->" and "... END -->" in
          gpu/hwcodec/JPEG_PHONE_TEST.md (appended under section 9 the first time).  --no-doc prints only.
The summary separates [V-phone live log], [V-phone files + PC check] and what is missing.
"""
import argparse, os, re, subprocess, sys, datetime

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(os.path.dirname(HERE))
B = os.path.join(os.path.dirname(HW), 'build')
DOC = os.environ.get('N2JPGP_DOC') or os.path.join(HW, 'JPEG_PHONE_TEST.md')


def grab(lines, pat):
    return [l for l in lines if re.search(pat, l)]


def summarize(lines, src):
    out = []
    j = [l[l.index('N2JPG'):] for l in lines if 'N2JPG' in l]
    k = [l.strip() for l in lines if re.search(r'NORD2 (JPEG|JPG):', l)]
    if not j:
        return ['- %s: **no N2JPGP line** (job did not run: gate/install? or console lost)' % src], {}
    done = grab(j, r'N2JPGP DONE')
    facts = {'done': done[-1] if done else None}
    out.append('- %s: %s' % (src, '`%s`' % done[-1] if done else '**no DONE line** (killed/crashed? last line: `%s`)' % j[-1]))
    for pat in (r'N2JPGP (NODEV|jetsam|sysctl)', r'N2JPGP info boot', r'N2JPG selftest 640x480 (PASS|FAIL)'):
        for l in grab(j, pat)[:1]:
            out.append('  - `%s`' % l)
    enc = grab(j, r'N2JPGP enc \d+x\d+ q=\d+')
    if enc:
        out.append('  - encodes (size q bytes hw_us wall_us hw_ms/MP):')
        for l in enc:
            m = re.search(r'enc (\d+x\d+) q=(\d+) code=\S+ (?:bytes=(\d+).*?hw_us=(\d+) wall_us=(\d+) hw_ms_per_mp=([\d.]+)|(rc=.*))', l)
            if m and m.group(3):
                out.append('    - %s q%s: %s B, hw %s us, wall %s us, %s ms/MP' % (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5), m.group(6)))
            elif m:
                out.append('    - %s q%s: **FAILED** %s' % (m.group(1), m.group(2), m.group(7)))
    big = grab(j, r'(PASS|FAIL) enc 4032x3024')
    if big:
        ok = all('PASS' in l for l in big)
        facts['12mp'] = ok
        out.append('  - **12 MP (4032x3024) single job: %s**' % ('PASS: no strips needed' if ok else 'FAIL: strips needed (see limit lines)'))
    for l in grab(j, r'(PASS|FAIL) limit '):
        out.append('  - `%s`' % l)
    for l in grab(j, r'N2JPG bench'):
        out.append('  - `%s`' % l)
    for l in grab(j, r'N2JPGP (FAIL|SKIP) '):
        out.append('  - `%s`' % l)
    for l in grab(j, r'N2JPGP (PASS|FAIL) idle off|after idle: wall'):
        out.append('  - `%s`' % l)
    for l in grab(j, r'hwdec (E0/E1 probe|search on|after \S+ \(post-reset|window |\S+ \S+ regs\(|\S+ \S+: (rc=|Y mean))|S7 kernel'):
        out.append('  - `%s`' % l[:400])
    for l in grab(j, r'dec .*(query|decoded|does NOT fit)'):
        out.append('  - `%s`' % l)
    if k:
        out.append('  - kernel: %d `NORD2 JPEG/JPG` lines; first/last: `%s` / `%s`' % (len(k), k[0][:160], k[-1][:160]))
        # a failed RWJD_JOB returns errno only (0316): status / 0x274 / 0x278 / 0x340 of each step are these lines, in step order
        for l in [x for x in k if 'NORD2 JPEG: dec' in x][:40]:
            out.append('  - kernel `%s`' % l[l.index('NORD2 JPEG: dec'):][:160])
    return out, facts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name'); ap.add_argument('--fetch', action='store_true'); ap.add_argument('--no-doc', action='store_true')
    ap.add_argument('--live')
    a = ap.parse_args()
    body = ['_generated %s by jpeg/phone/phone_report.py %s_' % (datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), a.name), '']
    live = a.live or os.path.join(B, 'autocycle', 'runs', a.name, 'ui-usb', 'xnu-live.log')
    if os.path.exists(live):
        s, _ = summarize(open(live, errors='replace').read().splitlines(), '[V-phone live console] `%s`' % os.path.relpath(live, B))
        body += s
    else:
        body.append('- live console log %s: absent' % live)
    fd = os.path.join(B, 'apps-20261104', 'vcodec-fetch', a.name)
    if a.fetch:
        r = subprocess.run([sys.executable, os.path.join(B, 'apps-20261104', 'vcodec_check.py'), 'fetch', a.name], capture_output=True, text=True)
        body.append('- fetch: rc %d, %d files' % (r.returncode, r.stdout.count('FILE ')))
    logf = next((os.path.join(fd, n) for n in ('jpeg-selftest.log', 'jpeg.log') if os.path.exists(os.path.join(fd, n))), None)
    if logf:
        s, _ = summarize(open(logf, errors='replace').read().splitlines(), '[V-phone file] `%s`' % os.path.relpath(logf, B))
        body += s
        r = subprocess.run([sys.executable, os.path.join(HERE, 'phone_check.py'), fd, '--inputs',
                            os.path.join(B, 'apps-20261104', 'jpeg-inputs')], capture_output=True, text=True)
        body.append('- PC check (phone_check.py, rc %d):' % r.returncode)
        body += ['  - `%s`' % l for l in r.stdout.splitlines() if re.match(r'(ENC|DEC|JPEG_CHECK)', l)]
    else:
        body.append('- files: not fetched yet (they arrive with the next transaction snapshot; then `phone_report.py %s --fetch`)' % a.name)
    text = '\n'.join(body)
    print(text)
    if a.no_doc:
        return 0
    doc = open(DOC).read()
    b, e = '<!-- JPEG-RESULT %s BEGIN -->' % a.name, '<!-- JPEG-RESULT %s END -->' % a.name
    block = '%s\n### Run %s\n\n%s\n%s' % (b, a.name, text, e)
    if b in doc:
        doc = re.sub(re.escape(b) + '.*?' + re.escape(e), lambda m: block, doc, flags=re.S)
    else:
        if '## 9. Phone results' not in doc:
            doc = doc.rstrip('\n') + '\n\n## 9. Phone results (generated: jpeg/phone/phone_report.py)\n'
        doc = doc.rstrip('\n') + '\n\n' + block + '\n'
    open(DOC, 'w').write(doc)
    print('updated', DOC)
    return 0


if __name__ == '__main__':
    sys.exit(main())
