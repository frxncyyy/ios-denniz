#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,subprocess,os
P=Path(__file__).resolve().parent
A=P.parent/'aks-candidate'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=A/'proposed/iokit/Kernel/arm/SweetKeyStore.cpp'
before=sha(source)
out=P/'verification';out.mkdir(exist_ok=True)
q=subprocess.run(['python3',str(A/'test-handler.py'),'--output',str(out/'host')],text=True,capture_output=True)
(out/'author-runner.log').write_text(q.stdout+q.stderr);assert q.returncode==0,q.stderr
include=out/'host/include'
base=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-DSWEET=1','-I'+str(include),'-I'+str(A/'fixtures')]
# With NORD2 absent the full preprocessed original and new C++ TU must be equal.
preprocessed=[]
for name,path in [('base',A/'SweetKeyStore.base.cpp'),('not-nord2',source)]:
 q=subprocess.run(base+['-E','-P','-x','c++',str(path)],capture_output=True,text=True);assert q.returncode==0,q.stderr
 preprocessed.append(q.stdout);(out/(name+'.preprocessed.cpp')).write_text(q.stdout)
assert preprocessed[0]==preprocessed[1]
cmd=base+['-DNORD2=1','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DAKS_SOURCE="'+str(source)+'"',str(P/'extra.cpp'),'-o',str(out/'extra')]
q=subprocess.run(cmd,capture_output=True,text=True);(out/'extra-build.log').write_text(q.stdout+q.stderr);assert q.returncode==0,q.stderr
env=os.environ.copy();env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
q=subprocess.run([str(out/'extra')],capture_output=True,text=True,env=env);(out/'extra.log').write_text(q.stdout+q.stderr);assert q.returncode==0,q.stderr
assert sha(source)==before
report={'verdict':'PASS_BOUNDED_HOST_REVIEW','tested_source_sha256':before,'nonnord2_preprocessed_equal':True,'extra':q.stdout.strip(),'author_tests':json.loads((out/'host/report.json').read_text()),'device_access':False,'canonical_mutation':False}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'source_sha256':before,'extra':q.stdout.strip(),'nonnord2_preprocessed_equal':True,'author_tests':[x['output'] for x in report['author_tests']['tests']]},indent=2))
