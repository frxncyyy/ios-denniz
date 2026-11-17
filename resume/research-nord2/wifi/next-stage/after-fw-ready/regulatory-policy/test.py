#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,hashlib
p=Path(__file__).resolve().parent;r=p.parents[5];board=r/'research/nord2/wifi/calibration/WIFI'
assert hashlib.sha256(board.read_bytes()).hexdigest()=='0000000000000000000000000000000000000000000000000000000000000000'
with tempfile.TemporaryDirectory() as d:
 cmd=['cc','-O1','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'regulatory.c'),str(p/'test.c'),'-o',d+'/test'];subprocess.run(cmd,check=True);q=subprocess.run([d+'/test',str(board)],capture_output=True,text=True);(p/'test.log').write_text(q.stdout+q.stderr);q.check_returncode();print(q.stdout.strip())
