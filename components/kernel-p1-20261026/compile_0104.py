#!/usr/bin/env python3
"""Compile bsd/dev/nord2_cpufreq.c (patch 0104) with the CI flags of bsd/dev/sweet_touch.c, reusing ansuz-port's compile_0099.py machinery.

    compile_0104.py <source.c> <out.o>      (the directory of <source.c> is added with -I for nord2_cpufreq.h)
Host-only, nice 19, nothing outside <out.o>/<out.log> is written."""
import hashlib, json, os, shlex, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path('$NORD2_WORK/gpu/build/ansuz-port-20261026/0099')))
import compile_0099 as c

src, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
argv = c.rewrite(c.template('bsd'), 'bsd', src, out)
argv.insert(1, '-I' + str(src.parent))
env = dict(os.environ, SDKROOT='')
r = subprocess.run(['nice', '-n', '19'] + argv, cwd=c.OBJ / 'bsd' / 'DEVELOPMENT', env=env, capture_output=True, text=True)
out.with_suffix('.log').write_text(' '.join(shlex.quote(x) for x in argv) + '\n\n' + r.stdout + r.stderr)
res = {'rc': r.returncode, 'errors': r.stderr.count('error:'), 'warnings': r.stderr.count('warning:'),
       'source_sha256': hashlib.sha256(src.read_bytes()).hexdigest(),
       'object_sha256': hashlib.sha256(out.read_bytes()).hexdigest() if r.returncode == 0 and out.exists() else None}
out.with_suffix('.json').write_text(json.dumps(res, indent=1) + '\n')
print(json.dumps(res))
if r.returncode:
    print(r.stderr[-3000:], file=sys.stderr)
sys.exit(r.returncode)
