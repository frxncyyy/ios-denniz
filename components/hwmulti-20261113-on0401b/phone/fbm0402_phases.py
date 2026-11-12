#!/usr/bin/env python3
"""0402b cycle summary: fbm0402_phases.py LOG
Lists the N2FBHWCTL feature phases (A defaults, B scaling, C H6, D 4L, E all), backboardd restarts and, per phase, the HW counters and
fault lines seen inside it; then names the phase that was running when the log ends.  'done' reached = no lockup in any phase (exit 0);
log cut inside a phase = that phase's feature set is the suspect (exit 1); no phase line = NO DATA (exit 2)."""
import re, sys
NAMES = {1: 'A defaults', 2: 'B kern.n2fbscaling=1', 3: 'C kern.n2fbh6=1', 4: 'D kern.n2fbhwlayers=4', 5: 'E all three'}
t = open(sys.argv[1], errors='replace').read()
ph = [(m.start(), int(m.group(1)), int(m.group(2)), m.group(3)) for m in re.finditer(r'N2FBHWCTL phase (\d+)/(\d+) mode=\d+ set rc=-?\d+ readback=-?\d+ t=\d+([^\n]*)', t)]
if not ph: print('NO DATA: no N2FBHWCTL phase line'); sys.exit(2)
done = re.search(r'N2FBHWCTL done final', t)
for k, (pos, i, n, feat) in enumerate(ph):
    end = ph[k + 1][0] if k + 1 < len(ph) else (done.start() if done else len(t))
    seg = t[pos:end]
    hw = re.findall(r'NORD2 FB HW: mode=\d+ [^\n]*?hw_frames=(\d+) cpu_frames=(\d+)', seg)
    flt = re.findall(r'NORD2 FB HW: (AUTO-DISABLE[^\n]{0,80}|4L OFF[^\n]{0,80}|BACKOFF[^\n]{0,60})', seg)
    cf = re.findall(r'FB HW: CPU-FREE (\w+)', seg)
    bbd = len(re.findall(r'N2FBHWCTL bbd restart', t[ph[k - 1][0] if k else 0:pos]))
    print('phase %d/%d %-22s%s | log %d bytes, HW lines %d (last hw/cpu %s), cpufree %s, faults %s, bbd restarts before it %d' % (
        i, n, NAMES.get(i, '?'), feat, len(seg), len(hw), '/'.join(hw[-1]) if hw else '-', ','.join(cf) or '-', flt or '-', bbd))
if done:
    print('RESULT: N2FBHWCTL done reached -- no lockup in any phase'); sys.exit(0)
last = ph[-1]
fe = [m for m in re.finditer(r'N2FBHWCTL feat (\d+)/\d+ ([^\n]*)', t)]
if fe and fe[-1].start() > last[0]:
    i = int(fe[-1].group(1))
    print('RESULT: log ends while phase %d (%s) was being applied (feature sysctls set%s, before its phase line): suspect = %s %s' % (
        i, NAMES.get(i, '?'), ', backboardd restarted' if 'N2FBHWCTL bbd restart' in t[fe[-1].start():] else '', NAMES.get(i, '?'), fe[-1].group(2)))
    sys.exit(1)
print('RESULT: log ends inside phase %d (%s) -- %d bytes after its phase line: suspect = %s%s' % (last[1], NAMES.get(last[1], '?'), len(t) - last[0],
      NAMES.get(last[1], '?'), last[3]))
sys.exit(1)
