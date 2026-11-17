#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,json,hashlib
p=Path(__file__).resolve().parent
board=p.parents[2]/'calibration/WIFI'
assert hashlib.sha256(board.read_bytes()).hexdigest()=='0000000000000000000000000000000000000000000000000000000000000000'
with tempfile.TemporaryDirectory(prefix='nord2-normal-init-') as temp:
 cmd=['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'normal_init.c'),str(p/'test-normal-init.c'),'-o',temp+'/test']
 subprocess.run(cmd,check=True)
 r=subprocess.run([temp+'/test',str(board)],capture_output=True,text=True);(p/'test.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
 print(r.stdout.splitlines()[-1]);(p/'test-command.json').write_text(json.dumps({'argv':cmd[:-1]+['TEMP_BINARY'],'runtime_args':[str(board)],'board_sha256':hashlib.sha256(board.read_bytes()).hexdigest(),'returncode':r.returncode},indent=2)+'\n')
