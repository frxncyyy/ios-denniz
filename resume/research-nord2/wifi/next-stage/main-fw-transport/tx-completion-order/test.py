#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,json
p=Path(__file__).resolve().parent;root=p.parents[5];shim=root/'shim/nord2-xnu'
with tempfile.TemporaryDirectory(prefix='nord2-tx-ddone-') as temp:
 cmd=['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(shim),str(p/'wifi_main_transport.c'),str(shim/'wifi_main_command.c'),str(p/'test-transport.c'),'-o',temp+'/test']
 subprocess.run(cmd,check=True);r=subprocess.run([temp+'/test'],capture_output=True,text=True);(p/'test.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr;print(r.stdout.strip());(p/'test-command.json').write_text(json.dumps({'argv':cmd[:-1]+['TEMP_BINARY'],'returncode':r.returncode},indent=2)+'\n')
