#!/usr/bin/env python3
"""t_audio_check.py ROOT [BASELINE.json]: the layout checks of the kernel's audio installer (bsd/dev/audio_install.c, committed
boot = audio_install_flow.inc phase 2: custom(&j,1), same(B), same(V), absent(D)) applied to a root tree on the host.
custom(): entries(HAL) == {RedwoodAudio.driver}; entries(HAL/RedwoodAudio.driver) == {RedwoodAudio, Info.plist}; both files
regular, nlink 1. same(B/V): identity (inode) of HAL.nord2-original and /Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin unchanged
-> here: they exist and their trees are unchanged vs BASELINE (we never write into them). absent(D): HAL.disabled/VirtualAudio.plugin.
Writes BASELINE when it does not exist; exits 1 on any violation (that boot would print "HAL recovery required")."""
import hashlib, json, os, sys
R = sys.argv[1]; base = sys.argv[2] if len(sys.argv) > 2 else None
A = '/System/Library/Audio/Plug-Ins/HAL'; B = '/System/Library/Audio/Plug-Ins/HAL.nord2-original'
V = '/Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin'; D = '/Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin'
bad = []
def tree(p):
    out = {}
    for root, dirs, files in os.walk(R + p):
        for f in sorted(files):
            q = os.path.join(root, f); out[q[len(R):]] = hashlib.sha256(open(q, 'rb').read()).hexdigest() if not os.path.islink(q) else 'link:' + os.readlink(q)
        for d in sorted(dirs): out[os.path.join(root, d)[len(R):]] = 'dir'
    return out
ents = sorted(e for e in os.listdir(R + A)) if os.path.isdir(R + A) else None
if ents != ['RedwoodAudio.driver']: bad.append('custom(): %s holds %s, the kernel allows only [RedwoodAudio.driver]' % (A, ents))
if os.path.isdir(R + A + '/RedwoodAudio.driver'):
    e2 = sorted(os.listdir(R + A + '/RedwoodAudio.driver'))
    if e2 != ['Info.plist', 'RedwoodAudio']: bad.append('custom(): RedwoodAudio.driver holds %s' % e2)
    for f in ('RedwoodAudio', 'Info.plist'):
        p = R + A + '/RedwoodAudio.driver/' + f
        if os.path.exists(p) and (not os.path.isfile(p) or os.path.islink(p) or os.stat(p).st_nlink != 1): bad.append('custom(): %s not a single-link regular file' % f)
for p in (B, V):
    if not os.path.isdir(R + p): bad.append('same(): %s missing' % p)
if os.path.exists(R + D): bad.append('absent(D): %s exists' % D)
cur = {'B': tree(B), 'V': tree(V), 'A_redwood': tree(A + '/RedwoodAudio.driver')}
if base:
    if not os.path.exists(base): json.dump(cur, open(base, 'w')); print('AUDIOCHECK baseline written')
    else:
        old = json.load(open(base))
        for k in cur:
            if cur[k] != old[k]: bad.append('%s tree changed (same() would see a different object)' % k)
for b in bad: print('AUDIOCHECK FAIL', b)
print('AUDIOCHECK', 'OK' if not bad else 'WOULD REFUSE LAUNCHD')
sys.exit(1 if bad else 0)
