#!/usr/bin/env python3
"""Small tracked-test-ready wrapper; tests real production-named C.
Pass --helpers-dir shim/nord2-xnu --touch PATH after future promotion.
Harnesses are renamed frozen existing tests, with no new policy implementation.
"""
import argparse,subprocess,tempfile
from pathlib import Path
p=Path(__file__).resolve().parent
root=next(x for x in p.parents if (x/'AGENTS.md').is_file())
a=argparse.ArgumentParser();a.add_argument('--helpers-dir',type=Path,default=p);a.add_argument('--touch',type=Path,default=p/'sweet_touch.persistent.candidate.c');opts=a.parse_args()
helpers=opts.helpers_dir.resolve();common=root/'shim/nord2-xnu'
def function(s,name):
 start=s.index(name+'(');start=s.rfind('static ',0,start);brace=s.index('{',start);end=brace+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
# The PMIC harness executes the exact real function body; reject stale copy.
assert function(opts.touch.read_text(),'npa_pmic_source_enable')==function((p/'harness-pmic.c').read_text(),'npa_pmic_source_enable')
with tempfile.TemporaryDirectory(prefix='nord2-wifi-persistent-') as d:
 for case,files in [('core',['wifi_persistent.c']),('adapter',['wifi_persistent.c','wifi_persistent_adapter.c']),('pmic',[])]:
  out=Path(d)/case
  cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-I'+str(helpers),'-I'+str(common),str(p/f'harness-{case}.c')]
  cmd += [str(helpers/f) for f in files]
  if files:cmd.append(str(common/'wifi_common.c'))
  subprocess.run(cmd+['-o',str(out)],check=True)
  subprocess.run([str(out)],check=True)
print('PASS production-named real C; source-identical renamed core/native/PMIC harnesses')
