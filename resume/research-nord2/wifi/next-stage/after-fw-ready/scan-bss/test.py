#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,json
p=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='nord2-scan-bss-') as tmp:
 argv=['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'scan_bss.c'),str(p/'test-scan-bss.c'),'-o',tmp+'/test']
 subprocess.run(argv,check=True)
 r=subprocess.run([tmp+'/test'],capture_output=True,text=True)
 (p/'test.log').write_text(r.stdout+r.stderr)
 print(r.stdout+r.stderr,end='');assert r.returncode==0
 (p/'test-command.json').write_text(json.dumps({'argv':argv[:-1]+['TEMP_BINARY'],'returncode':r.returncode},indent=2)+'\n')
