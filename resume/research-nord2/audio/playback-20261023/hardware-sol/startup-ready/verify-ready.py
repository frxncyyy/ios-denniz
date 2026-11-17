#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,re,difflib,shutil
p=Path(__file__).resolve().parent;r=p.parents[5];canon=r/'shim/nord2-xnu';v=p/'verification';v.mkdir(exist_ok=True)
s=(p/'audio_device.c').read_text()
start=s.index('static int prepare_native_deferred(');a=s.index('{',start);depth=1;end=a+1
while depth:
 if s[end]=='{':depth+=1
 elif s[end]=='}':depth-=1
 end+=1
(p/'ready-worker-helper.inc').write_text(s[start:end]+'\n')
worker=s[s.index('static void audio_worker('):]
assert worker.index('nal_initialize(')<worker.index('prepare_native_deferred(')<worker.index('nau_hw_init(')
assert worker.count('nau_hw_init(')==1
checks=[]
def run(args):
 x=subprocess.run([str(a) for a in args],cwd=r,text=True,capture_output=True)
 checks.append({'command':[str(a) for a in args],'exit':x.returncode,'stdout':x.stdout,'stderr':x.stderr})
 if x.returncode:raise RuntimeError(x.stdout+x.stderr)
 return x.stdout
try:
 exe=v/'test-ready'
 run(['clang','-std=c11','-O1','-g','-DNORD2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(p),'-I'+str(canon),p/'test-ready.c','-o',exe])
 for name in ['wait-publish','publish-failed','publish-unaligned','map-once','no-context','worker-ready','worker-timeout','worker-failed','worker-map-once']:
  run([exe,name])
 bt=r/'research/ci-39398655023/buildtree'
 run(['python3',r/'tools/nord2-xnu-cc.py',bt,'bsd',p/'audio_device.c',v/'audio_device.arm64.o','-DNORD2=1','-I'+str(p),'-I'+str(canon)])
 # Real native bridge including publication compiled under normal kernel flags.
 native=p/'compile-native-ready.c'
 native.write_text('#include <mach/mach_types.h>\n#include <mach/vm_types.h>\n#include <kern/thread.h>\n#include <kern/clock.h>\n#include <pexpert/pexpert.h>\nstatic vm_offset_t ni_gpio,ni_lt;\nstatic struct {vm_offset_t infra;} nw;\n#define NAH_NATIVE_DEFINE_PREPARE 1\n#include "audio_hw_native.inc"\n')
 run(['python3',r/'tools/nord2-xnu-cc.py',bt,'bsd',native,v/'native-ready.arm64.o','-DNORD2=1','-I'+str(p),'-I'+str(canon)])
 for name in ['audio_native.h','audio_hw_native.inc','audio_device.c']:
  before=(canon/name).read_text();after=(p/name).read_text()
  (p/(name+'.patch')).write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/shim/nord2-xnu/'+name,tofile='b/shim/nord2-xnu/'+name)))
 print('PASS9 actual-bridge/worker ASan/UBSan cases,2 normal ARM64 TUs; no MMIO/maps before publication, no hardware-init retry')
finally:(v/'results.json').write_text(json.dumps(checks,indent=2))
