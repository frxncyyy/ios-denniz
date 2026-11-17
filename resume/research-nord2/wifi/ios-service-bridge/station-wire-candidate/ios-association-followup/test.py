from pathlib import Path
import subprocess
P=Path(__file__).resolve().parent;B=P/'_build';B.mkdir(exist_ok=True)
e=subprocess.run(['python3',str(P/'prove.py')],check=True,text=True,capture_output=True)
subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(B),
 str(P/'event9.c'),str(P.parent/'ios-association/association.c'),str(P/'test.c'),'-o',str(B/'test')],check=True)
t=subprocess.run([str(B/'test')],check=True,text=True,capture_output=True)
log=e.stdout+e.stderr+t.stdout+t.stderr;(P/'verification.log').write_text(log);print(log,end='')
