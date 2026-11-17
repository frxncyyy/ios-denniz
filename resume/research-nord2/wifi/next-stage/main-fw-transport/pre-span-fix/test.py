#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,json
p=Path(__file__).resolve().parent
root=p.parents[5]
with tempfile.TemporaryDirectory(prefix='nord2-pre-span-') as temp:
 cmd=['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(root/'shim/nord2-xnu'),str(p/'test-pre-span.c'),'-Wl,--gc-sections','-o',temp+'/test']
 subprocess.run(cmd,check=True)
 r=subprocess.run([temp+'/test'],capture_output=True,text=True);(p/'test.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
 print(r.stdout.splitlines()[-1]);(p/'test-command.json').write_text(json.dumps({'argv':cmd[:-1]+['TEMP_BINARY'],'returncode':r.returncode},indent=2)+'\n')
