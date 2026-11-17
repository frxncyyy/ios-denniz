#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,json,hashlib
P=Path(__file__).resolve().parent
subprocess.run(['python3',str(P/'extract-tables.py'),'--check'],check=True)
with tempfile.TemporaryDirectory(prefix='regulatory-evaluator-') as t:
 out=Path(t)/'test'
 subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(P/'test.c'),'-o',str(out)],check=True)
 q=subprocess.run([str(out)],capture_output=True,text=True);(P/'test.log').write_text(q.stdout+q.stderr);print(q.stdout);q.check_returncode()
