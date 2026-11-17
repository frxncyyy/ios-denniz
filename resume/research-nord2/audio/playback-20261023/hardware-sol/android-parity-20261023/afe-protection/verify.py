#!/usr/bin/env python3
from pathlib import Path
import subprocess,json
p=Path(__file__).resolve().parent;repo=p.parents[6];s=p/'shim/nord2-xnu';t=p/'tests/nord2-audio';v=p/'verification';v.mkdir(exist_ok=True)
flags=['clang','-std=c11','-DNORD2','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(s),'-I'+str(t)]
mods=[s/(n+'.c') for n in ['audio_hw','audio_afe','audio_id','audio_i2c8','audio_amp','audio_board']];checks=[]
def run(args):
 r=subprocess.run([str(a) for a in args],cwd=repo,capture_output=True,text=True);checks.append(dict(command=[str(a) for a in args],exit=r.returncode,stdout=r.stdout,stderr=r.stderr));(v/'results.json').write_text(json.dumps(checks,indent=2));print(r.stdout,end='');print(r.stderr,end='');assert not r.returncode
blob=repo/'research/nord2/audio/playback-20261023/profile-sol/tfa98xx.cnt'
for name in ['protection','inherited','order','plan-dump']:
 extra=mods if name not in ['order','plan-dump'] else ([s/'audio_i2c8.c',s/'audio_board.c'] if name=='order' else [s/'audio_afe.c',s/'audio_id.c'])
 src=t/('test-'+name+'.c') if name!='plan-dump' else t/'plan-dump.c';exe=v/name
 run(flags+[src]+extra+['-o',exe])
 if name=='inherited':
  for case in ['classify','plan','cycles','mixed','refuse-rate','refuse-status','refuse-adsp','refuse-coldmux','drift-audioh','drift-adsp','powerup-timeout','protect-timeout','ampe']:run([exe,case,blob])
 else:run([exe]+([blob] if name in ['full','protection'] else []))
for name in ['audio_id','audio_afe']:
 run(['python3',repo/'tools/nord2-xnu-cc.py',repo/'research/ci-39398655023/buildtree','bsd',s/(name+'.c'),v/(name+'.arm64.o'),'-DNORD2=1','-I'+str(s)])
print('PASS protection request/ACK regression, full affected model cases, durable order tests, normal ARM64 compile')
