#!/usr/bin/env python3
"""Apply ONLY the two audit patches to a private copy of current canonical audio.
No canonical mutation, CI, git, hardware, USB or flash activity.
"""
from pathlib import Path
import subprocess,shutil,json
p=Path(__file__).resolve().parent;r=p.parents[4];canon=r/'shim/nord2-xnu'
a=p/'verification/patched-canonical';d=a/'shim/nord2-xnu';d.mkdir(parents=True,exist_ok=True)
for f in canon.glob('audio*'):
 if f.is_file() and f.suffix in ('.c','.h','.inc'):shutil.copy2(f,d/f.name)
results=[]
def run(cmd):
 x=subprocess.run([str(v) for v in cmd],cwd=r,text=True,capture_output=True)
 results.append({'command':[str(v) for v in cmd],'exit':x.returncode,'stdout':x.stdout,'stderr':x.stderr})
 if x.returncode:raise RuntimeError(x.stdout+x.stderr)
 return x.stdout
try:
 for name in ('audio_hw.c','audio_hw.h'):
  run(['patch','--batch','-p1','-d',a,'-i',p/(name+'.integration-audit.patch')])
 modules=['audio_hw','audio_afe','audio_id','audio_i2c8','audio_amp','audio_board']
 flags=['clang','-std=c11','-DNORD2','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),'-I'+str(p)]
 for case in ('stream','i2c','full'):
  names=['audio_i2c8','audio_board'] if case=='i2c' else modules
  target=a/('test-'+case)
  run(flags+(['-Wno-unused-function'] if case=='full' else [])+[p/('test-'+case+'.c')]+[d/(n+'.c') for n in names]+['-o',target])
  print(run([target]+([p.parent/'profile-sol/tfa98xx.cnt'] if case=='full' else [])).strip())
 bt=r/'research/ci-39398655023/buildtree'
 for name in modules+['audio_device']:
  run(['python3',r/'tools/nord2-xnu-cc.py',bt,'bsd',d/(name+'.c'),a/(name+'.arm64.o'),'-DNORD2=1','-I'+str(d)])
 print('PASS actual patched canonical copy:3 sanitized suites +7 normal ARM64 units including latest audio_device worker')
finally:
 (a/'results.json').write_text(json.dumps(results,indent=2))
