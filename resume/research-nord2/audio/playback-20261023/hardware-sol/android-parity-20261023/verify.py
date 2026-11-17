#!/usr/bin/env python3
from pathlib import Path
import subprocess,json
p=Path(__file__).resolve().parent;repo=p.parents[5];v=p/'verification';v.mkdir(exist_ok=True)
c=repo/'shim/nord2-xnu';tests=repo/'tests/nord2-audio';checks=[]
def run(args):
 r=subprocess.run([str(a) for a in args],cwd=repo,capture_output=True,text=True)
 checks.append({'command':[str(a) for a in args],'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
 (v/'results.json').write_text(json.dumps(checks,indent=2));print(r.stdout,end='');print(r.stderr,end='')
 if r.returncode:raise SystemExit(r.returncode)
flags=['clang','-std=c11','-DNORD2','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(c),'-I'+str(tests)]
for name in ['order','speed']:
 exe=v/('test-'+name)
 run(flags+[p/('test-'+name+'.c'),p/'audio_i2c8.c',c/'audio_board.c','-o',exe]);run([exe])
exe=v/'test-order-old'
run(flags+[p/'test-order.c',c/'audio_i2c8.c',c/'audio_board.c','-o',exe]);run([exe,'reproduce'])
run(['python3',repo/'tools/nord2-xnu-cc.py',repo/'research/ci-39398655023/buildtree','bsd',p/'audio_i2c8.c',v/'audio_i2c8.arm64.o','-DNORD2=1','-I'+str(c)])
print('PASS private order/readback patch: Android speed oracle, ordered traces, fault regression, baseline reproduction, normal ARM64 compile')
