#!/usr/bin/env python3
"""Private inherited-state host regression; no device, CI or canonical edits.
Uncaptured controller/DMA/amp state is explicitly simulated, never hardware proof.
"""
from pathlib import Path
import argparse,subprocess,json,shutil,hashlib
from datetime import datetime,timezone
p=Path(__file__).resolve().parent
repo=p.parents[5]
a=argparse.ArgumentParser();a.add_argument('--source',type=Path,default=repo/'shim/nord2-xnu')
a.add_argument('--case',choices=['cycles','rate','status','adsp','drift','all'],default='all')
a.add_argument('--baseline',action='store_true',help='Compile lifecycle harness, run captured canonical rejection/rate decode only')
x=a.parse_args();src=x.source.resolve()
if (src/'shim/nord2-xnu').is_dir():src=src/'shim/nord2-xnu'
stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
v=p/'verification'/((('baseline' if x.baseline else 'candidate')+'-'+stamp));v.mkdir(parents=True)
model=v/'model';model.mkdir()
print('Receipt:',v/'results.json')
for name in ['test-full.c','test-stream.c','test-i2c-model.h']:shutil.copyfile(repo/'tests/nord2-audio'/name,model/name)
receipt_files=[p/'test-inherited.c',p/'actual-snapshot.inc']+list(model.glob('*.c'))+list(model.glob('*.h'))+[src/(n+'.c') for n in ['audio_hw','audio_afe','audio_amp','audio_id','audio_i2c8','audio_board']]+list(src.glob('audio_*.h'))
(v/'source-sha256.json').write_text(json.dumps({str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in receipt_files},indent=2))
checks=[]
def run(cmd):
 r=subprocess.run([str(z) for z in cmd],cwd=repo,text=True,capture_output=True)
 checks.append(dict(command=[str(z) for z in cmd],exit=r.returncode,stdout=r.stdout,stderr=r.stderr))
 (v/'results.json').write_text(json.dumps(checks,indent=2))
 print(r.stdout,end='');print(r.stderr,end='')
 if r.returncode:raise SystemExit(r.returncode)
flags=['clang','-std=c11','-DNORD2','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(model),'-I'+str(src)]
exe=v/'test-inherited'
run(flags+[p/'test-inherited.c']+[src/(n+'.c') for n in ['audio_hw','audio_afe','audio_amp','audio_id','audio_i2c8','audio_board']]+['-o',exe])
if x.baseline:
 baseline=v/'test-actual'
 canonical=repo/'shim/nord2-xnu'
 run(flags+['-I'+str(canonical),p/'test-actual.c',canonical/'audio_afe.c',canonical/'audio_id.c','-o',baseline])
 run([baseline]);print('Lifecycle harness compiled; candidate lifecycle not executed in baseline mode.')
else:
 blob=p.parent.parent/'profile-sol/tfa98xx.cnt'
 assert blob.is_file(),blob
 for case in (['cycles','rate','status','adsp','drift'] if x.case=='all' else [x.case]):run([exe,blob,case])
 print('PASS candidate inherited-state regression. Controller/amp behavior is simulated, not hardware proof.')
