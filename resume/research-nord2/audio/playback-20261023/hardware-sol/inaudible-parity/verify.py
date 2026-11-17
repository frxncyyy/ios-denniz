#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,re,hashlib
p=Path(__file__).resolve().parent;repo=p.parents[5];s=p/'shim/nord2-xnu';t=p/'tests/nord2-audio';v=p/'verification';v.mkdir(exist_ok=True);checks=[]
flags=['clang','-std=c11','-DNORD2','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all']
def run(args):
 r=subprocess.run([str(a) for a in args],cwd=repo,capture_output=True,text=True);checks.append(dict(command=[str(a) for a in args],exit=r.returncode,stdout=r.stdout,stderr=r.stderr));(v/'results.json').write_text(json.dumps(checks,indent=2));print(r.stdout if len(r.stdout)<4000 else r.stdout[:200],end='');print(r.stderr,end='');assert not r.returncode
blob=repo/'research/nord2/audio/playback-20261023/profile-sol/tfa98xx.cnt'
mods=['audio_hw','audio_afe','audio_id','audio_i2c8','audio_amp','audio_board']
# Demonstrate baseline regression against actual canonical implementation.
exe=v/'baseline-route'
run(flags+['-I'+str(repo/'shim/nord2-xnu'),'-I'+str(t),t/'test-route-plan.c',repo/'shim/nord2-xnu/audio_afe.c',repo/'shim/nord2-xnu/audio_id.c','-o',exe]);run([exe,'--expect-broken'])
for n in ['route-plan','route','inherited']:
 exe=v/n;extra=mods if n!='route-plan' else ['audio_afe','audio_id']
 run(flags+['-I'+str(s),'-I'+str(t),t/('test-'+n+'.c')]+[s/(m+'.c') for m in extra]+['-o',exe])
 if n=='inherited':
  for case in ['plan','cycles','mixed','powerup-timeout','protect-timeout','drift-audioh']:run([exe,case,blob])
 else:run([exe]+([blob] if n=='route' else []))
for n in ['audio_afe','audio_id']:
 run(['python3',repo/'tools/nord2-xnu-cc.py',repo/'research/ci-39398655023/buildtree','bsd',s/(n+'.c'),v/(n+'.arm64.o'),'-DNORD2=1','-I'+str(s)])
old=(repo/'tests/nord2-audio/actual-snapshot.inc').read_text();new=(t/'actual-snapshot.inc').read_text();assert old==new
assert [x for x in re.findall(r'\[NAU_A_.*?=.*', (s/'audio_id.c').read_text()) if 'CONN28' in x or 'CONN29' in x]==[x for x in re.findall(r'\[NAU_A_.*?=.*', (repo/'shim/nord2-xnu/audio_id.c').read_text()) if 'CONN28' in x or 'CONN29' in x]
(v/'source-sha256.json').write_text(json.dumps({str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in list(s.glob('audio_afe.*'))+list(s.glob('audio_id.*'))+list(t.glob('test-route*.c'))+[t/'route-source-oracle.h']},indent=2))
print('PASS route fix/source-oracle, baseline reproduction, rc74 lifecycle/packing, inherited cases, normal ARM64; all historical snapshot values untouched')
