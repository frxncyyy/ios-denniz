from pathlib import Path
import runpy,subprocess,tempfile
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if x.name=='ios-sweet')
runpy.run_path(str(P/'test-integrated.py'),run_name='__main__')
with tempfile.TemporaryDirectory(prefix='niwi-registry-') as t:
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(R/'shim/nord2-xnu'),str(P/'test-registry.cpp'),'-o',t+'/test'],check=True)
 subprocess.run([t+'/test'],check=True)
