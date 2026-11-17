#!/usr/bin/env python3
"""Private source-only tests and normal NORD2 ARM64 compilation; no device/CI."""
from pathlib import Path
import subprocess, json, hashlib
p=Path(__file__).resolve().parent
repo=p.parents[4]
profile=p.parent/'profile-sol'
art=p/'verification';art.mkdir(exist_ok=True)
modules=['audio_hw','audio_afe','audio_id','audio_i2c8','audio_amp']
checks=[]
def run(args):
 r=subprocess.run([str(x) for x in args],cwd=repo,text=True,capture_output=True)
 checks.append({'command':[str(x) for x in args],'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
 if r.returncode:
  (art/'results.json').write_text(json.dumps(checks,indent=2))
  raise RuntimeError(r.stdout+r.stderr)
 return r.stdout
flags=['clang','-std=c11','-DNORD2','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
for case in ['stream','i2c','full']:
 src=[p/f'test-{case}.c']
 if case=='i2c':src+=[p/'audio_i2c8.c']
 else:src += [p/(n+'.c') for n in modules]
 src+=[profile/'audio_board.c']
 extra=['-Wno-unused-function'] if case=='full' else []
 target=art/f'test-{case}'
 run(flags+extra+['-I'+str(p),'-I'+str(profile)]+src+['-o',target])
 print(run([target]+([profile/'tfa98xx.cnt'] if case=='full' else [])).strip())
bt=repo/'research/ci-39398655023/buildtree'
for name in modules+['compile-native']:
 run(['python3',repo/'tools/nord2-xnu-cc.py',bt,'bsd',p/(name+'.c'),art/(name+'.arm64.o'),'-DNORD2=1','-I'+str(profile)])
run(['python3',repo/'tools/nord2-xnu-cc.py',bt,'bsd',profile/'audio_board.c',art/'audio_board.arm64.o','-DNORD2=1','-I'+str(p)])
(art/'results.json').write_text(json.dumps(checks,indent=2))
(art/'source-sha256.json').write_text(json.dumps({str(f.relative_to(p)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(p.glob('*')) if f.is_file() and f.suffix in ['.c','.h','.inc','.py']},indent=2))
print('PASS: 3 ASan/UBSan host suites, 7 normal NORD2 ARM64 translation units; no device access')
