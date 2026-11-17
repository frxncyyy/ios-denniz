from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parent;B=P.parent
with tempfile.TemporaryDirectory(prefix='niwl-') as td:
 d=Path(td);objects=[]
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 for f in [B/'bind-transport-policy/binding_contract.c',B/'control_contract.c',B/'event-pipe/pipe.c']:
  o=d/(f.stem+'.o');subprocess.run(['clang','-std=c11',*flags,'-I'+str(B),'-c',str(f),'-o',str(o)],check=True);objects.append(o)
 exe=d/'test';subprocess.run(['clang++','-std=c++17',*flags,'-pthread',str(P/'test.cpp'),*map(str,objects),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
